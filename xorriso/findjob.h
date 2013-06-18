
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2012 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of classes FindjoB, ExprnodE, ExprtesT
   which perform tree searches in libisofs or in POSIX filesystem.
*/


#ifndef Xorriso_pvt_findjob_includeD
#define Xorriso_pvt_findjob_includeD yes


#define Xorriso_findjob_on_expR yes

#ifdef Xorriso_findjob_on_expR

/*
   A single Testnode.
*/
struct ExprtesT {

 struct FindjoB *boss;

 int invert;  /* 0=normal  1=invert result */ 

 /*
   0= -false (with invert : -true)
   1= -name char *arg1 (regex_t in *arg2)
   2= -type char *arg1
   3= -damaged
   4= -lba_range int *arg1 int *arg2
   5= -has_acl
   6= -has_xattr
   7= -has_aaip
   8= -has_filter
   9= -wanted_node IsoNode *arg1 (for internal use, arg1 not allocated)
  10= -pending_data
  11= -decision char *arg1 ("yes", "no")
  12= -prune
  13= -wholename char *arg1 (regex_t in *arg2)
  14= -has_any_xattr
  15= -has_md5
  16= -disk_name char *arg1 (regex_t in *arg2)
  17= -hidden int *arg1 (bit0=iso_rr, bit1=joliet)
  18= -has_hfs_crtp char *creator char *type 
  19= -has_hfs_bless int bless_index
  20= -disk_path char *arg1
 */
 int test_type;

 void *arg1;
 void *arg2;

};


/* 
   A computational node.
   A tree of these nodes forms the expression.
   Sequences of AND/OR operations form branches, brackets spawn new branches,
   NOT inverts node's test resp. subtree result.
*/
struct ExprnodE {

 struct ExprnodE *up;

 char origin[8];

  /* Operators */
 int invert;  /* 0=normal  1=invert own result (subtree or test, but not op) */ 

 int assoc; /*
                0= left : compute own value, combine with left value,
                          compute right value, combine with current value
                1= right: compute own value, compute right value, 
                          combine own and right, combine with left value
             */

 int use_shortcuts; /* 0= evaluate all tests of -and and -or,
                       1= evaluate only until the combined result is known
                     */

 struct ExprnodE *left;
 int left_op; /* 0=OR , 1=AND */
  
 struct ExprnodE *right;
 int right_op; /* see left_op */

 /* Brackets : a pointer to the first node in a subchain */ 
 struct ExprnodE *sub; 

 int is_if_then_else;
 struct ExprnodE *true_branch;
 struct ExprnodE *false_branch;

 /* elementary test : if sub!=NULL , test is ignored */
 struct ExprtesT *test;

 /* Result */
 int own_value;
 int composed_value;

};


struct FindjoB {

 char *start_path;

 struct ExprnodE *test_tree;

 struct ExprnodE *cursor;
 int invert;  /* 0=normal  1=set invert-property for next new test node */
 int use_shortcuts;

 /* 0= echo
    1= rm (also rmdir)
    2= rm_r
>>> 3= mv target
    4= chown user
    5= chgrp group
    6= chmod mode_and mode_or
    7= alter_date type date
    8= lsdl
    9= chown_r user
   10= chgrp_r group
   11= chmod_r mode_and mode_or
   12= alter_date_r type date
   13= find
   14= compare disk_equivalent_of_start_path
   15= in_iso iso_rr_equivalent_of_start_path
   16= not_in_iso iso_rr_equiv
   17= update disk_equiv
   18= add_missing iso_rr_equiv
   19= empty_iso_dir iso_rr_equiv
   20= is_full_in_iso iso_rr_equiv
   21= report_damage
   22= report_lba
   23= internal: memorize path of last matching node in found_path
   24= getfacl
   25= setfacl access_acl default_acl
   26= getfattr
   27= setfattr
   28= set_filter name
   29= show_stream
   30= internal: count by xorriso->node_counter
   31= internal: register in xorriso->node_array
   32= internal: widen_hardlinks disk_equiv: update nodes marked in di_do_widen
   33= get_any_xattr
   34= get_md5
   35= check_md5
   36= make_md5
   37= mkisofs_r
   38= sort_weight number
   39= hide on|iso_rr|joliet|off
   40= estimate_size
   41= update_merge disk_equiv
   42= rm_merge
   43= clear_merge
   44= list_extattr
   45= set_hfs_crtp creator type
   46= get_hfs_crtp
   47= set_hfs_bless blessing
   48= get_hfs_bless
   49= internal: update creator, type, and blessings from persistent isofs.*
 */
 int action;
 int prune;

