/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2007 Mario Danic
 * Copyright (c) 2011-2012 Thomas Schmitt
 *
 * This file is part of the libisofs project; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "joliet.h"
#include "messages.h"
#include "writer.h"
#include "image.h"
#include "filesrc.h"
#include "eltorito.h"
#include "libisofs.h"
#include "util.h"
#include "ecma119.h"


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static
int get_joliet_name(Ecma119Image *t, IsoNode *iso, uint16_t **name)
{
    int ret;
    uint16_t *ucs_name;
    uint16_t *jname = NULL;

    if (iso->name == NULL) {
        /* it is not necessarily an error, it can be the root */
        return ISO_SUCCESS;
    }

    ret = str2ucs(t->input_charset, iso->name, &ucs_name);
    if (ret < 0) {
        iso_msg_debug(t->image->id, "Can't convert %s", iso->name);
        return ret;
    }
    if (iso->type == LIBISO_DIR) {
        jname = iso_j_dir_id(ucs_name, t->joliet_long_names << 1);
    } else {
        jname = iso_j_file_id(ucs_name,
                       (t->joliet_long_names << 1) | !!(t->no_force_dots & 2));
    }
    free(ucs_name);
    if (jname != NULL) {
        *name = jname;
        return ISO_SUCCESS;
    } else {
        /*
         * only possible if mem error, as check for empty names is done
         * in public tree
         */
        return ISO_OUT_OF_MEM;
    }
}

static
void joliet_node_free(JolietNode *node)
{
    if (node == NULL) {
        return;
    }
    if (node->type == JOLIET_DIR) {
        size_t i;
        for (i = 0; i < node->info.dir->nchildren; i++) {
            joliet_node_free(node->info.dir->children[i]);
        }
        free(node->info.dir->children);
        free(node->info.dir);
    }
    iso_node_unref(node->node);
    free(node->name);
    free(node);
}

/**
 * Create a low level Joliet node
 * @return
 *      1 success, 0 ignored, < 0 error
 */
static
int create_node(Ecma119Image *t, IsoNode *iso, JolietNode **node)
{
    int ret;
    JolietNode *joliet;

    joliet = calloc(1, sizeof(JolietNode));
    if (joliet == NULL) {
        return ISO_OUT_OF_MEM;
    }

    if (iso->type == LIBISO_DIR) {
        IsoDir *dir = (IsoDir*) iso;
        joliet->info.dir = calloc(1, sizeof(struct joliet_dir_info));
        if (joliet->info.dir == NULL) {
            free(joliet);
            return ISO_OUT_OF_MEM;
        }
        joliet->info.dir->children = calloc(sizeof(void*), dir->nchildren);
        if (joliet->info.dir->children == NULL) {
            free(joliet->info.dir);
            free(joliet);
            return ISO_OUT_OF_MEM;
        }
        joliet->type = JOLIET_DIR;
    } else if (iso->type == LIBISO_FILE) {
        /* it's a file */
        off_t size;
        IsoFileSrc *src;
        IsoFile *file = (IsoFile*) iso;

        size = iso_stream_get_size(file->stream);
        if (size > (off_t)MAX_ISO_FILE_SECTION_SIZE && t->iso_level != 3) {
            char *ipath = iso_tree_get_node_path(iso);
            free(joliet);
            ret = iso_msg_submit(t->image->id, ISO_FILE_TOO_BIG, 0,
                         "File \"%s\" can't be added to image because is "
                         "greater than 4GB", ipath);
            free(ipath);
            return ret;
        }

        ret = iso_file_src_create(t, file, &src);
        if (ret < 0) {
            free(joliet);
            return ret;
        }
        joliet->info.file = src;
        joliet->type = JOLIET_FILE;
    } else if (iso->type == LIBISO_BOOT) {
        /* it's a el-torito boot catalog, that we write as a file */
        IsoFileSrc *src;

        ret = el_torito_catalog_file_src_create(t, &src);
        if (ret < 0) {
            free(joliet);
            return ret;
        }
        joliet->info.file = src;
        joliet->type = JOLIET_FILE;
    } else {
        /* should never happen */
        free(joliet);
        return ISO_ASSERT_FAILURE;
    }

    /* take a ref to the IsoNode */
    joliet->node = iso;
    iso_node_ref(iso);

    *node = joliet;
    return ISO_SUCCESS;
}

/**
 * Create the low level Joliet tree from the high level ISO tree.
 *
 * @return
 *      1 success, 0 file ignored, < 0 error
 */
