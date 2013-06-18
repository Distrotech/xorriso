/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2007 Mario Danic
 * Copyright (c) 2011-2012 Thomas Schmitt
 * Copyright (c) 2012 Vladimir Serbinenko
 *
 * This file is part of the libisofs project; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */


/* Some extra debugging messages for Vladimir Serbinenko
 #define Libisofs_hfsplus_verbose_debuG yes
*/

/* Some extra debugging messages for Thomas Schmitt
*/
#define Libisofs_ts_debuG yes


#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "hfsplus.h"
#include "messages.h"
#include "writer.h"
#include "image.h"
#include "filesrc.h"
#include "eltorito.h"
#include "libisofs.h"
#include "util.h"
#include "ecma119.h"
#include "system_area.h"


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* To be used if Ecma119.hfsplus_block_size == 0 in hfsplus_writer_create().
   It cannot be larger than 2048 because filesrc_writer aligns data file
   content start to 2048.
*/
#define HFSPLUS_DEFAULT_BLOCK_SIZE 2048

/* To be used with storage allocation.
*/
#define HFSPLUS_MAX_BLOCK_SIZE 2048


/* In libisofs/hfsplus_case.c */
extern uint16_t iso_hfsplus_cichar(uint16_t x);


/* ts B20623: pad up output block to full 2048 bytes */
static
int pad_up_block(Ecma119Image *t)
{
    int ret;
    static char buffer[2048], buf_zeroed = 0;

    if (!buf_zeroed) {
        memset(buffer, 0, 2048);
        buf_zeroed = 1;
    }
    if (t->bytes_written % 2048) {
	ret = iso_write(t, buffer, 2048 - (t->bytes_written % 2048));
	if (ret < 0)
	    return ret;
    }
    return 1;
}


static
int filesrc_block_and_size(Ecma119Image *t, IsoFileSrc *src,
                           uint32_t *start_block, uint64_t *total_size)
{
    int i;
    uint32_t pos;

    *start_block = 0;
    *total_size = 0;
    if (src->nsections <= 0)
        return 0;
    pos = *start_block = src->sections[0].block;
    for (i = 0; i < src->nsections; i++) {
        *total_size += src->sections[i].size;
        if (pos != src->sections[i].block) {
            iso_msg_submit(t->image->id, ISO_SECT_SCATTERED, 0,
                      "File sections do not form consequtive array of blocks");
            return ISO_SECT_SCATTERED;
        }
        /* If .size is not aligned to blocks then there is a byte gap.
           No need to trace the exact byte address.
        */
        pos = src->sections[i].block + src->sections[i].size / 2048;
    }
    return 1;
}

static
uint8_t get_class (uint16_t v)
{
  uint16_t s;
  uint8_t high, low;
  s = iso_ntohs (v);
  high = s >> 8;
  low = v & 0xff;
  if (!hfsplus_class_pages[high])
    return 0;
  return hfsplus_class_pages[high][low];
}

static
int set_hfsplus_name(Ecma119Image *t, char *name, HFSPlusNode *node)
{
    int ret;
    uint16_t *ucs_name, *iptr, *optr;
    uint32_t curlen;
    int done;

    if (name == NULL) {
        /* it is not necessarily an error, it can be the root */
        return ISO_SUCCESS;
    }

    ret = str2ucs(t->input_charset, name, &ucs_name);
    if (ret < 0) {
        iso_msg_debug(t->image->id, "Can't convert %s", name);
        return ret;
    }

    curlen = ucslen (ucs_name);
    node->name = calloc ((curlen * HFSPLUS_MAX_DECOMPOSE_LEN + 1),
			 sizeof (node->name[0]));
    if (!node->name)
      return ISO_OUT_OF_MEM;

    for (iptr = ucs_name, optr = node->name; *iptr; iptr++)
      {
	const uint16_t *dptr;
	uint16_t val = iso_ntohs (*iptr);
	uint8_t high = val >> 8;
	uint8_t low = val & 0xff;

	if (val == ':')
	  {
	    *optr++ = iso_htons ('/');
	    continue;
	  }

	if (val >= 0xac00 && val <= 0xd7a3)
	  {
	    uint16_t s, l, v, t;
	    s = val - 0xac00;
	    l = s / (21 * 28);
	    v = (s % (21 * 28)) / 28;
	    t = s % 28;
	    *optr++ = iso_htons (l + 0x1100);
	    *optr++ = iso_htons (v + 0x1161);
	    if (t)
	      *optr++ = iso_htons (t + 0x11a7);
	    continue;
	  }
	if (!hfsplus_decompose_pages[high])
	  {
	    *optr++ = *iptr;
	    continue;
	  }
	dptr = hfsplus_decompose_pages[high][low];
	if (!dptr[0])
	  {
	    *optr++ = *iptr;
	    continue;
	  }
	for (; *dptr; dptr++)
	  *optr++ = iso_htons (*dptr);
      }
    *optr = 0;

    do
      {
	uint8_t last_class;
	done = 0;
	if (!ucs_name[0])
	  break;
	last_class = get_class (ucs_name[0]);
	for (optr = node->name + 1; *optr; optr++)
	  {
	    uint8_t new_class = get_class (*optr);

	    if (last_class == 0 || new_class == 0
		|| last_class <= new_class)
	      last_class = new_class;
	    else
	      {
		uint16_t t;
		t = *(optr - 1);
		*(optr - 1) = *optr;
		*optr = t;
	      }
	  }
      }
    while (done);

    node->cmp_name = calloc ((ucslen (node->name) + 1), sizeof (node->cmp_name[0]));
    if (!node->cmp_name)
      return ISO_OUT_OF_MEM;

    for (iptr = node->name, optr = node->cmp_name; *iptr; iptr++)
      {
	*optr = iso_hfsplus_cichar(*iptr);
	if (*optr != 0)
	    optr++;
      }
    *optr = 0;

    free (ucs_name);

    node->strlen = ucslen (node->name);
    return ISO_SUCCESS;
}


/* >>> ts B20617
       This should be HFSPlusNode rather than IsoNode in order to have access
       to IsoFileSrc.no_write which indicates that the file content will not
       be in written the range of filesrc_writer.
*/
static
int hfsplus_count_tree(Ecma119Image *t, IsoNode *iso)
{
    if (t == NULL || iso == NULL) {
        return ISO_NULL_POINTER;
    }

    if (iso->hidden & LIBISO_HIDE_ON_HFSPLUS) {
        /* file will be ignored */
        return 0;
    }

    switch (iso->type) {
    case LIBISO_SYMLINK:
    case LIBISO_SPECIAL:
    case LIBISO_FILE:
      t->hfsp_nfiles++;
      return ISO_SUCCESS;
    case LIBISO_DIR:
      t->hfsp_ndirs++;
      {
	IsoNode *pos;
	IsoDir *dir = (IsoDir*)iso;
	pos = dir->children;
	while (pos) {
	  int cret;
	  cret = hfsplus_count_tree(t, pos);
	  if (cret < 0) {
	    /* error */
	    return cret;
	  }
	  pos = pos->next;
	}
      }
      return ISO_SUCCESS;
    case LIBISO_BOOT:
      return ISO_SUCCESS;
    default:
      /* should never happen */
      return ISO_ASSERT_FAILURE;
    }
}

/**
 * Create the low level Hfsplus tree from the high level ISO tree.
 *
 * @return
 *      1 success, 0 file ignored, < 0 error
 */
