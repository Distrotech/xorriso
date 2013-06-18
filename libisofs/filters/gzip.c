/*
 * Copyright (c) 2009 - 2011 Thomas Schmitt
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 *
 * It implements a filter facility which can pipe a IsoStream into gzip
 * compression resp. uncompression, read its output and forward it as IsoStream
 * output to an IsoFile.
 * The gzip compression is done via zlib by Jean-loup Gailly and Mark Adler
 * who state in <zlib.h>:
 * "The data format used by the zlib library is described by RFCs (Request for
 *  Comments) 1950 to 1952 in the files http://www.ietf.org/rfc/rfc1950.txt
 *  (zlib format), rfc1951.txt (deflate format) and rfc1952.txt (gzip format)."
 *
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "../libisofs.h"
#include "../filter.h"
#include "../fsource.h"
#include "../util.h"
#include "../stream.h"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#ifdef Libisofs_with_zliB
#include <zlib.h>
#else
/* If zlib is not available then this code is a dummy */
#endif


/*
 * A filter that encodes or decodes the content of gzip compressed files.
 */


/* --------------------------- GzipFilterRuntime ------------------------- */


/* Individual runtime properties exist only as long as the stream is opened.
 */
typedef struct
{

#ifdef Libisofs_with_zliB

    z_stream strm; /* The zlib processing context */

#endif

    char *in_buffer;
    char *out_buffer;
    int in_buffer_size;
    int out_buffer_size;
    char *rpt; /* out_buffer + read_bytes */

    off_t in_counter;
    off_t out_counter;

    int do_flush; /* flush mode for deflate() changes at end of input */

    int error_ret;

} GzipFilterRuntime;

#ifdef Libisofs_with_zliB

static
int gzip_running_destroy(GzipFilterRuntime **running, int flag)
{
    GzipFilterRuntime *o= *running;
    if (o == NULL)
        return 0;
    if (o->in_buffer != NULL)
        free(o->in_buffer);
    if (o->out_buffer != NULL)
        free(o->out_buffer);
    free((char *) o);
    *running = NULL;
    return 1;
}


static
int gzip_running_new(GzipFilterRuntime **running, int flag)
{
    GzipFilterRuntime *o;

    *running = o = calloc(sizeof(GzipFilterRuntime), 1);
    if (o == NULL) {
        return ISO_OUT_OF_MEM;
    }
    memset(&(o->strm), 0, sizeof(o->strm));
    o->in_buffer = NULL;
    o->out_buffer = NULL;
    o->in_buffer_size = 0;
    o->out_buffer_size = 0;
    o->rpt = NULL;
    o->in_counter = 0;
    o->out_counter = 0;
    o->do_flush = Z_NO_FLUSH;
    o->error_ret = 1;

    o->in_buffer_size= 2048;
    o->out_buffer_size= 2048;
    o->in_buffer = calloc(o->in_buffer_size, 1);
    o->out_buffer = calloc(o->out_buffer_size, 1);
    if (o->in_buffer == NULL || o->out_buffer == NULL)
        goto failed;
    o->rpt = o->out_buffer;
    return 1;
failed:
    gzip_running_destroy(running, 0);
    return -1;
}
#endif /* Libisofs_with_zliB */


/* ---------------------------- GzipFilterStreamData --------------------- */


/* Counts the number of active compression filters */
static off_t gzip_ref_count = 0;

/* Counts the number of active uncompression filters */
static off_t gunzip_ref_count = 0;


#ifdef Libisofs_with_zliB
/* Parameter for deflateInit2() , see <zlib.h> */

/* ??? get this from zisofs.c ziso_compression_level ?
 * ??? have an own global parameter API ?
 * For now level 6 seems to be a fine compromise between speed and size.
 */
static int gzip_compression_level = 6;

#endif /* Libisofs_with_zliB */


/*
 * The data payload of an individual Gzip Filter IsoStream
 */
/* IMPORTANT: Any change must be reflected by gzip_clone_stream() */
typedef struct
{
    IsoStream *orig;

    off_t size; /* -1 means that the size is unknown yet */

    GzipFilterRuntime *running; /* is non-NULL when open */

    ino_t id;

} GzipFilterStreamData;



