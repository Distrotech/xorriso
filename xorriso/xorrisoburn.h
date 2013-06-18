

/* Adapter to libisoburn, libisofs and libburn for xorriso,
   a command line oriented batch and dialog tool which creates, loads,
   manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2013 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the inner isofs- and burn-library interface of xorriso.
*/

#ifndef Xorrisoburn_includeD
#define Xorrisoburn_includeD yes


/* The minimum version of libisoburn to be used with this version of xorriso
*/
#define xorriso_libisoburn_req_major  1
#define xorriso_libisoburn_req_minor  3
#define xorriso_libisoburn_req_micro  0


struct SpotlisT;          /* List of intervals with different read qualities */
struct CheckmediajoB;     /* Parameters for Xorriso_check_media() */
struct Xorriso_msg_sievE; /* Fishes for info particles in reply messages */


int Xorriso_startup_libraries(struct XorrisO *xorriso, int flag);

/* @param flag bit0= global shutdown of libraries */
int Xorriso_detach_libraries(struct XorrisO *xorriso, int flag);

int Xorriso_create_empty_iso(struct XorrisO *xorriso, int flag);

/* @param flag bit0=aquire as isoburn input drive
               bit1=aquire as libburn output drive (as isoburn drive if bit0)
   @return <=0 failure , 1=success , 2=neither readable or writeable
*/
int Xorriso_aquire_drive(struct XorrisO *xorriso, char *adr, char *show_adr,
                         int flag);

int Xorriso_give_up_drive(struct XorrisO *xorriso, int flag);

int Xorriso_write_session(struct XorrisO *xorriso, int flag);

/* @param boss_iter Opaque handle to be forwarded to actions in ISO image
                    Set to NULL if calling this function from outside ISO world
   @param flag bit0= mkdir: graft in as empty directory, not as copy from disk
               bit1= do not report added files
   @return <=0 = error , 1 = added simple node , 2 = added directory 
*/
int Xorriso_graft_in(struct XorrisO *xorriso, void *boss_iter,       
                     char *disk_path, char *img_path,
                     off_t offset, off_t cut_size, int flag);

int Xorriso__text_to_sev(char *severity_name, int *severity_number,int flag);

int Xorriso__sev_to_text(int severity, char **severity_name, int flag);

/* @param flag bit0=report about output drive 
               bit1=short report form
               bit2=do not try to read ISO heads
               bit3=report to info channel (else to result channel)
*/
int Xorriso_toc(struct XorrisO *xorriso, int flag);

/* @param flag bit0= no output if no boot record was found
               bit3= report to info channel (else to result channel)
*/
int Xorriso_show_boot_info(struct XorrisO *xorriso, int flag);

int Xorriso_show_devices(struct XorrisO *xorriso, int flag);

int Xorriso_tell_media_space(struct XorrisO *xorriso,
                             int *media_space, int *free_space, int flag);

/* @param flag bit0=fast , bit1=deformat
   @return 0=failure, did not touch medium , -1=failure, altered medium
           1=success, altered medium       ,  2=success, did not touch medium
*/
int Xorriso_blank_media(struct XorrisO *xorriso, int flag);

/* @param flag bit0= try to achieve faster formatting
               bit1= use parameter size (else use default size)
               bit2= do not re-aquire drive
               bit7= by_index mode:
                     bit8 to bit15 contain the index of the format to use.
   @return 0=failure, did not touch medium , -1=failure, altered medium
           1=success, altered medium       ,  2=success, did not touch medium
*/
int Xorriso_format_media(struct XorrisO *xorriso, off_t size, int flag);

/* @return <=0 error, 1 success
*/
int Xorriso_list_formats(struct XorrisO *xorriso, int flag);

/* @return <=0 error, 1 success
*/ 
int Xorriso_list_speeds(struct XorrisO *xorriso, int flag);

/* @param flag bit1= obtain outdrive, else indrive
   @return <=0 error, 1 success
*/
int Xorriso_list_profiles(struct XorrisO *xorriso, int flag);

