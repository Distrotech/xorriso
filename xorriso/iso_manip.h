
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of functions which manipulate the
   libisofs tree model.
*/


#ifndef Xorriso_pvt_iso_manip_includeD
#define Xorriso_pvt_iso_manip_includeD yes


int Xorriso_transfer_properties(struct XorrisO *xorriso, struct stat *stbuf,
                               char *disk_path,  IsoNode *node, int flag);

int Xorriso_graft_split(struct XorrisO *xorriso, IsoImage *volume,
                        IsoDir *dir, char *disk_path, char *img_name,
                        char *nominal_source, char *nominal_target,
                        off_t size, IsoNode **node, int flag);

int Xorriso_tree_graft_node(struct XorrisO *xorriso, IsoImage *volume,
                            IsoDir *dir, char *disk_path, char *img_name,
                            char *nominal_source, char *nominal_target,
                            off_t offset, off_t cut_size,
                            IsoNode **node, int flag);

int Xorriso_add_tree(struct XorrisO *xorriso, IsoDir *dir,
                     char *img_dir_path, char *disk_dir_path,
                     struct LinkiteM *link_stack, int flag);

int Xorriso_copy_implicit_properties(struct XorrisO *xorriso, IsoDir *dir,
          char *full_img_path, char *img_path, char *full_disk_path, int flag);

int Xorriso_mkisofs_lower_r(struct XorrisO *xorriso, IsoNode *node, int flag);

int Xorriso_widen_hardlink(struct XorrisO *xorriso, void * boss_iter,
                           IsoNode *node,
                           char *abs_path, char *iso_prefix, char *disk_prefix,
                           int flag);


int Xorriso_cannot_create_iter(struct XorrisO *xorriso, int iso_error,
                               int flag);

int Xorriso_findi_iter(struct XorrisO *xorriso, IsoDir *dir_node, off_t *mem,
                       IsoDirIter **iter,
                       IsoNode ***node_array, int *node_count, int *node_idx,
                       IsoNode **iterated_node, int flag);

int Xorriso_findi_action(struct XorrisO *xorriso, struct FindjoB *job,
                         IsoDirIter *boss_iter, off_t boss_mem,
                         char *abs_path, char *show_path,
                         IsoNode *node, int depth, int flag);

int Xorriso_findi_headline(struct XorrisO *xorriso, struct FindjoB *job,
                           int flag);

int Xorriso_findi_sorted(struct XorrisO *xorriso, struct FindjoB *job,
                         off_t boss_mem, int filec, char **filev, int flag);

int Xorriso_all_node_array(struct XorrisO *xorriso, int addon_nodes, int flag);


int Xorriso__file_start_lba(IsoNode *node, int *lba, int flag);

int Xorriso__mark_update_xinfo(void *data, int flag);
int Xorriso__mark_update_cloner(void *old_data, void **new_data, int flag);

int Xorriso_get_blessing(struct XorrisO *xorriso, IsoNode *node,
                         int *bless_idx, char bless_code[17], int flag);

#endif /* ! Xorriso_pvt_iso_manip_includeD */

