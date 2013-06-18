
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2013 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains functions which operate on ISO images and their
   global properties.
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

#include <sys/wait.h>

#include "xorriso.h"
#include "xorriso_private.h"
#include "xorrisoburn.h"

#include "lib_mgt.h"
#include "iso_img.h"
#include "iso_tree.h"
#include "drive_mgt.h"


int Xorriso_set_ignore_aclea(struct XorrisO *xorriso, int flag)
{
 int ret, hflag;
 IsoImage *volume;

 ret= Xorriso_get_volume(xorriso, &volume, 1); 
 if(ret<=0)
   return(ret);
 hflag= (~xorriso->do_aaip) & 1;
 if((xorriso->ino_behavior & (1 | 2)) && !(xorriso->do_aaip & (4 | 16)))
   hflag|= 2; 
 iso_image_set_ignore_aclea(volume, hflag);
 return(1);
}


int Xorriso_update_volid(struct XorrisO *xorriso, int flag)
{
 int gret, sret= 1;

 gret= Xorriso_get_volid(xorriso, xorriso->loaded_volid, 0);
 if(gret<=0 || (!xorriso->volid_default) || xorriso->loaded_volid[0]==0)
   sret= Xorriso_set_volid(xorriso, xorriso->volid, 1);
 return(gret>0 && sret>0);
} 
 

int Xorriso_create_empty_iso(struct XorrisO *xorriso, int flag)
{
 int ret;
 IsoImage *volset;
 struct isoburn_read_opts *ropts;
 struct burn_drive_info *dinfo= NULL;
 struct burn_drive *drive= NULL;

 if(xorriso->out_drive_handle != NULL) {
   ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                  "on attempt to attach volset to drive", 2);
   if(ret<=0)
     return(ret);
 }
 if(xorriso->in_volset_handle!=NULL) {
   iso_image_unref((IsoImage *) xorriso->in_volset_handle);
   xorriso->in_volset_handle= NULL;
   Sectorbitmap_destroy(&(xorriso->in_sector_map), 0);
   Xorriso_destroy_di_array(xorriso, 0);
   Xorriso_destroy_hln_array(xorriso, 0);
   xorriso->loaded_volid[0]= 0;
   xorriso->volset_change_pending= 0;
   xorriso->boot_count= 0;
   xorriso->no_volset_present= 0;
 }

 ret= isoburn_ropt_new(&ropts, 0);
 if(ret<=0)
   return(ret);
 /* Note: no return before isoburn_ropt_destroy() */
 isoburn_ropt_set_extensions(ropts, isoburn_ropt_pretend_blank);
 isoburn_ropt_set_input_charset(ropts, xorriso->in_charset);
 isoburn_ropt_set_data_cache(ropts, 1, 1, 0);
 isoburn_set_read_pacifier(drive, NULL, NULL);
 ret= isoburn_read_image(drive, ropts, &volset);
 Xorriso_process_msg_queues(xorriso,0);
 isoburn_ropt_destroy(&ropts, 0);
 if(ret<=0) {
   sprintf(xorriso->info_text, "Failed to create new empty ISO image object");
   Xorriso_report_iso_error(xorriso, "", ret, xorriso->info_text, 0, "FATAL",
                            0);
   return(-1);
 }
 xorriso->in_volset_handle= (void *) volset;
 xorriso->in_sector_map= NULL;
 Xorriso_update_volid(xorriso, 0);
 xorriso->volset_change_pending= 0;
 xorriso->boot_count= 0;
 xorriso->no_volset_present= 0;
 return(1);
}


int Xorriso_record_boot_info(struct XorrisO *xorriso, int flag)
{
 int ret;
 struct burn_drive_info *dinfo;
 struct burn_drive *drive;
 IsoImage *image;
 ElToritoBootImage *bootimg;
 IsoFile *bootimg_node;
 IsoBoot *bootcat_node;

 xorriso->loaded_boot_bin_lba= -1;
 xorriso->loaded_boot_cat_path[0]= 0;
 ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                "on attempt to record boot LBAs", 0);
 if(ret<=0)
   return(0);
 image= isoburn_get_attached_image(drive);
 if(image == NULL)
   return(0);
 ret= iso_image_get_boot_image(image, &bootimg,
                               &bootimg_node, &bootcat_node);
 iso_image_unref(image); /* release obtained reference */
 if(ret != 1)
   return(0);
 if(bootimg_node != NULL)
   Xorriso__file_start_lba((IsoNode *) bootimg_node,
                           &(xorriso->loaded_boot_bin_lba), 0);
 if(bootcat_node != NULL)
   Xorriso_path_from_lba(xorriso, (IsoNode *) bootcat_node, 0,
                         xorriso->loaded_boot_cat_path, 0);
 return(1);
}


int Xorriso_assert_volid(struct XorrisO *xorriso, int msc1, int flag)
{
 int ret, image_blocks;
 char volid[33];
 struct burn_drive_info *dinfo;
 struct burn_drive *drive;

 if(xorriso->assert_volid[0] == 0)
   return(1);
 ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                "on attempt to perform -assert_volid", 0);
 if(ret<=0)
   return(0);
 ret= isoburn_read_iso_head(drive, msc1, &image_blocks, volid, 1);
 Xorriso_process_msg_queues(xorriso,0);
 if(ret <= 0) {
   sprintf(xorriso->info_text,
           "-assert_volid: Cannot determine Volume Id at LBA %d.", msc1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0,
                       xorriso->assert_volid_sev, 0);
   return(0);
 }
 ret= Sregex_match(xorriso->assert_volid, volid, 0);
 if(ret < 0)
   return(2);
 if(ret == 0) {
   strcpy(xorriso->info_text,
          "-assert_volid: Volume id does not match pattern: ");
   Text_shellsafe(xorriso->assert_volid, xorriso->info_text, 1);
   strcat(xorriso->info_text, " <> ");
   Text_shellsafe(volid, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0,
                       xorriso->assert_volid_sev, 0);
   return(0);
 }
 return(ret);
}


