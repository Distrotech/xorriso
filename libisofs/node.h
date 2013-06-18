/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2009 - 2011 Thomas Schmitt
 *
 * This file is part of the libisofs project; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */
#ifndef LIBISO_NODE_H_
#define LIBISO_NODE_H_

/*
 * Definitions for the public iso tree
 */

#include "libisofs.h"
#include "stream.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif


/* Maximum length of a leaf name in the libisofs node tree. This is currently
   restricted by the implemented maximum length of a Rock Ridge name.
   This might later become larger and may then be limited to smaller values.

   Rock Ridge specs do not impose an explicit limit on name length.
   But 255 is also specified by
     http://pubs.opengroup.org/onlinepubs/009695399/basedefs/limits.h.html
   which says
     NAME_MAX >= _XOPEN_NAME_MAX = 255 
*/
#define LIBISOFS_NODE_NAME_MAX 255


/* Maximum length of a path in the libisofs node tree.
   Rock Ridge specs do not impose an explicit limit on path length.

     http://pubs.opengroup.org/onlinepubs/009695399/basedefs/limits.h.html
   says
     PATH_MAX >= _XOPEN_PATH_MAX = 1024
*/
#define LIBISOFS_NODE_PATH_MAX 1024


/**
 * The extended information is a way to attach additional information to each
 * IsoNode. External applications may want to use this extension system to
 * store application specific information related to each node. On the other
 * side, libisofs may make use of this struct to attach information to nodes in
 * some particular, uncommon, cases, without incrementing the size of the
 * IsoNode struct.
 *
 * It is implemented like a chained list.
 */
typedef struct iso_extended_info IsoExtendedInfo;

struct iso_extended_info {
    /**
     * Next struct in the chain. NULL if it is the last item
     */
    IsoExtendedInfo *next;

    /**
     * Function to handle this particular extended information. The function
     * pointer acts as an identifier for the type of the information. Structs
     * with same information type must use the same function.
     *
     * @param data
     *     Attached data
     * @param flag
     *     What to do with the data. At this time the following values are
     *     defined:
     *      -> 1 the data must be freed
     * @return
     *     1
     */
    iso_node_xinfo_func process;

    /**
     * Pointer to information specific data.
     */
    void *data;
};

/**
 *
 */
struct Iso_Node
{
    /*
     * Initialized to 1, originally owned by user, until added to another node.
     * Then it is owned by the parent node, so the user must take his own ref
     * if needed. With the exception of the creation functions, none of the
     * other libisofs functions that return an IsoNode increment its
     * refcount. This is responsablity of the client, if (s)he needs it.
     */
    int refcount;

    /** Type of the IsoNode, do not confuse with mode */
    enum IsoNodeType type;

    char *name; /**< Real name, in default charset */

    mode_t mode; /**< protection */
    uid_t uid; /**< user ID of owner */
    gid_t gid; /**< group ID of owner */

    /* TODO #00001 : consider adding new timestamps */
    time_t atime; /**< time of last access */
    time_t mtime; /**< time of last modification */
    time_t ctime; /**< time of last status change */

    int hidden; /**< whether the node will be hidden, see IsoHideNodeFlag */

    IsoDir *parent; /**< parent node, NULL for root */

    /*
     * Pointer to the linked list of children in a dir.
     */
    IsoNode *next;

    /**
     * Extended information for the node.
     */
    IsoExtendedInfo *xinfo;
};

struct Iso_Dir
{
    IsoNode node;

    size_t nchildren; /**< The number of children of this directory. */
    IsoNode *children; /**< list of children. ptr to first child */
};

/* IMPORTANT: Any change must be reflected by iso_tree_clone_file. */
struct Iso_File
{
    IsoNode node;

    unsigned int from_old_session : 1;

    /**
     * It sorts the order in which the file data is written to the CD image.
     * Higher weighting files are written at the beginning of image
     */
    int sort_weight;
    IsoStream *stream;                    /* Knows fs_id, st_dev, and st_ino */
};

struct Iso_Symlink
{
    IsoNode node;

    char *dest;

