
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2013 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the implementation of options as mentioned in man page
   or info file derived from xorriso.texi.
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
#include <errno.h>


#include "xorriso.h"
#include "xorriso_private.h"
#include "xorrisoburn.h"


/* Command -data_cache_size */
int Xorriso_option_data_cache_size(struct XorrisO *xorriso, char *num_tiles,
                                   char *tile_blocks, int flag)
{
 int ret, blocks= -1, tiles= -1, to_default= 0;

 sscanf(num_tiles, "%d", &tiles);
 sscanf(tile_blocks, "%d", &blocks);
 if(strcmp(num_tiles, "default") == 0 || num_tiles[0] == 0)
   to_default|= 1;
 if(strcmp(tile_blocks, "default") == 0 || tile_blocks[0] == 0)
   to_default|= 2;
 ret= Xorriso_set_data_cache(xorriso, NULL, tiles, blocks, to_default);
 if(ret > 0) {
   xorriso->cache_num_tiles= tiles;
   xorriso->cache_tile_blocks= blocks;
   xorriso->cache_default= to_default;
 }
 return(ret);
}


/* Options -dev , -indev, -outdev */
/** @param flag bit0= use as indev
                bit1= use as outdev
                bit2= do not -reassure
                bit3= regard overwriteable media as blank
                bit4= if the drive is a regular disk file: truncate it to
                      the write start address
                bit5= do not print toc of aquired drive
                bit6= do not calm down drive after aquiring it
    @return <=0 error , 1 success, 2 revoked by -reassure
*/
int Xorriso_option_dev(struct XorrisO *xorriso, char *in_adr, int flag)
{
 int ret;
 char *adr;

 adr= in_adr;
 if(strcmp(in_adr, "-")==0)
   adr= "stdio:/dev/fd/1";
 if(strncmp(adr, "stdio:", 6)==0) {
   if(strlen(adr)==6 || strcmp(adr, "stdio:/")==0 ||
      strcmp(adr, "stdio:.")==0 || strcmp(adr, "stdio:..")==0 ||
      strcmp(adr, "stdio:-")==0) {
     sprintf(xorriso->info_text,
             "No suitable path given by device address '%s'", adr);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
 }

 if(Xorriso_change_is_pending(xorriso, 0) && (flag&1)) {
   sprintf(xorriso->info_text,
           "%s: Image changes pending. -commit or -rollback first",
           (flag&2) ? "-dev" : "-indev");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 if((flag&1) && (xorriso->in_drive_handle != NULL || adr[0]) && !(flag&4)) {
   ret= Xorriso_reassure(xorriso, (flag&2) ? "-dev" : "-indev",
                         "eventually discard the current image", 0);
   if(ret<=0)
     return(2);
 }

 if(adr[0]==0) {
   if((flag&1) && xorriso->in_drive_handle != NULL) {
     if(xorriso->in_drive_handle == xorriso->out_drive_handle)
       sprintf(xorriso->info_text,"Giving up -dev ");
     else
       sprintf(xorriso->info_text,"Giving up -indev ");
     Text_shellsafe(xorriso->indev, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   }
   if((flag&2) && xorriso->out_drive_handle != NULL &&
      xorriso->in_drive_handle != xorriso->out_drive_handle) {
     sprintf(xorriso->info_text,"Giving up -outdev ");
     Text_shellsafe(xorriso->outdev, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   }
   ret= Xorriso_give_up_drive(xorriso, (flag&3)|((flag&32)>>2));
 } else
   ret= Xorriso_aquire_drive(xorriso, adr, NULL,
                          (flag & (3 | 32 | 64)) | (((flag & (8 | 16)) >> 1))); 
 if(ret<=0)
   return(ret);
 if(xorriso->in_drive_handle == NULL)
   xorriso->image_start_mode= 0; /* session setting is invalid by now */
 return(1);
}


/* Option -devices , -device_links */
/* @param flag bit0= perform -device_links rather than -devices
   @return <=0 error , 1 success, 2 revoked by -reassure
*/
int Xorriso_option_devices(struct XorrisO *xorriso, int flag)
{
 int ret;

 if(Xorriso_change_is_pending(xorriso, 0)) {
   sprintf(xorriso->info_text,
           "-devices: Image changes pending. -commit or -rollback first");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 ret= Xorriso_reassure(xorriso, "-devices",
                       "eventually discard the current image", 0);
 if(ret<=0)
   return(2);
 xorriso->info_text[0]= 0;
 if(xorriso->in_drive_handle!=NULL || xorriso->out_drive_handle!=NULL) {
   if(xorriso->in_drive_handle == xorriso->out_drive_handle) {
     sprintf(xorriso->info_text, "Gave up -dev "); 
     Text_shellsafe(xorriso->indev, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   }else {
     if(xorriso->in_drive_handle!=NULL) {
       sprintf(xorriso->info_text, "Gave up -indev ");
       Text_shellsafe(xorriso->indev, xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
     }
     if(xorriso->out_drive_handle!=NULL) {
       sprintf(xorriso->info_text, "Gave up -outdev ");
       Text_shellsafe(xorriso->outdev, xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
     }
   }
   Xorriso_give_up_drive(xorriso, 3);
 }
 ret= Xorriso_show_devices(xorriso, flag & 1);
 return(ret);
}


/* Option -dialog "on"|"single_line"|"off" */
int Xorriso_option_dialog(struct XorrisO *xorriso, char *mode, int flag)
{
 if(strcmp(mode, "on") == 0 || strcmp(mode, "multi_line") == 0)
   xorriso->dialog= 2;
 else if(strcmp(mode, "single_line") == 0)
   xorriso->dialog= 1;
 else if(strcmp(mode, "off") == 0)
   xorriso->dialog= 0;
 else {
   sprintf(xorriso->info_text, "-dialog: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 return(1);
}


/* Option -disk_dev_ino "on"|"ino_only"|"off" */
int Xorriso_option_disk_dev_ino(struct XorrisO *xorriso, char *mode, int flag)
{
 if(strcmp(mode, "on") == 0)
   xorriso->do_aaip= (xorriso->do_aaip & ~128) | 16 | 32 | 64;
 else if(strcmp(mode, "ino_only") == 0)
   xorriso->do_aaip|= 16 | 32 | 64 | 128;
 else if(strcmp(mode, "off") == 0)
   xorriso->do_aaip &= ~(16 | 32 | 64 | 128);
 else {
   sprintf(xorriso->info_text, "-disk_dev_ino: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 return(1);
}


/* Option -disk_pattern "on"|"ls"|"off" */
int Xorriso_option_disk_pattern(struct XorrisO *xorriso, char *mode, int flag)
{
 if(strcmp(mode, "off")==0)
   xorriso->do_disk_pattern= 0;
 else if(strcmp(mode, "on")==0)
   xorriso->do_disk_pattern= 1;
 else if(strcmp(mode, "ls")==0)
   xorriso->do_disk_pattern= 2;
 else {
   sprintf(xorriso->info_text, "-disk_pattern: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(1);
}


/* Option -displacement [-]offset */
int Xorriso_option_displacement(struct XorrisO *xorriso, char *value, int flag)
{
 double num;
 int displacement_sign= 1, l;
 char *cpt;

 cpt= value;
 if(value[0] == '-') {
   displacement_sign= -1;
   cpt++;
 } else if(value[0] == '+')
   cpt++;
 num= Scanf_io_size(cpt, 0);
 l= strlen(cpt);
 if(cpt[l - 1] < '0' || cpt[l - 1] > '9')
   num/= 2048.0;
 if(num < 0.0 || num > 4294967295.0) {
   sprintf(xorriso->info_text,
           "-displacement: too large or too small: '%s'", value);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 if(num == 0)
   displacement_sign= 0;
 xorriso->displacement= num;
 xorriso->displacement_sign= displacement_sign;
 return(1);
}


/* Option -drive_class */
int Xorriso_option_drive_class(struct XorrisO *xorriso,
                               char *d_class, char *pattern, int flag)
{
 int ret= 1;

 if(strcmp(d_class, "banned") == 0) {
   ret= Xorriso_lst_new(&(xorriso->drive_blacklist), pattern,
                          xorriso->drive_blacklist, 1);
 } else if(strcmp(d_class, "caution") == 0) {
   ret= Xorriso_lst_new(&(xorriso->drive_greylist), pattern,
                          xorriso->drive_greylist, 1);
 } else if (strcmp(d_class, "harmless") == 0) {
   ret= Xorriso_lst_new(&(xorriso->drive_whitelist), pattern,
                        xorriso->drive_whitelist, 1);
 } else if (strcmp(d_class, "clear_list") == 0) {
   if(strcmp(pattern, "banned") == 0)
     Xorriso_lst_destroy_all(&(xorriso->drive_blacklist), 0);
   else if(strcmp(pattern, "caution") == 0)
     Xorriso_lst_destroy_all(&(xorriso->drive_greylist), 0);
   else if(strcmp(pattern, "harmless") == 0)
     Xorriso_lst_destroy_all(&(xorriso->drive_whitelist), 0);
   else if(strcmp(pattern, "all") == 0) {
     Xorriso_lst_destroy_all(&(xorriso->drive_blacklist), 0);
     Xorriso_lst_destroy_all(&(xorriso->drive_greylist), 0);
     Xorriso_lst_destroy_all(&(xorriso->drive_whitelist), 0);
   } else {
     sprintf(xorriso->info_text, "-drive_class clear : unknown class '%s'",
             pattern);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
   ret= 1;
 } else {
   sprintf(xorriso->info_text, "-drive_class: unknown class '%s'", d_class);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(ret);
}


/* Option -dummy "on"|"off" */
int Xorriso_option_dummy(struct XorrisO *xorriso, char *mode, int flag)
{
 xorriso->do_dummy= !!strcmp(mode, "off");
 return(1);
}


/* Option -dvd_obs "default"|"32k"|"64k" */
int Xorriso_option_dvd_obs(struct XorrisO *xorriso, char *obs, int flag)
{
 double num;

 if(strcmp(obs, "default") == 0)
   num= 0;
 else
   num = Scanf_io_size(obs,0);
 if(num != 0 && num != 32768 && num != 65536) {
   sprintf(xorriso->info_text,
           "-dvd_obs : Bad size. Acceptable are 0, 32k, 64k");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 } else
   xorriso->dvd_obs= num;
 return(1);
}


/* Option -early_stdio_test */
int Xorriso_option_early_stdio_test(struct XorrisO *xorriso, char *mode,
                                    int flag)
{
 if(strcmp(mode, "on") == 0)
   xorriso->early_stdio_test= 2 | 4;
 else if(strcmp(mode, "off") == 0)
   xorriso->early_stdio_test= 0;
 else if(strcmp(mode, "appendable_wo") == 0)
   xorriso->early_stdio_test= 2 | 4 | 8;
 else {
   sprintf(xorriso->info_text, "-early_stdio_test: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 return(1);
}


/* Option -eject */
/* @param flag bit0=do not report toc of eventually remaining drives
*/
int Xorriso_option_eject(struct XorrisO *xorriso, char *which, int flag)
{
 int gu_flag= 4, ret;

 if(strncmp(which,"in",2)==0)
   gu_flag|= 1;
 else if(strncmp(which,"out",3)==0)
   gu_flag|= 2;
 else
   gu_flag|= 3;
 if((gu_flag&1) && Xorriso_change_is_pending(xorriso, 0)) {
   sprintf(xorriso->info_text,
           "-eject: Image changes pending. -commit or -rollback first");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 if(flag&1)
   gu_flag|= 8;
 ret= Xorriso_give_up_drive(xorriso, gu_flag);
 return(ret);
}


/* Options -end , and -rollback_end */
/* @param flag bit0= discard pending changes
               bit1= do not -reassure
   @return <=0 error , 1 success, 2 revoked by -reassure
*/
int Xorriso_option_end(struct XorrisO *xorriso, int flag)
{
 int ret;
 char *cmd, *which_will;
 
 if(flag&1)
   cmd= "-rollback_end";
 else
   cmd= "-end";
 if(Xorriso_change_is_pending(xorriso, 0)) {
   if((flag & 1) || !Xorriso_change_is_pending(xorriso, 1))
     which_will= "end the program discarding image changes";
   else
     which_will= "commit image changes and then end the program";
 } else {
   which_will= "end the program";
 }
 if(!(flag&2)) {
   ret= Xorriso_reassure(xorriso, cmd, which_will, 0);
   if(ret<=0)
     return(2);
 }

 if(Xorriso_change_is_pending(xorriso, 0)) {
   if((flag & 1) || !Xorriso_change_is_pending(xorriso, 1)) {
     xorriso->volset_change_pending= 0;
   } else {
     ret= Xorriso_option_commit(xorriso, 1);
     xorriso->volset_change_pending= 0; /* no further tries to commit */
     if(ret<=0)
       return(ret);
   }
 }
 ret= Xorriso_give_up_drive(xorriso, 3);
 if(ret<=0)
   return(ret);
 return(1);
}


/* Option -errfile_log marked|plain  path|-|"" */
int Xorriso_option_errfile_log(struct XorrisO *xorriso,
                               char *mode, char *path, int flag)
{
 int ret, mode_word;
 FILE *fp= NULL;

 if(path[0]==0 || path[0]=='-') {
   /* ok */;
 } else {
   fp= fopen(path, "a");
   if(fp==0) {
     sprintf(xorriso->info_text, "-errfile_log: Cannot open file ");
     Text_shellsafe(path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
 }
 mode_word= xorriso->errfile_mode;
 if(strcmp(mode, "marked")==0)
   mode_word|= 1;
 else if(strcmp(mode, "plain")==0)
   mode_word&= ~1;
 else {
   sprintf(xorriso->info_text, "-errfile_log: Unknown mode ");
   Text_shellsafe(mode, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   if(fp != NULL)
     fclose(fp);
   return(0);
 }
 
 Xorriso_process_errfile(xorriso, 0, "log end", 0, 1);
 if(xorriso->errfile_fp!=NULL)
   fclose(xorriso->errfile_fp);
 xorriso->errfile_fp= fp;
 xorriso->errfile_mode= mode_word;
 ret= Sfile_str(xorriso->errfile_log, path, 0);
 if(ret>0)
   ret= Xorriso_process_errfile(xorriso, 0, "log start", 0, 1);
 if(ret<=0)
   return(ret);
 return(1);
}


/* Option -error_behavior */
int Xorriso_option_error_behavior(struct XorrisO *xorriso,
                                  char *occasion, char *behavior, int flag)
{
 if(strcmp(occasion, "image_loading")==0) {
   if(strcmp(behavior, "best_effort")==0)
     xorriso->img_read_error_mode= 0;
   else if(strcmp(behavior, "failure")==0 || strcmp(behavior, "FAILURE")==0)
     xorriso->img_read_error_mode= 1;
   else if(strcmp(behavior, "fatal")==0 || strcmp(behavior, "FATAL")==0)
     xorriso->img_read_error_mode= 2;
   else {
unknown_behavior:;
     sprintf(xorriso->info_text,
             "-error_behavior: with '%s': unknown behavior '%s'",
             occasion, behavior);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
 } else if(strcmp(occasion, "file_extraction")==0) {
   if(strcmp(behavior, "best_effort")==0)
     xorriso->extract_error_mode= 0;
   else if(strcmp(behavior, "keep")==0)
     xorriso->extract_error_mode= 1;
   else if(strcmp(behavior, "delete")==0)
     xorriso->extract_error_mode= 2;
   else
     goto unknown_behavior;
 } else {
   sprintf(xorriso->info_text, "-error_behavior: unknown occasion '%s'",
           occasion);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(1);
}


/* Option -external_filter */
int Xorriso_option_external_filter(struct XorrisO *xorriso,
                                   int argc, char **argv, int *idx, int flag)
{
 int ret, start_idx, end_idx;

 start_idx= *idx;
 end_idx= Xorriso_end_idx(xorriso, argc, argv, start_idx, 1);
 (*idx)= end_idx;
 if(end_idx - start_idx < 3) {
   sprintf(xorriso->info_text,
"-external_filter : Not enough parameters given. Needed: name options path %s",
           xorriso->list_delimiter);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 ret= Xorriso_external_filter(xorriso, argv[start_idx],
                             argv[start_idx + 1], argv[start_idx + 2],
                             end_idx - start_idx - 3, argv + start_idx + 3, 0);
 return(ret);
}


/* Options -extract , -extract_single */
/* @param flag bit0=do not report the restored item
               bit1=do not reset pacifier, no final pacifier message
               bit2= do not make lba-sorted node array for hardlink detection
               bit5= -extract_single: eventually do not insert directory tree
*/
int Xorriso_option_extract(struct XorrisO *xorriso, char *iso_path,
                           char *disk_path, int flag)
{
 int ret, problem_count;
 char *eff_origin= NULL, *eff_dest= NULL, *ipth, *eopt[1], *edpt[1];

 Xorriso_alloc_meM(eff_origin, char, SfileadrL);
 Xorriso_alloc_meM(eff_dest, char, SfileadrL);

 if(xorriso->allow_restore <= 0) {
   sprintf(xorriso->info_text,
          "-extract: image-to-disk copies are not enabled by option -osirrox");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   ret= 0; goto ex;
 }
 if(!(flag&2))
   Xorriso_pacifier_reset(xorriso, 0);

 ipth= iso_path;
 if(ipth[0]==0)
   ipth= disk_path;
 if(disk_path[0]==0) {
   sprintf(xorriso->info_text, "-extract: Empty disk_path given");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 1);
   ret= 0; goto ex;
 }
 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdx, disk_path, eff_dest,
                                 2|4);
 if(ret<=0)
   goto ex;
 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, ipth, eff_origin, 2|8);
 if(ret<=0)
   goto ex;

 eopt[0]= eff_origin;
 edpt[0]= eff_dest;
 ret= Xorriso_restore_sorted(xorriso, 1, eopt, edpt, &problem_count,
                             (flag & 32 ? 33 : 0));

 if(!(flag&2))
   Xorriso_pacifier_callback(xorriso, "files restored",xorriso->pacifier_count,
                             xorriso->pacifier_total, "", 1 | 4 | 8 | 32);
 if(ret <= 0 || problem_count > 0)
   goto ex;

 if(!(flag&1)) {
   sprintf(xorriso->info_text, "Extracted from ISO image: %s '%s'='%s'\n",
           (ret>1 ? "directory" : "file"), eff_origin, eff_dest);
   Xorriso_info(xorriso,0);
 }
 ret= 1;
ex:;
 if(!(flag & (4 | 32)))
   Xorriso_destroy_node_array(xorriso, 0);
 Xorriso_free_meM(eff_origin);
 Xorriso_free_meM(eff_dest);
 return(ret);
}


/* Option -extract_cut */
int Xorriso_option_extract_cut(struct XorrisO *xorriso, char *iso_rr_path,
                           char *start, char *count, char *disk_path, int flag)
{
 int ret;
 double num;
 off_t startbyte, bytecount;
 
 num= Scanf_io_size(start, 0);
 if(num<0 || num > 1.0e18) { /* 10^18 = 10^3 ^ 6 < 2^10 ^ 6 = 2^60 */
   sprintf(xorriso->info_text,
           "-extract_cut: startbyte address negative or much too large (%s)",
           start);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 startbyte= num;
 num= Scanf_io_size(count, 0);
 if(num<=0 || num > 1.0e18) {
   sprintf(xorriso->info_text,
           "-extract_cut: bytecount zero, negative or much too large (%s)",
           count);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 bytecount= num;
 sprintf(xorriso->info_text, 
         "-extract_cut from %s , byte %.f to %.f, and store as %s",
         iso_rr_path, (double) startbyte, (double) (startbyte+bytecount),
         disk_path);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);

 ret= Xorriso_extract_cut(xorriso, iso_rr_path, disk_path,
                          startbyte, bytecount, 0); 
 return(ret);
}


/* Option -file_size_limit */
int Xorriso_option_file_size_limit(struct XorrisO *xorriso,
                                   int argc, char **argv, int *idx, int flag)
{
 int ret, i, end_idx;
 off_t new_limit= 0;

 end_idx= Xorriso_end_idx(xorriso, argc, argv, *idx, 1);
 if(*idx >= end_idx)
   {ret= 2; goto ex;}
 if(*idx + 1 == end_idx && strcmp(argv[*idx], "off") == 0) {
   xorriso->file_size_limit= 0;
   ret= 1; goto ex;
 }
 for(i= *idx; i < end_idx; i++)
   new_limit+= Scanf_io_size(argv[i], 0);
 if(new_limit <= 0) {
   sprintf(xorriso->info_text, "-file_size_limit: values sum up to %.f",
           (double) new_limit);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 1);
   ret= 0; goto ex;
 }
 xorriso->file_size_limit= new_limit;
 ret= 1;
ex:;
 (*idx)= end_idx;
 if(ret > 0) {
   if(xorriso->file_size_limit > 0)
     sprintf(xorriso->info_text, "-file_size_limit now at %.f\n",
             (double) xorriso->file_size_limit);
   else
     sprintf(xorriso->info_text, "-file_size_limit now off\n");
   Xorriso_info(xorriso,0);
 }
 return(ret);
}


/* Option -find alias -findi, and -findx */
/* @param flag bit0= -findx rather than -findi
               bit1= do not reset pacifier, no final pacifier message
                     do not reset find_compare_result
               bit2= do not count deleted files with rm and rm_r
               bit3= use Xorriso_findi_sorted() rather than Xorriso_findi()
                     (this can also be ordered by test -sort_lba)
               bit4= return number of matches plus 1
*/
int Xorriso_option_find(struct XorrisO *xorriso, int argc, char **argv,
                       int *idx, int flag)
{
 int ret, i, end_idx, type= 0, action, deleter= 0, start_lba, count;
 int list_extattr_head= 0, bsl_mem, disk_path;
 struct FindjoB *job, *first_job= NULL, *new_job;
 char *start_path, *path= NULL, *cpt, *other_path_start= NULL, *cd_pt;
 char *access_acl_text= NULL, *default_acl_text= NULL, *list_extattr_mode;
 char *arg1_pt;

 struct stat dir_stbuf;
 uid_t user= 0;
 gid_t group= 0;
 time_t date= 0;
 mode_t mode_or= 0, mode_and= ~1;
 double mem_lut= 0.0;

 end_idx= Xorriso_end_idx(xorriso, argc, argv, *idx, 1);
 Xorriso_alloc_meM(path, char, SfileadrL);
 Xorriso_alloc_meM(other_path_start, char, SfileadrL);

 start_path= ".";
 list_extattr_mode= "e";
 if(end_idx > *idx && start_path[0]!=0)
   start_path= argv[*idx];
 ret= Findjob_new(&first_job, start_path, 0);
 if(ret<=0) {
   Xorriso_no_findjob(xorriso, "-find[ix]", 0);
   {ret= -1; goto ex;}
 }
 job= first_job;
 if(!(flag&2))
   xorriso->find_compare_result= 1;
 for(i= *idx+1; i<end_idx; i++) {
   if(strcmp(argv[i], "-name")==0) {
     if(i+1>=end_idx) {
not_enough_arguments:;
       sprintf(xorriso->info_text,
               "-find[ix]: not enough parameters with test ");
       Text_shellsafe(argv[i], xorriso->info_text, 1);
       goto sorry_ex;
     }
     i++;
     ret= Findjob_set_name_expr(job, argv[i], 0);
     if(ret<=0) {
       sprintf(xorriso->info_text, "-find[ix]: cannot set -name expression ");
       Text_shellsafe(argv[i], xorriso->info_text, 1);
       goto sorry_ex;
     }
   } else if(strcmp(argv[i], "-wholename")==0) {
     if(i+1>=end_idx)
       goto not_enough_arguments;
     i++;
     ret= Findjob_set_name_expr(job, argv[i], 1);
     if(ret<=0) {
       sprintf(xorriso->info_text,
               "-find[ix]: cannot set -wholename expression ");
       Text_shellsafe(argv[i], xorriso->info_text, 1);
       goto sorry_ex;
     }
   } else if(strcmp(argv[i], "-type")==0) {
     if(i+1>=end_idx)
       goto not_enough_arguments;
     i++;
     ret= Findjob_set_file_type(job, argv[i][0], 0);
     if(ret<=0) {
       sprintf(xorriso->info_text, "-find[ix]: unknown -type '%c'",argv[i][0]);
       goto sorry_ex;
     }
   } else if(strcmp(argv[i], "-damaged")==0) {
     Findjob_set_damage_filter(job, 1, 0);
   } else if(strcmp(argv[i], "-undamaged")==0) {
     Findjob_set_damage_filter(job, -1, 0);
   } else if(strcmp(argv[i], "-lba_range")==0) {
     if(i+2>=end_idx)
       goto not_enough_arguments;
     i+= 2;
     sscanf(argv[i-1], "%d", &start_lba);
     sscanf(argv[i], "%d", &count);
     Findjob_set_lba_range(job, start_lba, count, 0);
   } else if(strcmp(argv[i], "-pending_data")==0) {
     Findjob_set_commit_filter_2(job, 0);
   } else if(strcmp(argv[i], "-has_acl")==0) {
     Findjob_set_acl_filter(job, 1, 0);
   } else if(strcmp(argv[i], "-has_no_acl")==0) {
     Findjob_set_acl_filter(job, -1, 0);
   } else if(strcmp(argv[i], "-has_xattr")==0) {
     Findjob_set_xattr_filter(job, 1, 0);
   } else if(strcmp(argv[i], "-has_any_xattr")==0) {
     Findjob_set_xattr_filter(job, 1, 1);
   } else if(strcmp(argv[i], "-has_no_xattr")==0) {
     Findjob_set_xattr_filter(job, -1, 0);
   } else if(strcmp(argv[i], "-has_aaip")==0) {
     Findjob_set_aaip_filter(job, 1, 0);
   } else if(strcmp(argv[i], "-has_no_aaip")==0) {
     Findjob_set_aaip_filter(job, -1, 0);
   } else if(strcmp(argv[i], "-has_filter")==0) {
     Findjob_set_filter_filter(job, 1, 0);
   } else if(strcmp(argv[i], "-has_no_filter")==0) {
     Findjob_set_filter_filter(job, -1, 0);
   } else if(strcmp(argv[i], "-has_md5")==0) {
     Findjob_set_prop_filter(job, 15, 1, 0);
   } else if(strcmp(argv[i], "-disk_name")==0 ||
             strcmp(argv[i], "-disk_path")==0) {
     disk_path= (strcmp(argv[i], "-disk_path") == 0);
     if(i+1>=end_idx)
       goto not_enough_arguments;
     i++;
     arg1_pt= argv[i];
     if(disk_path) {
       ret= Xorriso_make_abs_adr(xorriso, xorriso->wdx, argv[i], path,
                                1 | 2 | 4 | 8);
       if(ret<=0)
         goto ex;
       arg1_pt= path;
     }
     ret= Findjob_set_name_expr(job, arg1_pt, 2 + disk_path);
     if(ret<=0) {
       sprintf(xorriso->info_text,
               "-find[ix]: cannot set %s ",
               disk_path ? "-disk_path address" : "-disk_name expression");
       Text_shellsafe(argv[i], xorriso->info_text, 1);
       goto sorry_ex;
     }
   } else if(strcmp(argv[i], "-hidden")==0) {
     if(i + 1 >= end_idx)
       goto not_enough_arguments;
     i+= 1;
     type= Xorriso__hide_mode(argv[i], 0);
     if(type < 0) {
       sprintf(xorriso->info_text, "-findi: -hidden : unknown hide state ");
       Text_shellsafe(argv[i], xorriso->info_text, 1);
       goto sorry_ex;
     } else {
       ret= Findjob_set_test_hidden(job, type, 0);
       if(ret <= 0) {
         sprintf(xorriso->info_text, "-findi: cannot setup -hidden test");
         goto sorry_ex;
       }
     }
   } else if(strcmp(argv[i], "-has_hfs_crtp")==0) {
     if(i + 2 >= end_idx)
       goto not_enough_arguments;
     i+= 2;
     ret= Xorriso_hfsplus_file_creator_type(xorriso, "", NULL,
                                            argv[i - 1], argv[i], 3);
     if(ret <= 0)
       {ret= 0; goto ex;}
     ret= Findjob_set_crtp_filter(job, argv[i - 1], argv[i], 0);
     if(ret <= 0) {
       sprintf(xorriso->info_text, "-findi: cannot setup -has_hfs_crtp test");
       goto sorry_ex;
     }
   } else if(strcmp(argv[i], "-has_hfs_bless")==0) {
     if(i + 1 >= end_idx)
       goto not_enough_arguments;
     i+= 1;
     ret= Findjob_set_bless_filter(xorriso, job, argv[i], 0);
     if(ret <= 0) {
       sprintf(xorriso->info_text, "-findi: cannot setup -has_hfs_bless test");
       goto sorry_ex;
     }
   } else if(strcmp(argv[i], "-true") == 0) {
     ret= Findjob_set_false(job, -1, 0);
   } else if(strcmp(argv[i], "-false") == 0) {
     ret= Findjob_set_false(job, 1, 0);
   } else if(strcmp(argv[i], "-decision") == 0) {
     if(i+1>=end_idx)
       goto not_enough_arguments;
     i++;
     ret= Findjob_set_decision(job, argv[i], 0);
   } else if(strcmp(argv[i], "-prune") == 0) {
     ret= Findjob_set_prune(job, 0);
   } else if(strcmp(argv[i], "-sub") == 0 || strcmp(argv[i], "(") == 0) {
     ret= Findjob_open_bracket(job, 0);
   } else if(strcmp(argv[i], "-subend") == 0 || strcmp(argv[i], ")") == 0) {
     ret= Findjob_close_bracket(job, 0);
   } else if(strcmp(argv[i], "-not") == 0 || strcmp(argv[i], "!") == 0) {
     ret= Findjob_not(job, 0);
   } else if(strcmp(argv[i], "-and") == 0 || strcmp(argv[i], "-a") == 0) {
     ret= Findjob_and(job, 0);
   } else if(strcmp(argv[i], "-or") == 0 || strcmp(argv[i], "-o") == 0) {
     ret= Findjob_or(job, 0);
   } else if(strcmp(argv[i], "-if") == 0) {
     ret= Findjob_if(job, 0);
   } else if(strcmp(argv[i], "-then") == 0) {
     ret= Findjob_then(job, 0);
   } else if(strcmp(argv[i], "-else") == 0) {
     ret= Findjob_else(job, 0);
   } else if(strcmp(argv[i], "-elseif") == 0) {
     ret= Findjob_elseif(job, 0);
   } else if(strcmp(argv[i], "-endif") == 0) {
     ret= Findjob_endif(job, 0);
   } else if(strcmp(argv[i], "-sort_lba") == 0) {
     flag|= 8;
     /* If an operator is open: insert a -true test, else do nothing */
     ret= Findjob_set_false(job, -1, 1);
     if(ret == 2)
       ret= 1;
   } else if(strcmp(argv[i], "-exec")==0) {
     if(i+1>=end_idx) {
not_enough_exec_arguments:;
       sprintf(xorriso->info_text,
               "-find[ix]: not enough parameters with -exec ");
       Text_shellsafe(argv[i], xorriso->info_text, 1);
       goto sorry_ex;
     }
     i++;
     cpt= argv[i];
     if(*cpt=='-')
       cpt++;
     if(strcmp(cpt, "echo")==0) {
       Findjob_set_action_target(job, 0, NULL, 0);
     } else if(strcmp(cpt, "rm")==0) {
       Findjob_set_action_target(job, 1, NULL, 0);
       deleter= 1;
     } else if(strcmp(cpt, "rm_r")==0) {
       Findjob_set_action_target(job, 2, NULL, 0);
       deleter= 1;

#ifdef NIX
/* >>> not implemented yet */;
     } else if(strcmp(cpt, "mv")==0) {
       if(i+1>=end_idx)
         goto not_enough_exec_arguments;
       i++;
       Findjob_set_action_target(job, 3, argv[i], 0);
#endif

     } else if(strcmp(cpt, "chown")==0 || strcmp(cpt, "chown_r")==0) {
       if(i+1>=end_idx)
         goto not_enough_exec_arguments;
       i++;
       ret= Xorriso_convert_uidstring(xorriso, argv[i], &user, 0);
       if(ret<=0)
         goto ex;
       ret= Findjob_set_action_chown(job, user, strlen(cpt)>5);
       if(ret<=0) {
         Xorriso_no_findjob(xorriso, "-find -exec chown_r", 0);
         goto ex;
       }
     } else if(strcmp(cpt, "chgrp")==0 || strcmp(cpt, "chgrp_r")==0) {
       if(i+1>=end_idx)
         goto not_enough_exec_arguments;
       i++;
       ret= Xorriso_convert_gidstring(xorriso, argv[i], &group, 0);
       if(ret<=0)
         goto ex;
       ret= Findjob_set_action_chgrp(job, group, strlen(cpt)>5);
       if(ret<=0) {
         Xorriso_no_findjob(xorriso, "-find -exec chgrp_r", 0);
         goto ex;
       }
     } else if(strcmp(cpt, "chmod")==0 || strcmp(cpt, "chmod_r")==0) {
       if(i+1>=end_idx)
         goto not_enough_exec_arguments;
       i++;
       ret= Xorriso_convert_modstring(xorriso, "-find -exec chmod",
                                      argv[i], &mode_and, &mode_or, 0);
       if(ret<=0)
         goto ex;
       ret= Findjob_set_action_chmod(job, mode_and, mode_or, strlen(cpt)>5);
       if(ret<=0) {
         Xorriso_no_findjob(xorriso, "-find -exec chmod_r", 0);
         goto ex;
       }
     } else if(strcmp(cpt, "alter_date")==0 || strcmp(cpt, "alter_date_r")==0){
       if(i+2>=end_idx)
         goto not_enough_exec_arguments;
       i+= 2;
       ret= Xorriso_convert_datestring(xorriso, "-find -exec alter_date",
                                       argv[i-1], argv[i], &type, &date, 0);
       if(ret<=0)
         goto ex;
       ret= Findjob_set_action_ad(job, type, date, strlen(cpt)>10);
       if(ret<=0) {
         Xorriso_no_findjob(xorriso, "-find -exec alter_date_r", 0);
         goto ex;
       }
     } else if(strcmp(cpt, "lsdl")==0) {
       Findjob_set_action_target(job, 8, NULL, 0);

     } else if(strcmp(cpt, "find")==0) {
       ret= Findjob_new(&new_job, "", 0);
       if(ret<=0) {
         Xorriso_no_findjob(xorriso, "-find[ix]", 0);
         {ret= -1; goto ex;}
       }
       Findjob_set_action_subjob(job, 13, new_job, 0);
       job= new_job;

     } else if(strcmp(cpt, "compare")==0 || strcmp(cpt, "update")==0 ||
               strcmp(cpt, "widen_hardlinks")==0 ||
               strcmp(cpt, "update_merge")==0) {
       if(i+1>=end_idx)
         goto not_enough_exec_arguments;
       i++;
       action= 14;
       if(strcmp(cpt, "update")==0)
         action= 17;
       else if(strcmp(cpt, "widen_hardlinks")==0)
         action= 32;
       else if(strcmp(cpt, "update_merge") == 0) {
         action= 41;
         /* Enter update_merge mode for node adding */
         xorriso->update_flags|= 1;
       }

       ret= Xorriso_make_abs_adr(xorriso, xorriso->wdx, argv[i],
                                 other_path_start, 1|2|4|8);
       if(ret<=0)
         goto ex;
       Findjob_set_action_target(job, action, other_path_start, 0);
       ret= Xorriso_make_abs_adr(xorriso, xorriso->wdi, start_path,
                                 path, 1|2|4);
       if(ret<=0)
         goto ex;
       Findjob_set_start_path(job, path, 0);
       if(!(flag&2)) {
         Xorriso_pacifier_reset(xorriso, 0);
         mem_lut= xorriso->last_update_time;
       }
     } else if(strcmp(cpt, "in_iso")==0 ||
               strcmp(cpt, "not_in_iso")==0 ||
               strcmp(cpt, "add_missing")==0 ||
               strcmp(cpt, "empty_iso_dir")==0 ||
               strcmp(cpt, "is_full_in_iso")==0) {
       if(i+1>=end_idx)
         goto not_enough_exec_arguments;
       i++;
       ret= Xorriso_make_abs_adr(xorriso, xorriso->wdi, argv[i],
                                 other_path_start, 1|2|4);
       if(ret<=0)
         goto ex;
       if(strcmp(cpt, "in_iso")==0)
         action= 15;
       else if(strcmp(cpt, "add_missing")==0)
         action= 18;
       else if(strcmp(cpt, "empty_iso_dir")==0)
         action= 19;
       else if(strcmp(cpt, "is_full_in_iso")==0)
         action= 20;
       else
         action= 16;
       Findjob_set_action_target(job, action, other_path_start, 0);
       ret= Xorriso_make_abs_adr(xorriso, xorriso->wdx, start_path, path,
                                 1|2|4|8);
       if(ret<=0)
         goto ex;
       Findjob_set_start_path(job, path, 0);

     } else if(strcmp(cpt, "report_damage")==0) {
       Findjob_set_action_target(job, 21, NULL, 0);
     } else if(strcmp(cpt, "report_lba")==0) {
       Findjob_set_action_target(job, 22, NULL, 0);
     } else if(strcmp(cpt, "getfacl")==0) {
       Findjob_set_action_target(job, 24, NULL, 0);
     } else if(strcmp(cpt, "setfacl")==0) {
       if(i+1>=end_idx)
         goto not_enough_exec_arguments;
       i++;
       ret= Xorriso_normalize_acl_text(xorriso, argv[i],
                                       &access_acl_text, &default_acl_text, 0);
       if(ret <= 0)
         goto ex;
       Findjob_set_action_text_2(job, 25, access_acl_text, default_acl_text,
                                 0);
     } else if(strcmp(cpt, "getfattr")==0) {
       Findjob_set_action_target(job, 26, NULL, 0);
     } else if(strcmp(cpt, "setfattr")==0) {
       if(i + 2 >= end_idx)
         goto not_enough_exec_arguments;
       i+= 2;
       /* check input */
       ret= Xorriso_path_setfattr(xorriso, NULL, "", argv[i - 1],
                                  strlen(argv[i]), argv[i], 1);
       if(ret <= 0)
         goto ex;
       Findjob_set_action_text_2(job, 27, argv[i - 1], argv[i], 0);
     } else if(strcmp(cpt, "set_filter")==0) {
       if(i + 1 >= end_idx)
         goto not_enough_exec_arguments;
       i+= 1;
       Findjob_set_action_target(job, 28, argv[i], 0);
       if(!(flag&2)) {
         Xorriso_pacifier_reset(xorriso, 0);
         mem_lut= xorriso->last_update_time;
       }
     } else if(strcmp(cpt, "show_stream")==0) {
       Findjob_set_action_target(job, 29, NULL, 0);
     } else if(strcmp(cpt, "get_any_xattr")==0) {
       Findjob_set_action_target(job, 33, NULL, 0);
     } else if(strcmp(cpt, "get_md5")==0) {
       Findjob_set_action_target(job, 34, NULL, 0);
     } else if(strcmp(cpt, "check_md5")==0) {
       if(i + 1 >= end_idx)
         goto not_enough_exec_arguments;
       i+= 1;
       Findjob_set_action_target(job, 35, argv[i], 0);
       flag|= 8;
       if(!(flag&2)) {
         Xorriso_pacifier_reset(xorriso, 0);
         mem_lut= xorriso->last_update_time;
       }
       if(!(flag & 1))
         xorriso->find_check_md5_result= 0;
     } else if(strcmp(cpt, "make_md5")==0) {
       Findjob_set_action_target(job, 36, NULL, 0);
       flag|= 8;
       if(!(flag&2)) {
         Xorriso_pacifier_reset(xorriso, 0);
         mem_lut= xorriso->last_update_time;
       }
     } else if(strcmp(cpt, "mkisofs_r")==0) {
       Findjob_set_action_target(job, 37, NULL, 0);
     } else if(strcmp(cpt, "sort_weight")==0) {
       if(i + 1 >= end_idx)
         goto not_enough_exec_arguments;
       i+= 1;
       sscanf(argv[i], "%d", &type);
       Findjob_set_action_type(job, 38, type, 0);
     } else if(strcmp(cpt, "hide")==0) {
       if(i+1>=end_idx)
         goto not_enough_exec_arguments;
       i++;
       type= Xorriso__hide_mode(argv[i], 0);
       if(type < 0) {
         sprintf(xorriso->info_text, "-find -exec hide: unknown hide state ");
         Text_shellsafe(argv[i], xorriso->info_text, 1);
         goto sorry_ex;
       }
       Findjob_set_action_type(job, 39, type, 0);
     } else if(strcmp(cpt, "estimate_size")==0) {
       Findjob_set_action_target(job, 40, NULL, 0);
     } else if(strcmp(cpt, "rm_merge")==0) {
       Findjob_set_action_target(job, 42, NULL, 0);
       xorriso->update_flags&= ~1; /* End update_merge mode for node adding */
     } else if(strcmp(cpt, "clear_merge")==0) {
       Findjob_set_action_target(job, 43, NULL, 0);
       xorriso->update_flags&= ~1; /* End update_merge mode for node adding */
     } else if(strcmp(cpt, "list_extattr")==0) {
       if(i+1>=end_idx)
         goto not_enough_exec_arguments;
       i++;
       Findjob_set_action_target(job, 44, argv[i], 0);
       list_extattr_head= 1;
       list_extattr_mode= argv[i];
     } else if(strcmp(cpt, "set_hfs_crtp")==0) {
       if(i + 2 >= end_idx)
         goto not_enough_exec_arguments;
       i+= 2;
       /* Check creator and type for compliance */
       ret= Xorriso_hfsplus_file_creator_type(xorriso, "", NULL,
                                              argv[i - 1], argv[i], 1);
       if(ret <= 0)
         goto ex;
       Findjob_set_action_text_2(job, 45, argv[i - 1], argv[i], 0);
     } else if(strcmp(cpt, "get_hfs_crtp")==0) {
       Findjob_set_action_target(job, 46, NULL, 0);
     } else if(strcmp(cpt, "set_hfs_bless")==0) {
       if(i+1>=end_idx)
         goto not_enough_exec_arguments;
       i++;
       /* Check type of blessing for compliance */
       ret= Xorriso_hfsplus_bless(xorriso, "", NULL, argv[i], 4);
       if(ret <= 0)
         goto ex;
       Findjob_set_action_target(job, 47, argv[i], 0);
     } else if(strcmp(cpt, "get_hfs_bless")==0) {
       Findjob_set_action_target(job, 48, NULL, 0);
     } else {
       sprintf(xorriso->info_text, "-find -exec: unknown action ");
       Text_shellsafe(argv[i], xorriso->info_text, 1);
       goto sorry_ex;
     }
   } else {
     sprintf(xorriso->info_text, "-find[ix]: unknown option ");
     Text_shellsafe(argv[i], xorriso->info_text, 1);
sorry_ex:;
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
 }
 if(list_extattr_head) {
   sprintf(xorriso->result_line,
           "# Output of xorriso %s action list_extattr\n",
           (flag & 1) ? "-findx" : "-find");
   Xorriso_result(xorriso, 0);
   strcpy(xorriso->result_line, "cd ");
   if(start_path[0] == '/')
     strcat(xorriso->result_line, "/");
   else {
     cd_pt= (flag & 1) ? xorriso->wdx : xorriso->wdi;
     if(cd_pt[0] == 0)
       cd_pt= "/";
     ret= Xorriso_append_extattr_comp(xorriso, cd_pt, strlen(cd_pt),
                                      list_extattr_mode, 0);
     if(ret <= 0)
       goto ex;
   }
   strcat(xorriso->result_line, "\n");
   /* temporarily disable -backslash_codes with result output */
   bsl_mem= xorriso->bsl_interpretation;
   xorriso->bsl_interpretation= 0;
   Xorriso_result(xorriso, 0);
   xorriso->bsl_interpretation= bsl_mem;

   sprintf(xorriso->result_line, "c=\"setextattr\"\n\n");
   Xorriso_result(xorriso, 0);
 }
 if(flag&1)
   ret= Xorriso_findx(xorriso, first_job, "", start_path, &dir_stbuf, 0, NULL,
                      0);
 else if(flag & 8) {
   cpt= start_path;
   ret= Xorriso_findi_sorted(xorriso, first_job, (off_t) 0, 1, &cpt, 0);
 } else
   ret= Xorriso_findi(xorriso, first_job, NULL, (off_t) 0, NULL,
                      start_path, &dir_stbuf, 0, (flag&4)>>1);
ex:;
 if(deleter && !(flag&2))
   Xorriso_pacifier_callback(xorriso, "iso_rr_paths deleted",
                             xorriso->pacifier_count, 0, "", 1|2);
 else if(first_job->action == 28  && !(flag&2))
   Xorriso_pacifier_callback(xorriso, "file filters processed",
                             xorriso->pacifier_count, 0, "", 1 | 2);
 else if(mem_lut!=xorriso->last_update_time && mem_lut!=0.0 && !(flag&2))
   Xorriso_pacifier_callback(xorriso, "content bytes read",
                             xorriso->pacifier_count, 0, "", 1 | 8 | 32);
 if(first_job->action == 35 && !(flag & 1))
   Xorriso_report_md5_outcome(xorriso, first_job->target, 0);
 if(first_job->action == 40) {
   sprintf(xorriso->result_line,"Size lower   : %lus\n",
          (unsigned long) (first_job->estim_lower_size / (off_t) 2048));
   Xorriso_result(xorriso,0);
   sprintf(xorriso->result_line,"Size upper   : %lus\n",
          (unsigned long) ((first_job->estim_upper_size / (off_t) 2048) +
                           !!(first_job->estim_upper_size % 2048)));
   Xorriso_result(xorriso,0);
 }
 if(access_acl_text != NULL)
   free(access_acl_text);
 if(default_acl_text != NULL)
   free(default_acl_text);
 if(ret > 0 && (flag & 16) && first_job != NULL)
   ret= first_job->match_count + 1;
 Findjob_destroy(&first_job, 0);
 Xorriso_free_meM(path);
 Xorriso_free_meM(other_path_start);
 (*idx)= end_idx;
 return(ret);
}


/* Option -follow */
int Xorriso_option_follow(struct XorrisO *xorriso, char *mode, int flag)
{
 int was_fl, was_fm, was_fpr, was_fpt, l;
 double num;
 char *cpt, *npt;

 was_fpt= xorriso->do_follow_pattern;
 was_fpr= xorriso->do_follow_param;
 was_fl= xorriso->do_follow_links;
 was_fm= xorriso->do_follow_mount;
 xorriso->do_follow_pattern= 0;
 xorriso->do_follow_param= 0;
 xorriso->do_follow_links= 0;
 xorriso->do_follow_mount= 0;
 npt= cpt= mode;
 for(cpt= mode; npt!=NULL; cpt= npt+1) {
   npt= strchr(cpt,':');
   if(npt==NULL)
     l= strlen(cpt);
   else
     l= npt-cpt;
   if(l==0)
     goto unknown_mode;
   if(strncmp(cpt, "off", l)==0) {
     xorriso->do_follow_pattern= 0;
     xorriso->do_follow_param= 0;
     xorriso->do_follow_links= 0;
     xorriso->do_follow_mount= 0;
   } else if(strncmp(cpt, "on", l)==0) {
     xorriso->do_follow_pattern= 1;
     xorriso->do_follow_param= 1;
     xorriso->do_follow_links= 1;
     xorriso->do_follow_mount= 1;
   } else if(strncmp(cpt, "default", l)==0) {
     xorriso->do_follow_pattern= 1;
     xorriso->do_follow_param= 0;
     xorriso->do_follow_links= 0;
     xorriso->do_follow_mount= 1;
     xorriso->follow_link_limit= 100;
   } else if(strncmp(cpt, "link", l)==0 || strncmp(cpt,"links", l)==0) {
     xorriso->do_follow_links= 1;
   } else if(strncmp(cpt, "mount", l)==0) {
     xorriso->do_follow_mount= 1;
   } else if(strncmp(cpt,"param", l)==0) {
     xorriso->do_follow_param= 1;
   } else if(strncmp(cpt, "pattern", l)==0) {
     xorriso->do_follow_pattern= 1;
   } else if(strncmp(cpt, "limit=", 6)==0) {
     sscanf(cpt+6, "%lf", &num);
     if(num<=0 || num>1.0e6) {
       sprintf(xorriso->info_text, "-follow: Value too %s with '%s'",
               num<=0 ? "small" : "large", cpt+6);
       goto sorry_ex;
     }
     xorriso->follow_link_limit= num;
   } else {
unknown_mode:;
     if(l<SfileadrL)
       sprintf(xorriso->info_text, "-follow: unknown mode '%s'", cpt);
     else
       sprintf(xorriso->info_text, "-follow: oversized mode parameter (%d)",l);
sorry_ex:
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     xorriso->do_follow_pattern= was_fpt;
     xorriso->do_follow_param= was_fpr;
     xorriso->do_follow_links= was_fl;
     xorriso->do_follow_mount= was_fm;
     return(0);
   }
 }
 return(1);
}


/* Option -fs */
int Xorriso_option_fs(struct XorrisO *xorriso, char *size, int flag)
{
 double num;

 num= Scanf_io_size(size, 0);
 if(num < 64*1024 || num > 1024.0 * 1024.0 * 1024.0) {
   sprintf(xorriso->info_text, "-fs: wrong size %.f (allowed: %.f - %.f)",
           num, 64.0 * 1024.0, 1024.0 * 1024.0 * 1024.0);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 xorriso->fs= num / 2048.0;
 if(xorriso->fs * 2048 < num)
   xorriso->fs++;
 return(1);
}


/* Optionis -getfacl alias -getfacli, -getfacl_r alias -getfacl_ri
            -getfattr alias getfattri
*/
/* @param flag bit0= recursive -getfacl_r
               bit1= getfattr rather than getfacl
               bit3= with bit1: do not ignore eventual non-user attributes
*/
int Xorriso_option_getfacli(struct XorrisO *xorriso,
                            int argc, char **argv, int *idx, int flag)
{
 int i, ret, was_failure= 0, end_idx, fret;
 int optc= 0;
 char **optv= NULL;
 struct FindjoB *job= NULL;
 struct stat dir_stbuf;

 ret= Xorriso_opt_args(xorriso, "-getfacl", argc, argv, *idx, &end_idx, &optc,
                       &optv, 0);
 if(ret<=0)
   goto ex;
 for(i= 0; i<optc; i++) {
   if(flag&1) {
     ret= Findjob_new(&job, optv[i], 0);
     if(ret<=0) {
       Xorriso_no_findjob(xorriso, "-getfacl_r", 0);
       {ret= -1; goto ex;}
     }
     if(flag & 2) {
       Findjob_set_action_target(job, 26, NULL, 0);
     } else
       Findjob_set_action_target(job, 24, NULL, 0);
     ret= Xorriso_findi(xorriso, job, NULL, (off_t) 0,
                        NULL, optv[i], &dir_stbuf, 0, 0);
     Findjob_destroy(&job, 0);
   } else {
     if(flag & 2)
       ret= Xorriso_getfattr(xorriso, NULL, optv[i], NULL, flag & 8);
     else
       ret= Xorriso_getfacl(xorriso, NULL, optv[i], NULL, 0);
   }
   if(ret>0 && !xorriso->request_to_abort)
 continue; /* regular bottom of loop */
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret>=0)
 continue;
   ret= 0; goto ex;
 }
 ret= 1;
ex:;
 (*idx)= end_idx;
 Xorriso_opt_args(xorriso, "-getfacl", argc, argv, *idx, &end_idx,
                  &optc, &optv, 256);
 Findjob_destroy(&job, 0);
 if(ret<=0)
   return(ret);
 return(!was_failure);
}


/* Option -gid */
int Xorriso_option_gid(struct XorrisO *xorriso, char *gid, int flag)
{
 int ret;

 xorriso->do_global_gid= 0;
 if(gid[0]==0 || strcmp(gid,"-")==0)
   return(1);
 ret= Xorriso_convert_gidstring(xorriso, gid, &(xorriso->global_gid), 0);   
 if(ret>0)
   xorriso->do_global_gid= 1;
 return(ret);
}


/* Option -grow_blindly */
int Xorriso_option_grow_blindly(struct XorrisO *xorriso, char *msc2, int flag)
{
 double num;
 int l;

 if(msc2[0]==0 || msc2[0]=='-' || strcmp(msc2, "off")==0) {
   xorriso->grow_blindly_msc2= -1;
   return(1);
 }
 num= Scanf_io_size(msc2, 0);
 l= strlen(msc2);
 if(msc2[l-1]<'0' || msc2[l-1]>'9')
   num/= 2048.0;
 xorriso->grow_blindly_msc2= num;
 return(1);
}


/* Option -hardlinks "on"|"off" */
int Xorriso_option_hardlinks(struct XorrisO *xorriso, char *mode, int flag)
{
 int ret;
 char *what_data= NULL, *what, *what_next;

 Xorriso_alloc_meM(what_data, char, SfileadrL);
 if(Sfile_str(what_data, mode, 0)<=0) {
   sprintf(xorriso->info_text,
           "-hardlinks: mode string is much too long (%d)",
           (int) strlen(mode));
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 } 
 for(what= what_data; what != NULL; what= what_next) {
   what_next= strchr(what, ':');
   if(what_next != NULL) {
     *what_next= 0;
     what_next++;
   }
   if(strcmp(what, "off") == 0) {
     Xorriso_finish_hl_update(xorriso, 0);
     xorriso->ino_behavior|= 1 | 2 | 4;
     xorriso->ino_behavior&= ~8;
   } else if(strcmp(what, "on") == 0) {
     xorriso->ino_behavior&= ~(1 | 2 | 4 | 8);
   } else if(strcmp(what, "without_update") == 0) {
     Xorriso_finish_hl_update(xorriso, 0);
     xorriso->ino_behavior&= ~(1 | 2 | 4);
     xorriso->ino_behavior|= 8;
   } else if(strcmp(what, "start_update") == 0) {
     xorriso->ino_behavior&= ~(1 | 2 | 4 | 8);
     ret= Xorriso_make_di_array(xorriso, 1);
     if(ret <= 0)
       goto ex;
   } else if(strcmp(what, "end_update") == 0) {
     Xorriso_finish_hl_update(xorriso, 0);
   } else if(strcmp(what, "perform_update") == 0) {
     Xorriso_finish_hl_update(xorriso, 0);
   } else if(strcmp(what, "start_extract") == 0) {
     xorriso->ino_behavior&= ~(1 | 2 | 4);
     ret= Xorriso_make_hln_array(xorriso, 1);
     if(ret <= 0)
       goto ex;
   } else if(strcmp(what, "end_extract") == 0) {
     Xorriso_destroy_hln_array(xorriso, 0);
   } else if(strcmp(what, "discard_extract") == 0) {
     Xorriso_destroy_hln_array(xorriso, 0);
   } else if(strcmp(what, "normal_extract") == 0) {
     xorriso->ino_behavior&= ~16;
   } else if(strcmp(what, "cheap_sorted_extract") == 0) {
     xorriso->ino_behavior|= 16;
   } else if(strcmp(what, "lsl_count") == 0) {
     xorriso->ino_behavior&= ~32;
   } else if(strcmp(what, "no_lsl_count") == 0) {
     xorriso->ino_behavior|= 32;
   } else {
     sprintf(xorriso->info_text, "-hardlinks: unknown mode '%s' in '%s'",
             what, mode);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
 }

 /* <<< ts B00613 : This is wrong: it enables new_rr if -hardlinks is off.
                    Documented is that new_rr gets enabled if hardlinks are on.
                    But it never worked that way.
                    A compromise seems to be to disable this totally and
                    to change man xorriso.
                    new_rr still is not recognized by mount on Solaris.

 if(xorriso->ino_behavior & 2)
   Xorriso_option_compliance(xorriso, "new_rr", 0);
 */

 ret= 1;
ex:;
 Xorriso_free_meM(what_data);
 return(ret);
}


/* Option -help and part of -prog_help */
int Xorriso_option_help(struct XorrisO *xorriso, int flag)
{
 static char text[][80]={

#ifdef Xorriso_no_helP

"This binary program does not contain a help text.",
"If available, read: man 1 xorriso",

#else

"This program creates, loads, manipulates and writes ISO 9660 filesystem",
"images with Rock Ridge extensions. Write targets can be drives with optical",
"media or local filesystem objects.",
"The program operations are controlled by a sequence of commands, of which",
"the initial ones are given as program arguments or as lines in startup",
"files. Further commands may get read from files in batch mode or from",
"standard input in dialog mode.",
"",
"  -x             Only in effect if given as program argument:",
"                 Execute commands given as program arguments in a sequence",
"                 that most likely makes some sense. Default is to execute",
"                 program arguments exactly in the sequence as given.",
"",
"Preparation commands:",
"Drive addresses are either /dev/... as listed with command -devices or",
"disk files, eventually with prefix \"stdio:\" if non-CD-drive in /dev tree.",
"E.g. /dev/sr0 , /tmp/pseudo_drive , stdio:/dev/sdc",
"  -dev address   Set input and output drive and load eventual ISO image.",
"                 Set the image expansion method to growing.",
"  -indev address  Set input drive and load eventual ISO image. Use expansion",
"                 methods modifying or blind growing.",
"  -outdev address",
"                 Set output drive and use modifying or blind growing.",
"  -drive_class \"harmless\"|\"banned\"|\"risky\"|\"clear_list\" disk_pattern",
"                 Add a drive path pattern to one of the safety lists or make",
"                 those lists empty. Defaulty entry in \"risky\" is \"/dev\".",
"  -grow_blindly \"off\"|predicted_nwa",
"                 Switch between modifying and blind growing.",
"  -load \"session\"|\"track\"|\"lba\"|\"sbsector\"|\"volid\"|\"auto\" id",
"                 Load a particular (outdated) ISO image from a -dev or",
"                 -indev which hosts more than one session.",
"  -displacement [-]block_address",
"                 When loading ISO tree or reading data files compensate a",
"                 displacement versus the start address for which the image",
"                 was prepared.",
"  -rom_toc_scan \"on\"|\"force\"|\"off\"[:\"emul_on\"|\"emul_off\"]",
"                [:\"emul_wide\"|\"emul_narrow\"]",
"                 Enable scanning for ISO sessions on read-only drives/media",
"                 resp. on overwriteable media with emulated TOC.",
"  -calm_drive \"in\"|\"out\"|\"all\"|\"on\"|\"off\"",
"                 Reduce drive noise until it gets actually used again.",
"  -assert_volid pattern severity",
"                 Accept input image only if its volume id matches pattern.",
"  -charset name  Set the character set name to be used for file name",
"                 conversion from and to media.",
"  -in_charset name",
"                 Like -charset but only for conversion from media.",
"  -auto_charset \"on\"|\"off\"",
"                 Enable writing and reading of character set name in image.",
"  -out_charset name",
"                 Like -charset but only for conversion to media.",
"  -local_charset name",
"                 Override system assumption of the local character set name.",
"  -hardlinks mode[:mode ...]",
"                 Enable resp. disable recording and restoring of hard links.",
"                 Modes are \"on\", \"off\", \"perform_update\",",
"                 \"without_update\", \"discard_extract\",",
"                 \"cheap_sorted_extract\", \"normal_extract\"",
"  -acl \"on\"|\"off\"",
"                 Enable resp. disable reading and writing of ACLs.",
"  -xattr \"on\"|\"off\"",
"                 Enable resp. disable reading and writing of xattr.",
"  -for_backup",
"                 Shortcut for: -hardlinks on -acl on -xattr on -md5 on",
"  -disk_dev_ino \"on\"|\"ino_only\"|\"off\"",
"                 Enable resp. disable recording of disk file dev_t and ino_t",
"                 and their use in file comparison.",
"  -md5 \"on\"|\"all\"|\"off\"",
"                 Enable resp. disable processing of MD5 checksums.",
"  -scdbackup_tag list_path record_name",
"                 Enable production of scdbackup tag with -md5 on",
"  -ban_stdio_write",
"                 Allow for writing only the usage of optical drives.",
"  -early_stdio_test \"on\"|\"appendable_wo\"|\"off\"",
"                 Classify stdio drives by effective access permissions.",
"  -data_cache_size number_of_tiles blocks_per_tile",
"                 Adjust size and granularity of the data read cache.",
"  -blank [\"force:\"]\"fast\"|\"all\"|\"deformat\"|\"deformat_quickest\"",
"                 Blank medium resp. invalidate ISO image on medium.",
"                 Prefix \"force:\" overrides medium evaluation.",
"  -close_damaged \"as_needed\"|\"force\"",
"                 Close track and session of damaged medium.",
"  -format \"as_needed\"|\"full\"|\"fast\"|\"by_index_#\"|\"by_size_#\"",
"                 Format BD-RE, BD-R, DVD-RAM, DVD-RW, DVD+RW.",
"  -volid volume_id",
"                 Specifies the volume ID text. (32 chars out of [A-Z0-9_])",
"  -volset_id name",
"                 Specifies the volume set id. (128 chars)",
"  -publisher name",
"                 Specifies the publisher name. (128 chars)",
"  -application_id name",
"                 Specifies the application id. (128 chars)",
"  -system_id name",
"                 Specifies the system id for the System Area. (32 chars)",
"  -volume_date type timestring",
"                 Specifies volume timestamps. [\"c\",\"m\",\"x\",\"f\",\"uuid\"]", 
"  -copyright_file name",
"                 Specifies the name of the Copyright File. (37 chars)",
"  -biblio_file name",
"                 Specifies the name of the Bibliographic File. (37 chars)",
"  -abstract_file name",
"                 Specifies the name of the Abstract File. (37 chars)",
"  -joliet \"on\"|\"off\"",
"                 Generate Joliet info additional to Rock Ridge info.",
"  -hfsplus \"on\"|\"off\"",
"                 Generate a HFS+ partition and filesystem within ISO image.",
"  -rockridge \"on\"|\"off\"",
"                 Opportunity to omit Rock Ridge info. (Do not do it !)",
"  -jigdo \"clear\"|\"template_path\"|\"jigdo_path\"|\"md5_path\"",
"         |\"min_size\"|\"checksum_iso\"|\"checksum_template\"",
"         |\"compression\"|\"exclude\"|\"demand_md5\"|\"mapping\"",
"         |\"checksum_iso\"|\"checksum_template\"",
"         value",
"                 Clear Jigdo Template Extraction parameter list or add a",
"                 parameter with its value to that list.",
"  -compliance rule[:rule...]",
"                 Allow more or less harmless deviations from strict standards",
"                 compliance.",
"  -rr_reloc_dir name",
"                 Specifies name of relocation directory in root directory,",
"                 to which deep subtrees will get relocated if -compliance",
"                 is set to \"deep_paths_off\".",
"  -boot_image \"any\"|\"isolinux\"|\"grub\"",
"              |\"discard\"|\"keep\"|\"patch\"|\"dir=\"|\"bin_path=\"",
"              |\"cat_path=\"|\"cat_hidden=on|iso_rr|joliet|off\"",
"              |\"load_size=\"|\"boot_info_table=\"",
"              |\"grub2_boot_info=\"|\"grub2_mbr=\"",
"              |\"system_area=\"|\"partition_table=on|off\"",
"              |\"chrp_boot_part=on|off=\"|\"prep_boot_part=\"",
"              |\"efi_boot_part=\"|\"efi_boot_part=--efi-boot-image\"",
"              |\"mips_path=\"|\"mipsel_path=\"|\"mips_discard\"",
"              |\"sparc_label=\"|\"grub2_sparc_core=\"|\"sparc_discard\"",
"              |\"hfsplus_serial=\"|\"hfsplus_block_size=\"",
"              |\"apm_block_size=\"|\"show_status\"",
"                 Whether to discard or keep an exiting El Torito boot image.",
"                 ISOLINUX can be made bootable by dir=/ or dir=/isolinux",
"                 or dir=/boot/isolinux. Others, like GRUB, by bin_path=...",
"                 and cat_path=...",
"                 The boot image and its helper files need to be added to the",
"                 ISO image by the usual commands like -map or -add.",
"                 system_area= and partition_table= are for MBR based booting",
"                 from USB stick. The system_area= file needs not to be added.",
"                 chrp_boot_part= and prep_boot_part= are for PowerPC.",
"                 efi_boot_part= is for booting EFI systems from USB stick.",
"                 mips_path= adds Big Endian MIPS boot files. mipsel_path=",
"                 sets one Little Endian MIPS boot file. sparc_label=",
"                 activates SUN Disk Label. All three are mutually exclusive",
"                 and exclusive to MBR production.",
"  -append_partition partition_number type_code disk_path",
"                 Append a prepared filesystem image after the end of the",
"                 ISO image. Caution: Will be overwritten by multi-session.",
"",
"  -uid uid       User id to be used for the whole multi-session ISO image.",
"  -gid gid       Group id for the same purpose.",
"",
"  -devices       Show list of available optical drives and their addresses.",
"  -device_links  Like devices, but showing link paths which are hopefully",
"                 persistent over reboot on modern Linux systems.",
"",
"  -toc           Show media specific tables of content (sessions).",
"  -toc_of \"in\"|\"out\"|\"all\"[\":short\"]",
"                 Show -toc of either input drive or output drive or both.",
"",
"  -mount_cmd drive entity id path",
"                 Print to result channel a command suitable to mount the",
"                 depicted entity (see -load) at the given directory path.",
"  -mount_opts \"exclusive\"|\"shared\"",
"                 Set options for -mount and -mount_cmd.",
"  -session_string drive entity id \"linux:\"path|\"freebsd:\"path|form",
"                 Print foreign OS command or custom line.",
"",
"  -list_formats  Show media specific list of format descriptors.",
"",
"  -list_speeds   Show media specific list of write speed descriptors.",
"",
"  -list_profiles \"in\"|\"out\"|\"all\"",
"                 Show list of media types supported by indev resp. outdev.",
"  -print_size    Print the foreseeable consumption by next -commit.",
"",
"  -tell_media_space",
"                 Print foreseeable available space on output medium",
"  -pvd_info      Print various id strings of the loaded ISO image."
"",
"Commands with variable length path list [...] need the list delimiter text",
"as end mark if they are followed by another command. By default this",
"delimiter is \"--\". In dialog and with commands read from files, the line",
"end serves as such a mark. With program arguments this mark can be omitted",
"only with the last command in the list of program arguments.",
"For brevity the list delimiter is referred as \"--\" throughout this text.",
"",
"  -list_delimiter text   Set the list delimiter to be used instead of \"--\"",
"                 It has to be a single word, must not be empty, not longer",
"                 than 80 characters, may mot contain quotation marks.",
"",
"Manipulation commands:",
"disk_path is a path to an object in the local filesystem tree.",
"iso_rr_path is the Rock Ridge name of a file object in the ISO image.",
"pathspec is either a disk_path or (if allowed) a pair: iso_rr_path=disk_path",
"Commands marked by [***] have variable length parameter lists and perform",
"pattern expansion if enabled by -iso_rr_pattern resp. -disk_pattern.",
"",
"  -pathspecs \"on\"|\"off\"  Allow or disallow pathspecs of form ",
"                 iso_rr_path=disk_path . Only \"off\" allows eventual",
"                 -disk_pattern expansion.",
"  -add pathspec [...] | disk_path [***]",
"                 Insert the given files or directory trees from",
"                 filesystem into the ISO image. Much like mkisofs.",
"  -add_plainly \"none\"|\"unknown\"|\"dashed\"|\"any\"",
"                 Whether to add lonely arguments as pathspec resp. disk_path.",
"  -path_list disk_path",
"                 Like -add but read the pathspecs from file disk_path.",
"  -quoted_path_list disk_path",
"                 Like -path_list but with line rules as -dialog \"on\".",
"",
"  -map disk_path iso_rr_path",
"                 Insert disk file object at the given iso_rr_path.",
"  -map_single disk_path iso_rr_path",
"                 Like -map but with directory do not insert its sub tree.",
"  -map_l disk_prefix iso_rr_prefix disk_path [***]",
"                 Performs -map with each disk_path.",
"  -update disk_path iso_rr_path",
"                 Compare both file objects and do what is necessary to make",
"                 iso_rr_path a matching copy of disk_path.", 
"  -update_r disk_path iso_rr_path",
"                 Like -update but affecting all files below directories.",
"  -update_l disk_prefix iso_rr_prefix disk_path [***]",
"                 Performs -update_r with each disk_path.",
"  -cut_out disk_path byte_offset byte_count iso_rr_path",
"                 Map a byte interval of a regular disk file into a regular",
"                 file in the ISO image.",
"",
"  -cpr disk_path [***] iso_rr_path",
"                 Insert the given files or directory trees from filesystem",
"                 into the ISO image, according to the rules of cp -r.",
"",
"  -rm iso_rr_path [***]",
"                 Delete the given files from the ISO image.",
"  -rm_r iso_rr_path [***]",
"                 Delete the given directory trees from ISO image.",
"  -move iso_rr_path iso_rr_path",
"                 Rename the single file given by the first iso_rr_path to",
"                 the second iso_rr_path.",
"  -mv iso_rr_path [***] iso_rr_path",
"                 Like shell command mv rename the given file objects in the",
"                 ISO tree to the last of the iso_rr_path parameters.",
"  -chown uid iso_rr_path [***]",
"                 Equivalent to chown in the ISO image.",
"  -chown_r uid iso_rr_path [***]",
"                 Like -chown but affecting all files below directories.",
"  -chgrp gid iso_rr_path [***]",
"                 Equivalent to chgrp in the ISO image.",
"  -chgrp_r gid iso_rr_path [***]",
"                 Like -chgrp but affecting all files below directories.",
"  -chmod mode iso_rr_path [***]",
"                 Equivalent to chmod in the ISO image.",
"  -chmod_r mode iso_rr_path [***]",
"                 Like -chmod but affecting all files below directories.",
"  -setfacl acl_text iso_rr_path [***]",
"                 Replace the permissions and eventual ACL of the given files",
"                 in the ISO image by the ACL which is defined by acl_text.",
"  -setfacl_r acl_text iso_rr_path [***]",
"                 Like -setfacl but affecting all files below directories.",
"  -setfacl_list disk_path",
"                 Read output of getfacl from file disk_path. Set owner,",
"                 group and ACL of the iso_rr_path given by line \"# file:\".",
"  -setfattr [-]name value iso_rr_path [***]",
"                 Set xattr pair with the given name to the given value, or",
"                 delete pair if name is prefixed with \"-\" and value is",
"                 an empty text.",
"  -setfattr_r [-]name value iso_rr_path [***]",
"                 Like -setfattr but affecting all files below directories.",
"  -setfattr_list disk_path",
"                 Read output of getfattr from file disk_path. Replace the",
"                 xattr of the iso_rr_path given by line \"# file:\".",
"  -alter_date type timestring iso_rr_path [***]",
"                 Alter the date entries of a file in the ISO image. type is",
"                 one of \"a\", \"m\", \"b\" for:",
"                 access time, modification time, both times.",
"  -alter_date_r type timestring iso_rr_path [***]",
"                 Like -alter_date but affecting all files below directories.",
"  -hide on|iso_rr:joliet:hfsplus|off iso_rr_path [***]",
"                 Keep names of files out of directory trees, but store their",
"                 data content in the image.",
"  -find iso_rr_path [test [op] [test ...]] [-exec action [params]]",
"                 performs an action on files below the given directory in",
"                 the ISO image. Tests:",
"                   -name pattern, -wholename pattern, -disk_name pattern,",
"                   -type b|c|d|p|f|l|s|e, -pending_data, -hidden,",
"                   -lba_range start count, -damaged, -has_acl, -has_xattr,",
"                   -has_aaip, -has_filter, -has_md5, -has_any_xattr,",
"                   -has_hfs_crtp, -has_hfs_bless,",
"                   -prune, -decision yes|no, -true, -false",
"                 Operators: -not, -or, -and, -sub, (, -subend, ),",
"                   -if, -then, -elseif, -else, -endif",
"                 Action may be one of: echo, chown, chown_r, chgrp, chgrp_r",
"                   chmod, chmod_r, alter_date, alter_date_r, lsdl, compare,",
"                   rm, rm_r, compare, update, report_damage, report_lba,",
"                   getfacl, setfacl, getfattr, setfattr, get_any_xattr,",
"                   list_extattr, get_md5, check_md5, make_md5,",
"                   set_hfs_crtp, get_hfs_crtp, set_hfs_bless, get_hfs_bless,",
"                   set_filter, show_stream, mkisofs_r, hide, find.",
"                 params are their parameters except iso_rr_path.",
"  -mkdir iso_rr_path [...]",
"                 Create empty directories if they do not exist yet.",
"  -lns target_text iso_rr_path",
"                 Create a symbolic link pointing to target_text",
"  -rmdir iso_rr_path [***]",
"                 Delete empty directories.",
"  -clone iso_rr_path_original iso_rr_path_copy",
"                 Create an ISO copy of an ISO file or ISO directory tree.",
"  -cp_clone iso_rr_path_original [***] iso_rr_path_dest",
"                 Create ISO to ISO copies according to the rules of cp -r.",
"",
"  --             Default list delimiter marking the end of command parameter",
"                 lists. It may be changed by command -list_delimiter.",
"",
"  -not_paths disk_path [***]",
"                 Add the given paths to the list of excluded absolute paths.",
"  -not_leaf pattern",
"                 Add the given pattern to the list of leafname exclusions.",
"  -not_list disk_path",
"                 Read lines from disk_path and use as -not_paths (with \"/\")",
"                 or as -not_leaf (without \"/\").",
"  -quoted_not_list disk_path",
"                 Like -not_list but with line rules as -dialog \"on\".",
"  -not_mgt \"reset\"|\"on\"|\"off\"|\"param_on\"|\"subtree_on\"|\"ignore_on\"",
"                 Control effect of exclusion lists.",
"  -follow \"on\"|\"pattern:param:link:mount:limit=#\"|\"default\"|\"off\"",
"                 Follow symbolic links and mount points within disk_path.",
"  -overwrite \"on\"|\"nondir\"|\"off\"",
"                 Allow or disallow to overwrite existing files in ISO image.",
"  -split_size number[\"k\"|\"m\"]",
"                 Set the threshold for automatic splitting of regular files.",
"  -reassure \"on\"|\"tree\"|\"off\"",
"                 If \"on\" then ask the user for \"y\" or \"n\" with any",
"                 file before deleting or overwriting it in the ISO image.",
"",
"Filter commands:",
"External filter processes may produce synthetic file content by reading the",
"original content from stdin and writing to stdout whatever they want.",

#ifdef Xorriso_allow_external_filterS

"  -external_filter name option[:option] program_path [arguments] --",
"                 Define an external filter. Options are: suffix=...: ",
"                 remove_suffix:if_nonempty:if_reduction:if_block_reduction.",
"  -unregister_filter name",
"                 Undefine an external filter.",
"  -close_filter_list",
"                 Irrevocably ban -external_filter and -unregister_filter.",

#else

"Sorry: The use of external filters was not enabled at compile time.",
"       E.g. by ./configure option --enable-external-filters",

#endif /* ! Xorriso_allow_external_filterS */

"  -set_filter name iso_rr_path [***]",
"                 Apply a defined filter to the given data files.",
"                 Special name \"--remove-all-filters\" revokes filtering.",
"                 Builtin filters are --gzip , --gunzip, --zisofs .",
"  -set_filter_r name iso_rr_path [***]",
"                 Like -set_filter but affecting all files below directories.",
"",
"zisofs is a compression format which is recognized by some Linux kernels.",
"xorriso supports it by builtin filter \"--zisofs\" which is to be applied by",
"the user, and by \"--zisofs-decode\" which is applied automatically when",
"compressed content is detected with a file in the ISO image.",
"  -zisofs option[:options]",
"                 Set global zisofs parameters:",
"                   level=0|...|9 , block_size=32k|64k|128k , by_magic=on|off",
"",
"Write-to-media commands:",
"  -rollback      Discard the manipulated ISO image and reload it.",
"",
"  -changes_pending \"no\"|\"yes\"|\"mkisofs_printed\"|\"show_status\"",
"                 Override the automatically determined change status of the",
"                 loaded image, or show the current status.",
"  -commit        Perform the write operation if changes are pending.",
"                 Then perform -dev outdrive.",
"                 Hint: To perform a final write operation with no new -dev",
"                       and no new loading of image, execute command -end.",
"  -commit_eject  \"in\"|\"out\"|\"all\"|\"none\"",
"                 Like -commit but rather eject than load image from outdrive.",
"                 Give up any unejected drive afterwards.",
"  -write_type \"auto\"|\"tao\"|\"sao/dao\"",
"                 Set write type for CD-R[W], DVD-R[W], DVD+R, BD-R.",
"  -close \"on\"|\"off\"",
"                 If \"on\" then mark the written medium as not appendable.",
"  -padding number[\"k\"|\"m\"]|\"included\"|\"appended\"",
"                 Append extra bytes to image stream. (Default is 300k)",
"  -dummy \"on\"|\"off\"",
"                 If \"on\" simulate burning. Refuse if medium cannot simulate.",
"  -speed number[\"k/s\"|\"m/s\"|\"[x]CD\"|\"[x]DVD\"|\"[x]BD\"]",
"                 Set the burn speed. Default is 0 = maximum speed.",
"  -stream_recording \"on\"|\"off\"",
"                 Try to circumvent slow checkread on DVD-RAM, BD-RE, BD-R.",
"  -dvd_obs \"default\"|\"32k\"|\"64k\"",
"                 Set number of bytes per DVD/BD write operation.",
"  -stdio_sync \"on\"|\"off\"|number",
"                 Set number of bytes after which to force output to stdio",
"                 pseudo drives. \"on\" is the same as 16m.",
"  -fs number[\"k\"|\"m\"]",
"                 Set the size of the fifo buffer. (Default is 4m)",
"  -eject \"in\"|\"out\"|\"all\"",
"                 Immediately eject the medium in -indev, resp. -outdev,",
"                 resp. both.",
"",
"Navigation commands:",
"",
"  -cd iso_rr_path  Change working directory in the ISO image. iso_rr_paths",
"                 which do not begin with '/' will be inserted beginning at",
"                 the path given with -cd. -ls patterns will eventually",
"                 looked up at this path.",
"  -cdi disk_path   Same as -cd disk_path",
"  -cdx disk_path  Change the current working directory in the local",
"                 filesystem. disk_paths which do not begin with '/'",
"                 will be looked up beginning at the path given with -cdx.",
"                 -lsx patterns will eventually be looked up at this path.",
"  -pwd           tells the current working directory in the ISO image.",
"  -pwdi          same as -pwd.",
"  -pwdx          tells the current working directory in the local filesystem.",
"",
"  -iso_rr_pattern \"on\"|\"ls\"|\"off\"",
"                 Enable or disable pattern expansions for ISO image commands",
"                 marked by [***]. \"ls\" restricts it to -ls and -du.",
"  -disk_pattern \"on\"|\"ls\"|\"off\"",
"                 Enable or disable pattern expansions for local filesystem",
"                 commands marked by [***]. \"ls\" restricts to -ls*x and -du*x.",
"",
"  -ls pattern [***]  lists files of the ISO image which match one of the",
"                 given shell parser patterns. (I.e. wildcards '*' '?').",
"                 Directories are listed by their content.",
"  -lsd pattern [***]   like -ls but listing directories as single items.",
"  -lsl pattern [***]   like -ls but also telling some file attributes.",
"  -lsdl pattern [***]  like -lsd but also telling some file attributes.",
"",
"  -lsx pattern [***]   lists files of the local filesystem which match one",
"                 of the patterns. Directories are listed by their content.",
"  -lsdx pattern [***]  like -lsx but listing directories as single items.",
"  -lslx pattern [***]  like -lsx but also telling some file attributes.",
"  -lsdlx pattern [***] like -lsdx but also telling some file attributes.",
"  -getfacl pattern [***]     list eventual ACLs of the given files.",
"  -getfacl_r pattern [***]   like -getfacl but listing whole file trees.",
"  -getfattr pattern [***]    list eventual xattr of the given files.",
"  -getfxattr_r pattern [***] like -getfxattr but listing whole file trees.",
"",
"  -du pattern [***]  recursively lists sizes of files or directories in the",
"                 ISO image which match one of the shell parser patterns.",
"  -dux pattern [***]  recursively lists sizes of files or directories in the",
"                 local filesystem which match one of the shell parser",
"                 patterns.",
"  -dus pattern [***]  like -du but summing up subdirectories without",
"                 listing them explicitely.",
"  -dusx pattern [***]  like -dux but summing up subdirectories without",
"                 listing them explicitely.",
"",
"  -findx disk_path [-name pattern] [-type t] [-exec action [params]]",
"                 Like -find but operating on local filesystem. Most -exec",
"                 actions are defaulted to action echo. Supported actions are:",
"                  in_iso, not_in_iso, is_full_in_iso, add_missing,",
"                  empty_iso_dir",
"",
"  -compare disk_path iso_rr_path",
"                 compare attributes and in case of regular data files the",
"                 content of filesystem object and ISO object.",
"  -compare_r disk_path iso_rr_path",
"                 Like -compare but affecting all files below directories.",
"  -compare_l disk_prefix iso_rr_prefix disk_path [***]",
"                 Performs -compare_r with each disk_path.",
"",
"  -show_stream iso_rr_path [***]",
"                 Show content stream chain of data files in the ISO image.",
"  -show_stream_r iso_rr_path [***]",
"                 Like -show_stream but affecting all files below directories.",
"",
"Restore commands which copy file objects from ISO image to disk filesystem:",
"  -osirrox \"on\"|\"device_files\"|\"off\"|\"blocked\"|\"unblock\"|\"banned\"",
"           [:\"concat_split_on\"|\"concat_split_off\"]",
"           [:\"auto_chmod_on\"|\"auto_chmod_off\"]",
"           [:\"sort_lba_on\"|\"sort_lba_off\"]",
"           [:\"strict_acl_on\"|\"strict_acl_off\"]",
"                 By default \"off\" the inverse operation of xorriso from ISO",
"                 image to disk filesystem is disabled. \"on\" allows xorriso",
"                 to create, overwrite, delete files in the disk filesystem.", 
"                 \"banned\" is irrevocably \"off\". \"blocked\" can only be",
"                 revoked by \"unblock\". (\"device_files\" is dangerous.)",
"  -extract iso_rr_path disk_path",
"                 Copy tree under iso_rr_path onto disk address disk_path.",
"                 This avoids the pitfalls of cp -r addressing rules.",
"  -extract_l iso_rr_prefix disk_prefix iso_rr_path [***]",
"                 Perform -extract with each iso_rr_path.",
"  -extract_single iso_rr_path disk_path",
"                 Like -extract but with directory do not restore sub tree.",
"  -extract_cut iso_rr_path byte_offset byte_count disk_path",
"                 Copy a byte interval from iso_rr_path to disk_path.",
"                 This is governed in part by -check_media_defaults.",
"  -cpx iso_rr_path [***] disk_path",
"                 Copy leaf file objects from ISO image to disk filesystem.",
"  -cpax iso_rr_path [***] disk_path",
"                 Like -cpx but trying to restore timestamps and ownership.",
"  -cp_rx iso_rr_path [***] disk_path",
"                 Copy directory trees from ISO image to disk filesystem.",
"  -cp_rax iso_rr_path [***] disk_path",
"                 Like -cp_rx but trying to restore timestamps and ownership.",
"  -paste_in iso_rr_path disk_path byte_offset byte_count",
"                 Copy ISO file content into a byte interval of a disk file.",
"  -mount drive entity id path",
"                 Like -mount_cmd but actually performing that command if",
"                 not setuid or setgid is active.",
"",
"Evaluation of readability:",
"  -check_media [options] --",
"                 Try to read data blocks from the medium and report about the",
"                 outcome. Several options modify the behavior:",
"                  use=indev|outdev , what=track|session ,",
"                  min_lba=blockadr , max_lba=blockadr ,",
"                  abort_file=path , time_limit=seconds , item_limit=number ,",
"                  retry=on|off|default , data_to=filepath ,",
"                  sector_map=filepath , map_with_volid=on|off ,",
"                  patch_lba0=on|off|force|blockadr[:force] ,",
"                  report=blocks|files|blocks_files event=severity ,",
"                  bad_limit=quality , slow_limit=seconds , chunk_size=bytes",
"  -check_media_defaults [options] --",
"                 Preset options for runs of -check_media and -extract_cut.",
"",
"Compatibility emulation (option list may be ended by list delimiter --):",
"  -as mkisofs  [-help|-version|-o|-R|-r|-J|-V|-P|-f|-m|-exclude-list|",
"                -no-pad|-M|-C|-graft-points|-path-list|pathspecs|-z|",
"                -no-emul-boot|-b|-c|-boot-info-table|-boot-load-size|-G|...]",
"              Perform some mkisofs gestures, understand pathspecs as mkisofs",
"              does. Commit happens outside emulation at usual occasions.",
"              For a list of options see -as mkisofs -help.",
"  -read_mkisofsrc",
"              Read and interpret the .mkisofsrc configuration file.",
"  -as cdrecord [-help|-v|dev=|speed=|blank=|fs=|-eject|-atip|padsize=|-multi]",
"               path|-",
"              Perform some cdrecord gestures, eventually write at most one",
"              data track to blank, appendable or overwriteable media.",
"  -pacifier \"xorriso\"|\"cdrecord\"|\"mkisofs\"",
"              Choose format of UPDATE pacifier during write operations.",
"",
"General commands:",
"  -help       Print this text",
"  -abort_on severity   Set the threshhold for events to abort the program.",
"              Useful severities: NEVER, ABORT, FATAL, FAILURE, SORRY, WARNING",
"  -return_with severity exit_value   Set the threshhold for events to return",
"              at program end the given exit_value even if not aborted.",
"              exit_value may be 0 or 32 to 63.",
"  -report_about severity   Set the threshhold for events to be reported.",
"              Use -abort_on severities or: HINT, NOTE, UPDATE, DEBUG, ALL",
"  -signal_handling \"on\"|\"off\"|\"sig_dfl\"|\"sig_ign\"",
"              Handling of signals. Default \"on\" uses libburn handler.",
"  -error_behavior \"image_loading\"|\"file_extraction\" behavior",
"              Behavior \"best_effort\" is most endurant but may produce",
"              results which are correct only on the first glimpse.",
"  -dialog \"on\"|\"off\"|\"single_line\"",
"              After all program arguments are processed, enter dialog mode.",
"              \"single_line\" does not support newline characters within",
"              open quotation marks and no line continuation by trailing \\.",
"  -page len width  Prompt user after len output lines (0=no prompt).",
"              width (default 80) can adjust line number computation",
"              to the output terminal's line width.",
#ifdef Xorriso_with_readlinE
"  -use_stdin  Use raw standard input even if libreadline is available",
"  -use_readline  Use libreadline for dialog if available",
"  -history text  Copy text into libreadline history. This command",
"              itself is not copied to the history list.",
#endif /* Xorriso_with_readlinE */
"  -backslash_codes \"on\"|\"off\"|",
"                   \"in_double_quotes\"|\"in_quotes\"|\"with_quoted_input\"",
"                   [:\"with_program_arguments\"][:\"encode_output\"]",
"              Disable or enable interpretation of \\a \\b \\e \\f \\n \\r \\t \\v",
"              \\\\ \\NNN \\xNN \\cC in input or program arguments.",
"  -pkt_output \"on\"|\"off\"  Direct output to stdout and prefix each line",
"              by a short header which tells channel id and a mode number.",
"              Each such output packet is finalized by a newline.",
"              Channel ids are 'R:' for result lines, 'I:' for notes",
"              and error messages, 'M:' for -mark texts. Bit 0 of the",
"              mode number tells whether the newline is also part of the",
"              packet payload. Example of a info message with newline:",
"                I:1: enter option text :",
"              -pkt_output:on is intended for use by frontend programs.",
"  -msg_op \"start_sieve\"|\"read_sieve\"|\"clear_sieve\"|\"end_sieve\"|",
"          \"parse\"|\"parse_bulk\"|\"compare_sev\"|\"list_sev\" param_text",
"              Enable, use, or disable message sieve. Or parse lines into",
"              words. Or compare or list severity names.",
"  -launch_frontend program [args ...] --",
"              Start a program, connect its stdin to xorriso stdout and",
"              stderr, connect its stdout to xorriso stdin.",
"              Use any given parameters as arguments for the started program.",
"  -logfile channel fileaddress  Copy output of a channel to the given file.",
"              channel may be 'R','I','M' as with -pkt_output or '.'",
"              for the consolidated -pkt_output stream.",
"  -mark text  If text is not empty it will get put out each time a command",
"              is completed.",
"  -temp_mem_limit number[\"k\"|\"m\"]",
"              Set the maximum size for pattern expansion. (Default is 16m)",
"  -prog text  Use text as this program's name in subsequent messages",
"  -prog_help text  Use text as this program's name and perform -help",
"  -status mode|filter  Report the current settings of persistent commands.",
"              Modes:",
"                 short... print only important or altered settings",
"                 long ... print settings even if they have default values",
"                 long_history  like long plus -history: lines",
"              Filters begin with '-' and are compared literally against the",
"              output lines of -status long_history. A line is put out only",
"              if its start matches the filter.",
"  -status_history_max number  Maximum number of history lines to be reported",
"              with -status:long_history",
"  -options_from_file fileaddress",
"              Reads lines from the given file and executes them as commands.",
"  -no_rc      Only if used as first program argument, this command",
"              prevents reading and interpretation of these startup files:",
"               /etc/default/xorriso , /etc/opt/xorriso/rc",
"               /etc/xorriso/xorriso.conf , $HOME/.xorrisorc",
"  -print text",
"              Print a text to result channel.",
"  -print_info text",
"              Print a text to info channel.",
"  -print_mark text",
"              Print a text to mark channel.",
"  -prompt text",
"              Wait for Enter key resp. for a line of input at stdin.",
"  -sleep number",
"              Do nothing during the given number of seconds.",
"  -errfile_log mode path|channel",
"              Log disk paths of files involved in problem events.",
"  -session_log path",
"              Set path of a file where a log record gets appended after",
"              each session. Form: timestamp start_lba size volume-id", 
"  -scsi_log \"on\"|\"off\"",
"              Enable or disable logging of SCSI commands to stderr.",
"  # any text  Is ignored. In dialog mode the input line will be stored in",
"              the eventual readline history, nevertheless.",
"  -list_extras code",
"              Tell whether certain extra features were enabled at compile",
"              time. Code \"all\" lists all features and a headline. Other",
"              codes pick a single feature. \"codes\" lists the known codes.",
"  -list_arg_sorting",
"              Print the sorting order of xorriso commands with option -x.",
"  -version    Tell program and version number",
"  -end        End program. Commit eventual pending changes.",
"  -rollback_end",
"              End program. Discard pending changes.",
"",
"",
"Command -page causes a user prompt after the given number of result lines.",
"Empty input resumes output until the next prompt. Other input may be:",
"  @     suppresses paging until the current action is done",
"  @@    suppresses further result output but continues the action",
"  @@@   aborts the current action",
"  other aborts the current action and executes input as new command",
"",

#endif /* ! Xorriso_no_helP */

"@ENDE_OF_HELPTEXT_(HOPEFULLY_UNIQUELY_SILLY_TEXT)@"
 };

 char *tpt= NULL;
 int i,pass;

 Xorriso_restxt(xorriso,"\n"); 
 sprintf(xorriso->result_line,"usage: %s [settings|actions]\n",
         xorriso->progname); 
 Xorriso_result(xorriso,0);
 Xorriso_restxt(xorriso,"\n"); 
 for(pass=0;pass<1;pass++) {
   for(i=0;1;i++) {
     if(pass==0)
       tpt= text[i];
     
     if(strcmp(tpt,"@ENDE_OF_HELPTEXT_(HOPEFULLY_UNIQUELY_SILLY_TEXT)@")==0)
   break;
     sprintf(xorriso->result_line,"%s\n",tpt);
     Xorriso_result(xorriso,0);
     if(xorriso->request_to_abort)
       return(1);
   }
 }
 Xorriso_restxt(xorriso,"\n"); 
 return(1);
}


/* Option -hfsplus "on"|"off" */
int Xorriso_option_hfsplus(struct XorrisO *xorriso, char *mode, int flag)
{
 if(strcmp(mode, "off")==0)
   xorriso->do_hfsplus= 0;
 else if(strcmp(mode, "on")==0)
   xorriso->do_hfsplus= 1;
 else {
   sprintf(xorriso->info_text, "-hfsplus: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(1);
}


/* Option -hide */
int Xorriso_option_hide(struct XorrisO *xorriso, char *hide_state,
                        int argc, char **argv, int *idx, int flag)
{
 int i, ret, end_idx, optc= 0, was_failure= 0, fret, hide_mode;
 char **optv= NULL;

 ret= Xorriso_opt_args(xorriso, "-hide", argc, argv, *idx, &end_idx, &optc,
                       &optv, 0);
 if(ret<=0)
   goto ex;
 hide_mode= Xorriso__hide_mode(hide_state, 0);
 if(hide_mode < 0) {
   sprintf(xorriso->info_text, "-hide : unknown hide state ");
   Text_shellsafe(hide_state, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   goto ex;
 }
 for(i= 0; i<optc; i++) {
   ret= Xorriso_set_hidden(xorriso, NULL, optv[i], hide_mode, 0);
   if(ret>0 && !xorriso->request_to_abort)
 continue; /* regular bottom of loop */

   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret>=0)
 continue;
   ret= 0; goto ex;
 }
 ret= 1;
ex:;
 (*idx)= end_idx;
 Xorriso_opt_args(xorriso, "-hide", argc, argv, *idx, &end_idx, &optc, &optv,
                  256);
 if(ret<=0)
   return(ret);
 return(!was_failure);
}


/* Option -history */
int Xorriso_option_history(struct XorrisO *xorriso, char *line, int flag)
{
 Xorriso_dialog_input(xorriso,line,strlen(line)+1,2);
 return(1);
}

