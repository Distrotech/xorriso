
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of functions which operate on ISO images
   and their global properties.
*/


#ifndef Xorriso_pvt_iso_img_includeD
#define Xorriso_pvt_iso_img_includeD yes


int Xorriso_update_volid(struct XorrisO *xorriso, int flag);

int Xorriso_record_boot_info(struct XorrisO *xorriso, int flag);

int Xorriso_assert_volid(struct XorrisO *xorriso, int msc1, int flag);

int Xorriso_is_isohybrid(struct XorrisO *xorriso, IsoFile *bootimg_node,
                         int flag);

int Xorriso_boot_item_status(struct XorrisO *xorriso, char *cat_path,
                             char *bin_path, int platform_id,
                             int patch_isolinux, int emul, off_t load_size,
                             unsigned char *id_string,
                             unsigned char *selection_crit, char *form,
                             char *filter, FILE *fp, int flag);

int Xorriso__append_boot_params(char *line, ElToritoBootImage *bootimg,
                                int flag);

int Xorriso_get_volume(struct XorrisO *xorriso, IsoImage **volume,
                       int flag);





#endif /* ! Xorriso_pvt_iso_img_includeD */

