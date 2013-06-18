/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2007 Mario Danic
 * Copyright (c) 2009 - 2013 Thomas Schmitt
 *
 * This file is part of the libisofs project; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

/*
   Use the copy of the struct burn_source definition in libisofs.h
*/
#define LIBISOFS_WITHOUT_LIBBURN yes
#include "libisofs.h"

#include "ecma119.h"
#include "joliet.h"
#include "hfsplus.h"
#include "iso1999.h"
#include "eltorito.h"
#include "ecma119_tree.h"
#include "filesrc.h"
#include "image.h"
#include "writer.h"
#include "messages.h"
#include "rockridge.h"
#include "util.h"
#include "system_area.h"
#include "md5.h"

#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <locale.h>
#include <langinfo.h>
#include <stdio.h>

#ifdef Xorriso_standalonE

#ifdef Xorriso_with_libjtE
#include "../libjte/libjte.h"
#endif

#else

#ifdef Libisofs_with_libjtE
#include <libjte/libjte.h>
#endif

#endif /* ! Xorriso_standalonE */

/*
 * TODO #00011 : guard against bad path table usage with more than 65535 dirs
 * image with more than 65535 directories have path_table related problems
 * due to 16 bits parent id. Note that this problem only affects to folders
 * that are parent of another folder.
 */

static
void ecma119_image_free(Ecma119Image *t)
{
    size_t i;

    if (t == NULL)
        return;

    if (t->refcount > 1) {
        t->refcount--;
        return;
    }

    if (t->root != NULL)
        ecma119_node_free(t->root);
    if (t->image != NULL)
        iso_image_unref(t->image);
    if (t->files != NULL)
        iso_rbtree_destroy(t->files, iso_file_src_free);
    if (t->buffer != NULL)
        iso_ring_buffer_free(t->buffer);

    for (i = 0; i < t->nwriters; ++i) {
        IsoImageWriter *writer = t->writers[i];
        writer->free_data(writer);
        free(writer);
    }
    if (t->rr_reloc_dir != NULL)
        free(t->rr_reloc_dir);
    if (t->input_charset != NULL)
        free(t->input_charset);
    if (t->output_charset != NULL)
        free(t->output_charset);
    if (t->bootsrc != NULL)
        free(t->bootsrc);
    if (t->system_area_data != NULL)
        free(t->system_area_data);
    if (t->checksum_ctx != NULL) { /* dispose checksum context */
        char md5[16];
        iso_md5_end(&(t->checksum_ctx), md5);
    }
    if (t->checksum_buffer != NULL)
        free(t->checksum_buffer);
    if (t->writers != NULL)
        free(t->writers);
    if (t->partition_root != NULL)
        ecma119_node_free(t->partition_root);
    if (t->prep_partition != NULL)
        free(t->prep_partition);
    if (t->efi_boot_partition != NULL)
        free(t->efi_boot_partition);
    for (i = 0; i < ISO_MAX_PARTITIONS; i++)
        if (t->appended_partitions[i] != NULL)
            free(t->appended_partitions[i]);
    for (i = 0; i < ISO_HFSPLUS_BLESS_MAX; i++)
        if (t->hfsplus_blessed[i] != NULL)
            iso_node_unref(t->hfsplus_blessed[i]);
    for (i = 0; (int) i < t->apm_req_count; i++)
        if (t->apm_req[i] != NULL)
            free(t->apm_req[i]);
    for (i = 0; (int) i < t->mbr_req_count; i++)
        if (t->mbr_req[i] != NULL)
            free(t->mbr_req[i]);
    for (i = 0; (int) i < t->gpt_req_count; i++)
        if (t->gpt_req[i] != NULL)
            free(t->gpt_req[i]);

    free(t);
}

static int show_chunk_to_jte(Ecma119Image *target, char *buf, int count)
{

#ifdef Libisofs_with_libjtE

    int ret;

    if (target->libjte_handle == NULL)
        return ISO_SUCCESS;
    ret = libjte_show_data_chunk(target->libjte_handle, buf, count, 1); 
    if (ret <= 0) {
        iso_libjte_forward_msgs(target->libjte_handle,
                                target->image->id, ISO_LIBJTE_FILE_FAILED, 0);
        return ISO_LIBJTE_FILE_FAILED;
    }

#endif /* Libisofs_with_libjtE */

    return ISO_SUCCESS;
}

/**
 * Check if we should add version number ";" to the given node name.
 */
static
int need_version_number(Ecma119Image *t, Ecma119Node *n)
{
    if ((t->omit_version_numbers & 1) || t->untranslated_name_len > 0) {
        return 0;
    }
    if (n->type == ECMA119_DIR || n->type == ECMA119_PLACEHOLDER) {
        return 0;
    } else {
        return 1;
    }
}

/**
 * Compute the size of a directory entry for a single node
 */
static
size_t calc_dirent_len(Ecma119Image *t, Ecma119Node *n)
{
    int ret = n->iso_name ? strlen(n->iso_name) + 33 : 34;
    if (need_version_number(t, n)) {
        ret += 2; /* take into account version numbers */
    }
    if (ret % 2)
        ret++;
    return ret;
}

/**
 * Computes the total size of all directory entries of a single dir,
 * acording to ECMA-119 6.8.1.1
 *
 * This also take into account the size needed for RR entries and
 * SUSP continuation areas (SUSP, 5.1).
 *
 * @param ce
 *      Will be filled with the size needed for Continuation Areas
 * @return
 *      The size needed for all dir entries of the given dir, without
 *      taking into account the continuation areas.
 */