#ifdef Libisofs_with_zliB

/* Each individual GzipFilterStreamData needs a unique id number. */
/* >>> This is very suboptimal:
       The counter can rollover.
*/
static ino_t gzip_ino_id = 0;

#endif /* Libisofs_with_zliB */


static
int gzip_stream_uncompress(IsoStream *stream, void *buf, size_t desired);


/*
 * Methods for the IsoStreamIface of a Gzip Filter object.
 */

/*
 * @param flag  bit0= original stream is not open
 */
static
int gzip_stream_close_flag(IsoStream *stream, int flag)
{

#ifdef Libisofs_with_zliB

    GzipFilterStreamData *data;

    if (stream == NULL) {
        return ISO_NULL_POINTER;
    }
    data = stream->data;

    if (data->running == NULL) {
        return 1;
    }
    if (stream->class->read == &gzip_stream_uncompress) {
        inflateEnd(&(data->running->strm));
    } else {
        deflateEnd(&(data->running->strm));
    }
    gzip_running_destroy(&(data->running), 0);

    if (flag & 1)
        return 1;
    return iso_stream_close(data->orig);

#else

    return ISO_ZLIB_NOT_ENABLED;

#endif

}


static
int gzip_stream_close(IsoStream *stream)
{
    return gzip_stream_close_flag(stream, 0);
}


/*
 * @param flag  bit0= do not run .get_size() if size is < 0
 */
static
int gzip_stream_open_flag(IsoStream *stream, int flag)
{

#ifdef Libisofs_with_zliB

    GzipFilterStreamData *data;
    GzipFilterRuntime *running = NULL;
    int ret;
    z_stream *strm;

    if (stream == NULL) {
        return ISO_NULL_POINTER;
    }
    data = (GzipFilterStreamData*) stream->data;
    if (data->running != NULL) {
        return ISO_FILE_ALREADY_OPENED;
    }
    if (data->size < 0 && !(flag & 1)) {
        /* Do the size determination run now, so that the size gets cached
           and .get_size() will not fail on an opened stream.
        */
        stream->class->get_size(stream);
    }

    ret = gzip_running_new(&running,
                           stream->class->read == &gzip_stream_uncompress);
    if (ret < 0) {
        return ret;
    }
    data->running = running;

    /* Start up zlib compression context */
    strm = &(running->strm);
    strm->zalloc = Z_NULL;
    strm->zfree = Z_NULL;
    strm->opaque = Z_NULL;
    if (stream->class->read == &gzip_stream_uncompress) {
        ret = inflateInit2(strm, 15 | 16);
    } else {
        ret = deflateInit2(strm, gzip_compression_level, Z_DEFLATED,
                           15 | 16, 8, Z_DEFAULT_STRATEGY);
    }
    if (ret != Z_OK)
        return ISO_ZLIB_COMPR_ERR;
    strm->next_out = (Bytef *) running->out_buffer;
    strm->avail_out = running->out_buffer_size;

    /* Open input stream */
    ret = iso_stream_open(data->orig);
    if (ret < 0) {
        return ret;
    }

    return 1;

#else

    return ISO_ZLIB_NOT_ENABLED;

#endif

}


static
int gzip_stream_open(IsoStream *stream)
{
    return gzip_stream_open_flag(stream, 0);
}


/*
 * @param flag bit1= uncompress rather than compress
 */
