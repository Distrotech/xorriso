
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of functions which sort and compare
   tree nodes.
*/


#ifndef Xorriso_pvt_sort_cmp_includeD
#define Xorriso_pvt_sort_cmp_includeD yes


int Xorriso__findi_sorted_ino_cmp(const void *p1, const void *p2);

int Xorriso__hln_cmp(const void *p1, const void *p2);

int Xorriso__findi_sorted_cmp(const void *p1, const void *p2);

int Xorriso__search_node(void *node_array[], int n,
                         int (*cmp)(const void *p1, const void *p2),
                         void *node, int *idx, int flag);

int Xorriso_search_in_hln_array(struct XorrisO *xorriso,
                                 void *node, int *idx, int flag);

int Xorriso__get_di(IsoNode *node, dev_t *dev, ino_t *ino, int flag);

int Xorriso__di_ino_cmp(const void *p1, const void *p2);

int Xorriso__di_cmp(const void *p1, const void *p2);

int Xorriso__sort_di(void *node_array[], int count, int flag);

int Xorriso_invalidate_di_item(struct XorrisO *xorriso, IsoNode *node,
                               int flag);

int Xorriso_search_di_range(struct XorrisO *xorriso, IsoNode *node,
                            int *idx, int *low, int *high, int flag);

int Xorriso__node_lba_cmp(const void *node1, const void *node2);

int Xorriso__node_name_cmp(const void *node1, const void *node2);

int Xorriso_sorted_node_array(struct XorrisO *xorriso,
                              IsoDir *dir_node,
                              int *nodec, IsoNode ***node_array,
                              off_t boss_mem, int flag);

int Xorriso_remake_hln_array(struct XorrisO *xorriso, int flag);

int Xorriso_make_di_array(struct XorrisO *xorriso, int flag);

int Xorriso_search_hardlinks(struct XorrisO *xorriso, IsoNode *node,
                            int *node_idx, int *min_hl, int *max_hl, int flag);


#endif /* ! Xorriso_pvt_sort_cmp_includeD */

