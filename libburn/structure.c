
/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2012 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

/* ts A61008 */
/* #include <a ssert.h> */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "libburn.h"
#include "structure.h"
#include "write.h"
#include "debug.h"
#include "init.h"
#include "util.h"
#include "transport.h"
#include "mmc.h"

#include "libdax_msgs.h"
extern struct libdax_msgs *libdax_messenger;


/* ts A61008 : replaced Assert by if and return 0 */
/* 	a ssert(!(pos > BURN_POS_END)); */

#define RESIZE(TO, NEW, pos) {\
	void *tmp;\
\
	if (pos > BURN_POS_END)\
		return 0;\
	if (pos == BURN_POS_END)\
		pos = TO->NEW##s;\
	if ((int) pos > TO->NEW##s)\
		return 0;\
\
	tmp = realloc(TO->NEW, sizeof(struct NEW *) * (TO->NEW##s + 1));\
	if (!tmp)\
		return 0;\
	TO->NEW = tmp;\
	memmove(TO->NEW + pos + 1, TO->NEW + pos,\
	        sizeof(struct NEW *) * (TO->NEW##s - pos));\
	TO->NEW##s++;\
}

struct burn_disc *burn_disc_create(void)
{
	struct burn_disc *d;
	d = calloc(1, sizeof(struct burn_disc));
	if (d == NULL) /* ts A70825 */
		return NULL;
	d->refcnt = 1;
	d->sessions = 0;
	d->session = NULL;

#ifdef Libburn_disc_with_incomplete_sessioN
	d->incomplete_sessions= 0;
#endif

	return d;
}

void burn_disc_free(struct burn_disc *d)
{
	d->refcnt--;
	if (d->refcnt == 0) {
		/* dec refs on all elements */
		int i;

		for (i = 0; i < d->sessions; i++)
			burn_session_free(d->session[i]);
		free(d->session);
		free(d);
	}
}

struct burn_session *burn_session_create(void)
{
	struct burn_session *s;
	int i;

	s = calloc(1, sizeof(struct burn_session));
	if (s == NULL) /* ts A70825 */
		return NULL;
	s->firsttrack = 1;
	s->lasttrack = 0;
	s->refcnt = 1;
	s->tracks = 0;
	s->track = NULL;
	s->hidefirst = 0;
	for (i = 0; i < 8; i++) {
		s->cdtext[i] = NULL;
		s->cdtext_language[i] = 0x00;                     /* Unknown */
		s->cdtext_char_code[i] = 0x00;                 /* ISO-8859-1 */
		s->cdtext_copyright[i] = 0x00;
	}
	s->cdtext_language[0] = 0x09;     /* Single-block default is English */
	s->mediacatalog[0] = 0;
	return s;
}

void burn_session_hide_first_track(struct burn_session *s, int onoff)
{
	s->hidefirst = onoff;
}

void burn_session_free(struct burn_session *s)
{
	int i;

	s->refcnt--;
	if (s->refcnt == 0) {
		/* dec refs on all elements */
		for (i = 0; i < s->tracks; i++)
			burn_track_free(s->track[i]);
		for (i = 0; i < 8; i++)
			burn_cdtext_free(&(s->cdtext[i]));
		free(s->track);
		free(s);
	}

}

int burn_disc_add_session(struct burn_disc *d, struct burn_session *s,
			  unsigned int pos)
{
	RESIZE(d, session, pos);
	d->session[pos] = s;
	s->refcnt++;
	return 1;
}


/* ts A81202: this function was in the API but not implemented.
*/
int burn_disc_remove_session(struct burn_disc *d, struct burn_session *s)
{
	int i, skip = 0;

	if (d->session == NULL)
		return 0;
	for (i = 0; i < d->sessions; i++) {
		if (s == d->session[i]) {
			skip++;
	continue;
		}
		d->session[i - skip] = d->session[i];
	}
	if (!skip)
		return 0;
	burn_session_free(s);
	d->sessions--;
	return 1;
}


struct burn_track *burn_track_create(void)
{
	struct burn_track *t;
	int i;

	t = calloc(1, sizeof(struct burn_track));
	if (t == NULL) /* ts A70825 */
		return NULL;
	t->refcnt = 1;
	t->indices = 0;
	for (i = 0; i < 100; i++)
		t->index[i] = 0x7fffffff;
	t->offset = 0;
	t->offsetcount = 0;
	t->tail = 0;
	t->tailcount = 0;
	t->mode = BURN_MODE1;
	t->isrc.has_isrc = 0;
	t->pad = 1;

	/* ts A70213 */
	t->fill_up_media = 0;
	/* ts A70218 */
	t->default_size = 0;

	t->entry = NULL;
	t->source = NULL;
	t->eos = 0;

	/* ts A61101 */
	t->sourcecount = 0;
	t->writecount = 0;
	t->written_sectors = 0;

	/* ts A61031 */
	t->open_ended = 0;
	t->track_data_done = 0;
	/* ts B10103 */
	t->end_on_premature_eoi = 0;

	t->pregap1 = 0;
	t->pregap2 = 0;
	t->pregap2_size = 150;

	t->postgap = 0;
	t->postgap_size = 150;

	/* ts A61024 */
	t->swap_source_bytes = 0;

	/* ts B11206 */
	for (i = 0; i < 8; i++)
		t->cdtext[i] = NULL;

	return t;
}

void burn_track_free(struct burn_track *t)
{
	int i;

	t->refcnt--;
	if (t->refcnt == 0) {
		/* dec refs on all elements */
		if (t->source)
			burn_source_free(t->source);
		for (i = 0; i < 8; i++)
			burn_cdtext_free(&(t->cdtext[i]));
		free(t);
	}
}

int burn_session_add_track(struct burn_session *s, struct burn_track *t,
			   unsigned int pos)
{
	RESIZE(s, track, pos);
	s->track[pos] = t;
	t->refcnt++;
	return 1;
}

int burn_session_remove_track(struct burn_session *s, struct burn_track *t)
{
	struct burn_track **tmp;
	int i, pos = -1;

	/* ts A61008 */
	/* a ssert(s->track != NULL); */
	if (s->track == NULL)
		return 0;

	burn_track_free(t);

	/* Find the position */
	for (i = 0; i < s->tracks; i++) {
		if (t == s->track[i]) {
			pos = i;
			break;
		}
	}

	if (pos == -1)
		return 0;

	/* Is it the last track? */
	if (pos != s->tracks - 1) {
		memmove(&s->track[pos], &s->track[pos + 1],
			sizeof(struct burn_track *) * (s->tracks - (pos + 1)));
	}

	s->tracks--;
	tmp = realloc(s->track, sizeof(struct burn_track *) * s->tracks);
	if (tmp)
		s->track = tmp;
	return 1;
}

void burn_structure_print_disc(struct burn_disc *d)
{
	int i;
	char msg[40];

	sprintf(msg, "This disc has %d sessions", d->sessions);
	libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
	for (i = 0; i < d->sessions; i++) {
		burn_structure_print_session(d->session[i]);
	}
}
void burn_structure_print_session(struct burn_session *s)
{
	int i;
	char msg[40];

	sprintf(msg, "    Session has %d tracks", s->tracks);
	libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
	for (i = 0; i < s->tracks; i++) {
		burn_structure_print_track(s->track[i]);
	}
}
void burn_structure_print_track(struct burn_track *t)
{
	char msg[80];

	sprintf(msg, "        track size %d sectors",
		burn_track_get_sectors(t));
	libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
}

void burn_track_define_data(struct burn_track *t, int offset, int tail,
			    int pad, int mode)
{
	int type_to_form(int mode, unsigned char *ctladr, int *form);
	int burn_sector_length(int tracktype);
	unsigned char ctladr;
	int form = -1; /* unchanged form will be considered an error too */
	char msg[80];

	type_to_form(mode, &ctladr, &form);
	if (form == -1 || burn_sector_length(mode) <= 0) {

		sprintf(msg,
			"Attempt to set track mode to unusable value 0x%X",
			(unsigned int) mode);
		libdax_msgs_submit(libdax_messenger, -1, 0x00020115,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
		return;
	}

	t->offset = offset;
	t->pad = pad;
	t->mode = mode;
	t->tail = tail;
}


/* ts A61024 */
int burn_track_set_byte_swap(struct burn_track *t, int swap_source_bytes)
{
	if (swap_source_bytes != 0 && swap_source_bytes != 1)
		return 0;
	t->swap_source_bytes = swap_source_bytes;
	return 1;
}


/* ts A90911 : API */
int burn_track_set_cdxa_conv(struct burn_track *t, int value)
{
	if (value < 0 || value > 1)
		return 0;
	t->cdxa_conversion = value;
	return 1;
}


void burn_track_set_isrc(struct burn_track *t, char *country, char *owner,
			 unsigned char year, unsigned int serial)
{
	int i;

	/* ts B11226 */
	t->isrc.has_isrc = 0;

	for (i = 0; i < 2; ++i) {

		/* ts A61008 : This is always true */
		/* a ssert((country[i] >= '0' || country[i] < '9') &&
		       (country[i] >= 'a' || country[i] < 'z') &&
		       (country[i] >= 'A' || country[i] < 'Z')); */
		/* ts A61008 : now coordinated with sector.c: char_to_isrc() */
		if (! ((country[i] >= '0' && country[i] <= '9') ||
		       (country[i] >= 'a' && country[i] <= 'z') ||
		       (country[i] >= 'A' && country[i] <= 'Z')   ) )
			goto is_not_allowed;

		t->isrc.country[i] = country[i];
	}
	for (i = 0; i < 3; ++i) {

		/* ts A61008 : This is always true */
		/* a ssert((owner[i] >= '0' || owner[i] < '9') &&
		       (owner[i] >= 'a' || owner[i] < 'z') &&
		       (owner[i] >= 'A' || owner[i] < 'Z')); */
		/* ts A61008 : now coordinated with sector.c: char_to_isrc() */
		if (! ((owner[i] >= '0' && owner[i] <= '9') ||
		       (owner[i] >= 'a' && owner[i] <= 'z') ||
		       (owner[i] >= 'A' && owner[i] <= 'Z')   ) )
			goto is_not_allowed;

		t->isrc.owner[i] = owner[i];
	}

	/* ts A61008 */
	/* a ssert(year <= 99); */
	if (year > 99)
		goto is_not_allowed;

	t->isrc.year = year;

	/* ts A61008 */
	/* a ssert(serial <= 99999); */
	if (serial > 99999)
		goto is_not_allowed;

	t->isrc.serial = serial;

	/* ts A61008 */
	t->isrc.has_isrc = 1;
	return;
is_not_allowed:;
	libdax_msgs_submit(libdax_messenger, -1, 0x00020114,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"Attempt to set ISRC with bad data", 0, 0);
	return;
}

/* ts B11226 API */
int burn_track_set_isrc_string(struct burn_track *t, char isrc[13], int flag)
{
	unsigned char year;
	unsigned int serial = 2000000000;

	if (strlen(isrc) != 12 ||
	    isrc[5] < '0' || isrc[5] > '9' || isrc[6] < '0' || isrc[6] > '9') {
		libdax_msgs_submit(libdax_messenger, -1, 0x00020114,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				"Attempt to set ISRC with bad data", 0, 0);
		return 0;
	}
	year = (isrc[5] - '0') * 10 + (isrc[6] - '0');
	isrc[12] = 0;
	sscanf(isrc + 7, "%u", &serial);
	burn_track_set_isrc(t, isrc, isrc + 2, year, serial);
	return t->isrc.has_isrc;
}

void burn_track_clear_isrc(struct burn_track *t)
{
	t->isrc.has_isrc = 0;
}

/* ts B20103 API */
int burn_track_set_index(struct burn_track *t, int index_number,
					unsigned int relative_lba, int flag)
{
	if (index_number < 0 || index_number > 99) {
		libdax_msgs_submit(libdax_messenger, -1, 0x0002019a,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				"Bad track index number", 0, 0);
		return 0;
	}

	/* >>> if track size known : check index */;

	t->index[index_number] = relative_lba;
	if (index_number >= t->indices)
		t->indices = index_number + 1;
	return 1;
}

/* ts B20103 API */
int burn_track_clear_indice(struct burn_track *t, int flag)
{
	int i;

	for (i = 0; i < 100; i++)
		t->index[i] = 0x7fffffff;
	t->indices = 0;
	return 1;
}

/* ts B20110 API */
int burn_track_set_pregap_size(struct burn_track *t, int size, int flag)
{
	t->pregap2 = (size >= 0);
	t->pregap2_size = size;
	return 1;
}

/* ts B20111 API */
int burn_track_set_postgap_size(struct burn_track *t, int size, int flag)
{
	t->postgap = (size >= 0);
	t->postgap_size = size;
	return 1;
}

/* ts B20119: outsourced from burn_track_get_sectors()
   @param flag bit0= do not add post-gap
*/
int burn_track_get_sectors_2(struct burn_track *t, int flag)
{
	/* ts A70125 : was int */
	off_t size = 0;
	int sectors, seclen;

	seclen = burn_sector_length(t->mode);

	if (t->cdxa_conversion == 1)
		/* ts A90911 : will read blocks of 2056 bytes and write 2048 */
		seclen += 8;

	if (t->source != NULL) {              /* ts A80808 : mending sigsegv */
		size = t->offset + t->source->get_size(t->source) + t->tail;
		/* ts B20119 : adding post-gap */
		if (t->postgap && !(flag & 1))
			size += t->postgap_size;
	} else if(t->entry != NULL) {
		/* ts A80808 : all burn_toc_entry of track starts should now
			have (extensions_valid & 1), even those from CD.
		*/
		if (t->entry->extensions_valid & 1)
			size = ((off_t) t->entry->track_blocks) * (off_t) 2048;
	}
	sectors = size / seclen;
	if (size % seclen)
		sectors++;
	return sectors;
}


int burn_track_get_sectors(struct burn_track *t)
{
	return burn_track_get_sectors_2(t, 0);
}

/* ts A70125 */
int burn_track_set_sectors(struct burn_track *t, int sectors)
{
	off_t size, seclen;
	int ret;

	seclen = burn_sector_length(t->mode);
	size = seclen * (off_t) sectors - (off_t) t->offset - (off_t) t->tail;
	if (size < 0)
		return 0;
	ret = t->source->set_size(t->source, size);
	t->open_ended = (t->source->get_size(t->source) <= 0);
	return ret;
}


/* ts A70218 , API since A70328 */
int burn_track_set_size(struct burn_track *t, off_t size)
{
	if (t->source == NULL)
		return 0;
	if (t->source->set_size == NULL)
		return 0;
	t->open_ended = (size <= 0);
	return t->source->set_size(t->source, size);
}


/* ts A70213 */
int burn_track_set_fillup(struct burn_track *t, int fill_up_media)
{
	t->fill_up_media = fill_up_media;
	if (fill_up_media)
		t->open_ended = 0;
	return 1;
}


/* ts A70213 */
/**
  @param flag bit0= force new size even if existing track size is larger
*/
int burn_track_apply_fillup(struct burn_track *t, off_t max_size, int flag)
{
	int max_sectors, ret = 2;
	char msg[80];

	if (t->fill_up_media <= 0)
		return 2;
	max_sectors = max_size / 2048;
	if (burn_track_get_sectors(t) < max_sectors || (flag & 1)) {
		sprintf(msg, "Setting total track size to %ds (payload %ds)\n",
			max_sectors & 0x7fffffff,
			(int) ((t->source->get_size(t->source) / 2048)
				& 0x7fffffff));
		libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
			msg, 0, 0);
		ret = burn_track_set_sectors(t, max_sectors);
		t->open_ended = 0;
	}
	return ret;
}


/* ts A61031 */
int burn_track_is_open_ended(struct burn_track *t)
{
	return !!t->open_ended;
}


/* ts A70218 : API */
int burn_track_set_default_size(struct burn_track *t, off_t size)
{
	t->default_size = size;
	return 1;
}


/* ts A70218 */
off_t burn_track_get_default_size(struct burn_track *t)
{
	return t->default_size;
}


/* ts A61101 : API function */
int burn_track_get_counters(struct burn_track *t, 
                            off_t *read_bytes, off_t *written_bytes)
{
/*
	fprintf(stderr, "libburn_experimental: sizeof(off_t)=%d\n",
		sizeof(off_t));
*/
	*read_bytes = t->sourcecount;
	*written_bytes = t->writecount;
	return 1;
}

/* ts A61031 */
int burn_track_is_data_done(struct burn_track *t)
{
        return !!t->track_data_done;
}

int burn_track_get_shortage(struct burn_track *t)
{
	int size;
	int seclen;

	seclen = burn_sector_length(t->mode);
	size = t->offset + t->source->get_size(t->source) + t->tail;
	if (size % seclen)
		return seclen - size % seclen;
	return 0;
}

int burn_session_get_sectors(struct burn_session *s)
{
	int sectors = 0, i;

	for (i = 0; i < s->tracks; i++)
		sectors += burn_track_get_sectors(s->track[i]);
	return sectors;
}


int burn_disc_get_sectors(struct burn_disc *d)
{
	int sectors = 0, i;

	for (i = 0; i < d->sessions; i++)
		sectors += burn_session_get_sectors(d->session[i]);
	return sectors;
}

void burn_track_get_entry(struct burn_track *t, struct burn_toc_entry *entry)
{
	if (t->entry == NULL)
		memset(entry, 0, sizeof(struct burn_toc_entry));
	else
		memcpy(entry, t->entry, sizeof(struct burn_toc_entry));
}

void burn_session_get_leadout_entry(struct burn_session *s,
				    struct burn_toc_entry *entry)
{
	if (s->leadout_entry == NULL)
		memset(entry, 0, sizeof(struct burn_toc_entry));
	else
		memcpy(entry, s->leadout_entry, sizeof(struct burn_toc_entry));
}

struct burn_session **burn_disc_get_sessions(struct burn_disc *d, int *num)
{

#ifdef Libburn_disc_with_incomplete_sessioN

	*num = d->sessions - d->incomplete_sessions;

#else

	*num = d->sessions;

#endif


	return d->session;
}


/* ts B30112 : API */
int burn_disc_get_incomplete_sessions(struct burn_disc *d)
{
#ifdef Libburn_disc_with_incomplete_sessioN

	return d->incomplete_sessions;

#else

	return 0;

#endif
}


struct burn_track **burn_session_get_tracks(struct burn_session *s, int *num)
{
	*num = s->tracks;
	return s->track;
}

int burn_track_get_mode(struct burn_track *track)
{
	return track->mode;
}

int burn_session_get_hidefirst(struct burn_session *session)
{
	return session->hidefirst;
}


/* ts A80808 : Enhance CD toc to DVD toc */
int burn_disc_cd_toc_extensions(struct burn_drive *drive, int flag)
{
	int sidx= 0, tidx= 0, ret, track_offset, alloc_len = 34;
	struct burn_toc_entry *entry, *prev_entry= NULL;
	struct burn_disc *d;
	/* ts A81126 : ticket 146 : There was a SIGSEGV in here */
	char *msg_data = NULL, *msg;
	struct buffer *buf = NULL;

	d = drive->disc;
	BURN_ALLOC_MEM(msg_data, char, 321);
	BURN_ALLOC_MEM(buf, struct buffer, 1);
	strcpy(msg_data,
		"Damaged CD table-of-content detected and truncated.");
	strcat(msg_data, " In burn_disc_cd_toc_extensions: ");
        msg = msg_data + strlen(msg_data);
	if (d->session == NULL) {
		strcpy(msg, "d->session == NULL");
		goto failure;
	}
	if (d->sessions <= 0) {
		ret = 1;
		goto ex;
	}

	for (sidx = 0; sidx < d->sessions; sidx++) {
		track_offset = burn_session_get_start_tno(d->session[sidx], 0);
		if (track_offset <= 0)
			track_offset = 1;
		if (d->session[sidx] == NULL) {
			sprintf(msg, "d->session[%d of %d] == NULL",
				sidx, d->sessions);
			goto failure;
		}
		if (d->session[sidx]->track == NULL) {
			sprintf(msg, "d->session[%d of %d]->track == NULL",
				sidx, d->sessions);
			goto failure;
		}
		if (d->session[sidx]->leadout_entry == NULL) {
			sprintf(msg,
				" Session %d of %d: Leadout entry missing.",
			  	sidx, d->sessions);
			goto failure;
		}
		for (tidx = 0; tidx < d->session[sidx]->tracks + 1; tidx++) {
			if (tidx < d->session[sidx]->tracks) {
				if (d->session[sidx]->track[tidx] == NULL) {
					sprintf(msg,
			  "d->session[%d of %d]->track[%d of %d] == NULL",
			   sidx, d->sessions, tidx,  d->session[sidx]->tracks);
					goto failure;
				}
				entry = d->session[sidx]->track[tidx]->entry;
				if (entry == NULL) {
					sprintf(msg,
			  "session %d of %d, track %d of %d, entry == NULL",
			  			sidx, d->sessions, tidx,
						d->session[sidx]->tracks);
					goto failure;
				}
			} else
				entry = d->session[sidx]->leadout_entry;
			entry->session_msb = 0;
			entry->point_msb = 0;
			entry->start_lba = burn_msf_to_lba(entry->pmin,
						entry->psec, entry->pframe);
			if (tidx > 0) {
				prev_entry->track_blocks =
					entry->start_lba
					- prev_entry->start_lba;

				/* The drive might know size restrictions
				   like pre-gaps
				*/
				ret = mmc_read_track_info(drive,
					tidx - 1 + track_offset, buf,
					alloc_len); 
				if (ret > 0) {
					ret = mmc_four_char_to_int(
							buf->data + 24);
					if (ret < prev_entry->track_blocks &&
					    ((!drive->current_is_cd_profile) ||
					   ret < prev_entry->track_blocks - 2))
						prev_entry->track_blocks = ret;
				}
				prev_entry->extensions_valid |= 1;
			}
			if (tidx == d->session[sidx]->tracks) {
				entry->session_msb = 0;
				entry->point_msb = 0;
				entry->track_blocks = 0;
				entry->extensions_valid |= 1;
			}
			prev_entry = entry;
		}
	}
	{ret = 1; goto ex;}
failure:
	libdax_msgs_submit(libdax_messenger, -1, 0x0002015f,
		LIBDAX_MSGS_SEV_MISHAP, LIBDAX_MSGS_PRIO_HIGH, msg_data, 0, 0);
	d->sessions= sidx;
	ret = 0;
ex:;
	BURN_FREE_MEM(buf);
	BURN_FREE_MEM(msg_data);
	return ret;
}


/* ts B20107 API */
int burn_session_set_start_tno(struct burn_session *session, int tno, int flag)
{
	if (tno < 1 || tno > 99) {
		libdax_msgs_submit(libdax_messenger, -1, 0x0002019b,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"CD start track number exceeds range of 1 to 99",
			0, 0);
		return 0;
	}
	if (tno + session->tracks - 1 > 99) {
		libdax_msgs_submit(libdax_messenger, -1, 0x0002019b,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"CD track number exceeds 99", 0, 0);
		return 0;
	}
	session->firsttrack = tno;
	return 1;
}


/* ts B20108 API */
int burn_session_get_start_tno(struct burn_session *session, int flag)
{
	return (int) session->firsttrack;
}


struct burn_cdtext *burn_cdtext_create(void)
{
	struct burn_cdtext *t;
	int i;

	t = burn_alloc_mem(sizeof(struct burn_cdtext), 1, 0);
	if (t == NULL)
		return NULL;
	for(i = 0; i < Libburn_pack_num_typeS; i ++) {
		t->payload[i] = NULL;
		t->length[i] = 0;
	}
	return t;
}


void burn_cdtext_free(struct burn_cdtext **cdtext)
{
	struct burn_cdtext *t;
	int i;

	t = *cdtext;
	if (t == NULL)
		return;
	for (i = 0; i < Libburn_pack_num_typeS; i++)
		if (t->payload[i] != NULL)
			free(t->payload[i]);
	free(t);
}


static int burn_cdtext_name_to_type(char *pack_type_name)
{
	int i, j;
	static char *pack_type_names[] = {
		Libburn_pack_type_nameS
	};

	for (i = 0; i < Libburn_pack_num_typeS; i++) {
		if (pack_type_names[i][0] == 0)
	continue;
		for (j = 0; pack_type_names[i][j]; j++)
			if (pack_type_names[i][j] != pack_type_name[j] &&
			    tolower(pack_type_names[i][j]) !=
							pack_type_name[j])
		break;
		if (pack_type_names[i][j] == 0)
			return Libburn_pack_type_basE + i;
	}
	return -1;
}


/* @param flag bit0= double byte characters
*/
static int burn_cdtext_set(struct burn_cdtext **cdtext,
				int pack_type, char *pack_type_name,
                    unsigned char *payload, int length, int flag)
{
	int i;
	struct burn_cdtext *t;

	if (pack_type_name != NULL)
		if (pack_type_name[0])
			pack_type = burn_cdtext_name_to_type(pack_type_name);
	if (pack_type < Libburn_pack_type_basE ||
		pack_type >= Libburn_pack_type_basE + Libburn_pack_num_typeS) {
		libdax_msgs_submit(libdax_messenger, -1, 0x0002018c,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			"CD-TEXT pack type out of range", 0, 0);
		return 0;
	}
	t = *cdtext;
	if (t == NULL) {
		*cdtext = t = burn_cdtext_create();
		if (t == NULL)
			return -1;
	}
	i = pack_type - Libburn_pack_type_basE;
	if (t->payload[i] != NULL)
		free(t->payload[i]);
	t->payload[i] = burn_alloc_mem((size_t) length, 1, 0);
	if (t->payload[i] == NULL)
		return -1;
	memcpy(t->payload[i], payload, length);
	t->length[i] = length;
	t->flags = (t->flags & ~(1 << i)) | (flag & (1 << i));
	return 1;
}


/* @return 1=single byte char , 2= double byte char , <=0 error */
static int burn_cdtext_get(struct burn_cdtext *t, int pack_type,
				char *pack_type_name,
				unsigned char **payload, int *length, int flag)
{
	if (pack_type_name != NULL)
		if (pack_type_name[0])
			pack_type = burn_cdtext_name_to_type(pack_type_name);
	if (pack_type < Libburn_pack_type_basE ||
		pack_type >= Libburn_pack_type_basE + Libburn_pack_num_typeS) {
		libdax_msgs_submit(libdax_messenger, -1, 0x0002018c,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			"CD-TEXT pack type out of range", 0, 0);
		return 0;
	}
	*payload = t->payload[pack_type - Libburn_pack_type_basE];
	*length = t->length[pack_type - Libburn_pack_type_basE];
	return 1 + ((t->flags >> (pack_type - Libburn_pack_type_basE)) & 1);
}


static int burn_cdtext_check_blockno(int block)
{
	if (block < 0 || block > 7) {
		libdax_msgs_submit(libdax_messenger, -1, 0x0002018d,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			"CD-TEXT block number out of range", 0, 0);
		return 0;
	}
	return 1;
}


/* ts B11206 API */
/* @param flag bit0= double byte characters
*/
int burn_track_set_cdtext(struct burn_track *t, int block,
                          int pack_type, char *pack_type_name,
                          unsigned char *payload, int length, int flag)
{
	int ret;

	if (burn_cdtext_check_blockno(block) <= 0)
		return 0;
	ret = burn_cdtext_set(&(t->cdtext[block]), pack_type, pack_type_name,
				payload, length, flag & 1);
	return ret;
}


/* ts B11206 API */
/* @return 1=single byte char , 2= double byte char , <=0 error */
int burn_track_get_cdtext(struct burn_track *t, int block,
                          int pack_type, char *pack_type_name,
                          unsigned char **payload, int *length, int flag)
{
	int ret;

	if (burn_cdtext_check_blockno(block) <= 0)
		return 0;
	if (t->cdtext[block] == NULL) {
		*payload = NULL;
		*length = 0;
		return 1;
	}
	ret = burn_cdtext_get(t->cdtext[block], pack_type, pack_type_name,
				payload, length, 0);
	return ret;
}


/* ts B11206 API */
int burn_track_dispose_cdtext(struct burn_track *t, int block) 
{
	int i;

	if (block == -1) {
		for (i= 0; i < 8; i++)
			burn_cdtext_free(&(t->cdtext[i]));
		return 1;
	}
	if (burn_cdtext_check_blockno(block) <= 0)
		return 0;
	burn_cdtext_free(&(t->cdtext[0]));
	return 1;
}


/* ts B11206 API */
/* @param flag bit0= double byte characters
*/
int burn_session_set_cdtext(struct burn_session *s, int block,
                          int pack_type, char *pack_type_name,
                          unsigned char *payload, int length, int flag)
{
	int ret;

	if (burn_cdtext_check_blockno(block) <= 0)
		return 0;
	ret = burn_cdtext_set(&(s->cdtext[block]), pack_type, pack_type_name,
				payload, length, flag & 1);
	return ret;
}


/* ts B11206 API */
/* @return 1=single byte char , 2= double byte char , <=0 error */
int burn_session_get_cdtext(struct burn_session *s, int block,
                          int pack_type, char *pack_type_name,
                          unsigned char **payload, int *length, int flag)
{
	int ret;

	if (burn_cdtext_check_blockno(block) <= 0)
		return 0;

	if (s->cdtext[block] == NULL) {
		*payload = NULL;
		*length = 0;
		return 1;
	}
	ret = burn_cdtext_get(s->cdtext[block], pack_type, pack_type_name,
				payload, length, 0);
	return ret;
}


/* ts B11206 API */
int burn_session_set_cdtext_par(struct burn_session *s,
				int char_codes[8], int copyrights[8],
				int block_languages[8], int flag)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (char_codes[i] >= 0 && char_codes[i] <= 255)
			s->cdtext_char_code[i] = char_codes[i];
		if (copyrights[i] >= 0 && copyrights[i] <= 255)
	                s->cdtext_copyright[i] = copyrights[i];
		if (block_languages[i] >= 0 && block_languages[i] <= 255)
			s->cdtext_language[i] = block_languages[i];
	}
	return 1;
}


/* ts B11206 API */
int burn_session_get_cdtext_par(struct burn_session *s,
				int char_codes[8], int copyrights[8],
				int block_languages[8], int flag)
{
	int i;

	for (i = 0; i < 8; i++) {
		char_codes[i] = s->cdtext_char_code[i];
		copyrights[i] = s->cdtext_copyright[i];
		block_languages[i]= s->cdtext_language[i];
	}
	return 1;
}


/* ts B11206 API */
int burn_session_dispose_cdtext(struct burn_session *s, int block) 
{
	int i;

	if (block == -1) {
		for (i= 0; i < 8; i++) {
			burn_session_dispose_cdtext(s, i);
			s->cdtext_char_code[i] = 0x01;        /* 7 bit ASCII */
			s->cdtext_copyright[i] = 0;
			s->cdtext_language[i] = 0;
		}
		return 1;
	}
	if (burn_cdtext_check_blockno(block) <= 0)
		return 0;
	burn_cdtext_free(&(s->cdtext[block]));
	s->cdtext_language[block] = 0x09;                   /* english */
	return 1;
}


/* --------------------- Reading CDRWIN cue sheet files ----------------- */


struct burn_cue_file_cursor {
	char *cdtextfile;
	char *source_file;
	off_t source_size;
	struct burn_source *file_source;
	int fifo_size;
	struct burn_source *fifo;
	int swap_audio_bytes;
	int no_cdtext;
	int no_catalog_isrc;
	int start_track_no;
	struct burn_source *offst_source;
	int current_file_ba;
	int current_index_ba;
	struct burn_track *prev_track;
	int prev_file_ba;
	int prev_block_size;
	struct burn_track *track;
	int track_no;
	int track_current_index;
	int track_has_source;
	int block_size;
	int block_size_locked;
	int track_mode;
	int flags;
};


static int cue_crs_new(struct burn_cue_file_cursor **reply, int flag)
{
	int ret;
	struct burn_cue_file_cursor *crs;

	BURN_ALLOC_MEM(crs, struct burn_cue_file_cursor, 1);
	crs->cdtextfile = NULL;
	crs->source_file = NULL;
	crs->source_size = -1;
	crs->file_source = NULL;
	crs->fifo_size = 0;
	crs->fifo = NULL;
	crs->swap_audio_bytes = 0;
	crs->no_cdtext = 0;
	crs->no_catalog_isrc = 0;
	crs->start_track_no = 1;
	crs->offst_source = NULL;
	crs->current_file_ba = -1000000000;
	crs->current_index_ba = -1000000000;
	crs->prev_track = NULL;
	crs->prev_file_ba = -1000000000;
	crs->prev_block_size = 0;
	crs->track = NULL;
	crs->track_no = 0;
	crs->track_current_index = -1;
	crs->track_has_source = 0;
	crs->block_size = 0;
	crs->block_size_locked = 0;
	crs->track_mode = 0;
	crs->flags = 0;

	*reply = crs;
	ret = 1;
ex:;
	return ret;
}


static int cue_crs_destroy(struct burn_cue_file_cursor **victim, int flag)
{
	struct burn_cue_file_cursor *crs;

	if (*victim == NULL)
		return 2;
	crs = *victim;
	if (crs->cdtextfile != NULL)
		free(crs->cdtextfile);
	if (crs->source_file != NULL)
		free(crs->source_file);
	if (crs->file_source != NULL)
		burn_source_free(crs->file_source);
	if (crs->fifo != NULL)
		burn_source_free(crs->fifo);
	if (crs->offst_source != NULL)
		burn_source_free(crs->offst_source);
	if (crs->prev_track != NULL)
		burn_track_free(crs->prev_track);
	if (crs->track != NULL)
		burn_track_free(crs->track);
	BURN_FREE_MEM(crs);
	*victim = NULL;
	return 1;
}


static char *cue_unquote_text(char *text, int flag)
{
	char *ept, *spt;

	spt = text;
	for (ept = text + strlen(text); ept > text; ept--)
		if (*(ept - 1) != 32 && *(ept - 1) != 9)
			break;
	if (text[0] == '"') {
		spt = text + 1;
		if (ept > spt)
			if (*(ept - 1) == '"')
				ept--;
	}
	*ept = 0;
	return spt;
}


/* @param flag bit0= insist in having a track object
               bit1= remove quotation marks if present
*/
static int cue_set_cdtext(struct burn_session *session,
			struct burn_track *track, int pack_type, char *text,
			struct burn_cue_file_cursor *crs, int flag)
{
	int ret;
	char *payload;

	if (crs->no_cdtext == 1) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00020195,
			LIBDAX_MSGS_SEV_WARNING, LIBDAX_MSGS_PRIO_HIGH,
		  "In cue sheet file: Being set to ignore all CD-TEXT aspects",
			0, 0);
		crs->no_cdtext = 2;
	}
	if (crs->no_cdtext)
		return 2;
	if ((flag & 1) && track == NULL) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00020192,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
		    "Track attribute set before first track in cue sheet file",
			 0, 0);
		ret = 0; goto ex;
	}
	if (flag & 2)
		payload = cue_unquote_text(text, 0);
	else
		payload = text;
	if (track != NULL) {
		ret =  burn_track_set_cdtext(track, 0, pack_type, "",
					(unsigned char *) payload,
					strlen(payload) + 1, 0);
	} else {
		ret =  burn_session_set_cdtext(session, 0, pack_type, "",
					(unsigned char *) payload,
					strlen(payload) + 1, 0);
	}
