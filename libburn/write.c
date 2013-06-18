/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2012 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/


#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <unistd.h>
#include <signal.h>

/* ts A61009 */
/* #include <a ssert.h> */


/* ts A61106 : Deliberate defect provocation macros
               DO NOT DEFINE THESE IF YOU WANT SUCCESSFUL TAO !
#define Libburn_experimental_no_close_tracK 1
#define Libburn_experimental_no_close_sessioN 1
*/

/* ts A61114 : Highly experimental : try to achieve SAO on appendables
               THIS DOES NOT WORK YET !
#define Libburn_sao_can_appenD 1
*/

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "error.h"
#include "sector.h"
#include "libburn.h"
#include "drive.h"
#include "transport.h"
#include "debug.h"
#include "init.h"
#include "toc.h"
#include "util.h"
#include "sg.h"
#include "write.h"
#include "options.h"
#include "structure.h"
#include "source.h"
#include "mmc.h"
#include "spc.h"

#include "libdax_msgs.h"
extern struct libdax_msgs *libdax_messenger;


/* ts A91120 : <<< experimental */
#ifdef Libburn_mmap_write_buffeR
#include <sys/mman.h>
#endif


/* The maximum output size to be used with CD media. This is also curbed
   by BURN_OS_TRANSPORT_BUFFER_SIZE. The smaller number gets into effect.
*/ 
#define Libburn_cd_obS (32 * 1024)

/* The size to be used with DVD media.
*/
#define Libburn_dvd_obS (32 * 1024)

/* The size to be used with BD-RE media in normal, not streamed mode.
*/
#define Libburn_bd_re_obS (32 * 1024)

/* The size to be used with BD-RE media in streamed mode.
*/
#define Libburn_bd_re_streamed_obS (64 * 1024)


static int type_to_ctrl(int mode)
{
	int ctrl = 0;

	int data = BURN_MODE2 | BURN_MODE1 | BURN_MODE0;

	if (mode & data) {
		ctrl |= 4;
	} else if (mode & BURN_AUDIO) {
		if (mode & BURN_4CH)
			ctrl |= 8;
		if (mode & BURN_PREEMPHASIS)
			ctrl |= 1;
	} else
		/* ts A61008 */
		/* a ssert(0); */
		return -1;

	if (mode & BURN_COPY)
		ctrl |= 2;

	return ctrl;
}

/* only the ctrl nibble is set here (not adr) */
/* ts A61009 : removed "static" , reacted on type_to_ctrl() == -1
               preserved ignorance towards unknown modes (for now) */
void type_to_form(int mode, unsigned char *ctladr, int *form)
{
	int ret;

	ret = type_to_ctrl(mode) << 4;
	if (ret == -1) {
		*ctladr = 0xff;
		*form = -1;
		return;
	}
	*ctladr = ret;

	if (mode & BURN_AUDIO)
		*form = 0;
	if (mode & BURN_MODE0) {

		/* ts A61009 */
		/* a ssert(0); */
		*form = -1;
		return;
	}

	if (mode & BURN_MODE1)
		*form = 0x10;
	if (mode & BURN_MODE2) {

		/* ts A61009 */
		/* a ssert(0); */ /* XXX someone's gonna want this sometime */
		*form = -1;
		return;
	}

	if (mode & BURN_MODE_RAW)
		*form = 0;
	if (mode & BURN_SUBCODE_P16)	/* must be expanded to R96 */
		*form |= 0x40;
	if (mode & BURN_SUBCODE_P96)
		*form |= 0xC0;
	if (mode & BURN_SUBCODE_R96)
		*form |= 0x40;
}


/* ts A71002 : outsourced from burn_write_flush() : no sync cache here */
int burn_write_flush_buffer(struct burn_write_opts *o,struct burn_track *track)
{
	struct burn_drive *d = o->drive;

	if (d->buffer->bytes && !d->cancel) {
		int err;
		err = d->write(d, d->nwa, d->buffer);
		if (err == BE_CANCELLED)
			return 0;
		/* A61101 */
		if(track != NULL) {
			track->writecount += d->buffer->bytes;
			track->written_sectors += d->buffer->sectors;
		}
		/* ts A61119 */
		d->progress.buffered_bytes += d->buffer->bytes;

		d->nwa += d->buffer->sectors;
		d->buffer->bytes = 0;
		d->buffer->sectors = 0;
	}
	return 1;
}


int burn_write_flush(struct burn_write_opts *o, struct burn_track *track)
{
	int ret;
	struct burn_drive *d = o->drive;

	ret = burn_write_flush_buffer(o, track);
	if (ret <= 0)
		return ret;
	d->sync_cache(d);
	return 1;
}


/* ts A71002 : outsourced from burn_write_close_track() */
int burn_write_track_minsize(struct burn_write_opts *o, struct burn_session *s,
				int tnum)
{
	char msg[81];
	struct burn_drive *d;
	struct burn_track *t;
	int todo, step, cancelled, seclen;

	d = o->drive;
	t = s->track[tnum];

	/* ts A61103 : pad up track to minimum size of 600 sectors */
	if (t->written_sectors < 300) {
		todo = 300 - t->written_sectors;
		sprintf(msg,"Padding up track to minimum size (+ %d sectors)",
			todo);
		libdax_msgs_submit(libdax_messenger, o->drive->global_index,
			0x0002011a,
			LIBDAX_MSGS_SEV_NOTE, LIBDAX_MSGS_PRIO_HIGH, msg,0,0);
		step = BUFFER_SIZE / 4096; /* shall fit any sector size */
		if (step <= 0)
			step = 1;
		seclen = burn_sector_length(t->mode);
		if (seclen <= 0)
			seclen = 2048;
		memset(d->buffer, 0, sizeof(struct buffer));
		cancelled = d->cancel;
		for (; todo > 0; todo -= step) {
			if (step > todo)
				step = todo;
			d->buffer->bytes = step*seclen;
			d->buffer->sectors = step;
			d->cancel = 0;
			d->write(d, d->nwa, d->buffer);
			d->nwa += d->buffer->sectors;
			t->writecount += d->buffer->bytes;
			t->written_sectors += d->buffer->sectors;
			d->progress.buffered_bytes += d->buffer->bytes;
		}
		d->cancel = cancelled;
	}
	return 1;
}


/* ts A61030 */
int burn_write_close_track(struct burn_write_opts *o, struct burn_session *s,
				int tnum)
{
	char msg[81];
	struct burn_drive *d;

	/* ts A61106 */
#ifdef Libburn_experimental_no_close_tracK
	return 1;
#endif

	d = o->drive;

	d->busy = BURN_DRIVE_CLOSING_TRACK;

	sprintf(msg, "Closing track %2.2d", tnum+1);
	libdax_msgs_submit(libdax_messenger, o->drive->global_index,0x00020119,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH, msg,0,0);

	/* MMC-1 mentions track number 0xFF for "the incomplete track",
	   MMC-3 does not. I tried both. 0xFF was in effect when other
	   bugs finally gave up and made way for readable tracks. */
	/* ts A70129 
	   Probably the right value for appendables is d->last_track_no
	*/
	d->close_track_session(o->drive, 0, 0xff);

	d->busy = BURN_DRIVE_WRITING;

	return 1;
}


/* ts A61030 */
int burn_write_close_session(struct burn_write_opts *o)
{

	/* ts A61106 */
#ifdef Libburn_experimental_no_close_sessioN
	return 1;
#endif

	libdax_msgs_submit(libdax_messenger, o->drive->global_index,0x00020119,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			"Closing session", 0, 0);

	/* ts A61102 */
	o->drive->busy = BURN_DRIVE_CLOSING_SESSION;

	o->drive->close_track_session(o->drive, 1, 0);

	/* ts A61102 */
	o->drive->busy = BURN_DRIVE_WRITING;

	return 1;
}


/* ts A60819, B20101:
   This is useful only when changes about CD SAO get tested.
 # define Libburn_write_with_function_print_cuE yes
*/

#ifdef Libburn_write_with_function_print_cuE


static char cue_printify(char c)
{
	if (c >= 32 && c < 127)
		return c;
	return '#';
}


static void print_cue(struct cue_sheet *sheet)
{
	int i;
	unsigned char *unit;

	printf("\n");
	printf("ctladr|trno|indx|form|scms|   msf    |  text\n");
	printf("------+----+----+----+----+----------+--------\n");
	for (i = 0; i < sheet->count; i++) {
		unit = sheet->data + 8 * i;
		if ((unit[0] & 0xf) == 2) {
			printf(
		" %1X  %1X |    |    |    |    |          | %c%c%c%c%c%c%c\n",
				(unit[0] & 0xf0) >> 4, unit[0] & 0xf,
				cue_printify(unit[1]), cue_printify(unit[2]), 
				cue_printify(unit[3]), cue_printify(unit[4]), 
				cue_printify(unit[5]), cue_printify(unit[6]), 
				unit[7] == 0 ? ' ' : cue_printify(unit[7]));
		} else if ((unit[0] & 0xf) == 3) {
			printf(
		" %1X  %1X | %2d |    |    |    |          | %c%c%c%c%c%c\n",
				(unit[0] & 0xf0) >> 4, unit[0] & 0xf,
				unit[1], cue_printify(unit[2]), 
				cue_printify(unit[3]), cue_printify(unit[4]), 
				cue_printify(unit[5]), cue_printify(unit[6]), 
				cue_printify(unit[7]));
		} else if (unit[1] > 99) {
			printf(" %1X  %1X |0x%02X| %2d | %02X | %02X |",
				(unit[0] & 0xf0) >> 4, unit[0] & 0xf,
				unit[1], unit[2], unit[3], unit[4]);
			printf(" %02d:%02d:%02d |\n",
				unit[5], unit[6], unit[7]);
		} else {
			printf(" %1X  %1X | %2d | %2d | %02X | %02X |",
				(unit[0] & 0xf0) >> 4, unit[0] & 0xf,
				unit[1], unit[2], unit[3], unit[4]);
			printf(" %02d:%02d:%02d |\n",
				unit[5], unit[6], unit[7]);
		}
	}
	fflush(stdout);
}

#endif /* Libburn_write_with_print_cuE */


/* ts B11226 */
static int new_cue(struct cue_sheet *sheet, int number, int flag)
{
	unsigned char *ptr;

	ptr = realloc(sheet->data, (sheet->count + number) * 8);
	if (ptr == NULL) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00020111,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
		"Could not allocate new auxiliary object (cue_sheet->data)",
			0, 0);
		return -1;
	}
	sheet->data = ptr;
	sheet->count += number;
	return 1;
}


/* ts B11226 : outsourced new_cue() */
/** @return 1 = success , <=0 failure */
static int add_cue(struct cue_sheet *sheet, unsigned char ctladr,
		    unsigned char tno, unsigned char indx,
		    unsigned char form, unsigned char scms, int lba)
{
	unsigned char *unit;
	int m, s, f, ret;

	burn_lba_to_msf(lba, &m, &s, &f);

	ret = new_cue(sheet, 1, 0);
	if (ret <= 0)
		return -1;
	unit = sheet->data + (sheet->count - 1) * 8;
	unit[0] = ctladr;
	unit[1] = tno;
	unit[2] = indx;
	unit[3] = form;
	unit[4] = scms;
	unit[5] = m;
	unit[6] = s;
	unit[7] = f;
	return 1;
}


/* ts B11226 */
static int add_catalog_cue(struct cue_sheet *sheet, unsigned char catalog[13])
{
	unsigned char *unit;
	int i, ret;

	ret = new_cue(sheet, 2, 0);
	if (ret <= 0)
		return -1;
	unit = sheet->data + (sheet->count - 2) * 8;
	unit[0] = unit[8] = 0x02;
	for (i = 0; i < 13; i++)
		unit[1 + (i >= 7) * 8 + (i % 7)] = catalog[i];
	unit[15] = 0x00;
	return 1;
}


/* ts B11226 */
static int add_isrc_cue(struct cue_sheet *sheet, unsigned char ctladr, int tno,
			struct isrc *isrc)
{
	unsigned char *unit;
	int i, ret;
	char text[8];

	ret = new_cue(sheet, 2, 0);
	if (ret <= 0)
		return -1;
	unit = sheet->data + (sheet->count - 2) * 8;
	unit[0] = unit[8] = (ctladr & 0xf0) | 0x03;
	unit[1] = unit[9] = tno;
	unit[2] = isrc->country[0];
	unit[3] = isrc->country[1];
	unit[4] = isrc->owner[0];
	unit[5] = isrc->owner[1];
	unit[6] = isrc->owner[2];
	sprintf(text, "%-2.2u%-5.5u", (unsigned int) isrc->year, isrc->serial);
	unit[7] = text[0];
	for (i = 1; i < 7; i++)
		unit[9 + i] = text[i];
	return 1;
}