    /* If the IsoNode represents an object in an existing filesystem then
       the following three numbers should unique identify it.
       (0,0,0) will always be taken as unique.
     */
    unsigned int fs_id;
    dev_t st_dev;
    ino_t st_ino;
};

struct Iso_Special
{
    IsoNode node;
    dev_t dev;

    /* If the IsoNode represents an object in an existing filesystem then
       the following three numbers should unique identify it.
       (0,0,0) will always be taken as unique.
     */
    unsigned int fs_id;
    dev_t st_dev;
    ino_t st_ino;
};

struct iso_dir_iter_iface
{

    int (*next)(IsoDirIter *iter, IsoNode **node);

    int (*has_next)(IsoDirIter *iter);

    void (*free)(IsoDirIter *iter);

    int (*take)(IsoDirIter *iter);

    int (*remove)(IsoDirIter *iter);

    /**
     * This is called just before remove a node from a directory. The iterator
     * may want to update its internal state according to this.
     */
    void (*notify_child_taken)(IsoDirIter *iter, IsoNode *node);
};

/**
 * An iterator for directory children.
 */
struct Iso_Dir_Iter
{
    struct iso_dir_iter_iface *class;

    /* the directory this iterator iterates over */
    IsoDir *dir;

    void *data;
};

int iso_node_new_root(IsoDir **root);

/**
 * Create a new IsoDir. Attributes, uid/gid, timestamps, etc are set to
 * default (0) values. You must set them.
 *
 * @param name
 *      Name for the node. It is not strdup() so you shouldn't use this
 *      reference when this function returns successfully. NULL is not
 *      allowed.
 * @param dir
 *
 * @return
 *      1 on success, < 0 on error.
 */
int iso_node_new_dir(char *name, IsoDir **dir);

/**
 * Create a new file node. Attributes, uid/gid, timestamps, etc are set to
 * default (0) values. You must set them.
 *
 * @param name
 *      Name for the node. It is not strdup() so you shouldn't use this
 *      reference when this function returns successfully. NULL is not
 *      allowed.
 * @param stream
 *      Source for file contents. The reference is taken by the node,
 *      you must call iso_stream_ref() if you need your own ref.
 * @return
 *      1 on success, < 0 on error.
 */
int iso_node_new_file(char *name, IsoStream *stream, IsoFile **file);

/**
 * Creates a new IsoSymlink node. Attributes, uid/gid, timestamps, etc are set
 * to default (0) values. You must set them.
 *
 * @param name
 *      name for the new symlink. It is not strdup() so you shouldn't use this
 *      reference when this function returns successfully. NULL is not
 *      allowed.
 * @param dest
 *      destination of the link. It is not strdup() so you shouldn't use this
 *      reference when this function returns successfully. NULL is not
 *      allowed.
 * @param link
 *      place where to store a pointer to the newly created link.
 * @return
 *     1 on success, < 0 otherwise
 */
int iso_node_new_symlink(char *name, char *dest, IsoSymlink **link);

/**
 * Create a new special file node. As far as libisofs concerns,
 * an special file is a block device, a character device, a FIFO (named pipe)
 * or a socket. You can choose the specific kind of file you want to add
 * by setting mode propertly (see man 2 stat).
 *
 * Note that special files are only written to image when Rock Ridge
 * extensions are enabled. Moreover, a special file is just a directory entry
 * in the image tree, no data is written beyond that.
 *
 * Owner and hidden atts are taken from parent. You can modify any of them
 * later.
 *
 * @param name
 *      name for the new special file. It is not strdup() so you shouldn't use
 *      this reference when this function returns successfully. NULL is not
 *      allowed.
 * @param mode
 *      file type and permissions for the new node. Note that you can't
 *      specify any kind of file here, only special types are allowed. i.e,
 *      S_IFSOCK, S_IFBLK, S_IFCHR and S_IFIFO are valid types; S_IFLNK,
 *      S_IFREG and S_IFDIR aren't.
 * @param dev
 *      device ID, equivalent to the st_rdev field in man 2 stat.
 * @param special
 *      place where to store a pointer to the newly created special file.
 * @return
 *     1 on success, < 0 otherwise
 */