static
int create_tree(Ecma119Image *t, IsoNode *iso, uint32_t parent_id)
{
    int ret;
    uint32_t cat_id, cleaf;
    int i;

    if (t == NULL || iso == NULL) {
        return ISO_NULL_POINTER;
    }

    if (iso->hidden & LIBISO_HIDE_ON_HFSPLUS) {
        /* file will be ignored */
        return 0;
    }

    if (iso->type != LIBISO_FILE && iso->type != LIBISO_DIR
	&& iso->type != LIBISO_SYMLINK && iso->type != LIBISO_SPECIAL)
      return 0;

    cat_id = t->hfsp_cat_id++;

    for (i = 0; i < ISO_HFSPLUS_BLESS_MAX; i++)
      if (t->hfsplus_blessed[i] == iso) {

#ifdef Libisofs_ts_debuG
    iso_msg_debug(t->image->id, "hfsplus bless %d to cat_id %u ('%s')",
                                i, cat_id, iso->name);
#endif /* Libisofs_ts_debuG */

	t->hfsp_bless_id[i] = cat_id;
      }

    t->hfsp_leafs[t->hfsp_curleaf].node = iso;
    t->hfsp_leafs[t->hfsp_curleaf].parent_id = parent_id;
    ret = set_hfsplus_name (t, iso->name, &t->hfsp_leafs[t->hfsp_curleaf]);
    if (ret < 0)
        return ret;
    t->hfsp_leafs[t->hfsp_curleaf].cat_id = cat_id;
    t->hfsp_leafs[t->hfsp_curleaf].unix_type = UNIX_NONE;
    t->hfsp_leafs[t->hfsp_curleaf].symlink_dest = NULL;

    switch (iso->type)
      {
      case LIBISO_SYMLINK:
	{
	  IsoSymlink *sym = (IsoSymlink*) iso;
	  t->hfsp_leafs[t->hfsp_curleaf].type = HFSPLUS_FILE;
	  t->hfsp_leafs[t->hfsp_curleaf].symlink_dest = strdup(sym->dest);
	  if (t->hfsp_leafs[t->hfsp_curleaf].symlink_dest == NULL)
	      return ISO_OUT_OF_MEM;
	  t->hfsp_leafs[t->hfsp_curleaf].unix_type = UNIX_SYMLINK;
	  t->hfsp_leafs[t->hfsp_curleaf].used_size = t->hfsp_leafs[t->hfsp_curleaf].strlen * 2 + 8 + 2 + sizeof (struct hfsplus_catfile_common) + 2 * sizeof (struct hfsplus_forkdata);
	  break;
	}
      case LIBISO_SPECIAL:
	t->hfsp_leafs[t->hfsp_curleaf].unix_type = UNIX_SPECIAL;
	t->hfsp_leafs[t->hfsp_curleaf].type = HFSPLUS_FILE;
	t->hfsp_leafs[t->hfsp_curleaf].used_size = t->hfsp_leafs[t->hfsp_curleaf].strlen * 2 + 8 + 2 + sizeof (struct hfsplus_catfile_common) + 2 * sizeof (struct hfsplus_forkdata);
	break;

      case LIBISO_FILE:
	{
	  IsoFile *file = (IsoFile*) iso;
	  t->hfsp_leafs[t->hfsp_curleaf].type = HFSPLUS_FILE;
	  ret = iso_file_src_create(t, file, &t->hfsp_leafs[t->hfsp_curleaf].file);
	  if (ret < 0) {
            return ret;
	  }
	  t->hfsp_leafs[t->hfsp_curleaf].used_size = t->hfsp_leafs[t->hfsp_curleaf].strlen * 2 + 8 + 2 + sizeof (struct hfsplus_catfile_common) + 2 * sizeof (struct hfsplus_forkdata);
	}
	break;
      case LIBISO_DIR:
	{
	  t->hfsp_leafs[t->hfsp_curleaf].type = HFSPLUS_DIR;
	  t->hfsp_leafs[t->hfsp_curleaf].used_size = t->hfsp_leafs[t->hfsp_curleaf].strlen * 2 + 8 + 2 + sizeof (struct hfsplus_catfile_common);
	  break;
	}
      default:
	return ISO_ASSERT_FAILURE;
      }
    cleaf = t->hfsp_curleaf;
    t->hfsp_leafs[t->hfsp_curleaf].nchildren = 0;
    t->hfsp_curleaf++;

    t->hfsp_leafs[t->hfsp_curleaf].name = t->hfsp_leafs[t->hfsp_curleaf - 1].name;
    t->hfsp_leafs[t->hfsp_curleaf].cmp_name = NULL;
    t->hfsp_leafs[t->hfsp_curleaf].strlen = t->hfsp_leafs[t->hfsp_curleaf - 1].strlen;
    t->hfsp_leafs[t->hfsp_curleaf].used_size = t->hfsp_leafs[t->hfsp_curleaf].strlen * 2 + 8 + 2 + sizeof (struct hfsplus_catfile_thread);
    t->hfsp_leafs[t->hfsp_curleaf].node = iso;
    t->hfsp_leafs[t->hfsp_curleaf].type = (iso->type == LIBISO_DIR) ? HFSPLUS_DIR_THREAD : HFSPLUS_FILE_THREAD;
    t->hfsp_leafs[t->hfsp_curleaf].file = 0;
    t->hfsp_leafs[t->hfsp_curleaf].cat_id = parent_id;
    t->hfsp_leafs[t->hfsp_curleaf].parent_id = cat_id;
    t->hfsp_leafs[t->hfsp_curleaf].unix_type = UNIX_NONE;
    t->hfsp_curleaf++;

    if (iso->type == LIBISO_DIR)
      {
	IsoNode *pos;
	IsoDir *dir = (IsoDir*)iso;

	pos = dir->children;
	while (pos)
	  {
	    int cret;
	    cret = create_tree(t, pos, cat_id);
	    if (cret < 0)
	      return cret;
	    pos = pos->next;
	    t->hfsp_leafs[cleaf].nchildren++;
	  }
      }
    return ISO_SUCCESS;
}

static int
cmp_node(const void *f1, const void *f2)
{
  HFSPlusNode *f = (HFSPlusNode*) f1;
  HFSPlusNode *g = (HFSPlusNode*) f2;
  const uint16_t empty[1] = {0};
  const uint16_t *a, *b; 
  if (f->parent_id > g->parent_id)
    return +1;
  if (f->parent_id < g->parent_id)
    return -1;
  a = f->cmp_name;
  b = g->cmp_name;
  if (!a)
    a = empty;
  if (!b)
    b = empty;

  return ucscmp(a, b);
}


static
int hfsplus_tail_writer_compute_data_blocks(IsoImageWriter *writer)
{
  Ecma119Image *t;
  uint32_t hfsp_size, hfsp_curblock, block_fac, block_size;
    
  if (writer == NULL) {
    return ISO_OUT_OF_MEM;
  }

  t = writer->target;
  block_size = t->hfsp_block_size;
  block_fac = t->hfsp_iso_block_fac;

#ifdef Libisofs_ts_debuG
  iso_msg_debug(t->image->id, "hfsplus tail writer start = %.f",
                ((double) t->curblock) * 2048.0);
#endif

  hfsp_curblock = t->curblock * block_fac;
  hfsp_size = hfsp_curblock - t->hfsp_part_start + 1;

  /* We need one bit for every block. */
  /* So if we allocate x blocks we have to satisfy:
     8 * block_size * x >= total_size + x
     (8 * block_size - 1) * x >= total_size
  */
  t->hfsp_allocation_blocks = hfsp_size / (8 * block_size - 1) + 1;
  t->hfsp_allocation_file_start = hfsp_curblock;
  hfsp_curblock += t->hfsp_allocation_blocks;

  /* write_data() will need to pad up ISO block before superblock copy */
  t->curblock = hfsp_curblock / block_fac;
  if (hfsp_curblock % block_fac)
      t->curblock++;
  hfsp_curblock = t->curblock * block_fac;

  /* Superblock always occupies 2K */
  hfsp_curblock += block_fac;
  t->curblock++;

#ifdef Libisofs_ts_debuG
  iso_msg_debug(t->image->id, "hfsplus tail writer end = %.f",
                ((double) hfsp_curblock) * block_size);
#endif

  t->hfsp_total_blocks = hfsp_curblock - t->hfsp_part_start;

  return iso_quick_apm_entry(t, t->hfsp_part_start / block_fac,
			     t->hfsp_total_blocks / block_fac +
			     !!(t->hfsp_total_blocks % block_fac),
                            "HFSPLUS_Hybrid", "Apple_HFS");
}


static
int hfsplus_writer_compute_data_blocks(IsoImageWriter *writer)
{
    Ecma119Image *t;
    uint32_t i, link_blocks, hfsp_curblock;
    uint32_t block_fac, cat_node_size, block_size;

    if (writer == NULL) {
        return ISO_OUT_OF_MEM;
    }

    t = writer->target;
    block_size = t->hfsp_block_size;
    block_fac = t->hfsp_iso_block_fac;
    cat_node_size = t->hfsp_cat_node_size;

    iso_msg_debug(t->image->id, "(b) curblock=%d, nodes =%d", t->curblock, t->hfsp_nnodes);
    t->hfsp_part_start = t->curblock * block_fac;

    hfsp_curblock = t->curblock * block_fac;

    /* Superblock always occupies 2K */
    hfsp_curblock += block_fac;

    t->hfsp_catalog_file_start = hfsp_curblock;

/* 
    hfsp_curblock += (t->hfsp_nnodes * cat_node_size + block_size - 1) / block_size;
*/
    hfsp_curblock += 2 * t->hfsp_nnodes;

    t->hfsp_extent_file_start = hfsp_curblock;
    hfsp_curblock++;

    iso_msg_debug(t->image->id, "(d) hfsp_curblock=%d, nodes =%d", hfsp_curblock, t->hfsp_nnodes);

    link_blocks = 0;
    for (i = 0; i < t->hfsp_nleafs; i++)
      if (t->hfsp_leafs[i].unix_type == UNIX_SYMLINK)
	{
          t->hfsp_leafs[i].symlink_block = hfsp_curblock;
          hfsp_curblock += (strlen(t->hfsp_leafs[i].symlink_dest) +
                            block_size - 1) / block_size;
	}

    t->curblock = hfsp_curblock / block_fac;
    if (hfsp_curblock % block_fac)
        t->curblock++;

    iso_msg_debug(t->image->id, "(a) curblock=%d, nodes =%d", t->curblock, t->hfsp_nnodes);

    return ISO_SUCCESS;
}