/* @param flag bit2= formatting rather than blanking
   @return 0=failure, did not touch medium , -1=failure, altered medium
           1=success, altered medium       ,  2=success, did not touch medium
*/ 
int Xorriso_blank_as_needed(struct XorrisO *xorriso, int flag);


/* @param boss_iter  Opaque internal handle. Use NULL outside xorrisoburn.c :
               If not NULL then this is an iterator suitable for
               iso_dir_iter_remove() which is then to be used instead
               of iso_node_remove().
   @param flag bit0= remove whole sub tree: rm -r
               bit1= remove empty directory: rmdir
               bit2= recursion: do not reassure in mode 2 "tree"
               bit3= this is for overwriting and not for plain removal
               bit4= count deleted files in xorriso->pacifier_count
               bit5= with bit0 only remove directory content, not the directory
               bit6= do not delete eventually existing node from di_array
   @return   <=0 = error
               1 = removed simple node
               2 = removed directory or tree
               3 = did not remove on user revocation
*/
int Xorriso_rmi(struct XorrisO *xorriso, void *boss_iter, off_t boss_mem,
                char *path, int flag);


/* @param flag bit0= long format
               bit1= do not print count of nodes
               bit2= du format
               bit3= print directories as themselves (ls -d)
*/
int Xorriso_ls_filev(struct XorrisO *xorriso, char *wd, 
                     int filec, char **filev, off_t boss_mem, int flag);

/* This function needs less buffer memory than Xorriso_ls_filev() but cannot
   perform structured pattern matching.
   @param flag bit0= long format
               bit1= only check for directory existence
               bit2= do not apply search pattern but accept any file
               bit3= just count nodes and return number
*/
int Xorriso_ls(struct XorrisO *xorriso, int flag);

/* @param wd        Path to prepend in case img_path is not absolute
   @param img_path  Absolute or relative path to be normalized
   @param eff_path  returns resulting effective path.
                    Must provide at least SfileadrL bytes of storage.
   @param flag bit0= do not produce problem events (unless faulty path format)
               bit1= work purely literally, do not use libisofs
               bit2= (with bit1) this is an address in the disk world
   @return -1 = faulty path format, 0 = not found ,
            1 = found simple node , 2 = found directory
*/
int Xorriso_normalize_img_path(struct XorrisO *xorriso, char *wd,
                               char *img_path, char eff_path[], int flag);

/* @param boss_iter Opaque handle to be forwarded to actions in ISO image
                    Set to NULL if calling this function from outside ISO world
*/
int Xorriso_rename(struct XorrisO *xorriso, void *boss_iter,
                   char *origin, char *dest, int flag);

/* @param flag bit0= do not produce info message on success
   @return 1=success, 0=was already directory, -1=was other type, -2=bad path
*/
int Xorriso_mkdir(struct XorrisO *xorriso, char *img_path, int flag);

/* @param flag bit0= a match count !=1 is a SORRY event */
int Xorriso_expand_pattern(struct XorrisO *xorriso,
                           int num_patterns, char **patterns, int extra_filec,
                           int *filec, char ***filev, off_t *mem, int flag);

int Xorriso_set_st_mode(struct XorrisO *xorriso, char *path,
                        mode_t mode_and, mode_t mode_or, int flag);

int Xorriso_set_uid(struct XorrisO *xorriso, char *in_path, uid_t uid,
                    int flag);

int Xorriso_set_gid(struct XorrisO *xorriso, char *in_path, gid_t gid,
                    int flag);

/* @parm flag  bit0= atime, bit1= ctime, bit2= mtime, bit8=no auto ctime */
int Xorriso_set_time(struct XorrisO *xorriso, char *in_path, time_t t,
                    int flag);

/* @param flag bit0= recursion
               bit1= do not count deleted files with rm and rm_r
*/
int Xorriso_findi(struct XorrisO *xorriso, struct FindjoB *job,
                  void *boss_iter, off_t boss_mem,
                  void *dir_node_generic, char *dir_path,
                  struct stat *dir_stbuf, int depth, int flag);