/* @return <0 yes , 0 no , <0 error */
int Xorriso_is_isohybrid(struct XorrisO *xorriso, IsoFile *bootimg_node,
                         int flag)
{
 int ret;
 unsigned char buf[68];
 void *data_stream= NULL;

 ret= Xorriso_iso_file_open(xorriso, "", (void *) bootimg_node,
                            &data_stream, 1);
 if(ret <= 0)
   return(-1);
 ret= Xorriso_iso_file_read(xorriso, data_stream, (char *) buf, 68, 0);
 Xorriso_iso_file_close(xorriso, &data_stream, 0);
 if(ret <= 0)
   return(0);
 if(buf[64] == 0xfb && buf[65] == 0xc0 && buf[66] == 0x78 && buf[67] == 0x70)
   return(1);
 return(0);
}


/* @return <0 yes , 0 no , <0 error */
int Xorriso_is_grub2_elto(struct XorrisO *xorriso, IsoFile *bootimg_node,
                          int flag)
{
#define Xorriso_grub2_boot_info_poS 2548
 int ret, i;
 unsigned char buf[Xorriso_grub2_boot_info_poS + 8];
 void *data_stream= NULL;
 off_t blk;

 ret= Xorriso_iso_file_open(xorriso, "", (void *) bootimg_node,
                            &data_stream, 1);
 if(ret <= 0)
   return(-1);
 ret= Xorriso_iso_file_read(xorriso, data_stream, (char *) buf,
                            Xorriso_grub2_boot_info_poS  + 8, 0);
 Xorriso_iso_file_close(xorriso, &data_stream, 0);
 if(ret < Xorriso_grub2_boot_info_poS + 8)
   return(0);

 /* >>> ??? Check for some id */;

 /* Check whether it has grub2_boot_info patching */
 blk= 0;
 for(i= 0; i < 8; i++)
   blk|= buf[Xorriso_grub2_boot_info_poS + i] << (8 * i);
 blk-= 5;
 blk/= 4;
 if(blk != (off_t) xorriso->loaded_boot_bin_lba)
   return(0);
 return(1);
}


int Xorriso_image_has_md5(struct XorrisO *xorriso, int flag)
{
 int ret;
 IsoImage *image;
 uint32_t start_lba, end_lba;
 char md5[16];

 ret= Xorriso_get_volume(xorriso, &image, 0);
 if(ret<=0)
   return(ret);
 ret= iso_image_get_session_md5(image, &start_lba, &end_lba, md5, 0);
 Xorriso_process_msg_queues(xorriso,0);
 if(ret <= 0)
   return(0);
 return(1);
}


static const char *un0(const char *text)
{
 if(text == NULL)
   return("");
 return(text);
}


int Xorriso_pvd_info(struct XorrisO *xorriso, int flag)
{
 int ret, msc1= -1, msc2, i;
 IsoImage *image;
 struct burn_drive_info *dinfo;
 struct burn_drive *drive;
 char *msg, block_head[8], *crt, *mdt, *ext, *eft;
 off_t head_count;

 msg= xorriso->result_line;
 ret= Xorriso_get_volume(xorriso, &image, 0);
 if(ret<=0)
   return(ret);
 ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive, "", 16);
 if(ret > 0) {
   ret= Xorriso_msinfo(xorriso, &msc1, &msc2, 1 | 4);
   if(ret<0)
     return(ret);
   Xorriso_toc(xorriso, 128);
   if(msc1 >= 0) {
     for(i = msc1 + 16; i < msc1 + 32; i++) {
       ret= burn_read_data(drive, (off_t) i * (off_t) 2048, block_head,
                           (off_t) sizeof(block_head), &head_count, 2);
       if(ret <= 0) {
         i= msc1 + 32;
     break;
       }
       if(block_head[0] == 1 && strncmp(block_head + 1, "CD001", 5) == 0)
     break;
     }
     if(i < msc1 + 32) {
       sprintf(msg, "PVD address  : %ds\n", i);
       Xorriso_result(xorriso,0);
     }
   }
 }
 sprintf(msg, "Volume Id    : %s\n", un0(iso_image_get_volume_id(image)));
 Xorriso_result(xorriso,0);
 sprintf(msg, "Volume Set Id: %s\n", xorriso->volset_id);
 Xorriso_result(xorriso,0);
 sprintf(msg, "Publisher Id : %s\n", xorriso->publisher);
 Xorriso_result(xorriso,0);
 sprintf(msg, "Preparer Id  : %s\n",
         un0(iso_image_get_data_preparer_id(image)));
 Xorriso_result(xorriso,0);
 sprintf(msg, "App Id       : %s\n", xorriso->application_id);
 Xorriso_result(xorriso,0);
 sprintf(msg, "System Id    : %s\n", xorriso->system_id);
 Xorriso_result(xorriso,0);
 sprintf(msg, "CopyrightFile: %s\n", xorriso->copyright_file);
 Xorriso_result(xorriso,0);
 sprintf(msg, "Abstract File: %s\n", xorriso->abstract_file);
 Xorriso_result(xorriso,0);
 sprintf(msg, "Biblio File  : %s\n", xorriso->biblio_file);
 Xorriso_result(xorriso,0);

 ret= iso_image_get_pvd_times(image, &crt, &mdt, &ext, &eft);
 if(ret != ISO_SUCCESS)
   crt= mdt= ext= eft= "";
 sprintf(msg, "Creation Time: %s\n", crt);
 Xorriso_result(xorriso,0);
 sprintf(msg, "Modif. Time  : %s\n", mdt);
 Xorriso_result(xorriso,0);
 sprintf(msg, "Expir. Time  : %s\n", ext);
 Xorriso_result(xorriso,0);
 sprintf(msg, "Eff. Time    : %s\n", eft);
 Xorriso_result(xorriso,0);

 return(1);
}


