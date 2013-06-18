/*
 * Copyright (c) 2007 Vreixo Formoso
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

#include "libisofs.h"
#include "image.h"
#include "node.h"
#include "messages.h"
#include "eltorito.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * Create a new image, empty.
 *
 * The image will be owned by you and should be unref() when no more needed.
 *
 * @param name
 *     Name of the image. This will be used as volset_id and volume_id.
 * @param image
 *     Location where the image pointer will be stored.
 * @return
 *     1 success, < 0 error
 */
int iso_image_new(const char *name, IsoImage **image)
{
    int res, i;
    IsoImage *img;

    if (image == NULL) {
        return ISO_NULL_POINTER;
    }

    img = calloc(1, sizeof(IsoImage));
    if (img == NULL) {
        return ISO_OUT_OF_MEM;
    }

    /* local filesystem will be used by default */
    res = iso_local_filesystem_new(&(img->fs));
    if (res < 0) {
        free(img);
        return ISO_OUT_OF_MEM;
    }

    /* use basic builder as default */
    res = iso_node_basic_builder_new(&(img->builder));
    if (res < 0) {
        iso_filesystem_unref(img->fs);
        free(img);
        return ISO_OUT_OF_MEM;
    }

    /* fill image fields */
    res = iso_node_new_root(&img->root);
    if (res < 0) {
        iso_node_builder_unref(img->builder);
        iso_filesystem_unref(img->fs);
        free(img);
        return res;
    }
    img->refcount = 1;
    img->id = iso_message_id++;

    if (name != NULL) {
        img->volset_id = strdup(name);
        img->volume_id = strdup(name);
    }
    img->system_area_data = NULL;
    img->system_area_options = 0;
    img->num_mips_boot_files = 0;
    for (i = 0; i < 15; i++)
         img->mips_boot_file_paths[i] = NULL;
    img->sparc_core_node = NULL;
    img->builder_ignore_acl = 1;
    img->builder_ignore_ea = 1;
    img->inode_counter = 0;
    img->used_inodes = NULL;
    img->used_inodes_start = 0;
    img->checksum_start_lba = 0;
    img->checksum_end_lba = 0;
    img->checksum_idx_count = 0;
    img->checksum_array = NULL;
    img->generator_is_running = 0;
    for (i = 0; i < ISO_HFSPLUS_BLESS_MAX; i++)
        img->hfsplus_blessed[i] = NULL;

    *image = img;
    return ISO_SUCCESS;
}

/**
 * Increments the reference counting of the given image.
 */
void iso_image_ref(IsoImage *image)
{
    ++image->refcount;
}

/**
 * Decrements the reference counting of the given image.
 * If it reaches 0, the image is free, together with its tree nodes (whether
 * their refcount reach 0 too, of course).
 */
void iso_image_unref(IsoImage *image)
{
    int nexcl, i;

    if (--image->refcount == 0) {
        /* we need to free the image */

        if (image->user_data_free != NULL) {
            /* free attached data */
            image->user_data_free(image->user_data);
        }
        for (nexcl = 0; nexcl < image->nexcludes; ++nexcl) {
            free(image->excludes[nexcl]);
        }
        free(image->excludes);
        for (i = 0; i < ISO_HFSPLUS_BLESS_MAX; i++)
            if (image->hfsplus_blessed[i] != NULL)
                iso_node_unref(image->hfsplus_blessed[i]);
        iso_node_unref((IsoNode*)image->root);
        iso_node_builder_unref(image->builder);
        iso_filesystem_unref(image->fs);
        el_torito_boot_catalog_free(image->bootcat);
        iso_image_give_up_mips_boot(image, 0);
        if (image->sparc_core_node != NULL)
            iso_node_unref((IsoNode *) image->sparc_core_node);
        free(image->volset_id);
        free(image->volume_id);
        free(image->publisher_id);
        free(image->data_preparer_id);
        free(image->system_id);
        free(image->application_id);
        free(image->copyright_file_id);
        free(image->abstract_file_id);
        free(image->biblio_file_id);
        free(image->creation_time);
        free(image->modification_time);
        free(image->expiration_time);
        free(image->effective_time);
        if (image->used_inodes != NULL)
            free(image->used_inodes);
        if (image->system_area_data != NULL)
            free(image->system_area_data);
        iso_image_free_checksums(image, 0);
        free(image);
    }
}


int iso_image_free_checksums(IsoImage *image, int flag)
{
    image->checksum_start_lba = 0;
    image->checksum_end_lba = 0;
    image->checksum_idx_count = 0;
    if (image->checksum_array != NULL)
        free(image->checksum_array);
    image->checksum_array = NULL;
    return 1;
}


