/*
 * Copyright (c) 2007 Vreixo Formoso
 *               2012 Thomas Schmitt
 *
 * This file is part of the libisofs project; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */
#ifndef LIBISO_FILESRC_H_
#define LIBISO_FILESRC_H_

#include "libisofs.h"
#include "stream.h"
#include "ecma119.h"

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif


/* Abstraction of data file content in the emerging image.
*/
struct Iso_File_Src
{
    /* This marks an IsoFileSrc which shall only expose its extent addresses
       and sizes but shall not be counted or written by filesrc_writer.
    */
    unsigned int no_write :1;

    unsigned int checksum_index :31;

    /** File Sections of the file in the image */
    /* Special sections[0].block values while they are relative
       before filesrc_writer_compute_data_blocks().
       Valid only with .no_write == 0:
       0xfffffffe  This Iso_File_Src is claimed as external partition.
                   Others will take care of the content data.
                   filesrc_writer shall neither count nor write it.
                   At write_data time it is already converted to
                   a fileadress between  Ecma119Image.ms_block  and
                   Ecma119Image.filesrc_start - 1.
       0xffffffff  This is the block to which empty files shall point.
       Normal data files have relative addresses from 0 to 0xffffffdf.
       They cannot be higher, because mspad_writer forces the absolute
       filesrc addresses to start at least at 0x20.
    */
    struct iso_file_section *sections;
    int nsections;

    int sort_weight;
    IsoStream *stream;
};

int iso_file_src_cmp(const void *n1, const void *n2);

/**
 * Create a new IsoFileSrc to get data from a specific IsoFile.
 *
 * The IsoFileSrc will be cached in a tree to prevent the same file for
 * being written several times to image. If you call again this function
 * with a node that refers to the same source file, the previously
 * created one will be returned. No new IsoFileSrc is created in that case.
 *
 * @param img
 *      The image where this file is to be written
 * @param file
 *      The IsoNode we want to write
 * @param src
 *      Will be filled with a pointer to the IsoFileSrc
 * @return
 *      1 if new object was created, 0 if object existed, < 0 on error
 */
int iso_file_src_create(Ecma119Image *img, IsoFile *file, IsoFileSrc **src);

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
int iso_file_src_add(Ecma119Image *img, IsoFileSrc *new, IsoFileSrc **src);

/**
 * Free the IsoFileSrc especific data
 */
void iso_file_src_free(void *node);

/**
 * Get the size of the file this IsoFileSrc represents
 */
off_t iso_file_src_get_size(IsoFileSrc *file);

/**
 * Create a Writer for file contents.
 *
 * It takes care of written the files in the correct order.
 */
int iso_file_src_writer_create(Ecma119Image *target);

/**
 * Determine number of filesrc blocks in the image and compute extent addresses
 * relative to start of the file source writer area.
 * filesrc_writer_compute_data_blocks() later makes them absolute.
 */
int filesrc_writer_pre_compute(IsoImageWriter *writer);

/**
 * Write the content of file into the output stream of t.
 * name must be NULL or offer at least PATH_MAX characters of storage.
 * buffer must be NULL or offer at least BLOCK_SIZE characters of storage.
 * flag is not used yet, submit 0.
 */
int iso_filesrc_write_data(Ecma119Image *t, IsoFileSrc *file,
                           char *name, char *buffer, int flag);


#endif /*LIBISO_FILESRC_H_*/