static
size_t calc_dir_size(Ecma119Image *t, Ecma119Node *dir, size_t *ce)
{
    size_t i, len;
    size_t ce_len = 0;

    /* size of "." and ".." entries */
    len = 34 + 34;
    if (t->rockridge) {
        len += rrip_calc_len(t, dir, 1, 34, &ce_len);
        *ce += ce_len;
        len += rrip_calc_len(t, dir, 2, 34, &ce_len);
        *ce += ce_len;
    }

    for (i = 0; i < dir->info.dir->nchildren; ++i) {
        size_t remaining;
        int section, nsections;
        Ecma119Node *child = dir->info.dir->children[i];

        nsections = (child->type == ECMA119_FILE) ? child->info.file->nsections : 1;
        for (section = 0; section < nsections; ++section) {
            size_t dirent_len = calc_dirent_len(t, child);
            if (t->rockridge) {
                dirent_len += rrip_calc_len(t, child, 0, dirent_len, &ce_len);
                *ce += ce_len;
            }
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
void calc_dir_pos(Ecma119Image *t, Ecma119Node *dir)
{
    size_t i, len;
    size_t ce_len = 0;

    t->ndirs++;
    dir->info.dir->block = t->curblock;
    len = calc_dir_size(t, dir, &ce_len);
    t->curblock += DIV_UP(len, BLOCK_SIZE);
    if (t->rockridge) {
        t->curblock += DIV_UP(ce_len, BLOCK_SIZE);
    }
    for (i = 0; i < dir->info.dir->nchildren; i++) {
        Ecma119Node *child = dir->info.dir->children[i];
        if (child->type == ECMA119_DIR) {
            calc_dir_pos(t, child);
        }
    }
}

/**
 * Compute the length of the path table, in bytes.
 */
static
uint32_t calc_path_table_size(Ecma119Node *dir)
{
    uint32_t size;
    size_t i;

    /* size of path table for this entry */
    size = 8;
    size += dir->iso_name ? strlen(dir->iso_name) : 1;
    size += (size % 2);

    /* and recurse */
    for (i = 0; i < dir->info.dir->nchildren; i++) {
        Ecma119Node *child = dir->info.dir->children[i];
        if (child->type == ECMA119_DIR) {
            size += calc_path_table_size(child);
        }
    }
    return size;
}

static
int ecma119_writer_compute_data_blocks(IsoImageWriter *writer)
{
    Ecma119Image *target;
    uint32_t path_table_size;
    size_t ndirs;

    if (writer == NULL) {
        return ISO_ASSERT_FAILURE;
    }

    target = writer->target;

    /* compute position of directories */
    iso_msg_debug(target->image->id, "Computing position of dir structure");
    target->ndirs = 0;
    calc_dir_pos(target, target->root);

    /* compute length of pathlist */
    iso_msg_debug(target->image->id, "Computing length of pathlist");
    path_table_size = calc_path_table_size(target->root);

    /* compute location for path tables */
    target->l_path_table_pos = target->curblock;
    target->curblock += DIV_UP(path_table_size, BLOCK_SIZE);
    target->m_path_table_pos = target->curblock;
    target->curblock += DIV_UP(path_table_size, BLOCK_SIZE);
    target->path_table_size = path_table_size;

    if (target->md5_session_checksum) {
        /* Account for first tree checksum tag */
        target->checksum_tree_tag_pos = target->curblock;
        target->curblock++;
    }

    if (target->partition_offset > 0) {
        /* Take into respect the second directory tree */
        ndirs = target->ndirs;
        target->ndirs = 0;
        calc_dir_pos(target, target->partition_root);
        if (target->ndirs != ndirs) {
            iso_msg_submit(target->image->id, ISO_ASSERT_FAILURE, 0,
                    "Number of directories differs in ECMA-119 partiton_tree");
            return ISO_ASSERT_FAILURE;
	}
        /* Take into respect the second set of path tables */
        path_table_size = calc_path_table_size(target->partition_root);
        target->partition_l_table_pos = target->curblock;
        target->curblock += DIV_UP(path_table_size, BLOCK_SIZE);
        target->partition_m_table_pos = target->curblock;
        target->curblock += DIV_UP(path_table_size, BLOCK_SIZE);

        /* >>> TWINTREE: >>> For now, checksum tags are only for the
                             image start and not for the partition */;

    }

    target->tree_end_block = target->curblock;

    return ISO_SUCCESS;
}

/**
 * Write a single directory record (ECMA-119, 9.1)
 *
 * @param file_id
 *     if >= 0, we use it instead of the filename (for "." and ".." entries).
 * @param len_fi
 *     Computed length of the file identifier. Total size of the directory
 *     entry will be len + 33 + padding if needed (ECMA-119, 9.1.12)
 * @param info
 *     SUSP entries for the given directory record. It will be NULL for the
 *     root directory record in the PVD (ECMA-119, 8.4.18) (in order to
 *     distinguish it from the "." entry in the root directory)
 */
static
void write_one_dir_record(Ecma119Image *t, Ecma119Node *node, int file_id,
                          uint8_t *buf, size_t len_fi, struct susp_info *info,
                          int extent)
{
    uint32_t len;
    uint32_t block;
    uint8_t len_dr; /*< size of dir entry without SUSP fields */
    int multi_extend = 0;
    uint8_t *name = (file_id >= 0) ? (uint8_t*)&file_id
            : (uint8_t*)node->iso_name;

    struct ecma119_dir_record *rec = (struct ecma119_dir_record*)buf;
    IsoNode *iso;

    len_dr = 33 + len_fi + ((len_fi % 2) ? 0 : 1);

    memcpy(rec->file_id, name, len_fi);

    if (need_version_number(t, node)) {
        len_dr += 2;
        rec->file_id[len_fi++] = ';';
        rec->file_id[len_fi++] = '1';
    }

    if (node->type == ECMA119_DIR) {
        /* use the cached length */
        len = node->info.dir->len;
        block = node->info.dir->block;
    } else if (node->type == ECMA119_FILE) {
        block = node->info.file->sections[extent].block;
        len = node->info.file->sections[extent].size;
        multi_extend = (node->info.file->nsections - 1 == extent) ? 0 : 1;
    } else {
        /*
         * for nodes other than files and dirs, we set len to 0, and
         * the content block address to a dummy value.
         */
        len = 0;
        if (! t->old_empty)
            block = t->empty_file_block;
        else
            block = 0;
    }

    /*
     * For ".." entry we need to write the parent info!
     */
    if (file_id == 1 && node->parent)
        node = node->parent;

    rec->len_dr[0] = len_dr + (info != NULL ? info->suf_len : 0);
    iso_bb(rec->block, block - t->eff_partition_offset, 4);
    iso_bb(rec->length, len, 4);
    if (t->dir_rec_mtime & 1) {
        iso= node->node;
        iso_datetime_7(rec->recording_time,
                       t->replace_timestamps ? t->timestamp : iso->mtime,
                       t->always_gmt);
    } else {
        iso_datetime_7(rec->recording_time, t->now, t->always_gmt);
    }
    rec->flags[0] = ((node->type == ECMA119_DIR) ? 2 : 0) | (multi_extend ? 0x80 : 0);
    iso_bb(rec->vol_seq_number, (uint32_t) 1, 2);
    rec->len_fi[0] = len_fi;

    /*
     * and finally write the SUSP fields.
     */
    if (info != NULL) {
        rrip_write_susp_fields(t, info, buf + len_dr);
    }
}

static
char *get_relaxed_vol_id(Ecma119Image *t, const char *name)
{
    int ret;
    if (name == NULL) {
        return NULL;
    }
    if (strcmp(t->input_charset, t->output_charset)) {
        /* charset conversion needed */
        char *str;
        ret = strconv(name, t->input_charset, t->output_charset, &str);
        if (ret == ISO_SUCCESS) {
            return str;
        }
        iso_msg_submit(t->image->id, ISO_FILENAME_WRONG_CHARSET, ret,
                  "Charset conversion error. Cannot convert from %s to %s",
                  t->input_charset, t->output_charset);
    }
    return strdup(name);
}

/**
 * Set the timestamps of Primary, Supplementary, or Enhanced Volume Descriptor.
 */
void ecma119_set_voldescr_times(IsoImageWriter *writer,
                                struct ecma119_pri_vol_desc *vol)
{
    Ecma119Image *t = writer->target;
    int i;

    if (t->vol_uuid[0]) {
        for(i = 0; i < 16; i++)
            if(t->vol_uuid[i] < '0' || t->vol_uuid[i] > '9')
        break;
            else
                vol->vol_creation_time[i] = t->vol_uuid[i];
       for(; i < 16; i++)
           vol->vol_creation_time[i] = '1';
       vol->vol_creation_time[16] = 0;
    } else if (t->vol_creation_time > 0)
        iso_datetime_17(vol->vol_creation_time, t->vol_creation_time,
                        t->always_gmt);
    else
        iso_datetime_17(vol->vol_creation_time, t->now, t->always_gmt);

    if (t->vol_uuid[0]) {
        for(i = 0; i < 16; i++)
            if(t->vol_uuid[i] < '0' || t->vol_uuid[i] > '9')
        break;
            else
                vol->vol_modification_time[i] = t->vol_uuid[i];
       for(; i < 16; i++)
           vol->vol_modification_time[i] = '1';
       vol->vol_modification_time[16] = 0;
    } else if (t->vol_modification_time > 0)
        iso_datetime_17(vol->vol_modification_time, t->vol_modification_time,
                        t->always_gmt);
    else
        iso_datetime_17(vol->vol_modification_time, t->now, t->always_gmt);

    if (t->vol_expiration_time > 0) {
        iso_datetime_17(vol->vol_expiration_time, t->vol_expiration_time,
                        t->always_gmt);
    } else {
       for(i = 0; i < 16; i++)
           vol->vol_expiration_time[i] = '0';
       vol->vol_expiration_time[16] = 0;
    }

    if (t->vol_effective_time > 0) {
        iso_datetime_17(vol->vol_effective_time, t->vol_effective_time,
                        t->always_gmt);
    } else {
       for(i = 0; i < 16; i++)
           vol->vol_effective_time[i] = '0';
       vol->vol_effective_time[16] = 0;
    }
}

/**
 * Write the Primary Volume Descriptor (ECMA-119, 8.4)
 */
static
int ecma119_writer_write_vol_desc(IsoImageWriter *writer)
{
    IsoImage *image;
    Ecma119Image *t;
    struct ecma119_pri_vol_desc vol;
    char *vol_id, *pub_id, *data_id, *volset_id;
    char *system_id, *application_id, *copyright_file_id;
    char *abstract_file_id, *biblio_file_id;

    if (writer == NULL) {
        return ISO_ASSERT_FAILURE;
    }

    t = writer->target;
    image = t->image;

    iso_msg_debug(image->id, "Write Primary Volume Descriptor");

    memset(&vol, 0, sizeof(struct ecma119_pri_vol_desc));

    if (t->relaxed_vol_atts) {
        vol_id = get_relaxed_vol_id(t, image->volume_id);
        volset_id = get_relaxed_vol_id(t, image->volset_id);
    } else {
        str2d_char(t->input_charset, image->volume_id, &vol_id);
        str2d_char(t->input_charset, image->volset_id, &volset_id);
    }
    str2a_char(t->input_charset, image->publisher_id, &pub_id);
    str2a_char(t->input_charset, image->data_preparer_id, &data_id);
    str2a_char(t->input_charset, image->system_id, &system_id);
    str2a_char(t->input_charset, image->application_id, &application_id);
    str2d_char(t->input_charset, image->copyright_file_id, &copyright_file_id);
    str2d_char(t->input_charset, image->abstract_file_id, &abstract_file_id);
    str2d_char(t->input_charset, image->biblio_file_id, &biblio_file_id);

    vol.vol_desc_type[0] = 1;
    memcpy(vol.std_identifier, "CD001", 5);
    vol.vol_desc_version[0] = 1;
    strncpy_pad((char*)vol.system_id, system_id, 32);
    strncpy_pad((char*)vol.volume_id, vol_id, 32);
    iso_bb(vol.vol_space_size, t->vol_space_size  - t->eff_partition_offset,
           4);
    iso_bb(vol.vol_set_size, (uint32_t) 1, 2);
    iso_bb(vol.vol_seq_number, (uint32_t) 1, 2);
    iso_bb(vol.block_size, (uint32_t) BLOCK_SIZE, 2);
    iso_bb(vol.path_table_size, t->path_table_size, 4);

    if (t->eff_partition_offset > 0) {
        /* Point to second tables and second root */
        iso_lsb(vol.l_path_table_pos,
                t->partition_l_table_pos - t->eff_partition_offset, 4);
        iso_msb(vol.m_path_table_pos,
                t->partition_m_table_pos - t->eff_partition_offset, 4);
        write_one_dir_record(t, t->partition_root, 0,
                             vol.root_dir_record, 1, NULL, 0);
    } else {
        iso_lsb(vol.l_path_table_pos, t->l_path_table_pos, 4);
        iso_msb(vol.m_path_table_pos, t->m_path_table_pos, 4);
        write_one_dir_record(t, t->root, 0, vol.root_dir_record, 1, NULL, 0);
    }

    strncpy_pad((char*)vol.vol_set_id, volset_id, 128);
    strncpy_pad((char*)vol.publisher_id, pub_id, 128);
    strncpy_pad((char*)vol.data_prep_id, data_id, 128);

    strncpy_pad((char*)vol.application_id, application_id, 128);
    strncpy_pad((char*)vol.copyright_file_id, copyright_file_id, 37);
    strncpy_pad((char*)vol.abstract_file_id, abstract_file_id, 37);
    strncpy_pad((char*)vol.bibliographic_file_id, biblio_file_id, 37);

    ecma119_set_voldescr_times(writer, &vol);
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
    return iso_write(t, &vol, sizeof(struct ecma119_pri_vol_desc));
}

static
int write_one_dir(Ecma119Image *t, Ecma119Node *dir, Ecma119Node *parent)
{
    int ret;
    uint8_t *buffer = NULL;
    size_t i;
    size_t fi_len, len;
    struct susp_info info;

    /* buf will point to current write position on buffer */
    uint8_t *buf;

    LIBISO_ALLOC_MEM(buffer, uint8_t, BLOCK_SIZE);
    buf = buffer;

    /*
     * set susp_info to 0's, this way code for both plain ECMA-119 and
     * RR is very similar
     */
    memset(&info, 0, sizeof(struct susp_info));
    if (t->rockridge) {
        /* initialize the ce_block, it might be needed */
        info.ce_block = dir->info.dir->block + DIV_UP(dir->info.dir->len,
                                                      BLOCK_SIZE);
    }

    /* write the "." and ".." entries first */
    if (t->rockridge) {
        ret = rrip_get_susp_fields(t, dir, 1, 34, &info);
        if (ret < 0) {
            goto ex;
        }
    }
    len = 34 + info.suf_len;
    write_one_dir_record(t, dir, 0, buf, 1, &info, 0);
    buf += len;

    if (t->rockridge) {
        ret = rrip_get_susp_fields(t, dir, 2, 34, &info);
        if (ret < 0) {
            goto ex;
        }
    }
    len = 34 + info.suf_len;
    write_one_dir_record(t, parent, 1, buf, 1, &info, 0);
    buf += len;

    for (i = 0; i < dir->info.dir->nchildren; i++) {
        int section, nsections;
        Ecma119Node *child = dir->info.dir->children[i];

        fi_len = strlen(child->iso_name);

        nsections = (child->type == ECMA119_FILE) ? child->info.file->nsections : 1;
        for (section = 0; section < nsections; ++section) {

            /* compute len of directory entry */
            len = fi_len + 33 + ((fi_len % 2) ? 0 : 1);
            if (need_version_number(t, child)) {
                len += 2;
            }

            /* get the SUSP fields if rockridge is enabled */
            if (t->rockridge) {
                ret = rrip_get_susp_fields(t, child, 0, len, &info);
                if (ret < 0) {
                    goto ex;
                }
                len += info.suf_len;
            }

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
            write_one_dir_record(t, child, -1, buf, fi_len, &info, section);
            buf += len;
        }
    }

    /* write the last block */
    ret = iso_write(t, buffer, BLOCK_SIZE);
    if (ret < 0) {
        goto ex;
    }

    /* write the Continuation Area if needed */
    if (info.ce_len > 0) {
        ret = rrip_write_ce_fields(t, &info);
    }

ex:;
    LIBISO_FREE_MEM(buffer);
    return ret;
}

static
int write_dirs(Ecma119Image *t, Ecma119Node *root, Ecma119Node *parent)
{
    int ret;
    size_t i;

    /* write all directory entries for this dir */
    ret = write_one_dir(t, root, parent);
    if (ret < 0) {
        return ret;
    }

    /* recurse */
    for (i = 0; i < root->info.dir->nchildren; i++) {
        Ecma119Node *child = root->info.dir->children[i];
        if (child->type == ECMA119_DIR) {
            ret = write_dirs(t, child, root);
            if (ret < 0) {
                return ret;
            }
        }
    }
    return ISO_SUCCESS;
}

static
int write_path_table(Ecma119Image *t, Ecma119Node **pathlist, int l_type)
{
    size_t i, len;
    uint8_t buf[64]; /* 64 is just a convenient size larger enought */
    struct ecma119_path_table_record *rec;
    void (*write_int)(uint8_t*, uint32_t, int);
    Ecma119Node *dir;
    uint32_t path_table_size;
    int parent = 0;
    int ret= ISO_SUCCESS;
    uint8_t *zeros = NULL;

    path_table_size = 0;
    write_int = l_type ? iso_lsb : iso_msb;

    for (i = 0; i < t->ndirs; i++) {
        dir = pathlist[i];

        /* find the index of the parent in the table */
        while ((i) && pathlist[parent] != dir->parent) {
            parent++;
        }

        /* write the Path Table Record (ECMA-119, 9.4) */
        memset(buf, 0, 64);
        rec = (struct ecma119_path_table_record*) buf;
        rec->len_di[0] = dir->parent ? (uint8_t) strlen(dir->iso_name) : 1;
        rec->len_xa[0] = 0;
        write_int(rec->block, dir->info.dir->block - t->eff_partition_offset,
                  4);
        write_int(rec->parent, parent + 1, 2);
        if (dir->parent) {
            memcpy(rec->dir_id, dir->iso_name, rec->len_di[0]);
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
        LIBISO_ALLOC_MEM(zeros, uint8_t, len);
        ret = iso_write(t, zeros, len);
    }
ex:;
    LIBISO_FREE_MEM(zeros);
    return ret;
}

static
int write_path_tables(Ecma119Image *t)
{
    int ret;
    size_t i, j, cur;
    Ecma119Node **pathlist;

    iso_msg_debug(t->image->id, "Writing ISO Path tables");

    /* allocate temporal pathlist */
    pathlist = malloc(sizeof(void*) * t->ndirs);
    if (pathlist == NULL) {
        return ISO_OUT_OF_MEM;
    }

    if (t->eff_partition_offset > 0) {
        pathlist[0] = t->partition_root;
    } else {
        pathlist[0] = t->root;
    }
    cur = 1;

    for (i = 0; i < t->ndirs; i++) {
        Ecma119Node *dir = pathlist[i];
        for (j = 0; j < dir->info.dir->nchildren; j++) {
            Ecma119Node *child = dir->info.dir->children[j];
            if (child->type == ECMA119_DIR) {
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


/**
 * Write the directory structure (ECMA-119, 6.8) and the L and M
 * Path Tables (ECMA-119, 6.9).
 */
static
int ecma119_writer_write_dirs(IsoImageWriter *writer)
{
    int ret, isofs_ca_changed = 0;
    Ecma119Image *t;
    Ecma119Node *root;
    char *value = NULL;
    size_t value_length;

    t = writer->target;

    /* first of all, we write the directory structure */
    if (t->eff_partition_offset > 0) {
        root = t->partition_root;

        if ((t->md5_file_checksums & 1) || t->md5_session_checksum) {
            /* Take into respect the address offset in "isofs.ca" */
            ret = iso_node_lookup_attr((IsoNode *) t->image->root, "isofs.ca",
                                       &value_length, &value, 0);
            if (value != NULL)
                free(value);
            if (ret == 1 && value_length == 20) {
                /* "isofs.ca" does really exist and has the expected length */
                ret = iso_root_set_isofsca((IsoNode *) t->image->root,
                             t->checksum_range_start - t->eff_partition_offset,
                             t->checksum_array_pos - t->eff_partition_offset,
                             t->checksum_idx_counter + 2, 16, "MD5", 0);
                if (ret < 0)
                    return ret;
                isofs_ca_changed = 1;
            }
        }
    } else {
        root = t->root;
    }
    ret = write_dirs(t, root, root);
    if (ret < 0) {
        return ret;
    }

    /* and write the path tables */
    ret = write_path_tables(t);
    if (ret < 0)
        return ret;
    if (t->md5_session_checksum) {
        /* Write tree checksum tag */
        if (t->eff_partition_offset > 0) {
            /* >>> TWINTREE: >>> For now, tags are only for the
                                 image start and not for the partition */;
        } else {
            ret = iso_md5_write_tag(t, 3);
        }
    }
    if (isofs_ca_changed) {
        /* Restore old addresses offset in "isofs.ca" of root node */
        ret = iso_root_set_isofsca((IsoNode *) t->image->root,
                             t->checksum_range_start,
                             t->checksum_array_pos,
                             t->checksum_idx_counter + 2, 16, "MD5", 0);
        if (ret < 0)
            return ret;
    }
    return ret;
}

/**
 * Write directory structure and Path Tables of the ECMA-119 tree.
 * This happens eventually a second time for the duplicates which use
 * addresses with partition offset.
 */
static
int ecma119_writer_write_data(IsoImageWriter *writer)
{
    int ret;
    Ecma119Image *t;
    uint32_t curblock;
    char *msg = NULL;

    if (writer == NULL)
        {ret = ISO_ASSERT_FAILURE; goto ex;}

    t = writer->target;

    ret = ecma119_writer_write_dirs(writer);
    if (ret < 0)
        goto ex;

    if (t->partition_offset > 0) {
        t->eff_partition_offset = t->partition_offset;
        ret = ecma119_writer_write_dirs(writer);
        t->eff_partition_offset = 0;
        if (ret < 0) 
            goto ex;
    }

    curblock = (t->bytes_written / 2048) + t->ms_block;
    if (curblock != t->tree_end_block) {
        LIBISO_ALLOC_MEM(msg, char, 100);
        sprintf(msg,
                "Calculated and written ECMA-119 tree end differ: %lu <> %lu",
                (unsigned long) t->tree_end_block,
                (unsigned long) curblock);
        iso_msgs_submit(0, msg, 0, "WARNING", 0);

        t->tree_end_block = 1;/* Mark for harsher reaction at end of writing */
    }
    ret = ISO_SUCCESS;
ex:;
    LIBISO_FREE_MEM(msg);
    return ret;
}

static
int ecma119_writer_free_data(IsoImageWriter *writer)
{
    /* nothing to do */
    return ISO_SUCCESS;
}

int ecma119_writer_create(Ecma119Image *target)
{
    int ret;
    IsoImageWriter *writer;

    writer = malloc(sizeof(IsoImageWriter));
    if (writer == NULL) {
        return ISO_OUT_OF_MEM;
    }

    writer->compute_data_blocks = ecma119_writer_compute_data_blocks;
    writer->write_vol_desc = ecma119_writer_write_vol_desc;
    writer->write_data = ecma119_writer_write_data;
    writer->free_data = ecma119_writer_free_data;
    writer->data = NULL;
    writer->target = target;

    /* add this writer to image */
    target->writers[target->nwriters++] = writer;

    iso_msg_debug(target->image->id, "Creating low level ECMA-119 tree...");
    ret = ecma119_tree_create(target);
    if (ret < 0) {
        return ret;
    }

    if (target->image->sparc_core_node != NULL) {
        /* Obtain a duplicate of the IsoFile's Ecma119Node->file */
        ret = iso_file_src_create(target, target->image->sparc_core_node,
                                  &target->sparc_core_src);
        if (ret < 0)
            return ret;
    }

    if(target->partition_offset > 0) {
        /* Create second tree */
        target->eff_partition_offset = target->partition_offset;
        ret = ecma119_tree_create(target);
        target->eff_partition_offset = 0;
        if (ret < 0)
            return ret;
    }

    /* we need the volume descriptor */
    target->curblock++;
    return ISO_SUCCESS;
}

/** compute how many padding bytes are needed */
static
int mspad_writer_compute_data_blocks(IsoImageWriter *writer)
{
    Ecma119Image *target;
    uint32_t min_size;

    if (writer == NULL) {
        return ISO_ASSERT_FAILURE;
    }
    target = writer->target;
    min_size = 32 + target->partition_offset;
    if (target->curblock < min_size) {
        target->mspad_blocks = min_size - target->curblock;
        target->curblock = min_size;
    }
    return ISO_SUCCESS;
}

static
int mspad_writer_write_vol_desc(IsoImageWriter *writer)
{
    /* nothing to do */
    return ISO_SUCCESS;
}

static
int mspad_writer_write_data(IsoImageWriter *writer)
{
    int ret;
    Ecma119Image *t;
    uint8_t *pad = NULL;
    size_t i;

    if (writer == NULL) {
        {ret = ISO_ASSERT_FAILURE; goto ex;}
    }
    t = writer->target;

    if (t->mspad_blocks == 0) {
        {ret = ISO_SUCCESS; goto ex;}
    }

    LIBISO_ALLOC_MEM(pad, uint8_t, BLOCK_SIZE);
    for (i = 0; i < t->mspad_blocks; ++i) {
        ret = iso_write(t, pad, BLOCK_SIZE);
        if (ret < 0) {
            goto ex;
        }
    }

    ret = ISO_SUCCESS;
ex:;
    LIBISO_FREE_MEM(pad);
    return ret;
}

static
int mspad_writer_free_data(IsoImageWriter *writer)
{
    /* nothing to do */
    return ISO_SUCCESS;
}

static
int mspad_writer_create(Ecma119Image *target)
{
    IsoImageWriter *writer;

    writer = malloc(sizeof(IsoImageWriter));
    if (writer == NULL) {
        return ISO_OUT_OF_MEM;
    }

    writer->compute_data_blocks = mspad_writer_compute_data_blocks;
    writer->write_vol_desc = mspad_writer_write_vol_desc;
    writer->write_data = mspad_writer_write_data;
    writer->free_data = mspad_writer_free_data;
    writer->data = NULL;
    writer->target = target;

    /* add this writer to image */
    target->writers[target->nwriters++] = writer;
    return ISO_SUCCESS;
}


/** ----- Zero padding writer ----- */

struct iso_zero_writer_data_struct {
    uint32_t num_blocks;
};

static
int zero_writer_compute_data_blocks(IsoImageWriter *writer)
{
    Ecma119Image *target;
    struct iso_zero_writer_data_struct *data;

    if (writer == NULL)
        return ISO_ASSERT_FAILURE;
    target = writer->target;
    data = (struct iso_zero_writer_data_struct *) writer->data;
    target->curblock += data->num_blocks;
    return ISO_SUCCESS;
}

static
int zero_writer_write_vol_desc(IsoImageWriter *writer)
{
    /* nothing to do */
    return ISO_SUCCESS;
}

static
int zero_writer_write_data(IsoImageWriter *writer)
{
    int ret;
    Ecma119Image *t;
    struct iso_zero_writer_data_struct *data;
    uint8_t *pad = NULL;
    size_t i;

    if (writer == NULL)
        {ret = ISO_ASSERT_FAILURE; goto ex;}
    t = writer->target;
    data = (struct iso_zero_writer_data_struct *) writer->data;

    if (data->num_blocks == 0) 
        {ret = ISO_SUCCESS; goto ex;}
    LIBISO_ALLOC_MEM(pad, uint8_t, BLOCK_SIZE);
    for (i = 0; i < data->num_blocks; ++i) {
        ret = iso_write(t, pad, BLOCK_SIZE);
        if (ret < 0) 
            goto ex;
    }
    ret = ISO_SUCCESS;
ex:;
    LIBISO_FREE_MEM(pad);
    return ret;
}

static
int zero_writer_free_data(IsoImageWriter *writer)
{
    if (writer == NULL)
        return ISO_SUCCESS;
    if (writer->data == NULL)
        return ISO_SUCCESS;
    free(writer->data);
    writer->data = NULL;
    return ISO_SUCCESS;
}

static
int tail_writer_compute_data_blocks(IsoImageWriter *writer)
{
    int ret;
    Ecma119Image *target;
    struct iso_zero_writer_data_struct *data;
    char msg[80];

    target = writer->target;
    ret = iso_align_isohybrid(target, 0);
    if (ret < 0)
        return ret;
    data = (struct iso_zero_writer_data_struct *) writer->data;
    if (data->num_blocks != target->tail_blocks) {
        sprintf(msg, "Aligned image size to cylinder size by %d blocks",
                     target->tail_blocks - data->num_blocks);
        iso_msgs_submit(0, msg, 0, "NOTE", 0);
        data->num_blocks = target->tail_blocks;
    }
    if (target->tail_blocks <= 0)
        return ISO_SUCCESS;
    ret = zero_writer_compute_data_blocks(writer);
    return ret;
}

/*
  @param flag bit0= use tail_writer_compute_data_blocks rather than
                    zero_writer_compute_data_blocks
*/
static
int zero_writer_create(Ecma119Image *target, uint32_t num_blocks, int flag)
{
    IsoImageWriter *writer;
    struct iso_zero_writer_data_struct *data;

    writer = malloc(sizeof(IsoImageWriter));
    if (writer == NULL) {
        return ISO_OUT_OF_MEM;
    }
    data = calloc(1, sizeof(struct iso_zero_writer_data_struct));
    if (data == NULL) {
        free(writer);
        return ISO_OUT_OF_MEM;
    }
    data->num_blocks = num_blocks;

    if (flag & 1) {
        writer->compute_data_blocks = tail_writer_compute_data_blocks;
    } else {
        writer->compute_data_blocks = zero_writer_compute_data_blocks;
    }
    writer->write_vol_desc = zero_writer_write_vol_desc;
    writer->write_data = zero_writer_write_data;
    writer->free_data = zero_writer_free_data;
    writer->data = data;
    writer->target = target;

    /* add this writer to image */
    target->writers[target->nwriters++] = writer;
    return ISO_SUCCESS;
}

static
int transplant_checksum_buffer(Ecma119Image *target, int flag)
{
    /* Transplant checksum buffer from Ecma119Image to IsoImage */
    iso_image_set_checksums(target->image, target->checksum_buffer,
                            target->checksum_range_start,
                            target->checksum_array_pos,
                            target->checksum_idx_counter + 2, 0);
    target->checksum_buffer = NULL;
    target->checksum_idx_counter = 0;
    return 1;
}

static
int write_vol_desc_terminator(Ecma119Image *target)
{
    int ret;
    uint8_t *buf = NULL;
    struct ecma119_vol_desc_terminator *vol;

    LIBISO_ALLOC_MEM(buf, uint8_t, BLOCK_SIZE);

    vol = (struct ecma119_vol_desc_terminator *) buf;

    vol->vol_desc_type[0] = 255;
    memcpy(vol->std_identifier, "CD001", 5);
    vol->vol_desc_version[0] = 1;

    ret = iso_write(target, buf, BLOCK_SIZE);
ex:
    LIBISO_FREE_MEM(buf);
    return ret;
}


/* @param flag bit0= initialize system area by target->opts_overwrite
               bit1= fifo is not yet draining. Inquire write_count from fifo.
*/
static
int write_head_part1(Ecma119Image *target, int *write_count, int flag)
{
    int res, i;
    uint8_t *sa;
    IsoImageWriter *writer;
    size_t buffer_size = 0, buffer_free = 0, buffer_start_free = 0;

    sa = target->sys_area_as_written;
    iso_ring_buffer_get_buf_status(target->buffer, &buffer_size,
                                   &buffer_start_free);
    *write_count = 0;
    /* Write System Area (ECMA-119, 6.2.1) */
    if ((flag & 1) && target->opts_overwrite != NULL)
        memcpy(sa, target->opts_overwrite, 16 * BLOCK_SIZE);
    res = iso_write_system_area(target, sa);
    if (res < 0)
        goto write_error;
    res = iso_write(target, sa, 16 * BLOCK_SIZE);
    if (res < 0)
        goto write_error;
    *write_count = 16;

    /* write volume descriptors, one per writer */
    iso_msg_debug(target->image->id, "Write volume descriptors");
    for (i = 0; i < (int) target->nwriters; ++i) {
        writer = target->writers[i];
        res = writer->write_vol_desc(writer);
        if (res < 0) 
            goto write_error;
    }

    /* write Volume Descriptor Set Terminator (ECMA-119, 8.3) */
    res = write_vol_desc_terminator(target);
    if (res < 0)
        goto write_error;

    if(flag & 2) {
      iso_ring_buffer_get_buf_status(target->buffer, &buffer_size,
                                     &buffer_free);
      *write_count = ( buffer_start_free - buffer_free ) / BLOCK_SIZE;
    } else {
      *write_count = target->bytes_written / BLOCK_SIZE;
    }

    return ISO_SUCCESS;
write_error:;
    return res;
}

static
int write_head_part2(Ecma119Image *target, int *write_count, int flag)
{
    int ret, i;
    uint8_t *buf = NULL;
    IsoImageWriter *writer;

    if (target->partition_offset <= 0)
        {ret = ISO_SUCCESS; goto ex;}

    /* Write multi-session padding up to target->partition_offset + 16 */
    LIBISO_ALLOC_MEM(buf, uint8_t, BLOCK_SIZE);
    for(; *write_count < (int) target->partition_offset + 16;
        (*write_count)++) {
        ret = iso_write(target, buf, BLOCK_SIZE);
        if (ret < 0)
            goto ex;
    }

    /* Write volume descriptors subtracting
      target->partiton_offset from any LBA pointer.
    */
    target->eff_partition_offset = target->partition_offset;
    for (i = 0; i < (int) target->nwriters; ++i) {
        writer = target->writers[i];
        /* Not all writers have an entry in the partion volume descriptor set.
           It must be guaranteed that they write exactly one block.
        */

        /* >>> TWINTREE: Enhance ISO1999 writer and add it here */

        if(writer->write_vol_desc != ecma119_writer_write_vol_desc &&
           writer->write_vol_desc != joliet_writer_write_vol_desc)
    continue;
        ret = writer->write_vol_desc(writer);
        if (ret < 0)
            goto ex;
        (*write_count)++;
    }
    ret = write_vol_desc_terminator(target);
    if (ret < 0)
        goto ex;
    (*write_count)++;
    target->eff_partition_offset = 0;

    /* >>> TWINTREE: Postponed for now:
                     Write second superblock checksum tag */;

    ret = ISO_SUCCESS;
ex:;
    if (buf != NULL)
        free(buf);
    return ret;
}

static
int write_head_part(Ecma119Image *target, int flag)
{
    int res, write_count = 0;

    /* System area and volume descriptors */
    res = write_head_part1(target, &write_count, 0);
    if (res < 0)
        return res;

    /* Write superblock checksum tag */
    if (target->md5_session_checksum && target->checksum_ctx != NULL) {
        res = iso_md5_write_tag(target, 2);
        if (res < 0)
            return res;
        write_count++;
    }

    /* Second set of system area and volume descriptors for partition_offset */
    res = write_head_part2(target, &write_count, 0);
    if (res < 0)
        return res;
    return ISO_SUCCESS;
}


/* Eventually end Jigdo Template Extraction */
static int finish_libjte(Ecma119Image *target)
{
#ifdef Libisofs_with_libjtE

    int ret;

    if (target->libjte_handle != NULL) {
        ret = libjte_write_footer(target->libjte_handle);
        if (ret <= 0) {
            iso_libjte_forward_msgs(target->libjte_handle, 
                                target->image->id, ISO_LIBJTE_END_FAILED, 0);
            return ISO_LIBJTE_END_FAILED;
        }
    }

#endif /* Libisofs_with_libjtE */

    return 1;
}


int iso_write_partition_file(Ecma119Image *target, char *path,
                             uint32_t prepad, uint32_t blocks, int flag)
{
    FILE *fp = NULL;
    uint32_t i;
    uint8_t *buf = NULL;
    int ret;

    LIBISO_ALLOC_MEM(buf, uint8_t, BLOCK_SIZE);
    for (i = 0; i < prepad; i++) {
        ret = iso_write(target, buf, BLOCK_SIZE);
        if (ret < 0)
            goto ex;
    }

    fp = fopen(path, "rb");
    if (fp == NULL)
        {ret = ISO_BAD_PARTITION_FILE; goto ex;}

    for (i = 0; i < blocks; i++) {
        memset(buf, 0, BLOCK_SIZE);
        if (fp != NULL) {
            ret = fread(buf, 1, BLOCK_SIZE, fp);
            if (ret != BLOCK_SIZE) {
                 fclose(fp);
                 fp = NULL;
            }
        }
        ret = iso_write(target, buf, BLOCK_SIZE);
        if (ret < 0) {
            fclose(fp);
            goto ex;
        }
    }
    if (fp != NULL) 
        fclose(fp);
    ret = ISO_SUCCESS;
ex:;
    LIBISO_FREE_MEM(buf);
    return ret;
}


static
void *write_function(void *arg)
{
    int res, first_partition = 1, last_partition = 0, sa_type;
    int i;
    IsoImageWriter *writer;

    Ecma119Image *target = (Ecma119Image*)arg;
    iso_msg_debug(target->image->id, "Starting image writing...");

    target->bytes_written = (off_t) 0;
    target->percent_written = 0;

    res = write_head_part(target, 0);
    if (res < 0)
        goto write_error;

    /* write data for each writer */
    for (i = 0; i < (int) target->nwriters; ++i) {
        writer = target->writers[i];
        res = writer->write_data(writer);
        if (res < 0) {
            goto write_error;
        }
    }

    /* Append partition data */
    sa_type = (target->system_area_options >> 2) & 0x3f;
    if (sa_type == 0) { /* MBR */
        first_partition = 1;
        last_partition = 4;
    } else if (sa_type == 3) { /* SUN Disk Label */
        first_partition = 2;
        last_partition = 8;
    }
    for (i = first_partition - 1; i <= last_partition - 1; i++) {
        if (target->appended_partitions[i] == NULL)
    continue;
        if (target->appended_partitions[i][0] == 0)
    continue;
        res = iso_write_partition_file(target, target->appended_partitions[i],
                                       target->appended_part_prepad[i],
                                       target->appended_part_size[i], 0);
        if (res < 0)
            goto write_error;
    }

    /* Transplant checksum buffer from Ecma119Image to IsoImage */
    transplant_checksum_buffer(target, 0);

    iso_ring_buffer_writer_close(target->buffer, 0);

    res = finish_libjte(target);
    if (res <= 0)
        goto write_error;

    target->image->generator_is_running = 0;

    /* Give up reference claim made in ecma119_image_new().
       Eventually free target */
    ecma119_image_free(target);

    if (target->tree_end_block == 1) {
        iso_msgs_submit(0,
 "Image is most likely damaged. Calculated/written tree end address mismatch.",
                        0, "FATAL", 0);
    }
    if (target->bytes_written != target->total_size) {
        iso_msg_debug(target->image->id,
                 "bytes_written = %.f <-> total_size = %.f",
                 (double) target->bytes_written, (double) target->total_size);
        iso_msgs_submit(0,
"Image is most likely damaged. Calculated/written image end address mismatch.",
                        0, "FATAL", 0);
    }

#ifdef Libisofs_with_pthread_exiT
    pthread_exit(NULL);
#else
    return NULL;
#endif

    write_error: ;
    if (res != (int) ISO_LIBJTE_END_FAILED)
        finish_libjte(target);
    target->eff_partition_offset = 0;
    if (res == (int) ISO_CANCELED) {
        /* canceled */
        if (!target->will_cancel)
            iso_msg_submit(target->image->id, ISO_IMAGE_WRITE_CANCELED,
                           0, NULL);
    } else {
        /* image write error */
        iso_msg_submit(target->image->id, ISO_WRITE_ERROR, res,
                   "Image write error");
    }
    iso_ring_buffer_writer_close(target->buffer, 1);

    /* Transplant checksum buffer away from Ecma119Image */
    transplant_checksum_buffer(target, 0);
    /* Invalidate the transplanted checksum buffer in IsoImage */
    iso_image_free_checksums(target->image, 0);

    target->image->generator_is_running = 0;

    /* Give up reference claim made in ecma119_image_new().
       Eventually free target */
    ecma119_image_free(target);

#ifdef Libisofs_with_pthread_exiT
    pthread_exit(NULL);
#else
    return NULL;
#endif

}


static
int checksum_prepare_image(IsoImage *src, int flag)
{
    int ret;

    /* Set provisory value of isofs.ca with
       4 byte LBA, 4 byte count, size 16, name MD5 */
    ret = iso_root_set_isofsca((IsoNode *) src->root, 0, 0, 0, 16, "MD5", 0);
    if (ret < 0)
        return ret;
    return ISO_SUCCESS;
}


/*
  @flag bit0= recursion
*/
static
int checksum_prepare_nodes(Ecma119Image *target, IsoNode *node, int flag)
{
    IsoNode *pos;
    IsoFile *file;
    IsoImage *img;
    int ret, i, no_md5 = 0, has_xinfo = 0;
    size_t value_length;
    unsigned int idx = 0;
    char *value= NULL;
    void *xipt = NULL;
    static char *cx_names = "isofs.cx";
    static size_t cx_value_lengths[1] = {0};
    char *cx_valuept = "";

    img= target->image;

    if (node->type == LIBISO_FILE) {
        file = (IsoFile *) node;
        if (file->from_old_session && target->appendable) {
            /* Save MD5 data of files from old image which will not
               be copied and have an MD5 recorded in the old image. */
            has_xinfo = iso_node_get_xinfo(node, checksum_md5_xinfo_func,
                                           &xipt);
            if (has_xinfo <= 0) {
                ret = iso_node_lookup_attr(node, "isofs.cx", &value_length,
                                           &value, 0);
            }
            if (has_xinfo > 0) {
                /* xinfo MD5 overrides everything else unless data get copied
                   and checksummed during that copying
                 */;
            } else if (ret == 1 && img->checksum_array == NULL) {
                /* No checksum array loaded. Delete "isofs.cx" */
                if (!target->will_cancel)
                  iso_node_set_attrs(node, (size_t) 1,
                              &cx_names, cx_value_lengths, &cx_valuept, 4 | 8);
                no_md5 = 1;
            } else if (ret == 1 && value_length == 4) {
                for (i = 0; i < 4; i++)
                    idx = (idx << 8) | ((unsigned char *) value)[i];
                if (idx > 0 && idx < 0x8000000) {
                    /* xipt is an int disguised as void pointer */
                    for (i = 0; i < 4; i++)
                        ((char *) &xipt)[i] = value[i];
                    ret = iso_node_add_xinfo(node, checksum_cx_xinfo_func,
                                             xipt);
                    if (ret < 0)
                        return ret;
                } else
                    no_md5 = 1;
            } else {
                no_md5 = 1;
            }
            if (value != NULL) {
                free(value);
                value= NULL;
            }
        }
        /* Equip nodes with provisory isofs.cx numbers: 4 byte, all 0.
           Omit those from old image which will not be copied and have no MD5.
           Do not alter the nodes if this is only a will_cancel run.
         */
        if (!(target->will_cancel || no_md5)) {
            ret = iso_file_set_isofscx(file, (unsigned int) 0, 0);
            if (ret < 0)
                return ret;
        }
    } else if (node->type == LIBISO_DIR) {
        for (pos = ((IsoDir *) node)->children; pos != NULL; pos = pos->next) {
            ret = checksum_prepare_nodes(target, pos, 1);
            if (ret < 0)
                return ret;
        }
    }
    return ISO_SUCCESS;
}

static
int ecma119_image_new(IsoImage *src, IsoWriteOpts *opts, Ecma119Image **img)
{
    int ret, i, voldesc_size, nwriters, image_checksums_mad = 0, tag_pos;
    int sa_type;
    Ecma119Image *target;
    IsoImageWriter *writer;
    int el_torito_writer_index = -1, file_src_writer_index = -1;
    int system_area_options = 0;
    char *system_area = NULL;
    int write_count = 0, write_count_mem;

    /* 1. Allocate target and copy opts there */
    target = calloc(1, sizeof(Ecma119Image));
    if (target == NULL) {
        return ISO_OUT_OF_MEM;
    }
    /* This reference will be transfered to the burn_source and released by
       bs_free_data.
    */
    target->refcount = 1;

    /* create the tree for file caching */
    ret = iso_rbtree_new(iso_file_src_cmp, &(target->files));
    if (ret < 0) {
        goto target_cleanup;
    }

    target->image = src;
    iso_image_ref(src);

    target->will_cancel = opts->will_cancel;
    target->iso_level = opts->level;
    target->rockridge = opts->rockridge;
    target->joliet = opts->joliet;
    target->hfsplus = opts->hfsplus;
    target->fat = opts->fat;
    target->iso1999 = opts->iso1999;
    target->hardlinks = opts->hardlinks;
    target->aaip = opts->aaip;
    target->always_gmt = opts->always_gmt;
    target->old_empty = opts->old_empty;
    target->untranslated_name_len = opts->untranslated_name_len;
    target->allow_dir_id_ext = opts->allow_dir_id_ext;
    target->omit_version_numbers = opts->omit_version_numbers
                                 | opts->max_37_char_filenames;
    target->allow_deep_paths = opts->allow_deep_paths;
    target->allow_longer_paths = opts->allow_longer_paths;
    target->max_37_char_filenames = opts->max_37_char_filenames;
    target->no_force_dots = opts->no_force_dots;
    target->allow_lowercase = opts->allow_lowercase;
    target->allow_full_ascii = opts->allow_full_ascii;
    target->allow_7bit_ascii = opts->allow_7bit_ascii;
    target->relaxed_vol_atts = opts->relaxed_vol_atts;
    target->joliet_longer_paths = opts->joliet_longer_paths;
    target->joliet_long_names = opts->joliet_long_names;
    target->rrip_version_1_10 = opts->rrip_version_1_10;
    target->rrip_1_10_px_ino = opts->rrip_1_10_px_ino;
    target->aaip_susp_1_10 = opts->aaip_susp_1_10;
    target->dir_rec_mtime = opts->dir_rec_mtime;
    target->rr_reloc_dir = NULL;
    if (opts->rr_reloc_dir != NULL) {
        target->rr_reloc_dir = strdup(opts->rr_reloc_dir);
        if (target->rr_reloc_dir == NULL) {
            ret = ISO_OUT_OF_MEM;
            goto target_cleanup;
        }
    }
    target->rr_reloc_flags = opts->rr_reloc_flags;
    target->rr_reloc_node = NULL;
    target->sort_files = opts->sort_files;

    target->replace_uid = opts->replace_uid ? 1 : 0;
    target->replace_gid = opts->replace_gid ? 1 : 0;
    target->replace_dir_mode = opts->replace_dir_mode ? 1 : 0;
    target->replace_file_mode = opts->replace_file_mode ? 1 : 0;

    target->uid = opts->replace_uid == 2 ? opts->uid : 0;
    target->gid = opts->replace_gid == 2 ? opts->gid : 0;
    target->dir_mode = opts->replace_dir_mode == 2 ? opts->dir_mode : 0555;
    target->file_mode = opts->replace_file_mode == 2 ? opts->file_mode : 0444;

    target->now = time(NULL);
    target->ms_block = opts->ms_block;
    target->appendable = opts->appendable;

    target->replace_timestamps = opts->replace_timestamps ? 1 : 0;
    target->timestamp = opts->replace_timestamps == 2 ?
                        opts->timestamp : target->now;

    /* el-torito? */
    target->eltorito = (src->bootcat == NULL ? 0 : 1);
    target->catalog = src->bootcat;
    if (target->catalog != NULL) {
        target->num_bootsrc = target->catalog->num_bootimages;
        target->bootsrc = calloc(target->num_bootsrc + 1,
                                    sizeof(IsoFileSrc *));
        if (target->bootsrc == NULL) {
            ret = ISO_OUT_OF_MEM;
            goto target_cleanup;
        }
        for (i= 0; i < target->num_bootsrc; i++)
            target->bootsrc[i] = NULL;
    } else {
        target->num_bootsrc = 0;
        target->bootsrc = NULL;
    }

    if (opts->system_area_data != NULL) {
        system_area = opts->system_area_data;
        system_area_options = opts->system_area_options;
    } else if (src->system_area_data != NULL) {
        system_area = src->system_area_data;
        system_area_options = src->system_area_options;
    } else {
        system_area_options = opts->system_area_options & 0xfffffffc;
    }
    sa_type = (system_area_options >> 2) & 0x3f;
    if (sa_type != 0 && sa_type != 3)
        for (i = 0; i < ISO_MAX_PARTITIONS; i++)
            if (opts->appended_partitions[i] != NULL)
                return ISO_NON_MBR_SYS_AREA;
    if (sa_type != 0 && opts->prep_partition != NULL)
        return ISO_NON_MBR_SYS_AREA;

    target->system_area_data = NULL;
    if (system_area != NULL) {
        target->system_area_data = calloc(32768, 1);
        if (target->system_area_data == NULL) {
            ret = ISO_OUT_OF_MEM;
            goto target_cleanup;
        }
        memcpy(target->system_area_data, system_area, 32768);
    }
    target->system_area_options = system_area_options;

    target->vol_creation_time = opts->vol_creation_time;
    target->vol_modification_time = opts->vol_modification_time;
    target->vol_expiration_time = opts->vol_expiration_time;
    target->vol_effective_time = opts->vol_effective_time;
    strcpy(target->vol_uuid, opts->vol_uuid);

    target->partition_offset = opts->partition_offset;
    target->partition_secs_per_head = opts->partition_secs_per_head;
    target->partition_heads_per_cyl = opts->partition_heads_per_cyl;
    if (target->partition_secs_per_head == 0)
        target->partition_secs_per_head = 32;
    if (target->partition_heads_per_cyl == 0)
        target->partition_heads_per_cyl = 64;
    target->eff_partition_offset = 0;
    target->partition_root = NULL;
    target->partition_l_table_pos = 0;
    target->partition_m_table_pos = 0;
    target->j_part_root = NULL;
    target->j_part_l_path_table_pos = 0;
    target->j_part_m_path_table_pos = 0;
    target->input_charset = strdup(iso_get_local_charset(0));
    if (target->input_charset == NULL) {
        ret = ISO_OUT_OF_MEM;
        goto target_cleanup;
    }

    if (opts->output_charset != NULL) {
        target->output_charset = strdup(opts->output_charset);
    } else {
        target->output_charset = strdup(target->input_charset);
    }
    if (target->output_charset == NULL) {
        ret = ISO_OUT_OF_MEM;
        goto target_cleanup;
    }

    memcpy(target->hfsp_serial_number, opts->hfsp_serial_number, 8);

    target->md5_file_checksums = opts->md5_file_checksums;
    target->md5_session_checksum = opts->md5_session_checksum;
    strcpy(target->scdbackup_tag_parm, opts->scdbackup_tag_parm);
    target->scdbackup_tag_written = opts->scdbackup_tag_written;
    target->checksum_idx_counter = 0;
    target->checksum_ctx = NULL;
    target->checksum_counter = 0;
    target->checksum_rlsb_tag_pos = 0;
    target->checksum_sb_tag_pos = 0;
    target->checksum_tree_tag_pos = 0;
    target->checksum_tag_pos = 0;
    target->checksum_buffer = NULL;
    target->checksum_array_pos = 0;
    target->checksum_range_start = 0;
    target->checksum_range_size = 0;
    target->opts_overwrite = NULL;

#ifdef Libisofs_with_libjtE

    /* Eventually start Jigdo Template Extraction */
    target->libjte_handle = NULL;
    if (opts->libjte_handle != NULL) {
        target->libjte_handle = opts->libjte_handle;
        ret = libjte_write_header(target->libjte_handle);
        if (ret <= 0) {
            iso_libjte_forward_msgs(target->libjte_handle, 
                                target->image->id, ISO_LIBJTE_START_FAILED, 0);
            ret = ISO_LIBJTE_START_FAILED;
            goto target_cleanup;
        }
    }
    
#endif /* Libisofs_with_libjtE */

    target->tail_blocks = opts->tail_blocks;

    target->mipsel_e_entry = 0;
    target->mipsel_p_offset = 0;
    target->mipsel_p_vaddr = 0;
    target->mipsel_p_filesz = 0;

    target->sparc_core_src = NULL;

    target->empty_file_block = 0;
    target->tree_end_block = 0;

    target->wthread_is_running = 0;

    target->prep_partition = NULL;
    if (opts->prep_partition != NULL) {
        target->prep_partition = strdup(opts->prep_partition);
        if (target->prep_partition == NULL)
            return ISO_OUT_OF_MEM;
    }
    target->prep_part_size = 0;
    target->efi_boot_partition = NULL;
    if (opts->efi_boot_partition != NULL) {
        target->efi_boot_partition = strdup(opts->efi_boot_partition);
        if (target->efi_boot_partition == NULL)
            return ISO_OUT_OF_MEM;
    }
    target->efi_boot_part_size = 0;
    target->efi_boot_part_filesrc = NULL;
    for (i = 0; i < ISO_MAX_PARTITIONS; i++) {
        target->appended_partitions[i] = NULL;
        if (opts->appended_partitions[i] != NULL) {
            target->appended_partitions[i] =
                                          strdup(opts->appended_partitions[i]);
            if (target->appended_partitions[i] == NULL)
                return ISO_OUT_OF_MEM;
            target->appended_part_types[i] = opts->appended_part_types[i];
        }
        target->appended_part_prepad[i] = 0;
        target->appended_part_start[i] = target->appended_part_size[i] = 0;
    }
    strcpy(target->ascii_disc_label, opts->ascii_disc_label);
    for (i = 0; i < ISO_HFSPLUS_BLESS_MAX; i++) {
        target->hfsplus_blessed[i] = src->hfsplus_blessed[i];
        if (target->hfsplus_blessed[i] != NULL)
            iso_node_ref(target->hfsplus_blessed[i]);
    }
    target->apm_block_size = opts->apm_block_size;
    target->hfsp_block_size = opts->hfsp_block_size;
    target->hfsp_cat_node_size = 0;
    target->hfsp_iso_block_fac = 0;
    target->hfsp_collision_count = 0;
    target->apm_req_count = 0;
    target->apm_req_flags = 0;
    for (i = 0; i < ISO_APM_ENTRIES_MAX; i++)
        target->apm_req[i] = NULL;
    for (i = 0; i < ISO_MBR_ENTRIES_MAX; i++)
        target->mbr_req[i] = NULL;
    target->mbr_req_count = 0;
    for (i = 0; i < ISO_GPT_ENTRIES_MAX; i++)
        target->gpt_req[i] = NULL;
    target->gpt_req_count = 0;
    target->gpt_req_flags = 0;
    target->gpt_part_start = 0;
    target->gpt_backup_end = 0;
    target->gpt_backup_size = 0;
    target->gpt_max_entries = 0;
    target->gpt_is_computed = 0;

    target->filesrc_start = 0;
    target->filesrc_blocks = 0;

    /*
     * 2. Based on those options, create needed writers: iso, joliet...
     * Each writer inits its structures and stores needed info into
     * target.
     * If the writer needs an volume descriptor, it increments image
     * current block.
     * Finally, create Writer for files.
     */
    target->curblock = target->ms_block + 16;

    if (opts->overwrite != NULL && target->ms_block != 0 &&
        target->ms_block < target->partition_offset + 32) {
        /* Not enough room for superblock relocation */
        ret = ISO_OVWRT_MS_TOO_SMALL;
        goto target_cleanup;
    }

    /* the number of writers is dependent of the extensions */
    nwriters = 1 + 1 + 1; /* ECMA-119 + multi-session padding + files */

    if (target->eltorito) {
        nwriters++;
    }
    if (target->joliet) {
        nwriters++;
    }
    if (target->iso1999) {
        nwriters++;
    }
    nwriters++; /* Partition Prepend writer */
    if (target->hfsplus || target->fat) {
        nwriters+= 2;
    }
    nwriters++; /* GPT backup tail writer */
    nwriters++; /* Tail padding writer */
    if ((target->md5_file_checksums & 1) || target->md5_session_checksum) {
        nwriters++;
        image_checksums_mad = 1; /* from here on the loaded checksums are
                                    not consistent with isofs.cx any more.
                                  */
        ret = checksum_prepare_image(src, 0);
        if (ret < 0)
            goto target_cleanup;
        if (target->appendable) {
            ret = checksum_prepare_nodes(target, (IsoNode *) src->root, 0);
            if (ret < 0)
                goto target_cleanup;
        }
        target->checksum_idx_counter = 0;
    }

    target->writers = malloc(nwriters * sizeof(void*));
    if (target->writers == NULL) {
        ret = ISO_OUT_OF_MEM;
        goto target_cleanup;
    }

    /* create writer for ECMA-119 structure */
    ret = ecma119_writer_create(target);
    if (ret < 0) {
        goto target_cleanup;
    }

    /* create writer for El-Torito */
    if (target->eltorito) {
        ret = eltorito_writer_create(target);
        if (ret < 0) {
            goto target_cleanup;
        }
        el_torito_writer_index = target->nwriters - 1;
    }

    /* create writer for Joliet structure */
    if (target->joliet) {
        ret = joliet_writer_create(target);
        if (ret < 0) {
            goto target_cleanup;
        }
    }

    /* create writer for ISO 9660:1999 structure */
    if (target->iso1999) {
        ret = iso1999_writer_create(target);
        if (ret < 0) {
            goto target_cleanup;
        }
    }

    voldesc_size = target->curblock - target->ms_block - 16;

    /* Volume Descriptor Set Terminator */
    target->curblock++;

    /* All empty files point to the Volume Descriptor Set Terminator
     * rather than to addresses of non-empty files.
     */
    target->empty_file_block = target->curblock - 1;

    /*
     * Create the writer for possible padding to ensure that in case of image
     * growing we can safely overwrite the first 64 KiB of image.
     */
    ret = mspad_writer_create(target);
    if (ret < 0) {
        goto target_cleanup;
    }

    /* The writer for MBR and GPT partitions outside iso_file_src.
     * If PreP or FAT are desired, it creates MBR partition entries and
     * surrounding protecting partition entries.
     * If EFI boot partition is desired, it creates a GPT entry for it.
     */
     ret = partprepend_writer_create(target);
    if (ret < 0)
        goto target_cleanup;

    /* create writer for HFS+/FAT structure */
    /* Impotant: It must be created directly before iso_file_src_writer_create.
    */
    if (target->hfsplus || target->fat) {
        ret = hfsplus_writer_create(target);
        if (ret < 0) {
            goto target_cleanup;
        }
    }

    /* create writer for file contents */
    ret = iso_file_src_writer_create(target);
    if (ret < 0) {
        goto target_cleanup;
    }
    file_src_writer_index = target->nwriters - 1;

    /* create writer for HFS+ structure */
    if (target->hfsplus || target->fat) {
        ret = hfsplus_tail_writer_create(target);
        if (ret < 0) {
            goto target_cleanup;
        }
    }

/* >>> Decide whether the GPT tail writer can be the last of all
*/
#define Libisofs_gpt_writer_lasT yes

#ifndef Libisofs_gpt_writer_lasT
    /* This writer has to be added to the list after any writer which might
       request production of APM or GPT partition entries by its
       compute_data_blocks() method. Its compute_data_blocks() fills the gaps
       in APM requests. It determines the position of primary GPT and
       backup GPT. Further it reserves blocks for the backup GPT.
    */
    ret = gpt_tail_writer_create(target);
    if (ret < 0)
        goto target_cleanup;
#endif /* ! Libisofs_gpt_writer_lasT */


/* >>> Should not the checksum writer come before the zero writer ?
*/
#define Libisofs_checksums_before_paddinG yes
#ifndef Libisofs_checksums_before_paddinG

    /* >>> ??? Why is this important ? */
    /* IMPORTANT: This must be the last writer before the checksum writer */
    ret = zero_writer_create(target, target->tail_blocks, 1);
    if (ret < 0)
        goto target_cleanup;

#endif /* !Libisofs_checksums_before_paddinG */

    if ((target->md5_file_checksums & 1) || target->md5_session_checksum) {
        ret = checksum_writer_create(target);
        if (ret < 0)
            goto target_cleanup;
    }
    
#ifdef Libisofs_checksums_before_paddinG

    ret = zero_writer_create(target, target->tail_blocks, 1);
    if (ret < 0)
        goto target_cleanup;

#endif /* Libisofs_checksums_before_paddinG */

#ifdef Libisofs_gpt_writer_lasT
    /* This writer shall be the last one in the list, because it protects the
       image on media which are seen as GPT partitioned.
       In any case it has to come after any writer which might request
       production of APM or GPT partition entries by its compute_data_blocks()
       method.
       gpt_tail_writer_compute_data_blocks() fills the gaps in APM requests.
       It determines the position of primary GPT and backup GPT.
       Further it reserves blocks for the backup GPT.
    */
    ret = gpt_tail_writer_create(target);
    if (ret < 0)
        goto target_cleanup;
#endif /* Libisofs_gpt_writer_lasT */

    if (target->partition_offset > 0) {
        /* After volume descriptors and superblock tag are accounted for:
           account for second volset
        */
        if (target->ms_block + target->partition_offset + 16
            < target->curblock) {
            /* Overflow of partition system area */
            ret = ISO_PART_OFFST_TOO_SMALL;
            goto target_cleanup;
        }
        target->curblock = target->ms_block + target->partition_offset + 16;

        /* Account for partition tree volume descriptors */
        for (i = 0; i < (int) target->nwriters; ++i) {
            /* Not all writers have an entry in the partition
               volume descriptor set.
            */
            writer = target->writers[i];

            /* >>> TWINTREE: Enhance ISO1999 writer and add it here */

            if(writer->write_vol_desc != ecma119_writer_write_vol_desc &&
               writer->write_vol_desc != joliet_writer_write_vol_desc)
        continue;
            target->curblock++;
        }
        target->curblock++; /* + Terminator */

        /* >>> TWINTREE: eventually later : second superblock checksum tag */;

    }


    /* At least the FAT computation needs to know the size of filesrc data
       in advance. So this call produces target->filesrc_blocks and
       relative extent addresses, which get absolute in
       filesrc_writer_compute_data_blocks().
    */
    ret = filesrc_writer_pre_compute(target->writers[file_src_writer_index]);
    if (ret < 0)
        goto target_cleanup;

    /*
     * 3.
     * Call compute_data_blocks() in each Writer.
     * That function computes the size needed by its structures and
     * increments image current block propertly.
     */
    for (i = 0; i < (int) target->nwriters; ++i) {
        IsoImageWriter *writer = target->writers[i];

        /* Exposing address of data start to IsoWriteOpts and memorizing
           this address for all files which have no block address: 
           symbolic links, device files, empty data files.
           filesrc_writer_compute_data_blocks() and filesrc_writer_write_data()
           will account resp. write this single block. 
        */
        if (i == file_src_writer_index) {
            if (! target->old_empty)
                target->empty_file_block = target->curblock;
            opts->data_start_lba = target->curblock;
        }

        ret = writer->compute_data_blocks(writer);
        if (ret < 0) {
            goto target_cleanup;
        }
    }

    ret = iso_patch_eltoritos(target);
    if (ret < 0)
        goto target_cleanup;
    if (((target->system_area_options & 0xfc) >> 2) == 2) {
        ret = iso_read_mipsel_elf(target, 0);
        if (ret < 0)
            goto target_cleanup;
    }

    /*
     * The volume space size is just the size of the last session, in
     * case of ms images.
     */
    target->vol_space_size = target->curblock - target->ms_block;
    target->total_size = (off_t) target->vol_space_size * BLOCK_SIZE;

    /* Add sizes of eventually appended partitions */
    ret = iso_compute_append_partitions(target, 0);
    if (ret < 0)
        goto target_cleanup;

    /* create the ring buffer */
    if (opts->overwrite != NULL &&
        opts->fifo_size < 32 + target->partition_offset) {
        /* The ring buffer must be large enough to take opts->overwrite
        */
        ret = ISO_OVWRT_FIFO_TOO_SMALL;
        goto target_cleanup;
    }
    ret = iso_ring_buffer_new(opts->fifo_size, &target->buffer);
    if (ret < 0) {
        goto target_cleanup;
    }

    /* check if we need to provide a copy of volume descriptors */
    if (opts->overwrite != NULL) {

        /* opts->overwrite must be larger by partion_offset
           This storage is provided by the application.
        */


        /*
         * In the PVM to be written in the 16th sector of the disc, we
         * need to specify the full size.
         */
        target->vol_space_size = target->curblock;

        /* System area and volume descriptors */
        target->opts_overwrite = (char *) opts->overwrite;
        ret = write_head_part1(target, &write_count, 1 | 2);
        target->opts_overwrite = NULL;
        if (ret < 0)
            goto target_cleanup;

        /* copy the volume descriptors to the overwrite buffer... */
        voldesc_size *= BLOCK_SIZE;
        ret = iso_ring_buffer_read(target->buffer, opts->overwrite,
                                   write_count * BLOCK_SIZE);
        if (ret < 0) {
            iso_msg_debug(target->image->id,
                          "Error reading overwrite volume descriptors");
            goto target_cleanup;
        }

        /* Write relocated superblock checksum tag */
        tag_pos = voldesc_size / BLOCK_SIZE + 16 + 1;
        if (target->md5_session_checksum) {
            target->checksum_rlsb_tag_pos = tag_pos;
            if (target->checksum_rlsb_tag_pos < 32) {
                ret = iso_md5_start(&(target->checksum_ctx));
                if (ret < 0)
                    goto target_cleanup;
                target->opts_overwrite = (char *) opts->overwrite;
                iso_md5_compute(target->checksum_ctx, target->opts_overwrite,
                                target->checksum_rlsb_tag_pos * 2048);
                ret = iso_md5_write_tag(target, 4);
                target->opts_overwrite = NULL; /* opts might not persist */
                if (ret < 0)
                    goto target_cleanup;
            }
            tag_pos++;
            write_count++;
        } 

        /* Clean out eventual obsolete checksum tags */
        for (i = tag_pos; i < 32; i++) {
            int tag_type;
            uint32_t pos, range_start, range_size, next_tag;
            char md5[16];
 	
            ret = iso_util_decode_md5_tag((char *)(opts->overwrite + i * 2048),
                                          &tag_type, &pos, &range_start,
                                          &range_size, &next_tag, md5, 0);
            if (ret > 0)
                opts->overwrite[i * 2048] = 0;
        }

        /* Write second set of volume descriptors */
        write_count_mem= write_count;
        ret = write_head_part2(target, &write_count, 0);
        if (ret < 0)
            goto target_cleanup;

        /* Read written data into opts->overwrite */
        ret = iso_ring_buffer_read(target->buffer,
                                opts->overwrite + write_count_mem * BLOCK_SIZE,
                                (write_count - write_count_mem) * BLOCK_SIZE);
        if (ret < 0) {
            iso_msg_debug(target->image->id,
                          "Error reading overwrite volume descriptors");
            goto target_cleanup;
        }
    }

    /* This was possibly altered by above overwrite buffer production */
    target->vol_space_size = target->curblock - target->ms_block;

/*
*/
#define Libisofs_print_size_no_forK 1

#ifdef Libisofs_print_size_no_forK
    if (opts->will_cancel) {
        iso_msg_debug(target->image->id,
      "Will not start write thread because of iso_write_opts_set_will_cancel");
        *img = target;
        return ISO_SUCCESS;
    }
#endif


    /* 4. Create and start writing thread */
    if (target->md5_session_checksum) {
        /* After any fake writes are done: Initialize image checksum context */
        if (target->checksum_ctx != NULL)
            iso_md5_end(&(target->checksum_ctx), target->image_md5);
        ret = iso_md5_start(&(target->checksum_ctx));
        if (ret < 0)
            goto target_cleanup;
    }
    /* Dispose old image checksum buffer. The one of target is supposed to
       get attached at the end of write_function(). */
    iso_image_free_checksums(target->image, 0);
    image_checksums_mad = 0;

    if (target->apm_block_size == 0) {
        if (target->gpt_req_count)
            target->apm_block_size = 2048; /* Combinable with GPT */
        else
            target->apm_block_size = 512; /* Mountable on Linux */
    }

    /* ensure the thread is created joinable */
    pthread_attr_init(&(target->th_attr));
    pthread_attr_setdetachstate(&(target->th_attr), PTHREAD_CREATE_JOINABLE);

    /* To avoid race conditions with the caller, this mark must be set
       before the thread starts. So the caller can rely on a value of 0
       really meaning that the write has ended, and not that it might not have
       begun yet.
       In normal processing, the value will be changed to 0 at the end of
       write_function().
    */
    target->image->generator_is_running = 1;


    /* Claim that target may not get destroyed by bs_free_data() before
       write_function() is done with it */
    target->refcount++;

    ret = pthread_create(&(target->wthread), &(target->th_attr),
                         write_function, (void *) target);
    if (ret != 0) {
        target->refcount--;
        iso_msg_submit(target->image->id, ISO_THREAD_ERROR, 0,
                      "Cannot create writer thread");
        ret = ISO_THREAD_ERROR;
        goto target_cleanup;
    }
    target->wthread_is_running= 1;

    /*
     * Notice that once we reach this point, target belongs to the writer
     * thread and should not be modified until the writer thread finished.
     * There're however, specific fields in target that can be accessed, or
     * even modified by the read thread (look inside bs_* functions)
     */

    *img = target;
    return ISO_SUCCESS;

    target_cleanup: ;
    if(image_checksums_mad) /* No checksums is better than mad checksums */
      iso_image_free_checksums(target->image, 0);
    target->image->generator_is_running = 0;
    ecma119_image_free(target);
    return ret;
}

static int bs_read(struct burn_source *bs, unsigned char *buf, int size)
{
    int ret;
    Ecma119Image *t = (Ecma119Image*)bs->data;

    ret = iso_ring_buffer_read(t->buffer, buf, size);
    if (ret == ISO_SUCCESS) {
        return size;
    } else if (ret < 0) {
        /* error */
        iso_msg_submit(t->image->id, ISO_BUF_READ_ERROR, ret, NULL);
        return -1;
    } else {
        /* EOF */
        return 0;
    }
}

static off_t bs_get_size(struct burn_source *bs)
{
    Ecma119Image *target = (Ecma119Image*)bs->data;
    return target->total_size;
}

static void bs_free_data(struct burn_source *bs)
{
    int st;
    Ecma119Image *target = (Ecma119Image*)bs->data;

    st = iso_ring_buffer_get_status(bs, NULL, NULL);

    /* was read already finished (i.e, canceled)? */
    if (st < 4) {
        /* forces writer to stop if it is still running */
        iso_ring_buffer_reader_close(target->buffer, 0);

        /* wait until writer thread finishes */
        if (target->wthread_is_running) {
            pthread_join(target->wthread, NULL);
            target->wthread_is_running = 0;
            iso_msg_debug(target->image->id, "Writer thread joined");
        }
    }

    iso_msg_debug(target->image->id,
                  "Ring buffer was %d times full and %d times empty",
                  iso_ring_buffer_get_times_full(target->buffer),
                  iso_ring_buffer_get_times_empty(target->buffer));

    /* The reference to target was inherited from ecma119_image_new() */
    ecma119_image_free(target);
}

static
int bs_cancel(struct burn_source *bs)
{
    int st;
    size_t cap, free;
    Ecma119Image *target = (Ecma119Image*)bs->data;

    st = iso_ring_buffer_get_status(bs, &cap, &free);

    if (free == cap && (st == 2 || st == 3)) {
        /* image was already consumed */
        iso_ring_buffer_reader_close(target->buffer, 0);
    } else {
        iso_msg_debug(target->image->id, "Reader thread being cancelled");

        /* forces writer to stop if it is still running */
        iso_ring_buffer_reader_close(target->buffer, ISO_CANCELED);
    }

    /* wait until writer thread finishes */
    if (target->wthread_is_running) {
        pthread_join(target->wthread, NULL);
        target->wthread_is_running = 0;
        iso_msg_debug(target->image->id, "Writer thread joined");
    }
    return ISO_SUCCESS;
}

static
int bs_set_size(struct burn_source *bs, off_t size)
{
    Ecma119Image *target = (Ecma119Image*)bs->data;

    /*
     * just set the value to be returned by get_size. This is not used at
     * all by libisofs, it is here just for helping libburn to correctly pad
     * the image if needed.
     */
    target->total_size = size;
    return 1;
}

static
int dive_to_depth_8(IsoDir *dir, int depth)
{
    int ret;
    IsoNode *pos;

    if (depth >= 8)
        return 1;
    pos = dir->children;
    for (pos = dir->children; pos != NULL; pos = pos->next) {
        if (pos->type != LIBISO_DIR)
    continue;
        ret = dive_to_depth_8((IsoDir *) pos, depth + 1);
        if (ret != 0)
            return ret;
    }
    return 0;
}

static
int make_reloc_dir_if_needed(IsoImage *img, IsoWriteOpts *opts, int flag)
{
    int ret;
    IsoDir *dir;

    /* Two forms to express the root directory */
    if (opts->rr_reloc_dir == NULL)
        return 1;
    if (opts->rr_reloc_dir[0] == 0)
        return 1;

    if (strchr(opts->rr_reloc_dir, '/') != NULL)
        return 0;

    /* Check existence of opts->rr_reloc_dir */
    ret = iso_dir_get_node(img->root, opts->rr_reloc_dir, NULL);
    if (ret > 0)
        return 1;
    if (ret < 0)
        return ret;

    /* Check whether there is a directory of depth 8 (root is depth 1) */
    ret = dive_to_depth_8(img->root, 1);
    if (ret < 0)
        return ret;
    if (ret == 0)
        return 1;

    /* Make IsoDir with same permissions as root directory */
    ret = iso_tree_add_new_dir(img->root, opts->rr_reloc_dir, &dir);
    if (ret < 0)
        return ret;

    opts->rr_reloc_flags |= 2; /* Auto-created relocation directory */

    return 1;
}

int iso_image_create_burn_source(IsoImage *image, IsoWriteOpts *opts,
                                 struct burn_source **burn_src)
{
    int ret;
    struct burn_source *source;
    Ecma119Image *target= NULL;

    if (image == NULL || opts == NULL || burn_src == NULL) {
        return ISO_NULL_POINTER;
    }

    source = calloc(1, sizeof(struct burn_source));
    if (source == NULL) {
        return ISO_OUT_OF_MEM;
    }

    if (!opts->allow_deep_paths) { 
        ret = make_reloc_dir_if_needed(image, opts, 0);
        if (ret < 0) {
            free(source);
            return ret;
        }
    }

    ret = ecma119_image_new(image, opts, &target);
    if (ret < 0) {
        free(source);
        return ret;
    }

    source->refcount = 1;
    source->version = 1;
    source->read = NULL;
    source->get_size = bs_get_size;
    source->set_size = bs_set_size;
    source->free_data = bs_free_data;
    source->read_xt = bs_read;
    source->cancel = bs_cancel;
    source->data = target;

    *burn_src = source;
    return ISO_SUCCESS;
}

int iso_write(Ecma119Image *target, void *buf, size_t count)
{
    int ret;

    if (target->bytes_written + (off_t) count > target->total_size) {
        iso_msg_submit(target->image->id, ISO_ASSERT_FAILURE, 0,
                       "ISO overwrite");
        return ISO_ASSERT_FAILURE;
    }

    ret = iso_ring_buffer_write(target->buffer, buf, count);
    if (ret == 0) {
        /* reader cancelled */
        return ISO_CANCELED;
    }
    if (ret < 0)
        return ret;
    if (target->checksum_ctx != NULL) {
        /* Add to image checksum */
        target->checksum_counter += count;
        iso_md5_compute(target->checksum_ctx, (char *) buf, (int) count);
    }

    ret = show_chunk_to_jte(target, buf, count);
    if (ret != ISO_SUCCESS)
        return ret;

    /* total size is 0 when writing the overwrite buffer */
    if (target->total_size != (off_t) 0){
        unsigned int kbw, kbt;
        int percent;

        target->bytes_written += (off_t) count;
        kbw = (unsigned int) (target->bytes_written >> 10);
        kbt = (unsigned int) (target->total_size >> 10);
        percent = (kbw * 100) / kbt;

        /* only report in 5% chunks */
        if (percent >= target->percent_written + 5) {
            iso_msg_debug(target->image->id, "Processed %u of %u KB (%d %%)",
                          kbw, kbt, percent);
            target->percent_written = percent;
        }
    }

    return ISO_SUCCESS;
}

int iso_write_opts_new(IsoWriteOpts **opts, int profile)
{
    int i;
    IsoWriteOpts *wopts;

    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    if (profile < 0 || profile > 2) {
        return ISO_WRONG_ARG_VALUE;
    }

    wopts = calloc(1, sizeof(IsoWriteOpts));
    if (wopts == NULL) {
        return ISO_OUT_OF_MEM;
    }
    wopts->scdbackup_tag_written = NULL;

    switch (profile) {
    case 0:
        wopts->level = 1;
        break;
    case 1:
        wopts->level = 3;
        wopts->rockridge = 1;
        break;
    case 2:
        wopts->level = 2;
        wopts->rockridge = 1;
        wopts->joliet = 1;
        wopts->replace_dir_mode = 1;
        wopts->replace_file_mode = 1;
        wopts->replace_uid = 1;
        wopts->replace_gid = 1;
        wopts->replace_timestamps = 1;
        wopts->always_gmt = 1;
        break;
    default:
        /* should never happen */
        free(wopts);
        return ISO_ASSERT_FAILURE;
        break;
    }
    wopts->hfsplus = 0;
    wopts->fat = 0;
    wopts->fifo_size = 1024; /* 2 MB buffer */
    wopts->sort_files = 1; /* file sorting is always good */
    wopts->rr_reloc_dir = NULL;
    wopts->rr_reloc_flags = 0;
    wopts->system_area_data = NULL;
    wopts->system_area_options = 0;
    wopts->vol_creation_time = 0;
    wopts->vol_modification_time = 0;
    wopts->vol_expiration_time = 0;
    wopts->vol_effective_time = 0;
    wopts->vol_uuid[0] = 0;
    wopts->partition_offset = 0;
    wopts->partition_secs_per_head = 0;
    wopts->partition_heads_per_cyl = 0;

#ifdef Libisofs_with_libjtE
    wopts->libjte_handle = NULL;
#endif /* Libisofs_with_libjtE */

    wopts->tail_blocks = 0;
    wopts->prep_partition = NULL;
    wopts->efi_boot_partition = NULL;
    for (i = 0; i < ISO_MAX_PARTITIONS; i++)
        wopts->appended_partitions[i] = NULL;
    wopts->ascii_disc_label[0] = 0;
    wopts->will_cancel = 0;
    wopts->allow_dir_id_ext = 0;
    wopts->old_empty = 0;
    wopts->untranslated_name_len = 0;
    for (i = 0; i < 8; i++)
        wopts->hfsp_serial_number[i] = 0;
    wopts->apm_block_size = 0;
    wopts->hfsp_block_size = 0;

    *opts = wopts;
    return ISO_SUCCESS;
}

void iso_write_opts_free(IsoWriteOpts *opts)
{
    int i;

    if (opts == NULL) {
        return;
    }
    free(opts->output_charset);
    if (opts->rr_reloc_dir != NULL)
        free(opts->rr_reloc_dir);
    if (opts->system_area_data != NULL)
        free(opts->system_area_data);
    if (opts->prep_partition != NULL)
        free(opts->prep_partition);
    if (opts->efi_boot_partition != NULL)
        free(opts->efi_boot_partition);
    for (i = 0; i < ISO_MAX_PARTITIONS; i++)
        if (opts->appended_partitions[i] != NULL)
            free(opts->appended_partitions[i]);

    free(opts);
}

int iso_write_opts_set_will_cancel(IsoWriteOpts *opts, int will_cancel)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->will_cancel = !!will_cancel;
    return ISO_SUCCESS;
}

int iso_write_opts_set_iso_level(IsoWriteOpts *opts, int level)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    if (level != 1 && level != 2 && level != 3) {
        return ISO_WRONG_ARG_VALUE;
    }
    opts->level = level;
    return ISO_SUCCESS;
}

int iso_write_opts_set_rockridge(IsoWriteOpts *opts, int enable)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->rockridge = enable ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_joliet(IsoWriteOpts *opts, int enable)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->joliet = enable ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_hfsplus(IsoWriteOpts *opts, int enable)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->hfsplus = enable ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_fat(IsoWriteOpts *opts, int enable)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->fat = enable ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_iso1999(IsoWriteOpts *opts, int enable)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->iso1999 = enable ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_hardlinks(IsoWriteOpts *opts, int enable)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->hardlinks = enable ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_aaip(IsoWriteOpts *opts, int enable)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->aaip = enable ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_old_empty(IsoWriteOpts *opts, int enable)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->old_empty = enable ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_untranslated_name_len(IsoWriteOpts *opts, int len)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    if (len == -1)
        opts->untranslated_name_len = ISO_UNTRANSLATED_NAMES_MAX;
    else if(len == 0)
        opts->untranslated_name_len = 0;
    else if(len > ISO_UNTRANSLATED_NAMES_MAX || len < 0)
        return ISO_WRONG_ARG_VALUE;
    else
        opts->untranslated_name_len = len;
    return opts->untranslated_name_len;
}

int iso_write_opts_set_allow_dir_id_ext(IsoWriteOpts *opts, int allow)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->allow_dir_id_ext = allow ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_omit_version_numbers(IsoWriteOpts *opts, int omit)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->omit_version_numbers = omit & 3;
    return ISO_SUCCESS;
}

int iso_write_opts_set_allow_deep_paths(IsoWriteOpts *opts, int allow)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->allow_deep_paths = allow ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_allow_longer_paths(IsoWriteOpts *opts, int allow)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->allow_longer_paths = allow ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_max_37_char_filenames(IsoWriteOpts *opts, int allow)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->max_37_char_filenames = allow ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_no_force_dots(IsoWriteOpts *opts, int no)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->no_force_dots = no & 3;
    return ISO_SUCCESS;
}

int iso_write_opts_set_allow_lowercase(IsoWriteOpts *opts, int allow)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->allow_lowercase = allow ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_allow_full_ascii(IsoWriteOpts *opts, int allow)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->allow_full_ascii = allow ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_allow_7bit_ascii(IsoWriteOpts *opts, int allow)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->allow_7bit_ascii = allow ? 1 : 0;
    return ISO_SUCCESS;
}


int iso_write_opts_set_relaxed_vol_atts(IsoWriteOpts *opts, int allow)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->relaxed_vol_atts = allow ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_joliet_longer_paths(IsoWriteOpts *opts, int allow)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->joliet_longer_paths = allow ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_joliet_long_names(IsoWriteOpts *opts, int allow)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->joliet_long_names = allow ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_rrip_version_1_10(IsoWriteOpts *opts, int oldvers)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->rrip_version_1_10 = oldvers ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_rrip_1_10_px_ino(IsoWriteOpts *opts, int enable)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->rrip_1_10_px_ino = enable ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_aaip_susp_1_10(IsoWriteOpts *opts, int oldvers)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->aaip_susp_1_10 = oldvers ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_dir_rec_mtime(IsoWriteOpts *opts, int allow)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    if (allow < 0)
        allow = 1;
    else if (allow & (1 << 14))
        allow &= ~1;
    else if (allow & 6)
        allow |= 1;
    opts->dir_rec_mtime = allow & 7;
    return ISO_SUCCESS;
}

int iso_write_opts_set_rr_reloc(IsoWriteOpts *opts, char *name, int flags)
{
    if (opts->rr_reloc_dir != name) {
        if (opts->rr_reloc_dir != NULL)
            free(opts->rr_reloc_dir);
        opts->rr_reloc_dir = NULL;
        if (name != NULL) {
            opts->rr_reloc_dir = strdup(name);
            if (opts->rr_reloc_dir == NULL)
                return ISO_OUT_OF_MEM;
        }
    }
    opts->rr_reloc_flags = flags & 1;
    return ISO_SUCCESS;
}

int iso_write_opts_set_sort_files(IsoWriteOpts *opts, int sort)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->sort_files = sort ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_record_md5(IsoWriteOpts *opts, int session, int files)
{
    opts->md5_session_checksum = session & 1;
    opts->md5_file_checksums = files & 3;
    return ISO_SUCCESS;
}

int iso_write_opts_set_scdbackup_tag(IsoWriteOpts *opts,
                                     char *name, char *timestamp,
                                     char *tag_written)
{
    char eff_name[81], eff_time[19];
    int i;

    for (i = 0; name[i] != 0 && i < 80; i++)
         if (isspace((int) ((unsigned char *) name)[i]))
             eff_name[i] = '_';
         else
             eff_name[i] = name[i];
    if (i == 0)
        eff_name[i++] = '_';
    eff_name[i] = 0;
    for (i = 0; timestamp[i] != 0 && i < 18; i++)
         if (isspace((int) ((unsigned char *) timestamp)[i]))
             eff_time[i] = '_';
         else
             eff_time[i] = timestamp[i];
    if (i == 0)
        eff_time[i++] = '_';
    eff_time[i] = 0;

    sprintf(opts->scdbackup_tag_parm, "%s %s", eff_name, eff_time);

    opts->scdbackup_tag_written = tag_written;
    if (tag_written != NULL)
        tag_written[0] = 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_replace_mode(IsoWriteOpts *opts, int dir_mode,
                                    int file_mode, int uid, int gid)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    if (dir_mode < 0 || dir_mode > 2) {
        return ISO_WRONG_ARG_VALUE;
    }
    if (file_mode < 0 || file_mode > 2) {
        return ISO_WRONG_ARG_VALUE;
    }
    if (uid < 0 || uid > 2) {
        return ISO_WRONG_ARG_VALUE;
    }
    if (gid < 0 || gid > 2) {
        return ISO_WRONG_ARG_VALUE;
    }
    opts->replace_dir_mode = dir_mode;
    opts->replace_file_mode = file_mode;
    opts->replace_uid = uid;
    opts->replace_gid = gid;
    return ISO_SUCCESS;
}

int iso_write_opts_set_default_dir_mode(IsoWriteOpts *opts, mode_t dir_mode)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->dir_mode = dir_mode;
    return ISO_SUCCESS;
}