/* @param flag bit0= do not dive into trees
               bit1= do not perform job->action on resulting node array
*/
int Xorriso_findi_sorted(struct XorrisO *xorriso, struct FindjoB *job,
                         off_t boss_mem, int filec, char **filev, int flag);

/* @param flag bit0= do not mark image as changed */
int Xorriso_set_volid(struct XorrisO *xorriso, char *volid, int flag);

int Xorriso_get_volid(struct XorrisO *xorriso, char volid[33], int flag);

int Xorriso_set_abort_severity(struct XorrisO *xorriso, int flag);

int Xorriso_report_lib_versions(struct XorrisO *xorriso, int flag);

/* @return 0= stbuf content is valid ,
          -1 = path not found , -2 = severe error occured
*/
int Xorriso_iso_lstat(struct XorrisO *xorriso, char *path, struct stat *stbuf,
                      int flag);

/* @param flag bit0= -inq
               bit1= -checkdrive
*/
int Xorriso_atip(struct XorrisO *xorriso, int flag);

/* @param write_start_address  is valid if >=0 
   @param tsize is valid if >0
   @param flag bit0= grow_overwriteable_iso
               bit1= do_isosize
*/
int Xorriso_burn_track(struct XorrisO *xorriso, off_t write_start_address,
                       char *track_source, off_t tsize, int flag);

/* @param flag bit1= outdev rather than indev
   @return <=0 = failure , 1= ok , 2= ok, is CD profile
*/ 
int Xorriso_get_profile(struct XorrisO *xorriso, int *profile_number,
                        char profile_name[80], int flag);

/* @param flag bit0= node_pt is a valid ISO object handle, ignore pathname
               bit1= dig out the most original stream for reading
*/
int Xorriso_iso_file_open(struct XorrisO *xorriso, char *pathname,
                          void *node_pt, void **stream, int flag);

int Xorriso_iso_file_read(struct XorrisO *xorriso, void *stream, char *buf,
                          int count, int flag);

int Xorriso_iso_file_close(struct XorrisO *xorriso, void **stream, int flag);

/* @param bit0= copy link target properties rather than link properties
*/
int Xorriso_copy_properties(struct XorrisO *xorriso,
                            char *disk_path, char *img_path, int flag);

int Xorriso_cut_out(struct XorrisO *xorriso, char *disk_path,
                off_t startbyte, off_t bytecount, char *iso_rr_path, int flag);

int Xorriso_paste_in(struct XorrisO *xorriso, char *disk_path,
                off_t startbyte, off_t bytecount, char *iso_rr_path, int flag);

struct SplitparT;

/* @param flag bit0= in_node is valid, do not resolve iso_adr
*/
int Xorriso_identify_split(struct XorrisO *xorriso, char *iso_adr,
                           void *in_node,
                           struct SplitparT **parts, int *count,
                           struct stat *total_stbuf, int flag);

/* @param flag bit0= node is valid, do not resolve path
               bit1= insist in complete collection of part files
*/
int Xorriso_is_split(struct XorrisO *xorriso, char *path, void *node,
                     int flag);


/* @param flag
           >>> bit0= mkdir: graft in as empty directory, not as copy from iso
               bit1= do not report copied files
               bit2= -follow, -not_*: this is not a command parameter
               bit3= use offset and cut_size for -paste_in
               bit4= return 3 on rejection by exclusion or user
               bit5= if directory then do not add sub tree
               bit6= this is a copy action: do not fake times and ownership
   @return <=0 = error , 1 = added leaf file object , 2 = added directory ,
                         3 = rejected
*/
int Xorriso_restore(struct XorrisO *xorriso,
                    char *img_path, char *disk_path,
                    off_t offset, off_t cut_size, int flag);


/* @param flag bit0= in_node is valid, do not resolve img_path
*/
int Xorriso_restore_is_identical(struct XorrisO *xorriso, void *in_node,
                                 char *img_path, char *disk_path,
                                 char type_text[5], int flag);


