
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2012 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the implementation of functions around files and strings.
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
#include <pwd.h>
#include <grp.h>


#include "sfile.h"


/* @param flag bit0= do not clip off carriage return at line end
*/
char *Sfile_fgets_n(char *line, int maxl, FILE *fp, int flag)
{
 int l;
 char *ret;

 ret= fgets(line,maxl,fp);
 if(ret==NULL)
   return(NULL);
 l= strlen(line);
 if(l > 0 && !(flag & 1)) if(line[l-1] == '\r') line[--l]= 0;
 if(l > 0) if(line[l-1] == '\n') line[--l]= 0;
 if(l > 0 && !(flag & 1)) if(line[l-1] == '\r') line[--l]= 0;
 return(ret);
}


int Sfile_count_char(char *text, char to_count)
{
 int count= 0;
 char *cpt;

 for(cpt= text; *cpt != 0; cpt++)
   if(*cpt == to_count)
     count++;
 return count;
}


int Sfile_count_components(char *path, int flag)
/*
 bit0= do not ignore trailing slash
 bit1= do not ignore empty components (other than the empty root name)
*/
{
 int l,count= 0;
 char *cpt;

 l= strlen(path);
 if(l==0)
   return(0);
 count= 1;
 for(cpt= path+l-1;cpt>=path;cpt--) { 
   if(*cpt=='/') {
     if(*(cpt+1)==0   && !(flag&1))
 continue;
     if(*(cpt+1)=='/' && !(flag&2))
 continue;
     count++;
   }
 }
 return(count);
}


int Sfile_component_pointer(char *path, char **sourcept, int idx, int flag)
/*
 bit0= do not ignore trailing slash
 bit1= do not ignore empty components (other than the empty root name)
 bit2= accept 0 as '/'
*/
{
 int count= 0;
 char *spt;

 for(spt= path;*spt!=0 || (flag&4);spt++) {
   if(count>=idx) {
     *sourcept= spt;
     return(1);
   }
   if(*spt=='/' || *spt==0) {
     if(*(spt+1)=='/' && !(flag&2))
 continue;
     if(*(spt+1)==0 && !(flag&1))
 continue;
     count++;
   }
 }
 if((flag&1) && count>=idx)
   return(1);
 return(0);
}


int Sfile_leafname(char *path, char leafname[SfileadrL], int flag)
{
 int count, ret;
 char *lpt;

 leafname[0]= 0;
 count= Sfile_count_components(path, 0);
 if(count==0)
   return(0);
 ret= Sfile_component_pointer(path, &lpt, count-1, 0);
 if(ret<=0)
   return(ret);
 if(Sfile_str(leafname, lpt, 0)<=0)
   return(0);
 lpt= strchr(leafname, '/');
 if(lpt!=NULL)
   *lpt= 0;
 return(1);
} 


int Sfile_add_to_path(char path[SfileadrL], char *addon, int flag)
{
 int l;

 l= strlen(path);
 if(l+1>=SfileadrL)
   return(0);
 if(l==0) {
   strcpy(path,"/");
   l= 1;
 } else if(path[l-1]!='/') {
   path[l++]= '/';
   path[l]= 0;
 }
 if(l+strlen(addon)>=SfileadrL)
   return(0);
 if(addon[0]=='/')
   strcpy(path+l,addon+1);
 else
   strcpy(path+l,addon);
 return(1);
}