/* ts A61114: added parameter nwa */
struct cue_sheet *burn_create_toc_entries(struct burn_write_opts *o,
					  struct burn_session *session,
					  int nwa)
{
	int i, m, s, f, form, runtime = -150, ret, track_length;
	int leadin_form, leadin_start, pregap = 150, postgap;
	unsigned char ctladr, scms;
	struct burn_drive *d;
	struct burn_toc_entry *e;
	struct cue_sheet *sheet;
	struct burn_track **tar = session->track;
	int ntr = session->tracks;
	int rem = 0;

#define Libburn_track_multi_indeX yes

#ifdef Libburn_track_multi_indeX
	int j;
#else
	int pform;
#endif

	if (ntr < 1) {
		libdax_msgs_submit(libdax_messenger, -1, 0x0002019c,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"Session has no defined tracks", 0, 0);
		return NULL;
	}

	d = o->drive;

#ifdef Libburn_sao_can_appenD
	if (d->status == BURN_DISC_APPENDABLE)
 		runtime = nwa-150;
#endif

	sheet = calloc(1, sizeof(struct cue_sheet));

	/* ts A61009 : react on failures of calloc(), add_cue_sheet()
	               type_to_form() */
	if (sheet == NULL) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00020111,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Could not allocate new auxiliary object (cue_sheet)",
			0, 0);
		return NULL;
	}

	sheet->data = NULL;
	sheet->count = 0;
	type_to_form(tar[0]->mode, &ctladr, &form);
	if (form == -1) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00020116,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Track mode has unusable value", 0, 0);
		goto failed;
	}
	if (tar[0]->mode & BURN_AUDIO)
		leadin_form = 0x01;
	else
		leadin_form = 0x14;
	if (o->num_text_packs > 0) {
		leadin_form |= 0x40;
	} else {
		/* Check for CD-TEXT in session. Not the final creation,
		   because the cue sheet content might be needed for CD-TEXT
		   pack type 0x88 "TOC".
		 */
		if (o->text_packs == NULL) {
			ret = burn_cdtext_from_session(session, NULL, NULL, 1);
			if (ret < 0)
				goto failed;
			else if (ret > 0)
				leadin_form |= 0x40;
		}
	}

	if (o->has_mediacatalog)
		ret = add_catalog_cue(sheet, o->mediacatalog);
	else if (session->mediacatalog[0])
		ret = add_catalog_cue(sheet, session->mediacatalog);
	else
		ret = 1;
	if (ret <= 0)
		goto failed;

	/* ts B11225 
	   MMC-5 6.33.3.15 Data Form of Sub-channel
	   seems to indicate that for leadin_form 0x41 one should announce
	   d->start_lba as start of the leadin (e.g. -12490) and that data
	   block type should 2 or 3 with mode page 05h. But my drives refuse
	   on that.
           It works with LBA -150 and data block type 0. Shrug.
	*/
	leadin_start = runtime;
	ret = add_cue(sheet, (ctladr & 64) | 1, 0, 0, leadin_form, 0,
								leadin_start);
	if (ret <= 0)
		goto failed;

	d->toc_entries = ntr + 3;

	/* ts A61009 */
	/* a ssert(d->toc_entry == NULL); */
	if (d->toc_entry != NULL) {

		/* ts A61109 : this happens with appendable CDs 
			>>> Open question: is the existing TOC needed ? */

		/* ts A61109 : for non-SAO, this sheet is thrown away later */
		free((char *) d->toc_entry);

		/*
		libdax_msgs_submit(libdax_messenger,
			d->global_index, 0x00020117,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"toc_entry of drive is already in use", 0, 0);
		goto failed;
		*/
	}
	if (session->firsttrack + ntr - 1 > 99) {
		libdax_msgs_submit(libdax_messenger, -1, 0x0002019b,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"CD track number exceeds 99", 0, 0);
		goto failed;
	}
	session->lasttrack = session->firsttrack + ntr - 1;

	d->toc_entry = calloc(d->toc_entries, sizeof(struct burn_toc_entry));
	e = d->toc_entry;
	e[0].point = 0xA0;
	if (tar[0]->mode & BURN_AUDIO)
		e[0].control = TOC_CONTROL_AUDIO;
	else
		e[0].control = TOC_CONTROL_DATA;
	e[0].pmin = session->firsttrack;
	e[0].psec = o->format;
	e[0].adr = 1;
	e[1].point = 0xA1;
	e[1].pmin = session->lasttrack;
	e[1].adr = 1;
	if (tar[ntr - 1]->mode & BURN_AUDIO)
		e[1].control = TOC_CONTROL_AUDIO;
	else
		e[1].control = TOC_CONTROL_DATA;
	e[2].point = 0xA2;
	e[2].control = e[1].control;
	e[2].adr = 1;

	tar[0]->pregap2 = 1;
	if (tar[0]->pregap2_size < 150)
		tar[0]->pregap2_size = 150;

#ifndef Libburn_track_multi_indeX
	pform = form;
#endif

	for (i = 0; i < ntr; i++) {

		/* ts A70125 : 
		   Still not understanding the sense behind linking tracks,
		   i decided to at least enforce the MMC specs' minimum
		   track length.
		*/ 
		track_length = burn_track_get_sectors_2(tar[i], 1);
		if (track_length < 300 && !burn_track_is_open_ended(tar[i])) {
			track_length = 300;
			if (!tar[i]->pad)
				tar[i]->pad = 1;
			burn_track_set_sectors(tar[i], track_length);
		}

		type_to_form(tar[i]->mode, &ctladr, &form);
		if (tar[i]->mode & BURN_SCMS)
			scms = 0x80;
		else
			scms = 0; 

		if (tar[i]->isrc.has_isrc) {
			ret = add_isrc_cue(sheet, ctladr,
				i + session->firsttrack, &(tar[i]->isrc));
			if (ret <= 0)
				goto failed;
		}
		pregap = 0;
		if (tar[i]->pregap2)
			pregap = tar[i]->pregap2_size;
		postgap = 0;
		if (tar[i]->postgap) {
			if (tar[i]->indices >= 99) {
				libdax_msgs_submit(libdax_messenger, -1,
					0x0002019a, LIBDAX_MSGS_SEV_SORRY,
					LIBDAX_MSGS_PRIO_HIGH,
					"Post-gap index number exceeds 99",
					0, 0);
				goto failed;
			}
			if (tar[i]->indices < 2)
				tar[i]->indices = 2;
			tar[i]->index[tar[i]->indices] = track_length;
			postgap = tar[i]->postgap_size;
		}

#ifdef Libburn_track_multi_indeX

		for(j = 0; j < (tar[i]->indices + !!tar[i]->postgap) || j < 2;
									j++) {
			if(tar[i]->index[j] == 0x7fffffff) {
				if (j > 1)
		break;
				if (j == 0 && pregap <= 0)
		continue;
				/* force existence of mandatory index */
				tar[i]->index[j] = 0;
			} else if (j == 0) {
				tar[i]->index[j] = 0;
			} else if (j == 1 && tar[i]->index[0] == 0x7fffffff) {
				tar[i]->index[j] = 0;
			}

			if (j == 1) {
				tar[i]->entry = &e[3 + i];
				e[3 + i].point = i + session->firsttrack;
				burn_lba_to_msf(runtime, &m, &s, &f);
				e[3 + i].pmin = m;
				e[3 + i].psec = s;
				e[3 + i].pframe = f;
				e[3 + i].adr = 1;
				e[3 + i].control = type_to_ctrl(tar[i]->mode);
			}

			/* >>> ??? else if j == 0 && mode change to -data :
							Extended pregap */;

			/* >>> check index with track size */;

			tar[i]->index[j] += runtime;
			ret = add_cue(sheet, ctladr | 1,
					i + session->firsttrack, j, form, scms,
					tar[i]->index[j]);
			if (ret <= 0)
				goto failed;
			runtime += pregap;
			pregap = 0;
		}

		runtime += track_length + postgap;

#else /* Libburn_track_multi_indeX */

		if (i == 0) {
			ret = add_cue(sheet, ctladr | 1, session->firsttrack,
					0, form, 0, runtime);
			if (ret <= 0)
				goto failed;
			runtime += 150;
		} else if (pform != form) {

		/* ts A70121 : This seems to be thw wrong test. Correct would
		   be to compare tar[]->mode or bit2 of ctladr.
		*/ 

			ret = add_cue(sheet, ctladr | 1,
					i + session->firsttrack, 0, form, scms,
					runtime);
			if (ret <= 0)
				goto failed;

			runtime += 150;
/* XXX fix pregap interval 1 for data tracks */
/* ts A60813 silence righteous compiler warning about C++ style comments
   This is possibly not a comment but rather a trace of Derek Foreman
   experiments. Thus not to be beautified - but to be preserved rectified.
/ /                      if (!(form & BURN_AUDIO))
/ /                              tar[i]->pregap1 = 1;
*/
/* ts A70121 : it is unclear why (form & BURN_AUDIO) should prevent pregap1.
   I believe, correct would be:
			runtime += 75;
			tar[i]->pregap1 = 1;

   The test for pform != form is wrong anyway. 

   Next one has to care for Post-gap: table 555 in mmc5r03c.pdf does not
   show any although 6.33.3.19 would prescribe some.
   ts B20111: Table 1 of MMC-1 shows two post-gaps. The first matches the
              precriptions with SEND CUE SHEET. The second one is riddling.
              Both are part of a track and occupy the range of the last index
              of the track. Length is 2 seconds for each.

   Nobody seems to have ever tested this situation, up to now.
   It is banned for now in burn_disc_write().
   Warning have been placed in libburn.h .
*/

			tar[i]->pregap2 = 1;
		}
/* XXX HERE IS WHERE WE DO INDICES IN THE CUE SHEET */
/* XXX and we should make sure the gaps conform to ecma-130... */
		tar[i]->entry = &e[3 + i];
		e[3 + i].point = i + session->firsttrack;
		burn_lba_to_msf(runtime, &m, &s, &f);
		e[3 + i].pmin = m;
		e[3 + i].psec = s;
		e[3 + i].pframe = f;
		e[3 + i].adr = 1;
		e[3 + i].control = type_to_ctrl(tar[i]->mode);

		ret = add_cue(sheet, ctladr | 1, i + session->firsttrack,
				1, form, scms, runtime);
		if (ret <= 0)
			goto failed;

		runtime += track_length;

#endif /* ! Libburn_track_multi_indeX */


/* if we're padding, we'll clear any current shortage.
   if we're not, we'll slip toc entries by a sector every time our
   shortage is more than a sector
XXX this is untested :)
*/
		if (!tar[i]->pad) {
			rem += burn_track_get_shortage(tar[i]);

			/* ts A61101 : I doubt that linking would yield a
					desireable effect. With TAO it is
					counterproductive in any way.
			*/
			if (o->write_type == BURN_WRITE_TAO)
				tar[i]->source->next = NULL;
			else

				if (i +1 != ntr)
					tar[i]->source->next = tar[i+1]->source;
		} else if (rem) {
			rem = 0;
			runtime++;
		}
		if (rem > burn_sector_length(tar[i]->mode)) {
			rem -= burn_sector_length(tar[i]->mode);
			runtime--;
		}

#ifndef Libburn_track_multi_indeX
		pform = form;
#endif

	}
	burn_lba_to_msf(runtime, &m, &s, &f);
	e[2].pmin = m;
	e[2].psec = s;
	e[2].pframe = f;

	ret = add_cue(sheet, ctladr | 1, 0xAA, 1, leadin_form & 0x3f,
                      0, runtime);
	if (ret <= 0)
		goto failed;
	return sheet;

failed:;
	if (sheet != NULL)
		free((char *) sheet);
	return NULL;
}

int burn_sector_length(int tracktype)
{
	if (tracktype & BURN_AUDIO)
		return 2352;
	if (tracktype & BURN_MODE_RAW)
		return 2352;
	if (tracktype & BURN_MODE1)
		return 2048;
	/* ts A61009 */
	/* a ssert(0); */
	return -1;
}

int burn_subcode_length(int tracktype)
{
	if (tracktype & BURN_SUBCODE_P16)
		return 16;
	if ((tracktype & BURN_SUBCODE_P96) || (tracktype & BURN_SUBCODE_R96))
		return 96;
	return 0;
}

int burn_write_leadin(struct burn_write_opts *o,
		       struct burn_session *s, int first)
{
	struct burn_drive *d = o->drive;
	int count;

	d->busy = BURN_DRIVE_WRITING_LEADIN;

	if (first)
		count = 0 - d->alba - 150;
	else
		count = 4500;

	d->progress.start_sector = d->alba;
	d->progress.sectors = count;
	d->progress.sector = 0;

	while (count != 0) {
		if (!sector_toc(o, s->track[0]->mode))
			return 0;
		count--;
		d->progress.sector++;
	}
	d->busy = BURN_DRIVE_WRITING;
	return 1;
}

int burn_write_leadout(struct burn_write_opts *o,
			int first, unsigned char control, int mode)
{
	struct burn_drive *d = o->drive;
	int count;

	d->busy = BURN_DRIVE_WRITING_LEADOUT;

	d->rlba = -150;
	if (first)
		count = 6750;
	else
		count = 2250;
	d->progress.start_sector = d->alba;
	d->progress.sectors = count;
	d->progress.sector = 0;

	while (count != 0) {
		if (!sector_lout(o, control, mode))
			return 0;
		count--;
		d->progress.sector++;
	}
	d->busy = BURN_DRIVE_WRITING;
	return 1;
}


