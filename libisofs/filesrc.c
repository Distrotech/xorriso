/*
 * Copyright (c) 2007 Vreixo Formoso
 *               2010 - 2012 Thomas Schmitt
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
#include "filesrc.h"
#include "node.h"
#include "util.h"
#include "writer.h"
#include "messages.h"
#include "image.h"
#include "stream.h"
#include "md5.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* <<< */
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


#ifndef PATH_MAX
#define PATH_MAX Libisofs_default_path_maX
#endif


int iso_file_src_cmp(const void *n1, const void *n2)
{
    int ret;
    const IsoFileSrc *f1, *f2;

    if (n1 == n2) {
        return 0; /* Normally just a shortcut.
                     But important if Libisofs_file_src_cmp_non_zerO */
    }

    f1 = (const IsoFileSrc *)n1;
    f2 = (const IsoFileSrc *)n2;

    ret = iso_stream_cmp_ino(f1->stream, f2->stream, 0);
    return ret;
}

int iso_file_src_create(Ecma119Image *img, IsoFile *file, IsoFileSrc **src)
{
    int ret, i;
    IsoFileSrc *fsrc;
    unsigned int fs_id;
    dev_t dev_id;
    ino_t ino_id;
    int cret, no_md5= 0;
    void *xipt = NULL;

    if (img == NULL || file == NULL || src == NULL) {
        return ISO_NULL_POINTER;
    }

    iso_stream_get_id(file->stream, &fs_id, &dev_id, &ino_id);

    fsrc = calloc(1, sizeof(IsoFileSrc));
    if (fsrc == NULL) {
        return ISO_OUT_OF_MEM;
    }

    /* fill key and other atts */
    fsrc->no_write = (file->from_old_session && img->appendable);
    if (file->from_old_session && img->appendable) {
        /*
         * On multisession discs we keep file sections from old image.
         */
        int ret = iso_file_get_old_image_sections(file, &(fsrc->nsections),
                                                  &(fsrc->sections), 0);
        if (ret < 0) {
            free(fsrc);
            return ISO_OUT_OF_MEM;
        }
    } else {

        /*
         * For new files, or for image copy, we compute our own file sections.
         * Block and size of each section will be filled later.
         */
        off_t section_size = iso_stream_get_size(file->stream);
        if (section_size > (off_t) MAX_ISO_FILE_SECTION_SIZE) {
            fsrc->nsections = DIV_UP(section_size - (off_t) MAX_ISO_FILE_SECTION_SIZE,
                                     (off_t)ISO_EXTENT_SIZE) + 1;
        } else {
            fsrc->nsections = 1;
        }
        fsrc->sections = calloc(fsrc->nsections,
                                sizeof(struct iso_file_section));
        if (fsrc->sections == NULL) {
            free(fsrc);
            return ISO_OUT_OF_MEM;
        }
        for (i = 0; i < fsrc->nsections; i++)
            fsrc->sections[i].block = 0;
    }
    fsrc->sort_weight = file->sort_weight;
    fsrc->stream = file->stream;

    /* insert the filesrc in the tree */
    ret = iso_rbtree_insert(img->files, fsrc, (void**)src);
    if (ret <= 0) {
        if (ret == 0 && (*src)->checksum_index > 0 && !img->will_cancel) {
            /* Duplicate file source was mapped to previously registered source
            */
            cret = iso_file_set_isofscx(file, (*src)->checksum_index, 0);
            if (cret < 0)
                ret = cret;
        }
        free(fsrc->sections);
        free(fsrc);
        return ret;
    }
    iso_stream_ref(fsrc->stream);

    if ((img->md5_file_checksums & 1) &&
        file->from_old_session && img->appendable) {
        ret = iso_node_get_xinfo((IsoNode *) file, checksum_md5_xinfo_func,
                                  &xipt);
        if (ret <= 0)
            ret = iso_node_get_xinfo((IsoNode *) file, checksum_cx_xinfo_func,
                                      &xipt);
        if (ret <= 0)
            /* Omit MD5 indexing with old image nodes which have no MD5 */
            no_md5 = 1;
    }

    if ((img->md5_file_checksums & 1) && !(no_md5 || img->will_cancel)) {
        img->checksum_idx_counter++;
        if (img->checksum_idx_counter < 0x7fffffff) {
            fsrc->checksum_index = img->checksum_idx_counter;
        } else {
            fsrc->checksum_index= 0;
            img->checksum_idx_counter= 0x7fffffff; /* keep from rolling over */
        }
        cret = iso_file_set_isofscx(file, (*src)->checksum_index, 0);
        if (cret < 0)
            return cret;
    }

    return ISO_SUCCESS;
}