int Sfile_prepend_path(char *prefix, char path[SfileadrL], int flag)
{
 int l, i, slashes, prefix_len, path_len;

 l= strlen(prefix);
 if(l == 0)
   return(1);

 /* Do not copy slashes between both parts */
 for(prefix_len= l; prefix_len > 0; prefix_len--)
   if(prefix[prefix_len - 1] != '/')
 break;
 if(prefix_len == 0)
   prefix_len= strlen(prefix) - 1; 
 path_len= strlen(path);
 for(slashes= 0; slashes < path_len; slashes++)
   if(path[slashes] != '/')
 break;

 l= (strlen(path) - slashes) + prefix_len + 1;
 if(l>=SfileadrL) {

#ifdef Not_yeT
   /* >>> ??? how to transport messages to xorriso ? */
   sprintf(xorriso->info_text,
           "Combination of wd and relative address too long (%d > %d)",
           l,SfileadrL-1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
#endif

   return(-1);
 }
 l-= strlen(path);
 if(l < 0) {
   for(i= slashes; i <= path_len + 1; i++)
     path[i+l]= path[i];
 } else if(l > 0) {
   for(i= path_len + 1; i >= slashes; i--)
     path[i+l]= path[i];
 }
 if(prefix_len > 0)
   memcpy(path, prefix, prefix_len);
 path[l - 1 + slashes]= '/';
 return(1);
}

int Sfile_being_group_member(struct stat *stbuf, int flag)
{
 int i, suppl_groups;
 gid_t *suppl_glist;

 if (getegid()==stbuf->st_gid)
   return(1);
 suppl_groups= getgroups(0, NULL);
 suppl_glist= (gid_t *) malloc((suppl_groups + 1) * sizeof(gid_t));
 if (suppl_glist==NULL)
   return(-1);
 suppl_groups= getgroups(suppl_groups+1,suppl_glist);
 for (i= 0; i<suppl_groups; i++) {
   if (suppl_glist[i]==stbuf->st_gid) {
     free((char *) suppl_glist);
     return(1);
   }
 }
 free((char *) suppl_glist);
 return(0);
}


int Sfile_type(char *filename, int flag)
/*
 bit0= return -1 if file is missing 
 bit1= return a hardlink with siblings as type 5
 bit2= evaluate eventual link target rather than the link object itself
 bit3= return a socket or a char device as types 7 or 8 rather than 0
*/
/*
 return:
  0=unknown
  1=regular
  2=directory
  3=symbolic link
  4=named pipe
  5=multiple hardlink (with bit1)
  6=block device
  7=socket (with bit3)
  8=character device (with bit3)
*/
{
 struct stat stbuf;
 if(flag&4) {
   if(stat(filename,&stbuf)==-1) {
     if(flag&1) return(-1);
     else       return(0);
   }
 } else {
   if(lstat(filename,&stbuf)==-1) {
     if(flag&1) return(-1);
     else       return(0);
   }
 }
 if(S_ISREG(stbuf.st_mode)) {
   if(flag&2)
     if(stbuf.st_nlink>1)
       return(5);
   return(1);
 }
 if(S_ISDIR(stbuf.st_mode))
   return(2);
 if((stbuf.st_mode&S_IFMT)==S_IFLNK)
   return(3);
 if(S_ISFIFO(stbuf.st_mode))
   return(4);
 if(S_ISBLK(stbuf.st_mode))
   return(6);
 if(flag&8)
   if((stbuf.st_mode&S_IFMT)==S_IFSOCK)
     return(7);
 if(flag&8)
   if(S_ISCHR(stbuf.st_mode))
     return(8);
 return(0);
}


char *Sfile_datestr(time_t tim, short int flag)
/*
 bit0=with hours+minutes  
 bit1=with seconds

 bit8= local time rather than UTC
*/
{
 static char zeitcode[80]={"000000"};
 char puff[80];
 struct tm *azt;

 if(flag&256) 
   azt = localtime(&tim);
 else
   azt = gmtime(&tim);
 
 if(azt->tm_year>99)
   sprintf(zeitcode,"%c%1.1d%2.2d%2.2d",
           'A'+(azt->tm_year-100)/10,azt->tm_year%10,
           azt->tm_mon+1,azt->tm_mday);
 else
   sprintf(zeitcode,"%2.2d%2.2d%2.2d",
           azt->tm_year,azt->tm_mon+1,azt->tm_mday);
 if(flag&1){
   sprintf(puff,".%2.2d%2.2d",azt->tm_hour,azt->tm_min);
   strcat(zeitcode,puff);
 }
 if(flag&2){
   sprintf(puff,"%2.2d",azt->tm_sec);
   strcat(zeitcode,puff);
 }

 return(zeitcode);
}


int Sfile_scale(double value, char *result, int siz, double thresh, int flag)
/*
 bit0= eventually ommit 'b'
 bit1= make text as short as possible
 bit2= no fraction (if it would fit at all)
*/
{
 char scale_c,scales[7],form[80], *negpt= NULL, *cpt;
 int i,dec_siz= 0,avail_siz= 1;

 if(value<0) {
   value= -value;
   siz--;
   result[0]= '-';
   negpt= result;
   result++;
 }
 strcpy(scales,"bkmgtp");
 scale_c= scales[0];
 for(i=1;scales[i]!=0;i++) {
   if(value<thresh-0.5) 
 break;
   value/= 1024.0;
   scale_c= scales[i];
 } 
 if(scale_c!='b' && !(flag&4)) { /* is there room for fractional part ? */
   avail_siz= siz-1;
   sprintf(form,"%%.f");
   sprintf(result,"%.f",value);
   if(((int) strlen(result)) <= avail_siz - 2) 
     dec_siz= 1;                                  /* we are very modest */
 }
 if(scale_c=='b' && (flag&1)) {
   if(flag&2)
     sprintf(form,"%%.f");
   else
     sprintf(form,"%%%d.f",siz);
   sprintf(result,form,value);
 } else {
   if(flag&2)
     sprintf(form,"%%.f%%c");
   else if(dec_siz>0) 
     sprintf(form,"%%%d.%df%%c",avail_siz,dec_siz);
   else
     sprintf(form,"%%%d.f%%c",siz-1);
   sprintf(result,form,value,scale_c);
 }
 if(negpt != NULL) {
   for(cpt= result; *cpt==' '; cpt++);
   if(cpt > result) {
     *negpt= ' ';
     *(cpt - 1)= '-';
   } 
 }
 return(1);
}


int Sfile_off_t_text(char text[80], off_t num, int flag)
{
 char *tpt;
 off_t hnum, scale= 1;
 int digits= 0, d, i;

 tpt= text;
 hnum= num;
 if(hnum<0) {
   *(tpt++)= '-';
   hnum= -num;
 }
 if(hnum<0) { /* it can stay nastily persistent */
   strcpy(text, "_overflow_");
   return(0);
 }
 for(i= 0; i<23; i++) { /* good for up to 70 bit = 10 exp 21.07... */
   if(hnum==0)
 break;
   hnum/= 10;
   if(hnum)
     scale*= 10;
 }
 if(i==0) {
   strcpy(text, "0");
   return(1);
 }
 if(i==23) {
   strcpy(text, "_overflow_");
   return(0);
 }
 digits= i;
 hnum= num;
 for(; i>0; i--) {
   d= hnum/scale;
   tpt[digits-i]= '0'+d;
   hnum= hnum%scale;
   scale/= 10;
 }
 tpt[digits]= 0;
 return(1);
}


/* Converts backslash codes into single characters:
    \a BEL 7 , \b BS 8 , \e ESC 27 , \f FF 12 , \n LF 10 , \r CR 13 ,
    \t  HT 9 , \v VT 11 , \\ \ 92 
    \[0-9][0-9][0-9] octal code , \x[0-9a-f][0-9a-f] hex code ,
    \cX control-x (ascii(X)-64)
   @param upto  maximum number of characters to examine for backslash.
                The scope of a backslash (0 to 3 characters) is not affected.
   @param eaten returns the difference in length between input and output
   @param flag bit0= only determine *eaten, do not convert
               bit1= allow to convert \000 to binary 0 
*/
int Sfile_bsl_interpreter(char *text, int upto, int *eaten, int flag)
{
 char *rpt, *wpt, num_text[8], wdummy[8];
 unsigned int num= 0;

 *eaten= 0;
 wpt= text;
 for(rpt= text; *rpt != 0 && rpt - text < upto; rpt++) {
   if(flag & 1)
     wpt= wdummy;
   if(*rpt == '\\') {
     rpt++;
     (*eaten)++;
     if(*rpt == 'a') {
       *(wpt++)= 7;
     } else if(*rpt == 'b') {
       *(wpt++)= 8;
     } else if(*rpt == 'e') {
       *(wpt++)= 27;
     } else if(*rpt == 'f') {
       *(wpt++)= 12;
     } else if(*rpt == 'n') {
       *(wpt++)= 10;
     } else if(*rpt == 'r') {
       *(wpt++)= 13;
     } else if(*rpt == 't') {
       *(wpt++)= 9;
     } else if(*rpt == 'v') {
       *(wpt++)= 11;
     } else if(*rpt == '\\') {
       *(wpt++)= '\\';
     } else if(rpt[0] >= '0' && rpt[0] <= '7' &&
               rpt[1] >= '0' && rpt[1] <= '7' &&
               rpt[2] >= '0' && rpt[2] <= '7') {
       num_text[0]= '0';
       num_text[1]= *(rpt + 0);
       num_text[2]= *(rpt + 1);
       num_text[3]= *(rpt + 2);
       num_text[4]= 0;
       sscanf(num_text, "%o", &num);
       if((num > 0 || (flag & 2)) && num <= 255) {
         rpt+= 2;
         (*eaten)+= 2;
         *(wpt++)= num;
       } else
         goto not_a_code;
     } else if(rpt[0] == 'x' &&
               ((rpt[1] >= '0' && rpt[1] <= '9') ||
                (rpt[1] >= 'A' && rpt[1] <= 'F') ||
                (rpt[1] >= 'a' && rpt[1] <= 'f'))
               &&
               ((rpt[2] >= '0' && rpt[2] <= '9') ||
                (rpt[2] >= 'A' && rpt[2] <= 'F') ||
                (rpt[2] >= 'a' && rpt[2] <= 'f'))
               ) {
       num_text[0]= *(rpt + 1);
       num_text[1]= *(rpt + 2);
       num_text[2]= 0;
       sscanf(num_text, "%x", &num);
       if(num > 0 && num <= 255) {
         rpt+= 2;
         (*eaten)+= 2;
         *(wpt++)= num;
       } else
         goto not_a_code;
     } else if(*rpt == 'c') {
       if(rpt[1] > 64 && rpt[1] < 96) {
         *(wpt++)= rpt[1] - 64;
         rpt++;
         (*eaten)++;
       } else
         goto not_a_code;
     } else {
not_a_code:;
       *(wpt++)= '\\';
       rpt--;
       (*eaten)--;
     }
   } else
     *(wpt++)= *rpt;
 }
 *wpt= *rpt;
 return(1);
}


int Sfile_argv_bsl(int argc, char ***argv, int flag)
{
 int i, ret, eaten;
 char **new_argv= NULL;

 if(argc <= 0)
   return(0);
 new_argv= (char **) Smem_malloC(argc * sizeof(char *));
 if(new_argv == NULL)
   return(-1);
 for(i= 0; i < argc; i++) {
   new_argv[i]= strdup((*argv)[i]);
   if(new_argv[i] == NULL)
     {ret= -1; goto ex;}
   ret= Sfile_bsl_interpreter(new_argv[i], strlen(new_argv[i]), &eaten, 0);
   if(ret <= 0)
     goto ex;
 }
 ret= 1;
ex:;
 if(ret <= 0) {
   if(new_argv != NULL)
     free((char *) new_argv);
 } else
   *argv= new_argv;
 return(ret);
}


/* @param flag bit0= only encode inside quotes
               bit1= encode < 32 outside quotes except 7, 8, 9, 10, 12, 13
               bit2= encode in any case above 126
               bit3= encode in any case shellsafe and name-value-safe:
                     <=42 , 59, 60, 61, 62, 63, 92, 94, 96, >=123
*/
int Sfile_bsl_encoder(char **result, char *text, size_t text_len, int flag)
{
 signed char *rpt;
 char *wpt;
 int count, sq_open= 0, dq_open= 0;

 count= 0;
 for(rpt= (signed char *) text; (size_t) (((char *) rpt) - text) < text_len;
     rpt++) {
   count++;
   if(flag & 8) {
      if(!(*rpt <= 42 || (*rpt >= 59 && *rpt <= 63) ||
           *rpt == 92 || *rpt == 94 || *rpt == 96 || *rpt >= 123))
 continue;
   } else if(*rpt >= 32 && *rpt <= 126 && *rpt != '\\')
 continue;
   if(((*rpt >= 7 && *rpt <= 13) || *rpt == 27 || *rpt == '\\') && !(flag & 8))
     count++;
   else
     count+= 3;
 }
 (*result)= wpt= calloc(count + 1, 1);
 if(wpt == NULL)
   return(-1);
 for(rpt= (signed char *) text; (size_t) (((char *) rpt) - text) < text_len;
     rpt++) {
   if(*rpt == '\'')
     sq_open= !(sq_open || dq_open);
   if(*rpt == '"')
     dq_open= !(sq_open || dq_open);
 
   if(flag & 8) {
     if(!(*rpt <= 42 || (*rpt >= 59 && *rpt <= 63) ||
          *rpt == 92 || *rpt == 94 || *rpt == 96 || *rpt >= 123)) {
       *(wpt++)= *rpt; 
 continue;
     }  
   } else if(*rpt >= 32 && *rpt <= 126 && *rpt != '\\') {
     *(wpt++)= *rpt;
 continue;
   } else if( ((flag & 1) && !(sq_open || dq_open)) &&
             !((flag & 2) && (*rpt >= 1 && * rpt <= 31 &&
               !(*rpt == 7 || *rpt == 8 || *rpt == 9 || *rpt == 10 ||
                 *rpt == 12 || *rpt == 13))) &&
             !((flag & 4) && (*rpt > 126 || *rpt < 0)) &&
             !((flag & 6) && *rpt == '\\')) {
     *(wpt++)= *rpt;
 continue;
   }
   *(wpt++)= '\\';
   if(((*rpt >= 7 && *rpt <= 13) || *rpt == 27 || *rpt == '\\') && !(flag&8)) {
     if(*rpt == 7)
       *(wpt++)= 'a';
     else if(*rpt == 8)
       *(wpt++)= 'b';
     else if(*rpt == 9)
       *(wpt++)= 't';
     else if(*rpt == 10) {
       *(wpt++)= 'n';
     } else if(*rpt == 11)
       *(wpt++)= 'v';
     else if(*rpt == 12)
       *(wpt++)= 'f';
     else if(*rpt == 13)
       *(wpt++)= 'c';
     else if(*rpt == 27)
       *(wpt++)= 'e';
     else if(*rpt == '\\')
       *(wpt++)= '\\';
   } else {
     sprintf(wpt, "%-3.3o", (unsigned int) *((unsigned char *) rpt));
     wpt+= 3;
   }
 }
 *wpt= 0;
 return(1);
}


int Sfile_destroy_argv(int *argc, char ***argv, int flag)
{
 int i;

 if(*argc>0 && *argv!=NULL){
   for(i=0;i<*argc;i++){
     if((*argv)[i]!=NULL)
       Smem_freE((*argv)[i]);
   }
   Smem_freE((char *) *argv);
 }
 *argc= 0;
 *argv= NULL;
 return(1);
}


int Sfile_sep_make_argv(char *progname, char *line, char *separators,
                        int max_words, int *argc, char ***argv, int flag)
/*
 bit0= read progname as first argument from line
 bit1= just release argument list argv and return
 bit2= abort with return(0) if incomplete quotes are found
 bit3= eventually prepend missing '-' to first argument read from line
 bit4= like bit2 but only check quote completeness, do not allocate memory
 bit5+6= interpretation of backslashes:
       0= no interpretation, leave unchanged
       1= only inside double quotes
       2= outside single quotes
       3= everywhere
 bit7= append a NULL element to argv
*/
{
 int i,pass,maxl=0,l,argzaehl=0,bufl,line_start_argc, bsl_mode, ret= 0, eaten;
 char *cpt,*start;
 char *buf= NULL;

 Sfile_destroy_argv(argc,argv,0);
 if(flag&2)
   {ret= 1; goto ex;}

 if(flag & 16)
   flag|= 4;
 bsl_mode= (flag >> 5) & 3;

 buf= calloc(strlen(line) + SfileadrL, 1);
 if(buf == NULL)
   {ret= -1; goto ex;}
 for(pass=0;pass<2;pass++) {
   cpt= line-1;
   if(!(flag&1)){
     argzaehl= line_start_argc= 1;
     if(pass==0)
       maxl= strlen(progname);
     else
       strcpy((*argv)[0],progname);
   } else {
     argzaehl= line_start_argc= 0;
     if(pass==0) maxl= 0;
   }
   while(*(++cpt)!=0){
     if(*separators) {
       if(strchr(separators, *cpt) != NULL)
   continue;
     } else if(isspace(*cpt))
   continue;
     start= cpt;
     buf[0]= 0;
     cpt--;

     if(max_words > 0 && argzaehl >= max_words && *cpt != 0) {
       /* take uninterpreted up to the end */
       cpt+= strlen(cpt) - 1;
     }

     while(*(++cpt)!=0) {
       if(*separators) {
         if(strchr(separators, *cpt) != NULL)
     break;
       } else if(isspace(*cpt))
     break;
       if(*cpt=='"'){
         l= cpt-start; bufl= strlen(buf);
         if(l>0) {
           strncpy(buf + bufl, start, l); buf[bufl + l]= 0;
           if(bsl_mode >= 3) {
             ret= Sfile_bsl_interpreter(buf, l, &eaten, 0);
             if(ret <= 0)
               goto ex;
           }
         }
         l= strlen(buf);
         start= cpt+1;
         while(*(++cpt)!=0) if(*cpt=='"') break;
         if((flag&4) && *cpt==0)
           {ret= 0; goto ex;}
         l= cpt-start; bufl= strlen(buf);
         if(l>0) {
           strncpy(buf + bufl, start, l);
           buf[bufl + l]= 0;
           if(bsl_mode >= 1) {
             ret= Sfile_bsl_interpreter(buf + bufl, l, &eaten, 0);
             if(ret <= 0)
               goto ex;
           }
         }
         start= cpt+1;
       }else if(*cpt=='\''){
         l= cpt-start; bufl= strlen(buf);
         if(l>0) {
           strncpy(buf + bufl, start, l); buf[bufl + l]= 0;
           if(bsl_mode >= 3) {
             ret= Sfile_bsl_interpreter(buf, l, &eaten, 0);
             if(ret <= 0)
               goto ex;
           }
         }
         l= strlen(buf);
         start= cpt+1;
         while(*(++cpt)!=0) if(*cpt=='\'') break;
         if((flag&4) && *cpt==0)
           {ret= 0; goto ex;}
         l= cpt-start; bufl= strlen(buf);
         if(l>0) {
           strncat(buf,start,l);buf[bufl+l]= 0;
           if(bsl_mode >= 2) {
             ret= Sfile_bsl_interpreter(buf + bufl, l, &eaten, 0);
             if(ret <= 0)
               goto ex;
           }
         }
         start= cpt+1;
       }
     if(*cpt==0) break;
     }
     l= cpt-start;
     bufl= strlen(buf);
     if(l>0) {
       strncpy(buf + bufl, start, l); buf[bufl + l]= 0;
       if(bsl_mode >= 3) {
         ret= Sfile_bsl_interpreter(buf, l, &eaten, 0);
         if(ret <= 0)
           goto ex;
       }
     }
     l= strlen(buf);
     if(pass==0){
       if(argzaehl==line_start_argc && (flag&8))
         if(buf[0]!='-' && buf[0]!=0 && buf[0]!='#')
           l++;
       if(l>maxl) maxl= l;
     }else{
       strcpy((*argv)[argzaehl],buf);
       if(argzaehl==line_start_argc && (flag&8))
         if(buf[0]!='-' && buf[0]!=0 && buf[0]!='#')
           sprintf((*argv)[argzaehl],"-%s", buf);
     }
     argzaehl++;
   if(*cpt==0) break;
   }
   if(pass==0){
     if(flag & 16)
       {ret= 1; goto ex;}
     *argc= argzaehl;
     if(argzaehl>0 || (flag & 128)) {
       *argv= (char **) Smem_malloC((argzaehl + !!(flag & 128))
                                    * sizeof(char *));
       if(*argv==NULL)
         {ret= -1; goto ex;}
     }
     for(i=0;i<*argc;i++) {
       (*argv)[i]= (char *) Smem_malloC((maxl+1));
       if((*argv)[i]==NULL)
         {ret= -1; goto ex;}
     }
     if(flag & 128)
       (*argv)[*argc]= NULL;
   }
 }
 ret= 1;
ex:
 if(buf != NULL)
   free(buf);
 return(ret);
}


int Sfile_make_argv(char *progname, char *line, int *argc, char ***argv,
                    int flag)
{
 return Sfile_sep_make_argv(progname, line, "", 0, argc, argv, flag);
}


/* @param flag bit0= append */
int Sfile_str(char target[SfileadrL], char *source, int flag)
{
 int l;

 l= strlen(source);
 if(flag&1)
   l+= strlen(target);
 if(l>=SfileadrL) {
   fprintf(stderr, "--- Path string overflow (%d > %d). Malicious input ?\n",
           l,SfileadrL-1);
   return(0);
 }
 if(flag&1)
   strcat(target, source);
 else
   strcpy(target, source);
 return(1);
}


/** Combine environment variable HOME with given filename
    @param filename Address relative to $HOME
    @param fileadr Resulting combined address
    @param fa_size Size of array fileadr
    @param flag Unused yet
    @return 1=ok , 0=no HOME variable , -1=result address too long
*/
int Sfile_home_adr_s(char *filename, char *fileadr, int fa_size, int flag)
{
 char *home;

 strcpy(fileadr,filename);
 home= getenv("HOME");
 if(home==NULL)
   return(0);
 if((int) (strlen(home) + strlen(filename) + 1) >= fa_size)
   return(-1);
 strcpy(fileadr,home);
 if(filename[0]!=0){
   strcat(fileadr,"/");
   strcat(fileadr,filename);
 }
 return(1);
}


/** Return a double representing seconds and microseconds since 1 Jan 1970 */
double Sfile_microtime(int flag)
{
 struct timeval tv;
 struct timezone tz;
 gettimeofday(&tv,&tz);
 return((double) (tv.tv_sec+1.0e-6*tv.tv_usec));
}


int Sfile_decode_datestr(struct tm *reply, char *text, int flag)
/* YYMMDD[.hhmm[ss]] */
{
 int i,l;
 time_t current_time;
 struct tm *now;

 current_time= time(0);
 now= localtime(&current_time);
 for(i=0; i < (int) sizeof(struct tm); i++)
   ((char *) reply)[i]= ((char *) now)[i];

 if(text[0]<'0'|| (text[0]>'9' && text[0]<'A') || text[0]>'Z')
   return(0);
 l= strlen(text);
 for(i=1;i<l;i++)
   if(text[i]<'0'||text[i]>'9')
     break;
 if(i!=6)
   return(0);
 if(text[i]==0)
   goto decode;
 if(text[i]!='.' || (l!=11 && l!=13))
   return(0);
 for(i++;i<l;i++)
   if(text[i]<'0'||text[i]>'9')
     break;
 if(i!=l)
   return(0);

decode:;
 reply->tm_hour= 0;
 reply->tm_min= 0;
 reply->tm_sec= 0;
 i= 0;
 if(text[0]>='A') 
   reply->tm_year= 100+(text[i]-'A')*10+text[1]-'0';
 else
   reply->tm_year= 10*(text[0]-'0')+text[1]-'0';
 reply->tm_mon=  10*(text[2]-'0')+text[3]-'0'-1;
 reply->tm_mday= 10*(text[4]-'0')+text[5]-'0';
 if(l==6)
   return(1);
 reply->tm_hour= 10*(text[7]-'0')+text[8]-'0';
 reply->tm_min=  10*(text[9]-'0')+text[10]-'0';
 if(l==11)
   return(1);
 reply->tm_sec=  10*(text[11]-'0')+text[12]-'0';
 return(1);
}