static int burn_create_text_packs(struct burn_write_opts *o,
					struct burn_session *s,
					int flag)
{
	int ret, num_packs = 0;
	unsigned char *text_packs = NULL;

	ret = burn_cdtext_from_session(s, &text_packs, &num_packs, 0);
	if (ret > 0) {
		if (o->text_packs != NULL)
			free(o->text_packs);
		o->text_packs = text_packs;
		o->num_text_packs = num_packs;
	}
	return(ret);
}


static int burn_write_leadin_cdtext(struct burn_write_opts *o,
					 struct burn_session *s, int flag)
{
	int ret, i, j, si, lba, sub_cursor = 0, err, write_lba, sectors = 0;
	int self_made_text_packs = 0;
	unsigned char *subdata = NULL;
	struct burn_drive *d = o->drive;
	struct buffer *buf = NULL;
	enum burn_drive_status was_busy = o->drive->busy;
#ifdef Libburn_debug_cd_texT
	unsigned char *packs;
#endif

	if (o->num_text_packs <= 0) {
		if (o->text_packs != NULL)
			{ret = 1; goto ex;}
		/* Try to create CD-TEXT from .cdtext_* of session and track */
		ret = burn_create_text_packs(o, s, 0);
		self_made_text_packs = 1;
		if (ret <= 0)
			goto ex;
		if (o->num_text_packs <= 0)
			{ret = 1; goto ex;}
	}

	if (!o->no_text_pack_crc_check) {
		ret = burn_cdtext_crc_mismatches(o->text_packs,
				 o->num_text_packs, 0);
		if (ret != 0) {
			libdax_msgs_submit(libdax_messenger, -1, 0x0002018f,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				"Program error: CD-TEXT pack CRC mismatch",
				0, 0);
			{ ret = 0; goto ex; }
		}
	}

	d->busy = BURN_DRIVE_WRITING_LEADIN;

#ifdef Libburn_debug_cd_texT
	packs = o->text_packs;
	fprintf(stderr,
		"libburn_DEBUG: 8 bit CD-TEXT packs to be transmitted:\n"); 
	for (i = 0; i < 18 * o->num_text_packs; i += 18) {
		fprintf(stderr, "%4d :", i / 18);
		for (j = 0; j < 18; j++) {
			if (j >= 4 && j <= 15 && packs[i + j] >= 32 &&
			    packs[i + j] <= 126 && packs[i] != 0x88 &&
			    packs[i] != 0x89 && packs[i] != 0x8f)
				fprintf(stderr, "  %c", packs[i + j]);
			else
				fprintf(stderr, " %2.2X", packs[i + j]);
		}
		fprintf(stderr, "\n");
	}
#endif /* Libburn_debug_cd_texT */

	/* Chop from 8 bit text pack to 6 bit subchannel */
	BURN_ALLOC_MEM(subdata, unsigned char, o->num_text_packs * 24);
	for (i = 0; i < 18 * o->num_text_packs; i += 3) {
		si = i / 3 * 4;
		subdata[si + 0] =  (o->text_packs[i + 0] >> 2) & 0x3f;
		subdata[si + 1] =  (o->text_packs[i + 0] << 4) & 0x30;
		subdata[si + 1] |= (o->text_packs[i + 1] >> 4) & 0x0f;
		subdata[si + 2] =  (o->text_packs[i + 1] << 2) & 0x3c;
		subdata[si + 2] |= (o->text_packs[i + 2] >> 6) & 0x03;
		subdata[si + 3] =  (o->text_packs[i + 2] >> 0) & 0x3f;
	}

	/* Start at Lead-in address of ATIP and write blocks up to -150 */
	BURN_ALLOC_MEM(buf, struct buffer, 1);
	write_lba = d->start_lba;
	for (lba = d->start_lba; lba < -150; lba++) {
		/* Collect subdata in buf */
		for (j = 0; j < 4; j++) {
			memcpy(buf->data + buf->bytes,
				subdata + sub_cursor * 24, 24);
			sub_cursor = (sub_cursor + 1) % o->num_text_packs;
			buf->bytes += 24;
		}
		buf->sectors++;
		sectors++;

		/* When full or last sector : perform WRITE */
		if (buf->bytes + 96 >= 32768 || lba == -151) {

#ifdef Libburn_debug_cd_texT
			fprintf(stderr,
			    "libburn_DEBUG: 6 bit data to be transmitted:\n"); 
			for (i = 0; i < buf->bytes; i += 24) {
				fprintf(stderr, "%4d :", i / 24);
				for (j = 0; j < 24; j++)
					fprintf(stderr, " %2.2X",
						buf->data[i + j]);
				fprintf(stderr, "\n");
			}
#endif /* Libburn_debug_cd_texT */
	
			err = d->write(d, write_lba, buf);
			if (err == BE_CANCELLED)
				{ ret = 0; goto ex; }
			write_lba += sectors;
			sectors = buf->sectors = buf->bytes = 0;
		}
	}
	ret = 1;
ex:;
	if (self_made_text_packs) {
		if (o->text_packs != NULL)
			free(o->text_packs);
		o->text_packs = NULL;
		o->num_text_packs = 0;
	}
	BURN_FREE_MEM(subdata);
	BURN_FREE_MEM(buf);
	d->busy = was_busy;
	return ret;
}


int burn_write_session(struct burn_write_opts *o, struct burn_session *s)
{
	struct burn_drive *d = o->drive;
	int i, ret;

	if (o->write_type == BURN_WRITE_SAO) {
		ret = burn_write_leadin_cdtext(o, s, 0);
		if (ret <= 0)
			goto ex;
	}
	d->rlba = 0;
	for (i = 0; i < s->tracks; i++) {
		if (!burn_write_track(o, s, i))
			{ ret = 0; goto ex; }
	}

	/* ts A61103 */
	ret = 1;
ex:;
	if (o->write_type == BURN_WRITE_TAO)
		burn_write_close_session(o);
	return ret;
}


/* ts A61218 : outsourced from burn_write_track() */
int burn_disc_init_track_status(struct burn_write_opts *o,
				struct burn_session *s, struct burn_track *t,
				int tnum, int sectors)
{
	struct burn_drive *d = o->drive;

	/* Update progress */

	d->progress.start_sector = d->nwa;

	d->progress.sectors = sectors;
	d->progress.sector = 0;

	/* ts A60831: added tnum-line, extended print message on proposal
           by bonfire-app@wanadoo.fr in http://libburn.pykix.org/ticket/58 */
        d->progress.track = tnum;

	/* ts B20113 */
	d->progress.indices = t->indices;
	d->progress.index = 0;
	if (d->progress.indices > 1)
		if (t->index[0] == 0x7fffffff)
			d->progress.index = 1;

	/* ts A61102 */
	d->busy = BURN_DRIVE_WRITING;

	return 1;
}


int burn_write_track(struct burn_write_opts *o, struct burn_session *s,
		      int tnum)
{
	struct burn_track *t = s->track[tnum];
	struct burn_drive *d = o->drive;
	int i, tmp = 0, open_ended = 0, ret= 0, nwa, lba;
	int sectors;
	char msg[80];

	d->rlba = -150;

/* XXX for tao, we don't want the pregaps  but still want post? */
	if (o->write_type != BURN_WRITE_TAO) {

		/* ts A61102 */
		d->busy = BURN_DRIVE_WRITING_PREGAP;

		if (t->pregap1)
			d->rlba += 75;
		if (t->pregap2)
			d->rlba += t->pregap2_size;

		if (t->pregap1) {

			struct burn_track *pt;
			/* ts A70121 : Removed pseudo suicidal initializer 
				 = s->track[tnum - 1];
			*/

			if (tnum == 0) {

				/* ts A70121 : This is not possible because
				   track 1 cannot have a pregap at all.
				   MMC-5 6.33.3.2 precribes a mandatory pause
				   prior to any track 1. Pre-gap is prescribed
				   for mode changes like audio-to-data.
				   To set burn_track.pregap1 for track 1 is
				   kindof a dirty hack.
				*/

				printf("first track should not have a pregap1\n");
				pt = t;
			} else
				pt = s->track[tnum - 1]; /* ts A70121 */
			for (i = 0; i < 75; i++)
				if (!sector_pregap(o, t->entry->point,
					           pt->entry->control, pt->mode))
					{ ret = 0; goto ex; }
		}
		if (t->pregap2)
			for (i = 0; i < t->pregap2_size; i++)
				if (!sector_pregap(o, t->entry->point,
					           t->entry->control, t->mode))
					{ ret = 0; goto ex; }

		/* ts B20113 : Flush buffer to avoid influence pregap
		               on track counter */
		ret = sector_write_buffer(d, NULL, 0);
		if (ret <= 0)
			goto ex;

	} else {
		o->control = t->entry->control;
		d->send_write_parameters(d, s, tnum, o);

		/* ts A61103 */
		ret = d->get_nwa(d, -1, &lba, &nwa);

		/* ts A70213: CD-TAO: eventually expand size of track to max */
		burn_track_apply_fillup(t, d->media_capacity_remaining, 0);

		/* <<< */
		sprintf(msg, 
	"TAO pre-track %2.2d : get_nwa(%d)=%d, d=%d , demand=%.f , cap=%.f\n",
			tnum+1, nwa, ret, d->nwa,
			(double) burn_track_get_sectors_2(t, 1) * 2048.0,
			(double) d->media_capacity_remaining);
		libdax_msgs_submit(libdax_messenger, d->global_index,
				 0x00000002,
				LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
				msg, 0, 0);

		/* ts A91003 */
		if (nwa < d->nwa) {
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020173,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			   "Drive tells NWA smaller than last written address",
				0, 0);
			d->sync_cache(d);
			return 0;
		}
		d->nwa = nwa;

	}

/* user data */

	sectors = burn_track_get_sectors_2(t, 1);
	open_ended = burn_track_is_open_ended(t);

	burn_disc_init_track_status(o, s, t, tnum, sectors);

	/* ts A61030 : this cannot happen. tnum is always < s->tracks */
	if (tnum == s->tracks)
		tmp = sectors > 150 ? 150 : sectors;

	for (i = 0; open_ended || i < sectors - tmp; i++) {

		/* ts A61023 : http://libburn.pykix.org/ticket/14
                               From time to time inquire drive buffer */
		if ((i%64)==0)
			d->read_buffer_capacity(d);

		if (!sector_data(o, t, 0))
			{ ret = 0; goto ex; }

		/* ts A61031 */
		if (open_ended) {
			d->progress.sectors = sectors = i;
                        if (burn_track_is_data_done(t)) 
	break;
		}

		/* update current progress */
		d->progress.sector++;
	}
	for (; i < sectors; i++) {

		/* ts A61030: program execution never gets to this point */
		fprintf(stderr,"LIBBURN_DEBUG: TNUM=%d  TRACKS=%d  TMP=%d\n",
			tnum, s->tracks, tmp);

		/* ts A61023 */
		if ((i%64)==0)
			d->read_buffer_capacity(d);

		if (!sector_data(o, t, 1))
			{ ret = 0; goto ex; }

		/* update progress */
		d->progress.sector++;
	}

	/* ts B20113 : Flush buffer to get buffered bytes assigned to the
	               track counter */
	ret = sector_write_buffer(d, t, 0);
	if (ret <= 0)
		goto ex;

	if (t->postgap && o->write_type != BURN_WRITE_TAO) {
		for (i = 0; i < t->postgap_size; i++)
			if (!sector_postgap(o, t->entry->point,
						 t->entry->control, t->mode))
				{ ret = 0; goto ex; }
		ret = sector_write_buffer(d, NULL, 0);
		if (ret <= 0)
			goto ex;
	}

	/* ts A61103 */
	ret = 1;
ex:;
	if (d->cancel)
		burn_source_cancel(t->source);
	if (o->write_type == BURN_WRITE_TAO) {

		/* ts A71002 */
		if (!burn_write_flush_buffer(o, t))
			ret = 0;

		/* Ensure that at least 600 kB get written */
		burn_write_track_minsize(o, s, tnum);
		d->sync_cache(d);

		/* ts A61030 */
		/* ts A91003 :
		   At least in simulation mode this causes NWA=0 for the
		   next track. cdrecord does not use CLOSE TRACK at all but
		   ends the tracks by SYNCHRONIZE CACHE alone.
		*/
		/* ts A91202 :
		   Peng Shao reports that his LG GH22LS30 issues an SCSI error
		   on CLOSE TRACK even in non-dummy mode. So i better give up
		   this gesture which seems not be needed by any drive.
			if (!o->simulate)
				if (burn_write_close_track(o, s, tnum) <= 0)
					ret = 0;
		*/
	}
	return ret;
}