/**
 * Add a given IsoFileSrc to the given image target.
 *
 * The IsoFileSrc will be cached in a tree to prevent the same file for
 * being written several times to image. If you call again this function
 * with a node that refers to the same source file, the previously
 * created one will be returned.
 *
 * @param img
 *      The image where this file is to be written
 * @param new
 *      The IsoFileSrc to add
 * @param src
 *      Will be filled with a pointer to the IsoFileSrc really present in
 *      the tree. It could be different than new if the same file already
 *      exists in the tree.
 * @return
 *      1 on success, 0 if file already exists on tree, < 0 error
 */
int iso_file_src_add(Ecma119Image *img, IsoFileSrc *new, IsoFileSrc **src)
{
    int ret;

    if (img == NULL || new == NULL || src == NULL) {
        return ISO_NULL_POINTER;
    }

    /* insert the filesrc in the tree */
    ret = iso_rbtree_insert(img->files, new, (void**)src);
    return ret;
}

void iso_file_src_free(void *node)
{
    iso_stream_unref(((IsoFileSrc*)node)->stream);
    free(((IsoFileSrc*)node)->sections);
    free(node);
}

off_t iso_file_src_get_size(IsoFileSrc *file)
{
    return iso_stream_get_size(file->stream);
}

static int cmp_by_weight(const void *f1, const void *f2)
{
    IsoFileSrc *f = *((IsoFileSrc**)f1);
    IsoFileSrc *g = *((IsoFileSrc**)f2);
    /* higher weighted first */
    return g->sort_weight - f->sort_weight;
}

static
int shall_be_written(void *arg)
{
    IsoFileSrc *f = (IsoFileSrc *)arg;
    return f->no_write ? 0 : 1;
}

int filesrc_writer_pre_compute(IsoImageWriter *writer)
{
    size_t i, size, is_external;
    Ecma119Image *t;
    IsoFileSrc **filelist;
    int (*inc_item)(void *);

    if (writer == NULL) {
        return ISO_ASSERT_FAILURE;
    }

    t = writer->target;
    t->filesrc_blocks = 0;

    /* Normally reserve a single zeroed block for all files which have
       no block address: symbolic links, device files, empty data files.
    */
    if (! t->old_empty)
        t->filesrc_blocks++;

    /* on appendable images, ms files shouldn't be included */
    if (t->appendable) {
        inc_item = shall_be_written;
    } else {
        inc_item = NULL;
    }

    /* store the filesrcs in a array */
    filelist = (IsoFileSrc**)iso_rbtree_to_array(t->files, inc_item, &size);
    if (filelist == NULL) {
        return ISO_OUT_OF_MEM;
    }

    /* sort files by weight, if needed */
    if (t->sort_files) {
        qsort(filelist, size, sizeof(void*), cmp_by_weight);
    }

    /* fill block value */
    for (i = 0; i < size; ++i) {
        int extent = 0;
        IsoFileSrc *file = filelist[i];
        off_t section_size;

        /* 0xfffffffe in emerging image means that this is an external
           partition. Only assess extent sizes but do not count as part
           of filesrc_writer output.
        */
        is_external = (file->no_write == 0 &&
                       file->sections[0].block == 0xfffffffe);

        section_size = iso_stream_get_size(file->stream);
        for (extent = 0; extent < file->nsections - 1; ++extent) {
            file->sections[extent].block = t->filesrc_blocks + extent *
                        (ISO_EXTENT_SIZE / BLOCK_SIZE);
            file->sections[extent].size = ISO_EXTENT_SIZE;
            section_size -= (off_t) ISO_EXTENT_SIZE;
        }

        /*
         * final section
         */
        if (section_size <= 0) {
            /* Will become t->empty_file_block
               in filesrc_writer_compute_data_blocks()
               Special use of 0xffffffe0 to 0xffffffff is covered by
               mspad_writer which enforces a minimum start of filesrc at
               block 0x00000020.
            */
            file->sections[extent].block = 0xffffffff;
        } else {
            file->sections[extent].block =
                   t->filesrc_blocks + extent * (ISO_EXTENT_SIZE / BLOCK_SIZE);
        }
        file->sections[extent].size = (uint32_t)section_size;

        /* 0xfffffffe in emerging image means that this is an external
           partition. Others will take care of the content data.
        */
        if (is_external) {
            file->sections[0].block = 0xfffffffe;
            file->no_write = 1; /* Ban for filesrc_writer */
    continue;
        }

        t->filesrc_blocks += DIV_UP(iso_file_src_get_size(file), BLOCK_SIZE);
    }

    /* the list is only needed by this writer, store locally */
    writer->data = filelist;
    return ISO_SUCCESS;
}

