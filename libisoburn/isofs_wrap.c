
/*
  cc -g -c isofs_wrap.c
*/

/*
  libisofs related functions of libisoburn.

  Copyright 2007 - 2011  Vreixo Formoso Lopes <metalpain2002@yahoo.es>
                         Thomas Schmitt <scdbackup@gmx.net>
  Provided under GPL version 2 or later.
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef Xorriso_standalonE

#include <libburn/libburn.h>

#include <libisofs/libisofs.h>

#else /* ! Xorriso_standalonE */

#include "../libisofs/libisofs.h"
#include "../libburn/libburn.h"

#endif /* Xorriso_standalonE */

#include "libisoburn.h"
#include "isoburn.h"

#define BP(a,b) [(b) - (a) + 1]

struct ecma119_pri_vol_desc
{
	uint8_t vol_desc_type		BP(1, 1);
	uint8_t std_identifier		BP(2, 6);
	uint8_t vol_desc_version	BP(7, 7);
	uint8_t unused1			BP(8, 8);
	uint8_t system_id		BP(9, 40);
	uint8_t volume_id		BP(41, 72);
	uint8_t unused2			BP(73, 80);
	uint8_t vol_space_size		BP(81, 88);
	uint8_t unused3			BP(89, 120);
	uint8_t vol_set_size		BP(121, 124);
	uint8_t vol_seq_number		BP(125, 128);
	uint8_t block_size		BP(129, 132);
	uint8_t path_table_size		BP(133, 140);
	uint8_t l_path_table_pos	BP(141, 144);
	uint8_t opt_l_path_table_pos	BP(145, 148);
	uint8_t m_path_table_pos	BP(149, 152);
	uint8_t opt_m_path_table_pos	BP(153, 156);
	uint8_t root_dir_record		BP(157, 190);
	uint8_t	vol_set_id		BP(191, 318);
	uint8_t publisher_id		BP(319, 446);
	uint8_t data_prep_id		BP(447, 574);
	uint8_t application_id		BP(575, 702);
	uint8_t copyright_file_id	BP(703, 739);
	uint8_t abstract_file_id	BP(740, 776);
	uint8_t bibliographic_file_id	BP(777, 813);
	uint8_t vol_creation_time	BP(814, 830);
	uint8_t vol_modification_time	BP(831, 847);
	uint8_t vol_expiration_time	BP(848, 864);
	uint8_t vol_effective_time	BP(865, 881);
	uint8_t file_structure_version	BP(882, 882);
	uint8_t reserved1		BP(883, 883);
	uint8_t app_use			BP(884, 1395);
	uint8_t reserved2		BP(1396, 2048);
};

static
uint32_t iso_read_lsb(const uint8_t *buf, int bytes)
{
	int i;
	uint32_t ret = 0;

	for (i=0; i<bytes; i++) {
		ret += ((uint32_t) buf[i]) << (i*8);
	}
	return ret;
}


/* API function. See libisoburn.h
*/
IsoImage *isoburn_get_attached_image(struct burn_drive *d)
{
 int ret;
 struct isoburn *o= NULL;
 ret = isoburn_find_emulator(&o, d, 0);
 if (ret < 0)
   return NULL;
  
 if (o == NULL) {
   return NULL;
 }
 iso_image_ref(o->image);
 return o->image;
}


/* API */
int isoburn_get_attached_start_lba(struct burn_drive *d)
{
 int ret;
 struct isoburn *o= NULL;

 ret = isoburn_find_emulator(&o, d, 0);
 if (ret < 0 || o == NULL)
   return -1;
 if(o->image == NULL)
   return -1;
 return o->image_start_lba;
}


static void isoburn_idle_free_function(void *ignored)
{
 return;
}


int isoburn_root_defaults(IsoImage *image, int flag)
{
 IsoNode *root_node;
 mode_t root_mode= 0755;

 root_node= (IsoNode *) iso_image_get_root(image);
 iso_node_set_permissions(root_node, root_mode);
 return(1);
}