/* ts A61009 */
/* @param flag bit1 = do not libdax_msgs_submit() */
int burn_disc_write_is_ok(struct burn_write_opts *o, struct burn_disc *disc,
			int flag)
{
	int i, t;
	char msg[80];

	for (i = 0; i < disc->sessions; i++)
		for (t = 0; t < disc->session[i]->tracks; t++)
			if (sector_headers_is_ok(
				o, disc->session[i]->track[t]->mode) != 1)
				goto bad_track_mode_found;
	return 1;
bad_track_mode_found:;
	sprintf(msg, "Unsuitable track mode 0x%x in track %d of session %d",
		disc->session[i]->track[t]->mode, i+1, t+1);
	if (!(flag & 2))
		libdax_msgs_submit(libdax_messenger, -1, 0x0002010a,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
	return 0;
}


/* ts A61218 : outsourced from burn_disc_write_sync() */
int burn_disc_init_write_status(struct burn_write_opts *o,
				struct burn_disc *disc)
{
	struct burn_drive *d = o->drive;
	struct burn_track *t = NULL;
	int sx, tx;

	d->cancel = 0;

#ifdef Libburn_reset_progress_asynC
	/* <<< This is now done in async.c */
	/* init progress before showing the state */
	d->progress.session = 0;
	d->progress.sessions = disc->sessions;
	d->progress.track = 0;
	d->progress.tracks = disc->session[0]->tracks;
	/* TODO: handle indices */
	d->progress.index = 0;
	d->progress.indices = disc->session[0]->track[0]->indices;
	/* TODO: handle multissession discs */
	/* XXX: sectors are only set during write track */
	d->progress.start_sector = 0;
	d->progress.sectors = 0;
	d->progress.sector = 0;
	d->progress.track = 0;
#endif /* Libburn_reset_progress_asynC */

	/* ts A61023 */
	d->progress.buffer_capacity = 0;
	d->progress.buffer_available = 0;
	d->progress.buffered_bytes = 0;
	d->progress.buffer_min_fill = 0xffffffff;

	/* ts A70711 */
	d->pessimistic_buffer_free = 0;
	d->pbf_altered = 0;
	d->pessimistic_writes = 0;
	d->waited_writes = 0;
	d->waited_tries = 0;
	d->waited_usec = 0;

	/* Set eventual media fill up for last track only */
	for (sx = 0; sx < disc->sessions; sx++)
		for (tx = 0 ; tx < disc->session[sx]->tracks; tx++) {
			t = disc->session[sx]->track[tx];
			burn_track_set_fillup(t, 0);
		}
	if (o->fill_up_media && t != NULL)
		burn_track_set_fillup(t, 1);

	d->busy = BURN_DRIVE_WRITING;

	return 1;
}


static int precheck_write_is_audio(struct burn_disc *disc, int flag)
{
	struct burn_session **sessions;
	int num_sessions, i, j;

	sessions = burn_disc_get_sessions(disc, &num_sessions);
	for (i = 0; i < num_sessions; i++)
		for (j = 0; j < sessions[i]->tracks; j++)
			if (!(sessions[i]->track[j]->mode & BURN_AUDIO))
				return 0;
	return 1;
}


static int precheck_disc_has_cdtext(struct burn_disc *disc, int flag)
{
	struct burn_session **sessions;
	int num_sessions, i, ret;

	sessions = burn_disc_get_sessions(disc, &num_sessions);
	for (i = 0; i < num_sessions; i++) {
		ret = burn_cdtext_from_session(sessions[i], NULL, NULL, 1);
		if (ret > 0)
			return 1;
	}
	return 0;
}


/* ts A70219 : API */
int burn_precheck_write(struct burn_write_opts *o, struct burn_disc *disc,
				 char reasons[BURN_REASONS_LEN], int silent)
{
	enum burn_write_types wt;
	struct burn_drive *d = o->drive;
	char *msg = NULL, *reason_pt;
	int no_media = 0, ret, has_cdtext;

	reason_pt= reasons;
	reasons[0] = 0;

	if (d->drive_role == 0 || d->drive_role == 4) {
		if (d->drive_role == 0)
			sprintf(reasons,
			       "DRIVE: is a virtual placeholder (null-drive)");
		else
			sprintf(reasons, "DRIVE: read-only pseudo drive");
		no_media = 1;
		goto ex;
	}

	/* check write mode against write job */
	wt = burn_write_opts_auto_write_type(o, disc, reasons, 1);
	if (wt == BURN_WRITE_NONE) {
		if (strncmp(reasons, "MEDIA: ", 7)==0)
			no_media = 1;
		goto ex;
	}

	sprintf(reasons, "%s: ", d->current_profile_text);
	reason_pt= reasons + strlen(reasons);
	if (d->status == BURN_DISC_UNSUITABLE)
		goto unsuitable_profile;
	if (o->num_text_packs > 0) {
		has_cdtext = 1;
	} else {
		has_cdtext = precheck_disc_has_cdtext(disc, 0);
	}
	if (has_cdtext > 0) {
		if (d->current_profile == 0x09 || d->current_profile == 0x0a) {
			ret = precheck_write_is_audio(disc, 0);
			if (ret <= 0)
				strcat(reasons,
			"CD-TEXT supported only with pure audio CD media, ");
		} else {
			strcat(reasons,
				"CD-TEXT supported only with CD media, ");
		}
	}
	if (d->drive_role == 2 || d->drive_role == 5 ||
		d->current_profile == 0x1a || d->current_profile == 0x12 ||
		d->current_profile == 0x43) { 
		/* DVD+RW , DVD-RAM , BD-RE, emulated drive on stdio file */
		if (o->start_byte >= 0 && (o->start_byte % 2048))
			strcat(reasons,
			 "write start address not properly aligned to 2048, ");
	} else if (d->current_profile == 0x09 || d->current_profile == 0x0a) {
		/* CD-R , CD-RW */
		if (!burn_disc_write_is_ok(o, disc, (!!silent) << 1))
			strcat(reasons, "unsuitable track mode found, ");
		if (o->start_byte >= 0)
			strcat(reasons, "write start address not supported, ");
		if (o->num_text_packs > 0) {
			if (o->write_type != BURN_WRITE_SAO)
				strcat(reasons,
				"CD-TEXT supported only with write type SAO, ");
			if (d->start_lba == -2000000000)
				strcat(reasons,
				"No Lead-in start address known with CD-TEXT, ");
		}
	} else if (d->current_profile == 0x13) {
		/* DVD-RW Restricted Overwrite */
		if (o->start_byte >= 0 && (o->start_byte % 32768))
			strcat(reasons,
			  "write start address not properly aligned to 32k, ");
	} else if (d->drive_role == 3 ||
		   d->current_profile == 0x11 || d->current_profile == 0x14 ||
	           d->current_profile == 0x15 ||
	           d->current_profile == 0x1b || d->current_profile == 0x2b ||
		   d->current_profile == 0x41) {
		/* DVD-R* Sequential , DVD+R[/DL] , BD-R,
		   sequential stdio "drive" */
		if (o->start_byte >= 0)
			strcat(reasons, "write start address not supported, ");
	} else {
unsuitable_profile:;
		msg = calloc(1, 160);
		if (msg != NULL && !silent) {
			sprintf(msg,
			    "Unsuitable media detected. Profile %4.4Xh  %s",
			    d->current_profile, d->current_profile_text);
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x0002011e,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
		}
		if (msg != NULL)
			free(msg);
		strcat(reasons, "no suitable media profile detected, ");
		return 0;
	}
ex:;
	if (reason_pt[0]) {
		if (no_media) {
			if (!silent)
				libdax_msgs_submit(libdax_messenger,
				  d->global_index, 0x0002013a,
				  LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				  "No suitable media detected", 0, 0);
			return -1;
		}
		if (!silent)
			libdax_msgs_submit(libdax_messenger,
				  d->global_index, 0x00020139,
				  LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				  "Write job parameters are unsuitable", 0, 0);
		return 0;
	}
	return 1;
}


/* ts A70129 : learned much from dvd+rw-tools-7.0/growisofs_mmc.cpp */
int burn_disc_open_track_dvd_minus_r(struct burn_write_opts *o,
					struct burn_session *s, int tnum)
{
	struct burn_drive *d = o->drive;
	char *msg = NULL;
	int ret, lba, nwa;
	off_t size;

	BURN_ALLOC_MEM(msg, char, 160);
	d->send_write_parameters(d, NULL, -1, o);
	ret = d->get_nwa(d, -1, &lba, &nwa);
	sprintf(msg, 
		"DVD pre-track %2.2d : get_nwa(%d), ret= %d , d->nwa= %d",
		tnum+1, nwa, ret, d->nwa);
	libdax_msgs_submit(libdax_messenger, d->global_index, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO, msg,0,0);
	if (nwa > d->nwa)
		d->nwa = nwa;
	/* ts A70214 : eventually adjust already expanded size of track */
	burn_track_apply_fillup(s->track[tnum], d->media_capacity_remaining,1);

#ifdef Libburn_pioneer_dvr_216d_with_opC
	fprintf(stderr, "libburn_DEBUG: Libburn_pioneer_dvr_216d_with_opC : num_opc_tables = %d\n", d->num_opc_tables);
	if (d->num_opc_tables <= 0 && !o->simulate) {
		fprintf(stderr, "libburn_DEBUG: Libburn_pioneer_dvr_216d_with_opC : performing OPC\n");
		d->perform_opc(d);
		fprintf(stderr, "libburn_DEBUG: Libburn_pioneer_dvr_216d_with_opC : done\n");
	}
#endif

#ifdef Libburn_pioneer_dvr_216d_get_evenT
	mmc_get_event(d);
#endif

	if (o->write_type == BURN_WRITE_SAO) { /* DAO */
		size = ((off_t) burn_track_get_sectors_2(s->track[tnum], 1))
			* (off_t) 2048;

 		/* Eventually round track size up to write chunk */
		if (o->obs_pad && (size % o->obs))
			size += (off_t) (o->obs - (size % o->obs));

		ret = d->reserve_track(d, size);
		if (ret <= 0) {
			sprintf(msg, "Cannot reserve track of %.f bytes",
				(double) size);
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020138,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
			{ret = 0; goto ex;}
		}
	}
	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	return ret;
}


/* ts A70226 */
int burn_disc_open_track_dvd_plus_r(struct burn_write_opts *o,
					struct burn_session *s, int tnum)
{
	struct burn_drive *d = o->drive;
	char *msg = NULL;
	int ret, lba, nwa;
	off_t size;

	BURN_ALLOC_MEM(msg, char, 160);
	ret = d->get_nwa(d, -1, &lba, &nwa);
	sprintf(msg, 
		"DVD+R pre-track %2.2d : get_nwa(%d), ret= %d , d->nwa= %d",
		tnum+1, nwa, ret, d->nwa);
	libdax_msgs_submit(libdax_messenger, d->global_index, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO, msg,0,0);
	if (nwa > d->nwa)
		d->nwa = nwa;
	/* ts A70214 : eventually adjust already expanded size of track */
	burn_track_apply_fillup(s->track[tnum], d->media_capacity_remaining,1);

	if (o->write_type == BURN_WRITE_SAO &&
	    ! burn_track_is_open_ended(s->track[tnum])) {
 		/* Reserve track */
		size = ((off_t) burn_track_get_sectors_2(s->track[tnum], 1))
			* (off_t) 2048;
		if (o->obs_pad) {
	 		/* Round track size up to write chunk size */
			/* o->obs should be 32k or 64k already. But 32k
			   alignment was once performed in d->reserve_track()*/
			if (o->obs % 32768)
				o->obs += 32768 - (o->obs % 32768);
			if (size % o->obs)
				size += (off_t) (o->obs - (size % o->obs));
		}

		/* <<< Only for now until the first DVD+R succeeded */
		if (!o->obs_pad) {
			sprintf(msg, "Program error: encountered DVD+R without chunk padding");
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00000004,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
			{ret = 0; goto ex;}
		}

		ret = d->reserve_track(d, size);
		if (ret <= 0) {
			sprintf(msg, "Cannot reserve track of %.f bytes",
				(double) size);
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020138,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
			{ret = 0; goto ex;}
		}
	}
	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	return ret;
}


/* ts A70129 */
int burn_disc_close_track_dvd_minus_r(struct burn_write_opts *o, int tnum)
{
	struct burn_drive *d = o->drive;
	char msg[80];

	/* only with Incremental writing */
	if (o->write_type != BURN_WRITE_TAO)
		return 2;

	sprintf(msg, "Closing track %2.2d  (absolute track number %d)",
		tnum + 1, d->last_track_no);
	libdax_msgs_submit(libdax_messenger, o->drive->global_index,0x00020119,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH, msg,0,0);

	d->busy = BURN_DRIVE_CLOSING_SESSION;
	/* Ignoring tnum here and hoping that d->last_track_no is correct */
	d->close_track_session(d, 0, d->last_track_no); /* CLOSE TRACK, 001b */
	d->busy = BURN_DRIVE_WRITING;
	d->last_track_no++;
	return 1;
}


/* ts A70229 */
int burn_disc_finalize_dvd_plus_r(struct burn_write_opts *o)
{
	struct burn_drive *d = o->drive;
	char msg[80];

	sprintf(msg, "Finalizing %s ...",
		d->current_profile_text);
	libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
			msg, 0, 0);

	if(d->current_profile == 0x41) { /* BD-R */
		/* CLOSE SESSION, 110b, Finalize Disc */
		d->close_track_session(d, 3, 0);  /* (3<<1)|0 = 6 */
	} else {
		/* CLOSE SESSION, 101b, Finalize with minimal radius */
		d->close_track_session(d, 2, 1);  /* (2<<1)|1 = 5 */
	}

	sprintf(msg, "... finalizing %s done               ",
		d->current_profile_text);
	libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
			msg, 0, 0);

	return 1;
}


