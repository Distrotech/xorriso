

/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2013 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains functions which are needed to write sessions.
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

#include <fcntl.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif

#ifdef Xorriso_standalonE

#ifdef Xorriso_with_libjtE
#include "../libjte/libjte.h"
#endif

#else

#ifdef Xorriso_with_libjtE
#include <libjte/libjte.h>
#endif

#endif /* ! Xorriso_standalonE */

#include "xorriso.h"
#include "xorriso_private.h"

#include "lib_mgt.h"
#include "drive_mgt.h"
#include "iso_img.h"
#include "iso_tree.h"
#include "write_run.h"


/* @param flag bit0= talk of -as cdrecord -multi rather than of -close
*/
int Xorriso_check_multi(struct XorrisO *xorriso, struct burn_drive *drive,
                        int flag)
{
 int profile_no= 0, ret;
 struct burn_multi_caps *caps= NULL;
 char profile_name[80];

 if(!xorriso->do_close) {
   burn_disc_get_profile(drive, &profile_no, profile_name);
   if(profile_no == 0x14) { /* DVD-RW sequential */
     ret= burn_disc_get_multi_caps(drive, BURN_WRITE_TAO, &caps, 0);
     if(caps != NULL)
       burn_disc_free_multi_caps(&caps);
     if(ret == 0) {
       if(flag & 1) {
         sprintf(xorriso->info_text,
                "This DVD-RW media can only be written without option -multi");
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         sprintf(xorriso->info_text,
                 "Possibly it was blanked by blank=deformat_quickest");
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
         sprintf(xorriso->info_text,
                 "After writing a session without -multi, apply blank=all");
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
       } else {
         sprintf(xorriso->info_text,
                 "This DVD-RW media can only be written with -close \"on\"");
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         sprintf(xorriso->info_text,
                 "Possibly it was blanked by -blank \"deformat_quickest\"");
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
         sprintf(xorriso->info_text,
          "After writing a session with -close \"on\", apply -blank \"all\"");
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
       }
       return(0);
     }
   } else if(profile_no == 0x15) { /* DVD-RW DL */
     if(flag & 1)
       sprintf(xorriso->info_text,
               "DVD-R DL media can only be written without option -multi");
     else
       sprintf(xorriso->info_text,
               "DVD-R DL media can only be written with -close \"on\"");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
 }
 return(1);
}


int Xorriso_make_write_options(
        struct XorrisO *xorriso, struct burn_drive *drive,
        struct burn_write_opts **burn_options, int flag)
{
 int drive_role, stream_mode= 0;

 *burn_options= burn_write_opts_new(drive);
 if(*burn_options==NULL) {
   Xorriso_process_msg_queues(xorriso,0);
   sprintf(xorriso->info_text,"Cannot allocate option set");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 burn_write_opts_set_simulate(*burn_options, !!xorriso->do_dummy);
 drive_role= burn_drive_get_drive_role(drive);
 burn_write_opts_set_multi(*burn_options,
                       !(xorriso->do_close || drive_role==0 || drive_role==3));
 burn_drive_set_speed(drive, xorriso->speed, xorriso->speed);
 if(xorriso->do_stream_recording == 1)
   stream_mode= 1;
 else if(xorriso->do_stream_recording == 2)
   stream_mode= 51200; /* 100 MB */
 else if(xorriso->do_stream_recording >= 16)
   stream_mode= xorriso->do_stream_recording;
 burn_write_opts_set_stream_recording(*burn_options, stream_mode);

#ifdef Xorriso_dvd_obs_default_64K
 if(xorriso->dvd_obs == 0)
   burn_write_opts_set_dvd_obs(*burn_options, 64 * 1024);
 else  
#endif
   burn_write_opts_set_dvd_obs(*burn_options, xorriso->dvd_obs);

 burn_write_opts_set_stdio_fsync(*burn_options, xorriso->stdio_sync);
 burn_write_opts_set_underrun_proof(*burn_options, 1);
 return(1);
}


/* @param flag bit0= do not write but only prepare and return size in sectors
               bit1= do not use isoburn wrappers, do not assume libisofs
*/
int Xorriso_sanitize_image_size(struct XorrisO *xorriso,
                    struct burn_drive *drive, struct burn_disc *disc,
                    struct burn_write_opts *burn_options, int flag)
{
 int ret, img_sectors, num_sessions= 0, num_tracks= 0, padding= 0, profile;
 int media_space, lba, nwa, multi_emul_blocks= 0;
 char profile_name[80];
 struct burn_session **sessions;
 struct burn_track **tracks;
 enum burn_disc_status s;

 img_sectors= burn_disc_get_sectors(disc);

 sessions= burn_disc_get_sessions(disc, &num_sessions);
 if(sessions==NULL || num_sessions < 1) {
no_track:;
   Xorriso_process_msg_queues(xorriso,0);
   sprintf(xorriso->info_text,"Program error : no track in prepared disc");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   {ret= -1; goto ex;}
 }
 tracks= burn_session_get_tracks(sessions[0], &num_tracks);
 if(tracks==NULL || num_tracks < 1)
   goto no_track;

 padding= 0;
 ret= burn_disc_get_profile(drive, &profile, profile_name);
 padding= xorriso->padding / 2048;
 if(xorriso->padding > padding * 2048)
   padding++;
 if(img_sectors>0 && ret>0 && 
    (profile==0x09 || profile==0x0a)) { /* CD-R , CD-RW */
   if(img_sectors + padding < Xorriso_cd_min_track_sizE) {
     padding= Xorriso_cd_min_track_sizE - img_sectors;
     sprintf(xorriso->info_text,
             "Expanded track to minimum size of %d sectors",
             Xorriso_cd_min_track_sizE);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   }
 }
 if(xorriso->alignment == 0 && ! (xorriso->no_emul_toc & 1)) {
   ret= isoburn_needs_emulation(drive);
   if(ret > 0) {
     /* Take care that the session is padded up to the future NWA.
        Else with padding < 32 it could happen that PVDs from older
        sessions survive and confuse -rom_toc_scan.
      */
     xorriso->alignment= 32;
     s= isoburn_disc_get_status(drive);
     if(s == BURN_DISC_BLANK) {
       /* Count blocks before nwa as part of the image */;
       ret= isoburn_disc_track_lba_nwa(drive, burn_options, 0, &lba, &nwa);
       if(ret <= 0)
         nwa= 0;
       multi_emul_blocks= nwa;
     }
   }
 }

 if(!(flag & 2)) {

#ifdef Xorriso_with_libjtE
   /* JTE : no multi-session, no_emul_toc, padding in libisofs */
   if(xorriso->libjte_handle != NULL)
     padding= 0;
#endif /* ! Xorriso_with_libjtE */

   if(xorriso->do_padding_by_libisofs)
     padding= 0;
 }

 if(xorriso->alignment > 0) {
   if(img_sectors > 0) {
     ret= isoburn_disc_track_lba_nwa(drive, burn_options, 0, &lba, &nwa);
     if(ret <= 0)
       nwa= 0;
     lba= (nwa + img_sectors + padding) % xorriso->alignment;
     if(lba > 0)
       padding+= xorriso->alignment - lba;
   }
 }

 burn_track_define_data(tracks[0], 0, padding * 2048, 0, BURN_MODE1);
 Xorriso_process_msg_queues(xorriso,0);

 if(flag&2)
   media_space= burn_disc_available_space(drive, burn_options) /
                (off_t) 2048;
 else
   media_space= isoburn_disc_available_space(drive, burn_options) /
                (off_t) 2048;
 if(media_space < img_sectors + padding) {
   Xorriso_process_msg_queues(xorriso,0);
   sprintf(xorriso->info_text,"Image size %ds exceeds free space on media %ds",
           img_sectors + padding, media_space);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 if(flag&1) {
   ret= multi_emul_blocks + img_sectors + padding;
 } else
   ret= 1;
ex:;
 return(ret);
}


int Xorriso_auto_format(struct XorrisO *xorriso, int flag)
{
 int ret, profile, status, num_formats;
 char profile_name[80];
 struct burn_drive_info *dinfo;
 struct burn_drive *drive;
 off_t size;
 unsigned dummy;

 ret= Xorriso_may_burn(xorriso, 0);
 if(ret <= 0)
   return(0);

 ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                "on attempt to autoformat", 2);
 if(ret<=0)
   return(0);
 ret= burn_disc_get_profile(drive, &profile, profile_name);
 if(ret>0 && (profile==0x12 || profile==0x43)) { /* DVD-RAM or BD-RE */
   ret= burn_disc_get_formats(drive, &status, &size, &dummy, &num_formats);
   if(ret>0 && status==BURN_FORMAT_IS_UNFORMATTED) {
     sprintf(xorriso->info_text,
             "Unformatted %s medium detected. Trying -format fast.",
             profile_name);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
     ret= Xorriso_format_media(xorriso, (off_t) 0, 1 | 4);
     if(ret<=0) {
       sprintf(xorriso->info_text, "Automatic formatting of %s failed",
               profile_name);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       return(ret);
     }
   }
 }
 return(1);
}


int Xorriso_set_system_area(struct XorrisO *xorriso, struct burn_drive *drive,
                            IsoImage *img, struct isoburn_imgen_opts *sopts,
                            int flag)
{
 int ret, options, system_area_options, iso_lba= -1, start_lba, image_blocks;
 char volid[33];
 FILE *fp= NULL;
 char *buf= NULL, *bufpt= NULL;
 off_t hd_lba;
 unsigned char *ub;
 ElToritoBootImage *bootimg;
 IsoFile *bootimg_node;
 IsoNode *sparc_core_node;
 uint32_t offst;
 enum burn_disc_status state;

 if(xorriso->grub2_sparc_core[0]) {
   ret= Xorriso_node_from_path(xorriso, img, xorriso->grub2_sparc_core,
                               &sparc_core_node, 1);
   if(ret <= 0) {
     sprintf(xorriso->info_text,
             "Cannot find in ISO image: -boot_image grub grub2_sparc_core=");
     Text_shellsafe(xorriso->grub2_sparc_core, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
   if(!ISO_NODE_IS_FILE(sparc_core_node)) {
     sprintf(xorriso->info_text,
             "Not a data file: -boot_image grub grub2_sparc_core=");
     Text_shellsafe(xorriso->grub2_sparc_core, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
   ret = iso_image_set_sparc_core(img, (IsoFile *) sparc_core_node, 0);
   if(ret < 0) {
     Xorriso_process_msg_queues(xorriso,0);
     Xorriso_report_iso_error(xorriso, "", ret,
                    "Error when setting up -boot_image grub grub2_sparc_core=",
                     0, "FAILURE", 1);
     {ret= 0; goto ex;}
   }
 }

 Xorriso_alloc_meM(buf, char, 32768);
 system_area_options= xorriso->system_area_options;
 memset(buf, 0, 32768);
 if(xorriso->system_area_disk_path[0] == 0) {
   if(xorriso->patch_system_area) {
     ret= iso_image_get_system_area(img, buf, &options, 0);
     if(ret == 0) {
       goto do_set;
     } else if(ret < 0) {
       Xorriso_process_msg_queues(xorriso,0);
       Xorriso_report_iso_error(xorriso, "", ret,
                     "Error when inquiring System Area data of ISO 9660 image",
                     0, "FAILURE", 1);
       {ret= 0; goto ex;}
     } else {
       system_area_options= xorriso->patch_system_area;
       /* Check whether partition 1 ends at image end */;
       ub= (unsigned char *) buf;
       hd_lba= (ub[454] | (ub[455] << 8) | (ub[456] << 16) | (ub[457] << 24)) +
               (ub[458] | (ub[459] << 8) | (ub[460] << 16) | (ub[461] << 24));

       iso_lba= -1;
       ret= isoburn_disc_get_msc1(drive, &start_lba);
       if(ret > 0) {
          ret= isoburn_read_iso_head(drive, start_lba, &image_blocks,
                                     volid, 1);
          if(ret > 0)
            iso_lba= start_lba + image_blocks;
       }
       if(((off_t) iso_lba) * (off_t) 4 > hd_lba) {
          system_area_options= 0;
       } else if((xorriso->patch_system_area & 1) &&
                 ((off_t) iso_lba) * (off_t) 4 != hd_lba) {
          system_area_options= 0;
       } else if((xorriso->patch_system_area & 2) &&
                 ((off_t) iso_lba) * (off_t) 4 + (off_t) (63 * 256) < hd_lba) {
          system_area_options= 0;
       } else if(xorriso->patch_system_area & 2) { /* isohybrid patching */
         /* Check whether bytes 432-345 point to ElTorito LBA */
         hd_lba= ub[432] | (ub[433] << 8) | (ub[434] << 16) | (ub[435] << 24);
         ret= iso_image_get_boot_image(img, &bootimg, &bootimg_node, NULL);
         if(ret != 1)
           system_area_options= 0;
         else if(bootimg_node != NULL) {
           Xorriso__file_start_lba((IsoNode *) bootimg_node, &(iso_lba), 0);
           if(((off_t) iso_lba) * (off_t) 4 != hd_lba)
             system_area_options= 0;
         }
       }
       if(system_area_options == 0) {
         Xorriso_msgs_submit(xorriso, 0,
                  "Loaded System Area data are not suitable for MBR patching.",
                  0, "DEBUG", 0);
       }
     }
     bufpt= buf;
     ret= 1;
   } else
     ret= 0;
   goto do_set;
 }
 if(strcmp(xorriso->system_area_disk_path, "/dev/zero") == 0)
   {ret= 1; goto do_set;}

 ret= Xorriso_afile_fopen(xorriso, xorriso->system_area_disk_path,
                          "rb", &fp, 2);
 if(ret <= 0)
   {ret= 0; goto ex;}
 ret= fread(buf, 1, 32768, fp);
 if(ret < 32768) {
   if(ferror(fp)) {
     sprintf(xorriso->info_text,
             "Error when reading -boot_image system_area=");
     Text_shellsafe(xorriso->system_area_disk_path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
 }
 bufpt= buf;

do_set:;
 if(ret > 0 && xorriso->system_area_disk_path[0]) {
   sprintf(xorriso->info_text, "Copying to System Area: %d bytes from file ",
           ret);
   Text_shellsafe(xorriso->system_area_disk_path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
 }
 ret= isoburn_igopt_set_system_area(sopts, bufpt, system_area_options);
 if(ret != ISO_SUCCESS) {
   Xorriso_process_msg_queues(xorriso,0);
   Xorriso_report_iso_error(xorriso, "", ret,
                     "Error when attaching System Area data to ISO 9660 image",
                      0, "FAILURE", 1);
   {ret= 0; goto ex;}
 }
 offst= xorriso->partition_offset;
 state= isoburn_disc_get_status(drive);
 if(state == BURN_DISC_APPENDABLE) {
   ret= isoburn_get_img_partition_offset(drive, &offst);
   if(ret == 1) {
     sprintf(xorriso->info_text,
             "Preserving in ISO image: -boot_image any partition_offset=%lu",
             (unsigned long) offst);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   } else
     offst= xorriso->partition_offset;
 }
 ret= isoburn_igopt_set_part_offset(sopts, offst,
                                    xorriso->partition_secs_per_head,
                                    xorriso->partition_heads_per_cyl);
 if(ret != ISO_SUCCESS) {
   Xorriso_process_msg_queues(xorriso,0);
   Xorriso_report_iso_error(xorriso, "", ret,
                       "Error when setting partition offset", 0, "FAILURE", 1);
   {ret= 0; goto ex;}
 }
 ret= 1;
ex:;
 if(fp != NULL && fp != stdin)
   fclose(fp);
 Xorriso_free_meM(buf);
 return(ret);
} 


/* @param flag bit0= do not increment boot_count
                     and do not reset boot parameters
           bit1= dispose attached boot images
*/
int Xorriso_attach_boot_image(struct XorrisO *xorriso, int flag)
{
 int ret;
 char *cpt;
 struct burn_drive_info *source_dinfo;
 struct burn_drive *source_drive;
 IsoImage *image= NULL;
 IsoNode *node;
 ElToritoBootImage *bootimg;
 enum eltorito_boot_media_type emul_type= ELTORITO_NO_EMUL;
 char *bin_path;
 int emul, platform_id;
 off_t load_size;
 struct stat stbuf;
 int hflag= 0;

 if(xorriso->boot_image_bin_path[0] == 0 && !(flag & 2)) {

   /* >>> no boot image path given : no op */;

   ret= 2; goto ex;
 }

 if(xorriso->in_drive_handle == NULL)
   hflag= 2;
 ret= Xorriso_get_drive_handles(xorriso, &source_dinfo, &source_drive,
                                "on attempt to attach boot image", hflag);
 if(ret<=0)
   goto ex;
 image= isoburn_get_attached_image(source_drive);
 if(image == NULL) {
   /* (should not happen) */
   sprintf(xorriso->info_text,
           "No ISO image present on attempt to attach boot image");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   ret= 0; goto ex;
 }
 if(flag & 2) {
   iso_image_remove_boot_image(image);
   xorriso->boot_count= 0;
   ret= 1; goto ex;
 }

 bin_path= xorriso->boot_image_bin_path;
 emul= xorriso->boot_image_emul;
 platform_id= xorriso->boot_platform_id;
 load_size= xorriso->boot_image_load_size;

 if(xorriso->boot_efi_default) {
   emul= 0;
   platform_id= 0xef;
   xorriso->patch_isolinux_image= (xorriso->patch_isolinux_image & ~3) | 0;
 }
 if(platform_id == 0xef || load_size < 0) {
   ret= Xorriso_iso_lstat(xorriso, bin_path, &stbuf, 2 | 4);
   if(ret != 0)
     {ret= 0; goto ex;}
   load_size= ((stbuf.st_size / (off_t) 512) +
              !!(stbuf.st_size % (off_t) 512)) * 512;
 }
 sprintf(xorriso->info_text, "Adding boot image ");
 Text_shellsafe(bin_path, xorriso->info_text, 1);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);

 if(emul == 0)
   emul_type= ELTORITO_NO_EMUL;
 else if(emul == 1)
   emul_type= ELTORITO_HARD_DISC_EMUL;
 else if(emul == 2)
   emul_type= ELTORITO_FLOPPY_EMUL;

 ret= Xorriso_node_from_path(xorriso, image, bin_path, &node, 1);
 if(ret <= 0) {
   sprintf(xorriso->info_text,
           "Cannot find in ISO image: -boot_image ... bin_path=");
   Text_shellsafe(bin_path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }

 if(xorriso->boot_count == 0) {
   if(xorriso->boot_image_cat_path[0] == 0) {
     strcpy(xorriso->boot_image_cat_path, bin_path);
     cpt= strrchr(xorriso->boot_image_cat_path, '/');
     if(cpt == NULL)
       cpt= xorriso->boot_image_cat_path;
     else
       cpt++;
     strcpy(cpt, "boot.cat");
   }
   ret= Xorriso_node_from_path(xorriso, image, xorriso->boot_image_cat_path,
                                &node, 1);
   if(ret > 0) {
     if(!xorriso->do_overwrite) {
       sprintf(xorriso->info_text,
               "May not overwite existing -boot_image ... cat_path=");
       Text_shellsafe(xorriso->boot_image_cat_path, xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       {ret= 0; goto ex;}
     }
     ret= Xorriso_rmi(xorriso, NULL, (off_t) 0, xorriso->boot_image_cat_path,
                      8 | (xorriso->do_overwrite == 1));
     if(ret != 1) {
       sprintf(xorriso->info_text,
               "Could not remove existing -boot_image cat_path=");
       Text_shellsafe(xorriso->boot_image_cat_path, xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       {ret= 0; goto ex;}
     }
   }

   /* Discard old boot image, set new one */
   ret= iso_image_get_boot_image(image, &bootimg, NULL, NULL);
   if(ret == 1)
     iso_image_remove_boot_image(image);
   ret= iso_image_set_boot_image(image, bin_path, emul_type,
                                 xorriso->boot_image_cat_path, &bootimg);
   if(ret > 0)
     iso_image_set_boot_catalog_weight(image, 1000000000);
 } else {
   ret= iso_image_add_boot_image(image, bin_path, emul_type, 0, &bootimg);
 }
 if(ret < 0) {
   Xorriso_process_msg_queues(xorriso,0);
   Xorriso_report_iso_error(xorriso, "", ret,
                 "Error when attaching El-Torito boot image to ISO 9660 image",
                 0, "FAILURE", 1);
   sprintf(xorriso->info_text,
           "Could not attach El-Torito boot image to ISO 9660 image");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 el_torito_set_boot_platform_id(bootimg, (uint8_t) platform_id);
 if(load_size / 512 > 65535) {
   sprintf(xorriso->info_text,
 "Boot image load size exceeds 65535 blocks. Will record 65535 in El Torito.");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
   load_size= 65535 * 512;
 }
 el_torito_set_load_size(bootimg, load_size / 512); 
 el_torito_set_id_string(bootimg, xorriso->boot_id_string);
 el_torito_set_selection_crit(bootimg, xorriso->boot_selection_crit);
 ret= Xorriso_set_isolinux_options(xorriso, image, 1);
 if(!(flag & 1)) {
   /* Register attachment and reset even in case of error return */
   xorriso->boot_count++;
   xorriso->boot_platform_id= 0;
   xorriso->patch_isolinux_image= 0;
   xorriso->boot_image_bin_path[0]= 0;
   xorriso->boot_image_bin_form[0]= 0;
   xorriso->boot_image_emul= 0;
   xorriso->boot_emul_default= 1;
   xorriso->boot_image_load_size= 4 * 512;
   memset(xorriso->boot_id_string, 0, sizeof(xorriso->boot_id_string));
   memset(xorriso->boot_selection_crit, 0,
          sizeof(xorriso->boot_selection_crit));
   xorriso->boot_efi_default= 0;
 }
 if(ret <= 0)
   goto ex;

 ret= 1;
ex:;
 if(image != NULL)
   iso_image_unref(image);
 return(ret);
}


/* @param flag bit0= do not write but only prepare and return size in sectors
*/
int Xorriso_write_session(struct XorrisO *xorriso, int flag)
{
 int ret, relax= 0, i, pacifier_speed= 0, data_lba, ext, is_bootable= 0;
 int freshly_bootable= 0, hide_attr, pad_by_libisofs= 0, signal_mode, role;
 char *xorriso_id= NULL, *img_id, *sfe= NULL, *out_cs, *part_image;
 struct isoburn_imgen_opts *sopts= NULL;
 struct burn_drive_info *dinfo, *source_dinfo;
 struct burn_drive *drive, *source_drive;
 struct burn_disc *disc= NULL;
 struct burn_write_opts *burn_options= NULL;
 off_t readcounter= 0,writecounter= 0;
 int num_sessions= 0, num_tracks= 0;
 struct burn_session **sessions;
 struct burn_track **tracks;
 enum burn_disc_status s;
 struct burn_multi_caps *caps= NULL;
 IsoImage *image= NULL;
 IsoNode *root_node;
 int profile_number;
 char *profile_name= NULL, *reasons= NULL;
 IsoBoot *bootcat_node;
 uint32_t padding;

 Xorriso_alloc_meM(sfe, char, 5 * SfileadrL);
 Xorriso_alloc_meM(xorriso_id, char, 256);
 Xorriso_alloc_meM(profile_name, char, 80);
 Xorriso_alloc_meM(reasons, char, BURN_REASONS_LEN);

 ret= Xorriso_finish_hl_update(xorriso, 0);
 if(ret <= 0)
   goto ex;

 out_cs= xorriso->out_charset;
 if(out_cs == NULL)
   Xorriso_get_local_charset(xorriso, &out_cs, 0);

 ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                "on attempt to write", 2);
 if(ret<=0)
   goto ex;
 if(!(flag & 1)) {
   ret= Xorriso_auto_format(xorriso, 0);
   if(ret <=0 )
     {ret= 0; goto ex;}
 }

 s= isoburn_disc_get_status(drive);
 if (xorriso->do_hfsplus && (
     (xorriso->grow_blindly_msc2 >= 0 &&
       xorriso->out_drive_handle != xorriso->in_drive_handle)
     ||
     (xorriso->out_drive_handle == xorriso->in_drive_handle && 
      s != BURN_DISC_BLANK)
    )) {
   sprintf(xorriso->info_text,
           "May not grow ISO image while -hfsplus is enabled");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 if(xorriso->out_drive_handle == xorriso->in_drive_handle) {
   if(abs(xorriso->displacement_sign) == 1 && xorriso->displacement != 0 &&
      s != BURN_DISC_BLANK) {
     sprintf(xorriso->info_text,
             "May not grow ISO image while -displacement is non-zero");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
   source_drive= drive;
 } else {
   if(xorriso->in_drive_handle == NULL) {
     source_drive= drive;
   } else {
     ret= Xorriso_get_drive_handles(xorriso, &source_dinfo, &source_drive,
                                    "on attempt to get source for write", 0);
     if(ret<=0)
       goto ex;
   }
   if(s!=BURN_DISC_BLANK) {
     s= burn_disc_get_status(drive);
     if(s!=BURN_DISC_BLANK)
       sprintf(xorriso->info_text,
               "-indev differs from -outdev and -outdev media is not blank");
     else
       sprintf(xorriso->info_text,
          "-indev differs from -outdev and -outdev media holds non-zero data");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
 }
 ret= Xorriso_get_profile(xorriso, &profile_number, profile_name, 2);
 if(ret == 2)
   pacifier_speed= 1;
 else if(ret == 3)
   pacifier_speed= 2;

 ret= Xorriso_check_multi(xorriso, drive, 0);
 if(ret<=0) 
   goto ex;

 ret= isoburn_igopt_new(&sopts, 0);
 if(ret<=0) {
   Xorriso_process_msg_queues(xorriso, 0);
   goto ex;
 }
 relax= xorriso->relax_compliance;

 xorriso->alignment= 0;
 image= isoburn_get_attached_image(source_drive);
 if(image != NULL) {
   iso_image_set_application_id(image, xorriso->application_id);
   iso_image_set_publisher_id(image, xorriso->publisher);
   iso_image_set_system_id(image, xorriso->system_id);
   iso_image_set_volset_id(image, xorriso->volset_id);
   iso_image_set_copyright_file_id(image, xorriso->copyright_file);
   iso_image_set_biblio_file_id(image, xorriso->biblio_file);
   iso_image_set_abstract_file_id(image, xorriso->abstract_file);
 }

 if((xorriso->do_aaip & 256) && out_cs != NULL) {
   static char *names = "isofs.cs";
   size_t value_lengths[1];

   value_lengths[0]= strlen(out_cs);
   ret= Xorriso_setfattr(xorriso, NULL, "/",
                         (size_t) 1, &names, value_lengths, &out_cs, 2 | 8);
   if(ret<=0)
     goto ex;
 }
 
 ret= Xorriso_set_system_area(xorriso, source_drive, image, sopts, 0);
 if(ret <= 0)
   goto ex;

 /* Activate, adjust or discard boot image */
 if(image!=NULL) {
   is_bootable= iso_image_get_boot_image(image, NULL, NULL, &bootcat_node);
   if(xorriso->boot_image_bin_path[0]) {
     ret= Xorriso_attach_boot_image(xorriso, xorriso->boot_count == 0);
     if(ret <= 0)
       goto ex;
     freshly_bootable= 1;
   }
 }
 if(image!=NULL && !(flag&1)) {
   if(xorriso->boot_count > 0) {
     /* Eventually rename boot catalog node to changed boot_image_cat_path */
     if(is_bootable > 0) {
       ret= Xorriso_path_from_node(xorriso, (IsoNode *) bootcat_node, sfe, 0);
       if(ret > 0) {
         if(strcmp(sfe, xorriso->boot_image_cat_path) != 0) {
           ret= Xorriso_rename(xorriso, NULL, sfe,
                               xorriso->boot_image_cat_path, 0);
           if(ret <= 0)
             goto ex;
         }
       }
     }
     hide_attr= !!(xorriso->boot_image_cat_hidden);
     if(xorriso->boot_image_cat_hidden & 1)
       hide_attr|= LIBISO_HIDE_ON_RR;
     if(xorriso->boot_image_cat_hidden & 2)
       hide_attr|= LIBISO_HIDE_ON_JOLIET;
     if(xorriso->boot_image_cat_hidden & 4)
       hide_attr|= LIBISO_HIDE_ON_HFSPLUS;
     iso_image_set_boot_catalog_hidden(image, hide_attr);
   } else if(xorriso->patch_isolinux_image & 1) {
     if(is_bootable == 1) {
       relax|= isoburn_igopt_allow_full_ascii;
       sprintf(xorriso->info_text, "Patching boot info table");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);

       ret= Xorriso_path_from_lba(xorriso, NULL, xorriso->loaded_boot_bin_lba,
                                   sfe, 1);
       if(ret < 0)
         goto ex;
       if(ret == 0) {
         sprintf(xorriso->info_text,
                 "Cannot patch boot image: no file found for its LBA.");
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         sprintf(xorriso->info_text,
           "Probably the loaded boot image file was deleted in this session.");
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         sprintf(xorriso->info_text,
           "Use -boot_image \"any\" \"discard\" or set new boot image");
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
         goto ex;
       }
       ret= Xorriso_set_isolinux_options(xorriso, image, 0);
       if(ret <= 0)
         goto ex;
     } else if(!freshly_bootable) {
       sprintf(xorriso->info_text,
               "Could not find any boot image for -boot_image patching");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
     }
   } else if(xorriso->keep_boot_image && is_bootable == 1) {
     relax|= isoburn_igopt_allow_full_ascii;
     sprintf(xorriso->info_text, "Keeping boot image unchanged");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   } else if(is_bootable == 1) {
     iso_image_remove_boot_image(image);
     sprintf(xorriso->info_text, "Discarded boot image from old session");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   }
 }
 
 if((xorriso->do_aaip & 16) || !(xorriso->ino_behavior & 2)) {
   /* Overwrite isofs.st of root node by xorriso->isofs_st_out */
   char *name= "isofs.st";
   char timestamp[16], *value= timestamp;
   size_t value_length;

   sprintf(timestamp, "%.f", (double) xorriso->isofs_st_out);
   value_length= strlen(timestamp);
   Xorriso_setfattr(xorriso, NULL, "/", (size_t) 1, &name,
                    &value_length, &value, 2 | 8);
 }

 isoburn_igopt_set_level(sopts, xorriso->iso_level);
 ext= ((!!xorriso->do_rockridge) * isoburn_igopt_rockridge) |
      ((!!xorriso->do_joliet) * isoburn_igopt_joliet) |
      ((!!xorriso->do_hfsplus) * isoburn_igopt_hfsplus) |
      ((!!xorriso->do_fat) * isoburn_igopt_fat) |
      ((!!xorriso->do_iso1999) * isoburn_igopt_iso1999) |
      (( !(xorriso->ino_behavior & 2)) * isoburn_igopt_hardlinks) |
      (( (!(xorriso->ino_behavior & 2)) ||
         (xorriso->do_aaip & (2 | 8 | 16 | 256)) ||
         (xorriso->do_md5 & (2 | 4)) ||
         xorriso->do_hfsplus
       ) * isoburn_igopt_aaip) |
      ((!!(xorriso->do_md5 & 2)) * isoburn_igopt_session_md5) |
      ((!!(xorriso->do_md5 & 4)) * isoburn_igopt_file_md5) |
      ((!!(xorriso->do_md5 & 8)) * isoburn_igopt_file_stability) |
      ((!!xorriso->do_old_empty) * isoburn_igopt_old_empty) |
      ((flag & 1) * isoburn_igopt_will_cancel);
 if(xorriso->no_emul_toc & 1)
   ext|= isoburn_igopt_no_emul_toc;
 isoburn_igopt_set_extensions(sopts, ext);
 isoburn_igopt_set_relaxed(sopts, relax);
 ret = isoburn_igopt_set_rr_reloc(sopts, xorriso->rr_reloc_dir,
                                  xorriso->rr_reloc_flags);
 if(ret <= 0)
   {ret= 0; goto ex;}
 ret= isoburn_igopt_set_untranslated_name_len(sopts,
                                              xorriso->untranslated_name_len);
 if(ret <= 0)
   {ret= 0; goto ex;}
 isoburn_igopt_set_sort_files(sopts, 1);
 isoburn_igopt_set_over_mode(sopts, 0, 0, (mode_t) 0, (mode_t) 0);
 isoburn_igopt_set_over_ugid(sopts, 2 * !!xorriso->do_global_uid,
                             2 * !!xorriso->do_global_gid,
                             (uid_t) xorriso->global_uid,
                             (gid_t) xorriso->global_gid);
 isoburn_igopt_set_out_charset(sopts, out_cs);
 isoburn_igopt_set_fifo_size(sopts, xorriso->fs * 2048);
 Ftimetxt(time(NULL), xorriso->scdbackup_tag_time, 8);
 isoburn_igopt_set_scdbackup_tag(sopts, xorriso->scdbackup_tag_name,
                                 xorriso->scdbackup_tag_time,
                                 xorriso->scdbackup_tag_written);
 if(xorriso->prep_partition[0]) {
   ret= isoburn_igopt_set_prep_partition(sopts, xorriso->prep_partition, 0);
   if(ret <= 0)
     {ret= 0; goto ex;}
 }
 if(xorriso->efi_boot_partition[0]) {
   ret= isoburn_igopt_set_efi_bootp(sopts, xorriso->efi_boot_partition, 0);
   if(ret <= 0)
     {ret= 0; goto ex;}
 }
 for(i= 0; i < Xorriso_max_appended_partitionS; i++) {
   if(xorriso->appended_partitions[i] == NULL)
 continue;
   if(xorriso->appended_partitions[i][0] == 0)
 continue;
   if(strcmp(xorriso->appended_partitions[i], ".") == 0)
     part_image= "";
   else
     part_image= xorriso->appended_partitions[i];
   isoburn_igopt_set_partition_img(sopts, i + 1,
                                  xorriso->appended_part_types[i], part_image);
 }
 isoburn_igopt_set_disc_label(sopts, xorriso->ascii_disc_label); 
 isoburn_igopt_set_hfsp_serial_number(sopts, xorriso->hfsp_serial_number);
 isoburn_igopt_set_hfsp_block_size(sopts, xorriso->hfsp_block_size,
                                          xorriso->apm_block_size);

 if(image!=NULL && 12+strlen(Xorriso_timestamP)<80) {
   strcpy(xorriso_id, xorriso->preparer_id);
   img_id= (char *) iso_image_get_data_preparer_id(image);
   if(img_id!=NULL) {
     for(i= strlen(img_id)-1; i>=0 && img_id[i]==' '; i--);
     if(i>0) {
       sprintf(xorriso->info_text, "Overwrote previous preparer id '%s'",
               img_id);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
     }
   }
   iso_image_set_data_preparer_id(image, xorriso_id);
 }
 isoburn_igopt_set_pvd_times(sopts,
                    xorriso->vol_creation_time, xorriso->vol_modification_time,
                    xorriso->vol_expiration_time, xorriso->vol_effective_time,
                    xorriso->vol_uuid);

#ifdef Xorriso_with_libjtE
 if(xorriso->libjte_handle && (xorriso->libjte_params_given & (4 | 8))) {

   /* >>> Check whether the mandatory parameters are set */;

   ret= libjte_set_outfile(xorriso->libjte_handle, xorriso->outdev);
   Xorriso_process_msg_queues(xorriso, 0);
   if(ret <= 0)
     goto ex;
   isoburn_igopt_attach_jte(sopts, xorriso->libjte_handle);
   pad_by_libisofs= 1;
 }
#endif /* Xorriso_with_libjtE */

 if(xorriso->do_padding_by_libisofs || pad_by_libisofs) {
   /* Padding to be done by libisofs, not by libburn.
   */
   padding= xorriso->padding / 2048;
   if((uint32_t) xorriso->padding > padding * 2048)
     padding++;
/*
fprintf(stderr, "XORRISO_DEBUG: isoburn_igopt_set_tail_blocks(%d)\n",
        (int) padding);
*/
   isoburn_igopt_set_tail_blocks(sopts, padding);
 }

 /* Make final abort check before starting expensive activities */
 ret= Xorriso_eval_problem_status(xorriso, 1, 0);
 if(ret<0)
   {ret= 0; goto ex;}

 if(xorriso->zisofs_by_magic) {
   sprintf(xorriso->info_text,
           "Checking disk file content for zisofs compression headers.");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "UPDATE", 0);
   root_node= (IsoNode *) iso_image_get_root(image);
   ret= iso_node_zf_by_magic(root_node,
             (xorriso->out_drive_handle == xorriso->in_drive_handle) | 2 | 16);
   if(ret<0) {
     Xorriso_report_iso_error(xorriso, "", ret,
              "Error when examining file content for zisofs headers",
              0, "FAILURE", 1);
   }
   ret= Xorriso_eval_problem_status(xorriso, 1, 0);
   if(ret<0)
     {ret= 0; goto ex;}
   sprintf(xorriso->info_text,
           "Check for zisofs compression headers done.");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "UPDATE", 0);
 }

 /* >>> omit iso_image_update_sizes if the image was filled up very quickly */;

 ret= iso_image_update_sizes(image);
 if(ret < 0) {
   Xorriso_process_msg_queues(xorriso, 0);
   if(ret<0) {
     Xorriso_report_iso_error(xorriso, "", ret,
              "Error when updating file sizes",
              0, "FAILURE", 1);
   }
   ret= Xorriso_eval_problem_status(xorriso, 1, 0);
   if(ret<0)
     {ret= 0; goto ex;}
 }

 ret = isoburn_igopt_set_write_type(sopts, xorriso->do_tao);
 if(ret <= 0)
   goto ex;

 Xorriso_set_abort_severity(xorriso, 1);
 if (xorriso->grow_blindly_msc2 >= 0 &&
     xorriso->out_drive_handle != xorriso->in_drive_handle) {
   ret= isoburn_prepare_blind_grow(source_drive, &disc, sopts, drive,
                                   xorriso->grow_blindly_msc2);
   if(ret>0) {
     /* Allow the consumer of output to access the input drive */
     source_drive= NULL;
     ret= Xorriso_give_up_drive(xorriso, 1|8); 
     if(ret<=0)
       goto ex;
   }
 } else if(xorriso->out_drive_handle == xorriso->in_drive_handle ||
           xorriso->in_drive_handle == NULL) {
   ret= isoburn_prepare_disc(source_drive, &disc, sopts);
 } else {
   ret= isoburn_prepare_new_image(source_drive, &disc, sopts, drive);
 }
 if(ret <= 0) {
   Xorriso_process_msg_queues(xorriso,0);
   sprintf(xorriso->info_text,"Failed to prepare session write run");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }

 ret= Xorriso_make_write_options(xorriso, drive, &burn_options, 0);
 if(ret<=0)
   goto cancel_iso;
 isoburn_igopt_get_effective_lba(sopts, &(xorriso->session_lba));
 if(xorriso->do_stream_recording == 2) {
   ret= isoburn_igopt_get_data_start(sopts, &data_lba);
   if(ret > 0 && data_lba >= 16)
     burn_write_opts_set_stream_recording(burn_options, data_lba);
 }

 ret= Xorriso_sanitize_image_size(xorriso, drive, disc, burn_options, flag&1);
 if(ret<=0 || (flag&1)) {
   Xorriso_process_msg_queues(xorriso,0);
   if(flag&1) /* set queue severity to FAILURE */
     Xorriso_set_image_severities(xorriso, 2);
   if(flag&1) /* reset queue severity */
     Xorriso_set_image_severities(xorriso, 0);
   goto cancel_iso;
 }

 ret= Xorriso_may_burn(xorriso, 0);
 if(ret <= 0)
   goto cancel_iso;

 role= burn_drive_get_drive_role(drive);

 /* Important: do not return until burn_is_aborting() was checked */

 signal_mode= 1;
 if(role == 1)
   signal_mode|= 2;
 Xorriso_set_signal_handling(xorriso, signal_mode);

 /* De-activate eventual target file truncation in dummy mode */
 ret= isoburn_set_truncate(drive, (!xorriso->do_dummy) | 2 | 4);
 if(ret < 0)
   goto cancel_iso;

 xorriso->run_state= 1; /* Indicate that burning has started */
 isoburn_disc_write(burn_options, disc);
 burn_write_opts_free(burn_options);
 burn_options= NULL;

 ret= Xorriso_pacifier_loop(xorriso, drive, pacifier_speed << 4);
 if(burn_is_aborting(0))
   Xorriso_abort(xorriso, 0); /* Never comes back */
 Xorriso_set_signal_handling(xorriso, 0);
 if(ret<=0)
   goto ex;
 if(!isoburn_drive_wrote_well(drive)) {
   isoburn_cancel_prepared_write(source_drive, drive, 0);
   Xorriso_process_msg_queues(xorriso,0);
   sprintf(xorriso->info_text,
           "libburn indicates failure with writing.");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   ret= 0; goto ex;
 }
 Xorriso_process_msg_queues(xorriso,0);

 sessions= burn_disc_get_sessions(disc, &num_sessions);
 if(num_sessions>0) {
   tracks= burn_session_get_tracks(sessions[0], &num_tracks);
   if(tracks!=NULL && num_tracks>0) {
     burn_track_get_counters(tracks[0],&readcounter,&writecounter);
     xorriso->session_blocks= (int) (writecounter/ (off_t) 2048);
     sprintf(xorriso->info_text,
  "ISO image produced: %d sectors\nWritten to medium : %d sectors at LBA %d\n",
        (int) (readcounter/ (off_t) 2048),
        xorriso->session_blocks, xorriso->session_lba);
     Xorriso_info(xorriso, 0);
   }
 }
 ret= isoburn_activate_session(drive);
 Xorriso_process_msg_queues(xorriso,0);
 if(ret<=0) {
   sprintf(xorriso->info_text,
           "Could not write new set of volume descriptors");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   goto ex;
 }
 /* Done early to free any reference to the libisofs resources via disc */
 if(disc!=NULL)
   burn_disc_free(disc);
 disc= NULL;
 /* To wait for the end of the libisofs threads and their messages. */
 isoburn_sync_after_write(source_drive, drive, 0);
 Xorriso_process_msg_queues(xorriso,0);

 sprintf(xorriso->info_text, "Writing to %s completed successfully.\n\n",
         Text_shellsafe(xorriso->outdev,sfe,0));
 Xorriso_info(xorriso, 0);
 ret= 1;
ex:;
 xorriso->run_state= 0; /* Indicate that burning has ended */
 Xorriso_set_abort_severity(xorriso, 0);

 if(ret<=0) {

   /* >>> ??? revive discarded boot image */;

 }
 if(disc!=NULL)
   burn_disc_free(disc);
 if(image != NULL)
   iso_image_unref(image);
 isoburn_igopt_destroy(&sopts, 0);
 if(burn_options != NULL)
   burn_write_opts_free(burn_options);
 if(caps != NULL)
   burn_disc_free_multi_caps(&caps);
 Xorriso_process_msg_queues(xorriso,0);
 Xorriso_append_scdbackup_record(xorriso, 0);
 Xorriso_free_meM(sfe);
 Xorriso_free_meM(xorriso_id);
 Xorriso_free_meM(profile_name);
 Xorriso_free_meM(reasons);
 return(ret);

cancel_iso:;
 isoburn_cancel_prepared_write(source_drive, drive, 0);
 goto ex;
} 


int Xorriso_check_burn_abort(struct XorrisO *xorriso, int flag)
{
 int ret;
 struct burn_drive_info *dinfo;
 struct burn_drive *drive;

 if(burn_is_aborting(0))
   return(2);
 if(xorriso->run_state!=1)
   return(0);
 ret= Xorriso_eval_problem_status(xorriso, 1, 1);
 if(ret>=0)
   return(0);
 sprintf(xorriso->info_text,
         "-abort_on '%s' encountered '%s' during image writing",
         xorriso->abort_on_text, xorriso->problem_status_text);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0,
                     xorriso->problem_status_text, 0);

 ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                "on attempt to abort burn run", 2);
 if(ret<=0)
   return(0);

 burn_drive_cancel(drive);
 sprintf(xorriso->info_text,
         "libburn has now been urged to cancel its operation");
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
 return(1);
}


/* This loop watches burn runs until they end.
   It issues pacifying update messages to the user.
   @param flag bit0-3 = emulation mode
                        0= xorriso
                        1= mkisofs
                        2= cdrecord
               bit4= report speed in CD units
               bit5= report speed in BD units
*/
int Xorriso_pacifier_loop(struct XorrisO *xorriso, struct burn_drive *drive,
                          int flag)
{
 int ret, size, free_bytes, i, aborting= 0, emul, buffer_fill= 50, last_sector;
 int iso_wait_counter= 0, iso_cancel_limit= 5;
 struct burn_progress progress;
 char *status_text, date_text[80], *speed_unit, mem_text[8];
 enum burn_drive_status drive_status;
 double start_time, current_time, last_time, base_time= 0.0, base_count= 0.0;
 double next_base_time= 0.0, next_base_count= 0.0, first_base_time= 0.0;
 double first_base_count= 0.0, norm= 0.0, now_time, fract_offset= 0.0;
 double measured_speed, speed_factor= 1385000, quot;
 time_t time_prediction;
 IsoImage *image= NULL;

 image= isoburn_get_attached_image(drive);

 start_time= Sfile_microtime(0);
 while(burn_drive_get_status(drive, NULL) == BURN_DRIVE_SPAWNING)
   usleep(100002);

 emul= flag&15;
 fract_offset= 0.2 * (double) emul - ((int) (0.2 * (double) emul));
 if(emul==0)
   emul= xorriso->pacifier_style;
 speed_unit= "D";
 if(flag&16) {
   speed_factor= 150.0*1024;
   speed_unit= "C";
 } else if(flag & 32) {
   speed_factor= 4495625;
   speed_unit= "B";
 }
 progress.sector= 0;
 current_time= Sfile_microtime(0);
 measured_speed= 0.0;
 while(1) {
   last_time= current_time;
   last_sector= progress.sector;
   drive_status= burn_drive_get_status(drive, &progress);

   if(drive_status == BURN_DRIVE_IDLE) {
     /* To avoid a race condition between burn_source and libisofs
        writer thread: Wait for ISO generator thread to have finished.
        After some seconds kick it by isoburn_cancel_prepared_write()
        which waits for the write thread to end.
     */
     if(image == NULL)
 break;
     if(!iso_image_generator_is_running(image))
 break;
     iso_wait_counter++;
     if(iso_wait_counter > iso_cancel_limit) {
       isoburn_cancel_prepared_write(drive, NULL, 0);
 break;
     }
   }
   current_time= Sfile_microtime(0);
   if(drive_status == BURN_DRIVE_WRITING && progress.sectors > 0) {
     if(current_time-last_time>0.2)
       measured_speed= (progress.sector - last_sector) * 2048.0 /
                       (current_time - last_time);
     buffer_fill= 50;
     if(progress.buffer_capacity>0)
       buffer_fill= (double) (progress.buffer_capacity
                              - progress.buffer_available) * 100.0
                    / (double) progress.buffer_capacity;
     if(emul==2) {
       if(progress.sector<=progress.sectors)
         sprintf(xorriso->info_text, "%4d of %4d MB written",
                 progress.sector / 512, progress.sectors / 512);
       else
         sprintf(xorriso->info_text, "%4d MB written",
                 progress.sector / 512);

       if(xorriso->pacifier_fifo!=NULL)
         ret= burn_fifo_inquire_status(xorriso->pacifier_fifo,
                                       &size, &free_bytes, &status_text);
       else
         ret= isoburn_get_fifo_status(drive, &size, &free_bytes, &status_text);
       if(ret>0 )
         sprintf(xorriso->info_text+strlen(xorriso->info_text),
                 " (fifo %2d%%)", 
                 (int) (100.0-100.0*((double) free_bytes)/(double) size));

       sprintf(xorriso->info_text+strlen(xorriso->info_text), " [buf %3d%%]",
               buffer_fill);

       if(current_time-last_time>0.2)
         sprintf(xorriso->info_text+strlen(xorriso->info_text), "  %4.1fx.",
                 measured_speed/speed_factor);

     } else if(emul == 1 &&
               progress.sectors > 0 && progress.sector <= progress.sectors) {
       /* "37.87% done, estimate finish Tue Jul 15 18:55:07 2008" */

       quot= ((double) progress.sector) / ((double) progress.sectors);
       sprintf(xorriso->info_text, " %2.2f%% done", quot*100.0);
       if(current_time - start_time >= 2 && quot > 0.0 &&
          (quot >= 0.02 || progress.sector >= 5*1024)) {
         if(base_time == 0.0 && progress.sector >= 16*1024) {
           first_base_time= base_time= next_base_time= current_time;
           first_base_count= next_base_count= progress.sector;
         } else if(next_base_time > 0 && current_time - next_base_time >= 10) {
           base_time= next_base_time;
           base_count= next_base_count;
           next_base_time= current_time;
           next_base_count= progress.sector;
         }
         if(first_base_time > 0 &&
            current_time - first_base_time >= 10 &&
            progress.sectors > first_base_count &&
            progress.sector > first_base_count) {
           norm= (1.0 - quot);
           if(norm < 0.0001)
             norm= 0.0001;
           quot= ((double) progress.sector - first_base_count)
                  / ((double) progress.sectors - first_base_count);
           time_prediction= norm * (1.0 - quot) / quot
                            * (current_time - first_base_time);
         } else {
           time_prediction= (1.0 - quot) / quot * (current_time - start_time);
           norm= 1.0;
         }
         if(base_time > 0 && 
            current_time - base_time >= 10 && progress.sectors > base_count) {
           quot= ((double) progress.sector - base_count)
                  / ((double) progress.sectors - base_count);
           time_prediction+= (1.0 - quot) / quot * (current_time - base_time);
           norm+= 1.0;
         }
         time_prediction/= norm;
         if(time_prediction < 30*86400 && time_prediction > 0) {
           time_prediction+= current_time + 1;
           Ftimetxt(time_prediction, date_text, 4);
           sprintf(xorriso->info_text+strlen(xorriso->info_text),
                   ", estimate finish %s", date_text);
         }
       }
     } else {
       if(progress.sector<=progress.sectors) {
         if(progress.sectors <= 0)
           strcpy(mem_text, " 99.9");
         else
           sprintf(mem_text, "%5.1f",
             100.0 * ((double) progress.sector) / ((double) progress.sectors));
         mem_text[5]= 0;
         sprintf(xorriso->info_text, "Writing: %10ds  %s%% ",
                 progress.sector, mem_text);
       } else {
         Sfile_scale(2048.0 * (double) progress.sector, mem_text, 5, 1e4, 1);
         sprintf(xorriso->info_text, "Writing: %10ds   %s ",
                 progress.sector, mem_text);
       }
       ret= isoburn_get_fifo_status(drive, &size, &free_bytes, &status_text);
       if(ret>0 )
         sprintf(xorriso->info_text+strlen(xorriso->info_text),
                 "  fifo %3d%%  buf %3d%%",
                 (int) (100.0-100.0*((double) free_bytes)/(double) size),
                 buffer_fill);
       if(current_time-last_time>0.2)
         sprintf(xorriso->info_text+strlen(xorriso->info_text), "  %5.1fx%s ",
                 measured_speed/speed_factor, speed_unit);
     }
   } else if(drive_status == BURN_DRIVE_CLOSING_SESSION ||
             drive_status == BURN_DRIVE_CLOSING_TRACK)
     sprintf(xorriso->info_text,
             "Closing track/session. Working since %.f seconds",
             current_time-start_time);
   else if(drive_status == BURN_DRIVE_FORMATTING)
     sprintf(xorriso->info_text, "Formatting. Working since %.f seconds",
             current_time-start_time);
   else
     sprintf(xorriso->info_text,
             "Thank you for being patient. Working since %.f seconds.",
             current_time-start_time);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "UPDATE", 0);

   for(i= 0; i<12; i++) { /* 2 usleeps more than supposed to be needed */
     Xorriso_process_msg_queues(xorriso, 0);
     if(aborting<=0)
       aborting= Xorriso_check_burn_abort(xorriso, 0);
     usleep(100000);
     now_time= Sfile_microtime(0);
     if(((time_t) now_time) - ((time_t) current_time) >= 1 &&
         now_time - ((time_t) now_time) >= fract_offset)
   break;
   }
 }
 iso_image_unref(image);
 return(1);
}


/* @param flag bit0= fast
               bit1= deformat
               bit2= do not re-aquire drive
   @return 0=failure, did not touch medium , -1=failure, altered medium
           1=success, altered medium       ,  2=success, did not touch medium
*/
int Xorriso_blank_media(struct XorrisO *xorriso, int flag)
{
 int ret, do_deformat= 0, signal_mode;
 struct burn_drive_info *dinfo;
 struct burn_drive *drive;
 enum burn_disc_status disc_state;
 struct burn_progress p;
 double percent = 1.0;
 int current_profile;
 char current_profile_name[80];
 time_t start_time;
 char mode_names[4][80]= {"all", "fast", "deformat", "deformat_quickest"};

 ret= Xorriso_may_burn(xorriso, 0);
 if(ret <= 0)
   return(0);
 ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                "on attempt to -blank", 2);
 if(ret<=0)
   return(0);

 burn_disc_get_profile(drive, &current_profile, current_profile_name);

 disc_state = isoburn_disc_get_status(drive);
 if(current_profile == 0x13) { /* overwriteable DVD-RW */
   /* Depending on flag bit1 formatted DVD-RW will get blanked to sequential
      state or pseudo blanked by invalidating an eventual ISO image. */
   if(flag&2)
     do_deformat= 1;
 } else if(current_profile == 0x14) { /* sequential DVD-RW */
   if((flag&1) && !(flag&2)) {
     sprintf(xorriso->info_text,
             "-blank: DVD-RW present. Mode 'fast' defaulted to mode 'all'.");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
     sprintf(xorriso->info_text,
             "Mode 'deformat_quickest' produces single-session-only media.");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
     flag&= ~1;
   }
 }
 if(disc_state == BURN_DISC_BLANK) {
   if(!do_deformat) {
     sprintf(xorriso->info_text,
             "Blank medium detected. Will leave it untouched");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
     return 2;
   }
 } else if(disc_state==BURN_DISC_FULL || disc_state==BURN_DISC_APPENDABLE) {
   ;
 } else if(disc_state == BURN_DISC_EMPTY) {
   sprintf(xorriso->info_text,"No media detected in drive");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return 0;
 } else {
   sprintf(xorriso->info_text, "Unsuitable drive and media state");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return 0;
 }
 if(!isoburn_disc_erasable(drive)) {
   sprintf(xorriso->info_text, "Media is not of erasable type");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return 0;
 }
 if(xorriso->do_dummy) {
   sprintf(xorriso->info_text,
           "-dummy mode prevents blanking of medium in mode '%s'.",
           mode_names[flag&3]);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   return(1);
 }
 sprintf(xorriso->info_text, "Beginning to blank medium in mode '%s'.\n",
         mode_names[flag&3]);
 Xorriso_info(xorriso,0);

 /* Important: do not return until burn_is_aborting() was checked */
 signal_mode= 1;
 ret= burn_drive_get_drive_role(drive);
 if(ret == 1)
   signal_mode|= 2;
 Xorriso_set_signal_handling(xorriso, signal_mode);

 if(do_deformat)
   burn_disc_erase(drive, (flag&1));
 else
   isoburn_disc_erase(drive, (flag&1));
 start_time= time(0);
 usleep(1000000);
 while (burn_drive_get_status(drive, &p) != BURN_DRIVE_IDLE) {
   Xorriso_process_msg_queues(xorriso,0);
   if(p.sectors>0 && p.sector>=0) /* display 1 to 99 percent */
     percent = 1.0 + ((double) p.sector+1.0) / ((double) p.sectors) * 98.0;
   sprintf(xorriso->info_text, "Blanking  ( %.1f%% done in %d seconds )",
           percent, (int) (time(0) - start_time));
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "UPDATE", 0);
   usleep(1000000);
 }
 Xorriso_process_msg_queues(xorriso,0);
 if(burn_is_aborting(0))
   Xorriso_abort(xorriso, 0); /* Never comes back */
 Xorriso_set_signal_handling(xorriso, 0);
 if(burn_drive_wrote_well(drive)) {
   sprintf(xorriso->info_text, "Blanking done\n");
   Xorriso_info(xorriso,0);
 } else {
   sprintf(xorriso->info_text, "Blanking failed.");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
 }
 if(!(flag & 4)) {
   ret= Xorriso_reaquire_outdev(xorriso, 
                  2 + (xorriso->in_drive_handle == xorriso->out_drive_handle));
   if(ret <= 0)
     return(-1);
 }
 return(1);
}


/* @param flag bit0= try to achieve faster formatting
               bit1= use parameter size (else use default size)
               bit2= do not re-aquire drive
               bit7= by_index mode:
                     bit8 to bit15 contain the index of the format to use.
   @return 0=failure, did not touch medium , -1=failure, altered medium
           1=success, altered medium       ,  2=success, did not touch medium
*/
int Xorriso_format_media(struct XorrisO *xorriso, off_t in_size, int flag)
{
 int ret, mode_flag= 0, index, status, num_formats, signal_mode;
 unsigned dummy;
 struct burn_drive_info *dinfo;
 struct burn_drive *drive;
 struct burn_progress p;
 double percent = 1.0;
 int current_profile;
 char current_profile_name[80];
 off_t size= 0;
 time_t start_time;
 enum burn_disc_status disc_state;

 ret= Xorriso_may_burn(xorriso, 0);
 if(ret <= 0)
   return(0);
 ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                "on attempt to -format", 2);
 if(ret<=0)
   return(0);

 if(flag & 2) {
   mode_flag= 0; /* format to given size */
 } else {
   mode_flag= 4; /* format to full size */
 }
 burn_disc_get_profile(drive, &current_profile, current_profile_name);

 if(flag&128) { /* by_index */
   index= (flag>>8) & 0xff;
   ret= burn_disc_get_formats(drive, &status, &size, &dummy, &num_formats);
   if(ret<=0)
     num_formats= 0;
   if(ret<=0 || index<0 || index>=num_formats) {
     if(num_formats>0)
       sprintf(xorriso->info_text,
            "-format by_index_%d: format descriptors range from index 0 to %d",
            index, num_formats-1);
     else
       sprintf(xorriso->info_text,
               "-format by_index_%d: no format descriptors available", index);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
   mode_flag|= (flag & 0xff80);
   if(flag&1)
     mode_flag|= (1<<6);

 } else if(current_profile == 0x12) { /* DVD+RAM */
   if(!(flag & 2))
     mode_flag= 6; /* format to default payload size */
   if(flag&1)
     mode_flag|= (1<<6);

 } else if(current_profile == 0x13) { /* DVD-RW */
   if(flag&1) {
     sprintf(xorriso->info_text,
          "Detected formatted DVD-RW. Thus omitting desired fast format run.");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
     return(2);
   }

 } else if(current_profile == 0x14) { /* DVD-RW sequential */
   if(flag & 1) {
     size= 128*1024*1024;
     mode_flag= 1; /* format to size, then write size of zeros */
   } else
     mode_flag= 4;

 } else if(current_profile == 0x1a) { /* DVD+RW */
   if(flag&1) {
     sprintf(xorriso->info_text,
             "Detected DVD+RW. Thus omitting desired fast format run.");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
     return(2);
   }

 } else if(current_profile == 0x41) { /* BD-R SRM */
   if(!(flag & 2))
     mode_flag= 6; /* format to default payload size */
   if(flag&1)
     mode_flag|= (1<<6);

 } else if(current_profile == 0x43) { /* BD-RE */
   if(!(flag & 2))
     mode_flag= 6; /* format to default payload size */
   if(flag&1)
     mode_flag|= (1<<6);

 } else {
   sprintf(xorriso->info_text,
          "-format: Unsuitable media detected.");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   sprintf(xorriso->info_text,"Media current: %s (%4.4xh)",
           current_profile_name, current_profile);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   return(0);
 }
 if(!(flag & 1))
   mode_flag|= 16; /* enable re-formatting */

 if(xorriso->do_dummy) {
   sprintf(xorriso->info_text, "-dummy mode prevents formatting of medium.");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   return(1);
 }
 sprintf(xorriso->info_text, "Beginning to format medium.\n");
 Xorriso_info(xorriso, 0);
 if(flag & 2)
   size= in_size;

 /* Important: do not return until burn_is_aborting() was checked */
 signal_mode= 1;
 ret= burn_drive_get_drive_role(drive);
 if(ret == 1)
   signal_mode|= 2;
 Xorriso_set_signal_handling(xorriso, signal_mode);

 burn_disc_format(drive, size, mode_flag);

 start_time= time(0);
 usleep(1000000);
 while (burn_drive_get_status(drive, &p) != BURN_DRIVE_IDLE) {
   Xorriso_process_msg_queues(xorriso,0);
   if(p.sectors>0 && p.sector>=0) /* display 1 to 99 percent */
     percent = 1.0 + ((double) p.sector+1.0) / ((double) p.sectors) * 98.0;
   sprintf(xorriso->info_text, "Formatting  ( %.1f%% done in %d seconds )",
           percent, (int) (time(0) - start_time));
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "UPDATE", 0);
   usleep(1000000);
 }
 Xorriso_process_msg_queues(xorriso,0);
 if(burn_is_aborting(0))
   Xorriso_abort(xorriso, 0); /* Never comes back */
 Xorriso_set_signal_handling(xorriso, 0);
 
 if(burn_drive_wrote_well(drive)) {
   sprintf(xorriso->info_text, "Formatting done\n");
   Xorriso_info(xorriso,0);
 } else {
   sprintf(xorriso->info_text,
           "libburn indicates failure with formatting."); 
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(-1);
 }

 if(!(flag & 4)) {
   ret= Xorriso_reaquire_outdev(xorriso, 
                  2 + (xorriso->in_drive_handle == xorriso->out_drive_handle));
   if(ret <= 0)
     return(-1);
 }
 disc_state = isoburn_disc_get_status(drive);
 if(disc_state==BURN_DISC_FULL && !(flag&1)) {
   /* Blank because full format certification pattern might be non-zero */
   ret= Xorriso_blank_media(xorriso, 1);
   if(ret <= 0)
     return(0);
 }
 return(1);
}


/* @param flag bit2= formatting rather than blanking
   @return 0=failure, did not touch medium , -1=failure, altered medium
           1=success, altered medium       ,  2=success, did not touch medium
*/
int Xorriso_blank_as_needed(struct XorrisO *xorriso, int flag)
{
 int ret, is_formatted= -1, status, num_formats, did_work= 0;
 struct burn_drive_info *dinfo;
 struct burn_drive *drive;
 enum burn_disc_status disc_state;
 unsigned dummy;
 int current_profile;
 char current_profile_name[80];
 off_t size;

 ret= Xorriso_may_burn(xorriso, 0);
 if(ret <= 0)
   return(0);
 ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                "on attempt to blank or format", 2);
 if(ret<=0)
   return(0);

 burn_disc_get_profile(drive, &current_profile, current_profile_name);

 ret= burn_disc_get_formats(drive, &status, &size, &dummy, &num_formats);
 if(ret>0) {
   if(status==BURN_FORMAT_IS_FORMATTED)
     is_formatted= 1;
   else if(status == BURN_FORMAT_IS_UNFORMATTED)
     is_formatted= 0;
 }
 if(current_profile == 0x12 || current_profile == 0x43) { /* DVD+RAM , BD-RE */
   if(is_formatted<0) {
     sprintf(xorriso->info_text,
             "-blank or -format: Unclear formatting status of %s",
             current_profile_name);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
   if(!is_formatted) {
     ret= Xorriso_format_media(xorriso, (off_t) 0, (current_profile == 0x43));
     if(ret <= 0)
       return(ret);
     did_work= (ret == 1);
   }
 } else if(current_profile == 0x14 && (flag&4)) { /* DVD-RW sequential */
   ret= Xorriso_format_media(xorriso, (off_t) 0, 0);
   if(ret <= 0)
     return(ret);
   did_work= (ret == 1);
 } else if(current_profile == 0x41) { /* BD-R SRM */
   if(!is_formatted) {
     ret= Xorriso_format_media(xorriso, (off_t) 0, 1);
     if(ret <= 0)
       return(ret);
     did_work= (ret == 1);
   }
 }

 disc_state = isoburn_disc_get_status(drive);
 if(disc_state != BURN_DISC_BLANK && !(flag&4)) {
   ret= Xorriso_blank_media(xorriso, 1);
   return(ret);
 }
 if(did_work)
   return(1);
 sprintf(xorriso->info_text, "%s as_needed: no need for action detected",
         (flag&4) ? "-format" : "-blank");
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
 return(2);
}


/* @param write_start_address  is valid if >=0    
   @param tsize                is valid if >0
   @param flag bit0= grow_overwriteable_iso
               bit1= do_isosize
               bit2= do_xa1 conversion
*/
int Xorriso_burn_track(struct XorrisO *xorriso, off_t write_start_address,
                       char *track_source, off_t tsize, int flag)
{
 int ret, fd, profile_number, is_cd= 0, dummy, nwa= -1;
 int isosize= -1, do_isosize, is_bd= 0, signal_mode;
 struct burn_drive_info *dinfo;
 struct burn_drive *drive;
 struct burn_write_opts *burn_options= NULL;
 struct burn_disc *disc= NULL;
 struct burn_session *session= NULL;
 struct burn_track *track= NULL;
 struct stat stbuf; 
 off_t fixed_size= 0;
 struct burn_source *data_src= NULL, *fifo_src= NULL;
 enum burn_disc_status disc_state;
 char *reasons= NULL, *profile_name= NULL;
 char *head_buffer= NULL;

 Xorriso_alloc_meM(reasons, char, BURN_REASONS_LEN);
 Xorriso_alloc_meM(profile_name, char, 80);
 Xorriso_alloc_meM(head_buffer, char, 64 * 1024);

 ret= Xorriso_may_burn(xorriso, 0);
 if(ret <= 0)
   {ret= 0; goto ex;}
 ret= Xorriso_auto_format(xorriso, 0);
 if(ret <=0 )
   {ret= 0; goto ex;}

 do_isosize= !!(flag&2);
 ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                "on attempt to burn track", 2);
 if(ret<=0)
   {ret= 0; goto ex;}

 ret= Xorriso_check_multi(xorriso, drive, 1);
 if(ret<=0) 
   goto ex;
 ret= Xorriso_make_write_options(xorriso, drive, &burn_options, 0);
 if(ret<=0)
   goto ex;

 disc= burn_disc_create();
 session= burn_session_create();
 ret= burn_disc_add_session(disc,session,BURN_POS_END);
 if(ret==0) {
   sprintf(xorriso->info_text, "Cannot add session object to disc object.");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   goto ex;
 }
 track= burn_track_create();
 if(track_source[0] == '-' && track_source[1] == 0) {
   fd= 0;
 } else {
   if(xorriso->fs >= 64)
     fd= burn_os_open_track_src(track_source, O_RDONLY, 0);
   else
     fd= open(track_source, O_RDONLY);
   if(fd>=0)
     if(fstat(fd,&stbuf)!=-1)
       if((stbuf.st_mode&S_IFMT)==S_IFREG)
         fixed_size= stbuf.st_size;
 }

 if(fd>=0)
   data_src= burn_fd_source_new(fd, -1, fixed_size);
 if(data_src==NULL) {
   sprintf(xorriso->info_text, "Could not open data source ");
   Text_shellsafe(track_source, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
   ret= 0; goto ex;
 }
 if((do_isosize || xorriso->fs != 0) && xorriso->fs < 64)
   xorriso->fs= 64;
 if(xorriso->fs > 0) {
   fifo_src= burn_fifo_source_new(data_src, 2048 + 8 * !!(flag & 4),
                                  xorriso->fs, 1);
   if(fifo_src == NULL) {
     sprintf(xorriso->info_text, "Could not create fifo object of %.f MB",
             ((double) xorriso->fs) / 1024.0 / 1024.0);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
     ret= 0; goto ex;
   }
 } 
 xorriso->pacifier_fifo= fifo_src;
 if(burn_track_set_source(track, fifo_src == NULL ? data_src : fifo_src)
    != BURN_SOURCE_OK) {
   sprintf(xorriso->info_text,
           "Cannot attach source object to track object");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   ret= 0; goto ex;
 }
 burn_track_set_cdxa_conv(track, !!(flag & 4));
 burn_session_add_track(session, track, BURN_POS_END);
 burn_source_free(data_src);

 if(flag&1)
   /* consider overwriteables with ISO as appendable */
   disc_state= isoburn_disc_get_status(drive);
 else
   /* handle overwriteables as always blank */
   disc_state= burn_disc_get_status(drive);

 if(disc_state == BURN_DISC_BLANK || disc_state == BURN_DISC_APPENDABLE) {
   /* ok */;
 } else {
   if(disc_state == BURN_DISC_FULL) {
     sprintf(xorriso->info_text,
          "Closed media with data detected. Need blank or appendable media.");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     if(burn_disc_erasable(drive)) {
       sprintf(xorriso->info_text, "Try -blank as_needed\n");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
     }
   } else if(disc_state == BURN_DISC_EMPTY) {
     sprintf(xorriso->info_text, "No media detected in drive");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   } else {
     sprintf(xorriso->info_text,
             "Cannot recognize state of drive and media");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   }
   ret= 0; goto ex;
 }
 if(isoburn_needs_emulation(drive))
   burn_write_opts_set_multi(burn_options, 0);

 if(tsize > 0) {
   fixed_size= tsize;
   burn_track_set_size(track, fixed_size);
 }
 if(do_isosize) {
   ret= burn_fifo_peek_data(xorriso->pacifier_fifo, head_buffer, 64*1024, 0);
   if(ret<=0) {
     Xorriso_process_msg_queues(xorriso,0);
     sprintf(xorriso->info_text,
             "Cannot obtain first 64 kB from input stream."); 
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }
   /* read isosize from head_buffer, not from medium */
   ret= isoburn_read_iso_head(drive, 0, &isosize, head_buffer, (1<<13));
   if(ret<=0) {
     Xorriso_process_msg_queues(xorriso,0);
     sprintf(xorriso->info_text,
             "Option -isosize given but data stream seems not to be ISO 9660"); 
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }
   sprintf(xorriso->info_text, "Size of ISO 9660 image: %ds", isosize);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   fixed_size= ((off_t) (isosize)) * (off_t) 2048;
   burn_track_set_size(track, fixed_size);
 }

 ret= Xorriso_get_profile(xorriso, &profile_number, profile_name, 2);
 is_cd= (ret==2);
 is_bd= (ret == 3);

 if(isoburn_needs_emulation(drive)) {
   if(flag&1) {
     ret= isoburn_disc_track_lba_nwa(drive, burn_options, 0, &dummy, &nwa);
     Xorriso_process_msg_queues(xorriso,0);
     if(ret<=0) {
       sprintf(xorriso->info_text,
     "Cannot obtain next writeable address of emulated multi-session media\n");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
     }
     if(nwa == 32 && disc_state != BURN_DISC_APPENDABLE)
       nwa= 0; /* No automatic toc emulation. Formatter might not be aware. */
   } else {
     nwa= 0;
     if (disc_state == BURN_DISC_APPENDABLE) {
       ret= isoburn_disc_track_lba_nwa(drive, burn_options, 0, &dummy, &nwa);
       Xorriso_process_msg_queues(xorriso,0);
       if(ret<=0) {
         sprintf(xorriso->info_text,
        "Cannot obtain next writeable address of emulated appendable media\n");
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         ret= 0; goto ex;
       }
     }
   }
   burn_write_opts_set_start_byte(burn_options,((off_t) nwa) * (off_t) 2048);
 }

 if(write_start_address>=0) {
   nwa= write_start_address / (off_t) 2048;
   if(((off_t) nwa) * (off_t) 2048 < write_start_address )
     nwa++;
   burn_write_opts_set_start_byte(burn_options, ((off_t) nwa) * (off_t) 2048);
 }

 if(xorriso->do_tao) {
   if (xorriso->do_tao > 0)
     burn_write_opts_set_write_type(burn_options,
                                    BURN_WRITE_TAO, BURN_BLOCK_MODE1);
   else
     burn_write_opts_set_write_type(burn_options,
                                    BURN_WRITE_SAO, BURN_BLOCK_SAO);
                                    
   ret = burn_precheck_write(burn_options, disc, reasons, 0);
   if(ret<=0) {
     sprintf(xorriso->info_text,
             "Cannot set write type %s for this medium.\n",
             xorriso->do_tao > 0 ? "TAO" : "SAO");
     sprintf(xorriso->info_text+strlen(xorriso->info_text),
             "Reasons given:\n%s", reasons);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }
   sprintf(xorriso->info_text, "Explicitly chosen write type: %s",
           xorriso->do_tao > 0 ? "TAO" : "SAO");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
 } else {
   if(burn_write_opts_auto_write_type(burn_options, disc, reasons, 0) ==
      BURN_WRITE_NONE) {
     sprintf(xorriso->info_text,
             "Failed to find a suitable write mode with this media.\n");
     sprintf(xorriso->info_text+strlen(xorriso->info_text),
             "Reasons given:\n%s", reasons);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }
 }

 ret= Xorriso_sanitize_image_size(xorriso, drive, disc, burn_options, 2);
 if(ret<=0) 
   goto ex;

 sprintf(xorriso->info_text, "Beginning to write data track.\n");
 Xorriso_info(xorriso,0);

 /* Important: do not return until burn_is_aborting() was checked */
 signal_mode= 1;
 ret= burn_drive_get_drive_role(drive);
 if(ret == 1)
   signal_mode|= 2;
 Xorriso_set_signal_handling(xorriso, signal_mode);

 xorriso->run_state= 1; /* Indicate that burning has started */
 burn_disc_write(burn_options, disc);

 ret= Xorriso_pacifier_loop(xorriso, drive, 2 | (is_cd << 4) | (is_bd << 5));
 if(burn_is_aborting(0))
   Xorriso_abort(xorriso, 0); /* Never comes back */
 Xorriso_set_signal_handling(xorriso, 0);
 if(ret<=0)
   goto ex;
 if(!burn_drive_wrote_well(drive)) {
   Xorriso_process_msg_queues(xorriso,0);
   sprintf(xorriso->info_text,
           "libburn indicates failure with writing."); 
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   ret= 0; goto ex;
 }

 if(flag & 1) {
   ret= Xorriso_update_iso_lba0(xorriso, nwa, isosize, head_buffer, NULL,
                                flag & 2);
   if(ret <= 0)
     goto ex;
 }
 sprintf(xorriso->info_text, "Writing to ");
 Text_shellsafe(xorriso->outdev, xorriso->info_text, 1);
 strcat(xorriso->info_text, " completed successfully.\n\n");
 Xorriso_info(xorriso, 0);
 ret= 1;
ex:;
 Xorriso_process_msg_queues(xorriso,0);
 if(disc!=NULL)
   burn_disc_free(disc);
 if(session != NULL)
   burn_session_free(session);
 if(track != NULL)
   burn_track_free(track);
 if(burn_options != NULL)
   burn_write_opts_free(burn_options);
 if(xorriso->pacifier_fifo!=NULL)
   burn_source_free(xorriso->pacifier_fifo);
 xorriso->pacifier_fifo= NULL;
 xorriso->run_state= 0; /* Indicate that burning has ended */
 Xorriso_free_meM(reasons);
 Xorriso_free_meM(profile_name);
 Xorriso_free_meM(head_buffer);
 return(ret);
}


int Xorriso_relax_compliance(struct XorrisO *xorriso, char *mode,
                                    int flag)
{
 char *npt, *cpt;
 int l, was, value, ret;
 struct isoburn_imgen_opts *opts= NULL;
 char *msg= NULL;
 off_t limit;

 was= xorriso->relax_compliance;
 npt= cpt= mode;
 for(; npt!=NULL; cpt= npt+1) {
   npt= strchr(cpt,':');
   if(npt==NULL)
     l= strlen(cpt);
   else
     l= npt-cpt;
   if(l == 0)
 continue;
   if((l == 6 && strncmp(cpt, "strict", l) == 0) ||
      (l == 5 && strncmp(cpt, "clear", l) == 0)) {
     xorriso->relax_compliance= 0;
   } else if(l == 7 && strncmp(cpt, "default", l) == 0) {
     xorriso->relax_compliance= Xorriso_relax_compliance_defaulT;

   } else if((l == 18 && strncmp(cpt, "untranslated_names", l) == 0) ||
             (l == 21 && strncmp(cpt, "untranslated_names_on", l) == 0) ) {
     xorriso->untranslated_name_len = -1;
   } else if((l == 22 && strncmp(cpt, "untranslated_names_off", l) == 0)) {
     xorriso->untranslated_name_len = 0;
   } else if((l >= 22 && strncmp(cpt, "untranslated_name_len=", 22) == 0)) {
     value= -1;
     sscanf(cpt + 22, "%d", &value);
     /* Let libisoburn check the value */
     ret= isoburn_igopt_new(&opts, 0);
     if(ret != 1)
       return(-1);
     ret= isoburn_igopt_set_untranslated_name_len(opts, value);
     isoburn_igopt_destroy(&opts, 0);
     if(ret <= 0) { /* Not a tasty value */
       xorriso->relax_compliance= was;
       return(0);
     }
     xorriso->untranslated_name_len = value;

   } else if((l == 16 && strncmp(cpt, "allow_dir_id_ext", l) == 0) ||
             (l == 19 && strncmp(cpt, "allow_dir_id_ext_on", l) == 0) ) {
     xorriso->relax_compliance|= isoburn_igopt_allow_dir_id_ext;
     xorriso->allow_dir_id_ext_dflt= 0;
   } else if((l == 20 && strncmp(cpt, "allow_dir_id_ext_off", l) == 0)) {
     xorriso->relax_compliance&= ~isoburn_igopt_allow_dir_id_ext;
     xorriso->allow_dir_id_ext_dflt= 0;

   } else if((l == 12 && strncmp(cpt, "omit_version", l) == 0) ||
             (l == 15 && strncmp(cpt, "omit_version_on", l) == 0) ) {
     xorriso->relax_compliance|= isoburn_igopt_omit_version_numbers;
   } else if((l == 16 && strncmp(cpt, "omit_version_off", l) == 0)) {
     xorriso->relax_compliance&= ~isoburn_igopt_omit_version_numbers;

   } else if((l == 16 && strncmp(cpt, "only_iso_version", l) == 0) ||
             (l == 19 && strncmp(cpt, "only_iso_version_on", l) == 0) ) {
     xorriso->relax_compliance|= isoburn_igopt_only_iso_versions;
   } else if((l == 20 && strncmp(cpt, "only_iso_version_off", l) == 0)) {
     xorriso->relax_compliance&= ~isoburn_igopt_only_iso_versions;

   } else if((l == 10 && strncmp(cpt, "deep_paths", l) == 0) ||
             (l == 13 && strncmp(cpt, "deep_paths_on", l) == 0)) {
     xorriso->relax_compliance|= isoburn_igopt_allow_deep_paths;
   } else if(l == 14 && strncmp(cpt, "deep_paths_off", l) == 0) {
     xorriso->relax_compliance&= ~isoburn_igopt_allow_deep_paths;

   } else if((l == 10 && strncmp(cpt, "long_paths", l) == 0) ||
             (l == 13 && strncmp(cpt, "long_paths_on", l) == 0) ) {
     xorriso->relax_compliance|= isoburn_igopt_allow_longer_paths;
   } else if(l == 14 && strncmp(cpt, "long_paths_off", l) == 0) {
     xorriso->relax_compliance&= ~isoburn_igopt_allow_longer_paths;

   } else if((l == 10 && strncmp(cpt, "long_names", l) == 0) ||
             (l == 13 && strncmp(cpt, "long_names_on", l) == 0)) {
     xorriso->relax_compliance|= isoburn_igopt_max_37_char_filenames;
   } else if(l == 14 && strncmp(cpt, "long_names_off", l) == 0) {
     xorriso->relax_compliance&= ~isoburn_igopt_max_37_char_filenames;

   } else if((l == 13 && strncmp(cpt, "no_force_dots", l) == 0) ||
             (l == 16 && strncmp(cpt, "no_force_dots_on", l) == 0)) {
     xorriso->relax_compliance|= isoburn_igopt_no_force_dots;
   } else if(l == 17 && strncmp(cpt, "no_force_dots_off", l) == 0) {
     xorriso->relax_compliance&= ~isoburn_igopt_no_force_dots;

   } else if((l == 15 && strncmp(cpt, "no_j_force_dots", l) == 0) ||
             (l == 18 && strncmp(cpt, "no_j_force_dots_on", l) == 0)) {
     xorriso->relax_compliance|= isoburn_igopt_no_j_force_dots;
   } else if(l == 19 && strncmp(cpt, "no_j_force_dots_off", l) == 0) {
     xorriso->relax_compliance&= ~isoburn_igopt_no_j_force_dots;

   } else if((l ==  9 && strncmp(cpt, "lowercase", l) == 0) ||
             (l == 12 && strncmp(cpt, "lowercase_on", l) == 0)) {
     xorriso->relax_compliance|= isoburn_igopt_allow_lowercase;
   } else if(l == 13 && strncmp(cpt, "lowercase_off", l) == 0) {
     xorriso->relax_compliance&= ~isoburn_igopt_allow_lowercase;

   } else if((l == 10 && strncmp(cpt, "full_ascii", l) == 0) ||
             (l == 13 && strncmp(cpt, "full_ascii_on", l) == 0)) {
     xorriso->relax_compliance|= isoburn_igopt_allow_full_ascii;
   } else if(l == 14 && strncmp(cpt, "full_ascii_off", l) == 0) {
     xorriso->relax_compliance&= ~isoburn_igopt_allow_full_ascii;

   } else if((l == 10 && strncmp(cpt, "7bit_ascii", l) == 0) ||
             (l == 13 && strncmp(cpt, "7bit_ascii_on", l) == 0)) {
     xorriso->relax_compliance|= isoburn_igopt_allow_7bit_ascii;
   } else if(l == 14 && strncmp(cpt, "7bit_ascii_off", l) == 0) {
     xorriso->relax_compliance&= ~isoburn_igopt_allow_7bit_ascii;

   } else if((l == 17 && strncmp(cpt, "joliet_long_paths", l) == 0) ||
             (l == 20 && strncmp(cpt, "joliet_long_paths_on", l) == 0)) {
     xorriso->relax_compliance|= isoburn_igopt_joliet_longer_paths;
   } else if(l == 21 && strncmp(cpt, "joliet_long_paths_off", l) == 0) {
     xorriso->relax_compliance&= ~isoburn_igopt_joliet_longer_paths;

   } else if((l == 17 && strncmp(cpt, "joliet_long_names", l) == 0) ||
             (l == 20 && strncmp(cpt, "joliet_long_names_on", l) == 0)) {
     xorriso->relax_compliance|= isoburn_igopt_joliet_long_names;
   } else if(l == 21 && strncmp(cpt, "joliet_long_names_off", l) == 0) {
     xorriso->relax_compliance&= ~isoburn_igopt_joliet_long_names;

   } else if((l == 10 && strncmp(cpt, "always_gmt", l) == 0) ||
             (l == 13 && strncmp(cpt, "always_gmt_on", l) == 0)) {
     xorriso->relax_compliance|= isoburn_igopt_always_gmt;
   } else if(l == 14 && strncmp(cpt, "always_gmt_off", l) == 0) {
     xorriso->relax_compliance&= ~isoburn_igopt_always_gmt;

   } else if((l ==  9 && strncmp(cpt, "rec_mtime", l) == 0) ||
             (l == 12 && strncmp(cpt, "rec_mtime_on", l) == 0)) {
     xorriso->relax_compliance|= (isoburn_igopt_dir_rec_mtime |
                                  isoburn_igopt_joliet_rec_mtime |
                                  isoburn_igopt_iso1999_rec_mtime);
   } else if(l == 13 && strncmp(cpt, "rec_mtime_off", l) == 0) {
     xorriso->relax_compliance&= ~(isoburn_igopt_dir_rec_mtime | 
                                  isoburn_igopt_joliet_rec_mtime |
                                  isoburn_igopt_iso1999_rec_mtime);

   } else if((l ==  6 && strncmp(cpt, "old_rr", l) == 0) ||
             (l ==  9 && strncmp(cpt, "old_rr_on", l) == 0) ||
             (l == 10 && strncmp(cpt, "new_rr_off", l) == 0)) {
     xorriso->relax_compliance|=
                isoburn_igopt_rrip_version_1_10 | isoburn_igopt_aaip_susp_1_10;
   } else if((l == 10 && strncmp(cpt, "old_rr_off", l) == 0) ||
             (l ==  9 && strncmp(cpt, "new_rr_on", l) == 0) ||
             (l ==  6 && strncmp(cpt, "new_rr", l) == 0)) {
     xorriso->relax_compliance&=
             ~(isoburn_igopt_rrip_version_1_10 | isoburn_igopt_aaip_susp_1_10);

   } else if((l == 14 && strncmp(cpt, "aaip_susp_1_10", l) == 0) ||
             (l == 17 && strncmp(cpt, "aaip_susp_1_10_on", l) == 0) ||
             (l == 18 && strncmp(cpt, "aaip_susp_1_12_off", l) == 0)) {
     xorriso->relax_compliance|= isoburn_igopt_aaip_susp_1_10;
   } else if((l == 18 && strncmp(cpt, "aaip_susp_1_10_off", l) == 0) ||
             (l == 17 && strncmp(cpt, "aaip_susp_1_12_on", l) == 0) ||
             (l == 14 && strncmp(cpt, "aaip_susp_1_12", l) == 0)) {
     xorriso->relax_compliance&= ~isoburn_igopt_aaip_susp_1_10;

   } else if((l == 11 && strncmp(cpt, "no_emul_toc", l) == 0) ||
             (l == 14 && strncmp(cpt, "no_emul_toc_on", l) == 0)) {
     xorriso->no_emul_toc|= 1;
   } else if((l == 15 && strncmp(cpt, "no_emul_toc_off", l) == 0) ||
             (l ==  8 && strncmp(cpt, "emul_toc", l) == 0)) {
     xorriso->no_emul_toc&= ~1;

   } else if((l == 13 && strncmp(cpt, "iso_9660_1999", l) == 0) ||
             (l == 16 && strncmp(cpt, "iso_9660_1999_on", l) == 0)) {
     xorriso->do_iso1999= 1;
   } else if(l == 17 && strncmp(cpt, "iso_9660_1999_off", l) == 0) {
     xorriso->do_iso1999= 0;

   } else if((l >= 15 && strncmp(cpt, "iso_9660_level=", 15) == 0)) {
     value= 0;
     sscanf(cpt + 15, "%d", &value);
     if(value == 1 || value == 2) {
       limit= ((off_t) 4) * ((off_t) 1024*1024*1024) - ((off_t) 1);
       xorriso->iso_level= value;
       xorriso->iso_level_is_default= 0;
       if(xorriso->file_size_limit > limit)
         xorriso->file_size_limit= limit;
     } else if(value == 3) {
       xorriso->iso_level= value;
       xorriso->iso_level_is_default= 0;
       if(xorriso->file_size_limit < Xorriso_default_file_size_limiT)
         xorriso->file_size_limit= Xorriso_default_file_size_limiT;
     } else {
       Xorriso_alloc_meM(msg, char, 160); 
       sprintf(msg,
             "-compliance iso_9660_level=%d : Only 1, 2, or 3 are permissible",
             value);
       Xorriso_msgs_submit(xorriso, 0, msg, 0, "FAILURE", 0);
       Xorriso_free_meM(msg);
       msg= NULL;
       xorriso->relax_compliance= was;
       return(0);
     }

   } else if((l ==  8 && strncmp(cpt, "iso_9660", l) == 0) ||
             (l == 11 && strncmp(cpt, "iso_9660_on", l) == 0)) {
     /* may have a meaning in future */;
   } else if(l == 12 && strncmp(cpt, "iso_9660_off", l) == 0) {
     /* may have a meaning in future */;
     Xorriso_msgs_submit(xorriso, 0,
            "-compliance -iso_9660_off : Cannot do anything else but ISO 9660",
            0, "FAILURE", 0);
     xorriso->relax_compliance= was;
     return(0);

   } else if((l ==  9 && strncmp(cpt, "old_empty", l) == 0) ||
             (l == 12 && strncmp(cpt, "old_empty_on", l) == 0)) {
     xorriso->do_old_empty= 1;
   } else if(l == 13 && strncmp(cpt, "old_empty_off", l) == 0) {
     xorriso->do_old_empty= 0;

   } else {
     if(l<SfileadrL)
       sprintf(xorriso->info_text, "-compliance: unknown rule '%s'",
               cpt);
     else
       sprintf(xorriso->info_text,
               "-compliance: oversized rule parameter (%d)", l);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     xorriso->relax_compliance= was;
     return(0);
   }
 }
 return(1);
ex:;
 Xorriso_free_meM(msg);
 return(ret);
}


/* @return 1=ok  2=ok, is default setting */
int Xorriso_get_relax_text(struct XorrisO *xorriso, char mode[1024],
                           int flag)
{
 int r;

 r= xorriso->relax_compliance;
 if(r == 0) {
   strcpy(mode, "strict");
   return(1);
 }
 strcpy(mode, "clear");
 sprintf(mode + strlen(mode), ":iso_9660_level=%d",  xorriso->iso_level);
 if(r & isoburn_igopt_allow_dir_id_ext)
   strcat(mode, ":allow_dir_id_ext");
 if(r & isoburn_igopt_omit_version_numbers)
   strcat(mode, ":omit_version");
 if(r & isoburn_igopt_only_iso_versions)
   strcat(mode, ":only_iso_version");
 if(r & isoburn_igopt_allow_deep_paths)
   strcat(mode, ":deep_paths");
 if(r & isoburn_igopt_allow_longer_paths)
   strcat(mode, ":long_paths");
 if(r & isoburn_igopt_max_37_char_filenames)
   strcat(mode, ":long_names");
 if(r & isoburn_igopt_no_force_dots)
   strcat(mode, ":no_force_dots");
 if(r & isoburn_igopt_no_j_force_dots)
   strcat(mode, ":no_j_force_dots");
 if(r & isoburn_igopt_allow_lowercase)
   strcat(mode, ":lowercase");
 if(r & isoburn_igopt_allow_full_ascii)
   strcat(mode, ":full_ascii");
 else if(r & isoburn_igopt_allow_7bit_ascii)
   strcat(mode, ":7bit_ascii");
 if(r & isoburn_igopt_joliet_longer_paths)
   strcat(mode, ":joliet_long_paths");
 if(r & isoburn_igopt_joliet_long_names)
   strcat(mode, ":joliet_long_names");
 if(r & isoburn_igopt_always_gmt)
   strcat(mode, ":always_gmt");
 if(r & isoburn_igopt_dir_rec_mtime)
   strcat(mode, ":rec_mtime");
 if(r & isoburn_igopt_rrip_version_1_10) {
   strcat(mode, ":old_rr");
   if(!(r & isoburn_igopt_aaip_susp_1_10))
     strcat(mode, ":aaip_susp_1_10_off");
 } else {
   strcat(mode, ":new_rr");
   if(r & isoburn_igopt_aaip_susp_1_10) 
     strcat(mode, ":aaip_susp_1_10");
 }
 if(xorriso->no_emul_toc & 1)
   strcat(mode, ":no_emul_toc");
 if(xorriso->untranslated_name_len != 0)
   sprintf(mode + strlen(mode), ":untranslated_name_len=%d",
           xorriso->untranslated_name_len);
 if(xorriso->do_iso1999)
   sprintf(mode + strlen(mode), ":iso_9660_1999");
 if(xorriso->do_old_empty)
   sprintf(mode + strlen(mode), ":old_empty");
 return(1 +
       (r == Xorriso_relax_compliance_defaulT && !(xorriso->no_emul_toc & 1)
        && xorriso->untranslated_name_len == 0 && !xorriso->do_iso1999 &&
        xorriso->iso_level == 3));
}


/* @param flag bit0= operating on newly attached boot image
*/
int Xorriso_set_isolinux_options(struct XorrisO *xorriso,
                                 IsoImage *image, int flag)
{
 int make_isohybrid_mbr= 0, ret, patch_table= 0, num_boots, i;
 ElToritoBootImage *bootimg, **boots = NULL;
 IsoFile *bootimg_node, **bootnodes = NULL;

 ret= iso_image_get_boot_image(image, &bootimg, &bootimg_node, NULL);
 Xorriso_process_msg_queues(xorriso,0);
 if(ret != 1) {
   sprintf(xorriso->info_text, "Programming error: No boot image available in Xorriso_set_isolinux_options()");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   ret= -1; goto ex;
 }
 ret= iso_image_get_all_boot_imgs(image, &num_boots, &boots, &bootnodes, 0);
 Xorriso_process_msg_queues(xorriso,0);
 if(ret != 1) {
   Xorriso_report_iso_error(xorriso, "", ret, "Cannot inquire boot images", 0,
                            "FATAL", 1);
   ret= -1; goto ex;
 }

 /* bit0 : 1=boot-info-table , bit2-7 : 1=EFI , 2=HFS+ , bit8 : 1=APM */
 patch_table = xorriso->patch_isolinux_image & 0x3fd;
 if((flag & 1) && num_boots > 1) {
   ret= el_torito_set_isolinux_options(boots[num_boots - 1], patch_table, 0);
   ret= (ret == 1); goto ex;
 }

 /* Handle patching of first attached boot image or of imported boot images 
 */
 for(i= 0; i < num_boots; i++) {
   patch_table = xorriso->patch_isolinux_image & 0x3fd;
   if(patch_table && !(flag & 1)) {
     if(!el_torito_seems_boot_info_table(boots[i], 0))
       patch_table&= ~1;
     else if((xorriso->patch_isolinux_image & 2) &&
             el_torito_get_boot_platform_id(boots[i]) == 0xef)
       patch_table&= ~1;
   }
   if(i > 0 || xorriso->boot_image_isohybrid == 0) {
     ret= el_torito_set_isolinux_options(boots[i], patch_table, 0);
     if(ret != 1)
       {ret= 0; goto ex;}
 continue;
   }
   if(xorriso->boot_image_isohybrid == 3) {
     make_isohybrid_mbr= 1;
   } else {
     ret= Xorriso_is_isohybrid(xorriso, bootimg_node, 0);
     if(ret < 0)
       {ret= 0; goto ex;}
     if(ret > 0)
       make_isohybrid_mbr= 1;
   }

   if(xorriso->boot_image_isohybrid == 2 && !make_isohybrid_mbr) {
     sprintf(xorriso->info_text,
          "Isohybrid signature is demanded but not found in boot image file.");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
   if(make_isohybrid_mbr) {
     sprintf(xorriso->info_text, "Will write isohybrid MBR.");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   }
   ret= el_torito_set_isolinux_options(bootimg,
                                    patch_table | (make_isohybrid_mbr << 1),0);
   if(ret != 1)
     {ret= 0; goto ex;}
 }
ex:
 Xorriso_process_msg_queues(xorriso,0);
 if(boots != NULL)
   free(boots);
 if(bootnodes != NULL)
   free(bootnodes);
 return(ret);
}


/*
   @param flag bit0= obtain iso_lba from indev
               bit1= head_buffer already contains a valid head
               bit2= issue message about success
               bit3= check whether source blocks are banned by in_sector_map
*/
int Xorriso_update_iso_lba0(struct XorrisO *xorriso, int iso_lba, int isosize,
                            char *head_buffer, struct CheckmediajoB *job,
                            int flag)
{
 int ret, full_size, i;
 char *headpt;
 struct burn_drive_info *dinfo;
 struct burn_drive *drive = NULL;
 off_t seek_ret, to_write;
 int tag_type;
 uint32_t pos, range_start, range_size, next_tag;
 char md5[16];

 ret= Xorriso_may_burn(xorriso, 0);
 if(ret <= 0)
   return(0);
 if(flag & 1) {
   ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                 "on attempt to learn current session lba", 1);
   if(ret<=0)
     return(0);
   ret= isoburn_disc_get_msc1(drive, &iso_lba);
   if(ret<=0)
     return(0);
   drive= NULL; /* indev will not be used furtherly */
 }
 if(job == NULL) { 
   ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                  "on attempt to update at lba 0 to 31", 2);
   if(ret<=0)
     return(0);
 }
 if(iso_lba < 32)
   return(2);

 if(!(flag & 2)) {
   /* head_buffer was not filled yet. Read it from output media. */
   if(drive != NULL)
     if(burn_drive_get_drive_role(drive) == 5) /* write-only */
       return(2);
   if(job != NULL && job->data_to_fd >= 0) {
     if((flag & 8) && job->sector_map != NULL) {
       ret= Sectorbitmap_bytes_are_set(job->sector_map,
                     ((off_t) iso_lba) * (off_t) 2048,
                     ((off_t) (iso_lba + 32)) * ((off_t) 2048) - (off_t) 1, 0);
       if(ret <= 0) {
         sprintf(xorriso->info_text,
           "ISO image head at lba %d is marked as invalid blocks in file copy",
           iso_lba); 
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE",
                             0);
          return(0);
       }
     }
     seek_ret= lseek(job->data_to_fd, ((off_t) 2048) * (off_t) iso_lba,
                     SEEK_SET);
     if(seek_ret == -1)
       ret= 0;
     else
       ret= read(job->data_to_fd, head_buffer, 64 * 1024); 
     if(ret < 64 * 1024) {
       Xorriso_process_msg_queues(xorriso,0);
       sprintf(xorriso->info_text,
               "Cannot read ISO image head from file copy"); 
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE",0);
       return(0);
     }
     ret= isoburn_read_iso_head(NULL, 0, &isosize, head_buffer, 1 << 13);
     if(ret<=0) {
       Xorriso_process_msg_queues(xorriso,0);
       sprintf(xorriso->info_text,
               "Alleged session start does not look like ISO 9660.");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE",0);
       return(0);
     }
   } else {
     ret= 0;
     if(drive != NULL)
       ret= isoburn_read_iso_head(drive, iso_lba, &isosize, head_buffer, 2);
     if(ret<=0) {
       Xorriso_process_msg_queues(xorriso,0);
       sprintf(xorriso->info_text,
               "Cannot read freshly written ISO image head"); 
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       return(0);
     }
   }
 }
 /* patch ISO header */
 full_size= iso_lba + isosize;
 headpt= head_buffer + 32*1024;
 for(i=0;i<4;i++)
   headpt[87-i]= headpt[80+i]= (full_size >> (8*i)) & 0xff;


 if(job != NULL) {
   /* This is a check_media superblock relocation:
      Invalidate eventual libisofs checksum tags.
      Write only up to PVD end plus eventual invalidated tag.
   */
   /* Look for volume descriptor end */
   for(i= 16; i < 32; i++)
     if(((unsigned char *) head_buffer)[i * 2048] == 0xff &&
        strncmp(head_buffer + i * 2048 + 1, "CD001", 5) == 0)
   break;
   /* Check whether the next one is a libisofs checksum tag */
   if(i < 32) {
     i++;
     ret= iso_util_decode_md5_tag(head_buffer + i * 2048, &tag_type, &pos,
                                &range_start, &range_size, &next_tag, md5, 0);
     if(ret != 0) /* corrupted or not: invalidate */
       memset(head_buffer + i * 2048, 0, 8);
   }
   to_write= 2048 * (i + 1);

   seek_ret= lseek(job->data_to_fd, (off_t) 0, SEEK_SET);
   if(seek_ret == -1)
     ret= 0;
   else
     ret= write(job->data_to_fd, head_buffer, to_write); 
   if(ret < to_write) {
     Xorriso_process_msg_queues(xorriso,0);
     sprintf(xorriso->info_text,
             "Cannot write ISO image head to file copy"); 
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE",0);
     return(0);
   }
 } else {
   /* This is a regular superblock relocation. Write full 64 kB. */
   to_write= 64 * 1024;
   ret= burn_random_access_write(drive, (off_t) 0, head_buffer, to_write, 1);
   if(ret<=0) {
     Xorriso_process_msg_queues(xorriso,0);
     sprintf(xorriso->info_text,
             "Cannot write new ISO image head to LBA 0"); 
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
 }
 if(flag & 4) {
   sprintf(xorriso->info_text,
           "Overwrote LBA 0 to 31 by 64 KiB from LBA %d", iso_lba); 
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
 }
 return(1);
}


int Xorriso_set_system_area_path(struct XorrisO *xorriso, char *path, int flag)
{
 int ret;
 char *eff_src= NULL;

 if(path[0] == 0) {
   xorriso->system_area_disk_path[0]= 0;
   {ret= 1; goto ex;}
 }
 Xorriso_alloc_meM(eff_src, char, SfileadrL);
 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdx, path, eff_src, 2|4|16);
 if(ret < 0)
   goto ex;
 if(ret == 0) {
   sprintf(xorriso->info_text,
           "Given path does not exist on disk: -boot_image system_area=");
   Text_shellsafe(eff_src, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
 }
 if(ret == 2) {
   sprintf(xorriso->info_text,
           "Given path leads to a directory: -boot_image system_area=");
   Text_shellsafe(eff_src, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 ret= Sfile_str(xorriso->system_area_disk_path, eff_src, 0);
 if(ret <= 0)
   {ret= -1; goto ex;}
 ret= 1;
ex:
 Xorriso_free_meM(eff_src);
 return(ret);
}


/* @param flag bit0=force burn_disc_close_damaged()
*/
int Xorriso_close_damaged(struct XorrisO *xorriso, int flag)
{
 int ret;
 struct burn_drive_info *dinfo;
 struct burn_drive *drive;
 struct burn_write_opts *burn_options= NULL;

 if(Xorriso_change_is_pending(xorriso, 0)) {
   sprintf(xorriso->info_text,
           "Image changes pending. -commit or -rollback first");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   ret= 0; goto ex;
 }
 ret= Xorriso_get_drive_handles(xorriso, &dinfo, &drive,
                                "on attempt to closed damaged session", 2);
 if(ret<=0)
   goto ex;
 ret= Xorriso_make_write_options(xorriso, drive, &burn_options, 0);
 if(ret <= 0)
   goto ex;
 ret= burn_disc_close_damaged(burn_options, flag & 1);
 Xorriso_process_msg_queues(xorriso, 0);
 Xorriso_option_dev(xorriso, "", 3 | 4); /* Give up drives */
 if(ret <= 0)
   goto ex;

 ret= 1;
ex:;
 Xorriso_process_msg_queues(xorriso, 0);
 if(burn_options != NULL)
   burn_write_opts_free(burn_options);
 return(ret); 
}