/* API function. See libisoburn.h
*/
int isoburn_read_image(struct burn_drive *d,
                       struct isoburn_read_opts *read_opts, 
                       IsoImage **image)
{
 int ret, int_num, dummy;
 IsoReadOpts *ropts= NULL;
 IsoReadImageFeatures *features= NULL;
 uint32_t ms_block;
 char *msg= NULL;
 enum burn_disc_status status= BURN_DISC_BLANK;
 IsoDataSource *ds= NULL;
 struct isoburn *o= NULL;

 msg= calloc(1, 160);

 if(d != NULL) {
   ret = isoburn_find_emulator(&o, d, 0);
   if (ret < 0 || o == NULL)
     {ret= 0; goto ex;}
   status = isoburn_disc_get_status(d);
   o->image_start_lba= -1;
 }
 if(read_opts==NULL) {
   isoburn_msgs_submit(o, 0x00060000,
                       "Program error: isoburn_read_image: read_opts==NULL",
                       0, "FATAL", 0);
   {ret= -1; goto ex;}
 }
 if (d == NULL || status == BURN_DISC_BLANK || read_opts->pretend_blank) {
create_blank_image:;
   /*
    * Blank disc, we create a new image without files.
    */
   
   if (d == NULL) {
     /* New empty image without relation to a drive */
     if (image==NULL) {
       isoburn_msgs_submit(o, 0x00060000,
                           "Program error: isoburn_read_image: image==NULL",
                           0, "FATAL", 0);
       {ret= -1; goto ex;}
     }
     /* create a new image */
     ret = iso_image_new("ISOIMAGE", image);
     if (ret < 0) {
       isoburn_report_iso_error(ret, "Cannot create image", 0, "FATAL", 0);
       goto ex;
     }
     iso_image_set_ignore_aclea(*image,
                         (!!(read_opts->noacl)) | ((!!read_opts->noea) << 1) );
   } else {
     /* Blank new image for the drive */
     iso_image_unref(o->image);
     ret = iso_image_new("ISOIMAGE", &o->image);
     if (ret < 0) {
       isoburn_report_iso_error(ret, "Cannot create image", 0, "FATAL", 0);
       goto ex;
     }
     if (image != NULL) {
       *image = o->image;
       iso_image_ref(*image); /*protects object from premature free*/
     }
     iso_image_set_ignore_aclea(o->image,
                         (!!(read_opts->noacl)) | ((!!read_opts->noea) << 1) );

     ret= isoburn_root_defaults(o->image, 0);
     if(ret <= 0)
       goto ex;
   }
   {ret= 1; goto ex;}
 }
 
 if (status != BURN_DISC_APPENDABLE && status != BURN_DISC_FULL) {
   isoburn_msgs_submit(o, 0x00060000,
                    "Program error: isoburn_read_image: incorrect disc status",
                    0, "FATAL", 0);
   {ret= -4; goto ex;}
 }
 
 ret = isoburn_disc_get_msc1(d, &int_num);
 if (ret <= 0)
   {ret= -2; goto ex;}
 ms_block= int_num;
 if (o != NULL)
   o->image_start_lba= ms_block;
 ret = isoburn_read_iso_head(d, int_num, &dummy, NULL, 0);
 if (ret <= 0) {
   sprintf(msg, "No ISO 9660 image at LBA %d. Creating blank image.", int_num);
   isoburn_msgs_submit(o, 0x00060000, msg, 0, "WARNING", 0);
   goto create_blank_image;
 }

 if(read_opts->displacement != 0 && abs(read_opts->displacement_sign) == 1) {
   /* Apply reverse displacement to session start */
   if(read_opts->displacement_sign == -1) {
     if(ms_block+ read_opts->displacement < ms_block) {
displacement_rollover:;
       sprintf(msg, "Displacement offset leads outside 32 bit range.");
       isoburn_msgs_submit(o, 0x00060000, msg, 0, "FAILURE", 0);
       {ret= 0; goto ex;}
     }
     ms_block+= read_opts->displacement;
   } else {
     if(ms_block < read_opts->displacement)
       goto displacement_rollover;
     ms_block-= read_opts->displacement;
   }
 }


 /* create the data source */
 ret = iso_read_opts_new(&ropts, 0);
 if (ret < 0) {
   isoburn_report_iso_error(ret, "Cannot create write opts", 0, "FATAL", 0);
   goto ex;
 }

 /* Important: do not return until iso_read_opts_free() */

 iso_read_opts_set_start_block(ropts, ms_block);
 iso_read_opts_set_no_rockridge(ropts, read_opts->norock);
 iso_read_opts_set_no_aaip(ropts, read_opts->noaaip);
 if(read_opts->nomd5 == 2)
   int_num= 2;
 else if(read_opts->nomd5 == 1)
   int_num= 1;
 else
   int_num= 0;
 iso_read_opts_set_no_md5(ropts, int_num);
 iso_read_opts_set_new_inos(ropts, read_opts->noino);

 iso_read_opts_set_no_joliet(ropts, read_opts->nojoliet);
 iso_read_opts_set_no_iso1999(ropts, read_opts->noiso1999);
 iso_read_opts_set_preferjoliet(ropts, read_opts->preferjoliet);
 iso_read_opts_set_default_permissions(ropts,
                                       read_opts->mode, read_opts->dirmode);
 iso_read_opts_set_default_uid(ropts, read_opts->uid);
 iso_read_opts_set_default_gid(ropts, read_opts->gid);
 iso_read_opts_set_input_charset(ropts, read_opts->input_charset);
 iso_read_opts_auto_input_charset(ropts, read_opts->auto_input_charset);
 iso_read_opts_load_system_area(ropts, 1);

 ds = isoburn_data_source_new(d, read_opts->displacement,
                         read_opts->displacement_sign,
                         read_opts->cache_tiles, read_opts->cache_tile_blocks);
 if (ds == NULL) {
   isoburn_report_iso_error(ret, "Cannot create IsoDataSource object", 0,
                            "FATAL", 0);
   ret= -1; goto ex;
 }
 if(o->iso_data_source!=NULL)
   iso_data_source_unref(o->iso_data_source);
 o->iso_data_source= ds;
 iso_image_attach_data(o->image, o->read_pacifier_handle,
                       isoburn_idle_free_function);
 if(o->read_pacifier_handle==NULL)
   iso_tree_set_report_callback(o->image, NULL);
 else
   iso_tree_set_report_callback(o->image, o->read_pacifier);
 ret = iso_image_import(o->image, ds, ropts, &features);
 iso_tree_set_report_callback(o->image, NULL);
 iso_read_opts_free(ropts);
 ropts= NULL;

 if (ret < 0) {
   isoburn_report_iso_error(ret, "Cannot import image", 0, "FAILURE", 0);
   goto ex;
 }
 /* Important: do not return until free(features) */
 if (image!=NULL) {
   *image = o->image;
   iso_image_ref(*image); /*protects object from premature free*/
 }
 read_opts->hasRR = iso_read_image_features_has_rockridge(features);
 read_opts->hasJoliet = iso_read_image_features_has_joliet(features);
 read_opts->hasIso1999 = iso_read_image_features_has_iso1999(features);
 read_opts->hasElTorito = iso_read_image_features_has_eltorito(features);
 read_opts->size = iso_read_image_features_get_size(features);
 ret= 1;
ex:;
 if(msg != NULL)
   free(msg);
 if(ropts != NULL)
   iso_read_opts_free(ropts);
 if(features != NULL)
    iso_read_image_features_destroy(features);
 return(ret);
}