/* ts A70226 */
int burn_disc_close_track_dvd_plus_r(struct burn_write_opts *o,
			int tnum, int is_last_track)
{
	struct burn_drive *d = o->drive;
	char msg[80];

	sprintf(msg,
		"Closing track %2.2d  (absolute track and session number %d)",
		tnum + 1, d->last_track_no);
	libdax_msgs_submit(libdax_messenger, o->drive->global_index,0x00020119,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH, msg,0,0);

	d->busy = BURN_DRIVE_CLOSING_SESSION;
	d->close_track_session(d, 0, d->last_track_no); /* CLOSE TRACK, 001b */

	/* Each session becomes a single logical track. So to distinguish them,
	   it is mandatory to close the session together with each track. */

	if (is_last_track && !o->multi) 
		burn_disc_finalize_dvd_plus_r(o);
	else
 		d->close_track_session(d, 1, 0); /* CLOSE SESSION, 010b */
	d->busy = BURN_DRIVE_WRITING;
	d->last_track_no++;
	return 1;
}


/* <<<
#define Libburn_simplified_dvd_chunk_transactioN 1
*/

#ifdef Libburn_simplified_dvd_chunk_transactioN

/* ts A91114 : EXPERIMENTAL, NOT COMPLETELY IMPLEMENTED

   Simplified data transmission for DVD. libburn via GNU/Linux USB is 30 %
   slower than growisofs or cdrecord when transmitting 32 KB chunks.
   With 64 KB chunks it is 20% faster than the competitors.
   No heavy CPU load is visible but there might be subtle race conditions in
   the USB driver which work better with shorter time gaps between WRITE
   commands.

   Insight: It is actually about the interference of track source reading
            with SCSI writing via USB. growisofs reads with O_DIRECT into a
            mmap()ed buffer. When doing the same, libburn with 32 KB chunks
            reaches similar write speed.
            On the other hand, 64 KB chunks are 20% faster than that and
            are not improved by reading O_DIRECT.

            O_DIRECT is a property of the input fd of struct burn_source.
            It can only be done with properly aligned memory and with aligned
            read size. Alignment size is file system system specific.
            System call
                mmap(NULL, (size_t) buffer_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, -1, (off_t) 0);
            is supposed to allocate a properly aligned buffer.
            64 KB is supposed to be a safe size.
            Actually mmap() seems to be the main cause for a positive effect
            of O_DIRECT.

   This simplified transmission function did not bring visible benefit.
   So for now it is not worth to teach it all applicable details of old
   CD sector oriented transmission.

   @return 1= ok, go on , 2= no input with track->open_ended = nothing written
           <= 0 = error
*/
static int transact_dvd_chunk(struct burn_write_opts *opts,
				 struct burn_track *track)
{
	int curr = 0, valid, err;
	struct burn_drive *d = opts->drive;
        struct buffer *out = d->buffer;
	unsigned char *data = out->data;

#ifdef Libburn_log_in_and_out_streaM
	/* ts A61031 */
	static int tee_fd= -1;
	if(tee_fd==-1)
		tee_fd= open("/tmp/libburn_sg_readin",
				O_WRONLY|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR);
#endif /* Libburn_log_in_and_out_streaM */


	/* Read a chunk full of data */

	/* ??? Do we have offset padding ? >>> First produce offset padding */;

	/*  <<<< */
	if (0 && !track->eos) {
		for (curr = 0; curr < opts->obs; curr += 2048) {
			if (track->source->read != NULL)
				valid = track->source->read(track->source,
						data + curr, 2048);
			else
				valid = track->source->read_xt(track->source,
						data + curr, 2048);
			if (valid <= 0) {
				track->eos = 1;
		break;
			}
			track->sourcecount += valid;

#ifdef Libburn_log_in_and_out_streaM
			if(tee_fd!=-1 && valid>0) {
				write(tee_fd, data + curr, valid);
			}
#endif /* Libburn_log_in_and_out_streaM */

		}
	} else if (!track->eos){
		valid = track->source->read(track->source, data, opts->obs);
		if (valid <= 0) {
			track->eos = 1;
		} else {
			track->sourcecount += valid;
			curr = valid;

#ifdef Libburn_log_in_and_out_streaM
			if(tee_fd!=-1 && valid>0) {
				write(tee_fd, data, valid);
			}
#endif /* Libburn_log_in_and_out_streaM */
		}
	}
	if (curr == 0 && track->open_ended) {

		/* >>> allow tail padding */;

		return 2;
	}
	if (curr < opts->obs)
		memset(data + curr , 0, opts->obs - curr);

	/* Write chunk */
	out->bytes = opts->obs;
	out->sectors = out->bytes / 2048;
	err = d->write(d, d->nwa, out);
	if (err == BE_CANCELLED)
		return 0;
	track->writecount += out->bytes;
	track->written_sectors += out->sectors;
	d->progress.buffered_bytes += out->bytes;
	d->nwa += out->sectors;
	out->bytes = 0;
	out->sectors = 0;

	return 1;
}

#endif /* Libburn_simplified_dvd_chunk_transactioN */


/* ts A61218 - A81208 */
int burn_dvd_write_track(struct burn_write_opts *o,
			struct burn_session *s, int tnum, int is_last_track)
{
	struct burn_track *t = s->track[tnum];
	struct burn_drive *d = o->drive;
	struct buffer *out = d->buffer;
	int sectors;
	int i, open_ended = 0, ret= 0, is_flushed = 0, track_open = 0;
	int first_buf_cap = 0, further_cap = 0, buf_cap_step = 1024;

	/* ts A70213 : eventually expand size of track to max */
	burn_track_apply_fillup(t, d->media_capacity_remaining, 0);

	if (d->current_profile == 0x11 || d->current_profile == 0x14 ||
	    d->current_profile == 0x15) {
		/* DVD-R, DVD-RW Sequential, DVD-R/DL Sequential */
		ret = burn_disc_open_track_dvd_minus_r(o, s, tnum);
		if (ret <= 0)
			goto ex;
		/* Pioneer DVR-216D rev 1.09 hates multiple buffer inquiries
		   before the drive buffer is full.
		*/
		first_buf_cap = 0;
		further_cap = -1;
	} else if (d->current_profile == 0x1b || d->current_profile == 0x2b) {
		/* DVD+R , DVD+R/DL */
		ret = burn_disc_open_track_dvd_plus_r(o, s, tnum);
		if (ret <= 0)
			goto ex;
	} else if (d->current_profile == 0x41) {
		/* BD-R SRM */
		ret = burn_disc_open_track_dvd_plus_r(o, s, tnum);
		if (ret <= 0)
			goto ex;
	}
	track_open = 1;

	sectors = burn_track_get_sectors_2(t, 1);
	open_ended = burn_track_is_open_ended(t);

	/* (offset padding is done within sector_data()) */

	burn_disc_init_track_status(o, s, t, tnum, sectors);
	for (i = 0; open_ended || i < sectors; i++) {

		/* From time to time inquire drive buffer */
		/* ts A91110: Eventually avoid to do this more than once
		              before the drive buffer is full. See above DVD-
		*/
		if (i == first_buf_cap ||
		   ((i % buf_cap_step) == 0 &&
		    (i >= further_cap || further_cap < 0))) {
			d->read_buffer_capacity(d);
			if (further_cap < 0)
				further_cap =
			 	    d->progress.buffer_capacity / 2048 + 128;
		}

#ifdef Libburn_simplified_dvd_chunk_transactioN

		ret = transact_dvd_chunk(o, t);
		if (ret <= 0)
			{ret = 0; goto ex;}
		i += o->obs / 2048 - 1;
		d->progress.sector += o->obs / 2048 - 1;
#else
		/* transact a (CD sized) sector */
		if (!sector_data(o, t, 0))
			{ ret = 0; goto ex; }
#endif

		if (open_ended) {
			d->progress.sectors = sectors = i;
                        if (burn_track_is_data_done(t)) 
	break;
		}

		/* update current progress */
		d->progress.sector++;
	}
	
	/* (tail padding is done in sector_data()) */

	/* Pad up buffer to next full o->obs (usually 32 kB) */
	if (o->obs_pad && out->bytes > 0 && out->bytes < o->obs) {
		memset(out->data + out->bytes, 0, o->obs - out->bytes);
		out->sectors += (o->obs - out->bytes) / 2048;
		out->bytes = o->obs;
	}
	ret = burn_write_flush(o, t);
	if (ret <= 0)
		goto ex;
	is_flushed = 1;

	/* Eventually finalize track */
	if (d->current_profile == 0x11 || d->current_profile == 0x14 ||
	    d->current_profile == 0x15) {
		/* DVD-R, DVD-RW Sequential, DVD-R/DL Sequential */
		ret = burn_disc_close_track_dvd_minus_r(o, tnum);
		if (ret <= 0)
			goto ex;
	} else if (d->current_profile == 0x1b || d->current_profile == 0x2b) {
		/* DVD+R , DVD+R/DL */
		ret = burn_disc_close_track_dvd_plus_r(o, tnum,
							 is_last_track);
		if (ret <= 0)
			goto ex;
	} else if (d->current_profile == 0x41) {
		/* BD-R SRM */
		ret = burn_disc_close_track_dvd_plus_r(o, tnum,
							 is_last_track);
		if (ret <= 0)
			goto ex;
	}
	ret = 1;
ex:;
	if (d->cancel)
		burn_source_cancel(t->source);
	if (track_open && !is_flushed)
		d->sync_cache(d); /* burn_write_flush() was not called */
	return ret;
}


/* ts A61219 */
int burn_disc_close_session_dvd_plus_rw(struct burn_write_opts *o,
					struct burn_session *s)
{
	struct burn_drive *d = o->drive;

	d->busy = BURN_DRIVE_CLOSING_SESSION;
	/* This seems to be a quick end : "if (!dvd_compat)" */
	/* >>> Stop de-icing (ongoing background format) quickly
	       by mmc_close() (but with opcode[2]=0).
	       Wait for unit to get ready.
	       return 1;
	*/
	/* Else: end eventual background format in a "DVD-RO" compatible way */
	d->close_track_session(d, 1, 0); /* same as CLOSE SESSION for CD */
	d->busy = BURN_DRIVE_WRITING;
	return 1;
}


/* ts A61228 */
int burn_disc_close_session_dvd_minus_rw(struct burn_write_opts *o,
					struct burn_session *s)
{
	struct burn_drive *d = o->drive;

	d->busy = BURN_DRIVE_CLOSING_SESSION;
	if (d->current_profile == 0x13) {
		d->close_track_session(d, 1, 0); /* CLOSE SESSION, 010b */

		/* ??? under what circumstances to use close functiom 011b 
		       "Finalize disc" ? */

	}
	d->busy = BURN_DRIVE_WRITING;
	return 1;
}


/* ts A70129 : for profile 0x11 DVD-R, 0x14 DVD-RW Seq, 0x15 DVD-R/DL Seq */
int burn_disc_close_session_dvd_minus_r(struct burn_write_opts *o)
{
	struct burn_drive *d = o->drive;

	/* only for Incremental writing */
	if (o->write_type != BURN_WRITE_TAO)
		return 2;

#ifdef Libburn_dvd_r_dl_multi_no_close_sessioN
	if (d->current_profile == 0x15 && o->multi)
		return 2;
#endif

	libdax_msgs_submit(libdax_messenger, o->drive->global_index,0x00020119,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			"Closing session", 0, 0);

	d->busy = BURN_DRIVE_CLOSING_SESSION;
	d->close_track_session(d, 1, 0); /* CLOSE SESSION, 010b */
	d->busy = BURN_DRIVE_WRITING;
	return 1;
}


/* ts A61218 */
int burn_dvd_write_session(struct burn_write_opts *o,
				struct burn_session *s, int is_last_session)
{
	int i, ret, multi_mem;
        struct burn_drive *d = o->drive;

	/* ts A90108 */
	if (d->current_profile == 0x41 && d->status == BURN_DISC_APPENDABLE &&
	    d->state_of_last_session == 1) {
		/* last session on BD-R is still open */;

		/* BR-R were not closed by libburn-0.6.0.pl00 if o->multi==0.
		   This leads to an unreadable, but recoverable) media state.
		   Technically they are appendable although the last session
		   is not readable.

		   By default the open session gets closed here before the new
		   session is written. E.g. after writing a small dummy seesion
		   number 2 one can read session 1 and write session 3 which
		   points to data of session 1.

		   For the case that no media with 3 sessions is desired it is
		   possible to activate the following coarse single-session
		   closing code:
		   No new session will be written but calling programs will
		   report success. Quite misleading.
		   Activate only if really needed by
		   # define Libburn_bug_A90108_close_disC yes
		*/


#ifdef Libburn_bug_A90108_close_disC

		/* Close open session and media. 
		   That was the goal of the failed run which led to the
		   unreadable (but recoverable) media state.

		   It is not easy to implement a general close function for
		   all media types. Therefore this pseudo write code is under
		   control of #ifdef.
		*/
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020171,
				LIBDAX_MSGS_SEV_NOTE, LIBDAX_MSGS_PRIO_HIGH,
				"Closing BD-R with accidently open session",
				0, 0);
 		d->close_track_session(d, 3, 0); /* CLOSE SESSION, 110b */
		d->state_of_last_session = 3; /* mark as complete session */
		d->status = BURN_DISC_FULL;
		sleep(3); /* The caller might need time to arrange itself */
		return 1;

#else /* Libburn_bug_A90108_close_disC */

