
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2011  Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the implementation of actions which compare or update
   files between disk filesystem and ISO filesystem.
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


/*
   @param result  Bitfield indicationg type of mismatch
              bit11= cannot open regular disk file
              bit12= cannot open iso file
              bit13= early eof of disk file
              bit14= early eof of iso file
              bit15= content bytes differ
   @param flag bit0= mtimes of both file objects are equal
              bit29= do not issue pacifier messages
              bit31= do not issue result messages
   @return    >0 ok , <=0 error
*/
int Xorriso_compare_2_contents(struct XorrisO *xorriso, char *common_adr,
                               char *disk_adr, off_t disk_size,
                               off_t offset, off_t bytes,
                               char *iso_adr, off_t iso_size,
                               int *result, int flag)
{
 int fd1= -1, ret, r1, r2, done, wanted, i, was_error= 0, use_md5= 0;
 void *stream2= NULL;
 off_t r1count= 0, r2count= 0, diffcount= 0, first_diff= -1;
 char *respt, *buf1= NULL, *buf2= NULL, offset_text[80];
 char disk_md5[16], iso_md5[16];
 void *ctx= NULL;
 int buf_size= 32 * 1024;

 Xorriso_alloc_meM(buf1, char, buf_size);
 Xorriso_alloc_meM(buf2, char, buf_size);

 respt= xorriso->result_line;

 fd1= open(disk_adr, O_RDONLY);
 if(fd1==-1) {
   sprintf(respt, "- %s (DISK) : cannot open() : %s\n",
           disk_adr, strerror(errno));
cannot_address:;
   if(!(flag&(1<<31)))
     Xorriso_result(xorriso,0);
   (*result)|= 2048;
   {ret= 0; goto ex;}
 }
 if(offset>0)
   if(lseek(fd1, offset, SEEK_SET)==-1) {
     sprintf(respt, "- %s (DISK) : cannot lseek(%.f) : %s\n",
             disk_adr, (double) offset, strerror(errno));
     close(fd1);
     goto cannot_address;
   }

 if(xorriso->do_md5 & 16) {
   use_md5= 1;
   ret= Xorriso_is_plain_image_file(xorriso, NULL, iso_adr, 0);
   if(ret <= 0) 
     ret= 0; /* (reverse) filtered files are likely not to match their MD5 */
   else
     ret= Xorriso_get_md5(xorriso, NULL, iso_adr, iso_md5, 1);
   if(ret <= 0)
     use_md5= 0;
   else {
     ret= Xorriso_md5_start(xorriso, &ctx, 0);
     if(ret <= 0)
       use_md5= 0;
   }
 }
 if (! use_md5) {
   ret= Xorriso_iso_file_open(xorriso, iso_adr, NULL, &stream2, 0);
   if(ret<=0) {
     sprintf(respt, "- %s  (ISO) : cannot open() file in ISO image\n",iso_adr);
     if(!(flag&(1<<31)))
       Xorriso_result(xorriso,0);
     close(fd1);
     (*result)|= 4096;
     {ret= 0; goto ex;}
   }
 }

 done= 0;
 while(!done) {

   wanted= buf_size;
   if(r1count+offset+wanted>disk_size)
     wanted= disk_size-r1count-offset;
   if(r1count+wanted>bytes)
     wanted= bytes-r1count;
   r1= 0;
   while(wanted>0) {
     ret= read(fd1, buf1, wanted);
     if(ret<=0)
   break;
     wanted-= ret;
     r1+= ret;
   }

   wanted= buf_size;
   if(r2count+wanted>iso_size)
     wanted= iso_size-r2count;
/*
   if(r2count+wanted>bytes)
     wanted= bytes-r2count;
*/
   if(use_md5)
     r2= r1;
   else if(wanted>0)
     r2= Xorriso_iso_file_read(xorriso, stream2, buf2, wanted, 0);
   else
     r2= 0;

   if(r1<0 || r2<0)
     was_error= 1;

   if(r1<=0 && r2<=0)
 break;
   if(r1<=0) {
     if(r1<0)
       r1= 0;
     if(disk_size > r1count + r1 + offset) {
       sprintf(respt, "- %s (DISK) : early EOF after %.f bytes\n",
               disk_adr, (double) r1count);
       if(!(flag&(1<<31)))
         Xorriso_result(xorriso,0);
       (*result)|= 8196;
     }
     (*result)|= (1<<15);
   }
   r1count+= r1;
   if(r2<=0 || r2<r1) {
     if(r2<0)
       r2= 0;
     if(iso_size > r2count + r2) {
       sprintf(respt, "- %s  (ISO) : early EOF after %.f bytes\n",
               iso_adr, (double) r2count);
       if(!(flag&(1<<31)))
         Xorriso_result(xorriso,0);
       (*result)|= (1<<14);
     }
     (*result)|= (1<<15);
     done= 1;
   }
   if(r2>r1) {
     if(disk_size > r1count + r1 + offset) {
       sprintf(respt, "- %s (DISK) : early EOF after %.f bytes\n",
               disk_adr, (double) r1count);
       if(!(flag&(1<<31)))
         Xorriso_result(xorriso,0);
       (*result)|= 8196;
     }
     (*result)|= (1<<15);
     done= 1;
   }
   r2count+= r2;
   if(r1>r2)
     r1= r2;

   if(use_md5) {
     Xorriso_md5_compute(xorriso, ctx, buf1, r1, 0);
   } else {
     for(i= 0; i<r1; i++) {
       if(buf1[i]!=buf2[i]) {
         if(first_diff<0)
           first_diff= r1count - r1 + i;
         diffcount++;
       }
     }
   }
   if(!(flag&(1<<29))) {
     xorriso->pacifier_count+= r1;
     xorriso->pacifier_byte_count+= r1;
     if(flag&(1<<31))
       Xorriso_pacifier_callback(xorriso, "content bytes read",
                                 xorriso->pacifier_count, 0, "", 8);
     else
       Xorriso_pacifier_callback(xorriso, "bytes", xorriso->pacifier_count, 0,
                                 "", 8 | 1<<6);
   }
 }

 if(use_md5) {
   ret= Xorriso_md5_end(xorriso, &ctx, disk_md5, 0);
   if(ret <= 0) {
     *result |= (1 << 15);
     ret= -1; goto ex;
   }
   for(i= 0; i < 16; i++)
     if(iso_md5[i] != disk_md5[i])
   break;
   if(i < 16 ) {
     offset_text[0]= 0;
     if(offset>0)
       sprintf(offset_text, "%.f+", (double) offset);
     sprintf(respt, "%s %s  :  differs by MD5 sums.\n",
             common_adr, (flag&1 ?  "CONTENT": "content"));
     if(!(flag&(1<<31)))
       Xorriso_result(xorriso,0);
     (*result)|= (1<<15);
   }
 } else if(diffcount>0 || r1count!=r2count) {
   if(first_diff<0)
     first_diff= (r1count>r2count ? r2count : r1count);
   offset_text[0]= 0;
   if(offset>0)
     sprintf(offset_text, "%.f+", (double) offset);
   sprintf(respt, "%s %s  :  differs by at least %.f bytes. First at %s%.f\n",
           common_adr, (flag&1 ?  "CONTENT": "content"),
           (double) (diffcount + abs(r1count-r2count)),
           offset_text, (double) first_diff);
   if(!(flag&(1<<31)))
     Xorriso_result(xorriso,0);
   (*result)|= (1<<15);
 }
 if(fd1!=-1)
   close(fd1);
 if(! use_md5)
   Xorriso_iso_file_close(xorriso, &stream2, 0);
 if(was_error)
   {ret= -1; goto ex;}
 ret= 1;
ex:;
 if(ctx != NULL)
   Xorriso_md5_end(xorriso, &ctx, disk_md5, 0);
 Xorriso_free_meM(buf1);
 Xorriso_free_meM(buf2);
 return(ret);
}


