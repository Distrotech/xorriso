/*
 * Copyright (c) 2008 Vreixo Formoso
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */
#ifndef LIBISO_FILTER_H_
#define LIBISO_FILTER_H_

/*
 * Definitions of filters.
 */


/* dev_id for stream identification */

/* libisofs/filters/xor_encrypt.c */
#define XOR_ENCRYPT_DEV_ID           1

/* libisofs/filters/external.c */
#define ISO_FILTER_EXTERNAL_DEV_ID   2

/* libisofs/filters/zisofs.c */
#define ISO_FILTER_ZISOFS_DEV_ID    3

/* libisofs/filters/gzip.c */
#define ISO_FILTER_GZIP_DEV_ID    4


typedef struct filter_context FilterContext;

struct filter_context {
    int version; /* reserved for future usage, set to 0 */
    int refcount; 
    
    /** filter specific shared data */
    void *data;

    /** 
     * Factory method to create a filtered stream from another stream.
     * 
     * @param original
     *      The original stream to be filtered. If the filter needs a ref to
     *      it (most cases), it should take a ref to it with iso_stream_ref().
     * @param filtered
     *      Will be filled with the filtered IsoStream (reference belongs to 
     *      caller).
     * @return
     *      1 on success, < 0 on error
     */
    int (*get_filter)(FilterContext *filter, IsoStream *original, 
                      IsoStream **filtered);

    /**
     * Free implementation specific data. Should never be called by user.
     * Use iso_filter_unref() instead.
     */
    void (*free)(FilterContext *filter);
};

/**
 * 
 * @param flag
 *      Reserved for future usage, pass always 0 for now. 
 *      TODO in a future a different value can mean filter caching, where
 *      the filter is applied once and the filtered file is stored in a temp
 *      dir. This prevent filter to be applied several times.
 */
int iso_file_add_filter(IsoFile *file, FilterContext *filter, int flag);

void iso_filter_ref(FilterContext *filter);
void iso_filter_unref(FilterContext *filter);

#endif /*LIBISO_FILTER_H_*/