static
int create_tree(Ecma119Image *t, IsoNode *iso, JolietNode **tree, int pathlen)
{
    int ret, max_path;
    JolietNode *node = NULL;
    uint16_t *jname = NULL;

    if (t == NULL || iso == NULL || tree == NULL) {
        return ISO_NULL_POINTER;
    }

    if (iso->hidden & LIBISO_HIDE_ON_JOLIET) {
        /* file will be ignored */
        return 0;
    }
    ret = get_joliet_name(t, iso, &jname);
    if (ret < 0) {
        return ret;
    }
    max_path = pathlen + 1 + (jname ? ucslen(jname) * 2 : 0);
    if (!t->joliet_longer_paths && max_path > 240) {
        char *ipath = iso_tree_get_node_path(iso);
        /*
         * Wow!! Joliet is even more restrictive than plain ISO-9660,
         * that allows up to 255 bytes!!
         */
        ret = iso_msg_submit(t->image->id, ISO_FILE_IMGPATH_WRONG, 0,
                     "File \"%s\" can't be added to Joliet tree, because "
                     "its path length is larger than 240", ipath);
        free(jname);
        free(ipath);
        return ret;
    }

    switch (iso->type) {
    case LIBISO_FILE:
        ret = create_node(t, iso, &node);
        break;
    case LIBISO_DIR:
        {
            IsoNode *pos;
            IsoDir *dir = (IsoDir*)iso;
            ret = create_node(t, iso, &node);
            if (ret < 0) {
                free(jname);
                return ret;
            }
            pos = dir->children;
            while (pos) {
                int cret;
                JolietNode *child;
                cret = create_tree(t, pos, &child, max_path);
                if (cret < 0) {
                    /* error */
                    joliet_node_free(node);
                    ret = cret;
                    break;
                } else if (cret == ISO_SUCCESS) {
                    /* add child to this node */
                    int nchildren = node->info.dir->nchildren++;
                    node->info.dir->children[nchildren] = child;
                    child->parent = node;
                }
                pos = pos->next;
            }
        }
        break;
    case LIBISO_BOOT:
        if (t->eltorito) {
            ret = create_node(t, iso, &node);
        } else {
            /* log and ignore */
            ret = iso_msg_submit(t->image->id, ISO_FILE_IGNORED, 0,
                "El-Torito catalog found on a image without El-Torito.");
        }
        break;
    case LIBISO_SYMLINK:
    case LIBISO_SPECIAL:
        {
            char *ipath = iso_tree_get_node_path(iso);
            ret = iso_msg_submit(t->image->id, ISO_FILE_IGNORED, 0,
                 "Can't add %s to Joliet tree. %s can only be added to a "
                 "Rock Ridge tree.", ipath, (iso->type == LIBISO_SYMLINK ?
                                             "Symlinks" : "Special files"));
            free(ipath);
        }
        break;
    default:
        /* should never happen */
        return ISO_ASSERT_FAILURE;
    }
    if (ret <= 0) {
        free(jname);
        return ret;
    }
    node->name = jname;
    *tree = node;
    return ISO_SUCCESS;
}

static int
cmp_node(const void *f1, const void *f2)
{
    JolietNode *f = *((JolietNode**)f1);
    JolietNode *g = *((JolietNode**)f2);
    return ucscmp(f->name, g->name);
}

static
void sort_tree(JolietNode *root)
{
    size_t i;

    qsort(root->info.dir->children, root->info.dir->nchildren,
          sizeof(void*), cmp_node);
    for (i = 0; i < root->info.dir->nchildren; i++) {
        JolietNode *child = root->info.dir->children[i];
        if (child->type == JOLIET_DIR)
            sort_tree(child);
    }
}

static
int cmp_node_name(const void *f1, const void *f2)
{
    JolietNode *f = *((JolietNode**)f1);
    JolietNode *g = *((JolietNode**)f2);
    return ucscmp(f->name, g->name);
}

static
int joliet_create_mangled_name(uint16_t *dest, uint16_t *src, int digits,
                                int number, uint16_t *ext)
{
    int ret, pos;
    uint16_t *ucsnumber;
    char fmt[16];
    char nstr[72];
              /* was: The only caller of this function allocates dest
                      with 66 elements and limits digits to < 8
                 But this does not match the usage of nstr which has to take
                 the decimal representation of an int.
              */

    if (digits >= 8)
        return ISO_ASSERT_FAILURE;

    sprintf(fmt, "%%0%dd", digits);
    sprintf(nstr, fmt, number);

    ret = str2ucs("ASCII", nstr, &ucsnumber);
    if (ret < 0) {
        return ret;
    }

    /* copy name */
    pos = ucslen(src);
    ucsncpy(dest, src, pos);

    /* copy number */
    ucsncpy(dest + pos, ucsnumber, digits);
    pos += digits;

    if (ext[0] != (uint16_t)0) {
        size_t extlen = ucslen(ext);
        iso_msb((uint8_t *) (dest + pos), 0x002E, 2); /* '.' in UCS */ 
        pos++;
        ucsncpy(dest + pos, ext, extlen);
        pos += extlen;
    }
    iso_msb((uint8_t *) (dest + pos), 0, 2);
    free(ucsnumber);
    return ISO_SUCCESS;
}