/*
   @param result  Bitfield indicationg type of mismatch
               bit0= disk_adr not existing
               bit1= iso_adr not existing
               bit2= access permissions
               bit3= file type 
               bit4= user id
               bit5= group id
               bit6= minor, major with device file
               bit7= size
               bit8= mtime
               bit9= atime
              bit10= ctime
              bit11= cannot open regular disk file
              bit12= cannot open iso file
              bit13= early eof of disk file
              bit14= early eof of iso file
              bit15= content bytes differ
              bit16= symbolic link on disk pointing to dir, dir in iso
              bit17= file chunks detected and compared
              bit18= incomplete chunk collection encountered
              bit19= ACL differs (this condition sets also bit2)
              bit20= xattr differ
              bit21= mismatch of recorded dev,inode
              bit22= no recorded dev,inode found in node
              bit23= timestamps younger than xorriso->isofs_st_in
              bit24= hardlink split
              bit25= hardlink fusion
   @param flag bit0= compare atime
               bit1= compare ctime
               bit2= check only existence of both file objects
                     count one or both missing as "difference"
              bit26= do not issue message about missing disk file
              bit27= for Xorriso_path_is_excluded(): bit0
              bit28= examine eventual disk_path link target rather than link
              bit29= do not issue pacifier messages
              bit30= omit adr_common_tail in report messages
              bit31= do not issue result messages
   @return     1=files match properly , 0=difference detected , -1=error
*/
int Xorriso_compare_2_files(struct XorrisO *xorriso, char *disk_adr,
                            char *iso_adr, char *adr_common_tail,
                            int *result, int flag)
{
 struct stat s1, s2, stbuf;
 int ret, missing= 0, is_split= 0, i, was_error= 0, diff_count= 0;
 int content_shortcut= 0, mask;
 char *respt;
 char *a= NULL;
 char ttx1[40], ttx2[40];
 char *a1_acl= NULL, *a2_acl= NULL, *d1_acl= NULL, *d2_acl= NULL;
 char *attrlist1= NULL, *attrlist2= NULL;
 struct SplitparT *split_parts= NULL;
 int split_count= 0;
 time_t stamp;

 char *part_path= NULL, *part_name;
 int partno, total_parts= 0;
 off_t offset, bytes, total_bytes;

 Xorriso_alloc_meM(a, char, 5*SfileadrL);
 Xorriso_alloc_meM(part_path, char, SfileadrL);
 
 *result= 0;
 respt= xorriso->result_line;

 if(!(xorriso->disk_excl_mode&8)) {
   ret= Xorriso_path_is_excluded(xorriso, disk_adr, 2 | !!(flag&(1<<27)));
   if(ret>0) {
     strcpy(respt, "? ");
     Text_shellsafe(disk_adr, respt, 1);
     sprintf(respt + strlen(respt), " (DISK) : exluded by %s\n",
             (ret==1 ? "-not_paths" : "-not_leaf"));
     if(! (flag & ((1 << 31) | (1 << 26))))
       Xorriso_result(xorriso,0);
     missing= 1;
     (*result)|= 1;
   }
 }
 if(!missing) {
   if(flag&(1<<28))
     ret= stat(disk_adr, &s1);
   else
     ret= lstat(disk_adr, &s1);
   if(ret==-1) {
     strcpy(respt, "? ");
     Text_shellsafe(disk_adr, respt, 1);
     sprintf(respt + strlen(respt),
             " (DISK) : cannot lstat() : %s\n", strerror(errno));
     if(! (flag & ((1 << 31) | (1 << 26))))
       Xorriso_result(xorriso,0);
     missing= 1;
     (*result)|= 1;
   }
 }
 if(missing)
   strcpy(a, "?");
 else
   strcpy(a, Ftypetxt(s1.st_mode, 1));  
 strcat(a, " ");
 if(adr_common_tail[0])
   Text_shellsafe(adr_common_tail, a, 1);
 else {
   Text_shellsafe(disk_adr, a+strlen(a), 0);
   strcat(a, " (DISK)");
/*
   strcat(a, "'.'");
*/
 }
 strcat(a, " :");
 if(flag&(1<<30))
   a[0]= 0;

 ret= Xorriso_iso_lstat(xorriso, iso_adr, &s2, 0);
 if(ret<0) {
   strcpy(respt, "? ");
   Text_shellsafe(iso_adr, respt, 1);
   strcat(respt, "  (ISO) : cannot find this file in ISO image\n");
   if(!(flag&(1<<31)))
     Xorriso_result(xorriso,0);
   missing= 1;
   (*result)|= 2;
 }

 if((flag&4)||missing)
   {ret= !missing; goto ex;}


 /* Splitfile parts */
 if((S_ISREG(s1.st_mode) || S_ISBLK(s1.st_mode)) && S_ISDIR(s2.st_mode)) {
   is_split= Xorriso_identify_split(xorriso, iso_adr, NULL, &split_parts,
                                    &split_count, &s2, 0);
   if(is_split>0)
     (*result)|= (1<<17);
   else
     is_split= 0;
 }

 /* Attributes */
 if(s1.st_mode != s2.st_mode) {
   if((s1.st_mode&~S_IFMT)!=(s2.st_mode&~S_IFMT)) {
     sprintf(respt, "%s st_mode  :  %7.7o  <>  %7.7o\n",
             a, (unsigned int) (s1.st_mode & ~S_IFMT),
                (unsigned int) (s2.st_mode & ~S_IFMT));
     if(!(flag&(1<<31)))
       Xorriso_result(xorriso,0);
     (*result)|= 4;
   }

   if((s1.st_mode&S_IFMT)!=(s2.st_mode&S_IFMT)) {
     sprintf(respt, "%s type     :  %s  <>  %s\n",
             a, Ftypetxt(s1.st_mode, 0), Ftypetxt(s2.st_mode, 0));
     if(!(flag&(1<<31)))
       Xorriso_result(xorriso,0);
     (*result)|= 8;
     if((s1.st_mode&S_IFMT) == S_IFLNK) {
       /* check whether link target type matches */
       ret= stat(disk_adr, &stbuf);
       if(ret!=-1)
         if(S_ISDIR(stbuf.st_mode) && S_ISDIR(s2.st_mode))
           (*result)|= (1<<16);
     }
   }
 }

 /* ACL */
 if(xorriso->do_aaip & 3) {
   Xorriso_local_getfacl(xorriso, disk_adr, &a1_acl,
                         16 | ((flag & (1 << 28)) >> 23));
   if(S_ISDIR(s1.st_mode))
     Xorriso_local_getfacl(xorriso, disk_adr, &d1_acl, 1);
   ret= Xorriso_getfacl(xorriso, NULL, iso_adr, &a2_acl, 1 | 4 | 16);
   if(ret < 0)
     goto ex;
   if(S_ISDIR(s1.st_mode)) {
     ret= Xorriso_getfacl(xorriso, NULL, iso_adr, &d2_acl, 1 | 8);
     if(ret < 0)
       goto ex;
   }
   ret= Compare_text_lines(a1_acl, a2_acl, &diff_count, 0);
   if(ret < 0)
     goto ex;
   if(ret == 0)
     (*result)|= 4 | (1 << 19);
   ret= Compare_text_lines(d1_acl, d2_acl, &diff_count, 1);
   if(ret < 0)
     goto ex;
   if(ret == 0)
     (*result)|= 4 | (1 << 19);
   if((*result) & (1 << 19)) {
     sprintf(respt, "%s ACL      :  %d difference%s\n",
             a, diff_count, diff_count == 1 ? "" : "s");
     if(!(flag&(1<<31)))
       Xorriso_result(xorriso,0);
   }
 }

 /* xattr */
 if(xorriso->do_aaip & 12) {
   ret= Xorriso_getfattr(xorriso, NULL, disk_adr, &attrlist1,
                         1 | 2 | ((flag & (1 << 28)) >> 23));
   if(ret < 0)
     goto ex;
   ret= Xorriso_getfattr(xorriso, NULL, iso_adr, &attrlist2, 1);
   if(ret < 0)
     goto ex;
   ret= Compare_text_lines(attrlist1, attrlist2, &diff_count, 0);
   if(ret < 0)
     goto ex;
   if(ret == 0) {
     (*result)|= (1 << 20);
     sprintf(respt, "%s xattr    :  %d difference%s\n",
             a, diff_count, diff_count == 1 ? "" : "s");
     if(!(flag&(1<<31)))
       Xorriso_result(xorriso,0);
   }
 }

 if(s1.st_uid != s2.st_uid) {
   sprintf(respt, "%s st_uid   :   %lu  <>  %lu\n", a,
           (unsigned long) s1.st_uid, (unsigned long) s2.st_uid);
   if(!(flag&(1<<31)))
     Xorriso_result(xorriso,0);
   (*result)|= 16;
 }
 if(s1.st_gid != s2.st_gid) {
   sprintf(respt, "%s st_gid   :   %lu  <>  %lu\n", a,
           (unsigned long) s1.st_gid, (unsigned long) s2.st_gid);
   if(!(flag&(1<<31)))
     Xorriso_result(xorriso,0);
   (*result)|= 32;
 }
 if((S_ISCHR(s1.st_mode) && S_ISCHR(s2.st_mode)) ||
    (S_ISBLK(s1.st_mode) && S_ISBLK(s2.st_mode))) {
   if(s1.st_rdev != s2.st_rdev) {
     sprintf(respt, "%s %s st_rdev  :  %lu  <>  %lu\n", a,
            (S_ISCHR(s1.st_mode) ? "S_IFCHR" : "S_IFBLK"),
            (unsigned long) s1.st_rdev, (unsigned long) s1.st_rdev);
     if(!(flag&(1<<31)))
       Xorriso_result(xorriso,0);
     (*result)|= 64;
   }
 }
 if((!(xorriso->do_aaip & 32)) &&
    S_ISREG(s2.st_mode) && s1.st_size != s2.st_size) {
   sprintf(respt, "%s st_size  :  %.f  <>  %.f      diff= %.f\n",
          a, (double) s1.st_size, (double) s2.st_size,
          ((double) s1.st_size) - (double) s2.st_size);
   if(!(flag&(1<<31)))
     Xorriso_result(xorriso,0);
   (*result)|= 128;
 }
 if(s1.st_mtime != s2.st_mtime) {
   sprintf(respt, "%s st_mtime :  %s  <>  %s      diff= %.f s\n",
           a, Ftimetxt(s1.st_mtime, ttx1, 0),
              Ftimetxt(s2.st_mtime, ttx2, 0),
              ((double) s1.st_mtime) - (double) s2.st_mtime);
   if(!(flag&(1<<31)))
     Xorriso_result(xorriso,0);
   (*result)|= 256;
 }
 if(flag&1) {
   if(s1.st_atime != s2.st_atime) {
     sprintf(respt, "%s st_atime :  %s  <>  %s      diff= %.f s\n",
             a, Ftimetxt(s1.st_atime, ttx1, 0), 
                Ftimetxt(s2.st_atime, ttx2, 0),
                ((double) s1.st_atime) - (double) s2.st_atime);
     if(!(flag&(1<<31)))
       Xorriso_result(xorriso,0);
     (*result)|= 512;
   }
 }
 if(flag&2) {
   if(s1.st_ctime != s2.st_ctime) {
     sprintf(respt, "%s st_ctime :  %s  <>  %s      diff= %.f s\n",
             a, Ftimetxt(s1.st_ctime, ttx1, 0),
                Ftimetxt(s2.st_ctime, ttx2, 0),
                ((double) s1.st_ctime) - (double) s2.st_ctime);
     if(!(flag&(1<<31)))
       Xorriso_result(xorriso,0);
     (*result)|= 1024;
   }
 }
 if(xorriso->isofs_st_in > 0 &&
    (xorriso->isofs_st_in <= s2.st_mtime ||
     ((flag & 1) && xorriso->isofs_st_in <= s2.st_atime) ||
     ((flag & 2) && xorriso->isofs_st_in <= s2.st_ctime)))
   (*result)|= 1 << 23;

 if((xorriso->do_aaip & 32) || !(xorriso->ino_behavior & 2)) {
   /* dev,inode comparison.
      For skipping content comparison or for hardlink detection.
   */
   ret= Xorriso_record_dev_inode(xorriso, "", s1.st_dev, s1.st_ino, NULL,
       iso_adr, 1 | 2 | ((flag & (1 << 28)) >> 23) | (xorriso->do_aaip & 128));
   if(ret < 0) {
     ret= -1; goto ex;
   } else if(ret == 0) { /* match */
     if((xorriso->do_aaip & 64) && S_ISREG(s1.st_mode) && S_ISREG(s2.st_mode)){
       if(xorriso->do_aaip & 32)
         content_shortcut= 1;
       if((*result) & (8 | 128 | 256 | 512 | 1024 | (1 << 23))) {
         (*result)|= (1 << 15); /* content bytes differ */
         if(((*result) & (1 << 23)) &&
            !((*result) & (8 | 128 | 256 | 512 | 1024))) {
           sprintf(respt,
            "%s content  :  node timestamp younger than image timestamp\n", a);
           if((xorriso->do_aaip & 32) && !(flag&(1<<31)))
             Xorriso_result(xorriso,0);
           stamp= s2.st_mtime;
           if((flag & 1) && s2.st_atime >= stamp)
             stamp= s2.st_atime;
           if((flag & 2) && s2.st_ctime >= stamp)
             stamp= s2.st_ctime;
           sprintf(respt, "%s content  :  %s  >  %s    diff= %.f s\n",
                   a, Ftimetxt(stamp, ttx1, 3 << 1),
                   Ftimetxt(xorriso->isofs_st_in, ttx2, 3 << 1),
                   ((double) stamp) - (double) xorriso->isofs_st_in);
           if((xorriso->do_aaip & 32) && !(flag&(1<<31)))
             Xorriso_result(xorriso,0);
         }
         sprintf(respt,
          "%s content  :  assuming inequality due to size or timestamps\n", a);
         if((xorriso->do_aaip & 32) && !(flag&(1<<31)))
           Xorriso_result(xorriso,0);
       }
     }
   } else if(ret == 1) { /* mismatch */
     (*result)|= (1 << 21);
     sprintf(respt, "%s dev_ino  :  differing\n", a);
     if((xorriso->do_aaip & 32) && !(flag&(1<<31)))
       Xorriso_result(xorriso,0);

     if((xorriso->do_aaip & 64) && S_ISREG(s1.st_mode) && S_ISREG(s2.st_mode)){
       if(xorriso->do_aaip & 32)
         content_shortcut= 1;
       (*result)|= (1 << 15); /* content bytes differ */
       sprintf(respt,
             "%s content  :  assuming inequality after dev_ino mismatch\n", a);
       if((xorriso->do_aaip & 32) && !(flag&(1<<31)))
         Xorriso_result(xorriso,0);
     }
   } else {
     sprintf(respt, "%s dev_ino  :  no dev_ino stored with image node\n", a);
     if((xorriso->do_aaip & 32) && !(flag&(1<<31)))
       Xorriso_result(xorriso,0);
     (*result)|= (1 << 22);
   }
 }

 if(S_ISREG(s1.st_mode) && S_ISREG(s2.st_mode) && !content_shortcut) {
   /* Content */
   if(is_split) { 
     for(i= 0; i<split_count; i++) {
       Splitparts_get(split_parts, i, &part_name, &partno, &total_parts,
                      &offset, &bytes, &total_bytes, 0);
       strcpy(part_path, iso_adr);
       if(Sfile_add_to_path(part_path, part_name, 0)<=0) {
         Xorriso_much_too_long(xorriso, strlen(iso_adr)+strlen(part_name)+1,
                               2);
         {ret= -1; goto ex;}
       }
       ret= Xorriso_iso_lstat(xorriso, part_path, &stbuf, 0);
       if(ret<0)
     continue;
       ret= Xorriso_compare_2_contents(xorriso, a, disk_adr, s1.st_size,
                                       offset, bytes,
                                       part_path, stbuf.st_size, result,
                        (s1.st_mtime==s2.st_mtime) | (flag&((1<<29)|(1<<31))));
       if(ret<0)
         was_error= 1;
     }
     if(total_parts>0 && split_count!=total_parts) {
       sprintf(xorriso->info_text,
               "- %s/* (ISO) : Not all split parts present (%d of %d)\n",
               iso_adr, split_count, total_parts);
       if(!(flag&(1<<31)))
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 1);
       (*result)|= 1<<18;
     }
   } else {
     ret= Xorriso_compare_2_contents(xorriso, a, disk_adr, s1.st_size,
                                     (off_t) 0, s1.st_size,
                                     iso_adr, s2.st_size, result,
                        (s1.st_mtime==s2.st_mtime) | (flag&((1<<29)|(1<<31))));
     if(ret<0)
       was_error= 1;
   }

 }
 if(was_error)
   ret= -1;
 else {
   mask= ~((1 << 17) | (1 << 18) | (1 << 22) | (1 << 23));
   if(xorriso->do_aaip & 32)
     mask|= 1 << 22;
   ret= (((*result) & mask)==0);
 }