static
int gzip_stream_convert(IsoStream *stream, void *buf, size_t desired, int flag)
{

#ifdef Libisofs_with_zliB

    int ret, todo, cnv_ret, c_bytes;
    GzipFilterStreamData *data;
    GzipFilterRuntime *rng;
    size_t fill = 0;
    z_stream *strm;

    if (stream == NULL) {
        return ISO_NULL_POINTER;
    }
    data = stream->data;
    rng= data->running;
    if (rng == NULL) {
        return ISO_FILE_NOT_OPENED;
    }
    strm = &(rng->strm);
    if (rng->error_ret < 0) {
        return rng->error_ret;
    } else if (rng->error_ret == 0) {
        if (rng->out_buffer_size - strm->avail_out
            - (rng->rpt - rng->out_buffer) <= 0)
            return 0;
    }

    while (1) {

        /* Transfer eventual converted bytes from strm to buf */
        c_bytes = rng->out_buffer_size - strm->avail_out
                  - (rng->rpt - rng->out_buffer);
        if (c_bytes > 0) {
           todo = desired - fill;
           if (todo > c_bytes)
               todo = c_bytes;
           memcpy(((char *) buf) + fill, rng->rpt, todo);
           rng->rpt += todo;
           fill += todo;
           rng->out_counter += todo;
        }

        if (fill >= desired || rng->error_ret == 0)
           return fill;

        /* All buffered out data are consumed now */
        rng->rpt = rng->out_buffer;
        strm->next_out = (Bytef *) rng->out_buffer;
        strm->avail_out = rng->out_buffer_size;

        if (strm->avail_in == 0) {
            /* All pending input is consumed. Get new input. */
            ret = iso_stream_read(data->orig, rng->in_buffer,
                                  rng->in_buffer_size);
            if (ret < 0)
                return (rng->error_ret = ret);
            if (ret == 0) {
                if (flag & 2) {
                    /* Early input EOF */
                    return (rng->error_ret = ISO_ZLIB_EARLY_EOF);
                } else {
                    /* Tell zlib by the next call that it is over */
                    rng->do_flush = Z_FINISH;
                }
            }
            strm->next_in = (Bytef *) rng->in_buffer;
            strm->avail_in = ret;
            rng->in_counter += ret;
        }

        /* Submit input and fetch output until input is consumed */
        while (1) {
            if (flag & 2) {
                cnv_ret = inflate(strm, rng->do_flush);
            } else {
                cnv_ret = deflate(strm, rng->do_flush);
            }
            if (cnv_ret == Z_STREAM_ERROR || cnv_ret == Z_BUF_ERROR) {
                return (rng->error_ret = ISO_ZLIB_COMPR_ERR);
            }
            if ((int) strm->avail_out < rng->out_buffer_size)
        break; /* output is available */
            if (strm->avail_in == 0) /* all pending input consumed */
        break;
        }
        if (cnv_ret == Z_STREAM_END)
            rng->error_ret = 0;
    }
    return fill;

#else

    return ISO_ZLIB_NOT_ENABLED;

#endif

}

static
int gzip_stream_compress(IsoStream *stream, void *buf, size_t desired)
{
    return gzip_stream_convert(stream, buf, desired, 0);
}

static
int gzip_stream_uncompress(IsoStream *stream, void *buf, size_t desired)
{
    return gzip_stream_convert(stream, buf, desired, 2);
}


static
off_t gzip_stream_get_size(IsoStream *stream)
{
    int ret, ret_close;
    off_t count = 0;
    GzipFilterStreamData *data;
    char buf[64 * 1024];
    size_t bufsize = 64 * 1024;

    if (stream == NULL) {
        return ISO_NULL_POINTER;
    }
    data = stream->data;

    if (data->size >= 0) {
        return data->size;
    }

    /* Run filter command and count output bytes */
    ret = gzip_stream_open_flag(stream, 1);
    if (ret < 0) {
        return ret;
    }
    while (1) {
        ret = stream->class->read(stream, buf, bufsize);
        if (ret <= 0)
    break;
        count += ret;
    }
    ret_close = gzip_stream_close(stream);
    if (ret < 0)
        return ret;
    if (ret_close < 0)
        return ret_close;

    data->size = count;
    return count;
}


static
int gzip_stream_is_repeatable(IsoStream *stream)
{
    /* Only repeatable streams are accepted as orig */
    return 1;
}


static
void gzip_stream_get_id(IsoStream *stream, unsigned int *fs_id, 
                        dev_t *dev_id, ino_t *ino_id)
{
    GzipFilterStreamData *data;

    data = stream->data;
    *fs_id = ISO_FILTER_FS_ID;
    *dev_id = ISO_FILTER_GZIP_DEV_ID;
    *ino_id = data->id;
}