int iso_node_new_special(char *name, mode_t mode, dev_t dev,
                         IsoSpecial **special);

/**
 * Check if a given name is valid for an iso node.
 *
 * @return
 *     1 if yes, <0 if not. The value is a specific ISO_* error code.
 */
int iso_node_is_valid_name(const char *name);

/**
 * Check if a given path is valid for the destination of a link.
 *
 * @return
 *     1 if yes, 0 if not
 */
int iso_node_is_valid_link_dest(const char *dest);

/**
 * Find the position where to insert a node
 *
 * @param dir
 *      A valid dir. It can't be NULL
 * @param name
 *      The node name to search for. It can't be NULL
 * @param pos
 *      Will be filled with the position where to insert. It can't be NULL
 */
void iso_dir_find(IsoDir *dir, const char *name, IsoNode ***pos);

/**
 * Check if a node with the given name exists in a dir.
 *
 * @param dir
 *      A valid dir. It can't be NULL
 * @param name
 *      The node name to search for. It can't be NULL
 * @param pos
 *      If not NULL, will be filled with the position where to insert. If the
 *      node exists, (**pos) will refer to the given node.
 * @return
 *      1 if node exists, 0 if not
 */
int iso_dir_exists(IsoDir *dir, const char *name, IsoNode ***pos);

/**
 * Inserts a given node in a dir, at the specified position.
 *
 * @param dir
 *     Dir where to insert. It can't be NULL
 * @param node
 *     The node to insert. It can't be NULL
 * @param pos
 *     Position where the node will be inserted. It is a pointer previously
 *     obtained with a call to iso_dir_exists() or iso_dir_find().
 *     It can't be NULL.
 * @param replace
 *     Whether to replace an old node with the same name with the new node.
 * @return
 *     If success, number of children in dir. < 0 on error
 */
int iso_dir_insert(IsoDir *dir, IsoNode *node, IsoNode **pos,
                   enum iso_replace_mode replace);

/**
 * Add a new iterator to the registry. The iterator register keeps track of
 * all iterators being used, and are notified when directory structure
 * changes.
 */
int iso_dir_iter_register(IsoDirIter *iter);

/**
 * Unregister a directory iterator.
 */
void iso_dir_iter_unregister(IsoDirIter *iter);

void iso_notify_dir_iters(IsoNode *node, int flag);


/**
 * See API function iso_node_set_permissions()
 *
 * @param flag  bit0= do not adjust ACL
 * @return      >0 success , <0 error
 */
int iso_node_set_perms_internal(IsoNode *node, mode_t mode, int flag);


/**
 * Like iso_node_get_acl_text() with param node replaced by aa_string and
 * st_mode from where to obtain the ACLs. All other parameter specs apply.
 */
int iso_aa_get_acl_text(unsigned char *aa_string, mode_t st_mode,
                        char **access_text, char **default_text, int flag);

/**
 * Backend of iso_node_get_attrs() with parameter node replaced by the
 * AAIP string from where to get the attribute list.
 * All other parameter specs apply.
 */
int iso_aa_get_attrs(unsigned char *aa_string, size_t *num_attrs,
              char ***names, size_t **value_lengths, char ***values, int flag);

/**
 * Search given name. Eventually calloc() and copy value. Add trailing 0 byte
 * for caller convenience.
 *
 * @return 1= found , 0= not found , <0 error
 */
int iso_aa_lookup_attr(unsigned char *aa_string, char *name,
                       size_t *value_length, char **value, int flag);


/**
 * Function to identify and manage ZF parameters which do not stem from ZF
 * fields (those are known to the FileSource) and do not stem from filters
 * ("ziso" knows them globally, "osiz" knows them individually) but rather
 * from an inspection of the file content header for zisofs magic number and
 * plausible parameters.
 * The parameters get attached in struct zisofs_zf_info as xinfo to an IsoNode.
 */
int zisofs_zf_xinfo_func(void *data, int flag);

