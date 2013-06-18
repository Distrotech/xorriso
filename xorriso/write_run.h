
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of functions which are needed to write
   sessions.
*/


#ifndef Xorriso_pvt_write_run_includeD
#define Xorriso_pvt_write_run_includeD yes


/* CD specs say one shall not write tracks < 600 kiB */
#define Xorriso_cd_min_track_sizE 300


/* Default setting for -compliance */
#define Xorriso_relax_compliance_defaulT \
        (isoburn_igopt_allow_deep_paths | isoburn_igopt_allow_longer_paths | \
         isoburn_igopt_always_gmt | isoburn_igopt_dir_rec_mtime | \
         isoburn_igopt_rrip_version_1_10 | isoburn_igopt_aaip_susp_1_10 | \
         isoburn_igopt_only_iso_versions | isoburn_igopt_no_j_force_dots)


int Xorriso_make_write_options(
        struct XorrisO *xorriso, struct burn_drive *drive,
        struct burn_write_opts **burn_options, int flag);

int Xorriso_sanitize_image_size(struct XorrisO *xorriso,
                    struct burn_drive *drive, struct burn_disc *disc,
                    struct burn_write_opts *burn_options, int flag);

int Xorriso_auto_format(struct XorrisO *xorriso, int flag);

int Xorriso_set_system_area(struct XorrisO *xorriso, struct burn_drive *drive,
                            IsoImage *img, struct isoburn_imgen_opts *sopts,
                            int flag);

int Xorriso_check_burn_abort(struct XorrisO *xorriso, int flag);

int Xorriso_pacifier_loop(struct XorrisO *xorriso, struct burn_drive *drive,
                          int flag);

int Xorriso_set_isolinux_options(struct XorrisO *xorriso,
                                 IsoImage *image, int flag);


#endif /* ! Xorriso_pvt_write_run_includeD */