/* @param flag bit0= do not mark image as changed */
int Xorriso_set_volid(struct XorrisO *xorriso, char *volid, int flag)
{
 int ret;
 IsoImage *volume;

 if(xorriso->in_volset_handle == NULL)
   return(2);
 ret= Xorriso_get_volume(xorriso, &volume, 0);
 if(ret<=0)
   return(ret);
 iso_image_set_volume_id(volume, volid);
 if(!(flag&1))
   Xorriso_set_change_pending(xorriso, 1);
 Xorriso_process_msg_queues(xorriso,0);
 sprintf(xorriso->info_text,"Volume ID: '%s'",iso_image_get_volume_id(volume));
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
 return(1);
}


int Xorriso_get_volid(struct XorrisO *xorriso, char volid[33], int flag)
{
 int ret;
 IsoImage *volume;

 ret= Xorriso_get_volume(xorriso, &volume, 0);
 if(ret<=0)
   return(ret);
 strncpy(volid, iso_image_get_volume_id(volume), 32);
 volid[32]= 0;
 return(1);
}


/* 
 bit0= do only report non-default settings
 bit1= do only report to fp
 bit2= is_default
 bit3= append -boot_image any next
 bit4= eventually concentrate boot options
*/
int Xorriso_boot_item_status(struct XorrisO *xorriso, char *cat_path,
                             char *bin_path, int platform_id,
                             int patch_isolinux, int emul, off_t load_size,
                             unsigned char *id_string,
                             unsigned char *selection_crit, char *form,
                             char *filter, FILE *fp, int flag)
{
 int is_default, no_defaults, i, is_default_id= 0, ret;
 char *line, *bspec= NULL, zeros[28];
 off_t file_size;
 struct stat stbuf;

 Xorriso_alloc_meM(bspec, char, SfileadrL + 80);

 no_defaults= flag & 1;
 line= xorriso->result_line;

 if(flag & 16) {
   /* Allow to concentrate boot options. */
   memset(zeros, 0, 28);
   if(memcmp(id_string, zeros, 28) == 0 &&
      memcmp(selection_crit, zeros, 20) == 0)
     is_default_id= 1;

   /* -boot_image isolinux dir= ... */
   bspec[0]= 0;
   if(strcmp(form, "isolinux") != 0 && strcmp(form, "any") != 0)
     ;
   else if(strcmp(bin_path, "/isolinux.bin") == 0 &&
      strcmp(cat_path, "/boot.cat") == 0)
     strcpy(bspec, "dir=/");
   else if(strcmp(bin_path, "/isolinux/isolinux.bin") == 0 &&
           strcmp(cat_path, "/isolinux/boot.cat") == 0)
     strcpy(bspec, "dir=/isolinux");
   else if(strcmp(xorriso->boot_image_bin_path,
                  "/boot/isolinux/isolinux.bin") == 0
           && strcmp(xorriso->boot_image_cat_path,
                     "/boot/isolinux/boot.cat") == 0)
     strcpy(bspec, "dir=/boot/isolinux");
   memset(zeros, 0, 28);
   if(bspec[0] && platform_id == 0 && (patch_isolinux & 3) &&
      load_size == 2048 && is_default_id && emul == 0) {
     sprintf(line, "-boot_image isolinux %s\n", bspec);
     Xorriso_status_result(xorriso,filter,fp,flag&2); 
     {ret= 1; goto ex;};
   }

   file_size= 0;
   ret= Xorriso_iso_lstat(xorriso, bin_path, &stbuf, 2 | 4);
   if(ret == 0)
     file_size= ((stbuf.st_size / (off_t) 512) +
                !!(stbuf.st_size % (off_t) 512)) * 512;
   if(platform_id == 0xef && !(patch_isolinux & 3) &&
      load_size == file_size && is_default_id && emul == 0) {
     sprintf(line, "-boot_image any efi_path=");
     Text_shellsafe(bin_path, line, 1);
     strcat(line, "\n");
     Xorriso_status_result(xorriso,filter,fp,flag&2);
     {ret= 1; goto ex;};
   }
 }

 is_default= (bin_path[0] == 0) || (flag & 4);
 sprintf(line, "-boot_image %s bin_path=", form);
 Text_shellsafe(bin_path, line, 1);
 strcat(line, "\n");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (emul == 0);
 sprintf(line, "-boot_image %s emul_type=%s\n",
      form, emul == 2 ? "diskette" : emul == 1 ? "hard_disk" : "no_emulation");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 
 is_default= (platform_id == 0 || (flag & 4));
 sprintf(line, "-boot_image %s platform_id=0x%-2.2x\n", form, platform_id);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2); 

 is_default= ((patch_isolinux & 1) == 0 || bin_path[0] == 0 || (flag & 4));
 sprintf(line, "-boot_image %s boot_info_table=%s\n",
               (patch_isolinux & 2) ? "grub" : form,
               (patch_isolinux & 1) ? "on" : "off");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2); 
 
 is_default= ((patch_isolinux & 512) == 0 || bin_path[0] == 0 || (flag & 4));
 sprintf(line, "-boot_image grub grub2_boot_info=%s\n",
               (patch_isolinux & 512) ? "on" : "off");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2); 
 
 is_default= (load_size == 2048 || (flag & 4));
 sprintf(line, "-boot_image %s load_size=%lu\n",
         form, (unsigned long) load_size);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2); 

 is_default= 1;
 if(!(flag & 4))
   for(i= 0; i < 20; i++)
     if(selection_crit[i])
       is_default= 0;
 sprintf(line, "-boot_image %s sel_crit=", form);
 for(i= 0; i < 20; i++)
   sprintf(line + strlen(line), "%-2.2X", (unsigned int) selection_crit[i]);
 strcat(line, "\n");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2); 

 is_default= 1;
 if(!(flag & 4))
   for(i= 0; i < 28; i++)
     if(id_string[i])
       is_default= 0;
 sprintf(line, "-boot_image %s id_string=", form);
 for(i= 0; i < 28; i++)
   sprintf(line + strlen(line), "%-2.2X", (unsigned int) id_string[i]);
 strcat(line, "\n");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2); 
 
 ret= 1; 