/**
 * Parameter structure which is to be managed by zisofs_zf_xinfo_func.
 */
struct zisofs_zf_info {
    uint32_t uncompressed_size;
    uint8_t header_size_div4;
    uint8_t block_size_log2;
};

/**
 * Checks whether a file effectively bears a zisofs file header and eventually
 * marks this by a struct zisofs_zf_info as xinfo of the file node.
 * @param flag bit0= inquire the most original stream of the file
 *             bit1= permission to overwrite existing zisofs_zf_info
 *             bit2= if no zisofs header is found:
                     create xinfo with parameters which indicate no zisofs
 * @return 1= zf xinfo added, 0= no zisofs data found ,
 *         2= found existing zf xinfo and flag bit1 was not set
 *         <0 means error
 */ 
int iso_file_zf_by_magic(IsoFile *file, int flag);

/*
 * @param flag
 *     bit0= do only retrieve id if node is in imported ISO image
 *           or has an explicit xinfo inode number
 * @return
 *     1= reply is valid from stream, 2= reply is valid from xinfo
 *     0= no id available,           <0= error
 *     (fs_id, dev_id, ino_id) will be (0,0,0) in case of return <= 0
 */
int iso_node_get_id(IsoNode *node, unsigned int *fs_id, dev_t *dev_id,
                    ino_t *ino_id, int flag);

/* Set a new unique inode ISO image number to the given node.
 * This number shall eventually persist during image generation.
 */
int iso_node_set_unique_id(IsoNode *node, IsoImage *image, int flag);

/* Use this with extreme care. Duplicate inode numbers will indicate hardlink
 * relationship between the nodes.
 */
int iso_node_set_ino(IsoNode *node, ino_t ino, int flag);

/*  
 * @param flag 
 *     bit0= compare stat properties and attributes
 *     bit1= treat all nodes with image ino == 0 as unique
 *           (those with 0,0,0 are treated as unique anyway)
 */
int iso_node_cmp_flag(IsoNode *n1, IsoNode *n2, int flag);


/**
 * Set the checksum index (typically comming from IsoFileSrc.checksum_index)
 * of a regular file node. The index is encoded as xattr "isofs.cx" with
 * four bytes of value.
 */
int iso_file_set_isofscx(IsoFile *file, unsigned int checksum_index,
                         int flag);


/**
 * Set the checksum area description. node should be the root node.
 * It is encoded as xattr "isofs.ca".
 */
int iso_root_set_isofsca(IsoNode *node, uint32_t start_lba, uint32_t end_lba,
                         uint32_t count, uint32_t size, char *typetext,
                         int flag);

/**
 * Get the checksum area description. node should be the root node.
 * It is encoded as xattr "isofs.ca".
 */
int iso_root_get_isofsca(IsoNode *node, uint32_t *start_lba, uint32_t *end_lba,
                         uint32_t *count, uint32_t *size, char typetext[81],
                         int flag);


/**
 * Copy the xinfo list from one node to the another.
 */
int iso_node_clone_xinfo(IsoNode *from_node, IsoNode *to_node, int flag);


/**
 * The iso_node_xinfo_func instance which governs the storing of the inode
 * number from Rock Ridge field PX.
 */
int iso_px_ino_xinfo_func(void *data, int flag);

/* The iso_node_xinfo_cloner function which gets associated to
 * iso_px_ino_xinfo_func by iso_init() resp. iso_init_with_flag() via
 * iso_node_xinfo_make_clonable()
 */
int iso_px_ino_xinfo_cloner(void *old_data, void **new_data, int flag);


/* Function to identify and manage ZF parameters of zisofs compression.
 * data is supposed to be a pointer to struct zisofs_zf_info
 */
int zisofs_zf_xinfo_func(void *data, int flag);

/* The iso_node_xinfo_cloner function which gets associated to
 * zisofs_zf_xinfo_func by iso_init() resp. iso_init_with_flag() via
 * iso_node_xinfo_make_clonable()
 */
int zisofs_zf_xinfo_cloner(void *old_data, void **new_data, int flag);


#endif /*LIBISO_NODE_H_*/
