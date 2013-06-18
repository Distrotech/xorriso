/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

#ifndef __SOURCE
#define __SOURCE

struct burn_source *burn_source_new(void);

int burn_source_cancel(struct burn_source *src);

int burn_source_read(struct burn_source *src, unsigned char *buffer, int size);

#endif /*__SOURCE*/
