/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2010 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/


#ifndef __SBC
#define __SBC

struct burn_drive;

void sbc_load(struct burn_drive *);
void sbc_eject(struct burn_drive *);

/* ts A61118 */
int sbc_start_unit(struct burn_drive *);

/* ts A61021 : the sbc specific part of sg.c:enumerate_common()
*/
int sbc_setup_drive(struct burn_drive *d);

#endif /* __SBC */