/* Return the official libburn address of an address string. This may fail
   if the string does not constitute a valid drive address.
   @param official_adr must offer SfileadrL bytes of reply buffer
   @return 1 = success , 0 = failure , -1 = severe error
*/
int Xorriso_libburn_adr(struct XorrisO *xorriso, char *address_string,
                        char official_adr[], int flag);


/* @param flag bit1= obtain info from outdev
*/
int Xorriso_msinfo(struct XorrisO *xorriso, int *msc1, int *msc2, int flag);

/*
   @param flag bit0= obtain iso_lba from indev
               bit1= head_buffer already contains a valid head
               bit2= issue message about success
               bit3= check whether source blocks are banned by in_sector_map
*/
int Xorriso_update_iso_lba0(struct XorrisO *xorriso, int iso_lba, int isosize,
                            char *head_buffer, struct CheckmediajoB *job,
                            int flag);

int Xorriso_get_local_charset(struct XorrisO *xorriso, char **name, int flag);

int Xorriso_set_local_charset(struct XorrisO *xorriso, char *name, int flag);

int Xorriso_destroy_node_array(struct XorrisO *xorriso, int flag);

int Xorriso_destroy_hln_array(struct XorrisO *xorriso, int flag);

int Xorriso_destroy_di_array(struct XorrisO *xorriso, int flag);

int Xorriso_new_node_array(struct XorrisO *xorriso, off_t mem_limit,
                           int addon_nodes, int flag);

int Xorriso_sort_node_array(struct XorrisO *xorriso, int flag);

int Xorriso_new_hln_array(struct XorrisO *xorriso, off_t mem_limit, int flag);

/* @param flag bit0= allocate xorriso->node_targets too
*/
int Xorriso_restore_node_array(struct XorrisO *xorriso, int flag);

int Xorriso_check_md5(struct XorrisO *xorriso, void *in_node, char *path,
                      int flag);

int Xorriso_check_session_md5(struct XorrisO *xorriso, char *severity,
                              int flag);

int Xorriso_image_has_md5(struct XorrisO *xorriso, int flag);


int Xorriso_check_media(struct XorrisO *xorriso, struct SpotlisT **spotlist,
                        struct CheckmediajoB *job, int flag);

int Xorriso_extract_cut(struct XorrisO *xorriso,
                        char *img_path, char *disk_path,
                        off_t img_offset, off_t bytes, int flag);


int Xorriso_relax_compliance(struct XorrisO *xorriso, char *mode,
                                    int flag);

/* @return 1=ok  2=ok, is default setting */
int Xorriso_get_relax_text(struct XorrisO *xorriso, char mode[1024],
                           int flag);


/**
    @param flag bit0= print mount command to result channel rather than
                      performing it 
*/
int Xorriso_mount(struct XorrisO *xorriso, char *dev, int adr_mode,
                  char *adr_value, char *cmd, int flag);



int Xorriso_auto_driveadr(struct XorrisO *xorriso, char *adr, char *result,
                          int flag);


/* @param node      Opaque handle to IsoNode which is to be inquired instead of
                    path if it is not NULL.
   @param path      is used as address if node is NULL.
   @param acl_text  if acl_text is not NULL, then *acl_text will be set to the
                    ACL text (without comments) of the file object. In this
                    case it finally has to be freed by the caller.
   @param flag      bit0= do not report to result but only retrieve ACL text
                    bit1= just check for existence of ACL, do not allocate and
                          set acl_text but return 1 or 2
   @return          2 ok, no ACL available, eventual *acl_text will be NULL
                    1 ok, ACL available, eventual *acl_text stems from malloc()
                  <=0 error
*/
int Xorriso_getfacl(struct XorrisO *xorriso, void *node,
                    char *path, char **acl_text, int flag);

int Xorriso_getfattr(struct XorrisO *xorriso, void *in_node, char *path,
                     char **attr_text, int flag);