static
int mangle_single_dir(Ecma119Image *t, JolietNode *dir)
{
    int ret;
    int i, nchildren, maxchar = 64;
    JolietNode **children;
    IsoHTable *table;
    int need_sort = 0;
    uint16_t *full_name = NULL;
    uint16_t *tmp = NULL;

    LIBISO_ALLOC_MEM(full_name, uint16_t, LIBISO_JOLIET_NAME_MAX);
    LIBISO_ALLOC_MEM(tmp, uint16_t, LIBISO_JOLIET_NAME_MAX);
    nchildren = dir->info.dir->nchildren;
    children = dir->info.dir->children;

    if (t->joliet_long_names)
        maxchar = 103;

    /* a hash table will temporary hold the names, for fast searching */
    ret = iso_htable_create((nchildren * 100) / 80, iso_str_hash,
                            (compare_function_t)ucscmp, &table);
    if (ret < 0) {
        goto ex;
    }
    for (i = 0; i < nchildren; ++i) {
        uint16_t *name = children[i]->name;
        ret = iso_htable_add(table, name, name);
        if (ret < 0) {
            goto mangle_cleanup;
        }
    }

    for (i = 0; i < nchildren; ++i) {
        uint16_t *name, *ext;
        int max; /* computed max len for name, without extension */
        int j = i;
        int digits = 1; /* characters to change per name */

        /* first, find all child with same name */
        while (j + 1 < nchildren &&
                !cmp_node_name(children + i, children + j + 1)) {
            ++j;
        }
        if (j == i) {
            /* name is unique */
            continue;
        }

        /*
         * A max of 7 characters is good enought, it allows handling up to
         * 9,999,999 files with same name.
         */
         /* Important: joliet_create_mangled_name() relies on digits < 8 */

        while (digits < 8) {
            int ok, k;
            uint16_t *dot;
            int change = 0; /* number to be written */

            /* copy name to buffer */
            ucscpy(full_name, children[i]->name);

            /* compute name and extension */
            dot = ucsrchr(full_name, '.');
            if (dot != NULL && children[i]->type != JOLIET_DIR) {

                /*
                 * File (not dir) with extension
                 */
                int extlen;
                full_name[dot - full_name] = 0;
                name = full_name;
                ext = dot + 1;

                extlen = ucslen(ext);
                max = maxchar + 1 - extlen - 1 - digits;
                if (max <= 0) {
                    /* this can happen if extension is too long */
                    if (extlen + max > 3) {
                        /*
                         * reduce extension len, to give name an extra char
                         * note that max is negative or 0
                         */
                        extlen = extlen + max - 1;
                        ext[extlen] = 0;
                        max = maxchar + 2 - extlen - 1 - digits;
                    } else {
                        /*
                         * error, we don't support extensions < 3
                         * This can't happen with current limit of digits.
                         */
                        ret = ISO_ERROR;
                        goto mangle_cleanup;
                    }
                }
                /* ok, reduce name by digits */
                if (name + max < dot) {
                    name[max] = 0;
                }
            } else {
                /* Directory, or file without extension */
                if (children[i]->type == JOLIET_DIR) {
                    max = maxchar + 1 - digits;
                    dot = NULL; /* dots have no meaning in dirs */
                } else {
                    max = maxchar + 1 - digits;
                }
                name = full_name;
                if ((size_t) max < ucslen(name)) {
                    name[max] = 0;
                }
                /* let ext be an empty string */
                ext = name + ucslen(name);
            }

            ok = 1;
            /* change name of each file */
            for (k = i; k <= j; ++k) {
                while (1) {
                    ret = joliet_create_mangled_name(tmp, name, digits,
                                                     change, ext);
                    if (ret < 0) {
                        goto mangle_cleanup;
                    }
                    ++change;
                    if (change > int_pow(10, digits)) {
                        ok = 0;
                        break;
                    }
                    if (!iso_htable_get(table, tmp, NULL)) {
                        /* the name is unique, so it can be used */
                        break;
                    }
                }
                if (ok) {
                    uint16_t *new = ucsdup(tmp);
                    if (new == NULL) {
                        ret = ISO_OUT_OF_MEM;
                        goto mangle_cleanup;
                    }

                    iso_htable_remove_ptr(table, children[k]->name, NULL);
                    free(children[k]->name);
                    children[k]->name = new;
                    iso_htable_add(table, new, new);

                    /*
                     * if we change a name we need to sort again children
                     * at the end
                     */
                    need_sort = 1;
                } else {
                    /* we need to increment digits */
                    break;
                }
            }
            if (ok) {
                break;
            } else {
                ++digits;
            }
        }
        if (digits == 8) {
            ret = ISO_MANGLE_TOO_MUCH_FILES;
            goto mangle_cleanup;
        }
        i = j;
    }

    /*
     * If needed, sort again the files inside dir
     */
    if (need_sort) {
        qsort(children, nchildren, sizeof(void*), cmp_node_name);
    }

    ret = ISO_SUCCESS;

mangle_cleanup : ;
ex:;
    iso_htable_destroy(table, NULL);
    LIBISO_FREE_MEM(tmp);
    LIBISO_FREE_MEM(full_name);
    return ret;
}