ex:;
	return ret;
}
	

static int cue_attach_track(struct burn_session *session,
				struct burn_cue_file_cursor *crs, int flag)
{
	int ret;

	if (crs->track == NULL)
		return 2;

	if (!crs->track_has_source) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00020194,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			"In cue sheet file: TRACK without INDEX 01", 0, 0);
		return 0;
	}
	if (crs->track_current_index < 1) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00020192,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
		     "No INDEX 01 defined for last TRACK in cue sheet file",
			0, 0);
		return 0;
	}
	if (session->tracks == 0) {
		crs->start_track_no = crs->track_no;
		ret = burn_session_set_start_tno(session, crs->track_no, 0);
		if (ret <= 0)
			return ret;
	}
	if (session->tracks + crs->start_track_no - 1 > 99) {
		libdax_msgs_submit(libdax_messenger, -1, 0x0002019b,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"CD track number exceeds 99",
			0, 0);
		return 0;
	}
	ret = burn_session_add_track(session, crs->track, BURN_POS_END);
	if (ret <= 0)
		return ret;
	if (crs->prev_track != NULL)
		burn_track_free(crs->prev_track); /* release reference */
	crs->prev_track = crs->track;
	crs->prev_file_ba = crs->current_file_ba;
	crs->prev_block_size = crs->block_size;
	crs->track = NULL;
	crs->track_current_index = -1;
	crs->track_has_source = 0;
	crs->current_file_ba = -1;
	crs->current_index_ba = -1;
	if (!crs->block_size_locked)
		crs->block_size = 0;
	return 1;
}