static void set_time (uint32_t *tm, uint32_t t)
{
  iso_msb ((uint8_t *) tm, t + 2082844800, 4);
}

int nop_writer_write_vol_desc(IsoImageWriter *writer)
{
  return ISO_SUCCESS;
}

static
uid_t px_get_uid(Ecma119Image *t, IsoNode *n)
{
    if (t->replace_uid) {
        return t->uid;
    } else {
        return n->uid;
    }
}

static
uid_t px_get_gid(Ecma119Image *t, IsoNode *n)
{
    if (t->replace_gid) {
        return t->gid;
    } else {
        return n->gid;
    }
}

static
mode_t px_get_mode(Ecma119Image *t, IsoNode *n, int isdir)
{
    if (isdir) {
        if (t->replace_dir_mode) {
            return (n->mode & S_IFMT) | t->dir_mode;
        }
    } else {
        if (t->replace_file_mode) {
            return (n->mode & S_IFMT) | t->file_mode;
        }
    }
    return n->mode;
}

int
write_sb (Ecma119Image *t)
{
    struct hfsplus_volheader sb;
    static char buffer[1024];
    int ret;
    int i;
    uint32_t block_size;

    iso_msg_debug(t->image->id, "Write HFS+ superblock");

    block_size = t->hfsp_block_size;

    memset (buffer, 0, sizeof (buffer));
    ret = iso_write(t, buffer, 1024);
    if (ret < 0)
      return ret;

    memset (&sb, 0, sizeof (sb));

    t->hfsp_allocation_size = (t->hfsp_total_blocks + 7) >> 3;

    iso_msb ((uint8_t *) &sb.magic, 0x482b, 2);
    iso_msb ((uint8_t *) &sb.version, 4, 2);
    /* Cleanly unmounted, software locked.  */
    iso_msb ((uint8_t *) &sb.attributes, (1 << 8) | (1 << 15), 4);
    iso_msb ((uint8_t *) &sb.last_mounted_version, 0x6c69736f, 4);
    set_time (&sb.ctime, t->now);
    set_time (&sb.utime, t->now);
    set_time (&sb.fsck_time, t->now);
    iso_msb ((uint8_t *) &sb.file_count, t->hfsp_nfiles, 4);
    iso_msb ((uint8_t *) &sb.folder_count, t->hfsp_ndirs - 1, 4);
    iso_msb ((uint8_t *) &sb.blksize, block_size, 4);
    iso_msb ((uint8_t *) &sb.catalog_node_id, t->hfsp_cat_id, 4);
    iso_msb ((uint8_t *) &sb.rsrc_clumpsize, block_size, 4);
    iso_msb ((uint8_t *) &sb.data_clumpsize, block_size, 4);
    iso_msb ((uint8_t *) &sb.total_blocks, t->hfsp_total_blocks, 4);
    iso_msb ((uint8_t *) &sb.encodings_bitmap + 4, 1, 4);

    iso_msb ((uint8_t *) &sb.allocations_file.size + 4, t->hfsp_allocation_size, 4);
    iso_msb ((uint8_t *) &sb.allocations_file.clumpsize, block_size, 4);
    iso_msb ((uint8_t *) &sb.allocations_file.blocks, (t->hfsp_allocation_size + block_size - 1) / block_size, 4);
    iso_msb ((uint8_t *) &sb.allocations_file.extents[0].start, t->hfsp_allocation_file_start - t->hfsp_part_start, 4);
    iso_msb ((uint8_t *) &sb.allocations_file.extents[0].count, (t->hfsp_allocation_size + block_size - 1) / block_size, 4);

    iso_msb ((uint8_t *) &sb.extents_file.size + 4, block_size, 4);
    iso_msb ((uint8_t *) &sb.extents_file.clumpsize, block_size, 4);
    iso_msb ((uint8_t *) &sb.extents_file.blocks, 1, 4);
    iso_msb ((uint8_t *) &sb.extents_file.extents[0].start, t->hfsp_extent_file_start - t->hfsp_part_start, 4);
    iso_msb ((uint8_t *) &sb.extents_file.extents[0].count, 1, 4);
    iso_msg_debug(t->image->id, "extent_file_start = %d\n", (int)t->hfsp_extent_file_start);

    iso_msb ((uint8_t *) &sb.catalog_file.size + 4, block_size * 2 * t->hfsp_nnodes, 4);
    iso_msb ((uint8_t *) &sb.catalog_file.clumpsize, block_size * 2, 4);
    iso_msb ((uint8_t *) &sb.catalog_file.blocks, 2 * t->hfsp_nnodes, 4);
    iso_msb ((uint8_t *) &sb.catalog_file.extents[0].start, t->hfsp_catalog_file_start - t->hfsp_part_start, 4);
    iso_msb ((uint8_t *) &sb.catalog_file.extents[0].count, 2 * t->hfsp_nnodes, 4);
    iso_msg_debug(t->image->id, "catalog_file_start = %d\n", (int)t->hfsp_catalog_file_start);

    for (i = 0; i < ISO_HFSPLUS_BLESS_MAX; i++) {
     iso_msb ((uint8_t *) (&sb.ppc_bootdir + i
			   + (i == ISO_HFSPLUS_BLESS_OSX_FOLDER)),
	      t->hfsp_bless_id[i], 4);

#ifdef Libisofs_ts_debuG
     iso_msg_debug(t->image->id, "hfsplus bless %d written for cat_id %u",
                   i, t->hfsp_bless_id[i]);
#endif /* Libisofs_ts_debuG */

    }

    memcpy (&sb.num_serial, &t->hfsp_serial_number, 8);
    ret = iso_write(t, &sb, sizeof (sb));
    if (ret < 0)
      return ret;
    return iso_write(t, buffer, 512);
}