		/* This is the default mode.
		*/
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020170,
				LIBDAX_MSGS_SEV_NOTE, LIBDAX_MSGS_PRIO_HIGH,
				"Closing open session before writing new one",
				0, 0);
 		d->close_track_session(d, 1, 0); /* CLOSE SESSION, 010b */
		d->state_of_last_session = 3; /* mark as complete session */

#endif /* ! Libburn_bug_A90108_close_disC */

	}

	for (i = 0; i < s->tracks; i++) {
		ret = burn_dvd_write_track(o, s, i,
			is_last_session && i == (s->tracks - 1));
		if (ret <= 0)
	break;
	}
	if (d->current_profile == 0x11 || d->current_profile == 0x14 ||
	    d->current_profile == 0x15) {
		/* DVD-R , DVD-RW Sequential, DVD-R/DL Sequential */
		multi_mem = o->multi;
		if (!is_last_session)
			o->multi = 1;
		ret = burn_disc_close_session_dvd_minus_r(o);
		o->multi = multi_mem;
		if (ret <= 0)
			return 0;
	} else if (d->current_profile == 0x12 || d->current_profile == 0x43) {
		/* DVD-RAM , BD-RE */
		/* ??? any finalization needed ? */;
	} else if (d->current_profile == 0x13) {
		/* DVD-RW restricted overwrite */
		if (d->needs_close_session) {
			ret = burn_disc_close_session_dvd_minus_rw(o, s);
			if (ret <= 0)
				return 0;
		}
	} else if (d->current_profile == 0x1a) {
		/* DVD+RW */
		if (d->needs_close_session) {
			ret = burn_disc_close_session_dvd_plus_rw(o, s);
			if (ret <= 0)
				return 0;
		}
	} else if (d->current_profile == 0x1b || d->current_profile == 0x2b) {
		/* DVD+R , DVD+R/DL do each track as an own session */;
	} else if (d->current_profile == 0x41) {
		/* BD-R SRM do each track as an own session */;
	}
	return 1;
}


/* ts A61218 : learned much from dvd+rw-tools-7.0/growisofs_mmc.cpp */
int burn_disc_setup_dvd_plus_rw(struct burn_write_opts *o,
				struct burn_disc *disc)
{
	struct burn_drive *d = o->drive;
	int ret;

	if (d->bg_format_status==0 || d->bg_format_status==1) {
		d->busy = BURN_DRIVE_FORMATTING;
		/* start or re-start dvd_plus_rw formatting */
		ret = d->format_unit(d, (off_t) 0, 0);
		if (ret <= 0)
			return 0;
		d->busy = BURN_DRIVE_WRITING;
		d->needs_close_session = 1;
	}

	/* >>> perform OPC if needed */;

	/* >>> ? what else ? */;

	return 1;
}


/* ts A61228 : learned much from dvd+rw-tools-7.0/growisofs_mmc.cpp */
int burn_disc_setup_dvd_minus_rw(struct burn_write_opts *o,
				struct burn_disc *disc)
{
	struct burn_drive *d = o->drive;
	char msg[60];
	int ret;

	d->nwa = 0;
	if (o->start_byte >= 0) {
		d->nwa = o->start_byte / 32768; /* align to 32 kB */

		sprintf(msg, "Write start address is  %d * 32768", d->nwa);
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020127,
				LIBDAX_MSGS_SEV_NOTE, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);

		d->nwa *= 16; /* convert to 2048 block units */
	}


	/* ??? mmc5r03c.pdf 7.5.2 :
	"For DVD-RW media ... If a medium is in Restricted overwrite
	 mode, this mode page shall not be used."

	But growisofs composes a page 5 and sends it.
	mmc5r03c.pdf 5.3.16 , table 127 specifies that mode page 5
	shall be supported with feature 0026h Restricted Overwrite.
	5.3.22 describes a feature 002Ch Rigid Restrictive Overwrite
	which seems to apply to DVD-RW and does not mention page 5.

	5.4.14 finally states that profile 0013h includes feature
	002Ch rather than 0026h.
		
		d->send_write_parameters(d, NULL, -1, o);
	*/

	d->busy = BURN_DRIVE_FORMATTING;

	/* "quick grow" to at least byte equivalent of d->nwa */
	ret = d->format_unit(d, (off_t) d->nwa * (off_t) 2048,
			     (d->nwa > 0) << 3);
	if (ret <= 0)
		return 0;

	d->busy = BURN_DRIVE_WRITING;

	/* >>> perform OPC if needed */;

	return 1;
}


/* ts A70129 : for DVD-R[W] Sequential Recoding */
int burn_disc_setup_dvd_minus_r(struct burn_write_opts *o,
				struct burn_disc *disc)
{
	struct burn_drive *d = o->drive;

	/* most setup is in burn_disc_setup_track_dvd_minus_r() */;

	d->nwa = 0;
	return 1;
}


/* ts A70226 : for DVD+R , DVD+R/DL */
int burn_disc_setup_dvd_plus_r(struct burn_write_opts *o,
				struct burn_disc *disc)
{
	struct burn_drive *d = o->drive;

	/* most setup is in burn_disc_setup_track_dvd_plus_r() */;

	d->nwa = 0;
	return 1;
}


/* ts A61218 - A70415 */
int burn_dvd_write_sync(struct burn_write_opts *o,
				 struct burn_disc *disc)
{
	int i, ret, o_end;
	off_t default_size = 0;
	struct burn_drive *d = o->drive;
	struct burn_track *t;
	char *msg = NULL;

	BURN_ALLOC_MEM(msg, char, 160);
	d->needs_close_session = 0;

	/* buffer flush trigger for sector.c:get_sector() */
	o->obs = Libburn_dvd_obS;

	if (d->current_profile == 0x1a || d->current_profile == 0x12 ||
	    d->current_profile == 0x43) { 
		/* DVD+RW , DVD-RAM , BD-RE */
		ret = 1;
		if (d->current_profile == 0x1a)
			ret = burn_disc_setup_dvd_plus_rw(o, disc);
		if (ret <= 0) {
			sprintf(msg,
			  "Write preparation setup failed for DVD+RW");
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020121,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
			goto early_failure;
		}
		d->nwa = 0;
		if (o->start_byte >= 0) {
			d->nwa = o->start_byte / 2048;
			sprintf(msg, "Write start address is  %d * 2048",
				d->nwa);
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020127,
				LIBDAX_MSGS_SEV_NOTE, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
		}
		if (o->obs_pad < 2)
			o->obs_pad = 0; /* no filling-up of last 32k buffer */
		if (d->current_profile == 0x43) /* BD-RE */
			o->obs = Libburn_bd_re_obS;
		if (d->do_stream_recording) {
			if (o->obs_pad < 2)
				o->obs_pad = 1;
			if (d->current_profile == 0x43) /* BD-RE */
				o->obs = Libburn_bd_re_streamed_obS;
		}

	} else if (d->current_profile == 0x13) {
		 /* DVD-RW Restricted Overwrite */
		ret = burn_disc_setup_dvd_minus_rw(o, disc);
		if (ret <= 0) {
			sprintf(msg,
			  "Write preparation setup failed for DVD-RW");
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020121,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
			goto early_failure;
		}

		/* _Rigid_ Restricted Overwrite demands this */
		o->obs_pad = 1; /* fill-up track's last 32k buffer */

	} else if (d->current_profile == 0x11 || d->current_profile == 0x14 ||
			d->current_profile == 0x15) {
		/* DVD-R , DVD-RW Sequential , DVD-R/DL Sequential */
		t = disc->session[0]->track[0];
		o_end = ( burn_track_is_open_ended(t) && !o->fill_up_media );
		default_size = burn_track_get_default_size(t);
		if (o->write_type == BURN_WRITE_SAO && o_end) {
			sprintf(msg, "Activated track default size %.f",
				(double) default_size);
			libdax_msgs_submit(libdax_messenger,
				  d->global_index, 0x0002012e,
				  LIBDAX_MSGS_SEV_NOTE, LIBDAX_MSGS_PRIO_HIGH,
				  msg, 0, 0);
			burn_track_set_size(t, default_size);
		}
		/* Whether to fill-up last 32k buffer of track. */
		if (o->obs_pad < 2)
			o->obs_pad = (o->write_type != BURN_WRITE_SAO);
		ret = burn_disc_setup_dvd_minus_r(o, disc);
		if (ret <= 0) {
			sprintf(msg,
			  "Write preparation setup failed for DVD-R[W]");
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020121,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
			goto early_failure;
		}
		
	} else if (d->current_profile == 0x1b || d->current_profile == 0x2b ||
		   d->current_profile == 0x41) {
		/* DVD+R , DVD+R/DL , BD-R SRM */

		/* >>> ts A81208 : with BD-R set o->obs to 64 kB ? */

		t = disc->session[0]->track[0];
		o_end = ( burn_track_is_open_ended(t) && !o->fill_up_media );
		default_size = burn_track_get_default_size(t);
		if (o->write_type == BURN_WRITE_SAO && o_end) {
			sprintf(msg, "Activated track default size %.f",
				(double) default_size);
			libdax_msgs_submit(libdax_messenger,
				  d->global_index, 0x0002012e,
				  LIBDAX_MSGS_SEV_NOTE, LIBDAX_MSGS_PRIO_HIGH,
				  msg, 0, 0);
			burn_track_set_size(t, default_size);
		}
		ret = burn_disc_setup_dvd_plus_r(o, disc);
		if (ret <= 0) {
			sprintf(msg, "Write preparation setup failed for %s",
			   	d->current_profile == 0x41 ? "BD-R" : "DVD+R");
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020121,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
			goto early_failure;
		}
		/* ??? padding needed ??? cowardly doing it for now */
		if (o->obs_pad < 2)
			o->obs_pad = 1; /* fill-up track's last 32k buffer */
		if (d->do_stream_recording) {
			if (d->current_profile == 0x41) /* BD-R */
				o->obs = Libburn_bd_re_streamed_obS;
		}
	}

#ifdef Libburn_dvd_obs_default_64K
	o->obs = 64 * 1024;
#endif

	/* <<< test only : Does this increase effective speed with USB ?
		ts A90801 : 64kB: speed with 16x DVD-R is 12 rather than 8
		            128kB: glibc complains about double free
		                   With BURN_OS_TRANSPORT_BUFFER_SIZE
		                   enlarged to 128 MB, the first WRITE fails
		                   with an i/o error.
	o->obs = 64 * 1024;
	*/

	if (o->dvd_obs_override >= 32 * 1024)
		o->obs = o->dvd_obs_override;

	if (o->obs > BUFFER_SIZE) {
		sprintf(msg, "Chosen write chunk size %d exceeds system dependent buffer size", o->obs);
		libdax_msgs_submit(libdax_messenger, d->global_index,
				 0x00000002, LIBDAX_MSGS_SEV_DEBUG,
				 LIBDAX_MSGS_PRIO_ZERO, msg, 0, 0);
		o->obs = 32 * 1024; /* This size is required to work */
	}

	if (d->do_stream_recording &&
		(d->current_profile == 0x43 || d->current_profile == 0x41) &&
		o->obs < Libburn_bd_re_streamed_obS) {
		/* LG GGW-H20 writes junk with stream recording and obs=32k */
		sprintf(msg,
		    "Stream recording disabled because of small OS buffer");
		libdax_msgs_submit(libdax_messenger, d->global_index,
			 0x00020176, LIBDAX_MSGS_SEV_NOTE,
			 LIBDAX_MSGS_PRIO_HIGH, msg, 0, 0);
		d->do_stream_recording = 0;
	}

	sprintf(msg, "dvd/bd Profile= %2.2Xh , obs= %d , obs_pad= %d",
		d->current_profile, o->obs, o->obs_pad);
	libdax_msgs_submit(libdax_messenger, d->global_index, 0x00000002,
		LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO, msg, 0, 0);

	for (i = 0; i < disc->sessions; i++) {
		/* update progress */
		d->progress.session = i;
		d->progress.tracks = disc->session[i]->tracks;

		ret = burn_dvd_write_session(o, disc->session[i],
					i == (disc->sessions - 1));
		if (ret <= 0)
			goto ex;

		/* XXX: currently signs an end of session */
		d->progress.sector = 0;
		d->progress.start_sector = 0;
		d->progress.sectors = 0;
	}
	ret = 1;
ex:;

	/* >>> eventual emergency finalization measures */

	/* update media state records */
	burn_drive_mark_unready(d, 0);
	burn_drive_inquire_media(d);

	if (d->current_profile == 0x41 && d->complete_sessions >= 300) {
		sprintf(msg, "Sequential BD-R media now contains %d sessions. It is likely to soon fail writing.", d->complete_sessions);
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x0002017b, LIBDAX_MSGS_SEV_WARNING,
				LIBDAX_MSGS_PRIO_ZERO, msg, 0, 0);
	}
	BURN_FREE_MEM(msg);
	return ret;
early_failure:;
	BURN_FREE_MEM(msg);
	return 0;
}