int iso_write_opts_set_default_file_mode(IsoWriteOpts *opts, mode_t file_mode)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->file_mode = file_mode;
    return ISO_SUCCESS;
}

int iso_write_opts_set_default_uid(IsoWriteOpts *opts, uid_t uid)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->uid = uid;
    return ISO_SUCCESS;
}

int iso_write_opts_set_default_gid(IsoWriteOpts *opts, gid_t gid)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->gid = gid;
    return ISO_SUCCESS;
}

int iso_write_opts_set_replace_timestamps(IsoWriteOpts *opts, int replace)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    if (replace < 0 || replace > 2) {
        return ISO_WRONG_ARG_VALUE;
    }
    opts->replace_timestamps = replace;
    return ISO_SUCCESS;
}

int iso_write_opts_set_default_timestamp(IsoWriteOpts *opts, time_t timestamp)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->timestamp = timestamp;
    return ISO_SUCCESS;
}

int iso_write_opts_set_always_gmt(IsoWriteOpts *opts, int gmt)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->always_gmt = gmt ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_output_charset(IsoWriteOpts *opts, const char *charset)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->output_charset = charset ? strdup(charset) : NULL;
    return ISO_SUCCESS;
}

int iso_write_opts_set_appendable(IsoWriteOpts *opts, int appendable)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->appendable = appendable ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_ms_block(IsoWriteOpts *opts, uint32_t ms_block)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->ms_block = ms_block;
    return ISO_SUCCESS;
}

