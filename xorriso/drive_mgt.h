
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of functions which operate on drives
   and media.
*/


#ifndef Xorriso_pvt_drive_mgt_includeD
#define Xorriso_pvt_drive_mgt_includeD yes


int Xorriso_may_burn(struct XorrisO *xorriso, int flag);

int Xorriso_toc_line(struct XorrisO *xorriso, int flag);

int Xorriso_media_product(struct XorrisO *xorriso, int flag);

int Xorriso_check_md5_range(struct XorrisO *xorriso, off_t start_lba,
                            off_t end_lba, char md5[16], int flag);

int Xorriso_check_interval(struct XorrisO *xorriso, struct SpotlisT *spotlist,
                           struct CheckmediajoB *job,
                           int from_lba, int block_count, int read_chunk,
                           int md5_start, int flag);

int Xorriso_get_drive_handles(struct XorrisO *xorriso,
                              struct burn_drive_info **dinfo,
                              struct burn_drive **drive,
                              char *attempt, int flag);

int Xorriso_check_for_abort(struct XorrisO *xorriso,
                            char *abort_file_path,
                            double post_read_time,
                            double *last_abort_file_time, int flag);

#endif /* ! Xorriso_pvt_drive_mgt_includeD */

