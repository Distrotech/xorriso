
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the implementation of functions for pattern matching.
*/


#ifndef Xorriso_pvt_match_includeD
#define Xorriso_pvt_match_includeD yes


int Xorriso_prepare_regex(struct XorrisO *xorriso, char *adr, int flag);

/* @return 0=match , else no match
*/
int Xorriso_regexec(struct XorrisO *xorriso, char *to_match, int *failed_at,
                    int flag);

int Xorriso_is_in_patternlist(struct XorrisO *xorriso,
                        struct Xorriso_lsT *patternlist, char *path, int flag);

char *Xorriso_get_pattern(struct XorrisO *xorriso,
                         struct Xorriso_lsT *patternlist, int index, int flag);

int Xorriso_prepare_expansion_pattern(struct XorrisO *xorriso, char *pattern,
                                      int flag);

/* @param flag bit0= count results rather than storing them
   @return <=0 error , 1 is root (end processing) ,
                       2 is not root (go on processing) 
*/
int Xorriso_check_for_root_pattern(struct XorrisO *xorriso,
              int *filec, char **filev, int count_limit, off_t *mem, int flag);

/* @param flag bit0= count result rather than storing it
               bit1= unexpected change of number is a FATAL event
*/
int Xorriso_register_matched_adr(struct XorrisO *xorriso,
                               char *adr, int count_limit,
                               int *filec, char **filev, off_t *mem, int flag);

int Xorriso_eval_nonmatch(struct XorrisO *xorriso, char *pattern,
                          int *nonconst_mismatches, off_t *mem, int flag);

/* @param flag bit0= a match count !=1 is a SORRY event
*/
int Xorriso_check_matchcount(struct XorrisO *xorriso,
                int count, int nonconst_mismatches, int num_patterns,
                char **patterns, int flag);

int Xorriso_no_pattern_memory(struct XorrisO *xorriso, off_t mem, int flag);

int Xorriso_alloc_pattern_mem(struct XorrisO *xorriso, off_t mem,
                              int count, char ***filev, int flag);

/* @param flag bit0= command without pattern capability
               bit1= disk_pattern rather than iso_rr_pattern
*/
int Xorriso_warn_of_wildcards(struct XorrisO *xorriso, char *path, int flag);

/* @param flag bit0= a match count !=1 is a FAILURE event
               bit1= with bit0 tolerate 0 matches if pattern is a constant
*/
int Xorriso_expand_disk_pattern(struct XorrisO *xorriso,
                           int num_patterns, char **patterns, int extra_filec,
                           int *filec, char ***filev, off_t *mem, int flag);


#endif /* ! Xorriso_pvt_match_includeD */