/* @param flag bit0= do not alter the content of *payload
                     do not change *payload
*/
static int cue_read_number(char **payload, int *number, int flag)
{
	int ret, at_end = 0;
	char *apt, *msg = NULL;

	for(apt = *payload; *apt != 0 && *apt != 32 && *apt != 9; apt++);
	if (*apt == 0)
		at_end = 1;
	else if (!(flag & 1))
		*apt = 0;
	ret = sscanf(*payload, "%d", number);
	if (ret != 1) {
		BURN_ALLOC_MEM(msg, char, 4096);
		sprintf(msg,
			"Unsuitable number in cue sheet file: '%.4000s'",
			*payload);
		libdax_msgs_submit(libdax_messenger, -1, 0x00020194,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			burn_printify(msg), 0, 0);
		ret = 0; goto ex;
	}
	/* Find start of next argument */
	if (!at_end)
		for (apt++; *apt == 32 || *apt == 9; apt++);
	if (!(flag & 1))
		*payload = apt;

	ret = 1;
ex:
	BURN_FREE_MEM(msg);
	return ret;
}


/* @param flag    bit0-7: desired type : 0=any , 1=.wav
*/
static int cue_open_audioxtr(char *path, struct burn_cue_file_cursor *crs,
				int *fd, int flag)
{
	struct libdax_audioxtr *xtr= NULL;
	char *fmt, *fmt_info;
	int ret, num_channels, sample_rate, bits_per_sample, msb_first;
	char *msg = NULL;

	BURN_ALLOC_MEM(msg, char, 4096);

	ret= libdax_audioxtr_new(&xtr, path, 0);
	if (ret <= 0)
		return ret;
	libdax_audioxtr_get_id(xtr, &fmt, &fmt_info, &num_channels,
		 		&sample_rate, &bits_per_sample, &msb_first, 0);
	if ((flag & 255) == 1) {
		if (strcmp(fmt, ".wav") != 0) {
			sprintf(msg,
		       "In cue sheet: Not recognized as WAVE : FILE '%.4000s'",
				path);
			libdax_msgs_submit(libdax_messenger, -1, 0x00020193,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				burn_printify(msg), 0, 0);
			ret = 0; goto ex;
		}
	}
	ret = libdax_audioxtr_get_size(xtr, &(crs->source_size), 0);
	if (ret <= 0) {
		sprintf(msg,
		     "In cue sheet: Cannot get payload size of FILE '%.4000s'",
			path);
		libdax_msgs_submit(libdax_messenger, -1, 0x00020193,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				burn_printify(msg), 0, 0);
		ret = 0; goto ex;
	}
	ret = libdax_audioxtr_detach_fd(xtr, fd, 0);
	if (ret <= 0) {
		sprintf(msg,
	  "In cue sheet: Cannot represent payload as plain fd: FILE '%.4000s'",
			path);
		libdax_msgs_submit(libdax_messenger, -1, 0x00020193,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				burn_printify(msg), 0, 0);
		ret = 0; goto ex;
	}
	crs->swap_audio_bytes = (msb_first == 1);

	ret = 1;
ex:
	if (xtr != NULL)
		libdax_audioxtr_destroy(&xtr, 0);
	BURN_FREE_MEM(msg);
	return ret;
}