static
int filesrc_writer_compute_data_blocks(IsoImageWriter *writer)
{
    Ecma119Image *t;
    int extent = 0;
    size_t  i;
    IsoFileSrc *file;
    IsoFileSrc **filelist;

    if (writer == NULL) {
        return ISO_ASSERT_FAILURE;
    }
    t = writer->target;
    filelist = (IsoFileSrc **) writer->data;

    /* >>> HFS: need to align to allocation block size */;
    /* >>> HFS: ??? how to handle multi-extent files ? */;

    t->filesrc_start = t->curblock;

    /* Give all extent addresses their final absolute value */
    i = 0;
    while ((file = filelist[i++]) != NULL) {

       /* Skip external partitions */ 
       if (file->no_write)
    continue;

       for (extent = 0; extent < file->nsections; ++extent) {
            if (file->sections[extent].block == 0xffffffff)
                file->sections[extent].block = t->empty_file_block;
            else
                file->sections[extent].block += t->curblock;
       }
    }

    t->curblock += t->filesrc_blocks;
    return ISO_SUCCESS;
}

static
int filesrc_writer_write_vol_desc(IsoImageWriter *writer)
{
    /* nothing needed */
    return ISO_SUCCESS;
}

/* open a file, i.e., its Stream */
static inline
int filesrc_open(IsoFileSrc *file)
{
    return iso_stream_open(file->stream);
}

static inline
int filesrc_close(IsoFileSrc *file)
{
    return iso_stream_close(file->stream);
}

/**
 * @return
 *     1 ok, 0 EOF, < 0 error
 */
static
int filesrc_read(IsoFileSrc *file, char *buf, size_t count)
{
    size_t got;

    return iso_stream_read_buffer(file->stream, buf, count, &got);
}

/* @return 1=ok, md5 is valid,
           0= not ok, go on,
          <0 fatal error, abort 
*/  
static
int filesrc_make_md5(Ecma119Image *t, IsoFileSrc *file, char md5[16], int flag)
{
    return iso_stream_make_md5(file->stream, md5, 0);
}