/* ts A70904 */
int burn_stdio_open_write(struct burn_drive *d, off_t start_byte,
			 int sector_size, int flag)
{

/* We normally need _LARGEFILE64_SOURCE defined by the build system.
   Nevertheless the system might use large address integers by default.
*/
#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif

	int fd = -1;
	int mode = O_RDWR | O_CREAT | O_LARGEFILE;
	char msg[60];
	off_t lseek_res;

	if(d->drive_role == 4) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020181,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			"Pseudo-drive is a read-only file. Cannot write.",
			0, 0);
		return 0;
	}
	if (d->drive_role == 5 || d->drive_role == 3)
		mode = O_WRONLY | O_CREAT | O_LARGEFILE;
	if (d->devname[0] == 0) /* null drives should not come here */
		return -1;
	fd = burn_drive__fd_from_special_adr(d->devname);
	if (fd >= 0)
		fd = dup(fd); /* check validity and make closeable */
	else
		fd = open(d->devname, mode,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (fd == -1) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020005,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"Failed to open device (a pseudo-drive)", errno, 0);
		d->cancel = 1;
		return -1;
	} 
	if (start_byte < 0)
		start_byte = 0;
	if (d->drive_role == 2 || d->drive_role == 5) {
		lseek_res = lseek(fd, start_byte, SEEK_SET);
		if (lseek_res == -1) {
			sprintf(msg, "Cannot address start byte %.f",
				(double) start_byte);
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020147,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				msg, errno, 0);
			close(fd);
			d->cancel = 1;
			fd = -1;
		}
	}
	d->nwa = start_byte / sector_size;
	return fd;
}


/* ts A70904 */
int burn_stdio_read_source(struct burn_source *source, char *buf, int bufsize,
			 	struct burn_write_opts *o, int flag)
{
	int count= 0, todo;

	for(todo = bufsize; todo > 0; todo -= count) {
		if(source->read!=NULL)
			count = source->read(source,
			    (unsigned char *) (buf + (bufsize - todo)), todo);
		else
			count = source->read_xt(source,
			    (unsigned char *) (buf + (bufsize - todo)), todo);
		if (count <= 0)
	break;
	}
	return (bufsize - todo);
}


/* ts A70904 */
int burn_stdio_write(int fd, char *buf, int count, struct burn_drive *d, 
			 int flag)
{
	int ret;
	char *msg = NULL;

	if (d->cancel)
		return 0;
/*
fprintf(stderr, "libburn_DEBUG: write(%d, %lX, %d)\n",
                fd, (unsigned long) buf, count);
*/

	ret = write(fd, buf, count);
	if (ret != count) {
		BURN_ALLOC_MEM(msg, char, 160);

		sprintf(msg,
		  "Cannot write desired amount of data. write(2) returned %d.",
		        ret);
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020148,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			msg, errno, 0);
		d->cancel = 1;
		return 0;
	}
ex:;
	BURN_FREE_MEM(msg);
	return ret;
}


/* ts A70910 : to be used as burn_drive.write(), emulating mmc_write() */
int burn_stdio_mmc_write(struct burn_drive *d, int start, struct buffer *buf)
{
	int ret;
	off_t start_byte;

	if (d->cancel)
		return BE_CANCELLED;
	if (d->stdio_fd < 0) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x0002017d,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Invalid file descriptor with stdio pseudo-drive",
			0, 0);
		d->cancel = 1;
		return BE_CANCELLED;
	}
	if (start != d->nwa) {
		char msg[80];

		start_byte = ((off_t) start) * 
				(off_t) (buf->bytes / buf->sectors);
		if (lseek(d->stdio_fd, start_byte, SEEK_SET)==-1) {
			sprintf(msg, "Cannot address start byte %.f",
			 	(double) start_byte);
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020147,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				msg, errno, 0);
			d->cancel = 1;
			return BE_CANCELLED;
		}
		d->nwa = start;
	}
	ret = burn_stdio_write(d->stdio_fd,(char *)buf->data, buf->bytes, d,0);
	if (ret <= 0)
		return BE_CANCELLED;
	d->nwa += buf->sectors;
	return 0;
}


/* ts A70910 : to be used as burn_drive.write(),
               emulating mmc_write() with simulated writing. */
int burn_stdio_mmc_dummy_write(struct burn_drive *d, int start,
							struct buffer *buf)
{
	if (d->cancel)
		return BE_CANCELLED;
	d->nwa = start + buf->sectors;
	return 0;
}


/* ts A70911 */
/* Flush stdio system buffer to physical device.
   @param flag bit0= do not report debug message (intermediate sync)
*/
int burn_stdio_sync_cache(int fd, struct burn_drive *d, int flag)
{
	int ret;
	char *msg = NULL;

	if (fd < 0) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x0002017d,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Invalid file descriptor with stdio pseudo-drive",
			0, 0);
		d->cancel = 1;
		return 0;
	}
	d->needs_sync_cache = 0;
	if (!(flag & 1))
		libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			   LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
			   "syncing cache (stdio fsync)", 0, 0);
	ret = fsync(fd);
	if (ret != 0 && errno == EIO) {
		BURN_ALLOC_MEM(msg, char, 160);

		sprintf(msg,
		  "Cannot write desired amount of data. fsync(2) returned %d.",
		  ret);
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020148,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			msg, errno, 0);
		d->cancel = 1;
		return 0;
	}
	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	return ret;
}


/* ts A70911 : to be used as burn_drive.sync_cache(),
               emulating mmc_sync_cache() */
void burn_stdio_mmc_sync_cache(struct burn_drive *d)
{
	burn_stdio_sync_cache(d->stdio_fd, d, 0);
}


/* ts A70912 */
/* Enforces eventual nominal write speed.
   @param flag bit0= initialize *prev_time */
int burn_stdio_slowdown(struct burn_drive *d, struct timeval *prev_time,
			 int amount, int flag)
{
	struct timeval tnow;
	struct timezone dummy_tz;
	double to_wait;

	if (flag & 1) {
		gettimeofday(prev_time, &dummy_tz);
		return 1;
	}
	if(d->nominal_write_speed <= 0)
		return 2;
	gettimeofday(&tnow, &dummy_tz);
	to_wait = ( ((double) amount) / (double) d->nominal_write_speed ) - 
		(double) ( tnow.tv_sec - prev_time->tv_sec ) -
		(double) ( tnow.tv_usec - prev_time->tv_usec ) / 1.0e6
		- 0.001; /* best would be 1 / kernel granularity HZ */
	if (to_wait >= 0.0001) {
		usleep((int) (to_wait * 1000000.0));
	}
	gettimeofday(prev_time, &dummy_tz);
	return 1;
}


/* ts A70904 */
int burn_stdio_write_track(struct burn_write_opts *o, struct burn_session *s,
				int tnum, int flag)
{
	int open_ended, bufsize = 16 * 2048, ret, sectors;
	struct burn_track *t = s->track[tnum];
	struct burn_drive *d = o->drive;
	char *buf = NULL;
	int i, prev_sync_sector = 0;
	struct buffer *out = d->buffer;
	struct timeval prev_time;

	BURN_ALLOC_MEM(buf, char, bufsize);

	sectors = burn_track_get_sectors_2(t, 1);
	burn_disc_init_track_status(o, s, t, tnum, sectors);
	open_ended = burn_track_is_open_ended(t);

	t->end_on_premature_eoi = (o->write_type == BURN_WRITE_TAO);

	/* attach stdio emulators for mmc_*() functions */
	if (o->simulate)
		d->write = burn_stdio_mmc_dummy_write;
	else
		d->write = burn_stdio_mmc_write;
	d->sync_cache = burn_stdio_mmc_sync_cache;

	burn_stdio_slowdown(d, &prev_time, 0, 1); /* initialize */
	for (i = 0; open_ended || i < sectors; i++) {
		/* transact a (CD sized) sector */
		if (!sector_data(o, t, 0))
			{ret= 0; goto ex;}
		if (open_ended)
			d->progress.sectors = sectors = d->progress.sector;
		if (open_ended || t->end_on_premature_eoi) {
			if (burn_track_is_data_done(t))
	break;
		}
		d->progress.sector++;
		/* Flush to disk from time to time */
		if (d->progress.sector - prev_sync_sector >=
		    o->stdio_fsync_size && o->stdio_fsync_size > 0) {
			prev_sync_sector = d->progress.sector;
			if (!o->simulate)
				burn_stdio_sync_cache(d->stdio_fd, d, 1);
		}
		if ((d->progress.sector % 512) == 0) 
			burn_stdio_slowdown(d, &prev_time, 512 * 2, 0);
	}

	/* Pad up buffer to next full o->obs (usually 32 kB) */
	if (o->obs_pad && out->bytes > 0 && out->bytes < o->obs) {
		memset(out->data + out->bytes, 0, o->obs - out->bytes);
		out->sectors += (o->obs - out->bytes) / 2048;
		out->bytes = o->obs;
	}
	ret = burn_write_flush(o, t);
	ret= 1;
ex:;
	if (d->cancel)
		burn_source_cancel(t->source);
	if (t->end_on_premature_eoi == 2)
		d->cancel = 1;
	BURN_FREE_MEM(buf);
	return ret;
}


/* ts A70904 */
int burn_stdio_write_sync(struct burn_write_opts *o,
				 struct burn_disc *disc)
{
	int ret;
	struct burn_drive *d = o->drive;

	d->needs_close_session = 0;
	if (o->obs_pad < 2)
		o->obs_pad = 0; /* no filling-up of track's last 32k buffer */
	o->obs = 32*1024; /* buffer size */

	if (disc->sessions != 1)
		{ret= 0 ; goto ex;}
	if (disc->session[0]->tracks != 1)
		{ret= 0 ; goto ex;}
	
	/* update progress */
	d->progress.session = 0;
	d->progress.tracks = 1;

	/* >>> adjust sector size (2048) to eventual audio or even raw */

	/* >>> ??? ts B11004 : Why this eagerness to close and open ? */

	/* open target file */
	if (d->stdio_fd >= 0)
		close(d->stdio_fd);
	if (d->drive_role == 5 && d->status == BURN_DISC_APPENDABLE &&
            o->start_byte < 0)
		o->start_byte = d->role_5_nwa * 2048;
	d->stdio_fd = burn_stdio_open_write(d, o->start_byte, 2048, 0);
	if (d->stdio_fd == -1)
		{ret = 0; goto ex;}

	ret = burn_stdio_write_track(o, disc->session[0], 0, 0);
	if (ret <= 0)
		goto ex;

	/* XXX: currently signs an end of session */
	d->progress.sector = 0;
	d->progress.start_sector = 0;
	d->progress.sectors = 0;
	ret = 1;
ex:;

	/* >>> ??? ts B11004 : Why this eagerness to close ? */

	if (d->stdio_fd >= 0)
		close(d->stdio_fd);
	d->stdio_fd = -1;

	/* update media state records */
	burn_drive_mark_unready(d, 8);

	/* <<< d->busy = BURN_DRIVE_IDLE; */
	return ret;
}


void burn_disc_write_sync(struct burn_write_opts *o, struct burn_disc *disc)
{
	struct cue_sheet *sheet;
	struct burn_drive *d = o->drive;
	struct buffer *buffer_mem = o->drive->buffer;
	struct burn_session *s;
	struct burn_track *lt, *t;
	int first = 1, i, ret, lba, nwa = 0, multi_mem;
	off_t default_size;
	char msg[80];


/* ts A60924 : libburn/message.c gets obsoleted
	burn_message_clear_queue();
*/

	/* ts A61224 */
	burn_disc_init_write_status(o, disc); /* must be done very early */

	/* ts A80412 , A90227 */
	d->do_stream_recording = !!o->do_stream_recording;
	if (o->do_stream_recording >= 16)
		d->stream_recording_start = o->do_stream_recording;
	else
		d->stream_recording_start = 0;

	/* ts A91122 : Get buffer suitable for sources made by
	               burn_os_open_track_src() */
	d->buffer = burn_os_alloc_buffer(sizeof(struct buffer), 0);
	if (d->buffer == NULL)
		goto fail_wo_sync;

/* >>> ts A90321

	memset(d->buffer, 0, sizeof(struct buffer));

fprintf(stderr, "libburn_DEBUG: d->buffer = %lX , size = %d\n",
                (unsigned long) d->buffer, (int) sizeof(struct buffer));

calloc() seems not to have the desired effect. valgrind warns:
==18251== Syscall param write(buf) points to uninitialised byte(s)
==18251==    at 0x5071DEB: (within /lib64/libpthread-2.5.so)
==18251==    by 0x4723FA: burn_stdio_write (write.c:1850)
==18251==    by 0x4725DC: burn_stdio_mmc_write (write.c:1894)
==18251==    by 0x483B7A: get_sector (sector.c:229)
==18251==    by 0x484F11: sector_data (sector.c:639)
==18251==    by 0x4729FE: burn_stdio_write_track (write.c:2012)
==18251==    by 0x472CF4: burn_stdio_write_sync (write.c:2072)
==18251==    by 0x472E8D: burn_disc_write_sync (write.c:2125) <<< we are here
==18251==    by 0x460254: write_disc_worker_func (async.c:514)
==18251==    by 0x506B09D: start_thread (in /lib64/libpthread-2.5.so)
==18251==    by 0x55484CC: clone (in /lib64/libc-2.5.so)
*/

	d->rlba = -150;
	d->toc_temp = 9;

	if(d->drive_role == 4) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020181,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			"Pseudo-drive is a read-only file. Cannot write.",
			0, 0);
		goto fail_wo_sync;
	}
	/* ts A70904 */
	if (d->drive_role != 1) {
		ret = burn_stdio_write_sync(o, disc);
		if (ret <= 0)
			goto fail_wo_sync;
		goto ex;
	}
	/* ts A61218 */
	if (! d->current_is_cd_profile) {
		ret = burn_dvd_write_sync(o, disc);
		if (ret <= 0)
			goto fail_wo_sync;
		goto ex;
	}

	/* ts A70521 : GNU/Linux 2.4 USB audio fails with 64 kiB */
	/* ts A80414 : might need 64 kiB for BD-RE streaming */
        /* buffer flush trigger for sector.c:get_sector() */
	o->obs = Libburn_cd_obS;

	sprintf(msg, "cd Profile= %2.2Xh , obs= %d , obs_pad= %d",
		d->current_profile, o->obs, o->obs_pad);
	libdax_msgs_submit(libdax_messenger, d->global_index, 0x00000002,
		LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO, msg, 0, 0);

	/* ts A70218 */
	if (o->write_type == BURN_WRITE_SAO) {
		for (i = 0 ; i < disc->session[0]->tracks; i++) {
			t = disc->session[0]->track[i];
			if (burn_track_is_open_ended(t)) {
				default_size = burn_track_get_default_size(t);
				sprintf(msg,
					"Activated track default size %.f",
					(double) default_size);
				libdax_msgs_submit(libdax_messenger,
					d->global_index, 0x0002012e,
					LIBDAX_MSGS_SEV_NOTE,
					LIBDAX_MSGS_PRIO_HIGH, msg, 0, 0);
				burn_track_set_size(t, default_size);
			}
		}
	}