int Xorriso_list_extattr(struct XorrisO *xorriso, void *in_node, char *path,
                         char *show_path, char *mode, int flag);

int Xorriso_append_extattr_comp(struct XorrisO *xorriso,
                                char *comp, size_t comp_len, 
                                char *mode, int flag);


/* Calls iso_image_set_ignore_aclea() according to  xorriso->do_aaip */
int Xorriso_set_ignore_aclea(struct XorrisO *xorriso, int flag);


/* @param node      Opaque handle to IsoNode which is to be manipulated
                    instead of path if it is not NULL.
   @param path      is used as address if node is NULL.
   @param access_text  "access" ACL in long text form
   @param default_text "default" ACL in long text form
   @param flag          Unused yet, submit 0
   @return          >0 success , <=0 failure
*/
int Xorriso_setfacl(struct XorrisO *xorriso, void *in_node, char *path,
                    char *access_text, char *default_text, int flag);

int Xorriso_get_attrs(struct XorrisO *xorriso, void *in_node, char *path,
                      size_t *num_attrs, char ***names,
                      size_t **value_lengths, char ***values, int flag);

int Xorriso_setfattr(struct XorrisO *xorriso, void *in_node, char *path,
                     size_t num_attrs, char **names,
                     size_t *value_lengths, char **values, int flag);

int Xorriso_perform_attr_from_list(struct XorrisO *xorriso, char *path,
                      struct Xorriso_lsT *lst_start, int flag);

int Xorriso_path_setfattr(struct XorrisO *xorriso, void *in_node, char *path,
                       char *name, size_t value_length, char *value, int flag);

int Xorriso_perform_acl_from_list(struct XorrisO *xorriso, char *file_path,
                                  char *uid, char *gid, char *acl, int flag);

int Xorriso_record_dev_inode(struct XorrisO *xorriso, char *disk_path,
                             dev_t dev, ino_t ino,
                             void *in_node, char *iso_path, int flag);

int Xorriso_local_getfacl(struct XorrisO *xorriso, char *disk_path,
                          char **text, int flag);

int Xorriso_set_filter(struct XorrisO *xorriso, void *in_node,
                       char *path, char *filter_name, int flag);

/* @param flag bit0= delete filter with the given name
*/
int Xorriso_external_filter(struct XorrisO *xorriso,
                            char *name, char *options, char *path,
                            int argc, char **argv, int flag);

int Xorriso_status_extf(struct XorrisO *xorriso, char *filter, FILE *fp,
                        int flag);

int Xorriso_destroy_all_extf(struct XorrisO *xorriso, int flag);

int Xorriso_show_stream(struct XorrisO *xorriso, void *in_node,
                        char *path, int flag);

int Xorriso_set_zisofs_params(struct XorrisO *xorriso, int flag);

int Xorriso_status_zisofs(struct XorrisO *xorriso, char *filter, FILE *fp,
                          int flag);

/* @param flag bit0= overwrite existing di_array (else return 2)
*/
int Xorriso_make_di_array(struct XorrisO *xorriso, int flag);

/* @param flag bit0= overwrite existing hln_array (else return 2)
*/
int Xorriso_make_hln_array(struct XorrisO *xorriso, int flag);

/*
   @param flag      bit2= -follow: this is not a command parameter
   @return -1= severe error
            0= not applicable for hard links
            1= go on with processing
            2= iso_rr_path is fully updated
*/
int Xorriso_hardlink_update(struct XorrisO *xorriso, int *compare_result,
                            char *disk_path, char *iso_rr_path, int flag);

int Xorriso_finish_hl_update(struct XorrisO *xorriso, int flag);

int Xorriso_get_md5(struct XorrisO *xorriso, void *in_node, char *path,
                    char md5[16], int flag);

int Xorriso_make_md5(struct XorrisO *xorriso, void *in_node, char *path,
                     int flag);

int Xorriso_md5_start(struct XorrisO *xorriso, void **ctx, int flag);

int Xorriso_md5_compute(struct XorrisO *xorriso, void *ctx,
                        char *data, int datalen, int flag);

