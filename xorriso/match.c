
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2013 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the implementation of functions for pattern matching.
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

#include "xorriso.h"
#include "xorriso_private.h"
#include "xorrisoburn.h"


/* @param flag bit0= do not augment relative structured search by xorriso->wdi
               bit1= return 2 if bonked at start point by .. 
                     (caller then aborts or retries without bit0)
               bit2= eventually prepend wdx rather than wdi
   @return <=0 error, 1= ok, 2= with bit1: relative pattern exceeds start point
*/
int Xorriso_prepare_regex(struct XorrisO *xorriso, char *adr, int flag)
{
 int l,ret,i,count,bonked= 0,is_constant,is_still_relative= 0, adr_size;
 char *cpt,*npt,*adr_part= NULL, *absolute_adr= NULL, *adr_start,*wd;

 adr_size= 2 * SfileadrL;
 Xorriso_alloc_meM(adr_part, char, adr_size);
 Xorriso_alloc_meM(absolute_adr, char, adr_size);

 if(flag&4)
   wd= xorriso->wdx;
 else
   wd= xorriso->wdi;

 if(xorriso->search_mode>=2 && xorriso->search_mode<=4) {
   if(xorriso->search_mode==3 || xorriso->search_mode==4) {
     l= strlen(adr)+strlen(wd)+1;
     if(l * 2 + 2 > ((int) sizeof(xorriso->reg_expr)) || l * 2 + 2 > adr_size){
       sprintf(xorriso->info_text,"Search pattern too long");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       {ret= 0; goto ex;}
     }
   }
   Xorriso_destroy_re(xorriso,0);
   if(xorriso->structured_search && xorriso->search_mode==3) {
     if(adr[0]!='/')
       is_still_relative= 1;
     if(is_still_relative && !(flag&1)) {
       /* relative expression : prepend working directory */
       sprintf(absolute_adr,"%s/%s",wd,adr);
       adr_start= absolute_adr;
       xorriso->prepended_wd= 1;
       is_still_relative= 0;
     } else
       adr_start= adr;
     /* count slashes */;
     cpt= adr_start;
     while(*cpt=='/')
       cpt++;
     for(i= 0;1;i++) {
       cpt= strchr(cpt,'/');
       if(cpt==NULL)
     break;
       while(*cpt=='/')
         cpt++;
     }
     count= i+1;
     xorriso->re= TSOB_FELD(regex_t,count);
     if(xorriso->re==NULL)
       {ret= -1; goto ex;}
     xorriso->re_constants= TSOB_FELD(char *,count);
     if(xorriso->re_constants==NULL)
       {ret= -1; goto ex;}
     for(i= 0;i<count;i++)
       xorriso->re_constants[i]= NULL;
     xorriso->re_count= count;
     xorriso->re_fill= 0;
       
     /* loop over slash chunks*/;
     cpt= adr_start;
     xorriso->re_fill= 0;
     while(*cpt=='/')
       cpt++;
     for(i= 0;i<count;i++) {
       npt= strchr(cpt,'/');
       if(npt==NULL) {
         if((int) strlen(cpt) >= adr_size)
           {ret= -1; goto ex;}
         strcpy(adr_part,cpt);
       } else {
         if(npt-cpt >= adr_size)
           {ret= -1; goto ex;}
         strncpy(adr_part,cpt,npt-cpt);
         adr_part[npt-cpt]= 0;
       }

       if(adr_part[0]==0)
         goto next_adr_part;
       if(adr_part[0]=='.' && adr_part[1]==0 &&
          (xorriso->re_fill>0 || i<count-1))
         goto next_adr_part;
       if(adr_part[0]=='.' && adr_part[1]=='.' && adr_part[2]==0) {
         /* delete previous part */
         if(xorriso->re_fill <= 0) {
           bonked= 1;
           goto next_adr_part;
         } 
         if(xorriso->re_constants[xorriso->re_fill-1]!=NULL) {
           free(xorriso->re_constants[xorriso->re_fill-1]);
           xorriso->re_constants[xorriso->re_fill-1]= NULL;
         } else
           regfree(&(xorriso->re[xorriso->re_fill-1]));
         (xorriso->re_fill)--;
         goto next_adr_part;
       }
       if(strcmp(adr_part,"*")==0) {
         adr_part[0]= 0;
         ret= 2;
       } else
         ret= Xorriso__bourne_to_reg(adr_part,xorriso->reg_expr,0);
       if(ret==2) {
         if(Sregex_string(&(xorriso->re_constants[xorriso->re_fill]),adr_part,0)
            <=0)
           {ret= -1; goto ex;}
       } else {
         if(regcomp(&(xorriso->re[xorriso->re_fill]),xorriso->reg_expr,0)!=0)
           goto cannot_compile;
       }
       xorriso->re_fill++;
next_adr_part:;
       if(i==count-1)
     break;
       cpt= npt+1;
       while(*cpt=='/')
         cpt++;
     }
     if(bonked) {
       if(flag&2)
         {ret= 2; goto ex;}
       sprintf(xorriso->info_text, "Your '..' bonked at the %s directory.",
               is_still_relative ? "working" : "root");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE",0);
       {ret= 0; goto ex;}
     }

     Xorriso__bourne_to_reg(adr_start,xorriso->reg_expr,0); /* just for show */

   } else { 
     is_constant= 0;
     if(strcmp(adr,"*")==0 || adr[0]==0) {
       is_constant= 1;
     } else if(xorriso->search_mode==3 || xorriso->search_mode==4) {
       ret= Xorriso__bourne_to_reg(adr,xorriso->reg_expr,0);
       is_constant= (ret==2);
     } else {
       if(strlen(adr)>=sizeof(xorriso->reg_expr))
         {ret= -1; goto ex;}
       strcpy(xorriso->reg_expr,adr);
     }
     xorriso->re_count= 0; /* tells matcher that this is not structured */
     xorriso->re_constants= TSOB_FELD(char *,1);
     if(xorriso->re_constants==NULL)
       {ret= -1; goto ex;}
     xorriso->re_constants[0]= NULL;
     if(is_constant) {
       if(strcmp(adr,"*")==0) {
         if(Sregex_string(&(xorriso->re_constants[0]),"",0)<=0)
           {ret= -1; goto ex;}
       } else {
         if(Sregex_string(&(xorriso->re_constants[0]),adr,0)<=0)
           {ret= -1; goto ex;}
       }
       xorriso->re_fill= 1;
     } else { 
       xorriso->re= TSOB_FELD(regex_t,1);
       if(xorriso->re==NULL)
         {ret= -1; goto ex;}
       if(regcomp(&(xorriso->re[0]),xorriso->reg_expr,0)!=0) {
cannot_compile:;
         sprintf(xorriso->info_text, "Cannot compile regular expression : %s",
                 xorriso->reg_expr);
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE",0);
         {ret= 0; goto ex;}
       }
     }

   }
 }
 ret= 1;
ex:;
 Xorriso_free_meM(adr_part);
 Xorriso_free_meM(absolute_adr);
 return(ret);
}