/**
 * Attach user defined data to the image. Use this if your application needs
 * to store addition info together with the IsoImage. If the image already
 * has data attached, the old data will be freed.
 *
 * @param data
 *      Pointer to application defined data that will be attached to the
 *      image. You can pass NULL to remove any already attached data.
 * @param give_up
 *      Function that will be called when the image does not need the data
 *      any more. It receives the data pointer as an argumente, and eventually
 *      causes data to be free. It can be NULL if you don't need it.
 */
int iso_image_attach_data(IsoImage *image, void *data, void (*give_up)(void*))
{
    if (image == NULL) {
        return ISO_NULL_POINTER;
    }

    if (image->user_data != NULL) {
        /* free previously attached data */
        if (image->user_data_free != NULL) {
            image->user_data_free(image->user_data);
        }
        image->user_data = NULL;
        image->user_data_free = NULL;
    }

    if (data != NULL) {
        image->user_data = data;
        image->user_data_free = give_up;
    }
    return ISO_SUCCESS;
}

/**
 * The the data previously attached with iso_image_attach_data()
 */
void *iso_image_get_attached_data(IsoImage *image)
{
    return image->user_data;
}

IsoDir *iso_image_get_root(const IsoImage *image)
{
    return image->root;
}

void iso_image_set_volset_id(IsoImage *image, const char *volset_id)
{
    free(image->volset_id);
    image->volset_id = strdup(volset_id);
}

const char *iso_image_get_volset_id(const IsoImage *image)
{
    if (image->volset_id == NULL)
        return "";
    return image->volset_id;
}

void iso_image_set_volume_id(IsoImage *image, const char *volume_id)
{
    free(image->volume_id);
    image->volume_id = strdup(volume_id);
}

const char *iso_image_get_volume_id(const IsoImage *image)
{
    if (image->volume_id == NULL)
        return "";
    return image->volume_id;
}

void iso_image_set_publisher_id(IsoImage *image, const char *publisher_id)
{
    free(image->publisher_id);
    image->publisher_id = strdup(publisher_id);
}

const char *iso_image_get_publisher_id(const IsoImage *image)
{
    if (image->publisher_id == NULL)
        return "";
    return image->publisher_id;
}

void iso_image_set_data_preparer_id(IsoImage *image,
                                    const char *data_preparer_id)
{
    free(image->data_preparer_id);
    image->data_preparer_id = strdup(data_preparer_id);
}

const char *iso_image_get_data_preparer_id(const IsoImage *image)
{
    if (image->data_preparer_id == NULL)
        return "";
    return image->data_preparer_id;
}

void iso_image_set_system_id(IsoImage *image, const char *system_id)
{
    free(image->system_id);
    image->system_id = strdup(system_id);
}

const char *iso_image_get_system_id(const IsoImage *image)
{
    if (image->system_id == NULL)
        return "";
    return image->system_id;
}

void iso_image_set_application_id(IsoImage *image, const char *application_id)
{
    free(image->application_id);
    image->application_id = strdup(application_id);
}

const char *iso_image_get_application_id(const IsoImage *image)
{
    if (image->application_id == NULL)
        return "";
    return image->application_id;
}

void iso_image_set_copyright_file_id(IsoImage *image,
                                     const char *copyright_file_id)
{
    free(image->copyright_file_id);
    image->copyright_file_id = strdup(copyright_file_id);
}

const char *iso_image_get_copyright_file_id(const IsoImage *image)
{
    if (image->copyright_file_id == NULL)
        return "";
    return image->copyright_file_id;
}

void iso_image_set_abstract_file_id(IsoImage *image,
                                    const char *abstract_file_id)
{
    free(image->abstract_file_id);
    image->abstract_file_id = strdup(abstract_file_id);
}

const char *iso_image_get_abstract_file_id(const IsoImage *image)
{
    if (image->abstract_file_id == NULL)
        return "";
    return image->abstract_file_id;
}

void iso_image_set_biblio_file_id(IsoImage *image, const char *biblio_file_id)
{
    free(image->biblio_file_id);
    image->biblio_file_id = strdup(biblio_file_id);
}

const char *iso_image_get_biblio_file_id(const IsoImage *image)
{
    if (image->biblio_file_id == NULL)
        return "";
    return image->biblio_file_id;
}