static
int mangle_tree(Ecma119Image *t, JolietNode *dir)
{
    int ret;
    size_t i;

    ret = mangle_single_dir(t, dir);
    if (ret < 0) {
        return ret;
    }

    /* recurse */
    for (i = 0; i < dir->info.dir->nchildren; ++i) {
        if (dir->info.dir->children[i]->type == JOLIET_DIR) {
            ret = mangle_tree(t, dir->info.dir->children[i]);
            if (ret < 0) {
                /* error */
                return ret;
            }
        }
    }
    return ISO_SUCCESS;
}

static
int joliet_tree_create(Ecma119Image *t)
{
    int ret;
    JolietNode *root;

    if (t == NULL) {
        return ISO_NULL_POINTER;
    }

    ret = create_tree(t, (IsoNode*)t->image->root, &root, 0);
    if (ret <= 0) {
        if (ret == 0) {
            /* unexpected error, root ignored!! This can't happen */
            ret = ISO_ASSERT_FAILURE;
        }
        return ret;
    }

    /* the Joliet tree is stored in Ecma119Image target */
    if (t->eff_partition_offset > 0) {
        t->j_part_root = root;
    } else {
        t->joliet_root = root;
    }

    iso_msg_debug(t->image->id, "Sorting the Joliet tree...");
    sort_tree(root);

    iso_msg_debug(t->image->id, "Mangling Joliet names...");
    ret = mangle_tree(t, root);
    if (ret < 0)
        return ret;
    return ISO_SUCCESS;
}

/**
 * Compute the size of a directory entry for a single node
 */
static
size_t calc_dirent_len(Ecma119Image *t, JolietNode *n)
{
    /* note than name len is always even, so we always need the pad byte */
    int ret = n->name ? ucslen(n->name) * 2 + 34 : 34;
    if (n->type == JOLIET_FILE && !(t->omit_version_numbers & 3)) {
        /* take into account version numbers */
        ret += 4;
    }
    return ret;
}

/**
 * Computes the total size of all directory entries of a single joliet dir.
 * This is like ECMA-119 6.8.1.1, but taking care that names are stored in
 * UCS.
 */
static
size_t calc_dir_size(Ecma119Image *t, JolietNode *dir)
{
    size_t i, len;

    /* size of "." and ".." entries */
    len = 34 + 34;

    for (i = 0; i < dir->info.dir->nchildren; ++i) {
        size_t remaining;
        int section, nsections;
        JolietNode *child = dir->info.dir->children[i];
        size_t dirent_len = calc_dirent_len(t, child);

        nsections = (child->type == JOLIET_FILE) ? child->info.file->nsections : 1;
        for (section = 0; section < nsections; ++section) {
            remaining = BLOCK_SIZE - (len % BLOCK_SIZE);
            if (dirent_len > remaining) {
                /* child directory entry doesn't fit on block */
                len += remaining + dirent_len;
            } else {
                len += dirent_len;
            }
        }
    }

    /*
     * The size of a dir is always a multiple of block size, as we must add
     * the size of the unused space after the last directory record
     * (ECMA-119, 6.8.1.3)
     */
    len = ROUND_UP(len, BLOCK_SIZE);

    /* cache the len */
    dir->info.dir->len = len;
    return len;
}

static
void calc_dir_pos(Ecma119Image *t, JolietNode *dir)
{
    size_t i, len;

    t->joliet_ndirs++;
    dir->info.dir->block = t->curblock;
    len = calc_dir_size(t, dir);
    t->curblock += DIV_UP(len, BLOCK_SIZE);
    for (i = 0; i < dir->info.dir->nchildren; i++) {
        JolietNode *child = dir->info.dir->children[i];
        if (child->type == JOLIET_DIR) {
            calc_dir_pos(t, child);
        }
    }
}