static
int hfsplus_writer_write_data(IsoImageWriter *writer)
{
    int ret;
    static char buffer[2 * HFSPLUS_MAX_BLOCK_SIZE];
    Ecma119Image *t;
    struct hfsplus_btnode *node_head;
    struct hfsplus_btheader *tree_head;
    int level;
    uint32_t curpos = 1, i, block_fac, cat_node_size, block_size;

    if (writer == NULL) {
        return ISO_NULL_POINTER;
    }

    t = writer->target;
    block_size = t->hfsp_block_size;
    block_fac = t->hfsp_iso_block_fac;
    cat_node_size = t->hfsp_cat_node_size;

    iso_msg_debug(t->image->id, "(b) %d written", (int) t->bytes_written / 0x800);

    ret = write_sb (t);
    if (ret < 0)
      return ret;

    iso_msg_debug(t->image->id, "(c) %d written", (int) t->bytes_written / 0x800);

    iso_msg_debug(t->image->id, "real catalog_file_start = %d\n", (int)t->bytes_written / 2048);

    memset (buffer, 0, sizeof (buffer));
    node_head = (struct hfsplus_btnode *) buffer;
    node_head->type = 1;
    iso_msb ((uint8_t *) &node_head->count, 3, 2);
    tree_head = (struct hfsplus_btheader *) (node_head + 1);
    iso_msb ((uint8_t *) &tree_head->depth, t->hfsp_nlevels, 2);
    iso_msb ((uint8_t *) &tree_head->root, 1, 4);
    iso_msb ((uint8_t *) &tree_head->leaf_records, t->hfsp_nleafs, 4);
    iso_msb ((uint8_t *) &tree_head->first_leaf_node, t->hfsp_nnodes - t->hfsp_levels[0].level_size, 4);
    iso_msb ((uint8_t *) &tree_head->last_leaf_node, t->hfsp_nnodes - 1, 4);
    iso_msb ((uint8_t *) &tree_head->nodesize, cat_node_size, 2);
    iso_msb ((uint8_t *) &tree_head->keysize, 6 + 2 * LIBISO_HFSPLUS_NAME_MAX, 2);
    iso_msb ((uint8_t *) &tree_head->total_nodes, t->hfsp_nnodes, 4);
    iso_msb ((uint8_t *) &tree_head->free_nodes, 0, 4);
    iso_msb ((uint8_t *) &tree_head->clump_size, cat_node_size, 4);
    tree_head->key_compare = 0xcf;
    iso_msb ((uint8_t *) &tree_head->attributes, 2 | 4, 4);
    memset (buffer + 0xf8, -1, t->hfsp_nnodes / 8);
    buffer[0xf8 + (t->hfsp_nnodes / 8)] = 0xff00 >> (t->hfsp_nnodes % 8);

    buffer[cat_node_size - 1] = sizeof (*node_head);
    buffer[cat_node_size - 3] = sizeof (*node_head) + sizeof (*tree_head);
    buffer[cat_node_size - 5] = (char) 0xf8;
    buffer[cat_node_size - 7] = (char) ((cat_node_size - 8) & 0xff);
    buffer[cat_node_size - 8] = (cat_node_size - 8) >> 8;

#ifdef Libisofs_hfsplus_verbose_debuG
    iso_msg_debug(t->image->id, "Write\n");
#endif

    ret = iso_write(t, buffer, cat_node_size);
    if (ret < 0)
        return ret;

    for (level = t->hfsp_nlevels - 1; level > 0; level--)
      {
	uint32_t i;
	uint32_t next_lev = curpos + t->hfsp_levels[level].level_size;
	for (i = 0; i < t->hfsp_levels[level].level_size; i++)
	  {
	    uint32_t curoff;
	    uint32_t j;
	    uint32_t curnode = t->hfsp_levels[level].nodes[i].start;
	    memset (buffer, 0, sizeof (buffer));
	    node_head = (struct hfsplus_btnode *) buffer;
	    if (i != t->hfsp_levels[level].level_size - 1)
	      iso_msb ((uint8_t *) &node_head->next, curpos + i + 1, 4);
	    if (i != 0)
	      iso_msb ((uint8_t *) &node_head->prev, curpos + i - 1, 4);
	    node_head->type = 0;
	    node_head->height = level + 1;
	    iso_msb ((uint8_t *) &node_head->count, t->hfsp_levels[level].nodes[i].cnt, 2);
	    curoff = sizeof (struct hfsplus_btnode);
	    for (j = 0; j < t->hfsp_levels[level].nodes[i].cnt; j++)
	      {
		iso_msb ((uint8_t *) buffer + cat_node_size - j * 2 - 2, curoff, 2);

		iso_msb ((uint8_t *) buffer + curoff, 2 * t->hfsp_levels[level - 1].nodes[curnode].strlen + 6, 2);
		iso_msb ((uint8_t *) buffer + curoff + 2, t->hfsp_levels[level - 1].nodes[curnode].parent_id, 4);
		iso_msb ((uint8_t *) buffer + curoff + 6, t->hfsp_levels[level - 1].nodes[curnode].strlen, 2);
		curoff += 8;
		memcpy ((uint8_t *) buffer + curoff, t->hfsp_levels[level - 1].nodes[curnode].str, 2 * t->hfsp_levels[level - 1].nodes[curnode].strlen);
		curoff += 2 * t->hfsp_levels[level - 1].nodes[curnode].strlen;
		iso_msb ((uint8_t *) buffer + curoff, next_lev + curnode, 4);
		curoff += 4;
		curnode++;
	      }
	    iso_msb ((uint8_t *) buffer +  cat_node_size - j * 2 - 2, curoff, 2);

#ifdef Libisofs_hfsplus_verbose_debuG
	    iso_msg_debug(t->image->id, "Write\n");
#endif

	    ret = iso_write(t, buffer, cat_node_size);

	    if (ret < 0)
	      return ret;
	  }
	curpos = next_lev;
      }

    {
      uint32_t i;
      uint32_t next_lev = curpos + t->hfsp_levels[level].level_size;
      for (i = 0; i < t->hfsp_levels[level].level_size; i++)
	{
	  uint32_t curoff;
	  uint32_t j;
	  uint32_t curnode = t->hfsp_levels[level].nodes[i].start;
	  memset (buffer, 0, sizeof (buffer));
	  node_head = (struct hfsplus_btnode *) buffer;
	  if (i != t->hfsp_levels[level].level_size - 1)
	    iso_msb ((uint8_t *) &node_head->next, curpos + i + 1, 4);
	  if (i != 0)
	    iso_msb ((uint8_t *) &node_head->prev, curpos + i - 1, 4);
	  node_head->type = -1;
	  node_head->height = level + 1;
	  iso_msb ((uint8_t *) &node_head->count, t->hfsp_levels[level].nodes[i].cnt, 2);
	  curoff = sizeof (struct hfsplus_btnode);
	  for (j = 0; j < t->hfsp_levels[level].nodes[i].cnt; j++)
	    {
	      iso_msb ((uint8_t *) buffer + cat_node_size - j * 2 - 2, curoff, 2);

#ifdef Libisofs_hfsplus_verbose_debuG

	      if (t->hfsp_leafs[curnode].node->name == NULL)
		{
		  iso_msg_debug(t->image->id, "%d out of %d",
				(int) curnode, t->hfsp_nleafs);
		}
	      else
		{
		  iso_msg_debug(t->image->id, "%d out of %d, %s",
				(int) curnode, t->hfsp_nleafs,
				t->hfsp_leafs[curnode].node->name);
		}

#endif /* Libisofs_hfsplus_verbose_debuG */

	      switch (t->hfsp_leafs[curnode].type)
		{
		case HFSPLUS_FILE_THREAD:
		case HFSPLUS_DIR_THREAD:
		  {
		    struct hfsplus_catfile_thread *thread;
		    iso_msb ((uint8_t *) buffer + curoff, 6, 2);
		    iso_msb ((uint8_t *) buffer + curoff + 2, t->hfsp_leafs[curnode].parent_id, 4);
		    iso_msb ((uint8_t *) buffer + curoff + 6, 0, 2);
		    curoff += 8;
		    thread = (struct hfsplus_catfile_thread *) (buffer + curoff);
		    ((uint8_t *) &thread->type)[1] = t->hfsp_leafs[curnode].type;
		    iso_msb ((uint8_t *) &thread->parentid, t->hfsp_leafs[curnode].cat_id, 4);
		    iso_msb ((uint8_t *) &thread->namelen, t->hfsp_leafs[curnode].strlen, 2);
		    curoff += sizeof (*thread);
		    memcpy (buffer + curoff, t->hfsp_leafs[curnode].name, t->hfsp_leafs[curnode].strlen * 2);
		    curoff += t->hfsp_leafs[curnode].strlen * 2;
		    break;
		  }
		case HFSPLUS_FILE:
		case HFSPLUS_DIR:
		  {
		    struct hfsplus_catfile_common *common;
		    struct hfsplus_forkdata *data_fork;
		    iso_msb ((uint8_t *) buffer + curoff, 6 + 2 * t->hfsp_leafs[curnode].strlen, 2);
		    iso_msb ((uint8_t *) buffer + curoff + 2, t->hfsp_leafs[curnode].parent_id, 4);
		    iso_msb ((uint8_t *) buffer + curoff + 6, t->hfsp_leafs[curnode].strlen, 2);
		    curoff += 8;
		    memcpy (buffer + curoff, t->hfsp_leafs[curnode].name, t->hfsp_leafs[curnode].strlen * 2);
		    curoff += t->hfsp_leafs[curnode].strlen * 2;

		    common = (struct hfsplus_catfile_common *) (buffer + curoff);
		    ((uint8_t *) &common->type)[1] = t->hfsp_leafs[curnode].type;
		    iso_msb ((uint8_t *) &common->valence, t->hfsp_leafs[curnode].nchildren, 4);
		    iso_msb ((uint8_t *) &common->fileid, t->hfsp_leafs[curnode].cat_id, 4);
		    set_time (&common->ctime, t->hfsp_leafs[curnode].node->ctime);
		    set_time (&common->mtime, t->hfsp_leafs[curnode].node->mtime);
		    /* FIXME: distinguish attr_mtime and mtime.  */
		    set_time (&common->attr_mtime, t->hfsp_leafs[curnode].node->mtime);
		    set_time (&common->atime, t->hfsp_leafs[curnode].node->atime);

		    iso_msb ((uint8_t *) &common->uid, px_get_uid (t, t->hfsp_leafs[curnode].node), 4);
		    iso_msb ((uint8_t *) &common->gid, px_get_gid (t, t->hfsp_leafs[curnode].node), 4);
		    iso_msb ((uint8_t *) &common->mode, px_get_mode (t, t->hfsp_leafs[curnode].node, (t->hfsp_leafs[curnode].type == HFSPLUS_DIR)), 2);

		    /*
		      FIXME:
		      uint8_t user_flags;
		      uint8_t group_flags;

		      finder info
		    */
		    if (t->hfsp_leafs[curnode].type == HFSPLUS_FILE)
		      {
			if (t->hfsp_leafs[curnode].unix_type == UNIX_SYMLINK)
			  {
			    memcpy (common->file_type, "slnk", 4);
			    memcpy (common->file_creator, "rhap", 4);
			  }
			else
			  {
			    struct iso_hfsplus_xinfo_data *xinfo;
			    ret = iso_node_get_xinfo(t->hfsp_leafs[curnode].node,
						     iso_hfsplus_xinfo_func,
						     (void *) &xinfo);
			    if (ret > 0)
			      {
				memcpy (common->file_type, xinfo->type_code,
					4);
				memcpy (common->file_creator,
					xinfo->creator_code, 4);

#ifdef Libisofs_ts_debuG
{
char crtp[14];

crtp[0] = '\'';
memcpy(crtp+1, xinfo->creator_code, 4);
strcpy(crtp + 5, "','");
memcpy(crtp + 8, xinfo->type_code, 4);
crtp[12] = '\'';
crtp[13]= 0;
iso_msg_debug(t->image->id,
        "hfsplus creator,type %s to '%s/%s'",
        crtp, ((IsoNode *) t->hfsp_leafs[curnode].node->parent)->name,
        t->hfsp_leafs[curnode].node->name);
}
#endif /* Libisofs_ts_debuG */

			      }
			    else if (ret < 0)
			      return ret;
			    else 
			      {
				memcpy (common->file_type, "????", 4);
				memcpy (common->file_creator, "????", 4);
			      }
			  }

			if (t->hfsp_leafs[curnode].unix_type == UNIX_SPECIAL
			    && (S_ISBLK(t->hfsp_leafs[curnode].node->mode)
				|| S_ISCHR(t->hfsp_leafs[curnode].node->mode)))
			  iso_msb ((uint8_t *) &common->special,
				   (((IsoSpecial*) t->hfsp_leafs[curnode].node)->dev & 0xffffffff),
				   4);

			iso_msb ((uint8_t *) &common->flags, 2, 2);
		      }
		    else if (t->hfsp_leafs[curnode].type == HFSPLUS_DIR)
		      {
			iso_msb ((uint8_t *) &common->flags, 0, 2);
		      }
		    curoff += sizeof (*common);
		    if (t->hfsp_leafs[curnode].type == HFSPLUS_FILE)
		      {
			uint64_t sz;
			uint32_t blk;
			data_fork = (struct hfsplus_forkdata *) (buffer + curoff);

			if (t->hfsp_leafs[curnode].unix_type == UNIX_SYMLINK)
			  {
			    blk = t->hfsp_leafs[curnode].symlink_block;
			    sz = strlen(t->hfsp_leafs[curnode].symlink_dest);
			  }
			else if (t->hfsp_leafs[curnode].unix_type == UNIX_SPECIAL)
			  {
			    blk = 0;
			    sz = 0;
			  }
			else
			  {
			    ret = filesrc_block_and_size(t,
							 t->hfsp_leafs[curnode].file,
							 &blk, &sz);
			    if (ret <= 0)
			      return ret;
			    blk *= block_fac;
			  }
			if (sz == 0)
			  blk = t->hfsp_part_start;
			iso_msb ((uint8_t *) &data_fork->size, sz >> 32, 4);
			iso_msb ((uint8_t *) &data_fork->size + 4, sz, 4);
			iso_msb ((uint8_t *) &data_fork->clumpsize, block_size, 4);
			iso_msb ((uint8_t *) &data_fork->blocks, (sz + block_size - 1) / block_size, 4);
			iso_msb ((uint8_t *) &data_fork->extents[0].start, blk - t->hfsp_part_start, 4);
			iso_msb ((uint8_t *) &data_fork->extents[0].count, (sz + block_size - 1) / block_size, 4);
			
			curoff += sizeof (*data_fork) * 2;
			/* FIXME: resource fork */
		      }
		    break;
		  }
		}
	      curnode++;
	    }
	  iso_msb ((uint8_t *) buffer + cat_node_size - j * 2 - 2, curoff, 2);

#ifdef Libisofs_hfsplus_verbose_debuG
	  iso_msg_debug(t->image->id, "Write\n");
#endif

	  ret = iso_write(t, buffer, cat_node_size);
	  if (ret < 0)
	    return ret;
	}
	curpos = next_lev;
    }
    memset (buffer, 0, sizeof (buffer));

    iso_msg_debug(t->image->id, "real extent_file_start = %d\n", (int)t->bytes_written / 2048);

    node_head = (struct hfsplus_btnode *) buffer;
    node_head->type = 1;
    iso_msb ((uint8_t *) &node_head->count, 3, 2);
    tree_head = (struct hfsplus_btheader *) (node_head + 1);
    iso_msb ((uint8_t *) &tree_head->nodesize, block_size, 2);
    iso_msb ((uint8_t *) &tree_head->keysize, 10, 2);
    iso_msb ((uint8_t *) &tree_head->total_nodes, 1, 4);
    iso_msb ((uint8_t *) &tree_head->free_nodes, 0, 4);
    iso_msb ((uint8_t *) &tree_head->clump_size, block_size, 4);
    iso_msb ((uint8_t *) &tree_head->attributes, 2, 4);
    buffer[0xf8] = (char) 0x80;

    buffer[block_size - 1] = sizeof (*node_head);
    buffer[block_size - 3] = sizeof (*node_head) + sizeof (*tree_head);
    buffer[block_size - 5] = (char) 0xf8;
    buffer[block_size - 7] = (char) ((block_size - 8) & 0xff);
    buffer[block_size - 8] = (block_size - 8) >> 8;

    ret = iso_write(t, buffer, block_size);
    if (ret < 0)
        return ret;

    iso_msg_debug(t->image->id, "(d) %d written", (int) t->bytes_written / 0x800);
    memset (buffer, 0, sizeof (buffer));
    for (i = 0; i < t->hfsp_nleafs; i++)
      if (t->hfsp_leafs[i].unix_type == UNIX_SYMLINK)
	{
	  int overhead;

	  ret = iso_write(t, t->hfsp_leafs[i].symlink_dest,
                          strlen(t->hfsp_leafs[i].symlink_dest));
	  if (ret < 0)
	    return ret;
	  overhead = strlen(t->hfsp_leafs[i].symlink_dest) % block_size;
	  if (overhead)
	    overhead = block_size - overhead;
	  ret = iso_write(t, buffer, overhead);
	  if (ret < 0)
	    return ret;
	}

    /* Need to align for start of next writer */
    ret = pad_up_block(t);
    if (ret < 0)
	return ret;

    iso_msg_debug(t->image->id, "(a) %d written", (int) t->bytes_written / 0x800);
    return ISO_SUCCESS;
}

