
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2011 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of functions which access nodes of the
   libisofs tree model.
*/


#ifndef Xorriso_pvt_iso_tree_includeD
#define Xorriso_pvt_iso_tree_includeD yes


#define LIBISO_ISDIR(node) (iso_node_get_type(node) == LIBISO_DIR)
#define LIBISO_ISREG(node) (iso_node_get_type(node) == LIBISO_FILE)
#define LIBISO_ISLNK(node) (iso_node_get_type(node) == LIBISO_SYMLINK)
#define LIBISO_ISCHR(node) (iso_node_get_type(node) == LIBISO_SPECIAL && \
                            S_ISCHR(iso_node_get_mode(node)))
#define LIBISO_ISBLK(node) (iso_node_get_type(node) == LIBISO_SPECIAL && \
                            S_ISBLK(iso_node_get_mode(node)))
#define LIBISO_ISFIFO(node) (iso_node_get_type(node) == LIBISO_SPECIAL && \
                             S_ISFIFO(iso_node_get_mode(node)))
#define LIBISO_ISSOCK(node) (iso_node_get_type(node) == LIBISO_SPECIAL && \
                             S_ISSOCK(iso_node_get_mode(node)))
#define LIBISO_ISBOOT(node) (iso_node_get_type(node) == LIBISO_BOOT)


int Xorriso_node_from_path(struct XorrisO *xorriso, IsoImage *volume,
                           char *path, IsoNode **node, int flag);

int Xorriso_get_node_by_path(struct XorrisO *xorriso,
                             char *in_path, char *eff_path,
                             IsoNode **node, int flag);

int Xorriso_dir_from_path(struct XorrisO *xorriso, char *purpose,
                          char *path, IsoDir **dir_node, int flag);

int Xorriso_node_get_dev(struct XorrisO *xorriso, IsoNode *node,
                         char *path, dev_t *dev, int flag);

int Xorriso_fake_stbuf(struct XorrisO *xorriso, char *path, struct stat *stbuf,
                       IsoNode **node, int flag);

int Xorriso_node_is_valid(struct XorrisO *xorriso, IsoNode *in_node, int flag);

int Xorriso_path_from_node(struct XorrisO *xorriso, IsoNode *in_node,
                           char path[SfileadrL], int flag);

int Xorriso_path_from_lba(struct XorrisO *xorriso, IsoNode *node, int lba,
                          char path[SfileadrL], int flag);

int Xorriso_get_attr_value(struct XorrisO *xorriso, void *in_node, char *path,
                     char *name, size_t *value_length, char **value, int flag);

int Xorriso_stream_type(struct XorrisO *xorriso, IsoNode *node,
                        IsoStream *stream, char type_text[], int flag);


int Xorriso_show_du_subs(struct XorrisO *xorriso, IsoDir *dir_node,
                      char *abs_path, char *rel_path, off_t *size,
                      off_t boss_mem, int flag);

int Xorriso_sorted_dir_i(struct XorrisO *xorriso, IsoDir *dir_node,
                         int *filec, char ***filev, off_t boss_mem, int flag);

int Xorriso_obtain_pattern_files_i(
       struct XorrisO *xorriso, char *wd, IsoDir *dir,
       int *filec, char **filev, int count_limit, off_t *mem,
       int *dive_count, int flag);

int Xorriso__start_end_lbas(IsoNode *node,
                            int *lba_count, int **start_lbas, int **end_lbas,
                            off_t *size, int flag);

int Xorriso__file_start_lba(IsoNode *node,
                           int *lba, int flag);

int Xorriso_file_eval_damage(struct XorrisO *xorriso, IsoNode *node,
                             off_t *damage_start, off_t *damage_end,
                             int flag);

int Xorriso_report_lba(struct XorrisO *xorriso, char *show_path,
                       IsoNode *node, int flag);

int Xorriso_report_damage(struct XorrisO *xorriso, char *show_path,
                          IsoNode *node, int flag);

int Xorriso_getfname(struct XorrisO *xorriso, char *path, int flag);

int Xorriso_retrieve_disk_path(struct XorrisO *xorriso, IsoNode *node,
                               char disk_path[SfileadrL], int flag);

#endif /* ! Xorriso_pvt_iso_tree_includeD */