/**
 * Compute the length of the joliet path table, in bytes.
 */
static
uint32_t calc_path_table_size(JolietNode *dir)
{
    uint32_t size;
    size_t i;

    /* size of path table for this entry */
    size = 8;
    size += dir->name ? ucslen(dir->name) * 2 : 2;

    /* and recurse */
    for (i = 0; i < dir->info.dir->nchildren; i++) {
        JolietNode *child = dir->info.dir->children[i];
        if (child->type == JOLIET_DIR) {
            size += calc_path_table_size(child);
        }
    }
    return size;
}

static
int joliet_writer_compute_data_blocks(IsoImageWriter *writer)
{
    Ecma119Image *t;
    uint32_t path_table_size;
    size_t ndirs;

    if (writer == NULL) {
        return ISO_OUT_OF_MEM;
    }

    t = writer->target;

    /* compute position of directories */
    iso_msg_debug(t->image->id, "Computing position of Joliet dir structure");
    t->joliet_ndirs = 0;
    calc_dir_pos(t, t->joliet_root);

    /* compute length of pathlist */
    iso_msg_debug(t->image->id, "Computing length of Joliet pathlist");
    path_table_size = calc_path_table_size(t->joliet_root);

    /* compute location for path tables */
    t->joliet_l_path_table_pos = t->curblock;
    t->curblock += DIV_UP(path_table_size, BLOCK_SIZE);
    t->joliet_m_path_table_pos = t->curblock;
    t->curblock += DIV_UP(path_table_size, BLOCK_SIZE);
    t->joliet_path_table_size = path_table_size;

    if (t->partition_offset > 0) {
        /* Take into respect second directory tree */
        ndirs = t->joliet_ndirs;
        t->joliet_ndirs = 0;
        calc_dir_pos(t, t->j_part_root);
        if (t->joliet_ndirs != ndirs) {
            iso_msg_submit(t->image->id, ISO_ASSERT_FAILURE, 0,
                    "Number of directories differs in Joliet partiton_tree");
            return ISO_ASSERT_FAILURE;
        }
        /* Take into respect second set of path tables */
        path_table_size = calc_path_table_size(t->j_part_root);
        t->j_part_l_path_table_pos = t->curblock;
        t->curblock += DIV_UP(path_table_size, BLOCK_SIZE);
        t->j_part_m_path_table_pos = t->curblock;
        t->curblock += DIV_UP(path_table_size, BLOCK_SIZE);
    }

    return ISO_SUCCESS;
}

/**
 * Write a single directory record for Joliet. It is like (ECMA-119, 9.1),
 * but file identifier is stored in UCS.
 *
 * @param file_id
 *     if >= 0, we use it instead of the filename (for "." and ".." entries).
 * @param len_fi
 *     Computed length of the file identifier. Total size of the directory
 *     entry will be len + 34 (ECMA-119, 9.1.12), as padding is always needed
 */
static
void write_one_dir_record(Ecma119Image *t, JolietNode *node, int file_id,
                          uint8_t *buf, size_t len_fi, int extent)
{
    uint32_t len;
    uint32_t block;
    uint8_t len_dr; /*< size of dir entry */
    int multi_extend = 0;
    uint8_t *name = (file_id >= 0) ? (uint8_t*)&file_id
            : (uint8_t*)node->name;

    struct ecma119_dir_record *rec = (struct ecma119_dir_record*)buf;
    IsoNode *iso;

    len_dr = 33 + len_fi + ((len_fi % 2) ? 0 : 1);

    memcpy(rec->file_id, name, len_fi);

    if (node->type == JOLIET_FILE && !(t->omit_version_numbers & 3)) {
        len_dr += 4;
        rec->file_id[len_fi++] = 0;
        rec->file_id[len_fi++] = ';';
        rec->file_id[len_fi++] = 0;
        rec->file_id[len_fi++] = '1';
    }

    if (node->type == JOLIET_DIR) {
        /* use the cached length */
        len = node->info.dir->len;
        block = node->info.dir->block;
    } else if (node->type == JOLIET_FILE) {
        block = node->info.file->sections[extent].block;
        len = node->info.file->sections[extent].size;
        multi_extend = (node->info.file->nsections - 1 == extent) ? 0 : 1;
    } else {
        /*
         * for nodes other than files and dirs, we set both
         * len and block to 0
         */
        len = 0;
        block = 0;
    }

    /*
     * For ".." entry we need to write the parent info!
     */
    if (file_id == 1 && node->parent)
        node = node->parent;

    rec->len_dr[0] = len_dr;
    iso_bb(rec->block, block - t->eff_partition_offset, 4);
    iso_bb(rec->length, len, 4);

    /* was: iso_datetime_7(rec->recording_time, t->now, t->always_gmt);
    */
    iso= node->node;
    iso_datetime_7(rec->recording_time, 
                   (t->dir_rec_mtime & 2) ? ( t->replace_timestamps ?
                                              t->timestamp : iso->mtime )
                                          : t->now, t->always_gmt);

    rec->flags[0] = ((node->type == JOLIET_DIR) ? 2 : 0) | (multi_extend ? 0x80 : 0);
    iso_bb(rec->vol_seq_number, (uint32_t) 1, 2);
    rec->len_fi[0] = len_fi;
}