static
int hfsplus_tail_writer_write_data(IsoImageWriter *writer)
{
    int ret;
    static char buffer[2 * HFSPLUS_MAX_BLOCK_SIZE];
    uint32_t complete_blocks, remaining_blocks, block_size;
    int over;
    Ecma119Image *t;

    if (writer == NULL) {
        return ISO_NULL_POINTER;
    }

    t = writer->target;
    block_size = t->hfsp_block_size;

#ifdef Libisofs_ts_debuG
    iso_msg_debug(t->image->id, "hfsplus tail writer writes at = %.f",
                  (double) t->bytes_written);
#endif

    memset (buffer, -1, sizeof (buffer));
    complete_blocks = (t->hfsp_allocation_size - 1) / block_size;
    remaining_blocks = t->hfsp_allocation_blocks - complete_blocks;

    while (complete_blocks--)
      {
	ret = iso_write(t, buffer, block_size);
	if (ret < 0)
	  return ret;
      }
    over = (t->hfsp_allocation_size - 1) % block_size;
    if (over)
      {
	memset (buffer + over, 0, sizeof (buffer) - over);
	buffer[over] = 0xff00 >> (t->hfsp_total_blocks % 8);
	ret = iso_write(t, buffer, block_size);
	if (ret < 0)
	  return ret;
	remaining_blocks--;
      }
    memset (buffer, 0, sizeof (buffer));
    /* When we have both FAT and HFS+ we may to overestimate needed blocks a bit.  */
    while (remaining_blocks--)
      {
	ret = iso_write(t, buffer, block_size);
	if (ret < 0)
	  return ret;
      }

    ret = pad_up_block(t);
    if (ret < 0)
	return ret;
    iso_msg_debug(t->image->id, "%d written", (int) t->bytes_written);

    ret = write_sb (t);

#ifdef Libisofs_ts_debuG
    iso_msg_debug(t->image->id, "hfsplus tail writer ends at = %.f",
                  (double) t->bytes_written);
#endif

    return ret;
}

static
int hfsplus_writer_free_data(IsoImageWriter *writer)
{
    /* free the Hfsplus tree */
    Ecma119Image *t = writer->target;
    uint32_t i;
    for (i = 0; i < t->hfsp_curleaf; i++)
      if (t->hfsp_leafs[i].type != HFSPLUS_FILE_THREAD
	  && t->hfsp_leafs[i].type != HFSPLUS_DIR_THREAD)
	{
	  free (t->hfsp_leafs[i].name);
	  free (t->hfsp_leafs[i].cmp_name);
	  if (t->hfsp_leafs[i].symlink_dest != NULL)
	      free (t->hfsp_leafs[i].symlink_dest);
	}
    free(t->hfsp_leafs);
    for (i = 0; i < t->hfsp_nlevels; i++)
      free (t->hfsp_levels[i].nodes);
    free(t->hfsp_levels);
    return ISO_SUCCESS;
}

static
int nop_writer_free_data(IsoImageWriter *writer)
{
    return ISO_SUCCESS;
}


/*
   ??? : Change this to binary search ?
         Expected advantage is low except with prefix "MANGLED".

   @param flag bit0= array is unsorted, do not abort on first larger element
   @return 0 = collision (collider in *new_idx), 1 = insert at *new_idx
*/
static
int search_mangled_pos(Ecma119Image *target, uint32_t idx, uint32_t *new_idx,
                       uint32_t search_start, uint32_t search_end, int flag)
{
    uint32_t i;
    int rel;

    for (i = search_start; i < search_end; i++) {
        if (target->hfsp_leafs[i].type == HFSPLUS_DIR_THREAD ||
            target->hfsp_leafs[i].type == HFSPLUS_FILE_THREAD)
    continue;
        rel = cmp_node(&(target->hfsp_leafs[idx]), &(target->hfsp_leafs[i]));
        if (rel == 0 && idx != i) {
            *new_idx = i;
            return 0; /* Collision */
        }
        if (rel < 0 && !(flag & 1)) {
            if (i <= idx)
                *new_idx = i;
            else
                *new_idx = i - 1;
            return 1;
        }
    }
    *new_idx = search_end - 1;
    return 1;
}

