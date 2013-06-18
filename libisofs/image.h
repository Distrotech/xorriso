/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2009 - 2012 Thomas Schmitt
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */
#ifndef LIBISO_IMAGE_H_
#define LIBISO_IMAGE_H_

#include "libisofs.h"
#include "node.h"
#include "fsource.h"
#include "builder.h"

/* Size of a inode recycling window. Each new window causes a tree traversal.
   Window memory consumption is ISO_USED_INODE_RANGE / 8.
   This must be a power of 2 smaller than 30 bit and larger than 8 bit.
   Here: 32 kB memory for 256k inodes.
*/
#define ISO_USED_INODE_RANGE (1 << 18)


/*
 * Image is a context for image manipulation.
 * Global objects such as the message_queues must belogn to that
 * context. Thus we will have, for example, a msg queue per image,
 * so images are completelly independent and can be managed together.
 * (Usefull, for example, in Multiple-Document-Interface GUI apps.
 * [The stuff we have in init belongs really to image!]
 */

struct Iso_Image
{

    int refcount;

    IsoDir *root;

    char *volset_id;

    char *volume_id; /**< Volume identifier. */
    char *publisher_id; /**< Volume publisher. */
    char *data_preparer_id; /**< Volume data preparer. */
    char *system_id; /**< Volume system identifier. */
    char *application_id; /**< Volume application id */
    char *copyright_file_id;
    char *abstract_file_id;
    char *biblio_file_id;
    char *creation_time;
    char *modification_time;
    char *expiration_time;
    char *effective_time;
    
    /* el-torito boot catalog */
    struct el_torito_boot_catalog *bootcat;

    /* Eventually loaded system area data, or NULL */
    char *system_area_data;
    /* Prescribed/detected options, see iso_write_opts_set_system_area() */
    int system_area_options;

   /*
    * Up to 15 boot files can be referred by a MIPS Big Endian Volume Header.
      The mips_boot_file_paths are ISO 9660 Rock Ridge paths.
    */
    int num_mips_boot_files;
    char *mips_boot_file_paths[15]; /* ISO 9660 Rock Ridge Paths */

    /* A data file of which the position and size shall be written after
       a SUN Disk Label.
    */
    IsoFile *sparc_core_node;

    /* image identifier, for message origin identifier */
    int id;

    /**
     * Default filesystem to use when adding files to the image tree.
     */
    IsoFilesystem *fs;

    /*
     * Default builder to use when adding files to the image tree.
     */
    IsoNodeBuilder *builder;

    /**
     * Whether to follow symlinks or just add them as symlinks
     */
    unsigned int follow_symlinks : 1;

    /**
     * Whether to skip hidden files
     */
    unsigned int ignore_hidden : 1;

    /**
     * Flags that determine what special files should be ignore. It is a
     * bitmask:
     * bit0: ignore FIFOs
     * bit1: ignore Sockets
     * bit2: ignore char devices
     * bit3: ignore block devices
     */
    int ignore_special;

    /**
     * Whether to ignore ACL when inserting nodes into the image.
     * Not in effect with loading a complete ISO image but only with image
     * manipulation.
     */
    unsigned int builder_ignore_acl : 1;

    /**
     * Whether to ignore EAs when inserting nodes into the image.
     * Not in effect with loading a complete ISO image but only with image
     * manipulation. ACL does not count as EA.
     */
    unsigned int builder_ignore_ea : 1;

    /**
     * Files to exclude. Wildcard support is included.
     */
    char** excludes;
    int nexcludes;

    /**
     * if the dir already contains a node with the same name, whether to
     * replace or not the old node with the new. 
     */
    enum iso_replace_mode replace;

    /* TODO
    enum iso_replace_mode (*confirm_replace)(IsoFileSource *src, IsoNode *node);
    */
    
    /**
     * When this is not NULL, it is a pointer to a function that will
     * be called just before a file will be added. You can control where
     * the file will be in fact added or ignored.
     * 
     * @return
     *      1 add, 0 ignore, < 0 cancel
     */
    int (*report)(IsoImage *image, IsoFileSource *src);

    /**
     * User supplied data
     */
    void *user_data;
    void (*user_data_free)(void *ptr);

    /**
     * Inode number management. inode_counter is taken over from
     * IsoImageFilesystem._ImageFsData after image import.
     * It is to be used with img_give_ino_number()
     */
    ino_t inode_counter;
    /*
     * A bitmap of used inode numbers in an interval beginning at
     * used_inodes_start and holding ISO_USED_INODE_RANGE bits.
     * If a bit is set, then the corresponding inode number is occupied.
     * This interval is kept around inode_counter and eventually gets
     * advanced by ISO_USED_INODE_RANGE numbers in a tree traversal
     * done by img_collect_inos().
     */
    uint8_t *used_inodes;
    ino_t used_inodes_start;

    /**
     * Array of MD5 checksums as announced by xattr "isofs.ca" of the 
     * root node. Array element 0 contains an overall image checksum for the
     * block range checksum_start_lba,checksum_end_lba. Element size is
     * 16 bytes. IsoFile objects in the image may have xattr "isofs.cx"
     * which gives their index in checksum_array.
     */
    uint32_t checksum_start_lba;
    uint32_t checksum_end_lba;
    uint32_t checksum_idx_count;
    char *checksum_array;

    /**
     * Whether a write run has been started by iso_image_create_burn_source()
     * and has not yet been finished.
     */
    int generator_is_running;

    /* Pointers to directories or files which shall be get a HFS+ blessing.
     * libisofs/hfsplus.c et.al. will compare these pointers
     * with the ->node pointer of Ecma119Nodes.
     * See libisofs.h
     */
    IsoNode *hfsplus_blessed[ISO_HFSPLUS_BLESS_MAX];

};


    
/* Collect the bitmap of used inode numbers in the range of
   _ImageFsData.used_inodes_start + ISO_USED_INODE_RANGE
   @param flag bit0= recursion is active
*/
int img_collect_inos(IsoImage *image, IsoDir *dir, int flag);

/**
 * A global counter for inode numbers for the ISO image filesystem.
 * On image import it gets maxed by the eventual inode numbers from PX
 * entries. Up to the first 32 bit rollover it simply increments the counter.
 * After the first rollover it uses a look ahead bitmap which gets filled
 * by a full tree traversal. It covers the next inode numbers to come
 * (somewhere between 1 and ISO_USED_INODE_RANGE which is quite many)
 * and advances when being exhausted.
 * @param image The image where the number shall be used
 * @param flag  bit0= reset count (Caution: image must get new inos then)
 * @return
 *     Since ino_t 0 is used as default and considered self-unique, 
 *     the value 0 should only be returned in case of error.
 */ 
ino_t img_give_ino_number(IsoImage *image, int flag);

/* @param flag bit0= overwrite any ino, else only ino == 0
               bit1= install inode with non-data, non-directory files
               bit2= install inode with directories
               bit3= with bit2: install inode on parameter dir
*/
int img_make_inos(IsoImage *image, IsoDir *dir, int flag);


/* Free the checksum array of an image and reset its layout parameters
*/
int iso_image_free_checksums(IsoImage *image, int flag);


/* Equip an ISO image with a new checksum array buffer (after isofs.ca and
   isofs.cx have already been adjusted).
*/
int iso_image_set_checksums(IsoImage *image, char *checksum_array, 
                            uint32_t start_lba, uint32_t end_lba,
                            uint32_t idx_count, int flag);


int iso_image_set_pvd_times(IsoImage *image,
                            char *creation_time, char *modification_time,
                            char *expiration_time, char *effective_time);

#endif /*LIBISO_IMAGE_H_*/
