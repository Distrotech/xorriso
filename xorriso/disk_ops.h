
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of class DirseQ which
   crawls along a directory's content list.
*/


#ifndef Xorriso_pvt_diskop_includeD
#define Xorriso_pvt_diskop_includeD yes


/* @param flag bit0= simple readlink(): no normalization, no multi-hop
*/
int Xorriso_resolve_link(struct XorrisO *xorriso,
                     char *link_path, char result_path[SfileadrL], int flag);

int Xorriso_convert_gidstring(struct XorrisO *xorriso, char *gid_string,
                              gid_t *gid, int flag);

int Xorriso_convert_modstring(struct XorrisO *xorriso, char *cmd, char *mode,
                              mode_t *mode_and, mode_t *mode_or, int flag);

int Xorriso_convert_uidstring(struct XorrisO *xorriso, char *uid_string,
                              uid_t *uid, int flag); 

/* @param flag bit0= for Xorriso_msgs_submit: use pager
*/
int Xorriso_hop_link(struct XorrisO *xorriso, char *link_path,
                   struct LinkiteM **link_stack, struct stat *stbuf, int flag);

int Xorriso__mode_to_perms(mode_t st_mode, char perms[11], int flag);

int Xorriso_format_ls_l(struct XorrisO *xorriso, struct stat *stbuf, int flag);

/* @param flag bit0= long format
               bit1= do not print count of nodes
               bit2= du format
               bit3= print directories as themselves (ls -d)
*/
int Xorriso_lsx_filev(struct XorrisO *xorriso, char *wd, 
                      int filec, char **filev, off_t boss_mem, int flag);

/*
   @param flag >>> bit0= remove whole sub tree: rm -r
               bit1= remove empty directory: rmdir 
               bit2= recursion: do not reassure in mode 2 "tree"
               bit3= this is for overwriting and not for plain removal
               bit4= count deleted files in xorriso->pacifier_count
               bit5= with bit0 only remove directory content, not the directory
   @return   <=0 = error
               1 = removed leaf file object
               2 = removed directory or tree
               3 = did not remove on user revocation
*/
int Xorriso_rmx(struct XorrisO *xorriso, off_t boss_mem, char *path, int flag);

int Xorriso_findx(struct XorrisO *xorriso, struct FindjoB *job,
                  char *abs_dir_parm, char *dir_path,
                  struct stat *dir_stbuf, int depth,
                  struct LinkiteM *link_stack, int flag);

/* @param flag bit0= no hardlink reconstruction
               bit1= do not set xorriso->node_*_prefixes
               bit5= -extract_single: eventually do not insert directory tree
*/
int Xorriso_restore_sorted(struct XorrisO *xorriso, int count,
                           char **src_array, char **tgt_array, 
                           int *problem_count, int flag);

/* @param flag bit0= path is a directory
               bit2= recursion: do not reassure in mode 2 "tree"
               bit3= this is for overwriting and not for plain removal
*/
int Xorriso_reassure_restore(struct XorrisO *xorriso, char *path, int flag);

/* @param flag bit7= return 4 if restore fails from denied permission
                     do not issue error message
   @return <=0 failure , 1 success ,
           4 with bit7: permission to create file was denied
*/
int Xorriso_make_tmp_path(struct XorrisO *xorriso, char *orig_path,
                          char *tmp_path, int *fd, int flag);

/* @param flag bit0= change regardless of xorriso->do_auto_chmod
               bit1= desired is only rx
*/
int Xorriso_auto_chmod(struct XorrisO *xorriso, char *disk_path, int flag);

int Xorriso_make_accessible(struct XorrisO *xorriso, char *disk_path,int flag);

/* @param flag bit0= prefer to find a match after *img_prefixes
                     (but deliver img_prefixes if no other can be found)
*/
int Xorriso_make_restore_path(struct XorrisO *xorriso,
         struct Xorriso_lsT **img_prefixes, struct Xorriso_lsT **disk_prefixes,
         char img_path[SfileadrL], char disk_path[SfileadrL], int flag);

int Xorriso_restore_make_hl(struct XorrisO *xorriso,
                            char *old_path, char *new_path, int flag);

int Xorriso_afile_fopen(struct XorrisO *xorriso,
                        char *filename, char *mode, FILE **ret_fp, int flag);

int Xorriso_make_mount_cmd(struct XorrisO *xorriso, char *cmd,
                           int lba, int track, int session, char *volid,
                           char *devadr, char result[SfileadrL], int flag);

int Xorriso_append_scdbackup_record(struct XorrisO *xorriso, int flag);


#endif /* ! Xorriso_pvt_diskop_includeD */