/**
 * Copy up to \p max characters from \p src to \p dest. If \p src has less than
 * \p max characters, we pad dest with " " characters.
 */
static
void ucsncpy_pad(uint16_t *dest, const uint16_t *src, size_t max)
{
    char *cdest, *csrc;
    size_t len, i;

    cdest = (char*)dest;
    csrc = (char*)src;

    if (src != NULL) {
        len = MIN(ucslen(src) * 2, max);
    } else {
        len = 0;
    }

    for (i = 0; i < len; ++i)
        cdest[i] = csrc[i];

    for (i = len; i < max; i += 2) {
        cdest[i] = '\0';
        cdest[i + 1] = ' ';
    }
}

int joliet_writer_write_vol_desc(IsoImageWriter *writer)
{
    IsoImage *image;
    Ecma119Image *t;
    struct ecma119_sup_vol_desc vol;

    uint16_t *vol_id = NULL, *pub_id = NULL, *data_id = NULL;
    uint16_t *volset_id = NULL, *system_id = NULL, *application_id = NULL;
    uint16_t *copyright_file_id = NULL, *abstract_file_id = NULL;
    uint16_t *biblio_file_id = NULL;

    if (writer == NULL) {
        return ISO_OUT_OF_MEM;
    }

    t = writer->target;
    image = t->image;

    iso_msg_debug(image->id, "Write SVD for Joliet");

    memset(&vol, 0, sizeof(struct ecma119_sup_vol_desc));

    str2ucs(t->input_charset, image->volume_id, &vol_id);
    str2ucs(t->input_charset, image->publisher_id, &pub_id);
    str2ucs(t->input_charset, image->data_preparer_id, &data_id);
    str2ucs(t->input_charset, image->volset_id, &volset_id);

    str2ucs(t->input_charset, image->system_id, &system_id);
    str2ucs(t->input_charset, image->application_id, &application_id);
    str2ucs(t->input_charset, image->copyright_file_id, &copyright_file_id);
    str2ucs(t->input_charset, image->abstract_file_id, &abstract_file_id);
    str2ucs(t->input_charset, image->biblio_file_id, &biblio_file_id);

    vol.vol_desc_type[0] = 2;
    memcpy(vol.std_identifier, "CD001", 5);
    vol.vol_desc_version[0] = 1;
    ucsncpy_pad((uint16_t*)vol.volume_id, vol_id, 32);

    /* make use of UCS-2 Level 3 */
    memcpy(vol.esc_sequences, "%/E", 3);
    iso_bb(vol.vol_space_size, t->vol_space_size  - t->eff_partition_offset,
           4);
    iso_bb(vol.vol_set_size, (uint32_t) 1, 2);
    iso_bb(vol.vol_seq_number, (uint32_t) 1, 2);
    iso_bb(vol.block_size, (uint32_t) BLOCK_SIZE, 2);
    iso_bb(vol.path_table_size, t->joliet_path_table_size, 4);

    if (t->eff_partition_offset > 0) {
        /* Point to second tables and second root */
        iso_lsb(vol.l_path_table_pos,
                t->j_part_l_path_table_pos - t->eff_partition_offset, 4);
        iso_msb(vol.m_path_table_pos,
                t->j_part_m_path_table_pos - t->eff_partition_offset, 4);
        write_one_dir_record(t, t->j_part_root, 0, vol.root_dir_record, 1, 0);
    } else {
        iso_lsb(vol.l_path_table_pos, t->joliet_l_path_table_pos, 4);
        iso_msb(vol.m_path_table_pos, t->joliet_m_path_table_pos, 4);
        write_one_dir_record(t, t->joliet_root, 0, vol.root_dir_record, 1, 0);
    }

    ucsncpy_pad((uint16_t*)vol.vol_set_id, volset_id, 128);
    ucsncpy_pad((uint16_t*)vol.publisher_id, pub_id, 128);
    ucsncpy_pad((uint16_t*)vol.data_prep_id, data_id, 128);

    ucsncpy_pad((uint16_t*)vol.system_id, system_id, 32);

    ucsncpy_pad((uint16_t*)vol.application_id, application_id, 128);
    ucsncpy_pad((uint16_t*)vol.copyright_file_id, copyright_file_id, 37);
    ucsncpy_pad((uint16_t*)vol.abstract_file_id, abstract_file_id, 37);
    ucsncpy_pad((uint16_t*)vol.bibliographic_file_id, biblio_file_id, 37);

    ecma119_set_voldescr_times(writer, (struct ecma119_pri_vol_desc *) &vol);
    vol.file_structure_version[0] = 1;

    free(vol_id);
    free(volset_id);
    free(pub_id);
    free(data_id);
    free(system_id);
    free(application_id);
    free(copyright_file_id);
    free(abstract_file_id);
    free(biblio_file_id);

    /* Finally write the Volume Descriptor */
    return iso_write(t, &vol, sizeof(struct ecma119_sup_vol_desc));
}