/* @param flag    bit0-7: desired type : 0=any , 1=.wav
                  bit8= open by libdax_audioxtr functions
                
*/
static int cue_create_file_source(char *path, struct burn_cue_file_cursor *crs,
								int flag)
{
	int fd, ret;
	char *msg = NULL;

	BURN_ALLOC_MEM(msg, char, 4096);

	if (flag & 256) {
		ret = cue_open_audioxtr(path, crs, &fd, flag & 255);
		if (ret <= 0)
			goto ex;
	} else {
		fd = open(path, O_RDONLY);
		if (fd == -1) {
			sprintf(msg,
				"In cue sheet: Cannot open FILE '%.4000s'",
				path);
			libdax_msgs_submit(libdax_messenger, -1, 0x00020193,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				burn_printify(msg), errno, 0);
			ret = 0; goto ex;
		}
	}
	crs->file_source = burn_fd_source_new(fd, -1, crs->source_size);
	if (crs->file_source == NULL) {
		ret = -1; goto ex;
	}

	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	return ret;
}


static int cue_read_timepoint_lba(char *apt, char *purpose, int *file_ba,
								int flag)
{
	int ret, minute, second, frame;
	char *msg = NULL, msf[3], *msf_pt;

	BURN_ALLOC_MEM(msg, char, 4096);
	if (strlen(apt) < 8) {
no_time_point:;
		sprintf(msg,
			"Inappropriate cue sheet file %s '%.4000s'",
			purpose, apt);
		libdax_msgs_submit(libdax_messenger, -1, 0x00020194,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			burn_printify(msg), 0, 0);
		ret = 0; goto ex;
	}
	if (apt[2] != ':' || apt[5] != ':' ||
				(apt[8] != 0 && apt[8] != 32 && apt[8] != 9))
		goto no_time_point;
	msf[2] = 0;
	msf_pt = msf;
	strncpy(msf, apt, 2);
	ret = cue_read_number(&msf_pt, &minute, 1);
	if (ret <= 0)
		goto ex;
	strncpy(msf, apt + 3, 2);
	ret = cue_read_number(&msf_pt, &second, 1);
	if (ret <= 0)
		goto ex;
	strncpy(msf, apt + 6, 2);
	ret = cue_read_number(&msf_pt, &frame, 1);
	if (ret <= 0)
		goto ex;

	*file_ba = ((minute * 60) + second ) * 75 + frame;
	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	return ret;
}

