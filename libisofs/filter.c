/*
 * Copyright (c) 2008 Vreixo Formoso
 * Copyright (c) 2009 Thomas Schmitt
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
#include "filter.h"
#include "node.h"


void iso_filter_ref(FilterContext *filter)
{
    ++filter->refcount;
}

void iso_filter_unref(FilterContext *filter)
{
    if (--filter->refcount == 0) {
        filter->free(filter);
        free(filter);
    }
}

int iso_file_add_filter(IsoFile *file, FilterContext *filter, int flag)
{
    int ret;
    IsoStream *original, *filtered;
    if (file == NULL || filter == NULL) {
        return ISO_NULL_POINTER;
    }
    
    original = file->stream;

    if (!iso_stream_is_repeatable(original)) {
        /* TODO use custom error */
        return ISO_WRONG_ARG_VALUE;
    }
    
    ret = filter->get_filter(filter, original, &filtered);
    if (ret < 0) {
        return ret;
    }
    iso_stream_unref(original);
    file->stream = filtered;
    return ISO_SUCCESS;
}


int iso_file_remove_filter(IsoFile *file, int flag)
{
    IsoStream *file_stream, *input_stream;

    file_stream = file->stream;
    input_stream = iso_stream_get_input_stream(file_stream, 0);
    if (input_stream == NULL)
        return 0;
    file->stream = input_stream;
    iso_stream_ref(input_stream); /* Protect against _unref(file_stream) */
    iso_stream_unref(file_stream);
    return 1;
}