static
int write_one_dir(Ecma119Image *t, JolietNode *dir)
{
    int ret;
    uint8_t *buffer = NULL;
    size_t i;
    size_t fi_len, len;

    /* buf will point to current write position on buffer */
    uint8_t *buf;

    /* initialize buffer with 0s */
    LIBISO_ALLOC_MEM(buffer, uint8_t, BLOCK_SIZE);
    buf = buffer;

    /* write the "." and ".." entries first */
    write_one_dir_record(t, dir, 0, buf, 1, 0);
    buf += 34;
    write_one_dir_record(t, dir, 1, buf, 1, 0);
    buf += 34;

    for (i = 0; i < dir->info.dir->nchildren; i++) {
        int section, nsections;
        JolietNode *child = dir->info.dir->children[i];

        /* compute len of directory entry */
        fi_len = ucslen(child->name) * 2;
        len = fi_len + 34;
        if (child->type == JOLIET_FILE && !(t->omit_version_numbers & 3)) {
            len += 4;
        }

        nsections = (child->type == JOLIET_FILE) ? child->info.file->nsections : 1;

        for (section = 0; section < nsections; ++section) {

            if ( (buf + len - buffer) > BLOCK_SIZE) {
                /* dir doesn't fit in current block */
                ret = iso_write(t, buffer, BLOCK_SIZE);
                if (ret < 0) {
                    goto ex;
                }
                memset(buffer, 0, BLOCK_SIZE);
                buf = buffer;
            }
            /* write the directory entry in any case */
            write_one_dir_record(t, child, -1, buf, fi_len, section);
            buf += len;
        }
    }

    /* write the last block */
    ret = iso_write(t, buffer, BLOCK_SIZE);
ex:;
    LIBISO_FREE_MEM(buffer);
    return ret;
}

static
int write_dirs(Ecma119Image *t, JolietNode *root)
{
    int ret;
    size_t i;

    /* write all directory entries for this dir */
    ret = write_one_dir(t, root);
    if (ret < 0) {
        return ret;
    }

    /* recurse */
    for (i = 0; i < root->info.dir->nchildren; i++) {
        JolietNode *child = root->info.dir->children[i];
        if (child->type == JOLIET_DIR) {
            ret = write_dirs(t, child);
            if (ret < 0) {
                return ret;
            }
        }
    }
    return ISO_SUCCESS;
}

static
int write_path_table(Ecma119Image *t, JolietNode **pathlist, int l_type)
{
    size_t i, len;
    uint8_t *buf = NULL;
    struct ecma119_path_table_record *rec;
    void (*write_int)(uint8_t*, uint32_t, int);
    JolietNode *dir;
    uint32_t path_table_size;
    int parent = 0;
    int ret= ISO_SUCCESS;
    uint8_t *zeros = NULL;

    /* 256 is just a convenient size large enought */
    LIBISO_ALLOC_MEM(buf, uint8_t, 256);
    LIBISO_ALLOC_MEM(zeros, uint8_t, BLOCK_SIZE);
    path_table_size = 0;
    write_int = l_type ? iso_lsb : iso_msb;

    for (i = 0; i < t->joliet_ndirs; i++) {
        dir = pathlist[i];

        /* find the index of the parent in the table */
        while ((i) && pathlist[parent] != dir->parent) {
            parent++;
        }

        /* write the Path Table Record (ECMA-119, 9.4) */
        memset(buf, 0, 256);
        rec = (struct ecma119_path_table_record*) buf;
        rec->len_di[0] = dir->parent ? (uint8_t) ucslen(dir->name) * 2 : 1;
        rec->len_xa[0] = 0;
        write_int(rec->block, dir->info.dir->block - t->eff_partition_offset,
                  4);
        write_int(rec->parent, parent + 1, 2);
        if (dir->parent) {
            memcpy(rec->dir_id, dir->name, rec->len_di[0]);
        }
        len = 8 + rec->len_di[0] + (rec->len_di[0] % 2);
        ret = iso_write(t, buf, len);
        if (ret < 0) {
            /* error */
            goto ex;
        }
        path_table_size += len;
    }

    /* we need to fill the last block with zeros */
    path_table_size %= BLOCK_SIZE;
    if (path_table_size) {
        len = BLOCK_SIZE - path_table_size;
        memset(zeros, 0, len);
        ret = iso_write(t, zeros, len);
    }
ex:;
    LIBISO_FREE_MEM(zeros);
    LIBISO_FREE_MEM(buf);
    return ret;
}