ex:;
 if(split_parts!=NULL)
   Splitparts_destroy(&split_parts, split_count, 0);
 Xorriso_local_getfacl(xorriso, disk_adr, &a1_acl, 1 << 15);
 Xorriso_local_getfacl(xorriso, disk_adr, &d1_acl, 1 << 15);
 if(a2_acl != NULL)
   free(a2_acl);
 if(d2_acl != NULL)
   free(d2_acl);
 Xorriso_free_meM(part_path);
 Xorriso_free_meM(a);
 return(ret);
}


int Xorriso_pfx_disk_path(struct XorrisO *xorriso, char *iso_path,
                          char *iso_prefix, char *disk_prefix,
                          char disk_path[SfileadrL], int flag)
{
 int ret;
 char *adrc= NULL;

 Xorriso_alloc_meM(adrc, char, SfileadrL);

 if(strncmp(iso_path, iso_prefix, strlen(iso_prefix))!=0)
   {ret= -1; goto ex;}
 if(strlen(disk_prefix) + strlen(iso_path) - strlen(iso_prefix)+1 >= SfileadrL)
   {ret= -1; goto ex;}
 if(iso_path[strlen(iso_prefix)] == '/')
   strcpy(adrc, iso_path + strlen(iso_prefix) + 1);
 else
   strcpy(adrc, iso_path + strlen(iso_prefix));
 ret= Xorriso_make_abs_adr(xorriso, disk_prefix, adrc, disk_path, 4 | 8);
 if(ret <= 0)
   goto ex;
 ret= 1;
ex:;
 Xorriso_free_meM(adrc);
 return(ret);
}