/* @param flag bit0= do not shortcut last component of to_match
               bit1= consider match if regex matches parent of path
               bit2= retry beginning at failed last component 

 @return 0=match , else no match 
*/
int Xorriso_regexec(struct XorrisO *xorriso, char *to_match, int *failed_at,
                    int flag)
{
 int ret,i,re_start= 0,reg_nomatch= -1;
 char *cpt,*npt, *adr_part= NULL, *mpt;

 Xorriso_alloc_meM(adr_part, char, SfileadrL);

 reg_nomatch= REG_NOMATCH;

 *failed_at= 0;
 if(!(xorriso->structured_search && xorriso->re_count>0)) {
   if(xorriso->re_constants!=NULL)
     if(xorriso->re_constants[0]!=NULL) {
       if(xorriso->re_constants[0][0]==0)
         {ret= 0; goto ex;}
       if(strcmp(xorriso->re_constants[0],to_match)!=0)
         {ret= reg_nomatch; goto ex;}
       {ret= 0; goto ex;}
     }  
   ret= regexec(&(xorriso->re[0]),to_match,1,xorriso->match,0);
   goto ex;
 }

 cpt= to_match;
 while(*cpt=='/')
  cpt++;
 if(flag&4)
   re_start= xorriso->re_failed_at;
 if(re_start<0)
   re_start= 0;
 for(i= re_start;i<xorriso->re_fill;i++) {
   *failed_at= i;
   npt= strchr(cpt,'/');
   if(npt==NULL) {
     if(i<xorriso->re_fill-1 && !(flag&1)) 
       {ret= reg_nomatch; goto ex;} /* this must be the last expression part */
     mpt= cpt;
   } else {
     strncpy(adr_part,cpt,npt-cpt);
     adr_part[npt-cpt]= 0;
     mpt= adr_part;
   }
   if(xorriso->re_constants[i]!=NULL) {
     if(xorriso->re_constants[i][0]!=0) /* empty constant matches anything */
       if(strcmp(xorriso->re_constants[i],mpt)!=0)
         {ret= reg_nomatch; goto ex;}
   } else {
     ret= regexec(&(xorriso->re[i]),mpt,1,xorriso->match,0);
     if(ret!=0)
       goto ex;
   }
   if(npt==NULL) {
     if(i>=xorriso->re_fill-1)
       {ret= 0; goto ex;} /* MATCH */
     *failed_at= i+1;
     {ret= reg_nomatch; goto ex;}
   }
   cpt= npt+1;
   while(*cpt=='/')
     cpt++; 
 }
 *failed_at= xorriso->re_fill;
 if(flag & 2)
   {ret= 0; goto ex;} /* MATCH */
 ret= reg_nomatch;
ex:;
 Xorriso_free_meM(adr_part);
 return(ret);
}