static
void rotate_hfs_list(Ecma119Image *target, uint32_t old_idx, uint32_t new_idx,
                     int flag)
{
    uint32_t i, sz;
    HFSPlusNode tr;

    if (old_idx == new_idx)
        return;
    sz = sizeof(HFSPlusNode);
    memcpy(&tr, &target->hfsp_leafs[old_idx], sz);
    if (old_idx > new_idx) {
        for (i = old_idx; i > new_idx; i--)
            memcpy(&target->hfsp_leafs[i], &target->hfsp_leafs[i - 1], sz);
    } else {
        for (i = old_idx; i < new_idx; i++)
            memcpy(&target->hfsp_leafs[i], &target->hfsp_leafs[i + 1], sz);
    }
    memcpy(&target->hfsp_leafs[new_idx], &tr, sz);
}

static
int subst_symlink_dest_comp(Ecma119Image *target, uint32_t idx,
                            char **dest, unsigned int *dest_len, 
                            char **comp_start, char **comp_end,
                            char *new_name, int flag)
{
    int new_len;
    unsigned int new_dest_len;
    char *new_dest, *wpt;

    new_len = strlen(new_name);
    new_dest_len =
               *comp_start - *dest + new_len + *dest_len - (*comp_end - *dest);
    new_dest = calloc(1, new_dest_len + 1);
    if (new_dest == NULL)
        return ISO_OUT_OF_MEM;
    wpt = new_dest;
    if (*comp_start - *dest > 0)
        memcpy(wpt, *dest, *comp_start - *dest);
    wpt += *comp_start - *dest;
    memcpy(wpt, new_name, new_len);
    wpt += new_len;
    if ((unsigned int) (*comp_end - *dest) < *dest_len)
        memcpy(wpt, *comp_end, *dest_len - (*comp_end - *dest));
    wpt += *dest_len - (*comp_end - *dest);
    *wpt = 0;

    *comp_start = new_dest + (*comp_start - *dest);
    *comp_end = *comp_start + new_len;
    target->hfsp_leafs[idx].symlink_dest = new_dest;
    *dest_len = new_dest_len;
    free(*dest);
    *dest = new_dest;
    return ISO_SUCCESS;
}

/* A specialized version of API call iso_tree_resolve_symlink().
   It updates symlink destination components which lead to the
   HFS+ node [changed_idx] in sync with resolution of the IsoImage
   destination path.
   It seems too much prone to weird link loopings if one would let
   a function underneath iso_tree_resolve_symlink() watch out for
   the IsoNode in question. Multiple passes through that node are
   possible.
   So this function exchanges components when encountered.
*/
static
int update_symlink(Ecma119Image *target, uint32_t changed_idx, char *new_name,
                   uint32_t link_idx, int *depth, int flag)
{
    IsoSymlink *sym;
    IsoDir *cur_dir = NULL;
    IsoNode *n, *resolved_node;
    char *orig_dest, *orig_start, *orig_end;
    char *hfsp_dest, *hfsp_start, *hfsp_end;
    int ret = 0;
    unsigned int comp_len, orig_len, hfsp_len, hfsp_comp_len;

    if (target->hfsp_leafs[link_idx].node->type != LIBISO_SYMLINK)
        return ISO_SUCCESS;
    sym = (IsoSymlink *) target->hfsp_leafs[link_idx].node;
    orig_dest = sym->dest;
    orig_len = strlen(orig_dest);
    hfsp_dest = target->hfsp_leafs[link_idx].symlink_dest;
    hfsp_len = strlen(hfsp_dest);

    if (orig_dest[0] == '/') {

        /* >>> ??? How to salvage absolute links without knowing the
                   path of the future mount point ?
               ??? Would it be better to leave them as is ?
           I can only assume that it gets mounted at / during some stage
           of booting.
        */;

        cur_dir = target->image->root;
        orig_end = orig_dest;
    } else {
        cur_dir = sym->node.parent;
        if (cur_dir == NULL)
            cur_dir = target->image->root;
        orig_end = orig_dest - 1;
    }

    if (hfsp_dest[0] == '/')
        hfsp_end = hfsp_dest;
    else
        hfsp_end = hfsp_dest - 1;

    while (orig_end < orig_dest + orig_len) {
        orig_start = orig_end + 1;
        hfsp_start = hfsp_end + 1;

        orig_end = strchr(orig_start, '/');
        if (orig_end == NULL)
            orig_end = orig_start + strlen(orig_start);
        comp_len = orig_end - orig_start;
        hfsp_end = strchr(hfsp_start, '/');
        if (hfsp_end == NULL)
            hfsp_end = hfsp_start + strlen(hfsp_start);
        hfsp_comp_len = hfsp_end - hfsp_start;

        if (comp_len == 0 || (comp_len == 1 && orig_start[0] == '.'))
   continue;
        if (comp_len == 2 && orig_start[0] == '.' && orig_start[1] == '.') {
            cur_dir = cur_dir->node.parent;
            if (cur_dir == NULL) /* link shoots over root */
                return ISO_SUCCESS;
   continue;
        }

        /* Search node in cur_dir */
        for (n = cur_dir->children; n != NULL; n = n->next)
            if (strncmp(orig_start, n->name, comp_len) == 0 &&
                strlen(n->name) == comp_len)
        break;
        if (n == NULL) /* dead link */
            return ISO_SUCCESS;

        if (n == target->hfsp_leafs[changed_idx].node) {
            iso_msg_debug(target->image->id,
                          "     link path '%s' touches RR '%s', HFS+ '%s'",
                          orig_dest, (n->name != NULL ? n->name : ""),
                          new_name);

            /* Exchange HFS+ component by new_name */
            ret = subst_symlink_dest_comp(target, link_idx,
                                          &hfsp_dest, &hfsp_len,
                                          &hfsp_start, &hfsp_end, new_name, 0);
            if (ret < 0)
                return ret;
        }

        if (n->type == LIBISO_DIR) {
            cur_dir = (IsoDir *) n;
        } else if (n->type == LIBISO_SYMLINK) {
            /* Resolve link and check whether it is a directory */
            if (*depth >= LIBISO_MAX_LINK_DEPTH)
                return ISO_SUCCESS;
            (*depth)++;
            ret = iso_tree_resolve_symlink(target->image, (IsoSymlink *) n,
                                           &resolved_node, depth, 0);
            if (ret == (int) ISO_DEAD_SYMLINK || ret == (int) ISO_DEEP_SYMLINK)
                return ISO_SUCCESS;
            if (ret < 0)
                return ret;
            if (resolved_node->type != LIBISO_DIR)
                return ISO_SUCCESS;
            cur_dir = (IsoDir *) resolved_node;
        } else {
    break;
        }
    }
    return ISO_SUCCESS;
}

/* Find the other nodes with old_name and switch to new .name
   One could make assumptions where name-followers are.
   But then there are still the symbolic links. They can be located anywhere.
*/
static
int update_name_followers(Ecma119Image *target, uint32_t idx, char *new_name,
                          uint16_t *old_name, uint16_t *old_cmp_name,
                          uint32_t old_strlen)
{
    uint32_t i;
    int ret, link_depth;

    for (i = 0; i < target->hfsp_nleafs; i++) {
        if (target->hfsp_leafs[i].unix_type == UNIX_SYMLINK) {
            link_depth = 0;
            ret = update_symlink(target, idx, new_name, i, &link_depth, 0);
            if (ret < 0)
                return ret;
        }
        if (target->hfsp_leafs[i].name != old_name)
    continue;
        target->hfsp_leafs[i].name = target->hfsp_leafs[idx].name;
        target->hfsp_leafs[i].strlen = target->hfsp_leafs[idx].strlen;
        if (target->hfsp_leafs[i].cmp_name == old_cmp_name)
            target->hfsp_leafs[i].cmp_name = target->hfsp_leafs[idx].cmp_name;
        if (target->hfsp_leafs[i].strlen > old_strlen)
            target->hfsp_leafs[i].used_size += (target->hfsp_leafs[i].strlen -
                                                old_strlen) * 2;
        else
            target->hfsp_leafs[i].used_size -= 2 * (old_strlen -
                                                 target->hfsp_leafs[i].strlen);
    }
    return 1;
}