 /* action specific parameters */
 char *target;
 char *text_2;
 uid_t user;
 gid_t group;
 mode_t mode_and, mode_or;
 int type; /* see Xorriso_set_time flag, also used as weight */
 time_t date;
 char *found_path;
 off_t estim_upper_size;
 off_t estim_lower_size;
 struct FindjoB *subjob;

 /* Errors */
 char errmsg[4096];
 int errn; /*
             >0 = UNIX errno
             -1 = close_bracket: no bracket open 
             -2 = binary operator or closing bracket expected
             -3 = unexpected binary operator or closing bracket
             -4 = unsupported command
             -5 = -then -elseif -else -endif without -if or at wrong place
           */

 /* Counts the test matches */
 unsigned long match_count;

};


int Exprnode_destroy(struct ExprnodE **fnode, int flag);

int Exprnode_tree_value(struct XorrisO *xorriso, struct ExprnodE *fnode,
                        int left_value, void *node, char *name, char *path,
                        struct stat *boss_stbuf, struct stat *stbuf, int flag);


int Findjob_new(struct FindjoB **o, char *start_path, int flag);

int Findjob_destroy(struct FindjoB **o, int flag);

int Findjob_set_start_path(struct FindjoB *o, char *start_path, int flag);

int Findjob_get_start_path(struct FindjoB *o, char **start_path, int flag);

int Findjob_set_commit_filter_2(struct FindjoB *o, int flag);

int Findjob_set_lba_range(struct FindjoB *o, int start_lba, int count, 
                          int flag);

int Findjob_set_wanted_node(struct FindjoB *o, void *wanted_node, int flag);

/* @param value -1= only undamaged files, 1= only damaged files
*/
int Findjob_set_damage_filter(struct FindjoB *o, int value, int flag);

int Findjob_set_test_hidden(struct FindjoB *o, int mode, int flag);

int Findjob_set_crtp_filter(struct FindjoB *o, char *creator, char *hfs_type,
                            int flag);

int Findjob_set_bless_filter(struct XorrisO *xorriso, struct FindjoB *o,
                             char *blessing, int flag);


int Findjob_set_decision(struct FindjoB *o, char *decision, int flag);

int Findjob_open_bracket(struct FindjoB *job, int flag);

int Findjob_close_bracket(struct FindjoB *job, int flag);

int Findjob_not(struct FindjoB *job, int flag);

int Findjob_and(struct FindjoB *job, int flag);

int Findjob_or(struct FindjoB *job, int flag);

int Findjob_if(struct FindjoB *job, int flag);

int Findjob_then(struct FindjoB *job, int flag);

int Findjob_else(struct FindjoB *job, int flag);

int Findjob_elseif(struct FindjoB *job, int flag);

int Findjob_endif(struct FindjoB *job, int flag);

int Findjob_test_2(struct XorrisO *xorriso, struct FindjoB *o, 
                   void *node, char *name, char *path,
                   struct stat *boss_stbuf, struct stat *stbuf, int flag);

int Findjob_set_action_found_path(struct FindjoB *o, int flag);

/* @param flag bit0= recursive
*/
int Findjob_set_action_target(struct FindjoB *o, int action, char *target,
                              int flag);

/* @param flag bit0= recursive
*/
int Findjob_set_action_ad(struct FindjoB *o, int type, time_t date, int flag);

/* @param flag bit0= recursive
*/
int Findjob_set_action_chgrp(struct FindjoB *o, gid_t group, int flag);

/* @param flag bit0= recursive
*/
int Findjob_set_action_chmod(struct FindjoB *o,
                             mode_t mode_and, mode_t mode_or, int flag);

/* @param flag bit0= recursive
*/
int Findjob_set_action_chown(struct FindjoB *o, uid_t user,int flag);

/* @param flag bit0= -wholename rather than -name
*/
int Findjob_set_name_expr(struct FindjoB *o, char *name_expr, int flag);

int Findjob_set_file_type(struct FindjoB *o, char file_type, int flag);

/* @param value -1= files without ACL, 1= only files with ACL
*/
int Findjob_set_acl_filter(struct FindjoB *o, int value, int flag);

/* @param value -1= files without xattr, 1= only files with xattr
   @param flag bit0=-has_any_xattr rather than -has_xattr
*/
int Findjob_set_xattr_filter(struct FindjoB *o, int value, int flag);

