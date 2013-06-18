/*
  Compare two copies of a file object in as many aspects as i can imagine
  to make sense. (E.g.: comparing atime makes no sense.)

  To compare tree /media/dvd and /original/dir :
      find /media/dvd -exec compare_file '{}' /media/dvd /original/dir ';'

  Copyright 2008 - 2010 Thomas Schmitt, <scdbackup@gmx.net>

  Provided under GPL version 2 or later.


  cc -g -o compare_file compare_file.c
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>


/* @param flag bit0= single letters */
char *Ftypetxt(mode_t st_mode, int flag)
{
 if(flag&1)
   goto single_letters;
 if(S_ISDIR(st_mode))
   return("directory");
 else if(S_ISREG(st_mode))
   return("regular_file");
 else if(S_ISLNK(st_mode))
   return("symbolic_link");
 else if(S_ISBLK(st_mode))
   return("block_device");
 else if(S_ISCHR(st_mode))
   return("char_device");
 else if(S_ISFIFO(st_mode))
   return("name_pipe");
 else if(S_ISSOCK(st_mode))
   return("unix_socket");
 return("unknown");
single_letters:;
 if(S_ISDIR(st_mode))
   return("d");
 else if(S_ISREG(st_mode))
   return("-");
 else if(S_ISLNK(st_mode))
   return("l");
 else if(S_ISBLK(st_mode))
   return("b");
 else if(S_ISCHR(st_mode))
   return("c");
 else if(S_ISFIFO(st_mode))
   return("p");
 else if(S_ISSOCK(st_mode))
   return("s");
 return("?");
}