static
int write_path_tables(Ecma119Image *t)
{
    int ret;
    size_t i, j, cur;
    JolietNode **pathlist;

    iso_msg_debug(t->image->id, "Writing Joliet Path tables");

    /* allocate temporal pathlist */
    pathlist = malloc(sizeof(void*) * t->joliet_ndirs);
    if (pathlist == NULL) {
        return ISO_OUT_OF_MEM;
    }

    if (t->eff_partition_offset > 0) {
        pathlist[0] = t->j_part_root;
    } else {
        pathlist[0] = t->joliet_root;
    }
    cur = 1;

    for (i = 0; i < t->joliet_ndirs; i++) {
        JolietNode *dir = pathlist[i];
        for (j = 0; j < dir->info.dir->nchildren; j++) {
            JolietNode *child = dir->info.dir->children[j];
            if (child->type == JOLIET_DIR) {
                pathlist[cur++] = child;
            }
        }
    }

    /* Write L Path Table */
    ret = write_path_table(t, pathlist, 1);
    if (ret < 0) {
        goto write_path_tables_exit;
    }

    /* Write L Path Table */
    ret = write_path_table(t, pathlist, 0);

    write_path_tables_exit: ;
    free(pathlist);
    return ret;
}

static
int joliet_writer_write_dirs(IsoImageWriter *writer)
{
    int ret;
    Ecma119Image *t;
    JolietNode *root;

    t = writer->target;

    /* first of all, we write the directory structure */
    if (t->eff_partition_offset > 0) {
        root = t->j_part_root;
    } else {
        root = t->joliet_root;
    }
    ret = write_dirs(t, root);
    if (ret < 0) {
        return ret;
    }

    /* and write the path tables */
    ret = write_path_tables(t);

    return ret;
}

static
int joliet_writer_write_data(IsoImageWriter *writer)
{
    int ret;
    Ecma119Image *t;

    if (writer == NULL) {
        return ISO_NULL_POINTER;
    }
    t = writer->target;

    ret = joliet_writer_write_dirs(writer);
    if (ret < 0)
        return ret;

    if (t->partition_offset > 0) {
        t->eff_partition_offset = t->partition_offset;
        ret = joliet_writer_write_dirs(writer);
        t->eff_partition_offset = 0;
        if (ret < 0)
            return ret;
    }
    return ISO_SUCCESS;
}

static
int joliet_writer_free_data(IsoImageWriter *writer)
{
    /* free the Joliet tree */
    Ecma119Image *t = writer->target;
    joliet_node_free(t->joliet_root);
    if (t->j_part_root != NULL)
        joliet_node_free(t->j_part_root);
    t->j_part_root = NULL;
    return ISO_SUCCESS;
}

int joliet_writer_create(Ecma119Image *target)
{
    int ret;
    IsoImageWriter *writer;

    writer = malloc(sizeof(IsoImageWriter));
    if (writer == NULL) {
        return ISO_OUT_OF_MEM;
    }

    writer->compute_data_blocks = joliet_writer_compute_data_blocks;
    writer->write_vol_desc = joliet_writer_write_vol_desc;
    writer->write_data = joliet_writer_write_data;
    writer->free_data = joliet_writer_free_data;
    writer->data = NULL;
    writer->target = target;

    iso_msg_debug(target->image->id, "Creating low level Joliet tree...");
    ret = joliet_tree_create(target);
    if (ret < 0) {
        free((char *) writer);
        return ret;
    }

    /* add this writer to image */
    target->writers[target->nwriters++] = writer;

    if(target->partition_offset > 0) {
        /* Create second tree */
        target->eff_partition_offset = target->partition_offset;
        ret = joliet_tree_create(target);
        if (ret < 0) {
            return ret;
        }
        target->eff_partition_offset = 0;
    }

    /* we need the volume descriptor */
    target->curblock++;
    return ISO_SUCCESS;
}