/* name must be NULL or offer at least PATH_MAX characters.
   buffer must be NULL or offer at least BLOCK_SIZE characters.
*/
int iso_filesrc_write_data(Ecma119Image *t, IsoFileSrc *file,
                           char *name, char *buffer, int flag)
{
    int res, ret, was_error;
    char *name_data = NULL;
    char *buffer_data = NULL;
    size_t b;
    off_t file_size;
    uint32_t nblocks;
    void *ctx= NULL;
    char md5[16], pre_md5[16];
    int pre_md5_valid = 0;
    IsoStream *stream, *inp;

#ifdef Libisofs_with_libjtE
    int jte_begun = 0;
#endif

    if (name == NULL) {
        LIBISO_ALLOC_MEM(name_data, char, PATH_MAX);
        name = name_data;
    }
    if (buffer == NULL) {
        LIBISO_ALLOC_MEM(buffer_data, char, BLOCK_SIZE);
        buffer = buffer_data;
    }

    was_error = 0;
    file_size = iso_file_src_get_size(file);
    nblocks = DIV_UP(file_size, BLOCK_SIZE);
    pre_md5_valid = 0; 
    if (file->checksum_index > 0 && (t->md5_file_checksums & 2)) {
        /* Obtain an MD5 of content by a first read pass */
        pre_md5_valid = filesrc_make_md5(t, file, pre_md5, 0);
    }
    res = filesrc_open(file);

    /* Get file name from end of filter chain */
    for (stream = file->stream; ; stream = inp) {
        inp = iso_stream_get_input_stream(stream, 0);
        if (inp == NULL)
    break;
    }
    iso_stream_get_file_name(stream, name);
    if (res < 0) {
        /*
         * UPS, very ugly error, the best we can do is just to write
         * 0's to image
         */
        iso_report_errfile(name, ISO_FILE_CANT_WRITE, 0, 0);
        was_error = 1;
        res = iso_msg_submit(t->image->id, ISO_FILE_CANT_WRITE, res,
                  "File \"%s\" can't be opened. Filling with 0s.", name);
        if (res < 0) {
            ret = res; /* aborted due to error severity */
            goto ex;
        }
        memset(buffer, 0, BLOCK_SIZE);
        for (b = 0; b < nblocks; ++b) {
            res = iso_write(t, buffer, BLOCK_SIZE);
            if (res < 0) {
                /* ko, writer error, we need to go out! */
                ret = res;
                goto ex;
            }
        }
        ret = ISO_SUCCESS;
        goto ex;
    } else if (res > 1) {
        iso_report_errfile(name, ISO_FILE_CANT_WRITE, 0, 0);
        was_error = 1;
        res = iso_msg_submit(t->image->id, ISO_FILE_CANT_WRITE, 0,
                  "Size of file \"%s\" has changed. It will be %s", name,
                  (res == 2 ? "truncated" : "padded with 0's"));
        if (res < 0) {
            filesrc_close(file);
            ret = res; /* aborted due to error severity */
            goto ex;
        }
    }
#ifdef LIBISOFS_VERBOSE_DEBUG
    else {
        iso_msg_debug(t->image->id, "Writing file %s", name);
    }
#endif

    /* >>> HFS: need to align to allocation block size */;

#ifdef Libisofs_with_libjtE
    if (t->libjte_handle != NULL) {
        res = libjte_begin_data_file(t->libjte_handle, name,
                                     BLOCK_SIZE, file_size);
        if (res <= 0) {
            res = iso_libjte_forward_msgs(t->libjte_handle, t->image->id,
                                    ISO_LIBJTE_FILE_FAILED, 0);
            if (res < 0) {
                filesrc_close(file);
                ret = ISO_LIBJTE_FILE_FAILED;
                goto ex;
            }
        }
        jte_begun = 1;
    }
#endif /* Libisofs_with_libjtE */

    if (file->checksum_index > 0) {
        /* initialize file checksum */
        res = iso_md5_start(&ctx);
        if (res <= 0)
            file->checksum_index = 0;
    }
    /* write file contents to image */
    for (b = 0; b < nblocks; ++b) {
        int wres;
        res = filesrc_read(file, buffer, BLOCK_SIZE);
        if (res < 0) {
            /* read error */
            break;
        }
        wres = iso_write(t, buffer, BLOCK_SIZE);
        if (wres < 0) {
            /* ko, writer error, we need to go out! */
            filesrc_close(file);
            ret = wres;
            goto ex;
        }
        if (file->checksum_index > 0) {
            /* Add to file checksum */
            if (file_size - b * BLOCK_SIZE > BLOCK_SIZE)
                res = BLOCK_SIZE;
            else
                res = file_size - b * BLOCK_SIZE;
            res = iso_md5_compute(ctx, buffer, res);
            if (res <= 0)
                file->checksum_index = 0;
        }
    }

    filesrc_close(file);

    if (b < nblocks) {
        /* premature end of file, due to error or eof */
        iso_report_errfile(name, ISO_FILE_CANT_WRITE, 0, 0);
        was_error = 1;
        if (res < 0) {
            /* error */
            res = iso_msg_submit(t->image->id, ISO_FILE_CANT_WRITE, res,
                           "Read error in file %s.", name);
        } else {
            /* eof */
            res = iso_msg_submit(t->image->id, ISO_FILE_CANT_WRITE, 0,
                          "Premature end of file %s.", name);
        }

        if (res < 0) {
            ret = res; /* aborted due error severity */
            goto ex;
        }

        /* fill with 0s */
        iso_msg_submit(t->image->id, ISO_FILE_CANT_WRITE, 0,
                       "Filling with 0");
        memset(buffer, 0, BLOCK_SIZE);
        while (b++ < nblocks) {
            res = iso_write(t, buffer, BLOCK_SIZE);
            if (res < 0) {
                /* ko, writer error, we need to go out! */
                ret = res;
                goto ex;
            }
            if (file->checksum_index > 0) {
                /* Add to file checksum */
                if (file_size - b * BLOCK_SIZE > BLOCK_SIZE)
                    res = BLOCK_SIZE;
                else
                    res = file_size - b * BLOCK_SIZE;
                res = iso_md5_compute(ctx, buffer, res);
                if (res <= 0)
                    file->checksum_index = 0;
            }
        }
    }
    if (file->checksum_index > 0 &&
        file->checksum_index <= t->checksum_idx_counter) {
        /* Obtain checksum and dispose checksum context */
        res = iso_md5_end(&ctx, md5);
        if (res <= 0)
            file->checksum_index = 0;
        if ((t->md5_file_checksums & 2) && pre_md5_valid > 0 &&
            !was_error) {
            if (! iso_md5_match(md5, pre_md5)) {
                /* Issue MISHAP event */
                iso_report_errfile(name, ISO_MD5_STREAM_CHANGE, 0, 0);
                was_error = 1;
                res = iso_msg_submit(t->image->id, ISO_MD5_STREAM_CHANGE,0,
       "Content of file '%s' changed while it was written into the image.",
                                     name);
                if (res < 0) {
                    ret = res; /* aborted due to error severity */
                    goto ex;
                }
            }
        }
        /* Write md5 into checksum buffer at file->checksum_index */
        memcpy(t->checksum_buffer + 16 * file->checksum_index, md5, 16);
    }

    ret = ISO_SUCCESS;
ex:;
    if (ctx != NULL) /* avoid any memory leak */
        iso_md5_end(&ctx, md5);

#ifdef Libisofs_with_libjtE
    if (jte_begun) {
        res = libjte_end_data_file(t->libjte_handle);
        iso_libjte_forward_msgs(t->libjte_handle, t->image->id,
                                ISO_LIBJTE_END_FAILED, 0);
        if (res <= 0 && ret >= 0)
            ret = ISO_LIBJTE_FILE_FAILED;
    }
#endif /* Libisofs_with_libjtE */

    LIBISO_FREE_MEM(buffer_data);
    LIBISO_FREE_MEM(name_data);
    return ret;
}