/* @param boss_iter Opaque handle to be forwarded to actions in ISO image
                    Set to NULL if calling this function from outside ISO world
   @param flag bit0= update rather than compare
               bit1= find[ix] is in recursion
               bit2= update_merge : do not delete but mark visited and found
   @return <=0 error, 1= ok , 2= iso_path was deleted
             3=ok, do not dive into directory (e.g. because it is a split file)
*/
int Xorriso_find_compare(struct XorrisO *xorriso, void *boss_iter, void *node,
                         char *iso_path, char *iso_prefix, char *disk_prefix,
                         int flag)
{
 int ret, result, uret, follow_links, deleted= 0;
 char *disk_path= NULL;

 Xorriso_alloc_meM(disk_path, char, SfileadrL);

 ret= Xorriso_pfx_disk_path(xorriso, iso_path, iso_prefix, disk_prefix,
                            disk_path, 0);
 if(ret <= 0)
   goto ex;

 /* compare exclusions against disk_path resp. leaf name */
 if(xorriso->disk_excl_mode&8)
   ret= Xorriso_path_is_excluded(xorriso, disk_path, !(flag&2));
 else
   ret= 0;
 if(ret<0)
   goto ex;
 if(ret>0)
   {ret= 3; goto ex;}

 follow_links= (xorriso->do_follow_links ||
               (xorriso->do_follow_param && !(flag&2))) <<28;
 ret= Xorriso_compare_2_files(xorriso, disk_path, iso_path, "", &result,
                        2 | follow_links | ((!!(flag & 4)) << 26)
                        | ((!(flag&2))<<27) | ((flag&1)<<31));
                                            /* was once: | ((!(flag&1))<<29) */
 if(ret<xorriso->find_compare_result)
   xorriso->find_compare_result= ret;
 if(flag&1) {
   if(ret<0)
     if(Xorriso_eval_problem_status(xorriso, ret, 1|2)<0)
       goto ex;
   if(ret > 0)
     result= 0;
   uret= Xorriso_update_interpreter(xorriso, boss_iter, node, result,
                                    disk_path, iso_path,
                                    ((flag & 2) << 1) | ((flag & 4) >> 1));
   if(uret<=0)
     ret= 0;
   if(uret==2)
     deleted= 1;
 }
 if(ret<0)
   goto ex;
 if(deleted)
   {ret= 2; goto ex;}
 if(result&(1<<17))
   {ret= 3; goto ex;}
ex:;
 Xorriso_free_meM(disk_path);
 return(ret);
}


