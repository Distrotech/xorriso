
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2012 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the implementation of actions on onjects of disk
   filesystems.
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>


#include "xorriso.h"
#include "xorriso_private.h"
#include "xorrisoburn.h"



/* @param flag bit0= simple readlink(): no normalization, no multi-hop
*/
int Xorriso_resolve_link(struct XorrisO *xorriso,
                     char *link_path, char result_path[SfileadrL], int flag)
{
 ssize_t l;
 struct stat stbuf;
 int link_count= 0, ret, show_errno= 0;
 char *buf= NULL, *dirbuf= NULL, *lpt, *spt;
 static int link_limit= 100;

 Xorriso_alloc_meM(buf, char, SfileadrL);
 Xorriso_alloc_meM(dirbuf, char, SfileadrL);

 if(!(flag&1))
   if(stat(link_path, &stbuf)==-1)
     if(errno==ELOOP) {
       show_errno= errno;
       goto too_many_hops;
     }
 lpt= link_path;
 while(1) {
   l= readlink(lpt, buf, SfileadrL-1);
   if(l==-1) {
handle_error:;
     Xorriso_msgs_submit(xorriso, 0, link_path, 0, "ERRFILE", 0);
     sprintf(xorriso->info_text, "Cannot obtain link target of : ");
     Text_shellsafe(link_path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE",0);
handle_abort:;
     if(strcmp(lpt, link_path)!=0) {
       sprintf(xorriso->info_text,
               "Problem occured with intermediate path : ");
       Text_shellsafe(lpt, xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
     }
     {ret= 0; goto ex;}
   }
   buf[l]= 0;
   if(l==0) {
     Xorriso_msgs_submit(xorriso, 0, link_path, 0, "ERRFILE", 0);
     sprintf(xorriso->info_text, "Empty link target with : ");
     Text_shellsafe(link_path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
     goto handle_abort;
   }

   if(flag&1) {
     strcpy(result_path, buf);
     {ret= 1; goto ex;}
   }

   /* normalize relative to disk_path */
   if(Sfile_str(dirbuf, lpt, 0)<=0)
     {ret= -1; goto ex;}
   while(1) {
     spt= strrchr(dirbuf,'/');
     if(spt!=NULL) {
       *spt= 0;
       if(*(spt+1)!=0)
   break;
     } else
   break;
   }
   ret= Xorriso_normalize_img_path(xorriso, dirbuf, buf, result_path, 2|4);
   if(ret<=0)
     goto ex;

   if(lstat(result_path, &stbuf)==-1) {
     lpt= result_path;
     goto handle_error;
   }
   if(!S_ISLNK(stbuf.st_mode))
 break;
   
   lpt= result_path;
   link_count++;
   if(link_count>link_limit) {
too_many_hops:;
     Xorriso_msgs_submit(xorriso, 0, link_path, 0, "ERRFILE", 0);
     sprintf(xorriso->info_text, "Too many link hops with : ");
     Text_shellsafe(link_path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, show_errno,
                         "FAILURE", 0);
     {ret= 0; goto ex;}
   }
 }
 ret= 1;
ex:;
 Xorriso_free_meM(buf);
 Xorriso_free_meM(dirbuf);
 return(ret);
}


int Xorriso_convert_uidstring(struct XorrisO *xorriso, char *uid_string,
                              uid_t *uid, int flag)
{
 double num= 0.0;
 char text[80];
 struct passwd *pwd;

 sscanf(uid_string, "%lf", &num);
 sprintf(text,"%.f",num);
 if(strcmp(text,uid_string)==0) {
   *uid= num;
   return(1);
 }
 pwd= getpwnam(uid_string);
 if(pwd==NULL) {
   sprintf(xorriso->info_text, "-uid: Not a known user: '%s'", uid_string);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 *uid= pwd->pw_uid;
 return(1);
}


int Xorriso_convert_gidstring(struct XorrisO *xorriso, char *gid_string,
                              gid_t *gid, int flag)
{
 double num= 0.0;
 char text[80];
 struct group *grp;

 sscanf(gid_string, "%lf", &num);
 sprintf(text,"%.f",num);
 if(strcmp(text,gid_string)==0) {
   *gid= num;
   return(1);
 }
 grp= getgrnam(gid_string);
 if(grp==NULL) {
   sprintf(xorriso->info_text, "-gid: Not a known group: '%s'", gid_string);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 *gid= grp->gr_gid;
 return(1);
}
 

int Xorriso_convert_modstring(struct XorrisO *xorriso, char *cmd, char *mode,
                              mode_t *mode_and, mode_t *mode_or, int flag)
{
 int who_val= 0;
 char *mpt, *vpt, *opt;
 unsigned int num= 0;
 mode_t mode_val,mask;

 *mode_and= ~0;
 *mode_or= 0;
 if(mode[0]=='0') {
   *mode_and= 0;
   sscanf(mode,"%o",&num);
   *mode_or= num;
 } else if(strchr(mode,'+')!=NULL || strchr(mode,'-')!=NULL
           || strchr(mode,'=')!=NULL) {
   /* [ugoa][+-][rwxst] */;
   for(mpt= mode; mpt!=NULL; mpt= strchr(mpt, ',')) {
     if(*mpt==',')
       mpt++;
     if(strlen(mpt)<2)
       goto unrecognizable;
     who_val= 0;
     for(vpt= mpt; *vpt!='+' && *vpt!='-' && *vpt!='='; vpt++) {
       if(*vpt=='u')
         who_val|= 4;
       else if(*vpt=='g')
         who_val|= 2;
       else if(*vpt=='o')
         who_val|= 1;
       else if(*vpt=='a')
         who_val|= 7;
       else
         goto unrecognizable;
     }
     opt= vpt;
     mode_val= 0;
     for(vpt= opt+1; *vpt!=0 && *vpt!=','; vpt++) {
       if(*vpt=='r') {
         if(who_val&4)
           mode_val|= S_IRUSR;
         if(who_val&2)
           mode_val|= S_IRGRP;
         if(who_val&1)
           mode_val|= S_IROTH;
       } else if(*vpt=='w') {
         if(who_val&4)
           mode_val|= S_IWUSR;
         if(who_val&2)
           mode_val|= S_IWGRP;
         if(who_val&1)
           mode_val|= S_IWOTH;
       } else if(*vpt=='x') {
         if(who_val&4)
           mode_val|= S_IXUSR;
         if(who_val&2)
           mode_val|= S_IXGRP;
         if(who_val&1)
           mode_val|= S_IXOTH;
       } else if(*vpt=='s') {
         if(who_val&4)
           mode_val|= S_ISUID;
         if(who_val&2)
           mode_val|= S_ISGID;
       } else if(*vpt=='t') {
         if(who_val&1)
           mode_val|= S_ISVTX;
       } else
         goto unrecognizable;
     }
     if(*opt=='+') {
       (*mode_or)|= mode_val;
     } else if(*opt=='=') {
       mask= 0;
       if(who_val&1)
         mask|= S_IRWXO|S_ISVTX;
       if(who_val&2)
         mask|= S_IRWXG|S_ISGID;
       if(who_val&4)
         mask|= S_IRWXU|S_ISUID;
       (*mode_and)&= ~(mask);
       (*mode_or)= ((*mode_or) & ~mask) | mode_val;
     } else if(*opt=='-') {
       (*mode_or)&= ~mode_val;
       (*mode_and)&= ~mode_val;
     }
   }
 } else {
unrecognizable:;
   sprintf(xorriso->info_text,
           "%s: Unrecognizable or faulty permission mode ", cmd);
   Text_shellsafe(mode, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 return(1);
}


/* @param flag bit0= for Xorriso_msgs_submit: use pager
               bit1= do not issue warnings
*/
int Xorriso_hop_link(struct XorrisO *xorriso, char *link_path,
                    struct LinkiteM **link_stack, struct stat *stbuf, int flag)
{
 int ret;
 struct LinkiteM *litm;

 if(*link_stack != NULL) {
   if(Linkitem_get_link_count(*link_stack, 0) >= xorriso->follow_link_limit) {
     sprintf(xorriso->info_text,
             "Too many symbolic links in single tree branch at : ");
     Text_shellsafe(link_path, xorriso->info_text, 1);
     if(!(flag&2))
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0,"WARNING",flag&1);
     return(0);
   }
 }
 ret= stat(link_path, stbuf);
 if(ret==-1)
   return(0);
 ret= Linkitem_find(*link_stack, stbuf->st_dev, stbuf->st_ino, &litm, 0);
 if(ret>0) {
   sprintf(xorriso->info_text, "Detected symbolic link loop around : ");
   Text_shellsafe(link_path, xorriso->info_text, 1);
   if(!(flag&2))
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", flag&1);
   return(0);
 }
 ret= Linkitem_new(&litm, link_path, stbuf->st_dev, stbuf->st_ino,
                   *link_stack, 0);
 if(ret<=0) {
   sprintf(xorriso->info_text,
           "Cannot add new item to link loop prevention stack");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", flag&1);
   return(-1);
 }
 *link_stack= litm;
 return(1);
}


/* @param flag bit0= do not only sum up sizes but also print subdirs
               bit1= this is a recursion
   @return <=0 error , 1 ok , 2 could not open directory
*/
int Xorriso_show_dux_subs(struct XorrisO *xorriso,
                      char *abs_path, char *rel_path, off_t *size,
                      off_t boss_mem,
                      struct LinkiteM *link_stack,
                      int flag)
{
 int i, ret, no_sort= 0, filec= 0, l, j, fc, no_dive, is_link;
 char **filev= NULL, *namept;
 off_t sub_size, report_size, mem= 0;
 struct DirseQ *dirseq= NULL;
 struct stat stbuf;
 dev_t dir_dev;
 struct LinkiteM *own_link_stack;
 char *path= NULL, *show_path= NULL, *name= NULL, *sfe= NULL;
 
 sfe= malloc(5*SfileadrL);
 path= malloc(SfileadrL);
 show_path= malloc(SfileadrL);
 name= malloc(SfileadrL);
 if(path==NULL || show_path==NULL || name==NULL || sfe==NULL) {
   Xorriso_no_malloc_memory(xorriso, &sfe, 0);
   {ret= -1; goto ex;}
 }
 own_link_stack= link_stack;
 namept= name;
 *size= 0;

 if(lstat(abs_path, &stbuf)==-1)
   {ret= 2; goto ex;}
 dir_dev= stbuf.st_dev;
 if(S_ISLNK(stbuf.st_mode)) {
   if(!(xorriso->do_follow_links || (xorriso->do_follow_param && !(flag&2))))
     {ret= 2; goto ex;}
   if(stat(abs_path, &stbuf)==-1)
     {ret= 2; goto ex;}
   if(dir_dev != stbuf.st_dev &&
      !(xorriso->do_follow_mount || (xorriso->do_follow_param && !(flag&2))))
     {ret= 2; goto ex;}
 }
 ret= Dirseq_new(&dirseq, abs_path, 1);
 if(ret<0) {
   sprintf(xorriso->info_text, "Cannot obtain disk directory iterator");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   {ret= -1; goto ex;}
 }
 if(ret==0)
   {ret= 2; goto ex;}

 while(1) {
   Linkitem_reset_stack(&own_link_stack, link_stack, 0);
   ret= Dirseq_next_adr(dirseq,name,0);
   if(ret<0)
     goto ex;
   if(ret==0)
 break;

   sub_size= 0;
   strcpy(show_path, rel_path);
   if(Sfile_add_to_path(show_path, name, 0)<=0)
       goto much_too_long;

   strcpy(path, abs_path);
   if(Sfile_add_to_path(path, name, 0)<=0) {
much_too_long:;
     Xorriso_much_too_long(xorriso, strlen(path)+strlen(name)+1, 2);
     {ret= -1; goto ex;}
   }
   no_dive= 0;

   ret= lstat(path, &stbuf);
   if(ret==-1)
 continue;
   is_link= S_ISLNK(stbuf.st_mode);
   if(is_link && xorriso->do_follow_links) {
     ret= Xorriso_hop_link(xorriso, path, &own_link_stack, &stbuf, 1);
     if(ret<0)
       {ret= -1; goto ex;}
     if(ret!=1)
       no_dive= 1;
   }
   if(!S_ISDIR(stbuf.st_mode))
     no_dive= 1;
   if(dir_dev != stbuf.st_dev && !xorriso->do_follow_mount)
     no_dive= 1;
   if(!no_dive) {
     filec++;
     l= strlen(rel_path)+1;
     mem+= l;
     if(l % sizeof(char *))
       mem+= sizeof(char *)-(l % sizeof(char *));
     if(flag&1) /* diving and counting is done further below */
 continue;
     ret= Xorriso_show_dux_subs(xorriso, path, show_path, &sub_size, boss_mem,
                                own_link_stack,2);
     if(ret<0)
       goto ex;
     if(ret==0)
 continue;
   }
   
/*
   sub_size+= stbuf.st_size+strlen(name)+1;
*/
   sub_size+= stbuf.st_size+2048;
   if(sub_size>0)
     (*size)+= sub_size;
 }

 if(filec<=0 || !(flag&1))
   {ret= 1; goto ex;}

 /* Try to get a sorted list of directory names */
 mem+= (filec+1)*sizeof(char *);
 ret= Xorriso_check_temp_mem_limit(xorriso, mem+boss_mem, 2);
 if(ret<0)
   goto ex;
 Dirseq_rewind(dirseq, 0);
 if(ret==0) {
no_sort_possible:;
   no_sort= 1;
 } else {
   filev= (char **) calloc(filec+1, sizeof(char *));
   if(filev==NULL)
     goto no_sort_possible;
   else {
     for(i= 0; i<filec; i++)
       filev[i]= NULL;
     fc= 0;
     while(1) {
       ret= Dirseq_next_adr(dirseq,name,0);
       if(ret<0)
         goto ex;
       if(ret==0)
     break;
       strcpy(path, abs_path);
       if(Sfile_add_to_path(path, name, 0)<=0)
         goto much_too_long;

       ret= lstat(path,&stbuf);
       if(ret==-1)
     continue;
       is_link= S_ISLNK(stbuf.st_mode);
       if(is_link && xorriso->do_follow_links) {
         ret= stat(path,&stbuf);
         if(ret==-1)
     continue;
       }
       if(!S_ISDIR(stbuf.st_mode))
     continue;
       if(dir_dev != stbuf.st_dev && !xorriso->do_follow_mount)
     continue;

       if(fc>=filec) { /* Number of files changed (or programming error) */
revoke_sorting:;
         for(j=0; j<fc; j++)
           free((char *) filev[j]);
         free((char *) filev);
         filev= NULL;
         goto no_sort_possible;
       }

       filev[fc]= strdup(name);
       if(filev[fc]==NULL)
         goto revoke_sorting;
       fc++;
     }
     filec= fc;
     if(filec>1)
       Sort_argv(filec, filev, 0);
   }
 }

 for(i= 0; (no_sort || i<filec) && !(xorriso->request_to_abort); i++) {
   Linkitem_reset_stack(&own_link_stack, link_stack, 0);
   if(no_sort) {
     ret= Dirseq_next_adr(dirseq,name,0);
     if(ret<0)
       goto ex;
     if(ret==0)
 break;
   } else
     namept= filev[i];

   sub_size= 0;
   strcpy(show_path, rel_path);
   if(Sfile_add_to_path(show_path, namept, 0)<=0)
     goto much_too_long;
   strcpy(path, abs_path);
   if(Sfile_add_to_path(path, namept, 0)<=0)
     goto much_too_long;
   no_dive= 0;

   ret= lstat(path,&stbuf);
   if(ret==-1)
 continue;
   is_link= S_ISLNK(stbuf.st_mode);
   if(is_link && xorriso->do_follow_links) {
     ret= Xorriso_hop_link(xorriso, path, &own_link_stack, &stbuf, 1);
     if(ret<0)
       {ret= -1; goto ex;}
     if(ret!=1)
 continue;
   }
   if(!S_ISDIR(stbuf.st_mode))
 continue;
   if(dir_dev == stbuf.st_dev || xorriso->do_follow_mount) {
     ret= Xorriso_show_dux_subs(xorriso, path, show_path, &sub_size,
                                boss_mem+mem, own_link_stack, 2|(flag&1));
     if(ret<0)
       goto ex;
   }

/*
   sub_size+= stbuf.st_size+strlen(namept)+1;
*/
   sub_size+= stbuf.st_size+2048;
   if(sub_size>0)
     (*size)+= sub_size;
   report_size= sub_size/1024;
   if(report_size*1024<sub_size)
      report_size++;
   sprintf(xorriso->result_line, "%7.f ",(double) (report_size));
   sprintf(xorriso->result_line+strlen(xorriso->result_line), "%s\n",
           Text_shellsafe(show_path, sfe, 0));
   Xorriso_result(xorriso, 0);
 }

 ret= 1;
ex:;
 if(sfe!=NULL)
   free(sfe);
 if(path!=NULL)
   free(path);
 if(show_path!=NULL)
   free(show_path);
 if(name!=NULL)
   free(name);
 Linkitem_reset_stack(&own_link_stack, link_stack, 0);
 Dirseq_destroy(&dirseq, 0);
 if(filev!=NULL) {
   for(i=0; i<filec; i++)
     if(filev[i]!=NULL)
       free((char *) filev[i]);
   free((char *) filev);
 }
 return(ret);
}


/* @param flag   bit1= add '+' to perms
                 bit2-7: hidden_state :
                   bit2= hide in ISO/RR
                   bit3= hide in Joliet
                   bit4= hide in HFS+
*/
int Xorriso__mode_to_perms(mode_t st_mode, char perms[11], int flag)
{
 int hidden_state;

 strcpy(perms,"--------- ");
 if(st_mode&S_IRUSR) perms[0]= 'r';
 if(st_mode&S_IWUSR) perms[1]= 'w';
 if(st_mode&S_IXUSR) perms[2]= 'x';
 if(st_mode&S_ISUID) {
   if(st_mode&S_IXUSR)
     perms[2]= 's';
   else
     perms[2]= 'S';
 }
 if(st_mode&S_IRGRP) perms[3]= 'r';
 if(st_mode&S_IWGRP) perms[4]= 'w';
 if(st_mode&S_IXGRP) perms[5]= 'x';
 if(st_mode&S_ISGID) {
   if(st_mode&S_IXGRP)
     perms[5]= 's';
   else
     perms[5]= 'S';
 }
 if(st_mode&S_IROTH) perms[6]= 'r';
 if(st_mode&S_IWOTH) perms[7]= 'w';
 if(st_mode&S_IXOTH) perms[8]= 'x';
 if(st_mode&S_ISVTX) {
   if(st_mode&S_IXOTH)
     perms[8]= 't';
   else
     perms[8]= 'T';
 }

 hidden_state= (flag >> 2) & 63;
 if(hidden_state == 1)
   perms[9]= 'I';
 else if(hidden_state == 2)
   perms[9]= 'J';
 else if(hidden_state == 4)
   perms[9]= 'A';
 else if(hidden_state)
   perms[9]= 'H';
 if(flag & 2) {
   if(hidden_state)
     perms[9]= tolower(perms[9]);
   else
     perms[9]= '+';
 }
 return(1);
}


/* @param flag bit0= recognize Xorriso_IFBOOT as file type
               bit1= add '+' to perms
               bit2-7: hidden_state :
                  bit2= hide in ISO/RR
                  bit3= hide in Joliet
                  bit4= hide in HFS+
*/
int Xorriso_format_ls_l(struct XorrisO *xorriso, struct stat *stbuf, int flag)
{
 int show_major_minor= 0, high_shift= 0, high_mask= 0;
 char *rpt, perms[11], mm_text[80];
 mode_t st_mode;
 dev_t dev, major, minor;

 rpt= xorriso->result_line;
 rpt[0]= 0;
 st_mode= stbuf->st_mode;

 if(S_ISDIR(st_mode))
   strcat(rpt, "d");
 else if(S_ISREG(st_mode)) {
   strcat(rpt, "-");
 } else if(S_ISLNK(st_mode))
   strcat(rpt, "l");
 else if(S_ISBLK(st_mode)) {
   strcat(rpt, "b");
   show_major_minor= 1;
 } else if(S_ISCHR(st_mode)) {
   strcat(rpt, "c");
   show_major_minor= 1;
 } else if(S_ISFIFO(st_mode))
   strcat(rpt, "p");
 else if(S_ISSOCK(st_mode))
   strcat(rpt, "s");
 else if((flag & 1) && (st_mode & S_IFMT) == Xorriso_IFBOOT)
   strcat(rpt, "e");
 else
   strcat(rpt, "?");

 Xorriso__mode_to_perms(st_mode, perms, flag & (2 | 252));
 strcat(rpt, perms);

 sprintf(rpt+strlen(rpt)," %3u ",(unsigned int) stbuf->st_nlink);

 sprintf(rpt+strlen(rpt), "%-8lu ", (unsigned long) stbuf->st_uid);
 sprintf(rpt+strlen(rpt), "%-8lu ", (unsigned long) stbuf->st_gid);
 if(show_major_minor) {
   dev= stbuf->st_rdev;

   /* according to /usr/include/sys/sysmacros.h : gnu_dev_major(),_minor() 
      >>> but this looks as if it should go to some system dependency
      >>> in FreeBSD dev_t is 32 bit
   */
   if(sizeof(dev_t) > 4) {
     high_shift= 32;
     high_mask= ~0xfff;
   }
   major= (((dev >> 8) & 0xfff) |
           ((unsigned int) (dev >> high_shift) & high_mask))
          & 0xffffffff;
   minor= (((dev & 0xff) | ((unsigned int) (dev >> 12) & ~0xff))) & 0xffffffff;

   sprintf(mm_text, "%u,%u", (unsigned int) major, (unsigned int) minor);
   sprintf(rpt+strlen(rpt), "%8s ", mm_text);
 } else
   sprintf(rpt+strlen(rpt), "%8.f ", (double) stbuf->st_size);

 Ftimetxt(stbuf->st_mtime, rpt+strlen(rpt), 0);
 strcat(rpt, " ");

 return(1);
}


struct DirentrY {
 char *adr;
 struct DirentrY *next;
};


int Xorriso_sorted_dir_x(struct XorrisO *xorriso, char *dir_path,
                         int *filec, char ***filev, off_t boss_mem, int flag)
{
 int count= 0, ret;
 char *name= NULL;
 struct DirseQ *dirseq= NULL;
 off_t mem;
 struct DirentrY *last= NULL, *current= NULL;

 Xorriso_alloc_meM(name, char, SfileadrL);

 *filec= 0;
 *filev= NULL;
 mem= boss_mem;
 ret= Dirseq_new(&dirseq, dir_path, 1);
 if(ret<=0)
   goto ex;
 while(1) { /* loop over directory content */
   ret= Dirseq_next_adr(dirseq,name,0);
   if(ret==0)
 break;
   if(ret<0)
     goto ex;
   mem+= strlen(name)+8+sizeof(struct DirentrY)+sizeof(char *);
   if(mem>xorriso->temp_mem_limit)
     {ret= 0; goto ex;}

   current= (struct DirentrY *) calloc(1, sizeof(struct DirentrY));
   if(current==NULL) {
     Xorriso_no_malloc_memory(xorriso, NULL, 0);
     {ret= -1; goto ex;}
   }
   current->adr= NULL;
   current->next= last;
   last= current;
   last->adr= strdup(name);
   if(last->adr==NULL) {
     Xorriso_no_malloc_memory(xorriso, NULL, 0);
     {ret= -1; goto ex;}
   }
   count++;
 }
 *filec= count;
 if(count==0)
   {ret= 1; goto ex;}
 (*filev)= (char **) calloc(count, sizeof(char *));
 if(*filev==NULL) {
   Xorriso_no_malloc_memory(xorriso, NULL, 0);
   {ret= -1; goto ex; }
 }
 count= 0;
 for(current= last; current!=NULL; current= last) {
   last= current->next;
   (*filev)[count++]= current->adr;
   free((char *) current);
 }
 Sort_argv(*filec, *filev, 0);
 ret= 1; 
ex:;
 for(current= last; current!=NULL; current= last) {
   last= current->next;
   free(current->adr);
   free((char *) current);
 }
 Xorriso_free_meM(name);
 Dirseq_destroy(&dirseq, 0);
 return(ret);
}


/* @param flag bit0= long format
               bit1= do not print count of nodes
               bit2= du format
               bit3= print directories as themselves (ls -d)
*/
int Xorriso_lsx_filev(struct XorrisO *xorriso, char *wd,
                      int filec, char **filev, off_t boss_mem, int flag)
{
 int i, ret, was_error= 0, dfilec= 0, pass, passes;
 char *path= NULL, *acl_text= NULL;
 char *rpt, *link_target= NULL, **dfilev= NULL;
 off_t size;
 struct stat stbuf;

 Xorriso_alloc_meM(path, char, SfileadrL);
 Xorriso_alloc_meM(link_target, char, SfileadrL);

 rpt= xorriso->result_line;

 Sort_argv(filec, filev, 0);

 /* Count valid nodes, warn of invalid ones */
 for(i= 0; i<filec; i++) {
   ret= Xorriso_make_abs_adr(xorriso, wd, filev[i], path, 1|2|4);
   if(ret<=0) {
     was_error++;
 continue;
   }
   ret= lstat(path, &stbuf);
   if(ret==-1) {
     sprintf(xorriso->info_text, "Not found in local filesystem: ");
     Text_shellsafe(path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 1);
     was_error++;
 continue;
   }
 }

 if((flag&8) && !(flag&(2|4))) {
   sprintf(xorriso->info_text,"Valid local files found: %d\n",filec-was_error);
   Xorriso_info(xorriso,1);
   if(filec-was_error<=0)
     {ret= !was_error; goto ex;}
 }

 passes= 1+!(flag&(4|8));
 for(pass= 0; pass<passes; pass++)
 for(i= 0; i<filec && !(xorriso->request_to_abort); i++) {
   ret= Xorriso_make_abs_adr(xorriso, wd, filev[i], path, 1|2|4);
   if(ret<=0)
 continue;
   ret= lstat(path, &stbuf);
   if(ret==-1)
 continue;
   if(S_ISLNK(stbuf.st_mode) &&
      (xorriso->do_follow_links || xorriso->do_follow_param)) {
     ret= stat(path, &stbuf);
     if(ret==-1) 
       ret= lstat(path, &stbuf);
     if(ret==-1)
 continue;
   }
   if(S_ISDIR(stbuf.st_mode) && !(flag&(4|8))) {
     if(pass==0)
 continue;
     if(filec>1) {
       strcpy(xorriso->result_line, "\n");
       Xorriso_result(xorriso,0);
       Text_shellsafe(filev[i], xorriso->result_line, 0);
       strcat(xorriso->result_line, ":\n");
       Xorriso_result(xorriso,0);
     }
     ret= Xorriso_sorted_dir_x(xorriso, path, &dfilec, &dfilev, boss_mem, 0);
     if(ret<=0) {

       /* >>> DirseQ loop and single item Xorriso_lsx_filev() */;

     } else {
       if(flag&1) {
         sprintf(xorriso->result_line, "total %d\n", dfilec);
         Xorriso_result(xorriso,0);
       }
       Xorriso_lsx_filev(xorriso, path,
                         dfilec, dfilev, boss_mem, (flag&1)|2|8);
     }
     if(dfilec>0)
       Sfile_destroy_argv(&dfilec, &dfilev, 0);
 continue;
   } else
     if(pass>0)
 continue;
   link_target[0]= 0;
   rpt[0]= 0;
   if((flag&5)==1) {
     Xorriso_local_getfacl(xorriso, path, &acl_text, 16);
     ret= Xorriso_format_ls_l(xorriso, &stbuf, (acl_text != NULL) << 1);
     Xorriso_local_getfacl(xorriso, path, &acl_text, 1 << 15);
     if(ret<=0)
 continue;
     if(S_ISLNK(stbuf.st_mode)) {
       ret= Xorriso_resolve_link(xorriso, path, link_target, 1);
       if(ret<=0)
         link_target[0]= 0;
     }
   } else if(flag&4) { /* -du or -dus */
     size= stbuf.st_size;
     if(S_ISDIR(stbuf.st_mode)) {
       ret= Xorriso_show_dux_subs(xorriso, path, filev[i], &size, boss_mem,
                                  NULL, flag&1);
       if(ret<0)
         {ret= -1; goto ex;}
       if(ret==0)
 continue;
     }
     sprintf(rpt, "%7.f ",(double) (size/1024));
   }
   if(link_target[0]) {
     Text_shellsafe(filev[i], xorriso->result_line, 1);
     strcat(xorriso->result_line, " -> ");
     Text_shellsafe(link_target, xorriso->result_line, 1 | 2);
   } else {
     Text_shellsafe(filev[i], xorriso->result_line, 1);
   }
   strcat(xorriso->result_line, "\n");
   Xorriso_result(xorriso, 0);
 }
 ret= !was_error;
ex:;
 Xorriso_free_meM(path);
 Xorriso_free_meM(link_target);
 return(ret);
}


/*
   @param flag >>> bit0= remove whole sub tree: rm -r
               bit1= remove empty directory: rmdir  
               bit2= recursion: do not reassure in mode 2 "tree"
               bit3= this is for overwriting and not for plain removal
               bit4= count deleted files in xorriso->pacifier_count
               bit5= with bit0 only remove directory content, not the directory
               bit6= permission to call Xorriso_make_accessible()
   @return   <=0 = error
               1 = removed leaf file object
               2 = removed directory or tree
               3 = did not remove on user revocation
*/
int Xorriso_rmx(struct XorrisO *xorriso, off_t boss_mem, char *path, int flag)
{
 int ret, is_dir= 0, made_accessible= 0;
 struct stat victim_stbuf;
 struct DirseQ *dirseq= NULL;
 char *sfe= NULL, *sub_path= NULL;
 struct PermiteM *perm_stack_mem;

 perm_stack_mem= xorriso->perm_stack;

 /* Avoiding large local memory objects in order to save stack space */
 sfe= malloc(5*SfileadrL);
 sub_path= malloc(2*SfileadrL);
 if(sfe==NULL || sub_path==NULL) {
   Xorriso_no_malloc_memory(xorriso, &sfe, 0);
   {ret= -1; goto ex;}
 }

 if(Xorriso_much_too_long(xorriso, strlen(path), 0)<=0)
   {ret= 0; goto ex;}

 ret= lstat(path, &victim_stbuf);
 if(ret==-1) {
   if((flag & 64) && errno == EACCES) {
     ret= Xorriso_make_accessible(xorriso, path, 0);
     if(ret < 0)
       goto ex;
     made_accessible= 1;
     ret= lstat(path, &victim_stbuf);
   }
   if(ret==-1) {
     sprintf(xorriso->info_text, "Cannot lstat(%s)",
             Text_shellsafe(path, sfe, 0));
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
 }
 if(strcmp(path, "/")==0) {
   sprintf(xorriso->info_text, "May not delete root directory");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 if(S_ISDIR(victim_stbuf.st_mode))
   is_dir= 1;
 if(!is_dir) {
   if(flag&2) { /* rmdir */
     sprintf(xorriso->info_text, "%s in disk filesystem is not a directory",
             Text_shellsafe(path, sfe, 0));
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }
 } else {
   if(flag&1) { /* rm -r */

#ifdef Osirrox_not_yeT
     /* >>> */
    
     struct stat *victim_node= NULL;

     victim_node= &victim_stbuf;

     if((xorriso->do_reassure==1 && !xorriso->request_not_to_ask) ||
        (flag&32)) {
       /* Iterate over subordinates and delete them */
       mem= boss_mem;

       ret= Xorriso_findi_iter(xorriso, (IsoDir *) victim_node, &mem,
                         &iter, &node_array, &node_count, &node_idx,
                         &node, 1|2);
       if(ret<=0) {
cannot_create_iter:;
         Xorriso_cannot_create_iter(xorriso, ret, 0);
         ret= -1; goto ex;
       }
       pl= strlen(path);
       strcpy(sub_path, path);
       if(pl==0 || sub_path[pl-1]!='/') {
         sub_path[pl++]= '/';
         sub_path[pl]= 0;
       }
       sub_name= sub_path+pl;
       while(1) { 
         ret= Xorriso_findi_iter(xorriso, (IsoDir *) victim_node, &mem, &iter,
                                &node_array, &node_count, &node_idx, &node, 0);
         if(ret<0)
           goto ex;
         if(ret==0 || xorriso->request_to_abort)
       break;
         name= (char *) iso_node_get_name(node);
         if(Xorriso_much_too_long(xorriso, pl+1+strlen(name), 0)<=0)
           {ret= 0; goto rm_r_problem_handler;}
         strcpy(sub_name, name);
         ret= Xorriso_rmi(xorriso, iter, mem, sub_path, (flag&(1|2|8|16))|4);
         if(ret==3 || ret<=0 || xorriso->request_to_abort) {
rm_r_problem_handler:;
           not_removed= 1;
           fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
           if(fret<0)
             goto dir_not_removed;
         }
       }
       if(flag&32)
         {ret= 2; goto ex;}

       if(not_removed) {
dir_not_removed:;
         sprintf(xorriso->info_text, "Directory not removed: %s",
                 Text_shellsafe(path, sfe, 0));
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
         if(ret>0)
           ret= 3;
         goto ex;
       }
     }

#else /* Osirrox_not_yeT */

       sprintf(xorriso->info_text, "-rm_rx is not implemented yet");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;

#endif /* !Osirrox_not_yeT */

   } else {
     if(!(flag&2)) { /* not rmdir */
       sprintf(xorriso->info_text, "%s in disk filesystem is a directory",
               Text_shellsafe(path, sfe, 0));
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
     }
     ret= Dirseq_new(&dirseq, path, 1);
     if(ret>0) {
       ret= Dirseq_next_adr(dirseq, sfe, 0);
       if(ret>0) {
         sprintf(xorriso->info_text,
                 "Directory not empty on attempt to delete: %s",
                 Text_shellsafe(path, sfe, 0));
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         ret= 0; goto ex;
       }
     }
   }
 }
 if(xorriso->request_to_abort)
   {ret= 3; goto ex;}
 ret= Xorriso_reassure_restore(xorriso, path, (flag&(4|8)) | !!is_dir);
 if(ret<=0 || ret==3)
   goto ex;
 if(is_dir)
   ret= rmdir(path);
 else
   ret= unlink(path);
 if(ret == -1) {
   if((flag & 64) && errno == EACCES && !made_accessible) {
     ret= Xorriso_make_accessible(xorriso, path, 0);
     if(ret < 0)
       goto ex;
     made_accessible= 1;
     if(is_dir)
       ret= rmdir(path);
     else
       ret= unlink(path);
   }
   if(ret == -1) {
     sprintf(xorriso->info_text, "Cannot delete from disk filesystem %s",
             Text_shellsafe(path, sfe, 0));
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
     ret= -1; goto ex;
   }
 }
 if(flag&16)
   xorriso->pacifier_count++;
 ret= 1+!!is_dir;
ex:;
 if(made_accessible)
   Permstack_pop(&(xorriso->perm_stack), perm_stack_mem, xorriso, 0);
 if(sfe!=NULL)
   free(sfe);
 if(sub_path!=NULL)
   free(sub_path);
 Dirseq_destroy(&dirseq, 0);
 return(ret);
} 


/* @param flag bit0= recursion
*/ 
int Xorriso_findx_action(struct XorrisO *xorriso, struct FindjoB *job,
                         char *abs_path, char *show_path, int depth, int flag)
{
 int ret= 0, type, action= 0, dpl= 0, compare_result, uret;
 uid_t user= 0;
 gid_t group= 0;
 time_t date= 0;
 mode_t mode_or= 0, mode_and= ~1;
 char *target, *text_2, *wdi_mem= NULL, *disk_prefix, *iso_path= NULL;
 char *basename;
 struct FindjoB *subjob;
 struct stat stbuf; 

 Xorriso_alloc_meM(iso_path, char, SfileadrL);

 action= Findjob_get_action_parms(job, &target, &text_2, &user, &group,
                                &mode_and, &mode_or, &type, &date, &subjob, 0);
 if(action<0)
   action= 0;
 if(action<0)
   action= 0;
 if(action==15 || action==16 || action==18 || action==19 || action==20) {
   /* in_iso , not_in_iso, add_missing , empty_iso_dir , is_full_in_iso */
   Findjob_get_start_path(job, &disk_prefix, 0);
   if(strncmp(abs_path, disk_prefix, strlen(disk_prefix))!=0)
     {ret= -1; goto ex;}
   dpl= strlen(disk_prefix);
   if(strlen(target)+strlen(abs_path)-dpl >= SfileadrL)
     {ret= -1; goto ex;}
   if(abs_path[dpl]=='/')
     dpl++;
   ret= Xorriso_make_abs_adr(xorriso, target, abs_path+dpl, iso_path, 4);
   if(ret<=0)
     {goto ex;}

 }
 if(action==15) { /* in_iso */
   ret= Xorriso_iso_lstat(xorriso, iso_path, &stbuf, 0);
   if(ret<0)
     {ret= 1; goto ex;}
   Text_shellsafe(show_path, xorriso->result_line, 0);
   strcat(xorriso->result_line, "\n");
   Xorriso_result(xorriso, 0);
   ret= 1;
 } else if(action==16) { /* not_in_iso */
   ret= Xorriso_compare_2_files(xorriso, abs_path, iso_path, abs_path+dpl,
                                &compare_result, 4);
   if(ret<xorriso->find_compare_result)
     xorriso->find_compare_result= ret;
   if(ret>=0)
     ret= 1;
 } else if(action==18) { /* add_missing */
   ret= Xorriso_compare_2_files(xorriso, abs_path, iso_path, abs_path+dpl,
                                &compare_result, 4|(1<<31));
   if(ret<xorriso->find_compare_result)
     xorriso->find_compare_result= ret;
   if(ret==0) {
     uret= Xorriso_update_interpreter(xorriso, NULL, NULL, compare_result,
                                      abs_path, iso_path, ((flag&1)<<2) | 2);
     if(uret<=0)
       ret= 0;
   }
   if(ret>=0)
     ret= 1;
 } else if(action==19) { /* empty_iso_dir */
   ret= Xorriso_iso_lstat(xorriso, iso_path, &stbuf, 0);
   if(ret<0)
     {ret= 1; goto ex;}
   if(!S_ISDIR(stbuf.st_mode))
     {ret= 1; goto ex;}
   ret= Xorriso_rmi(xorriso, NULL, (off_t) 0, iso_path, 1|32);
   if(ret>0) {
     sprintf(xorriso->info_text, "Emptied directory ");
     Text_shellsafe(iso_path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "UPDATE", 0);
   }
 } else if(action==20) { /* is_full_in_iso */
   ret= Xorriso_iso_lstat(xorriso, iso_path, &stbuf, 0);
   if(ret<0)
     {ret= 1; goto ex;}
   if(!S_ISDIR(stbuf.st_mode))
     {ret= 1; goto ex;}
   wdi_mem= strdup(xorriso->wdi);
   if(wdi_mem == NULL) {
     Xorriso_no_malloc_memory(xorriso, NULL, 0);
     {ret= -1; goto ex;}
   }
   strcpy(xorriso->wdi, iso_path);
   ret= Xorriso_ls(xorriso, 4|8);
   strcpy(xorriso->wdi, wdi_mem);
   if(ret>0) {
     strcpy(xorriso->result_line, "d ");
     Text_shellsafe(iso_path, xorriso->result_line, 1);
     strcat(xorriso->result_line,
            " (ISO) : non-empty directory (would not match mount point)\n");
     Xorriso_result(xorriso,0);
   }
   {ret= 1; goto ex;}
 } else if(action == 40) { /* estimate_size */
   basename= strrchr(abs_path, '/');
   if(basename != NULL)
     basename++;
   else
     basename= abs_path;
   ret= lstat(abs_path, &stbuf);
   if(ret != -1)
     ret= Xorriso_estimate_file_size(xorriso, job, basename, stbuf.st_mode,
                                     stbuf.st_size, 0);
 } else if(action == 44) { /* list_extattr */
   ret= Xorriso_list_extattr(xorriso, NULL, abs_path, show_path, target, 2);
 } else {
   Text_shellsafe(show_path, xorriso->result_line, 0);
   strcat(xorriso->result_line, "\n");
   Xorriso_result(xorriso, 0);
   ret= 1;
 } 
ex:;
 if(action==15 || action==16 || action==18 || action==19 || action==20)
   if(xorriso->no_volset_present)
     xorriso->request_to_abort= 1; /* Need an image. No use to try again. */
 if(wdi_mem != NULL)
   free(wdi_mem);
 Xorriso_free_meM(iso_path);
 return(ret);
}


/* @param flag bit0=recursion
*/
int Xorriso_findx(struct XorrisO *xorriso, struct FindjoB *job,
                  char *abs_dir_parm, char *dir_path,
                  struct stat *dir_stbuf, int depth,
                  struct LinkiteM *link_stack, int flag)
{
 int ret,is_link, no_dive;
 struct DirseQ *dirseq= NULL;
 struct stat stbuf;
 struct LinkiteM *own_link_stack;
 char *abs_dir_path, *namept;
 char *name= NULL, *path= NULL, *sfe= NULL;
 char *abs_dir_path_data= NULL, *abs_path= NULL;

 if(xorriso->request_to_abort)
   {ret= 0; goto ex;}

 Xorriso_alloc_meM(sfe, char, 5*SfileadrL);
 Xorriso_alloc_meM(name, char, SfileadrL);
 Xorriso_alloc_meM(path, char, SfileadrL);
 Xorriso_alloc_meM(abs_dir_path_data, char, SfileadrL);
 Xorriso_alloc_meM(abs_path, char, SfileadrL);

 own_link_stack= link_stack;
 abs_dir_path= abs_dir_parm;
 if(abs_dir_path[0]==0) {
   ret= Xorriso_make_abs_adr(xorriso, xorriso->wdx, dir_path,
                             abs_dir_path_data, 1|2|8);
   if(ret<=0)
     goto ex;
   abs_dir_path= abs_dir_path_data;
   ret= Xorriso_path_is_excluded(xorriso, abs_dir_path, !(flag&1));
   if(ret<0)
     goto ex;
   if(ret>0)
     {ret= 0; goto ex;}
   ret= lstat(abs_dir_path, dir_stbuf);
   if(ret==-1)
     {ret= 0; goto ex;}
   if(S_ISLNK(dir_stbuf->st_mode) &&
      (xorriso->do_follow_links || (xorriso->do_follow_param && !(flag&1))))
     if(stat(abs_dir_path, &stbuf)!=-1)
       if(dir_stbuf->st_dev == stbuf.st_dev ||
         (xorriso->do_follow_mount || (xorriso->do_follow_param && !(flag&1))))
         memcpy(dir_stbuf, &stbuf, sizeof(struct stat));

   namept= strrchr(dir_path, '/');
   if(namept==NULL)
     namept= dir_path;
   else
     namept++;

   ret= Findjob_test_2(xorriso, job, NULL, namept, dir_path, NULL, dir_stbuf,
                       0);
   if(ret<0)
     goto ex;
   if(ret>0) {
     ret= Xorriso_findx_action(xorriso, job, abs_dir_path, dir_path, depth,
                               flag&1);
     if(xorriso->request_to_abort)
       {ret= 0; goto ex;}
     if(ret<=0) {
       if(Xorriso_eval_problem_status(xorriso, ret, 1|2)<0)
         goto ex;
     }
   }
 }
 if(xorriso->request_to_abort)
   {ret= 1; goto ex;}
 if(!S_ISDIR(dir_stbuf->st_mode))
   {ret= 2; goto ex;}

 ret= Dirseq_new(&dirseq, abs_dir_path, 1);
 if(ret<0) {
   sprintf(xorriso->info_text, "Cannot obtain disk directory iterator");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   {ret= -1; goto ex;}
 }
 if(ret==0)
   {ret= 2; goto ex;}
 
 while(!xorriso->request_to_abort) {
   Linkitem_reset_stack(&own_link_stack, link_stack, 0);
   ret= Dirseq_next_adr(dirseq,name,0);
   if(ret==0)
 break;
   if(ret<0) {
     sprintf(xorriso->info_text,"Failed to obtain next directory entry");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
     {ret= -1; goto ex;}
   }
   ret= Xorriso_make_abs_adr(xorriso, abs_dir_path, name, abs_path, 1);
   if(ret<=0)
     goto ex;
   ret= Xorriso_make_abs_adr(xorriso, dir_path, name, path, 4);
   if(ret<=0)
     goto ex;
   ret= Xorriso_path_is_excluded(xorriso, abs_path, 0); /* (is never param) */
   if(ret<0)
     goto ex;
   if(ret>0)
 continue;
   ret= lstat(abs_path, &stbuf);
   if(ret==-1)
 continue;
   no_dive= 0;

   is_link= S_ISLNK(stbuf.st_mode);
   if(is_link && xorriso->do_follow_links) {
     ret= Xorriso_hop_link(xorriso, abs_path, &own_link_stack, &stbuf, 2);
     if(ret<0)
       {ret= -1; goto ex;}
     if(ret!=1)
       no_dive= 1;
   }

   ret= Findjob_test_2(xorriso, job, NULL, name, path, dir_stbuf, &stbuf, 0);
   if(ret<0)
     goto ex;
   if(ret>0) {
     ret= Xorriso_findx_action(xorriso, job, abs_path, path, depth, flag&1);
     if(xorriso->request_to_abort)
       {ret= 0; goto ex;}
     if(ret<=0) {
       if(Xorriso_eval_problem_status(xorriso, ret, 1|2)<0)
         goto ex;
     }
   }
   if(!S_ISDIR(stbuf.st_mode))
     no_dive= 1;
   if(dir_stbuf->st_dev != stbuf.st_dev && !xorriso->do_follow_mount)
     no_dive= 1;
   if(!no_dive) {
     ret= Xorriso_findx(xorriso, job, abs_path, path, &stbuf, depth+1,
                        own_link_stack, flag|1);
     if(ret<0)
       goto ex;
   }
 }

 ret= 1;
ex:;
 Xorriso_free_meM(sfe);
 Xorriso_free_meM(name);
 Xorriso_free_meM(path);
 Xorriso_free_meM(abs_dir_path_data);
 Xorriso_free_meM(abs_path);
 Dirseq_destroy(&dirseq, 0);
 return(ret);
}


/* @param flag bit0= no hardlink reconstruction
               bit1= do not set xorriso->node_*_prefixes
               bit5= -extract_single: eventually do not insert directory tree
*/
int Xorriso_restore_sorted(struct XorrisO *xorriso, int count,
                           char **src_array, char **tgt_array,
                           int *problem_count, int flag)
{
 int i, ret, with_node_array= 0, hflag= 0, hret;

 *problem_count= 0;
 if(!(((xorriso->ino_behavior & 16) && xorriso->do_restore_sort_lba) ||
      (xorriso->ino_behavior & 4) || (flag & 1))) {
   ret= Xorriso_make_hln_array(xorriso, 0);
   if(ret<=0)
     goto ex;
 }
 if(xorriso->do_restore_sort_lba) {
   /* Count affected nodes */
   Xorriso_destroy_node_array(xorriso, 0);
   for(i= 0; i < count; i++) {
     if(src_array[i] == NULL || tgt_array[i] == NULL)
   continue;
     /* sort_lba : Make directories plus node_array and then
                   run array extractor (with eventual hardlink detection)
     */
     hflag= (1 << 7) | ((!!(flag & 2)) << 9) | (flag & 32);
     ret= Xorriso_restore(xorriso, src_array[i], tgt_array[i],
                          (off_t) 0, (off_t) 0, hflag);
     if(ret <= 0) {
       (*problem_count)++;
       hret= Xorriso_eval_problem_status(xorriso, ret, 1 | 2);
       if(hret < 0)
         goto ex;
     }
     with_node_array= 1;
   }
 }

 if(with_node_array) {
   /* Allocate and fill node array */
   if(xorriso->node_counter <= 0)
     {ret= 2; goto ex;}
   ret= Xorriso_new_node_array(xorriso, xorriso->temp_mem_limit, 0,
                               !xorriso->do_restore_sort_lba);
   if(ret<=0)
     goto ex;
   for(i= 0; i < count; i++) {
     if(src_array[i] == NULL || tgt_array[i] == NULL)
   continue;
     ret= Xorriso_restore(xorriso, src_array[i], tgt_array[i],
                          (off_t) 0, (off_t) 0, (2 << 7) | (flag & 32));
     if(ret <= 0) {
       (*problem_count)++;
       hret= Xorriso_eval_problem_status(xorriso, ret, 1 | 2);
       if(hret < 0)
         goto ex;
     }
   }
 }

 /* Perform restore operations */
 if(xorriso->do_restore_sort_lba) {
   ret= Xorriso_restore_node_array(xorriso, 0);
   if(ret <= 0)
     goto ex;
 } else {
   for(i= 0; i < count; i++) {
     if(src_array[i] == NULL || tgt_array[i] == NULL)
   continue;
     ret= Xorriso_restore(xorriso, src_array[i], tgt_array[i],
                          (off_t) 0, (off_t) 0, flag & 32);
     if(ret <= 0) {
       (*problem_count)++;
       hret= Xorriso_eval_problem_status(xorriso, ret, 1 | 2);
       if(hret < 0)
         goto ex;
     }
   }
 }

 ret= 1;
ex:;
 return(ret);
}


/* @param flag bit0= path is a directory
               bit2= recursion: do not reassure in mode 2 "tree"
               bit3= this is for overwriting and not for plain removal
*/
int Xorriso_reassure_restore(struct XorrisO *xorriso, char *path, int flag)
{
 int ret;

 while((xorriso->do_reassure==1 || (xorriso->do_reassure==2 && !(flag&4)))
       && !xorriso->request_not_to_ask) {
   /* ls -ld */
   Xorriso_lsx_filev(xorriso, xorriso->wdx, 1, &path, (off_t) 0, 1|2|8);
   if(flag&1) /* du -s */
     Xorriso_lsx_filev(xorriso, xorriso->wdx, 1, &path, (off_t) 0, 2|4);
   if(flag&8)
     sprintf(xorriso->info_text,
  "File exists. Remove ?  n= keep old, y= remove, x= abort, @= stop asking\n");
   else
     sprintf(xorriso->info_text,
  "Remove above file ?  n= keep it, y= remove it, x= abort, @= stop asking\n");
   Xorriso_info(xorriso, 4);
   ret= Xorriso_request_confirmation(xorriso, 1|2|4|16);
   if(ret<=0)
     goto ex;
   if(xorriso->request_to_abort) {
     sprintf(xorriso->info_text,
             "Removal operation aborted by user before file: ");
     Text_shellsafe(path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
     ret= 3; goto ex;
   }
   if(ret==3)
 continue;
   if(ret==6) /* yes */
 break;
   if(ret==4) { /* yes, do not ask again */
     xorriso->request_not_to_ask= 1;
 break;
   }
   if(ret==1) { /* no */
     sprintf(xorriso->info_text, "Kept in existing state: ");
     Text_shellsafe(path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
     ret= 3; goto ex;
   }
 }
 ret= 1;
ex:
 return(ret);
}


/* @param flag bit7= return 4 if restore fails from denied permission
                     do not issue error message
   @return <=0 failure , 1 success ,
           4 with bit7: permission to  create file was denied
*/
int Xorriso_make_tmp_path(struct XorrisO *xorriso, char *orig_path,
                          char *tmp_path, int *fd, int flag)
{
 char *cpt;

 cpt= strrchr(orig_path, '/');
 if(cpt==NULL)
   tmp_path[0]= 0;
 else {
   strncpy(tmp_path, orig_path, cpt+1-orig_path);
   tmp_path[cpt+1-orig_path]= 0;
 }
 strcat(tmp_path, "_tmp_xorriso_restore_XXXXXX");
 *fd= mkstemp(tmp_path);
 if(*fd==-1) {
   if(errno == EACCES && (flag & 128))
     return(4);
   strcpy(xorriso->info_text, "Cannot create temporary file : ");
   Text_shellsafe(tmp_path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
   return(0);
 }
 fchmod(*fd, S_IRUSR|S_IWUSR);
 return(1);
}


/* @param flag bit0= change regardless of xorriso->do_auto_chmod
               bit1= desired is only rx
   @return -1=severe error , -2= cannot chmod, 0= nothing to do, 1 = chmoded
*/
int Xorriso_auto_chmod(struct XorrisO *xorriso, char *disk_path, int flag)
{
 int ret, is_link= 0;
 char *path_pt, *link_target= NULL;
 mode_t mode, desired= S_IRUSR | S_IWUSR | S_IXUSR;
 struct stat stbuf;

 Xorriso_alloc_meM(link_target, char, SfileadrL);

 if(!(xorriso->do_auto_chmod || (flag & 1)))
   {ret= 0; goto ex;}

 if(flag & 2)
   desired &= ~S_IWUSR;
 path_pt= disk_path;
 ret= lstat(path_pt, &stbuf);
 if(ret==-1)
   {ret= 0; goto ex;}
 if(S_ISLNK(stbuf.st_mode)) {
   is_link= 1;
   ret= stat(path_pt, &stbuf);
   if(ret==-1)
     {ret= 0; goto ex;}
 }
 if(!S_ISDIR(stbuf.st_mode))
   {ret= 0; goto ex;}
 if(is_link) {
   ret= Xorriso_resolve_link(xorriso, path_pt, link_target, 0);
   if(ret<=0)
     goto ex;
   path_pt= link_target;
 }
 if((stbuf.st_mode & desired) == desired)
   {ret= 0; goto ex;}
 if(stbuf.st_uid!=geteuid())
   {ret= -2; goto ex;}

 mode= (stbuf.st_mode | desired) & 07777;
 ret= chmod(path_pt, mode);
 if(ret==-1) {
   sprintf(xorriso->info_text,
           "Cannot change access permissions of disk directory: chmod %o ",
           (unsigned int) (mode & 07777));
   Text_shellsafe(path_pt, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "SORRY", 0);
   {ret= -2; goto ex;}
 }
 ret= Permstack_push(&(xorriso->perm_stack), path_pt, &stbuf, 0);
 if(ret<=0)
   goto ex;
 ret= 1;
ex:;
 Xorriso_free_meM(link_target);
 return(ret);
}


int Xorriso_make_accessible(struct XorrisO *xorriso, char *disk_path, int flag)
{
 int done= 0, ret, just_rx= 2;
 char *npt, *apt, *path, *wpt;

 Xorriso_alloc_meM(path, char, SfileadrL); 

 apt= disk_path;
 wpt= path;
 for(npt= apt; !done; apt= npt + 1) {
   npt= strchr(apt, '/');
   if(npt == NULL)
 break;
   if(strchr(npt + 1, '/') == NULL)
     just_rx= 0;
   strncpy(wpt, apt, npt + 1 - apt);
   wpt+= npt + 1 - apt;
   *wpt= 0;
   ret= Xorriso_auto_chmod(xorriso, path, just_rx);
   if(ret == -1)
     {ret= -1; goto ex;}
   if(ret == -2)
     {ret= 0; goto ex;}
 }
 ret= 1;
ex:
 Xorriso_free_meM(path);
 return(ret);
}


/* @param flag bit0= prefer to find a match after *img_prefixes
                     (but deliver img_prefixes if no other can be found)
*/
int Xorriso_make_restore_path(struct XorrisO *xorriso,
         struct Xorriso_lsT **img_prefixes, struct Xorriso_lsT **disk_prefixes,
         char img_path[SfileadrL], char disk_path[SfileadrL], int flag)
{
 int li;
 struct Xorriso_lsT *s, *d, *found_s= NULL, *found_d= NULL;
 char *ipfx, *dpfx;

 /* Obtain disk_path by replacing start piece of img_path */
 
 d= *disk_prefixes;
 for(s= *img_prefixes; s != NULL;
     s= Xorriso_lst_get_next(s, 0), d= Xorriso_lst_get_next(d, 0)) {

   ipfx= Xorriso_lst_get_text(s, 0);
   li= strlen(ipfx);
   dpfx= Xorriso_lst_get_text(d, 0);
   if(li == 1 && ipfx[0] == '/') {
     li= 0;
     if(img_path[0] != '/')
 continue;
   } else {
     if(strncmp(img_path, ipfx, li) != 0)
 continue;
     if(img_path[li] != 0 && img_path[li] != '/')
 continue;
   }
   if(strlen(dpfx) + strlen(img_path) - li + 1 >= SfileadrL)
     return(-1);
   if(img_path[li]=='/') {
     if(dpfx[0] == '/' && dpfx[1] == 0)
       sprintf(disk_path, "/%s", img_path + li + 1);
     else
       sprintf(disk_path, "%s/%s", dpfx, img_path + li + 1);
   } else
     strcpy(disk_path, dpfx);     /* img_path[li] is 0, img_path equals ipfx */
   found_s= s;
   found_d= d;
   if(s != *img_prefixes || !(flag & 1))
 break;
 }
 *img_prefixes= found_s;
 *disk_prefixes= found_d;
 return(found_s != NULL);
}


/* @param flag bit0=permission to run Xorriso_make_accessible
*/
int Xorriso_restore_make_hl(struct XorrisO *xorriso,
                            char *old_path, char *new_path, int flag)
{
 int ret;
 struct PermiteM *perm_stack_mem;

 ret= link(old_path, new_path);
 if(ret == 0)
   return(1);
 if(errno == EACCES && (flag & 1)) {
   perm_stack_mem= xorriso->perm_stack;
   ret= Xorriso_make_accessible(xorriso, new_path, 0);
   if(ret > 0) {
      ret= link(old_path, new_path);
      if(ret == 0) {
        Permstack_pop(&(xorriso->perm_stack), perm_stack_mem, xorriso, 0);
        return(1);
      }
   }
   Permstack_pop(&(xorriso->perm_stack), perm_stack_mem, xorriso, 0);
 }

 sprintf(xorriso->info_text, "Hardlinking failed: ");
 Text_shellsafe(new_path, xorriso->info_text, 1);
 strcat(xorriso->info_text, " -> ");
 Text_shellsafe(old_path, xorriso->info_text, 1 | 2);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "WARNING", 0);
 return(0);
}


int Xorriso_afile_fopen(struct XorrisO *xorriso,
                        char *filename, char *mode, FILE **ret_fp, int flag)
/*
 bit0= do not print error message on failure
 bit1= do not open stdin
*/
{
 FILE *fp= NULL;

 *ret_fp= NULL;
 if(strcmp(filename,"-")==0) {
   if(mode[0]=='a' || mode[0]=='w' ||
      (mode[0]=='r' && mode[1]=='+') ||
      (mode[0]=='r' && mode[1]=='b' && mode[2]=='+')) 
     fp= stdout;
   else if(flag & 2) {
     Xorriso_msgs_submit(xorriso, 0, "Not allowed as input path: '-'", 0,
                       "FAILURE", 0);
     return(0);
   } else {
     Xorriso_msgs_submit(xorriso, 0, "Ready for data at standard input", 0,
                         "NOTE", 0);
     fp= stdin;
   }
 } else if(strncmp(filename,"tcp:",4)==0){
   Xorriso_msgs_submit(xorriso, 0, "TCP/IP service isn't implemented yet.", 0,
                       "FAILURE", 0);
 } else if(strncmp(filename,"file:",5)==0){
   fp= fopen(filename+5,mode);
 } else {
   fp= fopen(filename,mode);
 }
 if(fp==NULL){
   if(!(flag&1)) {
     sprintf(xorriso->info_text,
             "Failed to open file '%s' in %s mode\n", filename, mode);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
   }
   return(0);
 }
 *ret_fp= fp;
 return(1);
}


/*
   @param flag bit0= make absolute command paths with known systems
               bit1= do not allow prefixes with cmd
               bit2= interpret unprefixed cmd as shell:
*/
int Xorriso_make_mount_cmd(struct XorrisO *xorriso, char *cmd,
                           int lba, int track, int session, char *volid,
                           char *devadr, char result[SfileadrL], int flag)
{
 int ret, reg_file= 0, is_safe= 0, sys_code= 0;
 char *form= NULL, session_text[12], track_text[12], lba_text[12];
 char *vars[5][2], *sfe= NULL, *volid_sfe= NULL, *cpt, *sysname;
 struct stat stbuf;

 Xorriso_alloc_meM(form, char, 6 * SfileadrL);
 Xorriso_alloc_meM(sfe, char, 5 * SfileadrL);
 Xorriso_alloc_meM(volid_sfe, char, 5 * 80 + 1);

 if(strlen(cmd) > SfileadrL) {
   Xorriso_msgs_submit(xorriso, 0, "Argument much too long", 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 ret= stat(devadr, &stbuf);
 if(ret != -1)
   if(S_ISREG(stbuf.st_mode))
     reg_file= 1;
 if(strncmp(cmd, "linux:", 6) == 0 && !(flag & 2)) {
   cpt= cmd + 6;
   sys_code= 1;
 } else if(strncmp(cmd, "freebsd:", 8) == 0 && !(flag & 2)) {
   cpt= cmd + 8;
   sys_code= 2;
 } else if(strncmp(cmd, "string:", 7) == 0 && !(flag & 2)) {
   cpt= cmd + 7;
   strcpy(form, cpt);
 } else if(flag & 4) {
   cpt= cmd;
   strcpy(form, cpt);
 } else {
   cpt= cmd;
   ret= System_uname(&sysname, NULL, NULL, NULL, 0);
   if(ret <= 0) {
     Xorriso_msgs_submit(xorriso, 0,
                         "-mount*: Cannot determine current system type",
                         0, "FAILURE", 0);
     {ret= 0; goto ex;}
   } else if(strcmp(sysname, "FreeBSD") == 0 ||
             strcmp(sysname, "GNU/kFreeBSD") == 0) {
                                         /* "GNU/kFreeBSD" = Debian kfreebsd */
     sys_code= 2;
   } else if(strcmp(sysname, "Linux") == 0) {
     sys_code= 1;
   } else {
     sprintf(xorriso->info_text, "-mount*: Unsupported system type %s",
             Text_shellsafe(sysname, sfe, 0));
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
 }

 if(sys_code == 1) { /* GNU/Linux */
   sprintf(form,
           "%smount -t iso9660 -o %snodev,noexec,nosuid,ro,sbsector=%%sbsector%% %%device%% %s",
           (flag & 1 ? "/bin/" : ""),
           (reg_file || (xorriso->mount_opts_flag & 1) ? "loop," : ""),
           Text_shellsafe(cpt, sfe, 0));
   is_safe= 1;
 } else if(sys_code == 2) { /* FreeBSD */
   if(reg_file) {

/*  <<< Considered to create vnode as of
        J.R. Oldroyd <fbsd@opal.com>, 20 Nov 2008
        but for now refraining from creating that persistent file object

     strcpy(form, "n=$(mdconfig -a -t vnode -f %device%)");
     sprintf(form + strlen(form),
         " && mount -t cd9660 -o noexec,nosuid -s %%sbsector%% /dev/\"$n\" %s",
         Text_shellsafe(cmd+8, sfe, 0));
*/

     Xorriso_msgs_submit(xorriso, 0, 
           "Detected regular file as mount device with FreeBSD style command.",
           0, "FAILURE", 0);
     Xorriso_msgs_submit(xorriso, 0,
"Command mdconfig -a -t vnode -f can create a device node which uses the file",
                         0, "HINT", 0);
     {ret= 0; goto ex;}
   } else {
     sprintf(form,
         "%smount_cd9660 -o noexec,nosuid -s %%sbsector%% %%device%% %s",
         (flag & 1 ? "/sbin/" : ""), Text_shellsafe(cmd+8, sfe, 0));
     /*
       Not working on FreeBSD 7.2 according to Zsolt Kuti, 11 Oct 2009:
         "%smount -t cd9660 -o noexec,nosuid -o -s %%sbsector%% %%device%% %s",
     */
   }
   is_safe= 1;
 }
 sprintf(session_text, "%d", session);
 sprintf(track_text, "%d", track);
 sprintf(lba_text, "%d", lba);
 vars[0][0]= "sbsector";
 vars[0][1]= lba_text;
 vars[1][0]= "track";
 vars[1][1]= track_text;
 vars[2][0]= "session";
 vars[2][1]= session_text;
 vars[3][0]= "volid";
 vars[3][1]= Text_shellsafe(volid, volid_sfe, 0);
 vars[4][0]= "device";
 vars[4][1]= Text_shellsafe(devadr, sfe, 0);
 ret= Sregex_resolve_var(form, vars, 5, "%", "%", "%", result, SfileadrL, 0);
 if(ret <= 0)
   goto ex;
 ret= 1 + is_safe;
ex:;
 Xorriso_free_meM(form);
 Xorriso_free_meM(sfe);
 Xorriso_free_meM(volid_sfe);
 return(ret);
}


int Xorriso_append_scdbackup_record(struct XorrisO *xorriso, int flag)
{
 FILE *fp= NULL;
 char dummy[81], name[81], timestamp[81], size[81], md5[81];

 if(xorriso->scdbackup_tag_written[0] == 0)
   return(1);

 name[0]= timestamp[0]= size[0]= md5[0]= 0;
 sscanf(xorriso->scdbackup_tag_written, "%s %s %s %s %s %s %s",
        dummy, dummy, dummy, name, timestamp, size, md5);
 sprintf(xorriso->info_text, "scdbackup tag written : %s %s %s %s\n",
         name, timestamp, size, md5);
 Xorriso_msgs_submit(xorriso, 0,  xorriso->info_text, 0, "NOTE", 0);

 if(xorriso->scdbackup_tag_listname[0]) {
   fp= fopen(xorriso->scdbackup_tag_listname, "a");
   if(fp==0) {
     strcpy(xorriso->info_text, "-scdbackup_tag: Cannot open file ");
     Text_shellsafe(xorriso->scdbackup_tag_listname, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
   fprintf(fp, "%s %s %s %s\n", name, timestamp, size, md5);
   fclose(fp);
 }
 return(1);
}