int iso_image_set_pvd_times(IsoImage *image,
                            char *creation_time, char *modification_time,
                            char *expiration_time, char *effective_time)
{
    if (creation_time == NULL || modification_time == NULL ||
        expiration_time == NULL || effective_time == NULL)
        return ISO_NULL_POINTER;
    image->creation_time = strdup(creation_time);
    image->modification_time = strdup(modification_time);
    image->expiration_time = strdup(expiration_time);
    image->effective_time = strdup(effective_time);
    if (image->creation_time == NULL || image->modification_time == NULL ||
        image->expiration_time == NULL || image->effective_time == NULL)
        return ISO_OUT_OF_MEM;
    return ISO_SUCCESS;
}

int iso_image_get_pvd_times(IsoImage *image,
                            char **creation_time, char **modification_time,
                            char **expiration_time, char **effective_time)
{
    if (image->creation_time == NULL || image->modification_time == NULL ||
        image->expiration_time == NULL || image->effective_time == NULL)
        return ISO_NULL_POINTER;
    *creation_time = image->creation_time;
    *modification_time = image->modification_time;
    *expiration_time = image->expiration_time;
    *effective_time = image->effective_time;
    return ISO_SUCCESS;
}

int iso_image_get_msg_id(IsoImage *image)
{
    return image->id;
}

int iso_image_get_system_area(IsoImage *img, char system_area_data[32768],
                              int *options, int flag)
{
    *options = img->system_area_options;
    if (img->system_area_data == NULL)
        return 0;
    memcpy(system_area_data, img->system_area_data, 32768);
    return 1;
}

static
int dir_update_size(IsoImage *image, IsoDir *dir)
{
    IsoNode *pos;
    pos = dir->children;
    while (pos) {
        int ret = 1;
        if (pos->type == LIBISO_FILE) {
            ret = iso_stream_update_size(ISO_FILE(pos)->stream);
        } else if (pos->type == LIBISO_DIR) {
            /* recurse */
            ret = dir_update_size(image, ISO_DIR(pos));
        }
        if (ret < 0) {
            ret = iso_msg_submit(image->id, ret, 0, NULL);
            if (ret < 0) {
                return ret; /* cancel due error threshold */
            }
        }
        pos = pos->next;
    }
    return ISO_SUCCESS;
}

int iso_image_update_sizes(IsoImage *image)
{
    if (image == NULL) {
        return ISO_NULL_POINTER;
    }

    return dir_update_size(image, image->root);
}


void iso_image_set_ignore_aclea(IsoImage *image, int what)
{
    image->builder_ignore_acl = (what & 1);
    image->builder_ignore_ea = !!(what & 2);
}


static
int img_register_ino(IsoImage *image, IsoNode *node, int flag)
{
    int ret;
    ino_t ino;
    unsigned int fs_id;
    dev_t dev_id;

    ret = iso_node_get_id(node, &fs_id, &dev_id, &ino, 1);
    if (ret < 0)
       return ret;
    if (ret > 0 && ino >= image->used_inodes_start &&
        ino <= image->used_inodes_start + (ISO_USED_INODE_RANGE - 1)) {
                                   /* without -1 : rollover hazard on 32 bit */
        image->used_inodes[(ino - image->used_inodes_start) / 8]
                                                           |= (1 << (ino % 8));
    }
    return 1;
}


