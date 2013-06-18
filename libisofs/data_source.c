/*
 * Copyright (c) 2007 Vreixo Formoso
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
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/**
 * Private data for File IsoDataSource
 */
struct file_data_src
{
    char *path;
    int fd;
};

/**
 * Increments the reference counting of the given IsoDataSource.
 */
void iso_data_source_ref(IsoDataSource *src)
{
    src->refcount++;
}

/**
 * Decrements the reference counting of the given IsoDataSource, freeing it
 * if refcount reach 0.
 */
void iso_data_source_unref(IsoDataSource *src)
{
    if (--src->refcount == 0) {
        src->free_data(src);
        free(src);
    }
}

static
int ds_open(IsoDataSource *src)
{
    int fd;
    struct file_data_src *data;

    if (src == NULL || src->data == NULL) {
        return ISO_NULL_POINTER;
    }

    data = (struct file_data_src*) src->data;
    if (data->fd != -1) {
        return ISO_FILE_ALREADY_OPENED;
    }

    fd = open(data->path, O_RDONLY);
    if (fd == -1) {
        return ISO_FILE_ERROR;
    }

    data->fd = fd;
    return ISO_SUCCESS;
}

static
int ds_close(IsoDataSource *src)
{
    int ret;
    struct file_data_src *data;

    if (src == NULL || src->data == NULL) {
        return ISO_NULL_POINTER;
    }

    data = (struct file_data_src*) src->data;
    if (data->fd == -1) {
        return ISO_FILE_NOT_OPENED;
    }

    /* close can fail if fd is not valid, but that should never happen */
    ret = close(data->fd);

    /* in any case we mark file as closed */
    data->fd = -1;
    return ret == 0 ? ISO_SUCCESS : ISO_FILE_ERROR;
}

static int ds_read_block(IsoDataSource *src, uint32_t lba, uint8_t *buffer)
{
    struct file_data_src *data;

    if (src == NULL || src->data == NULL || buffer == NULL) {
        return ISO_NULL_POINTER;
    }

    data = (struct file_data_src*) src->data;
    if (data->fd == -1) {
        return ISO_FILE_NOT_OPENED;
    }

    /* goes to requested block */
    if (lseek(data->fd, (off_t)lba * (off_t)2048, SEEK_SET) == (off_t) -1) {
        return ISO_FILE_SEEK_ERROR;
    }

    /* TODO #00008 : guard against partial reads. */
    if (read(data->fd, buffer, 2048) != 2048) {
        return ISO_FILE_READ_ERROR;
    }

    return ISO_SUCCESS;
}

static
void ds_free_data(IsoDataSource *src)
{
    struct file_data_src *data;

    data = (struct file_data_src*)src->data;

    /* close the file if needed */
    if (data->fd != -1) {
        close(data->fd);
    }
    free(data->path);
    free(data);
}

/**
 * Create a new IsoDataSource from a local file. This is suitable for
 * accessing regular .iso images, or to acces drives via its block device
 * and standard POSIX I/O calls.
 * 
 * @param path
 *     The path of the file
 * @param src
 *     Will be filled with the pointer to the newly created data source.
 * @return
 *    1 on success, < 0 on error.
 */
int iso_data_source_new_from_file(const char *path, IsoDataSource **src)
{
    int ret;
    struct file_data_src *data;
    IsoDataSource *ds;

    if (path == NULL || src == NULL) {
        return ISO_NULL_POINTER;
    }

    /* ensure we have read access to the file */
    ret = iso_eaccess(path);
    if (ret < 0) {
        return ret;
    }

    data = malloc(sizeof(struct file_data_src));
    if (data == NULL) {
        return ISO_OUT_OF_MEM;
    }

    ds = malloc(sizeof(IsoDataSource));
    if (ds == NULL) {
        free(data);
        return ISO_OUT_OF_MEM;
    }

    /* fill data fields */
    data->path = strdup(path);
    if (data->path == NULL) {
        free(data);
        free(ds);
        return ISO_OUT_OF_MEM;
    }

    data->fd = -1;
    ds->version = 0;
    ds->refcount = 1;
    ds->data = data;

    ds->open = ds_open;
    ds->close = ds_close;
    ds->read_block = ds_read_block;
    ds->free_data = ds_free_data;

    *src = ds;
    return ISO_SUCCESS;
}