/* Apparently some drives require this command to be sent, and a few drives
return crap.  so we send the command, then ignore the result.
*/
	/* ts A61107 : moved up send_write_parameters because LG GSA-4082B
			 seems to dislike get_nwa() in advance */
	d->alba = d->start_lba; /* ts A61114: this looks senseless */
	d->nwa = d->alba;
	if (o->write_type == BURN_WRITE_TAO) {
		nwa = 0; /* get_nwa() will be called in burn_track() */
	} else {
		if (disc->sessions > 0)
			s = disc->session[0];
		else
			s = NULL;
		d->send_write_parameters(d, s, -1, o);

		ret = d->get_nwa(d, -1, &lba, &nwa);
		sprintf(msg,
			"SAO|RAW: Inquired nwa: %d , ret= %d , cap=%.f\n",
			nwa, ret, (double) d->media_capacity_remaining);
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00000002,
				LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
				msg,0, 0);

		/* >>> ts A70212 : CD-DAO/SAO : eventually expand size of last track to maximum */;

	}

	for (i = 0; i < disc->sessions; i++) {
		/* update progress */
		d->progress.session = i;
		d->progress.tracks = disc->session[i]->tracks;

		/* ts A61114: added parameter nwa */
		sheet = burn_create_toc_entries(o, disc->session[i], nwa);

		/* ts A61009 */
		if (sheet == NULL)
			goto fail_wo_sync;

#ifdef Libburn_write_with_function_print_cuE
		print_cue(sheet);
		/* goto fail_wo_sync; */
#endif /* Libburn_write_with_function_print_cuE */

		ret = 1;
		if (o->write_type == BURN_WRITE_SAO)
			ret = d->send_cue_sheet(d, sheet);
		if (sheet->data != NULL)
			free(sheet->data);
		free(sheet);
		if (ret <= 0)
			goto fail_wo_sync;

		/* --- From here on, final sync is needed. --- */

		if (o->write_type == BURN_WRITE_RAW) {
			if (!burn_write_leadin(o, disc->session[i], first))
				goto fail;
		} else {
			if (first) {

				/* ts A61030 : 0 made the burner take data. */
				/* ts A61103 : Meanwhile d->nwa is updated in
						burn_write_track()  */
				if(o->write_type == BURN_WRITE_TAO) {
					d->nwa= d->alba = 0;
				} else {

#ifdef Libburn_sao_can_appenD
					/* ts A61114: address for d->write() */
					if (d->status == BURN_DISC_APPENDABLE
					  && o->write_type == BURN_WRITE_SAO) {
						d->nwa = d->alba = nwa-150;

						sprintf(msg, 
				"SAO appendable d->nwa= %d\n", d->nwa);
						libdax_msgs_submit(
				libdax_messenger, d->global_index, 0x00000002,
				LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
				msg, 0, 0);

					} else {
						d->nwa = -150;
						d->alba = -150;
					}
#else
					d->nwa = -150;
					d->alba = -150;
#endif /* ! Libburn_sao_can_appenD */


				}

			} else {
				d->nwa += 4500;
				d->alba += 4500;
			}
		}
		multi_mem = o->multi;
		if(i < disc->sessions - 1)
			o->multi = 1;
		ret = burn_write_session(o, disc->session[i]);
		o->multi = multi_mem;
		if (!ret)
			goto fail;

		lt = disc->session[i]->track[disc->session[i]->tracks - 1];
		if (o->write_type == BURN_WRITE_RAW) {
			if (!burn_write_leadout(o, first, lt->entry->control,
			                        lt->mode))
				goto fail;
		} else {

			/* ts A61030 */
			if (o->write_type != BURN_WRITE_TAO)

				if (!burn_write_flush(o, NULL))
					goto fail;

			d->nwa += first ? 6750 : 2250;
			d->alba += first ? 6750 : 2250;
		}
		if (first)
			first = 0;

		/* XXX: currently signs an end of session */
		d->progress.sector = 0;
		d->progress.start_sector = 0;
		d->progress.sectors = 0;
	}

	/* ts A61030: extended skipping of flush to TAO: session is closed */
	if (o->write_type != BURN_WRITE_SAO && o->write_type != BURN_WRITE_TAO)
		if (!burn_write_flush(o, NULL))
			goto fail;

	sleep(1);

	/* ts A61125 : update media state records */
	burn_drive_mark_unready(d, 0);
	burn_drive_inquire_media(d);

	/* ts A61012 : This return was traditionally missing. I suspect this
			to have caused Cdrskin_eject() failures */
	goto ex;

fail:
	d->sync_cache(d);
fail_wo_sync:;
	usleep(500001); /* ts A61222: to avoid a warning from remove_worker()*/
	libdax_msgs_submit(libdax_messenger, d->global_index, 0x0002010b,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Burn run failed", 0, 0);
	d->cancel = 1;
	/* <<< d->busy = BURN_DRIVE_IDLE; */
ex:;
	d->do_stream_recording = 0;
	if (d->buffer != NULL)
		burn_os_free_buffer((char *) d->buffer,
					sizeof(struct buffer), 0);
	d->buffer = buffer_mem;
	return;
}

/* ts A70811 : API function */
int burn_random_access_write(struct burn_drive *d, off_t byte_address,
				char *data, off_t data_count, int flag)
{
	int alignment = 0, start, upto, chunksize, err, fd = -1, ret;
	char msg[81], *rpt;
	struct buffer *buf = NULL, *buffer_mem = d->buffer;

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	if (d->released) {
		libdax_msgs_submit(libdax_messenger,
			d->global_index, 0x00020142,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Drive is not grabbed on random access write", 0, 0);
		{ret = 0; goto ex;}
	}
	if(d->drive_role == 0) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020146,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Drive is a virtual placeholder (null-drive)", 0, 0);
		{ret = 0; goto ex;}
	}
	if(d->drive_role == 4) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020181,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			"Pseudo-drive is a read-only file. Cannot write.",
			0, 0);
		{ret = 0; goto ex;}
	}

	if(d->drive_role == 2 || d->drive_role == 5)
		alignment = 2 * 1024;
	if (d->current_profile == 0x12) /* DVD-RAM */
		alignment = 2 * 1024;
        if (d->current_profile == 0x13) /* DVD-RW restricted overwrite */
		alignment = 32 * 1024;
	if (d->current_profile == 0x1a) /* DVD+RW */
		alignment = 2 * 1024;
	if (d->current_profile == 0x43) /* BD-RE */
		alignment = 2 * 1024;
	if (alignment == 0) {
		sprintf(msg, "Write start address not supported");
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020125,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"Write start address not supported", 0, 0);
		{ret = 0; goto ex;}
	}
	if ((byte_address % alignment) != 0) {
		sprintf(msg,
			"Write start address not properly aligned (%d bytes)",
			alignment);
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020126,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
		{ret = 0; goto ex;}
	}
	if ((data_count % alignment) != 0) {
		sprintf(msg,
			"Write data count not properly aligned (%ld bytes)",
			(long) alignment);
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020141,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
		{ret = 0; goto ex;}
	}
	if (d->busy != BURN_DRIVE_IDLE) {
		libdax_msgs_submit(libdax_messenger,
			d->global_index, 0x00020140,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Drive is busy on attempt to write random access",0,0);
		{ret = 0; goto ex;}
	}
	if(d->drive_role != 1) {
		fd = burn_stdio_open_write(d, byte_address, 2048, 0);
		if (fd == -1)
			{ret = 0; goto ex;}
	}
	d->busy = BURN_DRIVE_WRITING_SYNC;
	d->buffer = buf;

	start = byte_address / 2048;
	upto = start + data_count / 2048;
	rpt = data;
	for (; start < upto; start += 16) {
		chunksize = upto - start;
		if (chunksize > 16)
			chunksize = 16;
		d->buffer->bytes = chunksize * 2048;
		memcpy(d->buffer->data, rpt, d->buffer->bytes);
		rpt += d->buffer->bytes;
		d->buffer->sectors = chunksize;
		d->nwa = start;
		if(d->drive_role == 1) {
			err = d->write(d, d->nwa, d->buffer);
		} else {
			ret = burn_stdio_write(fd, (char *) d->buffer->data,
						d->buffer->bytes, d, 0);
			err = 0;
			if (ret <= 0)
				err = BE_CANCELLED;
		}
		if (err == BE_CANCELLED) {
			d->busy = BURN_DRIVE_IDLE;
			if(fd >= 0)
				close(fd);
			{ret = -(start * 2048 - byte_address); goto ex;}
		}
	}

	if(d->drive_role == 1)
		d->needs_sync_cache = 1;
	if(flag & 1) {
		if(d->drive_role == 1)
			d->sync_cache(d);
		else
			burn_stdio_sync_cache(fd, d, 0);
		d->needs_sync_cache = 0;
	}
		
	if(fd >= 0)
		close(fd);
	d->buffer = buffer_mem;
	d->busy = BURN_DRIVE_IDLE;
	ret = 1;
ex:
	BURN_FREE_MEM(buf);
	return ret;
}


/* ts B10527 */
/* @param bit0= force close, even if no damage was seen
*/
int burn_disc_close_damaged(struct burn_write_opts *o, int flag)
{
	struct burn_drive *d;
	int ret;
	enum burn_drive_status busy;

	d = o->drive;
	busy = d->busy;

	if (busy != BURN_DRIVE_IDLE) {
		libdax_msgs_submit(libdax_messenger,
			d->global_index, 0x00020106,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"Drive is busy on attempt to close damaged session",
			0, 0);
		{ret = 0; goto ex;}
	}
	if (!((d->next_track_damaged & 1) || (flag & 1))) {
		libdax_msgs_submit(libdax_messenger,
			d->global_index, 0x00020187,
			LIBDAX_MSGS_SEV_NOTE, LIBDAX_MSGS_PRIO_HIGH,
			"Track not marked as damaged. No action taken.",
			0, 0);
		{ret = 0; goto ex;}
	}
	d->busy = BURN_DRIVE_WRITING;

	if (d->current_profile == 0x09 || d->current_profile == 0x0a) {
		/* Close CD track and session */
		o->write_type = BURN_WRITE_TAO; /* no action without TAO */

		/* Send mode page 5 */;
		d->send_write_parameters(d, NULL, -1, o);

		ret = burn_write_close_session(o);
		if (ret <= 0)
			goto ex;

	} else if(d->current_profile == 0x11 || d->current_profile == 0x14) {
		/* Close DVD-R[W] track and session */
		o->write_type = BURN_WRITE_TAO; /* no action without TAO */

		/* Send mode page 5 */;
		d->send_write_parameters(d, NULL, -1, o);

		ret = burn_disc_close_track_dvd_minus_r(o, 0);
		if (ret <= 0)
			goto ex;
		ret = burn_disc_close_session_dvd_minus_r(o);
		if (ret <= 0)
			goto ex;

	} else if(d->current_profile == 0x1b || d->current_profile == 0x2b) {
		/* Close DVD+R track and session */
		ret = burn_disc_close_track_dvd_plus_r(o, d->last_track_no, 1);
		if (ret <= 0)
			goto ex;

	} else if(d->current_profile == 0x41) {
		/* Close BD-R track and session */
		ret = burn_disc_close_track_dvd_plus_r(o, d->last_track_no, 1);
		if (ret <= 0)
			goto ex;

	} else {
		libdax_msgs_submit(libdax_messenger,
			d->global_index, 0x00020188,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			"Cannot close damaged track on given media type",
			0, 0);
		{ret = 0; goto ex;}

	}
	ret = 1;
ex:;
	d->busy = busy;
	/* Record with drive that repair was attempted */
	d->next_track_damaged &= ~1;
	return ret;
}