/* Collect the bitmap of used inode numbers in the range of
   _ImageFsData.used_inodes_start + ISO_USED_INODE_RANGE
   @param flag bit0= recursion is active
*/
int img_collect_inos(IsoImage *image, IsoDir *dir, int flag)
{
    int ret, register_dir = 1;
    IsoDirIter *iter = NULL;
    IsoNode *node;
    IsoDir *subdir;

    if (dir == NULL)
        dir = image->root;
    if (image->used_inodes == NULL) {
        image->used_inodes = calloc(ISO_USED_INODE_RANGE / 8, 1);
        if (image->used_inodes == NULL)
            return ISO_OUT_OF_MEM;
    } else if(!(flag & 1)) {
        memset(image->used_inodes, 0, ISO_USED_INODE_RANGE / 8);
    } else {
        register_dir = 0;
    }
    if (register_dir) {
        node = (IsoNode *) dir;
        ret = img_register_ino(image, node, 0);
        if (ret < 0)
            return ret;
    }

    ret = iso_dir_get_children(dir, &iter);
    if (ret < 0)
        return ret;
    while (iso_dir_iter_next(iter, &node) == 1 ) {
        ret = img_register_ino(image, node, 0);
        if (ret < 0)
            goto ex;
        if (iso_node_get_type(node) == LIBISO_DIR) {
            subdir = (IsoDir *) node;
            ret = img_collect_inos(image, subdir, flag | 1);
            if (ret < 0)
                goto ex;
        }
    }
    ret = 1;
ex:;
    if (iter != NULL)
        iso_dir_iter_free(iter);
    return ret;
}


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
ino_t img_give_ino_number(IsoImage *image, int flag)
{
    int ret;
    ino_t new_ino, ino_idx;
    static uint64_t limit = 0xffffffff;

    if (flag & 1) {
        image->inode_counter = 0;
        if (image->used_inodes != NULL)
            free(image->used_inodes);
        image->used_inodes = NULL;
        image->used_inodes_start = 0;
    }
    new_ino = image->inode_counter + 1;
    if (image->used_inodes == NULL) {
        if (new_ino > 0 && new_ino <= limit) {
            image->inode_counter = new_ino;
            return image->inode_counter;
        }
    }
    /* Look for free number in used territory */
    while (1) {
        if (new_ino <= 0 || new_ino > limit ||
            new_ino >= image->used_inodes_start + ISO_USED_INODE_RANGE ) {

            /* Collect a bitmap of used inode numbers ahead */

            image->used_inodes_start += ISO_USED_INODE_RANGE;
            if (image->used_inodes_start > 0xffffffff ||
                image->used_inodes_start <= 0) 
                image->used_inodes_start = 0;
            ret = img_collect_inos(image, NULL, 0);
            if (ret < 0)
                goto return_result; /* >>> need error return value */

            new_ino = image->used_inodes_start + !image->used_inodes_start;
        }
        ino_idx = (new_ino - image->used_inodes_start) / 8;
        if (!(image->used_inodes[ino_idx] & (1 << (new_ino % 8)))) {
            image->used_inodes[ino_idx] |= (1 << (new_ino % 8));
    break;
        }
        new_ino++;
    }
return_result:;
    image->inode_counter = new_ino;
    return image->inode_counter;
}


/* @param flag bit0= overwrite any ino, else only ino == 0
               bit1= install inode with non-data, non-directory files
               bit2= install inode with directories
*/
static
int img_update_ino(IsoImage *image, IsoNode *node, int flag)
{
    int ret;
    ino_t ino;
    unsigned int fs_id;
    dev_t dev_id;

    ret = iso_node_get_id(node, &fs_id, &dev_id, &ino, 1);
    if (ret < 0)
        return ret;
    if (ret == 0)
       ino = 0;
    if (((flag & 1) || ino == 0) &&
        (iso_node_get_type(node) == LIBISO_FILE || (flag & (2 | 4))) &&
        ((flag & 4) || iso_node_get_type(node) != LIBISO_DIR)) {
        ret = iso_node_set_unique_id(node, image, 0);
        if (ret < 0)
            return ret;
    }
    return 1;
}


/* @param flag bit0= overwrite any ino, else only ino == 0
               bit1= install inode with non-data, non-directory files
               bit2= install inode with directories
               bit3= with bit2: install inode on parameter dir
*/
int img_make_inos(IsoImage *image, IsoDir *dir, int flag)
{
    int ret;
    IsoDirIter *iter = NULL;
    IsoNode *node;
    IsoDir *subdir;

    if (flag & 8) {
        node = (IsoNode *) dir;
        ret = img_update_ino(image, node, flag & 7);
        if (ret < 0)
            goto ex;
    }
    ret = iso_dir_get_children(dir, &iter);
    if (ret < 0)
        return ret;
    while (iso_dir_iter_next(iter, &node) == 1) {
        ret = img_update_ino(image, node, flag & 7);
        if (ret < 0)
            goto ex;
        if (iso_node_get_type(node) == LIBISO_DIR) {
            subdir = (IsoDir *) node;
            ret = img_make_inos(image, subdir, flag & ~8);
            if (ret < 0)
                goto ex;
        }
    }
    ret = 1;
ex:;
    if (iter != NULL)
        iso_dir_iter_free(iter);
    return ret;
}


/* API */
int iso_image_get_session_md5(IsoImage *image, uint32_t *start_lba,
                              uint32_t *end_lba, char md5[16], int flag)
{
    if (image->checksum_array == NULL || image->checksum_idx_count < 1)
        return 0;
    *start_lba = image->checksum_start_lba;
    *end_lba = image->checksum_end_lba;
    memcpy(md5, image->checksum_array, 16);
    return ISO_SUCCESS;
}