/* API function. See libisoburn.h
*/
int isoburn_attach_image(struct burn_drive *d, IsoImage *image)
{
 int ret;
 struct isoburn *o;

 ret = isoburn_find_emulator(&o, d, 0);
 if (ret < 0 || o == NULL)
   return 0;
 if (image == NULL) {
   isoburn_msgs_submit(o, 0x00060000,
                       "Program error: isoburn_attach_image: image==NULL",
                       0, "FATAL", 0);
   return -1;
 }
 if(o->image != NULL)
   iso_image_unref(o->image);
 o->image = image;
 o->image_start_lba = -1;
 return(1);
}


/* API */
int isoburn_attach_start_lba(struct burn_drive *d, int lba, int flag)
{
 int ret;
 struct isoburn *o;

 ret = isoburn_find_emulator(&o, d, 0);
 if(ret < 0)
   return ret;
 if(o == NULL)
   return 0;
 if(o->image == NULL)
   return 0;
 o->image_start_lba = lba;
 return 1;
}


/* API function. See libisoburn.h
*/
int isoburn_activate_session(struct burn_drive *drive)
{
 int ret;
 struct isoburn *o;

 ret = isoburn_find_emulator(&o, drive, 0);
 if (ret < 0)
   return -1;

 if (o->emulation_mode != 1)
   return 1; /* don't need to activate session */
 if (o->fabricated_msc2 >= 0)
   return 1; /* blind growing: do not alter anything outside the session */
 
 if (!(o->fabricated_disc_status == BURN_DISC_APPENDABLE ||
       (o->fabricated_disc_status == BURN_DISC_BLANK &&
        o->zero_nwa > 0)))
   return 1;
 
 ret = burn_random_access_write(drive, (off_t) 0, (char*)o->target_iso_head, 
                                o->target_iso_head_size, 1);

 return ret;
}


