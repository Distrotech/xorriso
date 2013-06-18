/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2010 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/

#ifndef __SECTOR
#define __SECTOR

#include "libburn.h"
#include "transport.h"

struct burn_drive;
struct isrc;

int dec_to_bcd(int);

int sector_toc(struct burn_write_opts *, int mode);
int sector_pregap(struct burn_write_opts *, unsigned char tno,
		   unsigned char control, int mode);
int sector_postgap(struct burn_write_opts *, unsigned char tno,
		    unsigned char control, int mode);
int sector_lout(struct burn_write_opts *, unsigned char control, int mode);
int sector_data(struct burn_write_opts *, struct burn_track *t, int psub);

/* ts B20113 */
int sector_write_buffer(struct burn_drive *d,
			struct burn_track *track, int flag);

/* ts A61009 */
int sector_headers_is_ok(struct burn_write_opts *o, int mode);

int sector_headers(struct burn_write_opts *, unsigned char *,
		    int mode, int leadin);
void subcode_user(struct burn_write_opts *, unsigned char *s,
		  unsigned char tno, unsigned char control,
		  unsigned char index, struct isrc *isrc, int psub);

int sector_identify(unsigned char *);

void process_q(struct burn_drive *d, unsigned char *q);

#endif /* __SECTOR */