int Xorriso_md5_end(struct XorrisO *xorriso, void **ctx, char md5[16],
                    int flag);

/* @param flag bit0=input drive
               bit1=output drive
*/
int Xorriso_drive_snooze(struct XorrisO *xorriso, int flag);

int Xorriso_is_plain_image_file(struct XorrisO *xorriso, void *in_node,
                                char *path, int flag);

int Xorriso_pvd_info(struct XorrisO *xorriso, int flag);

/* @param flag bit0= do not set hln_change_pending */
int Xorriso_set_change_pending(struct XorrisO *xorriso, int flag);

/* @param flag bit0= enable SCSI command logging to stderr */
int Xorriso_scsi_log(struct XorrisO *xorriso, int flag);

/* flag bit0= do not increment boot_count and do not reset boot parameters
        bit1= dispose attached boot images
*/
int Xorriso_attach_boot_image(struct XorrisO *xorriso, int flag);

/*
 bit0= do only report non-default settings
 bit1= do only report to fp
*/
int Xorriso_boot_image_status(struct XorrisO *xorriso, char *filter, FILE *fp,
                              int flag);

int Xorriso_add_mips_boot_file(struct XorrisO *xorriso, char *path, int flag);

int Xorriso_coordinate_system_area(struct XorrisO *xorriso, int sa_type,
                                   int options, char *cmd, int flag);




/* A pseudo file type for El-Torito bootsectors as in man 2 stat :
   For now take the highest possible value.
*/
#define Xorriso_IFBOOT S_IFMT



int Exprtest_match(struct XorrisO *xorriso, struct ExprtesT *ftest,
                   void *node_pt, char *name, char *path,
                   struct stat *boss_stbuf, struct stat *stbuf, int flag);


int Xorriso_toc_to_string(struct XorrisO *xorriso, char **toc_text, int flag);


int Xorriso_reaquire_outdev(struct XorrisO *xorriso, int flag);

int Xorriso_set_system_area_path(struct XorrisO *xorriso, char *path,
                                 int flag);

int Xorriso_set_hidden(struct XorrisO *xorriso, void *in_node, char *path,
                       int hide_state, int flag);


/* @param flag  bit0= avoid library calls
 */
int Xorriso_preparer_string(struct XorrisO *xorriso, char xorriso_id[129],
                            int flag);

int Xorriso_jigdo_interpreter(struct XorrisO *xorriso, char *aspect, char *arg,
                              int flag);


int Xorriso_estimate_file_size(struct XorrisO *xorriso, struct FindjoB *job,
                      char *basename, mode_t st_mode, off_t st_size, int flag);


int Xorriso_clone_tree(struct XorrisO *xorriso, void *boss_iter,
                       char *origin, char *dest, int flag);

int Xorriso_clone_under(struct XorrisO *xorriso, char *origin, char *dest,
                        int flag);

int Xorriso_mark_update_merge(struct XorrisO *xorriso, char *path,
                              void *node, int flag);

/* @param flag bit0= asynchronous handling (else catch thread, wait, and exit)
*/
int Xorriso_set_signal_handling(struct XorrisO *xorriso, int flag);

/* @param flag bit0=force burn_disc_close_damaged()
*/
int Xorriso_close_damaged(struct XorrisO *xorriso, int flag);

int Xorriso_list_extras(struct XorrisO *xorriso, char *mode, int flag);

int Xorriso_set_data_cache(struct XorrisO *xorriso, void *ropts,
                           int num_tiles, int tile_blocks, int flag);

int Xorriso_hfsplus_file_creator_type(struct XorrisO *xorriso, char *path,
                                      void *in_node, 
                                      char *creator, char *hfs_type, int flag); 

int Xorriso_hfsplus_bless(struct XorrisO *xorriso, char *path,
                          void *in_node, char *blessing, int flag);

int Xorriso_pretend_full_disc(struct XorrisO *xorriso, int flag);

#endif /* Xorrisoburn_includeD */