static
void gzip_stream_free(IsoStream *stream)
{
    GzipFilterStreamData *data;

    if (stream == NULL) {
        return;
    }
    data = stream->data;
    if (data->running != NULL) {
        gzip_stream_close(stream);
    }
    if (stream->class->read == &gzip_stream_uncompress) {
        if (--gunzip_ref_count < 0)
            gunzip_ref_count = 0;
    } else {
        if (--gzip_ref_count < 0)
            gzip_ref_count = 0;
    }
    iso_stream_unref(data->orig);
    free(data);
}


static
int gzip_update_size(IsoStream *stream)
{
    /* By principle size is determined only once */
    return 1;
}


static
IsoStream *gzip_get_input_stream(IsoStream *stream, int flag)
{
    GzipFilterStreamData *data;

    if (stream == NULL) {
        return NULL;
    }
    data = stream->data;
    return data->orig;
}


static
int gzip_clone_stream(IsoStream *old_stream, IsoStream **new_stream, int flag)
{

#ifdef Libisofs_with_zliB

    int ret;
    IsoStream *new_input_stream, *stream;
    GzipFilterStreamData *stream_data, *old_stream_data;

    if (flag)
        return ISO_STREAM_NO_CLONE; /* unknown option required */

    stream_data = calloc(1, sizeof(GzipFilterStreamData));
    if (stream_data == NULL)
        return ISO_OUT_OF_MEM;
    ret = iso_stream_clone_filter_common(old_stream, &stream,
                                         &new_input_stream, 0);
    if (ret < 0) {
        free((char *) stream_data);
        return ret;
    }
    old_stream_data = (GzipFilterStreamData *) old_stream->data;
    stream_data->orig = new_input_stream;
    stream_data->size = old_stream_data->size;
    stream_data->running = NULL;
    stream_data->id = ++gzip_ino_id;
    stream->data = stream_data;
    *new_stream = stream;
    return ISO_SUCCESS;

#else /* Libisofs_with_zliB */

    return ISO_STREAM_NO_CLONE;

#endif /* ! Libisofs_with_zliB */

}

static
int gzip_cmp_ino(IsoStream *s1, IsoStream *s2);


IsoStreamIface gzip_stream_compress_class = {
    4,
    "gzip",
    gzip_stream_open,
    gzip_stream_close,
    gzip_stream_get_size,
    gzip_stream_compress,
    gzip_stream_is_repeatable,
    gzip_stream_get_id,
    gzip_stream_free,
    gzip_update_size,
    gzip_get_input_stream,
    gzip_cmp_ino,
    gzip_clone_stream
};


IsoStreamIface gzip_stream_uncompress_class = {
    4,
    "pizg",
    gzip_stream_open,
    gzip_stream_close,
    gzip_stream_get_size,
    gzip_stream_uncompress,
    gzip_stream_is_repeatable,
    gzip_stream_get_id,
    gzip_stream_free,
    gzip_update_size,
    gzip_get_input_stream,
    gzip_cmp_ino,
    gzip_clone_stream
};

    
static
int gzip_cmp_ino(IsoStream *s1, IsoStream *s2)
{
    if (s1->class != s2->class || (s1->class != &gzip_stream_compress_class &&
                                   s2->class != &gzip_stream_compress_class))
        return iso_stream_cmp_ino(s1, s2, 1);
    return iso_stream_cmp_ino(iso_stream_get_input_stream(s1, 0),
                              iso_stream_get_input_stream(s2, 0), 0);
}   


/* ------------------------------------------------------------------------- */


#ifdef Libisofs_with_zliB

static
void gzip_filter_free(FilterContext *filter)
{
    /* no data are allocated */;
}

/*
 * @param flag bit1= Install a decompression filter
 */
