/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

#ifndef BURN__NULL_H
#define BURN__NULL_H

struct burn_source;
int null_read(struct burn_source *source, unsigned char *buffer, int size);
struct burn_source *burn_null_source_new(void);

#endif /* LIBBURN__NULL_H */
