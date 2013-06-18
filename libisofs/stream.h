/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2009 - 2011 Thomas Schmitt
 *
 * This file is part of the libisofs project; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */
#ifndef LIBISO_STREAM_H_
#define LIBISO_STREAM_H_

/*
 * Definitions of streams.
 */
#include "fsource.h"

/* IMPORTANT: Any change must be reflected by fsrc_clone_stream */
typedef struct
{
    IsoFileSource *src;

    /* key for file identification inside filesystem */
    dev_t dev_id;
    ino_t ino_id;
    off_t size; /**< size of this file */
} FSrcStreamData;

/**
 * Get an identifier for the file of the source, for debug purposes
 * @param name
 *      Must provide at least PATH_MAX bytes. If no PATH_MAX is defined
 *      then assume PATH_MAX = Libisofs_default_path_maX from libisofs.h
 */
void iso_stream_get_file_name(IsoStream *stream, char *name);

/**
 * Create a stream to read from a IsoFileSource.
 * The stream will take the ref. to the IsoFileSource, so after a successfully
 * exectution of this function, you musn't unref() the source, unless you
 * take an extra ref.
 *
 * @return
 *      1 sucess, < 0 error
 *      Possible errors:
 *
 */
int iso_file_source_stream_new(IsoFileSource *src, IsoStream **stream);

/**
 * Create a new stream to read a chunk of an IsoFileSource..
 * The stream will add a ref. to the IsoFileSource.
 *
 * @return
 *      1 sucess, < 0 error
 */
int iso_cut_out_stream_new(IsoFileSource *src, off_t offset, off_t size,
                           IsoStream **stream);

/**
 * Obtain eventual zisofs ZF field entry parameters from a file source out
 * of a loaded ISO image.
 * To make hope for non-zero reply the stream has to be the original stream
 * of an IsoFile with .from_old_session==1. The call is safe with any stream
 * type, though, unless fsrc_stream_class would be used without FSrcStreamData.
 * @return  1= returned parameters are valid, 0=no ZF info found , <0 error
 */
int iso_stream_get_src_zf(IsoStream *stream, int *header_size_div4,
                          int *block_size_log2, uint32_t *uncompressed_size,
                          int flag);

/**
 * Set the inode number of a stream that is based on FSrcStreamData, i.e.
 * stems from the imported ISO image.
 * @return 1 = ok , 0 = not an ISO image stream , <0 = error
 */
int iso_stream_set_image_ino(IsoStream *stream, ino_t ino, int flag);


/**
 * Read the full required amount of data unless error or EOF occurs.
 * Fill missing bytes by 0s.
 * @param count   Required amount
 * @param got     Returns number of actually read bytes
 * @return
 *     1 no problem encountered, 0 EOF encountered, < 0 error
 */
int iso_stream_read_buffer(IsoStream *stream, char *buf, size_t count,
                           size_t *got);

/**
 * @return 1=ok, md5 is valid,
 *        0= not ok
 *       <0 fatal error, abort 
 */
int iso_stream_make_md5(IsoStream *stream, char md5[16], int flag);


/**
 * Create a clone of the input stream of old_stream and a roughly initialized
 * clone of old_stream which has the same class and refcount 1. Its data
 * pointer will be NULL and needs to be filled by an expert which knows how
 * to clone the data of old_stream.
 * @param old_stream  The existing stream which is in process of cloning
 * @param new_stream  Will return the uninitialized memory object which shall
 *                    later become the clone of old_stream.
 * @param new_input   The clone of the input stream of old stream.
 * @param flag        Submit 0 for now.
 * @return            ISO_SUCCESS or an error code <0 
 */
int iso_stream_clone_filter_common(IsoStream *old_stream, 
                                   IsoStream **new_stream, 
                                   IsoStream **new_input, int flag);

#endif /*STREAM_H_*/