char *Ftimetxt(time_t t, char timetext[40], int flag)
{
 char *rpt;
 struct tm tms, *tmpt;
 static char months[12][4]= { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

 tmpt= localtime_r(&t, &tms);
 rpt= timetext;
 rpt[0]= 0;
 if(tmpt==0)
   sprintf(rpt+strlen(rpt), "%12.f", (double) t);
 else if(time(NULL)-t < 180*86400 && time(NULL)-t >= 0)
   sprintf(rpt+strlen(rpt), "%3s %2d %2.2d:%2.2d",
           months[tms.tm_mon], tms.tm_mday, tms.tm_hour, tms.tm_min);
 else
   sprintf(rpt+strlen(rpt), "%3s %2d  %4.4d",
           months[tms.tm_mon], tms.tm_mday, 1900+tms.tm_year);
 return(timetext);
}


/* @param flag bit0= compare atime
               bit1= compare ctime
*/
int Compare_2_files(char *adr1, char *adr2, char *adrc, int flag)
{
 struct stat s1, s2;
 int ret, differs= 0, r1, r2, fd1= -1, fd2= -1, i, done;
 char buf1[4096], buf2[4096], a[4096], ttx1[40], ttx2[40];
 off_t r1count= 0, r2count= 0, diffcount= 0, first_diff= -1;

 ret= lstat(adr1, &s1);
 if(ret==-1) {
   printf("? %s : cannot lstat() : %s\n", adr1, strerror(errno));
   return(0);
 }
 strcpy(a, Ftypetxt(s1.st_mode, 1));
 strcat(a, " ");
 if(adrc[0])
   strcat(a, adrc);
 else
   strcat(a, ".");

 ret= lstat(adr2, &s2);
 if(ret==-1) {
   printf("? %s : cannot lstat() : %s\n", adr2, strerror(errno));
   return(0);
 }

 /* Attributes */
 if(s1.st_mode != s2.st_mode) {
   if((s1.st_mode&~S_IFMT)!=(s2.st_mode&~S_IFMT))
     printf("%s : st_mode  :  %7.7o  <>  %7.7o\n", a,
             (unsigned int) (s1.st_mode & ~S_IFMT),
             (unsigned int) (s2.st_mode & ~S_IFMT));
   if((s1.st_mode&S_IFMT)!=(s2.st_mode&S_IFMT))
     printf("%s : type     :  %s  <>  %s\n",
            a, Ftypetxt(s1.st_mode, 0), Ftypetxt(s2.st_mode, 0));
   differs= 1;
 }
 if(s1.st_uid != s2.st_uid) {
   printf("%s : st_uid   :  %lu  <>  %lu\n",
          a, (unsigned long) s1.st_uid, (unsigned long) s2.st_uid);
   differs= 1;
 }
 if(s1.st_gid != s2.st_gid) {
   printf("%s : st_gid   :  %lu  <>  %lu\n",
          a, (unsigned long) s1.st_gid, (unsigned long) s2.st_gid);
   differs= 1;
 }
 if((S_ISCHR(s1.st_mode) && S_ISCHR(s2.st_mode)) ||
    (S_ISBLK(s1.st_mode) && S_ISBLK(s2.st_mode))) {
   if(s1.st_rdev != s2.st_rdev) {
     printf("%s : %s st_rdev  :  %lu  <>  %lu\n", a,
            (S_ISCHR(s1.st_mode) ? "S_IFCHR" : "S_IFBLK"),
            (unsigned long) s1.st_rdev, (unsigned long) s1.st_rdev);
     differs= 1;
   }
 }
 if(S_ISREG(s2.st_mode) && s1.st_size != s2.st_size) {
   printf("%s : st_size  :  %.f  <>  %.f      diff= %.f\n",
          a, (double) s1.st_size, (double) s2.st_size,
             ((double) s1.st_size) - (double) s2.st_size);
   differs= 1;
 }
 if(s1.st_mtime != s2.st_mtime) {
   printf("%s : st_mtime :  %s  <>  %s      diff= %.f s\n",
          a, Ftimetxt(s1.st_mtime, ttx1, 0),
             Ftimetxt(s2.st_mtime, ttx2, 0),
             ((double) s1.st_mtime) - (double) s2.st_mtime);
   differs= 1;
 }
 if(flag&1) {
   if(s1.st_atime != s2.st_atime) {
     printf("%s : st_atime :  %s  <>  %s      diff= %.f s\n",
            a, Ftimetxt(s1.st_atime, ttx1, 0),
               Ftimetxt(s2.st_atime, ttx2, 0),
               ((double) s1.st_atime) - (double) s2.st_atime);
     differs= 1;
   }
 }
 if(flag&2) {
   if(s1.st_ctime != s2.st_ctime) {
     printf("%s : st_ctime :  %s  <>  %s      diff= %.f s\n",
            a, Ftimetxt(s1.st_ctime, ttx1, 0),
               Ftimetxt(s2.st_ctime, ttx2, 0),
               ((double) s1.st_ctime) - (double) s2.st_ctime);
     differs= 1;
   }
 }
 if(S_ISREG(s1.st_mode) && S_ISREG(s2.st_mode)) {
   fd1= open(adr1, O_RDONLY);
   if(fd1==-1) {
     printf("- %s : cannot open() : %s\n", adr1, strerror(errno));
     return(0);
   }
   fd2= open(adr2, O_RDONLY);
   if(fd2==-1) {
     printf("- %s : cannot open() : %s\n", adr2, strerror(errno));
     close(fd1);
     return(0);
   }
  
   /* Content */
   done= 0;
   while(!done) {
     r1= read(fd1, buf1, sizeof(buf1));
     r2= read(fd2, buf2, sizeof(buf2));
     if((r1==EOF && r2==EOF) || (r1==0 && r2==0))
   break;
     if(r1==EOF || r1==0) {
       if(r1==EOF)
         r1= 0;
       if(s1.st_size > r1count + r1)
         printf("- %s : early EOF after %.f bytes\n", adr1, (double) r1count);
       differs= 1;
     }
     r1count+= r1;
     if(r2==EOF || r2<r1) {
       if(r2==EOF)
         r2= 0;
       if(s2.st_size > r2count + r2)
         printf("- %s : early EOF after %.f bytes\n", adr2, (double) r2count);
       differs= 1;
       done= 1;
     }
     if(r2>r1) {
       if(s1.st_size > r1count + r1)
         printf("- %s : early EOF after %.f bytes\n", adr1, (double) r1count);
       differs= 1;
       done= 1;
     }
     r2count+= r2;
     if(r1>r2)
       r1= r2;
     for(i= 0; i<r1; i++) {
       if(buf1[i]!=buf2[i]) {
         if(first_diff<0)
           first_diff= i;
         diffcount++;
       }
     }
   }
   if(diffcount>0 || r1count!=r2count) {
     if(first_diff<0)
       first_diff= (r1count>r2count ? r2count : r1count);
     printf("%s : %s  :  differs by at least %.f bytes. First at %.f\n", a,
            (s1.st_mtime==s2.st_mtime ?  "CONTENT":"content"),
            (double) (diffcount + abs(r1count-r2count)), (double) first_diff);
     differs= 1;
   }
 }
 if(fd1!=-1)
   close(fd1);
 if(fd2!=-1)
   close(fd2);
 return(!differs);
}


int main(int argc, char **argv)
{
 int ret, i, with_ctime= 1;
 char adr1[4096], adr2[4096], adrc[4096];

 if(sizeof(off_t) < 8) {
   fprintf(stderr,
    "%s : FATAL : Compile time misconfiguration. sizeof(off_t) too small.\n\n",
    argv[0]);
   exit(4);
 }
 if(argc<4) {
   fprintf(stderr, "usage: %s  path  prefix1  prefix2\n", argv[0]);
   exit(2);
 }
 for(i= 4; i<argc; i++) {
   if(strcmp(argv[i], "-no_ctime")==0)
     with_ctime= 0;
   else {
     fprintf(stderr, "%s : Option not recognized: '%s'\n", argv[0], argv[i]);
     exit(2);
   }
 }

 if(strncmp(argv[1], argv[2], strlen(argv[2]))!=0) {
   fprintf(stderr, "%s: path '%s' does not match prefix1 '%s'\n",
          argv[0], argv[1], argv[2]);
   exit(2);
 }
 strcpy(adr1, argv[1]);
 strcpy(adrc, argv[1]+strlen(argv[2]));
 sprintf(adr2, "%s%s%s",
         argv[3], (adrc[0]=='/' || adrc[0]==0 ? "" : "/"), adrc);

 ret=  Compare_2_files(adr1, adr2, adrc, (with_ctime<<1));
 exit(ret<=0);
}