/* @param boss_iter Opaque handle to be forwarded to actions in ISO image
                    Set to NULL if calling this function from outside ISO world
   @param flag      bit0= widen hardlink sibling:
                          Do not call Xorriso_hardlink_update()
                          Overwrite exactly if normal mode would not,
                          else do nothing
                    bit1= do not delete files which are not found under
                          disk_path, but rather mark visited files and mark
                          files which were found.
                    bit2= -follow: this is not a command parameter 
   @return <=0 error, 1= ok , 2= iso_rr_path node object has been deleted ,
                      3= no action taken
*/
int Xorriso_update_interpreter(struct XorrisO *xorriso,
                               void *boss_iter, void *node,
                               int compare_result, char *disk_path,
                               char *iso_rr_path, int flag)
{
 int ret= 1, deleted= 0, is_split= 0, i, loop_count, late_hardlink_update= 0;
 struct stat stbuf;
 struct SplitparT *split_parts= NULL;
 int split_count= 0;
 char *part_path= NULL, *part_name;
 int partno, total_parts, new_total_parts;
 off_t offset, bytes, total_bytes, disk_size, first_bytes;

 if((compare_result&3)==3) {
   sprintf(xorriso->info_text, "Missing on disk and in ISO: disk_path ");
   Text_shellsafe(disk_path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 1);
   xorriso->find_compare_result= -1;
   ret= 3; goto ex;
 }

 Xorriso_alloc_meM(part_path, char, SfileadrL);

 if((flag & 2) && !(compare_result & 2)) {
   ret= Xorriso_mark_update_merge(xorriso, iso_rr_path, node,
                                  !(compare_result & 1));
   if(ret <= 0)
     goto ex;
 }
 if(compare_result == 0)
   {ret= 1; goto ex;}

 if(compare_result&((1<<11)|(1<<13))) {
   if(flag & 1)
     {ret= 3; goto ex;}
   /* cannot open regular disk file, early eof of disk file */
   sprintf(xorriso->info_text, "Problems with reading disk file ");
   Text_shellsafe(disk_path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 1);
   xorriso->find_compare_result= -1;
   ret= 1; goto ex;
 }
 xorriso->info_text[0]= 0;
 is_split= !!(compare_result & (1<<17));

 if((!(xorriso->ino_behavior & 2)) && (compare_result & (2 | (3 << 21))) &&
    !(flag & 1)) {
   if(compare_result & 2) {
     /* File is not yet in image */
     late_hardlink_update= 1;
   } else {
     /* Hard link relation has changed resp. was not recorded. */
     ret= Xorriso_hardlink_update(xorriso, &compare_result,
                            disk_path, iso_rr_path,
                            (flag & 4) | ((compare_result >> 21) & 2));
     if(ret < 0)
       goto ex;
     if(ret == 2)
       {ret= 1; goto ex;}
   }
 }

 if(compare_result&(8|64)) {
   /* file type, minor+major with device file */
   if(flag & 1)
     {ret= 3; goto ex;}
   ret= Xorriso_rmi(xorriso, boss_iter, (off_t) 0, iso_rr_path, 1); /* rm_r */
   if(ret>0) {
     deleted= 1; 
     ret= Xorriso_graft_in(xorriso, boss_iter, disk_path, iso_rr_path,
                           (off_t) 0, (off_t) 0, 2|(flag&4));
     if(ret <= 0)
       goto ex;
     if(flag & 2) {
       ret= Xorriso_mark_update_merge(xorriso, iso_rr_path, NULL, 1);
       if(ret <= 0)
         goto ex;
     }
   }
   sprintf(xorriso->info_text, "Deleted and re-added ");

 } else if(compare_result&(1)) {
delete:;
   /* disk_adr not existing */
   if(!(flag & 2)) {
     ret= Xorriso_rmi(xorriso, boss_iter, (off_t) 0, iso_rr_path, 1);
     deleted= 1;
     sprintf(xorriso->info_text, "Deleted ");
   }

 } else if(compare_result&(2|128|(1<<12)|(1<<14)|(1<<15))) {
   /* iso_adr not existing, size, cannot open iso file, early eof of iso file
      content bytes differ */

   if(flag & 1)
     {ret= 3; goto ex;}
overwrite:;
   if(is_split) {
     ret= Xorriso_identify_split(xorriso, iso_rr_path, NULL,
                                 &split_parts, &split_count, &stbuf, 0);
     if(ret<=0)
       {ret= -1; goto ex;}  /* (should not happen) */
     ret= lstat(disk_path, &stbuf);
     if(ret==-1)
       goto delete;
     disk_size= stbuf.st_size;
     Splitparts_get(split_parts, 0, &part_name, &partno, &total_parts,
                    &offset, &first_bytes, &total_bytes, 0);
     new_total_parts= disk_size/first_bytes;
     if(disk_size % first_bytes)
       new_total_parts++;

     loop_count= split_count;
     /* If disk file grew over part limit and all parts are present:
        add new parts */
     if(new_total_parts > total_parts && split_count == total_parts)
       loop_count= new_total_parts;

     for(i= 0; i<loop_count; i++) {
       if(i<split_count) {
         /* Delete old part */
         Splitparts_get(split_parts, i, &part_name, &partno, &total_parts,
                       &offset, &bytes, &total_bytes, 0);
         strcpy(part_path, iso_rr_path);
         if(Sfile_add_to_path(part_path, part_name, 0)<=0) {
           Xorriso_much_too_long(xorriso,
                                 strlen(iso_rr_path)+strlen(part_path)+1, 2);
           {ret= -1; goto ex;}
         }
         ret= Xorriso_rmi(xorriso, NULL, (off_t) 0, part_path, 1);
         if(ret<=0)
           goto ex;
         deleted= 1;
       } else {
         partno= i+1;
         offset= i*first_bytes;
         bytes= first_bytes;
       }
       if(disk_size<=offset)
     continue;
       /* Insert new part */
       if(strlen(part_path)+160>SfileadrL) {
         Xorriso_much_too_long(xorriso, strlen(part_path)+160, 2);
         ret= 0; goto ex;
       }
       Splitpart__compose(part_path+strlen(iso_rr_path)+1, partno,
                          new_total_parts, offset, first_bytes, disk_size, 0);
       ret= Xorriso_graft_in(xorriso, boss_iter, disk_path, part_path,
                             offset, bytes, 2|(flag&4)|8|128);
       if(ret<=0)
         goto ex;
     }
     /* Copy file attributes to iso_rr_path, augment r-perms by x-perms */
     ret= Xorriso_copy_properties(xorriso, disk_path, iso_rr_path, 2 | 4);
     if(ret<=0)
       goto ex;
   } else {
     ret= Xorriso_graft_in(xorriso, boss_iter, disk_path, iso_rr_path,
                           (off_t) 0, (off_t) 0, 2|(flag&4));
     if(ret>0 && !(compare_result&2))
       deleted= 1;
   }
   if(late_hardlink_update) {
     /* Handle eventual hardlink siblings of newly created file */
     ret= Xorriso_hardlink_update(xorriso, &compare_result,
                                  disk_path, iso_rr_path, 1 | (flag & 4));
     if(ret < 0)
       goto ex;
   }
   if(flag & 2) {
     ret= Xorriso_mark_update_merge(xorriso, iso_rr_path, NULL, 1);
     if(ret <= 0)
       goto ex;
   }
   if(flag & 1)
     sprintf(xorriso->info_text, "Widened hard link ");
   else
     sprintf(xorriso->info_text, "Added/overwrote ");

 } else if(compare_result&(4|16|32|256|512|1024|(1<<19)|(1<<20)|(1<<22))) {
   /* access permissions, user id, group id, mtime, atime, ctime, ACL, xattr,
      dev_ino missing */

   if(flag & 1)
     goto overwrite;

   if(is_split) {
     ret= Xorriso_identify_split(xorriso, iso_rr_path, NULL,
                                 &split_parts, &split_count, &stbuf, 0);
     if(ret<=0)
       {ret= -1; goto ex;}  /* (should not happen) */
     for(i= 0; i<split_count; i++) {
       Splitparts_get(split_parts, i, &part_name, &partno, &total_parts,
                     &offset, &bytes, &total_bytes, 0);
       strcpy(part_path, iso_rr_path);
       if(Sfile_add_to_path(part_path, part_name, 0)<=0) {
         Xorriso_much_too_long(xorriso,
                               strlen(iso_rr_path)+strlen(part_path)+1, 2);
         {ret= -1; goto ex;}
       }
       ret= Xorriso_copy_properties(xorriso, disk_path, part_path,
                                    4 * !(compare_result & (1<<21))); 
                             /* do not update eventually mismatching dev_ino */
       if(ret<=0)
         goto ex;
     }
     /* Copy file attributes to iso_rr_path, augment r-perms by x-perms */
     ret= Xorriso_copy_properties(xorriso, disk_path, iso_rr_path, 2 | 4); 
     if(ret<=0)
       goto ex;
   } else
     ret= Xorriso_copy_properties(xorriso, disk_path, iso_rr_path, 4); 
   sprintf(xorriso->info_text, "Adjusted attributes of ");

 } else if(flag & 1) {
     goto overwrite;
 } else
   ret= 1;
 if(ret>0 && xorriso->info_text[0]) {
   Text_shellsafe(iso_rr_path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "UPDATE", 0);
 }
 ret= 1;
ex:;
 if(split_parts!=NULL)
   Splitparts_destroy(&split_parts, split_count, 0);
 Xorriso_free_meM(part_path);
 if(ret<=0)
   return(ret);
 if(deleted)
   return(2);
 return(ret);
}

