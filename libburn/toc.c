/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2011 - 2011 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/


#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

/* ts A61008 */
/* #include <a ssert.h> */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "toc.h"
#include "transport.h"
#include "libburn.h"
#include "sector.h"
#include "options.h"
#include "init.h"

#if 0
static void write_clonecd2(volatile struct toc *toc, int f);

static void write_clonecd2(volatile struct toc *toc, int f)
{
	int i;

	/* header */
	dprintf(f, "[CloneCD]\r\n");
	dprintf(f, "Version=2\r\n");
	dprintf(f, "\r\n");

	/* disc data */
	dprintf(f, "[Disc]\r\n");

	dprintf(f, "TocEntries=%d\r\n", toc->toc_entries);
	dprintf(f, "Sessions=%d\r\n", toc->sessions);
	dprintf(f, "DataTracksScrambled=%d\r\n", toc->datatracksscrambled);
	dprintf(f, "CDTextLength=%d\r\n", toc->cdtextlength);
	dprintf(f, "\r\n");

	/* session data */
	for (i = 0; i < toc->sessions; ++i) {
		dprintf(f, "[Session %d]\r\n", i + 1);

		{
			int m;

			switch (toc->session[i].track[0]->mode) {
			case BURN_MODE_RAW_DATA:
			case BURN_MODE_AUDIO:
				m = 0;
				break;
			case BURN_MODE0:
				m = 1;
				break;
			case BURN_MODE1:
			case BURN_MODE2_FORMLESS:
			case BURN_MODE2_FORM1:
			case BURN_MODE2_FORM2:
			case BURN_MODE_UNINITIALIZED:

				/* ts A61008 : do this softly without Assert */

				a ssert(0);	/* unhandled! find out ccd's
						   value for these modes! */
			}
			dprintf(f, "PreGapMode=%d\r\n", m);
		}
		dprintf(f, "\r\n");
	}

	for (i = 0; i < toc->toc_entries; ++i) {
		dprintf(f, "[Entry %d]\r\n", i);

		dprintf(f, "Session=%d\r\n", toc->toc_entry[i].session);
		dprintf(f, "Point=0x%02x\r\n", toc->toc_entry[i].point);
		dprintf(f, "ADR=0x%02x\r\n", toc->toc_entry[i].adr);
		dprintf(f, "Control=0x%02x\r\n", toc->toc_entry[i].control);
		dprintf(f, "TrackNo=%d\r\n", toc->toc_entry[i].tno);
		dprintf(f, "AMin=%d\r\n", toc->toc_entry[i].min);
		dprintf(f, "ASec=%d\r\n", toc->toc_entry[i].sec);
		dprintf(f, "AFrame=%d\r\n", toc->toc_entry[i].frame);
		dprintf(f, "ALBA=%d\r\n",
			burn_msf_to_lba(toc->toc_entry[i].min,
					toc->toc_entry[i].sec,
					toc->toc_entry[i].frame));
		dprintf(f, "Zero=%d\r\n", toc->toc_entry[i].zero);
		dprintf(f, "PMin=%d\r\n", toc->toc_entry[i].pmin);
		dprintf(f, "PSec=%d\r\n", toc->toc_entry[i].psec);
		dprintf(f, "PFrame=%d\r\n", toc->toc_entry[i].pframe);
		dprintf(f, "PLBA=%d\r\n",
			burn_msf_to_lba(toc->toc_entry[i].pmin,
					toc->toc_entry[i].psec,
					toc->toc_entry[i].pframe));
		dprintf(f, "\r\n");
	}
}
#endif

void toc_find_modes(struct burn_drive *d)
{
	int i, j;
	struct buffer *mem = NULL;
	struct burn_toc_entry *e;

/* ts A70519 : the code which needs this does not work with GNU/Linux 2.4 USB
	int lba;
	struct burn_read_opts o;

	o.raw = 1;
	o.c2errors = 0;
	o.subcodes_audio = 1;
	o.subcodes_data = 1;
	o.hardware_error_recovery = 1;
	o.report_recovered_errors = 0;
	o.transfer_damaged_blocks = 1;
	o.hardware_error_retries = 1;
*/

	BURN_ALLOC_MEM_VOID(mem, struct buffer, 1);

	mem->bytes = 0;
	mem->sectors = 1;

	for (i = 0; i < d->disc->sessions; i++)
		for (j = 0; j < d->disc->session[i]->tracks; j++) {
			struct burn_track *t = d->disc->session[i]->track[j];

			e = t->entry;
/* XXX | in the subcodes if appropriate! */
			if (e && !(e->control & 4)) {
				t->mode = BURN_AUDIO;
			} else {

				t->mode = BURN_MODE1;
/* ts A70519 : this does not work with GNU/Linux 2.4 USB because one cannot
               predict the exact dxfer_size without knowing the sector type.
				if (!e)
					lba = 0;
				else
					lba = burn_msf_to_lba(e->pmin, e->psec,
							      e->pframe);
				mem->sectors = 1;

				ts B21119 : Would now be d->read_cd() with
					    with sectype = 0 , mainch = 0xf8
				d->read_sectors(d, lba, mem.sectors, &o, mem);

				t->mode = sector_identify(mem->data);
*/
			}
		}

ex:
	BURN_FREE_MEM(mem);
}