int iso_write_opts_set_overwrite_buf(IsoWriteOpts *opts, uint8_t *overwrite)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->overwrite = overwrite;
    return ISO_SUCCESS;
}

int iso_write_opts_set_fifo_size(IsoWriteOpts *opts, size_t fifo_size)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    if (fifo_size < 32) {
        return ISO_WRONG_ARG_VALUE;
    }
    opts->fifo_size = fifo_size;
    return ISO_SUCCESS;
}

int iso_write_opts_get_data_start(IsoWriteOpts *opts, uint32_t *data_start,
                                  int flag)
{
    if (opts->data_start_lba == 0)
	return ISO_ERROR;
    *data_start = opts->data_start_lba;
    return ISO_SUCCESS;
}

/*
 * @param data     Either NULL or 32 kB of data. Do not submit less bytes !
 * @param options 
 *        Can cause manipulations of submitted data before they get written:
 *        bit0= apply a --protective-msdos-label as of grub-mkisofs.
 *              This means to patch bytes 446 to 512 of the system area so
 *              that one partition is defined which begins at the second
 *              512-byte block of the image and ends where the image ends.
 *              This works with and without system_area_data.
 *        bit1= apply isohybrid MBR patching to the system area.
 *              This works only with system area data from SYSLINUX plus an
 *              ISOLINUX boot image (see iso_image_set_boot_image()) and
 *              only if not bit0 is set.
 *        bit2-7= System area type
 *        bit8-9= Only with System area type 0 = MBR
 *                Cylinder alignment mode
 *        bit10-13= System area sub type
 * @param flag     bit0 = invalidate any attached system area data
 *                        same as data == NULL
 *                 bit1 = keep data unaltered
 *                 bit2 = keep options unaltered
 */
