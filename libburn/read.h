/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

#ifndef __LIBBURN_READ
#define __LIBBURN_READ

struct burn_drive;
struct burn_read_opts;

int burn_sector_length_read(struct burn_drive *d,
			    const struct burn_read_opts *o);
void burn_packet_process(struct burn_drive *d, unsigned char *data,
			 const struct burn_read_opts *o);

#endif /* __LIBBURN_READ */