ex:;
 Xorriso_free_meM(bspec);
 return(ret); 
}


/* 
 bit0= do only report non-default settings
 bit1= do only report to fp
*/
int Xorriso_boot_status_non_mbr(struct XorrisO *xorriso, IsoImage *image,
                                char *filter, FILE *fp, int flag)
{
 int i, num_boots, sa_type;
 char *paths[15], *line;

 line= xorriso->result_line;

 sa_type= (xorriso->system_area_options & 0xfc) >> 2;
 if(sa_type == 3) {
   sprintf(line, "-boot_image any sparc_label=");
   Text_shellsafe(xorriso->ascii_disc_label, line, 1);
   strcat(line, "\n");
   Xorriso_status_result(xorriso, filter, fp, flag & 2);
   sprintf(line, "-boot_image grub grub2_sparc_core=");
   Text_shellsafe(xorriso->grub2_sparc_core, line, 1);
   strcat(line, "\n");
   Xorriso_status_result(xorriso, filter, fp, flag & 2);
   return(0);
 }
 if(sa_type != 1 && sa_type != 2)
   return(0);
 num_boots= iso_image_get_mips_boot_files(image, paths, 0);
 Xorriso_process_msg_queues(xorriso, 0);
 if(num_boots <= 0)
   return(num_boots);
 if(sa_type == 2)
   num_boots= 1;
 for(i= 0; i < num_boots; i++) {
   sprintf(line, "-boot_image any mips%s_path=", sa_type ==2 ? "el" : "");
   Text_shellsafe(paths[i], line, 1);
   strcat(line, "\n");
   Xorriso_status_result(xorriso, filter, fp, flag & 2);
 }
 return(num_boots);
}


/* 
 bit0= do only report non-default settings
 bit1= do only report to fp
*/
int Xorriso_append_part_status(struct XorrisO *xorriso, IsoImage *image,
                             char *filter, FILE *fp, int flag)
{
 int i;

 for(i= 0; i < Xorriso_max_appended_partitionS; i++) {
   if(xorriso->appended_partitions[i] == NULL)
 continue;
   sprintf(xorriso->result_line, "-append_partition %d 0x%2.2x ",
           i + 1, (unsigned int) xorriso->appended_part_types[i]);
   Text_shellsafe(xorriso->appended_partitions[i], xorriso->result_line, 1);
   strcat(xorriso->result_line, "\n");
   Xorriso_status_result(xorriso, filter, fp, flag & 2);
 }
 return(1);
}


/* 
 bit0= do only report non-default settings
 bit1= do only report to fp
*/
int Xorriso_boot_image_status(struct XorrisO *xorriso, char *filter, FILE *fp,
                              int flag)
{
 int ret, i, num_boots, hflag;
 int bin_path_in_use= 0, is_default, no_defaults;
 char *path= NULL, *form= "any", *line, *hpt;
 struct burn_drive_info *dinfo;
 struct burn_drive *drive;
 IsoImage *image= NULL;
 ElToritoBootImage **boots = NULL;
 IsoFile **bootnodes = NULL;
 int platform_id, patch, load_size;
 enum eltorito_boot_media_type media_type;
 unsigned char id_string[29], sel_crit[21];

 Xorriso_alloc_meM(path, char, SfileadrL);
 line= xorriso->result_line;
 no_defaults= flag & 1;

 ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                "on attempt to print boot info", 2 | 16);
 if(ret<=0)
   goto no_image;
 image= isoburn_get_attached_image(drive);
 Xorriso_process_msg_queues(xorriso,0);
 if(image == NULL) 
   goto no_image;
 
 ret= Xorriso_boot_status_non_mbr(xorriso, image, filter, fp, flag & 3);
 if(ret < 0) /* == 0 is normal */
   {ret= 0; goto ex;}

 if(xorriso->boot_count == 0 && xorriso->boot_image_bin_path[0] == 0) {
no_image:;
   if(xorriso->patch_isolinux_image & 1) {
     sprintf(line, "-boot_image %s patch\n",
             xorriso->patch_isolinux_image & 2 ? "grub" : form);
     is_default= 0;
   } else if(xorriso->keep_boot_image) {
     sprintf(line, "-boot_image %s keep\n", form);
     is_default= 0;
   } else {
     sprintf(line, "-boot_image %s discard\n", form);
     is_default= 1;
   }
   if(!(is_default && no_defaults))
      Xorriso_status_result(xorriso,filter,fp,flag&2); 
   ret= 1; goto after_el_torito;
 }


 if(xorriso->boot_image_bin_path[0] || xorriso->boot_count > 0)
   bin_path_in_use= 1;
 if(xorriso->boot_image_cat_path[0] && bin_path_in_use) {
   is_default= 0;
   sprintf(line,"-boot_image %s cat_path=", form);
   Text_shellsafe(xorriso->boot_image_cat_path, line, 1);
   strcat(line, "\n");
   if(!(is_default && no_defaults))
     Xorriso_status_result(xorriso,filter,fp,flag&2);
 }
 if(bin_path_in_use) {
   is_default= !xorriso->boot_image_cat_hidden;
   hpt= Xorriso__hide_mode_text(xorriso->boot_image_cat_hidden & 63, 0);
   if(hpt != NULL)
     sprintf(line, "-boot_image %s cat_hidden=%s\n", form, hpt);
   Xorriso_free_meM(hpt);
   if(!(is_default && no_defaults))
     Xorriso_status_result(xorriso,filter,fp,flag&2);
 }

 if(xorriso->boot_count > 0) {

   /* show attached boot image info */;

   ret= iso_image_get_all_boot_imgs(image, &num_boots, &boots, &bootnodes, 0);
   Xorriso_process_msg_queues(xorriso,0);
   if(ret == 1 && num_boots > 0) {
     for(i= 0; i < num_boots; i++) {
       ret= Xorriso_path_from_node(xorriso, (IsoNode *) bootnodes[i], path, 0);
       if(ret <= 0)
    continue;
       platform_id= el_torito_get_boot_platform_id(boots[i]);
       patch= el_torito_get_isolinux_options(boots[i], 0);
       el_torito_get_boot_media_type(boots[i], &media_type);
       load_size= el_torito_get_load_size(boots[i]) * 512;
       el_torito_get_id_string(boots[i], id_string);
       el_torito_get_selection_crit(boots[i], sel_crit);
       if(media_type == ELTORITO_FLOPPY_EMUL)
         media_type= 2;
       else if(media_type == ELTORITO_HARD_DISC_EMUL)
         media_type= 1;
       else
         media_type= 0;
       ret= Xorriso_boot_item_status(xorriso, xorriso->boot_image_cat_path,
                  path, platform_id, patch, media_type,
                  load_size, id_string, sel_crit, "any",
                  filter, fp, 16 | (flag & 3));
       if(ret <= 0)
     continue;
       sprintf(line,"-boot_image %s next\n", form);
       Xorriso_status_result(xorriso,filter,fp,flag&2);
     }
   }
 } 

 /* Show pending boot image info */
 if(strcmp(xorriso->boot_image_bin_form, "isolinux") == 0 ||
    strcmp(xorriso->boot_image_bin_form, "grub") == 0)
   form= xorriso->boot_image_bin_form;

 if(xorriso->boot_count > 0 &&
    xorriso->boot_platform_id == 0 &&
    xorriso->patch_isolinux_image == 0 &&
    xorriso->boot_image_bin_path[0] == 0 &&
    xorriso->boot_image_emul == 0 &&
    xorriso->boot_image_load_size == 4 * 512) {
   for(i= 0; i < 20; i++)
     if(xorriso->boot_selection_crit[i])
   break;
   if(i >= 20)
     for(i= 0; i < 28; i++)
       if(xorriso->boot_id_string[i])
     break;
   if(i >= 28)
     {ret= 1; goto ex;} /* Images registered, pending is still default */
 }
  
 hflag= 16; 
 if(xorriso->boot_platform_id == 0xef && !xorriso->boot_efi_default)
   hflag= 0;
 ret= Xorriso_boot_item_status(xorriso, xorriso->boot_image_cat_path,
             xorriso->boot_image_bin_path, xorriso->boot_platform_id,
             xorriso->patch_isolinux_image, xorriso->boot_image_emul,
             xorriso->boot_image_load_size, xorriso->boot_id_string,
             xorriso->boot_selection_crit, form,
             filter, fp, hflag | (flag & 3));
 if(ret <= 0)
   goto ex;

