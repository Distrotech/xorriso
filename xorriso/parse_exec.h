
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of functions which deal with parsing
   and interpretation of command input.
*/


#ifndef Xorriso_pvt_cmd_includeD
#define Xorriso_pvt_cmd_includeD yes


/* @param flag bit0= do not warn of wildcards
               bit1= these are disk_paths
*/
int Xorriso_end_idx(struct XorrisO *xorriso,
                    int argc, char **argv, int idx, int flag);

int Xorriso_opt_args(struct XorrisO *xorriso, char *cmd,
                     int argc, char **argv, int idx,
                     int *end_idx, int *optc, char ***optv, int flag);

int Xorriso_get_problem_status(struct XorrisO *xorriso, char severity[80],
                               int flag);

int Xorriso_set_problem_status(struct XorrisO *xorriso, char *severity,
                               int flag);

/**
    @param flag       bit0= do not issue own event messages
                      bit1= take xorriso->request_to_abort as reason for abort
    @return           Gives the advice:
                        2= pardon was given, go on
                        1= no problem, go on
                        0= function failed but xorriso would not abort, go on
                       <0= do abort
                           -1 = due to problem_status
                           -2 = due to xorriso->request_to_abort
*/
int Xorriso_eval_problem_status(struct XorrisO *xorriso, int ret, int flag);

int Xorriso_cpmv_args(struct XorrisO *xorriso, char *cmd,
                      int argc, char **argv, int *idx,
                      int *optc, char ***optv, char eff_dest[SfileadrL],
                      int flag);

/* @param flag bit0= with adr_mode sbsector: adr_value is possibly 16 too high
*/
int Xorriso_decode_load_adr(struct XorrisO *xorriso, char *cmd,
                            char *adr_mode, char *adr_value,
                            int *entity_code, char entity_id[81],
                            int flag);

int Xorriso_check_name_len(struct XorrisO *xorriso, char *name, int size,
                           char *cmd, int flag);

/* @param flag bit0= prepend wd only if name does not begin by '/'
               bit2= prepend wd (automatically done if wd[0]!=0)
*/
int Xorriso_make_abs_adr(struct XorrisO *xorriso, char *wd, char *name,
                             char adr[], int flag);

/* @param flag bit0= do not complain in case of error, but set info_text */
int Xorriso_convert_datestring(struct XorrisO *xorriso, char *cmd,
                               char *time_type, char *timestring,
                               int *t_type, time_t *t, int flag);

int Xorriso_check_temp_mem_limit(struct XorrisO *xorriso, off_t mem, int flag);

/* @param flag bit0= use env_path to find the desired program
*/
int Xorriso_execv(struct XorrisO *xorriso, char *cmd, char *env_path,
                  int *status, int flag);

int Xorriso_path_is_excluded(struct XorrisO *xorriso, char *path, int flag);

int Xorriso_path_is_hidden(struct XorrisO *xorriso, char *path, int flag);


/* Normalize ACL and sort apart "access" ACL from "default" ACL.
 */
int Xorriso_normalize_acl_text(struct XorrisO *xorriso, char *in_text,
                    char **access_acl_text, char **default_acl_text, int flag);

int Xorriso_read_mkisofsrc(struct XorrisO *xorriso, int flag);

/* @param flag bit0= list sorting order rather than looking for argv[idx]
*/
int Xorriso_cmd_sorting_rank(struct XorrisO *xorriso,
                        int argc, char **argv, int idx, int flag);

#endif /* ! Xorriso_pvt_cmd_includeD */