/** API @since 0.6.2
*/
int isoburn_get_img_partition_offset(struct burn_drive *drive,
                                     uint32_t *block_offset_2k)
{
 int ret;
 struct isoburn *o;

 ret = isoburn_find_emulator(&o, drive, 0);
 if(ret < 0 || o == NULL)
   return -1;
 *block_offset_2k= o->loaded_partition_offset;
 if(o->loaded_partition_offset == 0)
   return(0);
 if(o->target_iso_head_size == (off_t) Libisoburn_target_head_sizE
                           + (off_t) 2048 * (off_t) o->loaded_partition_offset)
   return(1);
 return(2);
}


/* Check for MBR signature and a first partition that starts at a 2k block 
   and ends where the image ends.
   If not too large or too small, accept its start as partition offset.
*/
static int isoburn_inspect_partition(struct isoburn *o, uint32_t img_size,
                                     int flag)
{
 uint8_t *mbr, *part, *buf= NULL;
 uint32_t offst, numsec;
 struct ecma119_pri_vol_desc *pvm;
 off_t data_count;
 int ret;
 char *msg= NULL;
 static int max_offst= 512 - 32;

 buf= (uint8_t *) calloc(1, 2048);
 msg= calloc(1, 160);
 if(buf == NULL || msg == NULL)
   {ret= -1; goto ex;}

 mbr= o->target_iso_head;
 part= mbr + 446;
 if(mbr[510] != 0x55 || mbr[511] != 0xAA)
   {ret= 2; goto ex;} /* not an MBR */

 /* Does the first partition entry look credible ? */
 if(part[0] != 0x80 && part[0] != 0x00)
   {ret= 2; goto ex;} /* Invalid partition status */
 if(part[1] == 0 && part[2] == 0 && part[3] == 0)
   {ret= 2; goto ex;} /* Zero C/H/S start address */

 /* Does it match the normal ISO image ? */
 offst= iso_read_lsb(part + 8, 4);
 numsec= iso_read_lsb(part + 12, 4);
 if(offst < 64)
   {ret= 2; goto ex;} /* Zero or unusably small partition start */
 if((offst % 4) || (numsec % 4))
   {ret= 2; goto ex;} /* Not aligned to 2k */
 if(numsec < 72)
   {ret= 2; goto ex;} /* No room for volume descriptors */
 offst/= 4;
 numsec/= 4;
 if(offst + numsec != img_size)
   {ret= 2; goto ex;} /* Partition end does not match image end */

 /* Is there a PVD at the partition start ? */
 ret = burn_read_data(o->drive, (off_t) (offst + 16) * (off_t) 2048,
                      (char*) buf, 2048, &data_count, 2);
 if(ret <= 0)
   {ret= 2; goto ex;}
 pvm = (struct ecma119_pri_vol_desc *) buf;
 if (strncmp((char*) pvm->std_identifier, "CD001", 5) != 0)
   {ret= 2; goto ex;} /* not a PVD */
 if (pvm->vol_desc_type[0] != 1 || pvm->vol_desc_version[0] != 1 
     || pvm->file_structure_version[0] != 1 )
   {ret= 2; goto ex;} /* failed sanity check */

 if(iso_read_lsb(pvm->vol_space_size, 4) + offst != img_size)
   {ret= 2; goto ex;} /* Image ends do not match */

 /* Now it is credible. Not yet clear is whether it is acceptable. */
 o->loaded_partition_offset= offst;

 /* If the partition start is too large: Report but do not accept. */
 if(offst > (uint32_t) max_offst) {/* Not more than 1 MB of .target_iso_head */
   sprintf(msg,
      "Detected partition offset of %.f blocks. Maximum for load buffer is %d",
      (double) offst, max_offst);
   isoburn_msgs_submit(NULL, 0x00060000, msg, 0, "WARNING", 0);
   {ret= 3; goto ex;}
 }

 /* Accept partition start and adjust buffer size */
 ret= isoburn_adjust_target_iso_head(o, offst, 0);
 if(ret <= 0)
   goto ex;

 ret= 1;
ex:;
 if(buf != NULL)
   free(buf);
 if(msg != NULL)
   free(msg);
 return(ret);
}


