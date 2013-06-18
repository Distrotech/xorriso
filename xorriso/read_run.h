
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of functions which 
*/


#ifndef Xorriso_pvt_read_run_includeD
#define Xorriso_pvt_read_run_includeD yes


int Xorriso__read_pacifier(IsoImage *image, IsoFileSource *filesource);

int Xorriso_restore_properties(struct XorrisO *xorriso, char *disk_path,
                                IsoNode *node, int flag);

int Xorriso_restore_implicit_properties(struct XorrisO *xorriso,
         char *full_disk_path, char *disk_path, char *full_img_path, int flag);

int Xorriso_tree_restore_node(struct XorrisO *xorriso, IsoNode *node,
                              char *img_path, off_t img_offset,
                              char *disk_path, off_t disk_offset, off_t bytes,
                              int flag);

int Xorriso_restore_overwrite(struct XorrisO *xorriso,
                              IsoNode *node, char *img_path,
                              char *path, char *nominal_path,
                              struct stat *stbuf, int flag);

int Xorriso_restore_target_hl(struct XorrisO *xorriso, IsoNode *node,
                           char *disk_path, int *node_idx, int flag);

int Xorriso_restore_prefix_hl(struct XorrisO *xorriso, IsoNode *node,
                              char *disk_path, int node_idx, int flag);

int Xorriso_register_node_target(struct XorrisO *xorriso, int node_idx,
                                 char *disk_path, int flag);

int Xorriso_restore_disk_object(struct XorrisO *xorriso,
                                char *img_path, IsoNode *node,
                                char *disk_path,
                                off_t offset, off_t bytes, int flag);

int Xorriso_handle_collision(struct XorrisO *xorriso,
                             IsoNode *node, char *img_path,
                             char *disk_path, char *nominal_disk_path,
                             int *stbuf_ret, int flag);

int Xorriso_restore_tree(struct XorrisO *xorriso, IsoDir *dir,
                         char *img_dir_path, char *disk_dir_path,
                         off_t boss_mem,
                         struct LinkiteM *link_stack, int flag);

int Xorriso_read_file_data(struct XorrisO *xorriso, IsoNode *node,
                           char *img_path, char *disk_path,
                           off_t img_offset, off_t disk_offset,
                           off_t bytes, int flag);







#endif /* ! Xorriso_pvt_read_run_includeD */