/* @param value -1= files without aaip, 1= only files with aaip
*/
int Findjob_set_aaip_filter(struct FindjoB *o, int value, int flag);

/* @param value -1= files without filter, 1= files with filter
*/
int Findjob_set_filter_filter(struct FindjoB *o, int value, int flag);

/* @param value -1= only without property, 1= only with property
   @param flag bit0= pseudo-test:
                     if no operator is open, do nothing and return 2
*/
int Findjob_set_prop_filter(struct FindjoB *o, int test_type, int value,
                            int flag);

/* @param value -1= true, 1= false
   @param flag bit0= pseudo-test:
                     if no operator is open, do nothing and return 2
*/
int Findjob_set_false(struct FindjoB *o, int value, int flag);

int Findjob_set_prune(struct FindjoB *o, int flag);


int Findjob_set_action_subjob(struct FindjoB *o, int action,
                              struct FindjoB *subjob, int flag);

int Findjob_set_action_text_2(struct FindjoB *o, int action, char *target,
                              char* text_2, int flag);

int Findjob_set_action_type(struct FindjoB *o, int action, int type, int flag);


int Findjob_get_action(struct FindjoB *o, int flag);

int Findjob_get_action_parms(struct FindjoB *o, char **target, char **text_2,
                             uid_t *user, gid_t *group,
                             mode_t *mode_and, mode_t *mode_or,
                             int *type, time_t *date, struct FindjoB **subjob,
                             int flag);

int Findjob_set_found_path(struct FindjoB *o, char *path, int flag);

int Findjob_get_found_path(struct FindjoB *o, char **path, int flag);

#else /* Xorriso_findjob_on_expR */


struct FindjoB;


int Findjob_new(struct FindjoB **o, char *start_path, int flag);

int Findjob_destroy(struct FindjoB **job, int flag);


/* @return 0=no match , 1=match , <0 = error
*/
int Findjob_test(struct FindjoB *job, char *name, 
                 struct stat *boss_stbuf, struct stat *stbuf,
                 int depth, int flag);

/* @return <0 error, >=0 see xorriso.c struct FindjoB.action
*/
int Findjob_get_action(struct FindjoB *o, int flag);

/* @return <0 error, >=0 see xorriso.c struct FindjoB.action
*/ 
int Findjob_get_action_parms(struct FindjoB *o, char **target, char **text_2,
                             uid_t *user, gid_t *group,
                             mode_t *mode_and, mode_t *mode_or,
                             int *type, time_t *date, struct FindjoB **subjob,
                             int flag);

/* @param flag bit0= recursive
*/
int Findjob_set_action_target(struct FindjoB *o, int action, char *target,
                              int flag);

/* @param flag bit0= recursive
*/
int Findjob_set_action_chgrp(struct FindjoB *o, gid_t group, int flag);

/* @param flag bit0= recursive
*/
int Findjob_set_action_chmod(struct FindjoB *o,
                             mode_t mode_and, mode_t mode_or, int flag);

/* @param flag bit0= recursive
*/
int Findjob_set_action_ad(struct FindjoB *o, int type, time_t date, int flag);

int Findjob_set_start_path(struct FindjoB *o, char *start_path, int flag);

int Findjob_set_action_found_path(struct FindjoB *o, int flag);

int Findjob_get_start_path(struct FindjoB *o, char **start_path, int flag);

int Findjob_set_lba_range(struct FindjoB *o, int start_lba, int count,
                          int flag);

int Findjob_get_lba_damage_filter(struct FindjoB *o, int *start_lba,
                                  int *end_lba, int *damage_filter, int flag);

int Findjob_get_commit_filter(struct FindjoB *o, int *commit_filter, int flag);

int Findjob_get_acl_filter(struct FindjoB *o, int *acl_filter, int flag);

int Findjob_get_xattr_filter(struct FindjoB *o, int *xattr_filter, int flag);

int Findjob_get_aaip_filter(struct FindjoB *o, int *aaip_filter, int flag);

int Findjob_get_filter_filter(struct FindjoB *o, int *value, int flag);

int Findjob_set_wanted_node(struct FindjoB *o, void *wanted_node, int flag);

int Findjob_get_wanted_node(struct FindjoB *o, void **wanted_node, int flag);

int Findjob_set_found_path(struct FindjoB *o, char *path, int flag);

int Findjob_get_found_path(struct FindjoB *o, char **path, int flag);

#endif /* ! Xorriso_findjob_on_expR */


#endif /* ! Xorriso_pvt_findjob_includeD */

