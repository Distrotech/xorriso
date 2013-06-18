/*
 * Copyright (c) 2012 Vladimir Serbinenko
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

/**
 * Declare HFS+ related structures.
 */

#ifndef LIBISO_HFSPLUS_H
#define LIBISO_HFSPLUS_H

#include "libisofs.h"
#include "ecma119.h"

#define LIBISO_HFSPLUS_NAME_MAX 255


enum hfsplus_node_type {
  HFSPLUS_DIR = 1,
  HFSPLUS_FILE = 2,
  HFSPLUS_DIR_THREAD = 3,
  HFSPLUS_FILE_THREAD = 4
};

struct hfsplus_btree_node
{
  uint32_t start;
  uint32_t cnt;
  uint32_t strlen;
  uint16_t *str;
  uint32_t parent_id;
};

struct hfsplus_btree_level
{
  uint32_t level_size;
  struct hfsplus_btree_node *nodes;
};

struct hfsplus_node
{
  /* Note: .type HFSPLUS_DIR_THREAD and HFSPLUS_FILE_THREAD do not own their
     .name and .cmp_name. They have copies of others, if ever.
  */
  uint16_t *name; /* Name in UTF-16BE, decomposed. */
  uint16_t *cmp_name; /* Name used for comparing.  */ 

  IsoNode *node; /*< reference to the iso node */

  enum { UNIX_NONE, UNIX_SYMLINK, UNIX_SPECIAL } unix_type;
  uint32_t symlink_block;
  char *symlink_dest;

  enum hfsplus_node_type type;
  IsoFileSrc *file;
  uint32_t cat_id;
  uint32_t parent_id;
  uint32_t nchildren;

  uint32_t strlen;
  uint32_t used_size;
};

int hfsplus_writer_create(Ecma119Image *target);
int hfsplus_tail_writer_create(Ecma119Image *target);

struct hfsplus_extent
{
  /* The first block of a file on disk.  */
  uint32_t start;
  /* The amount of blocks described by this extent.  */
  uint32_t count;
} __attribute__ ((packed));

struct hfsplus_forkdata
{
  uint64_t size;
  uint32_t clumpsize;
  uint32_t blocks;
  struct hfsplus_extent extents[8];
} __attribute__ ((packed));

struct hfsplus_volheader
{
  uint16_t magic;
  uint16_t version;
  uint32_t attributes;
  uint32_t last_mounted_version;
  uint32_t journal;
  uint32_t ctime;
  uint32_t utime;
  uint32_t backup_time;
  uint32_t fsck_time;
  uint32_t file_count;
  uint32_t folder_count;
  uint32_t blksize;
  uint32_t total_blocks;
  uint32_t free_blocks;
  uint32_t next_allocation;
  uint32_t rsrc_clumpsize;
  uint32_t data_clumpsize;
  uint32_t catalog_node_id;
  uint32_t write_count;
  uint64_t encodings_bitmap;
  uint32_t ppc_bootdir;
  uint32_t intel_bootfile;
  /* Folder opened when disk is mounted.  */
  uint32_t showfolder;
  uint32_t os9folder;
  uint32_t unused;
  uint32_t osxfolder;
  uint64_t num_serial;
  struct hfsplus_forkdata allocations_file;
  struct hfsplus_forkdata extents_file;
  struct hfsplus_forkdata catalog_file;
  struct hfsplus_forkdata attrib_file;
  struct hfsplus_forkdata startup_file;
} __attribute__ ((packed));

struct hfsplus_btnode
{
  uint32_t next;
  uint32_t prev;
  int8_t type;
  uint8_t height;
  uint16_t count;
  uint16_t unused;
} __attribute__ ((packed));

/* The header of a HFS+ B+ Tree.  */
struct hfsplus_btheader
{
  uint16_t depth;
  uint32_t root;
  uint32_t leaf_records;
  uint32_t first_leaf_node;
  uint32_t last_leaf_node;
  uint16_t nodesize;
  uint16_t keysize;
  uint32_t total_nodes;
  uint32_t free_nodes;
  uint16_t reserved1;
  uint32_t clump_size;
  uint8_t btree_type;
  uint8_t key_compare;
  uint32_t attributes;
  uint32_t reserved[16];
} __attribute__ ((packed));

struct hfsplus_catfile_thread
{
  uint16_t type;
  uint16_t reserved;
  uint32_t parentid;
  uint16_t namelen;
} __attribute__ ((packed));

struct hfsplus_catfile_common
{
  uint16_t type;
  uint16_t flags;
  uint32_t valence; /* for files: reserved.  */
  uint32_t fileid;
  uint32_t ctime;
  uint32_t mtime;
  uint32_t attr_mtime;
  uint32_t atime;
  uint32_t backup_time;
  uint32_t uid;
  uint32_t gid;
  uint8_t user_flags;
  uint8_t group_flags;
  uint16_t mode;
  uint32_t special; 
  uint8_t file_type[4]; /* For folders: window size */
  uint8_t file_creator[4]; /* For folders: window size */
  uint8_t finder_info[24];
  uint32_t text_encoding;
  uint32_t reserved;
} __attribute__ ((packed));

#define HFSPLUS_MAX_DECOMPOSE_LEN 4

extern uint16_t (*hfsplus_decompose_pages[256])[HFSPLUS_MAX_DECOMPOSE_LEN + 1];
void make_hfsplus_decompose_pages();

extern uint16_t *hfsplus_class_pages[256];
void make_hfsplus_class_pages();

extern const uint16_t hfsplus_casefold[];

#endif /* LIBISO_HFSPLUS_H */