after_el_torito:;
 ret = Xorriso_append_part_status(xorriso, image, filter, fp, flag & 3);
 if(ret <= 0)
   goto ex;

 ret= 1;
ex:
 if(boots != NULL)
   free(boots);
 if(bootnodes != NULL)
   free(bootnodes);
 if(image != NULL)
   iso_image_unref(image);
 Xorriso_free_meM(path);
 return(ret);
}


int Xorriso__append_boot_params(char *line, ElToritoBootImage *bootimg,
                                int flag)
{
 unsigned int platform_id;

 platform_id= el_torito_get_boot_platform_id(bootimg); 
 if(platform_id != 0)
   sprintf(line + strlen(line),
           " , platform_id=0x%-2.2X ", (unsigned int) platform_id);
 if(el_torito_seems_boot_info_table(bootimg, 0))
   sprintf(line + strlen(line), " , boot_info_table=on");
 if(el_torito_seems_boot_info_table(bootimg, 1))
   sprintf(line + strlen(line), " , grub2_boot_info=on");
 return(1);
}


/* @param flag bit0= no output if no boot record was found
               bit1= short form
               bit3= report to info channel (else to result channel)
*/
int Xorriso_show_boot_info(struct XorrisO *xorriso, int flag)
{
 int ret, bin_path_valid= 0,has_isolinux_mbr= 0, i, num_boots, has_mbr= 0;
 int has_grub2_mbr= 0;
 unsigned int mbr_lba= 0;
 off_t lb0_count, blk;
 char *respt, *path;
 unsigned char *lb0= NULL;
 struct burn_drive_info *dinfo;
 struct burn_drive *drive;
 IsoImage *image= NULL;
 ElToritoBootImage *bootimg, **boots = NULL;
 IsoFile *bootimg_node, **bootnodes = NULL;
 IsoBoot *bootcat_node;

 Xorriso_alloc_meM(path, char, SfileadrL);
 Xorriso_alloc_meM(lb0, unsigned char, 2048);

 respt= xorriso->result_line;

 if(xorriso->boot_count > 0) {
   if(!(flag & 1)) {
     sprintf(respt, "Boot record  : overridden by -boot_image any next\n");
     Xorriso_toc_line(xorriso, flag & 8);
   }
   ret= 1; goto ex;
 }

 ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                "on attempt to print boot info", 16);
 if(ret<=0)
   goto no_boot;
 image= isoburn_get_attached_image(drive);
 if(image == NULL) {
   ret= 0;
no_boot:;
   if(!(flag & 1)) {
     sprintf(respt, "Boot record  : none\n");
     Xorriso_toc_line(xorriso, flag & 8);
   }
   goto ex;
 }

 /* Using the nodes with extreme care . They might be deleted meanwhile. */
 ret= iso_image_get_boot_image(image, &bootimg, &bootimg_node, &bootcat_node);
 if(ret != 1)
   goto no_boot;

 ret= iso_image_get_all_boot_imgs(image, &num_boots, &boots, &bootnodes, 0);
 Xorriso_process_msg_queues(xorriso,0);
 if(ret != 1) {
   num_boots= 0;
 } else {
   ret= Xorriso_path_from_node(xorriso, (IsoNode *) bootnodes[0], path, 0);
   if(ret > 0)
     bin_path_valid= 1;
 }
 sprintf(respt, "Boot record  : El Torito");
 /* Load and examine potential MBR */
 ret= burn_read_data(drive, (off_t) 0, (char *) lb0, (off_t) 2048,
                     &lb0_count, 2);
 if(ret > 0)
   if(lb0[510] == 0x55 && lb0[511] == 0xaa)
     has_mbr= 1;
 if(bin_path_valid)
   ret= Xorriso_is_isohybrid(xorriso, bootimg_node, 0);
 else
   ret= 0;
 if(ret > 0) {
   if(has_mbr) {
     has_isolinux_mbr= 1;
     mbr_lba= lb0[432] | (lb0[433] << 8) | (lb0[434] << 16) | (lb0[435] << 24);
     mbr_lba/= 4;
     if(mbr_lba != (unsigned int) xorriso->loaded_boot_bin_lba)
       has_isolinux_mbr= 0;
     if(has_isolinux_mbr) {
       for(i= 0; i < 426; i++)
         if(strncmp((char *) (lb0 + i), "isolinux", 8) == 0)
       break;
       if(i >= 426)
         has_isolinux_mbr= 0;
     }
   }
   if(has_isolinux_mbr)
     strcat(respt, " , ISOLINUX isohybrid MBR pointing to boot image");
   else
     strcat(respt, " , ISOLINUX boot image capable of isohybrid");
 }
 if(bin_path_valid)
   ret= Xorriso_is_grub2_elto(xorriso, bootimg_node, 0);
 else
   ret= 0;
 if(ret > 0) {
   if(has_mbr) {
     has_grub2_mbr= 1;
     blk= 0;
     for(i= 0; i < 8; i++)
       blk|= lb0[i + 0x1b0] << (8 * i);
     blk-= 4;
     blk/= 4;
     if(blk != (off_t) xorriso->loaded_boot_bin_lba)
       has_grub2_mbr= 0;
     for(i= 32; i < 427; i++)
       if(strncmp((char *) (lb0 + i), "GRUB", 4) == 0)
     break;
     if(i >= 427)
       has_grub2_mbr= 0;
   }
   if(has_grub2_mbr)
     strcat(respt, " , GRUB2 MBR pointing to first boot image");
 }
 strcat(respt, "\n");
 Xorriso_toc_line(xorriso, flag & 8);
 if(flag & 2)
   {ret= 1; goto ex;}

 if(xorriso->loaded_boot_cat_path[0]) {
   sprintf(respt, "Boot catalog : ");
   Text_shellsafe(xorriso->loaded_boot_cat_path, respt, 1);
   strcat(respt, "\n");
 } else {
   sprintf(respt, "Boot catalog : -not-found-at-load-time-\n");
 }
 Xorriso_toc_line(xorriso, flag & 8);

 if(bin_path_valid) {
   sprintf(respt, "Boot image   : ");
   Text_shellsafe(path, respt, 1);
 } else if(xorriso->loaded_boot_bin_lba <= 0) {
   sprintf(respt, "Boot image   : -not-found-at-load-time-");
 } else {
   sprintf(respt, "Boot image   : -not-found-any-more-by-lba=%d",
           xorriso->loaded_boot_bin_lba);
 }
 Xorriso__append_boot_params(respt, bootimg, 0);
 strcat(respt, "\n");
 Xorriso_toc_line(xorriso, flag & 8);

 if(num_boots > 1) {
   for(i= 1; i < num_boots; i++) {
     ret= Xorriso_path_from_node(xorriso, (IsoNode *) bootnodes[i], path, 0);
     if(ret > 0) {
       sprintf(respt, "Boot image   : ");
       Text_shellsafe(path, respt, 1);
     } else
       sprintf(respt, "Boot image   : -not-found-any-more-");
     Xorriso__append_boot_params(respt, boots[i], 0);
     strcat(respt, "\n");
     Xorriso_toc_line(xorriso, flag & 8);
   }
 }
 ret= 1;