int iso_write_opts_set_system_area(IsoWriteOpts *opts, char data[32768],
                                   int options, int flag)
{
    if (data == NULL || (flag & 1)) { /* Disable */
        if (opts->system_area_data != NULL)
            free(opts->system_area_data);
        opts->system_area_data = NULL;
    } else if (!(flag & 2)) {
        if (opts->system_area_data == NULL) {
            opts->system_area_data = calloc(32768, 1);
            if (opts->system_area_data == NULL)
                return ISO_OUT_OF_MEM;
        }
        memcpy(opts->system_area_data, data, 32768);
    }
    if (!(flag & 4))
        opts->system_area_options = options & 0x7fff;
    return ISO_SUCCESS;
}

int iso_write_opts_set_pvd_times(IsoWriteOpts *opts, 
                        time_t vol_creation_time, time_t vol_modification_time,
                        time_t vol_expiration_time, time_t vol_effective_time,
                        char *vol_uuid)
{
    opts->vol_creation_time = vol_creation_time;
    opts->vol_modification_time = vol_modification_time;
    opts->vol_expiration_time = vol_expiration_time;
    opts->vol_effective_time = vol_effective_time;
    strncpy(opts->vol_uuid, vol_uuid, 16);
    opts->vol_uuid[16] = 0;
    return ISO_SUCCESS;
}