int Xorriso_is_in_patternlist(struct XorrisO *xorriso,
                              struct Xorriso_lsT *patternlist, char *path,
                              int flag)
{
 int ret, failed_at, i= 0;
 struct Xorriso_lsT *s;

 xorriso->search_mode= 3;
 xorriso->structured_search= 1;

 for(s= patternlist; s != NULL; s= Xorriso_lst_get_next(s, 0)) {
   ret= Xorriso_prepare_regex(xorriso, Xorriso_lst_get_text(s, 0), 0);
   if(ret <= 0)
     return(-1);
   /* Match path or parent of path */
   ret= Xorriso_regexec(xorriso, path, &failed_at, 2);
   if(ret == 0)
     return(i + 1);
   i++;
 }
 return(0);
}


char *Xorriso_get_pattern(struct XorrisO *xorriso,
                          struct Xorriso_lsT *patternlist, int index, int flag)
{
 int i= 0;
 struct Xorriso_lsT *s;

 for(s= patternlist; s != NULL; s= Xorriso_lst_get_next(s, 0)) {
   if(i == index)
     return(Xorriso_lst_get_text(s, 0));
   i++;
 }
 return(NULL);
}



/* @param flag bit2= this is a disk_pattern
   @return <=0 failure , 1 pattern ok , 2 pattern needed prepended wd */
int Xorriso_prepare_expansion_pattern(struct XorrisO *xorriso, char *pattern,
                                      int flag)
{
 int ret, prepwd= 0;

 ret= Xorriso_prepare_regex(xorriso, pattern, 1|2|(flag&4));
 if(ret==2) {
   ret= Xorriso_prepare_regex(xorriso, pattern, flag&4);
   prepwd= 1;
 }
 if(ret<=0) {
   sprintf(xorriso->info_text,
           "Cannot compile pattern to regular expression:  %s", pattern);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(1+prepwd);
}


/* @param flag bit0= count results rather than storing them
               bit1= unexpected change of number is a FATAL event
   @return <=0 error , 1 is root (end processing) ,
                       2 is not root (go on processing)
*/
int Xorriso_check_for_root_pattern(struct XorrisO *xorriso, 
               int *filec, char **filev, int count_limit, off_t *mem, int flag)
{
 if(xorriso->re_fill!=0)
   return(2);
 /* This is the empty pattern representing root */
 if(flag&1) {
   (*filec)++;
   (*mem)+= 8;
 } else {
   if(*filec >= count_limit) {
     sprintf(xorriso->info_text,
            "Number of matching files changed unexpectedly (> %d)",
             count_limit);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0,
                         (flag&2 ? "FATAL" : "WARNING"), 0);
     return(flag&2 ? -1 : 0);
   }
   filev[*filec]= strdup("/");
   if(filev[*filec]==NULL) {
     Xorriso_no_pattern_memory(xorriso, (off_t) 2, 0);
     return(-1);
   }
   (*filec)++;
 }
 return(1);
}