static
int filesrc_writer_write_data(IsoImageWriter *writer)
{
    int ret;
    size_t i;
    Ecma119Image *t = NULL;
    IsoFileSrc *file;
    IsoFileSrc **filelist;
    char *name = NULL;
    char *buffer = NULL;

    if (writer == NULL) {
        ret = ISO_ASSERT_FAILURE; goto ex;
    }

    LIBISO_ALLOC_MEM(name, char, PATH_MAX);
    LIBISO_ALLOC_MEM(buffer, char, BLOCK_SIZE);
    t = writer->target;
    filelist = writer->data;

    iso_msg_debug(t->image->id, "Writing Files...");

    /* Normally write a single zeroed block as block address target for all
       files which have no block address:
       symbolic links, device files, empty data files.
    */
    if (! t->old_empty) {
        ret = iso_write(t, buffer, BLOCK_SIZE);
        if (ret < 0)
            goto ex;
    }

    i = 0;
    while ((file = filelist[i++]) != NULL) {
        if (file->no_write) {
            /* Do not write external partitions */
            iso_msg_debug(t->image->id,
                          "filesrc_writer: Skipping no_write-src [%.f , %.f]",
                          (double) file->sections[0].block, 
                          (double) (file->sections[0].block - 1 +
                                (file->sections[0].size + 2047) / BLOCK_SIZE));
    continue;
        }
        ret = iso_filesrc_write_data(t, file, name, buffer, 0);
        if (ret < 0)
            goto ex;
    }

    ret = ISO_SUCCESS;
ex:;
    LIBISO_FREE_MEM(buffer);
    LIBISO_FREE_MEM(name);
    return ret;
}


static
int filesrc_writer_free_data(IsoImageWriter *writer)
{
    /* free the list of files (contents are free together with the tree) */
    free(writer->data);
    return ISO_SUCCESS;
}

int iso_file_src_writer_create(Ecma119Image *target)
{
    IsoImageWriter *writer;

    writer = calloc(1, sizeof(IsoImageWriter));
    if (writer == NULL) {
        return ISO_OUT_OF_MEM;
    }

    writer->compute_data_blocks = filesrc_writer_compute_data_blocks;
    writer->write_vol_desc = filesrc_writer_write_vol_desc;
    writer->write_data = filesrc_writer_write_data;
    writer->free_data = filesrc_writer_free_data;
    writer->data = NULL;
    writer->target = target;

    /* add this writer to image */
    target->writers[target->nwriters++] = writer;

    return ISO_SUCCESS;
}
