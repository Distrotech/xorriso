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

#include "fsource.h"
#include <stdlib.h>

/**
 * Values belong 1000 are reserved for libisofs usage
 */
unsigned int iso_fs_global_id = 1000;

void iso_file_source_ref(IsoFileSource *src)
{
    ++src->refcount;
}

void iso_file_source_unref(IsoFileSource *src)
{
    if (--src->refcount == 0) {
        src->class->free(src);
        free(src);
    }
}

void iso_filesystem_ref(IsoFilesystem *fs)
{
    ++fs->refcount;
}

void iso_filesystem_unref(IsoFilesystem *fs)
{
    if (--fs->refcount == 0) {
        fs->free(fs);
        free(fs);
    }
}

/* 
 * this are just helpers to invoque methods in class
 */

inline
char* iso_file_source_get_path(IsoFileSource *src)
{
    return src->class->get_path(src);
}

inline
char* iso_file_source_get_name(IsoFileSource *src)
{
    return src->class->get_name(src);
}

inline
int iso_file_source_lstat(IsoFileSource *src, struct stat *info)
{
    return src->class->lstat(src, info);
}

inline
int iso_file_source_access(IsoFileSource *src)
{
    return src->class->access(src);
}

inline
int iso_file_source_stat(IsoFileSource *src, struct stat *info)
{
    return src->class->stat(src, info);
}

inline
int iso_file_source_open(IsoFileSource *src)
{
    return src->class->open(src);
}

inline
int iso_file_source_close(IsoFileSource *src)
{
    return src->class->close(src);
}

inline
int iso_file_source_read(IsoFileSource *src, void *buf, size_t count)
{
    return src->class->read(src, buf, count);
}

inline
off_t iso_file_source_lseek(IsoFileSource *src, off_t offset, int flag)
{
    return src->class->lseek(src, offset, flag);
}

inline
int iso_file_source_readdir(IsoFileSource *src, IsoFileSource **child)
{
    return src->class->readdir(src, child);
}

inline
int iso_file_source_readlink(IsoFileSource *src, char *buf, size_t bufsiz)
{
    return src->class->readlink(src, buf, bufsiz);
}

inline
IsoFilesystem* iso_file_source_get_filesystem(IsoFileSource *src)
{
    return src->class->get_filesystem(src);
}


inline
int iso_file_source_get_aa_string(IsoFileSource *src,
                                  unsigned char **aa_string, int flag)
{
    if (src->class->version < 1) {
        *aa_string = NULL;
        return 1;
    }
    return src->class->get_aa_string(src, aa_string, flag);
}