/* @param flag bit0= count result rather than storing it
               bit1= unexpected change of number is a FATAL event
*/
int Xorriso_register_matched_adr(struct XorrisO *xorriso,
                                char *adr, int count_limit,
                                int *filec, char **filev, off_t *mem, int flag)
{
 int l;

 if(flag&1) {
   (*filec)++;
   l= strlen(adr)+1;
   (*mem)+= sizeof(char *)+l;
   if(l % sizeof(char *))
     (*mem)+= sizeof(char *)-(l % sizeof(char *));
 } else {
   if(*filec >= count_limit) {
     sprintf(xorriso->info_text,
         "Number of matching files changed unexpectedly (> %d)",
         count_limit);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0,
                         (flag&2 ? "FATAL" : "WARNING"), 0);
     return(flag&2 ? -1 : 0);
   }
   filev[*filec]= strdup(adr);
   if(filev[*filec]==NULL) {
     Xorriso_no_pattern_memory(xorriso, (off_t) (strlen(adr)+1), 0);
     return(-1);
   }
   (*filec)++;
 }
 return(1);
}


/* @param flag bit0= count results rather than storing them
               bit1= this is a recursion
               bit2= prepend wd (automatically done if wd[0]!=0)
   @return <=0 error , 1 ok , 2 could not open directory
*/
int Xorriso_obtain_pattern_files_x(
       struct XorrisO *xorriso, char *wd, char *dir_adr,
       int *filec, char **filev, int count_limit, off_t *mem,
       int *dive_count, int flag)
{
 int ret, failed_at, follow_mount, follow_links;
 struct DirseQ *dirseq= NULL;
 struct stat stbuf;
 dev_t dir_dev;
 char *path;
 char *adr= NULL, *name= NULL, *path_data= NULL;

 adr= malloc(SfileadrL);
 name= malloc(SfileadrL);
 path_data= malloc(SfileadrL);
 if(adr==NULL || name==NULL || path_data==NULL) {
   Xorriso_no_malloc_memory(xorriso, &adr, 0);
   {ret= -1; goto ex;}
 }
 follow_mount= (xorriso->do_follow_mount || xorriso->do_follow_pattern);
 follow_links= (xorriso->do_follow_links || xorriso->do_follow_pattern);
 if(!(flag&2))
   *dive_count= 0;
 else
   (*dive_count)++;

 ret= Xorriso_check_for_root_pattern(xorriso, filec, filev, count_limit,
                                     mem, flag&1);
 if(ret!=2)
   goto ex;

 if(lstat(dir_adr, &stbuf)==-1)
   {ret= 2; goto ex;}
 dir_dev= stbuf.st_dev;
 if(S_ISLNK(stbuf.st_mode)) {
   if(stat(dir_adr, &stbuf)==-1)
     {ret= 2; goto ex;}
   if(dir_dev != stbuf.st_dev && !follow_mount)
     {ret= 2; goto ex;}
 }
 ret= Dirseq_new(&dirseq, dir_adr, 1);
 if(ret<0) {
   sprintf(xorriso->info_text, "Cannot obtain disk directory iterator");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   {ret= -1; goto ex;}
 }
 if(ret==0)
   {ret= 2; goto ex;}

 while(1) {
   ret= Dirseq_next_adr(dirseq,name,0);
   if(ret==0)
 break;
   if(ret<0) {
     sprintf(xorriso->info_text,"Failed to obtain next directory entry");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
     {ret= -1; goto ex;}
   }

   ret= Xorriso_make_abs_adr(xorriso, wd, name, adr, flag&4);
   if(ret<=0)
     goto ex;

   ret= Xorriso_regexec(xorriso, adr, &failed_at, 1);
   if(ret>0) { /* no match */
     if(failed_at <= *dive_count) /* no hope for a match */
 continue;
     path= adr;
     if(adr[0]!='/') {
       path= path_data;
       ret= Xorriso_make_abs_adr(xorriso, xorriso->wdx, adr, path, 1|4);
       if(ret<=0)
         goto ex;
     }

     if(follow_links)
       ret= stat(path,&stbuf);
     else
       ret= lstat(path,&stbuf);
     if(ret==-1)
 continue;
     if(!S_ISDIR(stbuf.st_mode))
 continue;
     if(dir_dev != stbuf.st_dev && !follow_mount)
 continue;

     /* dive deeper */
     ret= Xorriso_obtain_pattern_files_x(xorriso, adr, path,
                           filec, filev, count_limit, mem, dive_count, flag|2);
     if(ret<=0)
       goto ex;
   } else {
     ret= Xorriso_register_matched_adr(xorriso, adr, count_limit,
                                       filec, filev, mem, flag&1);
     if(ret<0)
       goto ex;
     if(ret==0)
 break;
   }
 } 
 ret= 1;
ex:;
 if(adr!=NULL)
   free(adr);
 if(name!=NULL)
   free(name);
 if(path_data!=NULL)
   free(path_data);
 Dirseq_destroy(&dirseq,0);
 if(flag&2)
   (*dive_count)--;
 return(ret);
}