ex:;
 if(boots != NULL)
   free(boots);
 if(bootnodes != NULL)
   free(bootnodes);
 if(image != NULL)
   iso_image_unref(image); /* release obtained reference */
 Xorriso_free_meM(path);
 Xorriso_free_meM(lb0);
 return(ret);
} 


/* @param flag    bit0=silently return 0 if no volume/image is present
*/
int Xorriso_get_volume(struct XorrisO *xorriso, IsoImage **volume,
                       int flag)
{
 if(xorriso->in_volset_handle==NULL) {
   if(flag & 1)
     return(0);
   Xorriso_process_msg_queues(xorriso,0);
   sprintf(xorriso->info_text,"No ISO image present.");
   if(xorriso->indev[0]==0 && xorriso->outdev[0]==0)
     sprintf(xorriso->info_text+strlen(xorriso->info_text),
             " No -dev, -indev, or -outdev selected.");
   else
     sprintf(xorriso->info_text+strlen(xorriso->info_text),
             " Possible program error with drive '%s'.", xorriso->indev);

   if(!xorriso->no_volset_present)
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   xorriso->no_volset_present= 1;
   return(0);
 }
 *volume= (IsoImage *) xorriso->in_volset_handle;
 xorriso->no_volset_present= 0;
 return(*volume != NULL);
}


/* @param flag bit0= do not return 1 on volset_change_pending != 1
*/
int Xorriso_change_is_pending(struct XorrisO *xorriso, int flag)
{
 if(flag & 1)
   return(xorriso->volset_change_pending == 1);
 return(!!xorriso->volset_change_pending);
}