int iso_write_opts_set_part_offset(IsoWriteOpts *opts,
                                   uint32_t block_offset_2k,
                                   int secs_512_per_head, int heads_per_cyl)
{
    if (block_offset_2k > 0 && block_offset_2k < 16)
        return ISO_PART_OFFST_TOO_SMALL;
    opts->partition_offset = block_offset_2k;
    opts->partition_secs_per_head = secs_512_per_head;
    opts->partition_heads_per_cyl = heads_per_cyl;
    return ISO_SUCCESS;
}

int iso_write_opts_attach_jte(IsoWriteOpts *opts, void *libjte_handle)
{
#ifdef Libisofs_with_libjtE

    opts->libjte_handle = libjte_handle;
    return ISO_SUCCESS;

#else

    return ISO_LIBJTE_NOT_ENABLED;

#endif /* Libisofs_with_libjtE */
}

int iso_write_opts_detach_jte(IsoWriteOpts *opts, void **libjte_handle)
{
#ifdef Libisofs_with_libjtE

    if (*libjte_handle != NULL)
        *libjte_handle = opts->libjte_handle;
    opts->libjte_handle = NULL;
    return ISO_SUCCESS;

#else

    return ISO_LIBJTE_NOT_ENABLED;

#endif /* Libisofs_with_libjtE */
}
 