int Xorriso_eval_nonmatch(struct XorrisO *xorriso, char *pattern,
                          int *nonconst_mismatches, off_t *mem, int flag)
{
 int k,l;

 /* Is this a constant pattern ? */
 for(k= 0; k<xorriso->re_fill; k++) {
   if(xorriso->re_constants[k]==NULL)
 break;
   if(xorriso->re_constants[k][0]==0)
 break;
 }
 if(k<xorriso->re_fill)
   (*nonconst_mismatches)++; /* it is not */

 l= strlen(pattern)+1;
 (*mem)+= sizeof(char *)+l;
 if(l % sizeof(char *))
   (*mem)+= sizeof(char *)-(l % sizeof(char *));
 return(1);
}


/* @param flag bit0= a match count !=1 is a SORRY event
               bit1= a match count !=1 is a FAILURE event
*/
int Xorriso_check_matchcount(struct XorrisO *xorriso,
                int count, int nonconst_mismatches, int num_patterns,
                char **patterns, int flag)
{

 if((flag&1) && (count!=1 || nonconst_mismatches)){
   if(count-nonconst_mismatches>0)
     sprintf(xorriso->info_text,
             "Pattern match with more than one file object");
   else
     sprintf(xorriso->info_text, "No pattern match with any file object");
   if(num_patterns==1)
     sprintf(xorriso->info_text+strlen(xorriso->info_text), ": ");
   Text_shellsafe(patterns[0], xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, 
                       (flag&2 ? "FAILURE" : "SORRY"), 0);
   return(0);
 }
 return(1);
}


int Xorriso_no_pattern_memory(struct XorrisO *xorriso, off_t mem, int flag)
{
 char mem_text[80];

 Sfile_scale((double) mem, mem_text,5,1e4,1);
 sprintf(xorriso->info_text,
         "Cannot allocate enough memory (%s) for pattern expansion",
         mem_text);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
 return(1);
}


int Xorriso_alloc_pattern_mem(struct XorrisO *xorriso, off_t mem, 
                              int count, char ***filev, int flag)
{
 char mem_text[80], limit_text[80];

 Sfile_scale((double) mem, mem_text,5,1e4,0);
 sprintf(xorriso->info_text,
         "Temporary memory needed for pattern expansion : %s", mem_text);
 if(!(flag&1))
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
 if(mem > xorriso->temp_mem_limit) {
   Sfile_scale((double) xorriso->temp_mem_limit, limit_text,5,1e4,1);
   sprintf(xorriso->info_text,
           "List of matching file addresses exceeds -temp_mem_limit (%s > %s)",
           mem_text, limit_text);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }

 (*filev)= (char **) calloc(count, sizeof(char *));
 if(*filev==NULL) {
   Xorriso_no_pattern_memory(xorriso, mem, 0);
   return(-1);
 }
 return(1);
}