static int cue_check_for_track(struct burn_cue_file_cursor *crs, char *cmd,
				int flag)
{
	int ret;
	char *msg = NULL;

	if (crs->track == NULL) {
		BURN_ALLOC_MEM(msg, char, 4096);
		sprintf(msg, "In cue sheet file: %s found before TRACK",
			cmd);
		libdax_msgs_submit(libdax_messenger, -1, 0x00020192,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
		ret = 0; goto ex;
	}
	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	return ret;
}

	
static int cue_interpret_line(struct burn_session *session, char *line,
				struct burn_cue_file_cursor *crs, int flag)
{
	int ret, mode, index_no, file_ba, chunks;
	int block_size, step, audio_xtr = 0;
	off_t size;
	char *cmd, *apt, *msg = NULL, *cpt, *filetype;
	struct burn_source *src, *inp_src;
	enum burn_source_status source_status;
	struct stat stbuf;

	BURN_ALLOC_MEM(msg, char, 4096);

	if (line[0] == 0 || line[0] == '#') {
		ret = 1; goto ex;
	}

	for (cmd = line; *cmd == 32 || *cmd == 9; cmd++);
	for(apt = cmd; *apt != 0 && *apt != 32 && *apt != 9; apt++);
	if (*apt != 0) {
		*apt = 0;
		for (apt++; *apt == 32 || *apt == 9; apt++);
	}

	if (strcmp(cmd, "ARRANGER") == 0) {
		ret = cue_set_cdtext(session, crs->track, 0x84, apt, crs, 2);
		if (ret <= 0)
			goto ex;

	} else if (strcmp(cmd, "CATALOG") == 0) {
		for (cpt = apt; (cpt - apt) < 13 && *cpt == (*cpt & 0x7f);
		     cpt++);
		if ((cpt - apt) < 13) {
			libdax_msgs_submit(libdax_messenger, -1, 0x00020194,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			 "In cue sheet file: Inappropriate content of CATALOG",
				0, 0);
			ret = 0; goto ex;
		}
		ret = cue_set_cdtext(session, NULL, 0x8e, apt, crs, 0);
		if (ret <= 0)
			goto ex;
		if (!crs->no_catalog_isrc) {
			memcpy(session->mediacatalog, apt, 13);
			session->mediacatalog[13] = 0;
		}

	} else if (strcmp(cmd, "CDTEXTFILE") == 0) {
		if (crs->no_cdtext) {
			ret = 1; goto ex;
		}
		apt = cue_unquote_text(apt, 0);
		if (crs->cdtextfile != NULL)
			free(crs->cdtextfile);
		crs->cdtextfile = strdup(apt);
		if (crs->cdtextfile == NULL) {
out_of_mem:;
			libdax_msgs_submit(libdax_messenger, -1, 0x00000003,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				"Out of virtual memory", 0, 0);
			ret = -1; goto ex;
		}

	} else if (strcmp(cmd, "COMPOSER") == 0) {
		ret = cue_set_cdtext(session, crs->track, 0x83, apt, crs, 2);
		if (ret <= 0)
			goto ex;

	} else if (strcmp(cmd, "FILE") == 0) {
		if (crs->file_source != NULL) {
			libdax_msgs_submit(libdax_messenger, -1, 0x00020192,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			      "In cue sheet file: Multiple occurences of FILE",
				0, 0);
			ret = 0; goto ex;
		}
		/* Obtain type */
		for (cpt = apt + (strlen(apt) - 1);
		     cpt > apt && (*cpt == 32 || *cpt == 9); cpt--);
		cpt[1] = 0;
		for (;  cpt > apt && *cpt != 32  && *cpt != 9; cpt--);
		if (cpt <= apt) {
			libdax_msgs_submit(libdax_messenger, -1, 0x00020194,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				"In cue sheet file: FILE without type word",
				0, 0);
			ret = 0; goto ex;
		}
		*cpt = 0;
		filetype = cpt + 1;
		if (strcmp(filetype, "BINARY") == 0) {
			crs->swap_audio_bytes = 0;
		} else if (strcmp(filetype, "MOTOROLA") == 0) {
			crs->swap_audio_bytes = 1;
		} else if (strcmp(filetype, "WAVE") == 0) {
			audio_xtr = 0x101;
		} else {
			sprintf(msg,
			  "In cue sheet file: Unsupported FILE type '%.4000s'",
				filetype);
			libdax_msgs_submit(libdax_messenger, -1, 0x00020197,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				burn_printify(msg), 0, 0);
			ret = 0; goto ex;
		}

		apt = cue_unquote_text(apt, 0);
		if (*apt == 0)
			ret = -1;
		else
			ret = stat(apt, &stbuf);
		if (ret == -1) {
not_usable_file:;
			sprintf(msg,
				"In cue sheet file: Unusable FILE '%.4000s'",
				apt);
			libdax_msgs_submit(libdax_messenger, -1, 0x00020194,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				burn_printify(msg), 0, 0);
			ret = 0; goto ex;
		}
		if (!S_ISREG(stbuf.st_mode))
			goto not_usable_file;
		crs->source_size = stbuf.st_size;
		if (crs->source_file != NULL)
			free(crs->source_file);
		crs->source_file = strdup(apt);
		if (crs->source_file == NULL)
			goto out_of_mem;
		ret = cue_create_file_source(apt, crs, audio_xtr);
		if (ret <= 0)
			goto ex;

	} else if (strcmp(cmd, "FLAGS") == 0) {
		ret = cue_check_for_track(crs, cmd, 0);
		if (ret <= 0)
			goto ex;
		while (*apt) {
			if (strncmp(apt, "DCP", 3) == 0) {
				crs->track_mode |= BURN_COPY;
				step = 3;
			} else if (strncmp(apt, "4CH", 3) == 0) {
				crs->track_mode |= BURN_4CH;
				step = 3;
			} else if (strncmp(apt, "PRE", 3) == 0) {
				crs->track_mode |= BURN_PREEMPHASIS;
				step = 3;
			} else if (strncmp(apt, "SCMS", 4) == 0) {
				crs->track_mode |= BURN_SCMS;
				step = 4;
			} else {
bad_flags:;
				for (cpt = apt;
				  *cpt != 32 && *cpt != 9 && *cpt != 0; cpt++);
				*cpt = 0;
				sprintf(msg,
			"In cue sheet file: Unknown FLAGS option '%.4000s'",
					apt);
				libdax_msgs_submit(libdax_messenger, -1,
						0x00020194,
						LIBDAX_MSGS_SEV_FAILURE,
						LIBDAX_MSGS_PRIO_HIGH,
						burn_printify(msg), 0, 0);
				ret = 0; goto ex;
			}

			/* Look for start of next word */
			if (apt[step] != 0 && apt[step] != 32 &&
			    apt[step] != 9)
				goto bad_flags;
			for (apt += step; *apt == 32 || *apt == 9; apt++);
		}
		burn_track_define_data(crs->track, 0, 0, 1, crs->track_mode);

	} else if (strcmp(cmd, "INDEX") == 0) {
		ret = cue_check_for_track(crs, cmd, 0);
		if (ret <= 0)
			goto ex;
		ret = cue_read_number(&apt, &index_no, 0);
		if (ret <= 0)
			goto ex;
		ret = cue_read_timepoint_lba(apt, "index time point",
					 &file_ba, 0);
		if (ret <= 0)
			goto ex;
		if (file_ba < crs->prev_file_ba) {
overlapping_ba:;
			libdax_msgs_submit(libdax_messenger, -1, 0x00020192,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				"Backward INDEX address in cue sheet file",
				0, 0);
			ret = 0; goto ex;
		}
		if (file_ba < crs->current_index_ba)
			goto overlapping_ba;
		if (crs->prev_track != NULL && crs->track_current_index < 0) {
			size = (file_ba - crs->prev_file_ba) *
							crs->prev_block_size;
			if (size <= 0)
				goto overlapping_ba;
			burn_track_set_size(crs->prev_track, size);
		}
		if (crs->track_current_index + 1 != index_no &&
		    !(crs->track_current_index < 0 && index_no <= 1)) {
			libdax_msgs_submit(libdax_messenger, -1, 0x00020192,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				"Unacceptable INDEX number in cue sheet file",
				0, 0);
			ret = 0; goto ex;
		}
		crs->track_current_index = index_no;

		if (crs->current_file_ba < 0)
			crs->current_file_ba = file_ba;
		crs->current_index_ba = file_ba;

		/* Set index address relative to track source start */
		ret = burn_track_set_index(crs->track, index_no,
					file_ba - crs->current_file_ba, 0);
		if (ret <= 0)
			goto ex;

		if (crs->track_has_source) {
			ret = 1; goto ex;
		}

		if (crs->block_size_locked && crs->fifo == NULL &&
		    crs->fifo_size > 0) {
			/* Now that the block size is known from TRACK:
			   Create fifo and use it for creating the offset
			   sources. This will fixate the block size to one
			   common value.
			*/
			chunks = crs->fifo_size / crs->block_size +
				!!(crs->fifo_size % crs->block_size);
			if (chunks < 4)
				chunks = 4;
			crs->fifo = burn_fifo_source_new(crs->file_source,
						crs->block_size, chunks, 0);
			if (crs->fifo == NULL) {
				ret = -1; goto ex;
			}
		}
		if (crs->fifo != NULL)
			inp_src = crs->fifo;
		else
			inp_src = crs->file_source;
		src = burn_offst_source_new(inp_src, crs->offst_source,
			(off_t) (file_ba * crs->block_size), (off_t) 0, 1);
		if (src == NULL)
			goto out_of_mem;

		/* >>> Alternative to above fifo creation:
		   Create a fifo for each track track.
		   This will be necessary if mixed-mode sessions get supporded.
		*/;

		source_status = burn_track_set_source(crs->track, src);
		if (source_status != BURN_SOURCE_OK) {
			ret = -1; goto ex;
		}

		/* Switch current source in crs */
		if (crs->offst_source != NULL)
			burn_source_free(crs->offst_source);
		crs->offst_source = src;
		crs->track_has_source = 1;

	} else if (strcmp(cmd, "ISRC") == 0) {
		ret = cue_check_for_track(crs, cmd, 0);
		if (ret <= 0)
			goto ex;
		ret = cue_set_cdtext(session, crs->track, 0x8e, apt, crs,
                                     1 | 2);
		if (ret <= 0)
			goto ex;
		if (!crs->no_catalog_isrc) {
			ret = burn_track_set_isrc_string(crs->track, apt, 0);
			if (ret <= 0)
				goto ex;
		}

	} else if (strcmp(cmd, "MESSAGE") == 0) {
		ret = cue_set_cdtext(session, crs->track, 0x85, apt, crs, 2);
		if (ret <= 0)
			goto ex;

	} else if (strcmp(cmd, "PERFORMER") == 0) {
		ret = cue_set_cdtext(session, crs->track, 0x81, apt, crs, 2);
		if (ret <= 0)
			goto ex;

	} else if (strcmp(cmd, "POSTGAP") == 0) {
		ret = cue_check_for_track(crs, cmd, 0);
		if (ret <= 0)
			goto ex;
		ret = cue_read_timepoint_lba(apt, "post-gap duration",
					 	&file_ba, 0);
		if (ret <= 0)
			goto ex;
		ret = burn_track_set_postgap_size(crs->track, file_ba, 0);
		if (ret <= 0)
			goto ex;

	} else if (strcmp(cmd, "PREGAP") == 0) {
		ret = cue_check_for_track(crs, cmd, 0);
		if (ret <= 0)
			goto ex;
		ret = cue_read_timepoint_lba(apt, "pre-gap duration",
					 	&file_ba, 0);
		if (ret <= 0)
			goto ex;
		ret = burn_track_set_pregap_size(crs->track, file_ba, 0);
		if (ret <= 0)
			goto ex;

	} else if (strcmp(cmd, "REM") == 0) {
		;

	} else if (strcmp(cmd, "SONGWRITER") == 0) {
		ret = cue_set_cdtext(session, crs->track, 0x82, apt, crs, 2);
		if (ret <= 0)
			goto ex;

	} else if (strcmp(cmd, "TITLE") == 0) {
		ret = cue_set_cdtext(session, crs->track, 0x80, apt, crs, 2);
		if (ret <= 0)
			goto ex;

	} else if (strcmp(cmd, "TRACK") == 0) {
		if (crs->file_source == NULL) {
			libdax_msgs_submit(libdax_messenger, -1, 0x00020192,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			      "No FILE defined before TRACK in cue sheet file",
				0, 0);
			ret = 0; goto ex;
		}
		/* Attach previous track to session */
		ret = cue_attach_track(session, crs, 0);
		if (ret <= 0)
			goto ex;
		/* Create new track */;
		ret = cue_read_number(&apt, &(crs->track_no), 0);
		if (ret <= 0)
			goto ex;
		if (crs->track_no < 1 || crs->track_no > 99) {
			sprintf(msg,
				"Inappropriate cue sheet file track number %d",
				crs->track_no);
			libdax_msgs_submit(libdax_messenger, -1, 0x00020194,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				burn_printify(msg), 0, 0);
			ret = 0; goto ex;
		}
		if (strcmp(apt, "AUDIO") == 0) {
			mode = BURN_AUDIO;
			block_size = 2352;
		} else if (strcmp(apt, "MODE1/2048") == 0) {
			mode = BURN_MODE1;
			block_size = 2048;
		} else {
			sprintf(msg,
			 "Unsupported cue sheet file track datatype '%.4000s'",
				apt);
			libdax_msgs_submit(libdax_messenger, -1, 0x00020197,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				burn_printify(msg), 0, 0);
			ret = 0; goto ex;
		}
		if (block_size != crs->block_size && crs->block_size > 0 &&
						crs->block_size_locked) {
			libdax_msgs_submit(libdax_messenger, -1, 0x00020197,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			"In cue sheet file: Unsupported mix track block sizes",
				0, 0);
			ret = 0; goto ex;
		}
		crs->block_size = block_size;

		crs->track = burn_track_create();
		if (crs->track == NULL)
			goto out_of_mem;
		crs->track_has_source = 0;
		crs->track_mode = mode;
		burn_track_define_data(crs->track, 0, 0, 1, mode);
		if (mode & BURN_AUDIO)
			burn_track_set_byte_swap(crs->track,
						!!crs->swap_audio_bytes);

	} else {
		sprintf(msg, "Unknown cue sheet file command '%.4000s'", line);
		libdax_msgs_submit(libdax_messenger, -1, 0x00020191,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				burn_printify(msg), 0, 0);
		ret = 0; goto ex;
	}

	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	return ret;
}


/* ts B11216 API */
/* @param flag bit0= do not attach CD-TEXT information to session and tracks
               bit1= do not attach CATALOG to session or ISRC to track for
                     writing to Q sub-channel
*/
int burn_session_by_cue_file(struct burn_session *session, char *path,
			int fifo_size, struct burn_source **fifo,
			unsigned char **text_packs, int *num_packs, int flag)
{
	int ret, num_tracks, i, pack_type, length, double_byte = 0;
	int line_counter = 0;
	struct burn_track **tracks;
	char *msg = NULL, *line = NULL;
	unsigned char *payload;
	struct stat stbuf;
	FILE *fp = NULL;
	struct burn_cue_file_cursor *crs = NULL;

	static unsigned char dummy_cdtext[2] = {0, 0};

	if (fifo != NULL)
		*fifo = NULL;
	if (text_packs != NULL)
		*text_packs = NULL;
	*num_packs = 0;

	BURN_ALLOC_MEM(msg, char, 4096);
	BURN_ALLOC_MEM(line, char, 4096);
	ret = cue_crs_new(&crs, 0);
	if (ret <= 0)
		goto ex;
	crs->no_cdtext = (flag & 1);
	crs->no_catalog_isrc = !!(flag & 2);
	crs->fifo_size = fifo_size;
	crs->block_size_locked = 1; /* No mixed sessions for now */

	tracks = burn_session_get_tracks(session, &num_tracks);
	if (num_tracks > 0) {
		sprintf(msg,
      "Cue sheet file reader called while session has already defined tracks");
		libdax_msgs_submit(libdax_messenger, -1, 0x00020196,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				burn_printify(msg), 0, 0);
		ret = 0; goto ex;
	}
	if (stat(path, &stbuf) == -1) {
cannot_open:;
		sprintf(msg, "Cannot open cue sheet file '%.4000s'",
			path);
		libdax_msgs_submit(libdax_messenger, -1, 0x00020193,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				burn_printify(msg), errno, 0);
		ret = 0; goto ex;
	}
	if (!S_ISREG(stbuf.st_mode)) {
		sprintf(msg,
			"File is not of usable type: Cue sheet file '%.4000s'",
			path);
		libdax_msgs_submit(libdax_messenger, -1, 0x00020193,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				burn_printify(msg), 0, 0);
		ret = 0; goto ex;
	}

	fp = fopen(path, "rb");
	if (fp == NULL)
		goto cannot_open;

	while (1) {
		if (burn_sfile_fgets(line, 4095, fp) == NULL) {
			if (!ferror(fp))
	break;
			sprintf(msg,
			 "Cannot read all bytes from cue sheet file '%.4000s'",
				path);
			libdax_msgs_submit(libdax_messenger, -1, 0x00020193,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				burn_printify(msg), 0, 0);
			ret = 0; goto ex;
		}
		line_counter++;
		ret = cue_interpret_line(session, line, crs, 0);
		if (ret <= 0) {
			sprintf(msg,
		 "Cue sheet file '%.4000s': Reading aborted after line %d",
				path, line_counter);
			libdax_msgs_submit(libdax_messenger, -1, 0x00020199,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				burn_printify(msg), 0, 0);
			goto ex;
		}
	}

	/* Attach last track to session */
	if (crs->track != NULL) {
		/* Set track size up to end of file */
		if (crs->current_file_ba < 0 || crs->track_current_index < 1) {
			libdax_msgs_submit(libdax_messenger, -1, 0x00020192,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
		     "No INDEX 01 defined for last TRACK in cue sheet file",
				0, 0);
			ret = 0; goto ex;
		}
		if (crs->current_file_ba * crs->block_size >=
							crs->source_size) {
			libdax_msgs_submit(libdax_messenger, -1, 0x00020194,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
	"TRACK start time point exceeds size of FILE from cue sheet file",
				0, 0);
			ret = 0; goto ex;
		}
		ret = burn_track_set_size(crs->track, crs->source_size -
			(off_t) (crs->current_file_ba * crs->block_size));
		if (ret <= 0)
			goto ex;

		ret = cue_attach_track(session, crs, 0);
		if (ret <= 0)
			goto ex;
	}
	if (crs->cdtextfile != NULL) {
		if (text_packs == NULL) {

			/* >>> Warn of ignored text packs */;

		} else {
			ret = burn_cdtext_from_packfile(crs->cdtextfile,
						text_packs, num_packs, 0);
			if (ret <= 0)
				goto ex;
		}
	}

	/* Check which tracks have data of pack types where session has not */
	tracks = burn_session_get_tracks(session, &num_tracks);
	for (pack_type = 0x80; pack_type < 0x8f; pack_type++) {
		if (pack_type > 0x86 && pack_type != 0x8e)
	continue;
		ret = burn_session_get_cdtext(session, 0, pack_type, "",
						&payload, &length, 0);
		if (ret <= 0)
			goto ex;
		if (payload != NULL)
	continue;
		for (i = 0; i < num_tracks; i++) {
			ret = burn_track_get_cdtext(tracks[i], 0, pack_type,
						 "", &payload, &length, 0);
			if (ret <= 0)
				goto ex;
			double_byte = (ret > 1);
			if (payload != NULL)
		break;
		}
		if (i < num_tracks) {
			ret = burn_session_set_cdtext(session, 0, pack_type,
					"", dummy_cdtext, 1 + double_byte,
					double_byte);
			if (ret <= 0)
				goto ex;
		}
	}
	ret = 1;
ex:
	if (ret <= 0) {
		tracks = burn_session_get_tracks(session, &num_tracks);
		for (i = 0; i < num_tracks; i++)
			burn_track_free(tracks[i]);
	} else {
		if (fifo != NULL) {
			*fifo = crs->fifo;
			crs->fifo = NULL;
		}
	}
	cue_crs_destroy(&crs, 0);
	BURN_FREE_MEM(line);
	BURN_FREE_MEM(msg);
	return ret;
}