int iso_write_opts_set_tail_blocks(IsoWriteOpts *opts, uint32_t num_blocks)
{
    opts->tail_blocks = num_blocks;
    return ISO_SUCCESS;
}

int iso_write_opts_set_prep_img(IsoWriteOpts *opts, char *image_path, int flag)
{
    if (opts->prep_partition != NULL)
        free(opts->prep_partition);
    if (image_path == NULL)
        return ISO_SUCCESS;
    opts->prep_partition = strdup(image_path);
    if (opts->prep_partition == NULL)
        return ISO_OUT_OF_MEM;
    return ISO_SUCCESS;
}

int iso_write_opts_set_efi_bootp(IsoWriteOpts *opts, char *image_path, 
                                 int flag)
{
    if (opts->efi_boot_partition != NULL)
        free(opts->efi_boot_partition);
    if (image_path == NULL)
        return ISO_SUCCESS;
    opts->efi_boot_partition = strdup(image_path);
    if (opts->efi_boot_partition == NULL)
        return ISO_OUT_OF_MEM;
    return ISO_SUCCESS;
}

int iso_write_opts_set_partition_img(IsoWriteOpts *opts, int partition_number,
                            uint8_t partition_type, char *image_path, int flag)
{
    if (partition_number < 1 || partition_number > ISO_MAX_PARTITIONS)
        return ISO_BAD_PARTITION_NO;
    if (opts->appended_partitions[partition_number - 1] != NULL)
        free(opts->appended_partitions[partition_number - 1]);
    if (image_path == NULL)
        return ISO_SUCCESS;
    opts->appended_partitions[partition_number - 1] = strdup(image_path);
    if (opts->appended_partitions[partition_number - 1] == NULL)
        return ISO_OUT_OF_MEM;
    opts->appended_part_types[partition_number - 1] = partition_type;
    return ISO_SUCCESS;
}

int iso_write_opts_set_disc_label(IsoWriteOpts *opts, char *label)
{
    strncpy(opts->ascii_disc_label, label, ISO_DISC_LABEL_SIZE - 1);
    opts->ascii_disc_label[ISO_DISC_LABEL_SIZE - 1] = 0;
    return ISO_SUCCESS;
}
 
int iso_write_opts_set_hfsp_serial_number(IsoWriteOpts *opts,
                                          uint8_t serial_number[8])
{
    memcpy(opts->hfsp_serial_number, serial_number, 8);
    return ISO_SUCCESS;
}
 
int iso_write_opts_set_hfsp_block_size(IsoWriteOpts *opts, 
                                     int hfsp_block_size, int apm_block_size)
{
    if (hfsp_block_size != 0 && hfsp_block_size != 512 &&
        hfsp_block_size != 2048)
        return ISO_BOOT_HFSP_BAD_BSIZE;
    opts->hfsp_block_size = hfsp_block_size;
    if (apm_block_size != 0 && apm_block_size != 512 && apm_block_size != 2048)
        return ISO_BOOT_HFSP_BAD_BSIZE;
    opts->apm_block_size = apm_block_size;
    return ISO_SUCCESS;
}