/** Initialize the emulation of multi-session on random access media.
    The need for emulation is confirmed already.
    @param o A freshly created isoburn object. isoburn_create_data_source() was
             already called, nevertheless.
    @param flag bit0= read-only
    @return <=0 error , 1 = success
*/
int isoburn_start_emulation(struct isoburn *o, int flag)
{
 int ret, i, capacity = -1, role, dummy;
 off_t data_count, to_read;
 struct burn_drive *drive;
 struct ecma119_pri_vol_desc *pvm;
 enum burn_disc_status s;
 char *path= NULL, *msg= NULL;

 path= calloc(1, BURN_DRIVE_ADR_LEN);
 msg= calloc(1, 2 * BURN_DRIVE_ADR_LEN);
 if(path == NULL || msg == NULL)
   {ret= -1; goto ex;} 

 if(o==NULL) {
   isoburn_msgs_submit(NULL, 0x00060000,
                       "Program error: isoburn_start_emulation: o==NULL",
                       0, "FATAL", 0);
   {ret= -1; goto ex;}
 }

 drive= o->drive;

 if(flag & 1)
   o->fabricated_disc_status= BURN_DISC_FULL;

 /* We can assume 0 as start block for image.
    The data there point to the most recent session.
 */
 role = burn_drive_get_drive_role(drive);
 ret = burn_get_read_capacity(drive, &capacity, 0);
 if (ret <= 0)
   capacity = -1;
 if (role == 5) { /* random access write-only medium */
   s = burn_disc_get_status(drive);
   o->fabricated_disc_status= s;
   burn_disc_track_lba_nwa(drive, NULL, 0, &dummy, &(o->nwa));
   if(o->nwa < o->zero_nwa)
     o->zero_nwa= 0;
   {ret= 1; goto ex;}
 } else if (capacity > 0 || role == 2 || role == 4) {
   /* Might be a block device on a system where libburn cannot determine its
      size.  Try to read anyway. */
   to_read = o->target_iso_head_size;
   memset(o->target_iso_head, 0, to_read);
   if(capacity > 0 && (off_t) capacity * (off_t) 2048 < to_read)
     to_read = (off_t) capacity * (off_t) 2048;
   ret = burn_read_data(drive, (off_t) 0, (char*)o->target_iso_head, 
                        to_read, &data_count, 2 | 8);
   if (ret <= 0) {
     /* an error means a disc with no ISO image */
     o->media_read_error= 1;
     if (ret == -2) {
       path[0]= 0;
       burn_drive_d_get_adr(drive, path);
       sprintf(msg, "Pseudo drive '%s' does not allow reading", path);
       isoburn_msgs_submit(NULL, 0x00060000, msg, 0, "NOTE", 0);
       o->fabricated_disc_status= BURN_DISC_BLANK;
     } else if (capacity > 0)
       o->fabricated_disc_status= BURN_DISC_FULL;
     else if(!(flag & 1))
       o->fabricated_disc_status= BURN_DISC_BLANK;
     {ret= 1; goto ex;}
   }
 } else {
   /* No read capacity means blank medium */
   if(!(flag & 1))
     o->fabricated_disc_status= BURN_DISC_BLANK;
   {ret= 1; goto ex;}
 }

 /* check first 64K. If 0's, the disc is treated as a blank disc, and thus
    overwritten without extra check. */
 i = Libisoburn_target_head_sizE;
 while (i && !o->target_iso_head[i-1]) 
   --i;

 if (!i) {
   if(!(flag & 1))
     o->fabricated_disc_status= BURN_DISC_BLANK;
   {ret= 1; goto ex;}
 }

 pvm = (struct ecma119_pri_vol_desc *)(o->target_iso_head + 16 * 2048);

 if (strncmp((char*)pvm->std_identifier, "CD001", 5) == 0) {
   off_t size;

   /* sanity check */
   if (pvm->vol_desc_type[0] != 1 || pvm->vol_desc_version[0] != 1 
       || pvm->file_structure_version[0] != 1 ) {
     /* TODO for now I treat this as a full disc */
     o->fabricated_disc_status= BURN_DISC_FULL;
     {ret= 1; goto ex;}
   }

   /* ok, PVM found, set size */
   size = (off_t) iso_read_lsb(pvm->vol_space_size, 4);
   ret= isoburn_inspect_partition(o, (uint32_t) size, 0);
   if (ret <= 0)
     goto ex;
   size *= (off_t) 2048; /* block size in bytes */
   isoburn_set_start_byte(o, size, 0);
   if(!(flag & 1))
     o->fabricated_disc_status= BURN_DISC_APPENDABLE;
 } else if (strncmp((char*)pvm->std_identifier, "CDXX1", 5) == 0 ||
            (strncmp((char*)pvm->std_identifier, "CDxx1", 5) == 0 &&
             pvm->vol_desc_type[0] == 'x')) {

   /* empty image */
   isoburn_set_start_byte(o, o->zero_nwa * 2048, 0);
   if(!(flag & 1))
     o->fabricated_disc_status= BURN_DISC_BLANK;
 } else {
   /* treat any disc in an unknown format as full */
   o->fabricated_disc_status= BURN_DISC_FULL;
 }

 ret= 1;
ex:;
 if(path != NULL)
   free(path);
 if(msg != NULL)
   free(msg);
 return(ret);
}


/** Alters and writes the first 64 kB of a "medium" to invalidate
    an ISO image. (It shall stay restorable by skilled humans, though).
    The result shall especially keep libisoburn from accepting the medium
    image as ISO filesystem.
    @param o A fully activated isoburn object. isoburn_start_emulation()
             was already called.
    @return <=0 error , 1 = success
*/
int isoburn_invalidate_iso(struct isoburn *o, int flag)
{
 /* 
  * replace CD001 with CDXX1 in PVM.
  * I think this is enought for invalidating an iso image
  */
 strncpy((char*)o->target_iso_head + 16 * 2048 + 1, "CDXX1", 5);
 return isoburn_activate_session(o->drive);
}


/* API @since 0.1.0 */
int isoburn_set_read_pacifier(struct burn_drive *drive,
                              int (*read_pacifier)(IsoImage*, IsoFileSource*),
                              void *read_handle)
{
 int ret;
 struct isoburn *o;

 ret = isoburn_find_emulator(&o, drive, 0);
 if(ret < 0 || o == NULL)
   return -1;
 o->read_pacifier_handle= read_handle;
 o->read_pacifier= read_pacifier;
 return(1);
}

