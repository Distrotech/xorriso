/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Provided under GPL version 2 or later.
*/


#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "null.h"
#include "libburn.h"
#include <stdlib.h>

#include <string.h>
int null_read(struct burn_source *source, unsigned char *buffer, int size)
{
	memset(buffer, 0, size);
	return size;
}

struct burn_source *burn_null_source_new(void)
{
	struct burn_source *src;

	src = calloc(1, sizeof(struct burn_source));
	src->refcount = 1;
	src->read = null_read;
	src->read_sub = NULL;

	src->get_size = 0;

	/* ts A70126 */
	src->set_size = NULL;

	src->free_data = NULL;
	src->data = NULL;
	return src;
}