/* @param flag bit0= a match count !=1 is a FAILURE event
               bit1= with bit0 tolerate 0 matches if pattern is a constant
*/
int Xorriso_expand_disk_pattern(struct XorrisO *xorriso,
                           int num_patterns, char **patterns, int extra_filec,
                           int *filec, char ***filev, off_t *mem, int flag)
{
 int ret, count= 0, abs_adr= 0, i, was_count, was_filec;
 int nonconst_mismatches= 0, dive_count= 0;
 char *dir_adr= NULL;

 Xorriso_alloc_meM(dir_adr, char, SfileadrL);

 *filec= 0;
 *filev= NULL;

 xorriso->search_mode= 3;
 xorriso->structured_search= 1;

 for(i= 0; i<num_patterns; i++) {

   ret= Xorriso_prepare_expansion_pattern(xorriso, patterns[i], 4);
   if(ret<=0)
     goto ex;
   if(ret==2)
     abs_adr= 4;

   if(patterns[i][0]=='/' || abs_adr) {
     strcpy(dir_adr, "/");
     abs_adr= 4;
   } else {
     strcpy(dir_adr, xorriso->wdx);
     if(dir_adr[0]==0)
       strcpy(dir_adr, "/");
     ret= Sfile_type(dir_adr, 1|4);
     if(ret!=2) {
       Xorriso_msgs_submit(xorriso, 0, dir_adr, 0, "ERRFILE", 0);
       sprintf(xorriso->info_text, "Address set by -cdx is not a directory: ");
       Text_shellsafe(dir_adr, xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
     }
   }

   /* count the matches */
   was_count= count;
   ret= Xorriso_obtain_pattern_files_x(xorriso, "", dir_adr, &count, NULL, 0,
                                       mem, &dive_count, 1 | abs_adr);
   if(ret<=0)
     goto ex;
   if(was_count==count && strcmp(patterns[i],"*")!=0 && (flag&3)!=1) {
     count++;
     ret= Xorriso_eval_nonmatch(xorriso, patterns[i],
                                &nonconst_mismatches, mem, 0);
     if(ret<=0)
       goto ex;
   }
 }

 ret= Xorriso_check_matchcount(xorriso, count, nonconst_mismatches,
                               num_patterns, patterns, (flag&1)|2);
 if(ret<=0)
   goto ex;

 count+= extra_filec;
 mem+= extra_filec*sizeof(char *);

 if(count<=0)
   {ret= 0; goto ex;}

 ret= Xorriso_alloc_pattern_mem(xorriso, *mem, count, filev, 0);
 if(ret<=0)
   goto ex;

 /* now store addresses */
 for(i= 0; i<num_patterns; i++) {

   ret= Xorriso_prepare_expansion_pattern(xorriso, patterns[i], 4);
   if(ret<=0)
     goto ex;

   if(patterns[i][0]=='/' || abs_adr) {
     strcpy(dir_adr, "/");
     abs_adr= 4;
   } else {
     strcpy(dir_adr, xorriso->wdx);
     if(dir_adr[0]==0)
       strcpy(dir_adr, "/");
   }

   was_filec= *filec;
   ret= Xorriso_obtain_pattern_files_x(xorriso, "", dir_adr, filec, *filev,
                                       count, mem, &dive_count, abs_adr);
   if(ret<=0)
     goto ex;

   if(was_filec == *filec && strcmp(patterns[i],"*")!=0) {
     (*filev)[*filec]= strdup(patterns[i]);
     if((*filev)[*filec]==NULL) {
       (*mem)= strlen(patterns[i])+1;
       Xorriso_no_pattern_memory(xorriso, *mem, 0);
       ret= -1; goto ex;
     }
     (*filec)++;
   } 
 }

 ret= 1;
ex:;
 if(ret<=0) {
   if(filev!=NULL)
     Sfile_destroy_argv(&count, filev, 0);
   *filec= 0;
 }
 Xorriso_free_meM(dir_adr);
 return(ret);
}


/* @param flag bit0= command without pattern capability
               bit1= disk_pattern rather than iso_rr_pattern
*/
int Xorriso_warn_of_wildcards(struct XorrisO *xorriso, char *path, int flag)
{
 static int count_iso= 0, count_disk= 0, max_iso= 3, max_disk= 3;

 if(strchr(path,'*')!=NULL || strchr(path,'?')!=NULL ||
    strchr(path,'[')!=NULL) {
   if(flag & 2) {
     count_disk++;
     if(count_disk > max_disk)
       return(1);
   } else {
     count_iso++;
     if(count_iso > max_iso)
       return(1);
   }
   if(flag&1) {
     sprintf(xorriso->info_text,
      "Pattern expansion of wildcards \"*?[\" does not apply to this command");
   } else {
     sprintf(xorriso->info_text,
            "Pattern expansion of wildcards \"*?[\" is disabled by command %s",
            (flag&2) ? "-disk_pattern or -pathspecs" : "-iso_rr_pattern");
   }
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
   sprintf(xorriso->info_text,"Pattern seen: ");
   Text_shellsafe(path, xorriso->info_text, 1);
   strcat(xorriso->info_text, "\n");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
   return(1);
 }
 return(0);
}