/* @param flag bit0= node is new: do not rotate, do not update followers
*/
static
int try_mangle(Ecma119Image *target, uint32_t idx, uint32_t prev_idx,
               uint32_t search_start, uint32_t search_end,
               uint32_t *new_idx, char *prefix, int flag)
{
    int i, ret = 0;
    char new_name[LIBISO_HFSPLUS_NAME_MAX + 1], number[9];
    uint16_t *old_name, *old_cmp_name;
    uint32_t old_strlen;

    old_name = target->hfsp_leafs[idx].name;
    old_cmp_name = target->hfsp_leafs[idx].cmp_name;
    old_strlen = target->hfsp_leafs[idx].strlen;

    for (i = -1; i < 0x7fffffff; i++) {
        if (i == -1)
            number[0] = 0;
        else
            sprintf(number, "%X", (unsigned int) i);
        if (strlen(prefix) + 1 + strlen(number) > LIBISO_HFSPLUS_NAME_MAX) {
            ret = 0;
            goto no_success;
        }

        /* "-" would sort lower than capital letters ,
           traditional "_" causes longer rotations
         */
        sprintf(new_name, "%s_%s", prefix, number);

        /* The original name is kept until the end of the try */
        if (target->hfsp_leafs[idx].name != old_name)
            free(target->hfsp_leafs[idx].name);
        if (target->hfsp_leafs[idx].cmp_name != old_cmp_name)
            free(target->hfsp_leafs[idx].cmp_name);


        ret = set_hfsplus_name(target, new_name, &(target->hfsp_leafs[idx]));
        if (ret < 0)
            goto no_success;

        ret = search_mangled_pos(target, idx, new_idx, search_start,
                                 search_end, (flag & 1));
        if (ret < 0)
            goto no_success;
        if (ret == 0)
    continue; /* collision */
        if (flag & 1)
            *new_idx = idx;
        else
            rotate_hfs_list(target, idx, *new_idx, 0);

        /* >>> Get full ISO-RR paths of colliding nodes */;
        /* >>> iso_tree_get_node_path(node); */

        iso_msg_debug(target->image->id,
                  "HFS+ name collision with \"%s\" : \"%s\" renamed to \"%s\"",
                  target->hfsp_leafs[prev_idx].node->name,
                  target->hfsp_leafs[*new_idx].node->name, new_name);

    break;    
    }
    target->hfsp_leafs[*new_idx].used_size +=
                        (target->hfsp_leafs[*new_idx].strlen - old_strlen) * 2;

    if (!(flag & 1)) {
        ret = update_name_followers(target, *new_idx, new_name,
                                    old_name, old_cmp_name, old_strlen);
        if (ret < 0)
            goto no_success;
    }

    free(old_name);
    free(old_cmp_name);
    return 1;

no_success:;
    target->hfsp_leafs[idx].name = old_name;
    target->hfsp_leafs[idx].cmp_name = old_cmp_name;
    target->hfsp_leafs[idx].strlen = old_strlen;
    return ret;
}

