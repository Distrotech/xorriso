
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of functions which operate on
   data filter objects.
*/


#ifndef Xorriso_pvt_filters_includeD
#define Xorriso_pvt_filters_includeD yes

int Xorriso_extf_new(struct XorrisO *xorriso, struct Xorriso_extF **filter,
                     char *path, int argc, char **argv, int behavior,
                     char *suffix, char *name, int flag);

int Xorriso_extf_destroy(struct XorrisO *xorriso, struct Xorriso_extF **filter,
                         int flag);

int Xorriso_lookup_extf(struct XorrisO *xorriso, char *name,
                        struct Xorriso_lsT **found_lst, int flag);

int Xorriso_rename_suffix(struct XorrisO *xorriso, IsoNode *node, char *suffix,
                          char *show_path, char new_name[], int flag);


#endif /* ! Xorriso_pvt_filters_includeD */