static
int gzip_filter_get_filter(FilterContext *filter, IsoStream *original, 
                           IsoStream **filtered, int flag)
{
    IsoStream *str;
    GzipFilterStreamData *data;

    if (filter == NULL || original == NULL || filtered == NULL) {
        return ISO_NULL_POINTER;
    }

    str = calloc(sizeof(IsoStream), 1);
    if (str == NULL) {
        return ISO_OUT_OF_MEM;
    }
    data = calloc(sizeof(GzipFilterStreamData), 1);
    if (data == NULL) {
        free(str);
        return ISO_OUT_OF_MEM;
    }

    /* These data items are not owned by this filter object */
    data->id = ++gzip_ino_id;
    data->orig = original;
    data->size = -1;
    data->running = NULL;

    /* get reference to the source */
    iso_stream_ref(data->orig);

    str->refcount = 1;
    str->data = data;
    if (flag & 2) {
        str->class = &gzip_stream_uncompress_class;
        gunzip_ref_count++;
    } else {
        str->class = &gzip_stream_compress_class;
        gzip_ref_count++;
    }

    *filtered = str;

    return ISO_SUCCESS;
}

/* To be called by iso_file_add_filter().
 * The FilterContext input parameter is not furtherly needed for the 
 * emerging IsoStream.
 */
static
int gzip_filter_get_compressor(FilterContext *filter, IsoStream *original, 
                               IsoStream **filtered)
{
    return gzip_filter_get_filter(filter, original, filtered, 0);
}

static
int gzip_filter_get_uncompressor(FilterContext *filter, IsoStream *original, 
                                 IsoStream **filtered)
{
    return gzip_filter_get_filter(filter, original, filtered, 2);
}


/* Produce a parameter object suitable for iso_file_add_filter().
 * It may be disposed by free() after all those calls are made.
 *
 * This is quite a dummy as it does not carry individual data.
 * @param flag bit1= Install a decompression filter
 */
static
int gzip_create_context(FilterContext **filter, int flag)
{
    FilterContext *f;
    
    *filter = f = calloc(1, sizeof(FilterContext));
    if (f == NULL) {
        return ISO_OUT_OF_MEM;
    }
    f->refcount = 1;
    f->version = 0;
    f->data = NULL;
    f->free = gzip_filter_free;
    if (flag & 2)
        f->get_filter = gzip_filter_get_uncompressor;
    else
        f->get_filter = gzip_filter_get_compressor;
    return ISO_SUCCESS;
}

#endif /* Libisofs_with_zliB */

/*
 * @param flag bit0= if_block_reduction rather than if_reduction
 *             bit1= Install a decompression filter
 *             bit2= only inquire availability of gzip filtering
 *             bit3= do not inquire size
 */
int gzip_add_filter(IsoFile *file, int flag)
{

#ifdef Libisofs_with_zliB

    int ret;
    FilterContext *f = NULL;
    IsoStream *stream;
    off_t original_size = 0, filtered_size = 0;

    if (flag & 4)
        return 2;

    original_size = iso_file_get_size(file);

    ret = gzip_create_context(&f, flag & 2);
    if (ret < 0) {
        return ret;
    }
    ret = iso_file_add_filter(file, f, 0);
    free(f);
    if (ret < 0) {
        return ret;
    }
    if (flag & 8) /* size will be filled in by caller */
        return ISO_SUCCESS;

    /* Run a full filter process getsize so that the size is cached */
    stream = iso_file_get_stream(file);
    filtered_size = iso_stream_get_size(stream);
    if (filtered_size < 0) {
        iso_file_remove_filter(file, 0);
        return filtered_size;
    }
    if ((filtered_size >= original_size ||
        ((flag & 1) && filtered_size / 2048 >= original_size / 2048))
        && !(flag & 2)){
        ret = iso_file_remove_filter(file, 0);
        if (ret < 0) {
            return ret;
        }
        return 2;
    }
    return ISO_SUCCESS;

#else

    return ISO_ZLIB_NOT_ENABLED;

#endif /* ! Libisofs_with_zliB */

}


/* API function */
int iso_file_add_gzip_filter(IsoFile *file, int flag)
{
    return gzip_add_filter(file, flag & ~8);
}


/* API function */
int iso_gzip_get_refcounts(off_t *gzip_count, off_t *gunzip_count, int flag)
{
    *gzip_count = gzip_ref_count;
    *gunzip_count = gunzip_ref_count;
    return ISO_SUCCESS;
}