/* @param flag bit0= do not set hln_change_pending */
int Xorriso_set_change_pending(struct XorrisO *xorriso, int flag)
{
 int ret;
 IsoImage *image;

 ret= Xorriso_get_volume(xorriso, &image, 1);
 if(ret <= 0)
   return ret;
 /* Do not override mark of -as mkisofs -print-size */
 if(xorriso->volset_change_pending != 2)
    xorriso->volset_change_pending= 1;
 if(!(flag & 1))
   xorriso->hln_change_pending= 1;
 return(1);
}


/**
    @param flag bit0= print mount command to result channel rather than
                      performing it 
                bit1= do not allow prefixes with cmd
                bit2= interpret unprefixed cmd as shell:
*/
int Xorriso_mount(struct XorrisO *xorriso, char *dev, int adr_mode,
                  char *adr_value, char *cmd, int flag)
{
 int ret, lba, track, session, params_flag= 0, is_safe= 0, is_extra_drive= 0;
 int give_up= 0, mount_chardev= 0, status, aquire_flag= 0;
 char volid[33], *devadr, *mount_command= NULL, *adr_data= NULL, *adr_pt;
 char *dev_path, *libburn_adr= NULL;
 char *dpt, *sysname= "";
 struct stat stbuf;
 struct burn_drive_info *dinfo= NULL;
 struct burn_drive *drive= NULL;

 Xorriso_alloc_meM(mount_command, char, SfileadrL);
 Xorriso_alloc_meM(adr_data, char, 163);
 Xorriso_alloc_meM(libburn_adr, char, BURN_DRIVE_ADR_LEN + SfileadrL);

 devadr= dev;
 adr_pt= adr_value;
 if(strcmp(dev, "indev") == 0) {
   ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                  "on attempt to perform -mount \"indev\"", 0);
   if(ret<=0)
     goto ex;
   dev_path= devadr= xorriso->indev;
   if(strncmp(dev_path, "stdio:", 6) == 0)
     dev_path+= 6;
   else if(strncmp(dev_path, "mmc:", 4) == 0)
     dev_path+= 4;
   if(xorriso->in_drive_handle == xorriso->out_drive_handle)
     give_up= 3;
   else
     give_up= 1;
 } else if(strcmp(dev, "outdev") == 0) {
   ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                  "on attempt to perform -mount \"outdev\"", 
                                  2);
   if(ret<=0)
     goto ex;
   dev_path= devadr= xorriso->outdev;
   if(strncmp(dev_path, "stdio:", 6) == 0)
     dev_path+= 6;
   else if(strncmp(dev_path, "mmc:", 4) == 0)
     dev_path+= 4;
   if(xorriso->in_drive_handle == xorriso->out_drive_handle)
     give_up= 3;
   else
     give_up= 2;
 } else {
   is_extra_drive= 1;
   dev_path= dev;
   if(strncmp(dev_path, "stdio:", 6) == 0)
     dev_path+= 6;
   else if(strncmp(dev_path, "mmc:", 4) == 0)
     dev_path+= 4;

   /* do only accept regular files and block devices */
   ret= stat(dev_path, &stbuf);
   if(ret == -1) {
     sprintf(xorriso->info_text, "Cannot determine properties of file ");
     Text_shellsafe(dev_path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }
   ret= System_uname(&sysname, NULL, NULL, NULL, 0);
   if(ret > 0 && strcmp(sysname, "FreeBSD") == 0)
     mount_chardev= 1;
   if(!(S_ISREG(stbuf.st_mode) || (S_ISBLK(stbuf.st_mode) && !mount_chardev)
        || (S_ISCHR(stbuf.st_mode) && !mount_chardev))) {
     sprintf(xorriso->info_text,
             "File object is not suitable as mount device: ");
     Text_shellsafe(dev_path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }

   /* Aquire drive as direct libburn address or via stdio: prefix */
   if(strncmp(dev, "mmc:", 4) == 0)
     ret= burn_drive_convert_fs_adr(dev + 4, libburn_adr);
   else
     ret= burn_drive_convert_fs_adr(dev, libburn_adr);
   Xorriso_process_msg_queues(xorriso,0);
   if(ret < 0)
     {ret= -1; goto ex;}
   if(ret == 0 && strncmp(dev, "stdio:", 6) != 0 &&
      strncmp(dev, "mmc:", 4) != 0)
     sprintf(libburn_adr, "stdio:%s", dev);
   burn_preset_device_open(
           xorriso->drives_exclusive && !(xorriso->mount_opts_flag & 1), 0, 0);
   aquire_flag= 1;
   if((xorriso->toc_emulation_flag & 2) && adr_mode == 3)
     aquire_flag|= 16;
   if(xorriso->toc_emulation_flag & 4)
     aquire_flag|= 128;
   if(xorriso->toc_emulation_flag & 8)
     aquire_flag|= 512;
   ret= isoburn_drive_aquire(&dinfo, libburn_adr, aquire_flag);
   burn_preset_device_open(1, 0, 0);
   Xorriso_process_msg_queues(xorriso,0);
   if(ret <= 0)
     {ret= 0; goto ex;}
   drive= dinfo[0].drive;
 }

 if(adr_mode == 4 && strlen(adr_pt) <= 80) {
   ret= Xorriso__bourne_to_reg(adr_pt, adr_data, 0);
   if(ret == 1) {
     params_flag|= 4;
     adr_pt= adr_data;
   }
 }
 ret= isoburn_get_mount_params(drive, adr_mode, adr_pt, &lba, &track,
                               &session, volid, params_flag);
 Xorriso_process_msg_queues(xorriso,0);
 if(ret <= 0)
   goto ex;
 if(((session <= 0 || track <= 0) && !(aquire_flag & 16)) || ret == 2) {
   Xorriso_msgs_submit(xorriso, 0,
                "-mount : Given address does not point to an ISO 9660 session",
                0, "FAILURE", 0);
   ret= 0; goto ex;
 }
 if(strstr(devadr, "stdio:") == devadr)
   devadr+= 6;
 if(strstr(devadr, "mmc:") == devadr)
   devadr+= 4;
 ret= Xorriso_make_mount_cmd(xorriso, cmd, lba, track, session, volid, devadr,
                             mount_command, flag & (2 | 4));
 if(ret <= 0)
   goto ex;
 if(ret == 2)
   is_safe= 1;

 if(is_extra_drive) {
   isoburn_drive_release(drive, 0);
   burn_drive_info_free(dinfo);
   drive= NULL;
 } else if(give_up > 0 && !((flag & 1) || (xorriso->mount_opts_flag & 1))) {
   Xorriso_give_up_drive(xorriso, give_up);
   if(ret <= 0)
     goto ex;
 }
 Xorriso_process_msg_queues(xorriso,0);

 sprintf(xorriso->info_text, "Volume id    : ");
 Text_shellsafe(volid, xorriso->info_text, 1);
 strcat(xorriso->info_text, "\n");
 Xorriso_info(xorriso, 0);
 if(flag & 1) {
   sprintf(xorriso->result_line, "%s\n", mount_command);
   Xorriso_result(xorriso,0);
 } else {
   sprintf(xorriso->info_text, "Mount command: %s\n", mount_command);
   Xorriso_info(xorriso, 0);
   if(!is_safe) {
     Xorriso_msgs_submit(xorriso, 0,
  "-mount : Will not perform mount command which stems from command template.",
       0, "SORRY", 0);
     sprintf(xorriso->result_line, "%s\n", mount_command);
     Xorriso_result(xorriso,0);
   } else {
     ret= Xorriso_execv(xorriso, mount_command, "/bin:/sbin", &status, 1);
     if(WIFEXITED(status) && WEXITSTATUS(status) != 0) {
       sprintf(xorriso->info_text,
               "-mount : mount command failed with exit value %d",
               (int) WEXITSTATUS(ret));
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
     }
     sprintf(xorriso->info_text, "\nMounted session %d of device ", session);
     Text_shellsafe(dev_path, xorriso->info_text, 1);
     dpt= strchr(cmd, ':');
     if(dpt == NULL)
       dpt= cmd ;
     else
       dpt++;
     sprintf(xorriso->info_text + strlen(xorriso->info_text), " as directory ");
     Text_shellsafe(dpt, xorriso->info_text, 1);
     strcat(xorriso->info_text, "\n");
     Xorriso_info(xorriso, 0);
   }
 }
 ret= 1;
ex:;
 if(is_extra_drive && drive != NULL) {
   isoburn_drive_release(drive, 0);
   burn_drive_info_free(dinfo);
   Xorriso_process_msg_queues(xorriso,0);
 }
 Xorriso_free_meM(mount_command);
 Xorriso_free_meM(adr_data);
 Xorriso_free_meM(libburn_adr);
 return(ret);
}


/* @param flag bit0= give up all boot file paths
               bit1= refuse if already a path is added
*/
int Xorriso_add_mips_boot_file(struct XorrisO *xorriso, char *path, int flag)
{
 int ret;
 IsoImage *image;
 char *paths[15];

 ret= Xorriso_get_volume(xorriso, &image, 0);
 if(ret <= 0)
   return ret;
 if(flag & 1) {
   iso_image_give_up_mips_boot(image, 0);
   Xorriso_process_msg_queues(xorriso,0);
   return(1);
 }
 if(flag & 2) {
   ret= iso_image_get_mips_boot_files(image, paths, 0);
   Xorriso_process_msg_queues(xorriso,0);
   if(ret < 0)
     goto report_error;
   if(ret > 0) {
     Xorriso_msgs_submit(xorriso, 0,
                         "There is already a boot image file registered.",
                         0, "FAILURE", 0);
     return(0);
   }
 }
 ret = iso_image_add_mips_boot_file(image, path, 0);
 Xorriso_process_msg_queues(xorriso,0);
 if (ret < 0) {
report_error:;
   Xorriso_report_iso_error(xorriso, "", ret,
                            "Error when adding MIPS boot file",
                            0, "FAILURE", 1);
   return(0);
 }
 return(1);
}


int Xorriso_coordinate_system_area(struct XorrisO *xorriso, int sa_type,
                                   int options, char *cmd, int flag)
{
 int old_type, old_options;
 static char *type_names[4] = {
      "MBR", "MIPS Big Endian Volume Header", "MIPS Little Endian Boot Block",
      "SUN Disk Label"};

 old_type= (xorriso->system_area_options & 0xfc) >> 2;
 old_options= xorriso->system_area_options & ~0x40fc;
 if((old_type != 0 || old_options != 0) &&
    (old_type != sa_type || (old_options != 0 && old_options != options))) {
   sprintf(xorriso->info_text, "%s : First sector already occupied by %s",
           cmd, old_type < 4 ? type_names[old_type] : "other boot facility");
   if(old_type == 0 && old_options == 2)
     strcat(xorriso->info_text, " for ISOLINUX isohybrid");
   if(old_type == 0 && old_options == 1)
     strcat(xorriso->info_text, " for partition table");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   goto hint_revoke;
 }
 xorriso->system_area_options= (xorriso->system_area_options & 0x4000) |
                               ((sa_type << 2) & 0xfc) | (options & ~0x40fc);
 if(sa_type == 0)
   xorriso->patch_system_area= xorriso->system_area_options;
 return(1);

hint_revoke:;
 if(old_type == 0)
   sprintf(xorriso->info_text, "Revokable by -boot_image any discard");
 else if(old_type == 1 || old_type == 2)
   sprintf(xorriso->info_text, "Revokable by -boot_image any mips_discard");
 else if(old_type == 3)
   sprintf(xorriso->info_text, "Revokable by -boot_image any sparc_discard");
 if(old_type < 4)
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
 return(0);
}

