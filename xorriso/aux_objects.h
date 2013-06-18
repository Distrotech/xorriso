
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of classes:

   - SplitparT which represents byte intervals of data files.

   - DirseQ which crawls along a directory's content list.

   - ExclusionS which manages the list of excluded file paths and 
     leaf patterns.

   - Xorriso_lsT which provides a generic double-linked list.

   - LinkiteM, PermiteM which temporarily record relations and states.

*/


#ifndef Xorriso_pvt_auxobj_includeD
#define Xorriso_pvt_auxobj_includeD yes

struct SplitparT;

int Splitparts_new(struct SplitparT **o, int count, int flag);

int Splitparts_destroy(struct SplitparT **o, int count, int flag);

int Splitparts_set(struct SplitparT *o, int idx,
                   char *name, int partno, int total_parts,
                   off_t offset, off_t bytes, off_t total_bytes, int flag);

int Splitparts_get(struct SplitparT *o, int idx, char **name, int *partno,
                   int *total_parts, off_t *offset, off_t *bytes, 
                   off_t *total_bytes, int flag);

int Splitpart__parse(char *name, int *partno, int *total_parts,
                    off_t *offset, off_t *bytes, off_t *total_bytes, int flag);

int Splitpart__is_part_path(char *path, int flag);

int Splitpart__compose(char *adr, int partno, int total_parts,
                       off_t offset, off_t bytes, off_t total_bytes, int flag);

int Splitpart__read_next_num(char *base_pt, char **next_pt, off_t *num,
                             int flag);

int Splitparts_sort(struct SplitparT *o, int count, int flag);



struct DirseQ;

int Dirseq_new(struct DirseQ **o, char *adr, int flag);

int Dirseq_destroy(struct DirseQ **o, int flag);

int Dirseq_next_adr(struct DirseQ *o, char reply[SfileadrL], int flag);

int Dirseq_rewind(struct DirseQ *o, int flag);



struct Xorriso_lsT {
  char *text;
  struct Xorriso_lsT *prev,*next;
};

/** Create a new list item with arbitrary byte content.
    @param lstring  The newly created object or NULL on failure
    @param data     An array of bytes to be copied into the new object
    @param data_len Number of bytes to be copied
    @param link     Xorriso_lsT object to which the new object shall be linked
    @param flag Bitfield for control purposes
                bit0= insert before link rather than after it
                bit1= do not copy data (e.g. because *data is invalid)
                bit2= attach data directly by pointer rather than by copying
    @return <=0 error, 1 ok
*/
int Xorriso_lst_new_binary(struct Xorriso_lsT **lstring, char *data,
                           int data_len, struct Xorriso_lsT *link, int flag);


/** Create a new list item with a 0-terminated text as content.
    @param lstring  The newly created object or NULL on failure
    @param text     A 0-terminated array of bytes
    @param link     Xorriso_lsT object to which the new object shall be linked
    @param flag     see Xorriso_lst_new_binary
    @return <=0 error, 1 ok
*/
int Xorriso_lst_new(struct Xorriso_lsT **lstring, char *text,
                    struct Xorriso_lsT *link, int flag);


/** Create a new list item at the end of a given list.
    @param entry    Contains as input a pointer to a pointer to any existing
                    list item. As output this list item pointer may be
                    changed to the address of the new list item:
                    if ((*entry == 0) || (flag & 1))
    @param data     An array of bytes to be copied into the new object
    @param data_len Number of bytes to be copied
    @param flag     Bitfield for control purposes
                    bit0= Return new object address in *entry
                    bit1= do not copy data (e.g. because *data is invalid)
                    bit2= attach data directly by pointer rather than by copying
                    bit2= attach data directly by pointer rather than by copying
    @return <=0 error, 1 ok
*/
int Xorriso_lst_append_binary(struct Xorriso_lsT **entry,
                              char *data, int data_len, int flag);


/** Destroy a single list item and connect its eventual list neighbors.
    @param lstring  pointer to the pointer to be freed and set to NULL
    @param flag     unused yet, submit 0
    @return 0= *lstring was alredy NULL, 1= ok
*/
int Xorriso_lst_destroy(struct Xorriso_lsT **lstring, int flag);


struct Xorriso_lsT *Xorriso_lst_get_next(struct Xorriso_lsT *entry, int flag);

struct Xorriso_lsT *Xorriso_lst_get_prev(struct Xorriso_lsT *entry, int flag);

char *Xorriso_lst_get_text(struct Xorriso_lsT *entry, int flag);

int Xorriso_lst_detach_text(struct Xorriso_lsT *entry, int flag);

int Xorriso_lst_get_last(struct Xorriso_lsT *entry, struct Xorriso_lsT **last,
                         int flag);

int Xorriso_lst_concat(struct Xorriso_lsT *first, struct Xorriso_lsT *second,
                       int flag);


int Exclusions_new(struct ExclusionS **o, int flag);

int Exclusions_destroy(struct ExclusionS **o, int flag);

int Exclusions_get_descrs(struct ExclusionS *o,
                          struct Xorriso_lsT **not_paths_descr,
                          struct Xorriso_lsT **not_leafs_descr, int flag);

/* @param flag bit0= whole subtree is banned with -not_paths
   @return 0=no match , 1=not_paths , 2=not_leafs, <0=error
*/
int Exclusions_match(struct ExclusionS *o, char *abs_path, int flag);

int Exclusions_add_not_leafs(struct ExclusionS *o, char *not_leafs_descr,
                             regex_t *re, int flag);

int Exclusions_add_not_paths(struct ExclusionS *o, int descrc, char **descrs,
                             int pathc, char **paths, int flag);



struct LinkiteM;          /* Trace of hops during symbolic link resolution */

int Linkitem_new(struct LinkiteM **o, char *link_path, dev_t target_dev,
                 ino_t target_ino, struct LinkiteM *next, int flag);

int Linkitem_destroy(struct LinkiteM **o, int flag);

int Linkitem_reset_stack(struct LinkiteM **o, struct LinkiteM *to, int flag);

int Linkitem_find(struct LinkiteM *stack, dev_t target_dev, ino_t target_ino,
                  struct LinkiteM **result, int flag);

int Linkitem_get_link_count(struct LinkiteM *item, int flag);


struct PermiteM;          /* Stack of temporarily altered access permissions */

int Permstack_push(struct PermiteM **o, char *disk_path, struct stat *stbuf,
                   int flag);

int Permstack_pop(struct PermiteM **o, struct PermiteM *stopper,
                  struct XorrisO *xorriso, int flag);


#endif /* ! Xorriso_pvt_auxobj_includeD */