int iso_image_set_checksums(IsoImage *image, char *checksum_array,
                            uint32_t start_lba, uint32_t end_lba,
                            uint32_t idx_count, int flag)
{
    iso_image_free_checksums(image, 0);
    image->checksum_array = checksum_array;
    image->checksum_start_lba = start_lba;
    image->checksum_end_lba = end_lba;
    image->checksum_idx_count = idx_count;
    return 1;
}

int iso_image_generator_is_running(IsoImage *image)
{
    return image->generator_is_running;
}


/* API */
int iso_image_add_mips_boot_file(IsoImage *image, char *path, int flag)
{
    if (image->num_mips_boot_files >= 15)
        return ISO_BOOT_TOO_MANY_MIPS;
    image->mips_boot_file_paths[image->num_mips_boot_files] = strdup(path);
    if (image->mips_boot_file_paths[image->num_mips_boot_files] == NULL)
        return ISO_OUT_OF_MEM;
    image->num_mips_boot_files++;
    return ISO_SUCCESS;
}

/* API */
int iso_image_get_mips_boot_files(IsoImage *image, char *paths[15], int flag)
{
    int i;

    for (i = 0; i < image->num_mips_boot_files; i++)
         paths[i] = image->mips_boot_file_paths[i];
    for (; i < 15; i++)
         paths[i] = NULL;
    return image->num_mips_boot_files;
}

/* API */
int iso_image_give_up_mips_boot(IsoImage *image, int flag)
{
    int i;

    for (i = 0; i < image->num_mips_boot_files; i++)
        if (image->mips_boot_file_paths[i] != NULL) {
            free(image->mips_boot_file_paths[i]);
            image->mips_boot_file_paths[i] = NULL;
        }
    image->num_mips_boot_files = 0;
    return ISO_SUCCESS;
}

static void unset_blessing(IsoImage *img, unsigned int idx)
{
    if (img->hfsplus_blessed[idx] != NULL)
        iso_node_unref(img->hfsplus_blessed[idx]);
    img->hfsplus_blessed[idx] = NULL;
}

/* API */
int iso_image_hfsplus_bless(IsoImage *img, enum IsoHfsplusBlessings blessing,
                            IsoNode *node, int flag)
{
    unsigned int i, ok = 0;

    if (flag & 2) {
        /* Delete any blessing */
        for (i = 0; i < ISO_HFSPLUS_BLESS_MAX; i++) {
            if (img->hfsplus_blessed[i] == node || node == NULL) {
                unset_blessing(img, i);
                ok = 1;
            }
        }
        return ok;
    }
    if (blessing == ISO_HFSPLUS_BLESS_MAX)
        return ISO_WRONG_ARG_VALUE;
    if (flag & 1) {
        /* Delete a particular blessing */
        if (img->hfsplus_blessed[blessing] == node || node == NULL) {
            unset_blessing(img, (unsigned int) blessing);
            return 1;
        }
        return 0;
    }

    if (node == NULL) {
        unset_blessing(img, (unsigned int) blessing);
        return 1;
    }
        
    /* No two hats on one node */
    for (i = 0; i < ISO_HFSPLUS_BLESS_MAX && node != NULL; i++)
        if (i != blessing && img->hfsplus_blessed[i] == node)
            return 0;
    /* Enforce correct file type */
    if (blessing == ISO_HFSPLUS_BLESS_INTEL_BOOTFILE) {
        if (node->type != LIBISO_FILE)
            return 0;
    } else {
        if (node->type != LIBISO_DIR)
            return 0;
    }

    unset_blessing(img, (unsigned int) blessing);
    img->hfsplus_blessed[blessing] = node;
    if (node != NULL)
        iso_node_ref(node);
    return 1;
}


/* API */
int iso_image_hfsplus_get_blessed(IsoImage *img, IsoNode ***blessed_nodes,
                                  int *bless_max, int flag)
{
    *blessed_nodes = img->hfsplus_blessed;
    *bless_max = ISO_HFSPLUS_BLESS_MAX;
    return 1;
}


/* API */
int iso_image_set_sparc_core(IsoImage *img, IsoFile *sparc_core, int flag)
{
    if (img->sparc_core_node != NULL)
        iso_node_unref((IsoNode *) img->sparc_core_node);
    img->sparc_core_node = sparc_core;
    if (sparc_core != NULL)
        iso_node_ref((IsoNode *) sparc_core);
    return 1;
}


/* API */
int iso_image_get_sparc_core(IsoImage *img, IsoFile **sparc_core, int flag)
{
    *sparc_core = img->sparc_core_node;
    return 1;
}


