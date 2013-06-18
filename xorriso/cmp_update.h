
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of class DirseQ which
   crawls along a directory's content list.
*/


#ifndef Xorriso_pvt_cmp_includeD
#define Xorriso_pvt_cmp_includeD yes

int Xorriso_compare_2_files(struct XorrisO *xorriso, char *disk_adr,
                            char *iso_adr, char *adr_common_tail,
                            int *result, int flag);

int Xorriso_pfx_disk_path(struct XorrisO *xorriso, char *iso_path,
                          char *iso_prefix, char *disk_prefix,
                          char disk_path[SfileadrL], int flag);

/* @param boss_iter Opaque handle to be forwarded to actions in ISO image
                    Set to NULL if calling this function from outside ISO world
   @param node      Opaque handle to be forwarded to actions in ISO image
                    Set to NULL if calling this function from outside ISO world
   @param flag bit0= update rather than compare
*/
int Xorriso_find_compare(struct XorrisO *xorriso, void *boss_iter, void *node,
                         char *iso_path, char *iso_prefix, char *disk_prefix,
                         int flag);

/* @param boss_iter Opaque handle to be forwarded to actions in ISO image
                    Set to NULL if calling this function from outside ISO world
   @param @node     Opaque handle to be forwarded to actions in ISO image
                    Set to NULL if calling this function from outside ISO world
*/
int Xorriso_update_interpreter(struct XorrisO *xorriso,
                               void *boss_iter, void *node,
                               int compare_result, char *disk_path,
                               char *iso_rr_path, int flag);

#endif /* ! Xorriso_pvt_cmp_includeD */