static
int mangle_leafs(Ecma119Image *target, int flag)
{
    int ret;
    uint32_t i, new_idx, prev, first_prev;

    iso_msg_debug(target->image->id, "%s", "HFS+ mangling started ...");

    /* Look for the first owner of a name */
    for (prev = 0; prev < target->hfsp_nleafs; prev++) {
        if (target->hfsp_leafs[prev].type == HFSPLUS_DIR_THREAD ||
            target->hfsp_leafs[prev].type == HFSPLUS_FILE_THREAD ||
            target->hfsp_leafs[prev].node == NULL ||
            target->hfsp_leafs[prev].name == NULL ||
            target->hfsp_leafs[prev].cmp_name == NULL)
    continue;
        if (target->hfsp_leafs[prev].node->name == NULL)
    continue;
    break;
    }

    first_prev = prev; 
    for (i = prev + 1; i < target->hfsp_nleafs; i++) {
        if (target->hfsp_leafs[i].type == HFSPLUS_DIR_THREAD ||
            target->hfsp_leafs[i].type == HFSPLUS_FILE_THREAD ||
            target->hfsp_leafs[i].node == NULL ||
            target->hfsp_leafs[i].name == NULL ||
            target->hfsp_leafs[i].cmp_name == NULL)
    continue;
        if (target->hfsp_leafs[i].node->name == NULL)
    continue;
        if (cmp_node(&(target->hfsp_leafs[prev]), &(target->hfsp_leafs[i]))
            != 0) {
            prev = i;
    continue;
        }
        target->hfsp_collision_count++;


#ifdef Libisofs_with_mangle_masK

        /* >>> Development sketch: */

        /* >>> define in libisofs.h : enum with LIBISO_NOMANGLE_xyz 
                                      xinfo function for uint32_t
        */

        /* >>> inquire xinfo for mangle protection : uint32_t mangle_mask */

        if (mangle_mask & (1 << LIBISO_NOMANGLE_HFSPLUS)) {

            /* >>> Get full ISO-RR paths of colliding nodes and print
                   error message */;

            return ISO_HFSP_NO_MANGLE;
        } else {

#else /* Libisofs_with_mangle_masK */

        {

#endif /* ! Libisofs_with_mangle_masK */


            ret= try_mangle(target, i, prev, i + 1, target->hfsp_nleafs,
                            &new_idx, target->hfsp_leafs[i].node->name, 0);
            if (ret == 0)
                ret= try_mangle(target, i, prev, 0, target->hfsp_nleafs,
                                &new_idx, "MANGLED", 0);
            if (ret < 0)
                return(ret);
            if (new_idx > i) {
                i--; /* an unprocessed candidate has been rotated to i */
            } else {
                prev = i; /* advance */
            }
        }
    }

    if (target->hfsp_collision_count > 0) {
        /* Mangling cannot be properly performed if the name owners do not
           stay in sorting order.
        */
        prev = first_prev;
        for (i = prev + 1; i < target->hfsp_nleafs; i++) {
            if (target->hfsp_leafs[i].type == HFSPLUS_DIR_THREAD ||
                target->hfsp_leafs[i].type == HFSPLUS_FILE_THREAD ||
                target->hfsp_leafs[i].node == NULL ||
                target->hfsp_leafs[i].name == NULL ||
                target->hfsp_leafs[i].cmp_name == NULL)
        continue;
            if (target->hfsp_leafs[i].node->name == NULL)
        continue;
            if (cmp_node(&(target->hfsp_leafs[prev]),
                         &(target->hfsp_leafs[i])) > 0) {

                iso_msg_debug(target->image->id,
                     "*********** Mangling messed up sorting *************\n");

        break;
            }
            prev = i;
        }

        /* Only the owners of names were considered during mangling.
           The HFSPLUS_*_THREAD types must get in line by sorting again.
        */
        qsort(target->hfsp_leafs, target->hfsp_nleafs,
              sizeof(*target->hfsp_leafs), cmp_node);
    }
    iso_msg_debug(target->image->id,
                  "HFS+ mangling done. Resolved Collisions: %lu",
                  (unsigned long) target->hfsp_collision_count);
    return ISO_SUCCESS;
}

int hfsplus_writer_create(Ecma119Image *target)
{
    int ret;
    IsoImageWriter *writer;
    int max_levels;
    int level = 0;
    IsoNode *pos;
    IsoDir *dir;
    int i;
    uint32_t cat_node_size;

    writer = calloc(1, sizeof(IsoImageWriter));
    if (writer == NULL) {
        return ISO_OUT_OF_MEM;
    }

    make_hfsplus_decompose_pages();
    make_hfsplus_class_pages();

    if (target->hfsp_block_size == 0)
        target->hfsp_block_size = HFSPLUS_DEFAULT_BLOCK_SIZE;
    target->hfsp_cat_node_size = 2 * target->hfsp_block_size;
    target->hfsp_iso_block_fac = 2048 / target->hfsp_block_size;
    cat_node_size = target->hfsp_cat_node_size;

    writer->compute_data_blocks = hfsplus_writer_compute_data_blocks;
    writer->write_vol_desc = nop_writer_write_vol_desc;
    writer->write_data = hfsplus_writer_write_data;
    writer->free_data = hfsplus_writer_free_data;
    writer->data = NULL;
    writer->target = target;

    iso_msg_debug(target->image->id, "Creating HFS+ tree...");
    target->hfsp_nfiles = 0;
    target->hfsp_ndirs = 0;
    target->hfsp_cat_id = 16;
    ret = hfsplus_count_tree(target, (IsoNode*)target->image->root);
    if (ret < 0) {
        free((char *) writer);
        return ret;
    }

    for (i = 0; i < ISO_HFSPLUS_BLESS_MAX; i++)
      target->hfsp_bless_id[i] = 0;

    target->hfsp_nleafs = 2 * (target->hfsp_nfiles + target->hfsp_ndirs);
    target->hfsp_curleaf = 0;

    target->hfsp_leafs = calloc (target->hfsp_nleafs, sizeof (target->hfsp_leafs[0]));
    if (target->hfsp_leafs == NULL) {
        return ISO_OUT_OF_MEM;
    }
    ret = set_hfsplus_name (target, target->image->volume_id,
                            &target->hfsp_leafs[target->hfsp_curleaf]);
    if (ret < 0)
        return ret;
    target->hfsp_leafs[target->hfsp_curleaf].node = (IsoNode *) target->image->root;
    target->hfsp_leafs[target->hfsp_curleaf].used_size = target->hfsp_leafs[target->hfsp_curleaf].strlen * 2 + 8 + 2 + sizeof (struct hfsplus_catfile_common);

    target->hfsp_leafs[target->hfsp_curleaf].type = HFSPLUS_DIR;
    target->hfsp_leafs[target->hfsp_curleaf].file = 0;
    target->hfsp_leafs[target->hfsp_curleaf].cat_id = 2;
    target->hfsp_leafs[target->hfsp_curleaf].parent_id = 1;
    target->hfsp_leafs[target->hfsp_curleaf].nchildren = 0;
    target->hfsp_leafs[target->hfsp_curleaf].unix_type = UNIX_NONE;
    target->hfsp_curleaf++;

    target->hfsp_leafs[target->hfsp_curleaf].name = target->hfsp_leafs[target->hfsp_curleaf - 1].name;
    target->hfsp_leafs[target->hfsp_curleaf].cmp_name = 0;
    target->hfsp_leafs[target->hfsp_curleaf].strlen = target->hfsp_leafs[target->hfsp_curleaf - 1].strlen;
    target->hfsp_leafs[target->hfsp_curleaf].used_size = target->hfsp_leafs[target->hfsp_curleaf].strlen * 2 + 8 + 2 + sizeof (struct hfsplus_catfile_thread);
    target->hfsp_leafs[target->hfsp_curleaf].node = (IsoNode *) target->image->root;
    target->hfsp_leafs[target->hfsp_curleaf].type = HFSPLUS_DIR_THREAD;
    target->hfsp_leafs[target->hfsp_curleaf].file = 0;
    target->hfsp_leafs[target->hfsp_curleaf].cat_id = 1;
    target->hfsp_leafs[target->hfsp_curleaf].parent_id = 2;
    target->hfsp_leafs[target->hfsp_curleaf].unix_type = UNIX_NONE;
    target->hfsp_curleaf++;

    dir = (IsoDir*)target->image->root;

    pos = dir->children;
    while (pos)
      {
	int cret;
	cret = create_tree(target, pos, 2);
	if (cret < 0)
	  return cret;
	pos = pos->next;
	target->hfsp_leafs[0].nchildren++;
      }

    qsort(target->hfsp_leafs, target->hfsp_nleafs,
          sizeof(*target->hfsp_leafs), cmp_node);

    ret = mangle_leafs(target, 0);
    if (ret < 0)
        return ret;

    for (max_levels = 0; target->hfsp_nleafs >> max_levels; max_levels++);
    max_levels += 2;
    target->hfsp_levels = calloc (max_levels, sizeof (target->hfsp_levels[0]));
    if (target->hfsp_levels == NULL) {
        return ISO_OUT_OF_MEM;
    }

    target->hfsp_nnodes = 1;
    {
      uint32_t last_start = 0;
      uint32_t i;
      unsigned bytes_rem = cat_node_size - sizeof (struct hfsplus_btnode) - 2;

      target->hfsp_levels[level].nodes = calloc ((target->hfsp_nleafs + 1),  sizeof (target->hfsp_levels[level].nodes[0]));
      if (!target->hfsp_levels[level].nodes)
	return ISO_OUT_OF_MEM;
    
      target->hfsp_levels[level].level_size = 0;  
      for (i = 0; i < target->hfsp_nleafs; i++)
	{
	  if (bytes_rem < target->hfsp_leafs[i].used_size)
	    {
	      target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].start = last_start;
	      target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].cnt = i - last_start;
	      if (target->hfsp_leafs[last_start].cmp_name)
		{
		  target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].strlen = target->hfsp_leafs[last_start].strlen;
		  target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].str = target->hfsp_leafs[last_start].name;
		}
	      else
		{
		  target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].strlen = 0;
		  target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].str = NULL;
		}
	      target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].parent_id = target->hfsp_leafs[last_start].parent_id;
	      target->hfsp_levels[level].level_size++;
	      last_start = i;
	      bytes_rem = cat_node_size - sizeof (struct hfsplus_btnode) - 2;
	    }
	  bytes_rem -= target->hfsp_leafs[i].used_size;
	}

      target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].start = last_start;
      target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].cnt = i - last_start;
      if (target->hfsp_leafs[last_start].cmp_name)
	{
	  target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].strlen = target->hfsp_leafs[last_start].strlen;
	  target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].str = target->hfsp_leafs[last_start].name;
	}
      else
	{
	  target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].strlen = 0;
	  target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].str = NULL;
	}
      target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].parent_id = target->hfsp_leafs[last_start].parent_id;
      target->hfsp_levels[level].level_size++;
      target->hfsp_nnodes += target->hfsp_levels[level].level_size;
    }

    while (target->hfsp_levels[level].level_size > 1)
      {
	uint32_t last_start = 0;
	uint32_t i;
	uint32_t last_size;
	unsigned bytes_rem = cat_node_size - sizeof (struct hfsplus_btnode) - 2;

	last_size = target->hfsp_levels[level].level_size;

	level++;

	target->hfsp_levels[level].nodes = calloc (((last_size + 1) / 2),  sizeof (target->hfsp_levels[level].nodes[0]));
	if (!target->hfsp_levels[level].nodes)
	  return ISO_OUT_OF_MEM;
    
	target->hfsp_levels[level].level_size = 0;  

	for (i = 0; i < last_size; i++)
	  {
	    uint32_t used_size;
	    used_size = target->hfsp_levels[level - 1].nodes[i].strlen * 2 + 14;
	    if (bytes_rem < used_size)
	    {
	      target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].start = last_start;
	      target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].cnt = i - last_start;
	      target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].strlen = target->hfsp_levels[level - 1].nodes[last_start].strlen;
	      target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].str = target->hfsp_levels[level - 1].nodes[last_start].str;
	      target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].parent_id = target->hfsp_levels[level - 1].nodes[last_start].parent_id;
	      target->hfsp_levels[level].level_size++;
	      last_start = i;
	      bytes_rem = cat_node_size - sizeof (struct hfsplus_btnode) - 2;
	    }
	  bytes_rem -= used_size;
	}

	target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].start = last_start;
	target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].cnt = i - last_start;
	target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].strlen = target->hfsp_levels[level - 1].nodes[last_start].strlen;
	target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].str = target->hfsp_levels[level - 1].nodes[last_start].str;
	target->hfsp_levels[level].nodes[target->hfsp_levels[level].level_size].parent_id = target->hfsp_levels[level - 1].nodes[last_start].parent_id;
	target->hfsp_levels[level].level_size++;
	target->hfsp_nnodes += target->hfsp_levels[level].level_size;
      }

    target->hfsp_nlevels = level + 1;

    if (target->hfsp_nnodes > (cat_node_size - 0x100) * 8)
      {
	return iso_msg_submit(target->image->id, ISO_MANGLE_TOO_MUCH_FILES, 0,
			      "HFS+ map nodes aren't implemented");

	return ISO_MANGLE_TOO_MUCH_FILES;
      }

    /* add this writer to image */
    target->writers[target->nwriters++] = writer;

    return ISO_SUCCESS;
}

int hfsplus_tail_writer_create(Ecma119Image *target)
{
    IsoImageWriter *writer;

    writer = calloc(1, sizeof(IsoImageWriter));
    if (writer == NULL) {
        return ISO_OUT_OF_MEM;
    }

    writer->compute_data_blocks = hfsplus_tail_writer_compute_data_blocks;
    writer->write_vol_desc = nop_writer_write_vol_desc;
    writer->write_data = hfsplus_tail_writer_write_data;
    writer->free_data = nop_writer_free_data;
    writer->data = NULL;
    writer->target = target;

    /* add this writer to image */
    target->writers[target->nwriters++] = writer;

    return ISO_SUCCESS;
}


/* API */
int iso_hfsplus_xinfo_func(void *data, int flag)
{
    if (flag == 1 && data != NULL)
        free(data);
    return 1;
}

/* API */
struct iso_hfsplus_xinfo_data *iso_hfsplus_xinfo_new(int flag)
{
    struct iso_hfsplus_xinfo_data *o;

    o = calloc(1, sizeof(struct iso_hfsplus_xinfo_data));
    if (o == NULL)
        return NULL;
    o->version = 0;
    return o;
}

/* The iso_node_xinfo_cloner function which gets associated to
 * iso_hfsplus_xinfo_func by iso_init() resp. iso_init_with_flag() via
 * iso_node_xinfo_make_clonable()
 */
int iso_hfsplus_xinfo_cloner(void *old_data, void **new_data, int flag)
{
    *new_data = NULL;
    if (flag)
        return ISO_XINFO_NO_CLONE;
    if (old_data == NULL)
        return 0;
    *new_data = iso_hfsplus_xinfo_new(0);
    if(*new_data == NULL)
       return ISO_OUT_OF_MEM;
    memcpy(*new_data, old_data, sizeof(struct iso_hfsplus_xinfo_data));
    return ISO_SUCCESS;
}

