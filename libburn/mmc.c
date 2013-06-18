/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2012 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif


/* ts A61009 */
/* #include <a ssert.h> */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <pthread.h>
#include <ctype.h>

#include "error.h"
#include "sector.h"
#include "libburn.h"
#include "transport.h"
#include "mmc.h"
#include "spc.h"
#include "drive.h"
#include "debug.h"
#include "toc.h"
#include "structure.h"
#include "options.h"
#include "util.h"
#include "init.h"


/* ts A70223 : in init.c */
extern int burn_support_untested_profiles;

static int mmc_get_configuration_al(struct burn_drive *d, int *alloc_len);


#ifdef Libburn_log_in_and_out_streaM
/* <<< ts A61031 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif /* Libburn_log_in_and_out_streaM */


/* ts A61005 */
#include "libdax_msgs.h"
extern struct libdax_msgs *libdax_messenger;


/* ts A61219 : Based on knowlege from dvd+rw-tools-7.0 and mmc5r03c.pdf */
#define Libburn_support_dvd_plus_rW 1

/* ts A61229 */
#define Libburn_support_dvd_minusrw_overW 1

/* ts A70112 */
/* ts A80410 : applies to BD-RE too */
#define Libburn_support_dvd_raM 1

/* ts A70129 */
#define Libburn_support_dvd_r_seQ 1

/* ts A70306 */
#define Libburn_support_dvd_plus_R 1

/* ts A70509 : handling 0x41 as read-only type */
#define Libburn_support_bd_r_readonlY 1

/* ts A81208 */
#define Libburn_support_bd_plus_r_srM 1


/* ts A80410 : <<< Dangerous experiment: Pretend that DVD-RAM is BD-RE
 # define Libburn_dvd_ram_as_bd_rE yes
*/
/* ts A80509 : <<< Experiment: pretend that DVD-ROM and CD-ROM are other media
                   like BD-ROM (0x40), BD-R seq (0x41), BD-R random (0x42)
 # define Libburn_rom_as_profilE 0x40
*/


/* ts A80425 : Prevents command FORMAT UNIT for DVD-RAM or BD-RE.
               Useful only to test the selection of format descriptors without
               actually formatting the media.
 # define Libburn_do_not_format_dvd_ram_or_bd_rE 1
*/


/* ts A90603 : Simulate the command restrictions of an old MMC-1 drive
 # define Libisofs_simulate_old_mmc1_drivE 1
*/


/* DVD/BD progress report:
   ts A61219 : It seems to work with a used (i.e. thoroughly formatted) DVD+RW.
               Error messages of class DEBUG appear because of inability to
               read TOC or track info. Nevertheless, the written images verify.
   ts A61220 : Burned to a virgin DVD+RW by help of new mmc_format_unit()
               (did not test wether it would work without). Burned to a
               not completely formatted DVD+RW. (Had worked before without
               mmc_format_unit() but i did not exceed the formatted range
               as reported by dvd+rw-mediainfo.) 
   ts A61221 : Speed setting now works for both of my drives. The according
               functions in dvd+rw-tools are a bit intimidating to the reader.
               I hope it is possible to leave much of this to the drive. 
               And if it fails ... well, it's only speed setting. :))
   ts A61229 : Burned to several DVD-RW formatted to mode Restricted Overwrite
               by dvd+rw-format. Needs Libburn_support_dvd_minusrw_overW.
   ts A61230 : Other than growisofs, libburn does not send a mode page 5 for
               such DVD-RW (which the MMC-5 standard does deprecate) and it
               really seems to work without such a page.
   ts A70101 : Formatted DVD-RW media. Success is varying with media, but
               dvd+rw-format does not do better with the same media.
   ts A70112 : Support for writing to DVD-RAM.
   ts A70130 : Burned a first non-multi sequential DVD-RW. Feature 0021h
               Incremental Recording vanishes after that and media thus gets
               not recognized as suitable any more.
               After a run with -multi another disc still offers 0021h .
               dvd+rw-mediainfo shows two tracks. The second, an afio archive
               is readable by afio. Third and forth veryfy too. Suddenly
               dvd+rw-mediainfo sees lba 0 with track 2. But #2 still verifies
               if one knows its address.
   ts A70203 : DVD-RW need to get blanked fully. Then feature 0021h persists.
               Meanwhile Incremental streaming is supported like CD TAO:
               with unpredicted size, multi-track, multi-session.
   ts A70205 : Beginning to implement DVD-R[W] DAO : single track and session,
               size prediction mandatory.
   ts A70208 : Finally made tests with DVD-R. Worked exactly as new DVD-RW.
   ts A70306 : Implemented DVD+R (always -multi for now)
   ts A70330 : Allowed finalizing of DVD+R.
   ts A80228 : Made DVD+R/DL support official after nightmorph reported success
               in http://libburnia-project.org/ticket/13
   ts A80416 : drive->do_stream_recording brings DVD-RAM to full nominal
               writing speed at cost of no defect management.
   ts A80416 : Giulio Orsero reports success with BD-RE writing. With
               drive->do_stream_recording it does full nominal speed.
   ts A80506 : Giulio Orsero reports success with BD-RE formatting.
               BD-RE is now an officially supported profile.
   ts A81209 : The first two sessions have been written to BD-R SRM
               (auto formatted without Defect Management).
   ts A90107 : BD-R is now supported media type
*/

/* ts A70519 : With MMC commands of data direction FROM_DRIVE:
               Made struct command.dxfer_len equal to Allocation Length
               of MMC commands. Made sure that not more bytes are allowed
               for transfer than there are available. 
*/


/* ts A70711 Trying to keep writing from clogging the SCSI driver due to
             full buffer at burner drive: 0=waiting disabled, 1=enabled
             These are only defaults which can be overwritten by
             burn_drive_set_buffer_waiting()
*/
#define Libburn_wait_for_buffer_freE          0
#define Libburn_wait_for_buffer_min_useC        10000
#define Libburn_wait_for_buffer_max_useC       100000
#define Libburn_wait_for_buffer_tio_seC     120
#define Libburn_wait_for_buffer_min_perC     65
#define Libburn_wait_for_buffer_max_perC     95


static unsigned char MMC_GET_MSINFO[] =
	{ 0x43, 0, 1, 0, 0, 0, 0, 16, 0, 0 };
static unsigned char MMC_GET_TOC[] = { 0x43, 2, 2, 0, 0, 0, 0, 16, 0, 0 };
static unsigned char MMC_GET_TOC_FMT0[] = { 0x43, 0, 0, 0, 0, 0, 0, 16, 0, 0 };
static unsigned char MMC_GET_ATIP[] = { 0x43, 2, 4, 0, 0, 0, 0, 16, 0, 0 };
static unsigned char MMC_GET_LEADTEXT[] = { 0x43, 2, 5, 0, 0, 0, 0, 4, 0, 0 };
static unsigned char MMC_GET_DISC_INFO[] =
	{ 0x51, 0, 0, 0, 0, 0, 0, 16, 0, 0 };
static unsigned char MMC_READ_CD[] = { 0xBE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static unsigned char MMC_BLANK[] = { 0xA1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static unsigned char MMC_SEND_OPC[] = { 0x54, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static unsigned char MMC_SET_SPEED[] =
	{ 0xBB, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static unsigned char MMC_WRITE_12[] =
	{ 0xAA, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static unsigned char MMC_WRITE_10[] = { 0x2A, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* ts A61201 : inserted 0, before 16, */
static unsigned char MMC_GET_CONFIGURATION[] =
	{ 0x46, 0, 0, 0, 0, 0, 0, 16, 0, 0 };

static unsigned char MMC_SYNC_CACHE[] = { 0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static unsigned char MMC_GET_EVENT[] = { 0x4A, 1, 0, 0, 0x7e, 0, 0, 0, 8, 0 };
static unsigned char MMC_CLOSE[] = { 0x5B, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static unsigned char MMC_TRACK_INFO[] = { 0x52, 0, 0, 0, 0, 0, 0, 16, 0, 0 };

static unsigned char MMC_SEND_CUE_SHEET[] =
	{ 0x5D, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* ts A61023 : get size and free space of drive buffer */
static unsigned char MMC_READ_BUFFER_CAPACITY[] =
	{ 0x5C, 0, 0, 0, 0, 0, 0, 16, 0, 0 };

/* ts A61219 : format DVD+RW (and various others) */
static unsigned char MMC_FORMAT_UNIT[] = { 0x04, 0x11, 0, 0, 0, 0 };

/* ts A61221 :
   To set speed for DVD media (0xBB is for CD but works on my LG GSA drive) */
static unsigned char MMC_SET_STREAMING[] =
	{ 0xB6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* ts A61225 :
   To obtain write speed descriptors (command can do other things too) */
static unsigned char MMC_GET_PERFORMANCE[] =
	{ 0xAC, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* ts A70108 : To obtain info about drive and media formatting opportunities */
static unsigned char MMC_READ_FORMAT_CAPACITIES[] =
	{ 0x23, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* ts A70205 : To describe the layout of a DVD-R[W] DAO session */
static unsigned char MMC_RESERVE_TRACK[] =
	{ 0x53, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* ts A70812 : Read data sectors (for types with 2048 bytes/sector only) */
static unsigned char MMC_READ_10[] =
	{ 0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* ts A81210 : Determine the upper limit of readable data size */
static unsigned char MMC_READ_CAPACITY[] =
	{ 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* ts A90903 : Obtain media type specific information. E.g. manufacturer.
*/
static unsigned char MMC_READ_DISC_STRUCTURE[] =
	{ 0xAD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* ts B21125 : An alternatvie to BEh READ CD
*/
static unsigned char MMC_READ_CD_MSF[] =
	{ 0xB9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static int mmc_function_spy_do_tell = 0;

int mmc_function_spy(struct burn_drive *d, char * text)
{
	if (mmc_function_spy_do_tell)
		fprintf(stderr,"libburn: experimental: mmc_function_spy: %s\n",
			text);
	if (d == NULL)
		return 1;
	if (d->drive_role != 1) {
		char msg[4096];
	
		sprintf(msg, "Emulated drive caught in SCSI adapter \"%s\"",
				text);
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x0002014c,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
		d->cancel = 1;
		return 0;
	}
	return 1;
}

int mmc_function_spy_ctrl(int do_tell)
{
	mmc_function_spy_do_tell= !!do_tell;
	return 1;
}


/* ts A70201 */
int mmc_four_char_to_int(unsigned char *data)
{
	return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}


/* ts A70201 */
int mmc_int_to_four_char(unsigned char *data, int num)
{
	data[0] = (num >> 24) & 0xff;
	data[1] = (num >> 16) & 0xff;
	data[2] = (num >> 8) & 0xff;
	data[3] = num & 0xff;
	return 1;
}


static int mmc_start_for_bit0 = 0;

/* @param flag bit0= the calling function should need no START UNIT.
                     (Handling depends on mmc_start_for_bit0)
*/
int mmc_start_if_needed(struct burn_drive *d, int flag)
{
	if (!d->is_stopped)
		return 2;
	if ((flag & 1) && !mmc_start_for_bit0)
		return 2;
	d->start_unit(d);
	d->is_stopped = 0;
	return 1;
}


int mmc_send_cue_sheet(struct burn_drive *d, struct cue_sheet *s)
{
	struct buffer *buf = NULL;
	struct command *c;

	c = &(d->casual_command);
	mmc_start_if_needed(d, 0);
	if (mmc_function_spy(d, "mmc_send_cue_sheet") <= 0)
		return 0;
	BURN_ALLOC_MEM_VOID(buf, struct buffer, 1);
	scsi_init_command(c, MMC_SEND_CUE_SHEET, sizeof(MMC_SEND_CUE_SHEET));
	c->retry = 1;
	c->page = buf;
	c->page->bytes = s->count * 8;
	c->page->sectors = 0;
	c->opcode[6] = (c->page->bytes >> 16) & 0xFF;
	c->opcode[7] = (c->page->bytes >> 8) & 0xFF;
	c->opcode[8] = c->page->bytes & 0xFF;
	c->dir = TO_DRIVE;
	memcpy(c->page->data, s->data, c->page->bytes);
	d->issue_command(d, c);
ex:;
	BURN_FREE_MEM(buf);
	if (c->error) {
		d->cancel = 1;
		scsi_notify_error(d, c, c->sense, 18, 2);
	}
	return !c->error;
}


/* ts A70205 : Announce size of a DVD-R[W] DAO session.
   @param size The size in bytes to be announced to the drive.
               It will get rounded up to align to 32 KiB.
*/
int mmc_reserve_track(struct burn_drive *d, off_t size)
{
	struct command *c;
	int lba;
	char msg[80];

	c = &(d->casual_command);
	mmc_start_if_needed(d, 0);
	if (mmc_function_spy(d, "mmc_reserve_track") <= 0)
		return 0;

	scsi_init_command(c, MMC_RESERVE_TRACK, sizeof(MMC_RESERVE_TRACK));
	c->retry = 1;

	lba = size / 2048;
	if (size % 2048)
		lba++;
	mmc_int_to_four_char(c->opcode+5, lba);

	sprintf(msg, "reserving track of %d blocks", lba);
	libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			   LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
			   msg, 0, 0);

	c->page = NULL;
	c->dir = NO_TRANSFER;
	c->timeout = Libburn_mmc_reserve_timeouT;
	d->issue_command(d, c);
	if (c->error) {
		d->cancel = 1;
		scsi_notify_error(d, c, c->sense, 18, 2);
	}
	return !c->error;
}


/* ts A70201 :
   Common track info fetcher for mmc_get_nwa() and mmc_fake_toc()
*/
int mmc_read_track_info(struct burn_drive *d, int trackno, struct buffer *buf,
			int alloc_len)
{
	struct command *c;

	c = &(d->casual_command);
	mmc_start_if_needed(d, 1);
	if (mmc_function_spy(d, "mmc_read_track_info") <= 0)
		return 0;

	scsi_init_command(c, MMC_TRACK_INFO, sizeof(MMC_TRACK_INFO));
	c->dxfer_len = alloc_len;
	c->opcode[7] = (c->dxfer_len >> 8) & 0xff;
	c->opcode[8] = c->dxfer_len & 0xff;
	c->retry = 1;
	c->opcode[1] = 1;
	if(trackno<=0) {
		if (d->current_profile == 0x1a || d->current_profile == 0x13 ||
		    d->current_profile == 0x12 || d->current_profile == 0x42 ||
		    d->current_profile == 0x43)
			 /* DVD+RW , DVD-RW restricted overwrite , DVD-RAM
			    BD-R random recording, BD-RE */
			trackno = 1;
		else if (d->current_profile == 0x10 ||
			 d->current_profile == 0x11 ||
			 d->current_profile == 0x14 ||
			 d->current_profile == 0x15 ||
			 d->current_profile == 0x40 ||
			 d->current_profile == 0x41)
			/* DVD-ROM ,  DVD-R[W] Sequential ,
			   BD-ROM , BD-R sequential */
			trackno = d->last_track_no;
		else /* mmc5r03c.pdf: valid only for CD, DVD+R, DVD+R DL */
			trackno = 0xFF;
	}
	mmc_int_to_four_char(c->opcode + 2, trackno);
	c->page = buf;
	memset(buf->data, 0, BUFFER_SIZE);
	c->dir = FROM_DRIVE;
	d->issue_command(d, c);
	if (c->error)
		return 0;
	return 1;
}


/* ts A61110 : added parameters trackno, lba, nwa. Redefined return value. 
   @return 1=nwa is valid , 0=nwa is not valid , -1=error */
/* ts A70201 : outsourced 52h READ TRACK INFO command */
int mmc_get_nwa(struct burn_drive *d, int trackno, int *lba, int *nwa)
{
	struct buffer *buf = NULL;
	int ret, num, alloc_len = 20, err;
	unsigned char *data;
	char *msg = NULL;

	if (trackno <= 0)
		d->next_track_damaged = 0;
	mmc_start_if_needed(d, 1);
	if (mmc_function_spy(d, "mmc_get_nwa") <= 0)
		{ret = -1; goto ex;}

	/* ts B00327 : Avoid to inquire unsuitable media states */
	if (d->status != BURN_DISC_BLANK && d->status != BURN_DISC_APPENDABLE)
		{ret = 0; goto ex;}

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	ret = mmc_read_track_info(d, trackno, buf, alloc_len);
	if (ret <= 0)
		goto ex;
	data = buf->data;
	*lba = mmc_four_char_to_int(data + 8);
	*nwa = mmc_four_char_to_int(data + 12);
	num = mmc_four_char_to_int(data + 16);

	/* Pioneer BD-RW BDR-205 and LITE-ON LTR-48125S return -150 as *nwa
	   of blank media */
	if (*nwa < *lba && d->status == BURN_DISC_BLANK)
		*nwa = *lba;

#ifdef Libburn_pioneer_dvr_216d_load_mode5
	/* >>> memorize track mode : data[6] & 0xf */;
#endif

{ static int fake_damage = 0; /* bit0= damage on , bit1= NWA_V off */

	if (fake_damage & 1)
		data[5] |= 32; /* Damage bit */
	if (fake_damage & 2)
		data[7] &= ~1;

}

	BURN_ALLOC_MEM(msg, char, 160);
	if (trackno > 0)
		sprintf(msg, "Track number %d: ", trackno);
	else
		sprintf(msg, "Upcomming track: ");
	if (d->current_profile == 0x1a || d->current_profile == 0x13 ||
	    d->current_profile == 0x12 || d->current_profile == 0x43) {
		 /* overwriteable */
		*lba = *nwa = num = 0;

	} else if (data[5] & 32) { /* ts B10534 : MMC-5 6.27.3.7 Damage Bit */
		if (!(data[7] & 1)) { /* NWA_V is set to zero */
			/* "not closed due to an incomplete write" */
			strcat(msg, "Damaged, not closed and not writable");
			err= 0x00020185;
		} else {
			/* "may be recorded further in an incremental manner"*/
			strcat(msg, "Damaged and not closed");
			err= 0x00020186;
		}
		libdax_msgs_submit(libdax_messenger, d->global_index, err,
				LIBDAX_MSGS_SEV_WARNING, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
		if (trackno <= 0)
			d->next_track_damaged |= ((!(data[7] & 1)) << 1) | 1;
		{ret = 0; goto ex;}

	} else if (!(data[7] & 1)) {
		/* ts A61106 :  MMC-1 Table 142 : NWA_V = NWA Valid Flag */
		strcat(msg, "No Next-Writable-Address");
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020184,
				LIBDAX_MSGS_SEV_WARNING, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
		if (trackno <= 0)
			d->next_track_damaged |= 2;
		{ret = 0; goto ex;}

	}
	if (num > 0) {
		burn_drive_set_media_capacity_remaining(d,
					((off_t) num) * ((off_t) 2048));
		d->media_lba_limit = *nwa + num;
	} else
		d->media_lba_limit = 0;

/*
	fprintf(stderr, "LIBBURN_DEBUG: media_lba_limit= %d\n",
		 d->media_lba_limit);
*/

	ret = 1;
ex:
	BURN_FREE_MEM(buf);
	BURN_FREE_MEM(msg);
	return ret;
}

/* ts A61009 : function is obviously unused. */
/* void mmc_close_disc(struct burn_drive *d, struct burn_write_opts *o) */
void mmc_close_disc(struct burn_write_opts *o)
{
	struct burn_drive *d = o->drive;

	if (mmc_function_spy(d, "mmc_close_disc") <= 0)
		return;

	libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			   LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
			   "HOW THAT ? mmc_close_disc() was called", 0, 0);

	/* ts A61009 : made impossible by removing redundant parameter d */
	/* a ssert(o->drive == d); */

	o->multi = 0;
	spc_select_write_params(d, NULL, 0, o);
	mmc_close(d, 1, 0);
}

/* ts A61009 : function is obviously unused. */
/* void mmc_close_session(struct burn_drive *d, struct burn_write_opts *o) */
void mmc_close_session(struct burn_write_opts *o)
{
	struct burn_drive *d = o->drive;

	if (mmc_function_spy(d, "mmc_close_session") <= 0)
		return;

	libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			   LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
			   "HOW THAT ? mmc_close_session() was called", 0, 0);

	/* ts A61009 : made impossible by removing redundant parameter d */
	/* a ssert(o->drive == d); */

	o->multi = 3;
	spc_select_write_params(d, NULL, 0, o);
	mmc_close(d, 1, 0);
}

/* ts A70227 : extended meaning of session to address all possible values
               of 5Bh CLOSE TRACK SESSION to address any Close Function.
               @param session contains the two high bits of Close Function
               @param track if not 0: sets the lowest bit of Close Function
*/
void mmc_close(struct burn_drive *d, int session, int track)
{
	struct command *c;
	char msg[256];
	int key, asc, ascq;

	c = &(d->casual_command);
	if (mmc_function_spy(d, "mmc_close") <= 0)
		return;

	scsi_init_command(c, MMC_CLOSE, sizeof(MMC_CLOSE));
	c->retry = 1;

	c->opcode[1] |= 1; /* ts A70918 : Immed */

	/* (ts A61030 : shifted !!session rather than or-ing plain session ) */
	c->opcode[2] = ((session & 3) << 1) | !!track;
	c->opcode[4] = track >> 8;
	c->opcode[5] = track & 0xFF;
	c->page = NULL;
	c->dir = NO_TRANSFER;
	c->timeout = Libburn_mmc_close_timeouT;
	d->issue_command(d, c);

	/* ts A70918 : Immed : wait for drive to complete command */
	if (c->error) {
		sprintf(msg, "Failed to close %s (%d)",
		      session > 1 ? "disc" : session > 0 ? "session" : "track",
		      ((session & 3) << 1) | !!track);
		sprintf(msg + strlen(msg), ". SCSI error : ");
		scsi_error_msg(d, c->sense, 14, msg + strlen(msg), 
					&key, &asc, &ascq);
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x0002017e,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
		d->cancel = 1;
		return;
	}
	if (spc_wait_unit_attention(d, 3600, "CLOSE TRACK SESSION", 0) <= 0)
		d->cancel = 1;
}

void mmc_get_event(struct burn_drive *d)
{
	struct buffer *buf = NULL;
	struct command *c;
	int alloc_len = 8, len, evt_code, loops = 0;
	unsigned char *evt;

	c = &(d->casual_command);
	BURN_ALLOC_MEM_VOID(buf, struct buffer, 1);
	if (mmc_function_spy(d, "mmc_get_event") <= 0)
		goto ex;

again:;
	scsi_init_command(c, MMC_GET_EVENT, sizeof(MMC_GET_EVENT));
	c->dxfer_len = 8;

	/* >>> have a burn_drive element for Notification Class */;
	c->opcode[4] = 0x7e;

	c->opcode[7] = (c->dxfer_len >> 8) & 0xff;
	c->opcode[8] = c->dxfer_len & 0xff;
	c->retry = 1;
	c->page = buf;
	c->page->bytes = 0;
	c->page->sectors = 0;
	c->dir = FROM_DRIVE;
	d->issue_command(d, c);
	if (c->error)
		goto ex;

	evt = c->page->data;
	len = ((evt[0] << 8) | evt[1]) + 2;
	if (len < 8)
		goto ex;

	/* >>> memorize evt[3] in burn_drive element for Notification Class */;
	if (evt[3] == 0) /* No event */
		goto ex;

	evt_code = evt[4] & 0xf;
	if (evt_code == 0) /* No change */
		goto ex;

	switch (evt[2] & 7) {
	case 0: /* no events supported */
		goto ex;
	case 1: /* Operational change */
		if (((evt[6] << 8) | evt[7])) {
			alloc_len = 8;
			mmc_get_configuration_al(d, &alloc_len);
		}
		break;
	case 2: /* Power Management */
		if (evt[5] >= 2)
			d->start_unit(d);
		break;
	case 3: /* External request */

		/* >>> report about external request */;

		break;
	case 4: /* Media */
		if (evt_code == 2) {
			d->start_unit(d);
			alloc_len = 8;
			mmc_get_configuration_al(d, &alloc_len);
		}
		break;
	case 5: /* Multiple Host Events */
		
		/* >>> report about foreign host interference */;

		break;

	case 6: /* Device busy */
		if (evt_code == 1 && evt[5]) {

			/* >>> wait the time announced in evt[6],[7]
				 as 100ms units */;
		}
		break;
	default: /* reserved */
		break;
	}
	loops++;
	if (loops < 100)
		goto again;
ex:;
	BURN_FREE_MEM(buf);
}


/* ts A70711
   This has become a little monster because of the creative buffer reports of
   my LG GSA-4082B : Belated, possibly statistically dampened. But only with
   DVD media. With CD it is ok.
*/
static int mmc_wait_for_buffer_free(struct burn_drive *d, struct buffer *buf)
{
	int usec= 0, need, reported_3s = 0, first_wait = 1;
	struct timeval t0,tnow;
	struct timezone dummy_tz;
	double max_fac, min_fac, waiting;

/* Enable to get reported waiting activities and total time.
#define Libburn_mmc_wfb_debuG 1
*/
#ifdef Libburn_mmc_wfb_debuG
	char sleeplist[32768];
	static int buffer_still_invalid = 1;
#endif

	max_fac = ((double) d->wfb_max_percent) / 100.0;

	/* Buffer info from the drive is valid only after writing has begun.
	   Caring for buffer space makes sense mostly after max_percent of the
	   buffer was transmitted. */
	if (d->progress.buffered_bytes <= 0 ||
		d->progress.buffer_capacity <= 0 ||
		d->progress.buffered_bytes + buf->bytes <=
 					d->progress.buffer_capacity * max_fac)
		return 2;

#ifdef Libburn_mmc_wfb_debuG
	if (buffer_still_invalid)
			fprintf(stderr,
			"\nLIBBURN_DEBUG: Buffer considered valid now\n");
	buffer_still_invalid = 0;
#endif

	/* The pessimistic counter does not assume any buffer consumption */
	if (d->pessimistic_buffer_free - buf->bytes >=
		( 1.0 - max_fac) * d->progress.buffer_capacity)
		return 1;

	/* There is need to inquire the buffer fill */
	d->pessimistic_writes++;
	min_fac = ((double) d->wfb_min_percent) / 100.0;
	gettimeofday(&t0, &dummy_tz);
#ifdef Libburn_mmc_wfb_debuG
	sleeplist[0]= 0;
	sprintf(sleeplist,"(%d%s %d)",
		(int) (d->pessimistic_buffer_free - buf->bytes),
		(d->pbf_altered ? "? -" : " -"),
		(int) ((1.0 - max_fac) * d->progress.buffer_capacity));
#endif

	while (1) {
		if ((!first_wait) || d->pbf_altered) {
			d->pbf_altered = 1;
			mmc_read_buffer_capacity(d);
		}
#ifdef Libburn_mmc_wfb_debuG
		if(strlen(sleeplist) < sizeof(sleeplist) - 80)
			sprintf(sleeplist+strlen(sleeplist)," (%d%s %d)",
			(int) (d->pessimistic_buffer_free - buf->bytes),
			(d->pbf_altered ? "? -" : " -"),
			(int) ((1.0 - min_fac) * d->progress.buffer_capacity));
#endif
		gettimeofday(&tnow,&dummy_tz);
		waiting = (tnow.tv_sec - t0.tv_sec) +
			  ((double) (tnow.tv_usec - t0.tv_usec)) / 1.0e6;
		if (d->pessimistic_buffer_free - buf->bytes >=
			(1.0 - min_fac) * d->progress.buffer_capacity) {
#ifdef Libburn_mmc_wfb_debuG
			if(strlen(sleeplist) >= sizeof(sleeplist) - 80)
				strcat(sleeplist," ...");
			sprintf(sleeplist+strlen(sleeplist)," -> %d [%.6f]",
				(int) (
				 d->pessimistic_buffer_free - buf->bytes -
				 (1.0 - min_fac) * d->progress.buffer_capacity
				), waiting);
			fprintf(stderr,
				"\nLIBBURN_DEBUG: sleeplist= %s\n",sleeplist);
#endif
			return 1;
		}

		/* Waiting is needed */
		if (waiting >= 3 && !reported_3s) {
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x0002013d,
				LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_LOW,
			"Waiting for free buffer takes more than 3 seconds",
				0,0);
			reported_3s = 1;
		} else if (d->wfb_timeout_sec > 0 &&
				waiting > d->wfb_timeout_sec) {
			d->wait_for_buffer_free = 0;
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x0002013d,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"Timeout with waiting for free buffer. Now disabled.",
				0,0);
	break;
		}

		need = (1.0 - min_fac) * d->progress.buffer_capacity +
			buf->bytes - d->pessimistic_buffer_free;
		usec = 0;
		if (d->nominal_write_speed > 0)
			usec = ((double) need) / 1000.0 /
				((double) d->nominal_write_speed) * 1.0e6;
		else
			usec = d->wfb_min_usec * 2;

		/* >>> learn about buffer progress and adjust usec */

		if (usec < (int) d->wfb_min_usec)
			usec = d->wfb_min_usec;
		else if (usec > (int) d->wfb_max_usec)
			usec = d->wfb_max_usec;
		usleep(usec);
		if (d->waited_usec < 0xf0000000)
			d->waited_usec += usec;
		d->waited_tries++;
		if(first_wait)
			d->waited_writes++;
#ifdef Libburn_mmc_wfb_debuG
		if(strlen(sleeplist) < sizeof(sleeplist) - 80)
			sprintf(sleeplist+strlen(sleeplist)," %d", usec);
#endif
		first_wait = 0;
	}
	return 0;
}


void mmc_write_12(struct burn_drive *d, int start, struct buffer *buf)
{
	struct command *c;
	int len;

	c = &(d->casual_command);
	mmc_start_if_needed(d, 0);
	if (mmc_function_spy(d, "mmc_write_12") <= 0)
		return;

	len = buf->sectors;

	scsi_init_command(c, MMC_WRITE_12, sizeof(MMC_WRITE_12));
	c->retry = 1;
	mmc_int_to_four_char(c->opcode + 2, start);
	mmc_int_to_four_char(c->opcode + 6, len);
	c->page = buf;
	c->dir = TO_DRIVE;
	c->timeout = Libburn_scsi_write_timeouT;

	d->issue_command(d, c);

	/* ts A70711 */
	d->pessimistic_buffer_free -= buf->bytes;
	d->pbf_altered = 1;
}


#ifdef Libburn_write_time_debuG

static int print_time(int flag)
{
	static struct timeval prev = {0, 0};
	struct timeval now;
	struct timezone tz;
	int ret, diff;

	ret = gettimeofday(&now, &tz);
	if (ret == -1)
		return 0;
	if (now.tv_sec - prev.tv_sec < Libburn_scsi_write_timeouT) {
		diff = (now.tv_sec - prev.tv_sec) * 1000000 +
			((int) (now.tv_usec) - (int) prev.tv_usec);
		fprintf(stderr, "\nlibburn_DEBUG:  %d.%-6d : %d\n", (int) now.tv_sec, (int) now.tv_usec, diff);
	}
	memcpy(&prev, &now, sizeof(struct timeval));
	return 1;
}

#endif /* Libburn_write_time_debuG */


int mmc_write(struct burn_drive *d, int start, struct buffer *buf)
{
	int cancelled;
	struct command *c;
	int len, key, asc, ascq;
	char *msg = NULL;

#ifdef Libburn_write_time_debuG
	extern int burn_sg_log_scsi;
#endif

/*
fprintf(stderr, "libburn_DEBUG: buffer sectors= %d  bytes= %d\n",
        buf->sectors, buf->bytes);
*/


	c = &(d->casual_command);

#ifdef Libburn_log_in_and_out_streaM
	/* <<< ts A61031 */
	static int tee_fd= -1;
	if(tee_fd==-1)
		tee_fd= open("/tmp/libburn_sg_written",
				O_WRONLY|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR);
#endif /* Libburn_log_in_and_out_streaM */

	mmc_start_if_needed(d, 0);
	if (mmc_function_spy(d, "mmc_write") <= 0)
		return BE_CANCELLED;

	cancelled = d->cancel;
	if (cancelled)
		return BE_CANCELLED;

	/* ts A70215 */
	if (d->media_lba_limit > 0 && start >= d->media_lba_limit) {

		msg = calloc(1, 160);
		if (msg != NULL) {
			sprintf(msg,
		"Exceeding range of permissible write addresses (%d >= %d)",
				start, d->media_lba_limit);
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x0002012d,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
			free(msg);
		}
		d->cancel = 1; /* No need for mutexing because atomic */
		return BE_CANCELLED;
	}

	len = buf->sectors;

	/* ts A61009 : buffer fill problems are to be handled by caller */
	/* a ssert(buf->bytes >= buf->sectors);*/	/* can be == at 0... */

	/* ts A70711 */
	if(d->wait_for_buffer_free)
		mmc_wait_for_buffer_free(d, buf);

#ifdef Libburn_write_time_debuG
	if (burn_sg_log_scsi & 3)
		print_time(0);
#endif

	/* ts A80412 */
	if(d->do_stream_recording > 0 && start >= d->stream_recording_start) {

		/* >>> ??? is WRITE12 available ?  */
			/* >>> ??? inquire feature 107h Stream Writing bit ? */

		scsi_init_command(c, MMC_WRITE_12, sizeof(MMC_WRITE_12));
		mmc_int_to_four_char(c->opcode + 2, start);
		mmc_int_to_four_char(c->opcode + 6, len);
		c->opcode[10] = 1<<7; /* Streaming bit */
	} else {
		scsi_init_command(c, MMC_WRITE_10, sizeof(MMC_WRITE_10));
		mmc_int_to_four_char(c->opcode + 2, start);
		c->opcode[6] = 0;
		c->opcode[7] = (len >> 8) & 0xFF;
		c->opcode[8] = len & 0xFF;
	}
	c->retry = 1;
	c->page = buf;
	c->dir = TO_DRIVE;
	c->timeout = Libburn_scsi_write_timeouT;

#ifdef Libburn_log_in_and_out_streaM
	/* <<< ts A61031 */
	if(tee_fd!=-1) {
		write(tee_fd, c->page->data, c->page->bytes);
	}
#endif /* Libburn_log_in_and_out_streaM */

	d->issue_command(d, c);

	/* ts A70711 */
	d->pessimistic_buffer_free -= buf->bytes;
	d->pbf_altered = 1;

	/* ts A61112 : react on eventual error condition */ 
	spc_decode_sense(c->sense, 0, &key, &asc, &ascq);
	if (c->error && key != 0) {

		/* >>> make this scsi_notify_error() when liberated */
		int key, asc, ascq;

		msg = calloc(1, 256);
		if (msg != NULL) {
			sprintf(msg, "SCSI error on write(%d,%d): ",
				 start, len);
			scsi_error_msg(d, c->sense, 14, msg + strlen(msg), 
						&key, &asc, &ascq);
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x0002011d,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
			free(msg);
		}
		d->cancel = 1;
		return BE_CANCELLED;
	} 

	return 0;
}


/* ts A70201 : Set up an entry for mmc_fake_toc() */
int mmc_fake_toc_entry(struct burn_toc_entry *entry, int session_number,
			 int track_number,
			 unsigned char *size_data, unsigned char *start_data,
			 unsigned char *last_adr_data)
{
	int min, sec, frames, num;

	/* mark DVD extensions and Track Info extension as valid */
	entry->extensions_valid |= (1 | 2); 

	/* defaults are as of mmc5r03.pdf 6.26.3.2.4 Fabricated TOC */
	entry->session = session_number & 0xff;
	entry->session_msb = (session_number >> 8) & 0xff;
	entry->adr = 1;
	entry->control = 4;
	entry->tno = 0;
	entry->point = track_number & 0xff;
	entry->point_msb = (track_number >> 8) & 0xff;
	num = mmc_four_char_to_int(size_data);
	entry->track_blocks = num;
	burn_lba_to_msf(num, &min, &sec, &frames);
	if (min > 255) {
		min = 255;
		sec = 255;
		frames = 255;
	}
	entry->min = min;
	entry->sec = sec;
	entry->frame = frames;
	entry->zero = 0;
	num = mmc_four_char_to_int(start_data);
	entry->start_lba = num;
	burn_lba_to_msf(num, &min, &sec, &frames);
	if (min > 255) {
		min = 255;
		sec = 255;
		frames = 255;
	}
	entry->pmin = min;
	entry->psec = sec;
	entry->pframe = frames;
	entry->last_recorded_address = mmc_four_char_to_int(last_adr_data);
	return 1;
}


/* ts A71128 : for DVD-ROM drives which offer no reliable track information */
static int mmc_read_toc_fmt0_al(struct burn_drive *d, int *alloc_len)
{
	struct burn_track *track;
	struct burn_session *session;
	struct burn_toc_entry *entry;
	struct buffer *buf = NULL;
	struct command *c = NULL;
	int dlen, i, old_alloc_len, session_number, prev_session = -1, ret;
	int lba, size;
	unsigned char *tdata, size_data[4], start_data[4], end_data[4];

	if (*alloc_len < 4)
		{ret = 0; goto ex;}

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	BURN_ALLOC_MEM(c, struct command, 1);
	scsi_init_command(c, MMC_GET_TOC_FMT0, sizeof(MMC_GET_TOC_FMT0));
	c->dxfer_len = *alloc_len;
	c->opcode[7] = (c->dxfer_len >> 8) & 0xff;
	c->opcode[8] = c->dxfer_len & 0xff;
	c->retry = 1;
	c->page = buf;
	c->page->bytes = 0;
	c->page->sectors = 0;
	c->dir = FROM_DRIVE;
	d->issue_command(d, c);

	if (c->error) {
err_ex:;
		libdax_msgs_submit(libdax_messenger, d->global_index,
			 0x0002010d,
			 LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			 "Could not inquire TOC", 0,0);
		d->status = BURN_DISC_UNSUITABLE;
		d->toc_entries = 0;
		/* Prefering memory leaks over fandangos */
		d->toc_entry = calloc(1, sizeof(struct burn_toc_entry));
		{ret = 0; goto ex;}
	}
	dlen = c->page->data[0] * 256 + c->page->data[1];
	old_alloc_len = *alloc_len;
	*alloc_len = dlen + 2;
	if (old_alloc_len < 12)
		{ret = 1; goto ex;}
	if (dlen + 2 > old_alloc_len)
		dlen = old_alloc_len - 2;
	d->complete_sessions = 1 + c->page->data[3] - c->page->data[2];

#ifdef Libburn_disc_with_incomplete_sessioN
	/* ts B30112 : number of open sessions */
	d->incomplete_sessions = 0;
#endif

	d->last_track_no = d->complete_sessions;
	if (dlen - 2 < (d->last_track_no + 1) * 8) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
			 0x00020159,
			 LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			 "TOC Format 0 returns inconsistent data", 0,0);
		goto err_ex;
	}

	d->toc_entries = d->last_track_no + d->complete_sessions;
	if (d->toc_entries < 1)
		{ret = 0; goto ex;}
	d->toc_entry = calloc(d->toc_entries, sizeof(struct burn_toc_entry));
	if(d->toc_entry == NULL)
		{ret = 0; goto ex;}

	d->disc = burn_disc_create();
	if (d->disc == NULL)
		{ret = 0; goto ex;}
	for (i = 0; i < d->complete_sessions; i++) {
		session = burn_session_create();
		if (session == NULL)
			{ret = 0; goto ex;}
		burn_disc_add_session(d->disc, session, BURN_POS_END);
		burn_session_free(session);
	}


	for (i = 0; i < d->last_track_no; i++) {
		tdata = c->page->data + 4 + i * 8;
		session_number = i + 1;
		if (session_number != prev_session && prev_session > 0) {
			/* leadout entry previous session */
			entry = &(d->toc_entry[(i - 1) + prev_session]);
			lba = mmc_four_char_to_int(start_data) +
			      mmc_four_char_to_int(size_data);
			mmc_int_to_four_char(start_data, lba);
			mmc_int_to_four_char(size_data, 0);
			mmc_int_to_four_char(end_data, lba - 1);
			mmc_fake_toc_entry(entry, prev_session, 0xA2,
					 size_data, start_data, end_data);
			entry->min= entry->sec= entry->frame= 0;
			d->disc->session[prev_session - 1]->leadout_entry =
									entry;
		}

		/* ??? >>> d->media_capacity_remaining , d->media_lba_limit
				as of mmc_fake_toc()
		*/

		entry = &(d->toc_entry[i + session_number - 1]);
 		track = burn_track_create();
		if (track == NULL)
			{ret = -1; goto ex;}
		burn_session_add_track(
			d->disc->session[session_number - 1],
			track, BURN_POS_END);
		track->entry = entry;
		burn_track_free(track);

		memcpy(start_data, tdata + 4, 4);
			/* size_data are estimated from next track start */
		memcpy(size_data, tdata + 8 + 4, 4);
		mmc_int_to_four_char(end_data,
				mmc_four_char_to_int(size_data) - 1);
		size = mmc_four_char_to_int(size_data) -
	      		mmc_four_char_to_int(start_data);
		mmc_int_to_four_char(size_data, size);
		mmc_fake_toc_entry(entry, session_number, i + 1,
					 size_data, start_data, end_data);
		if (prev_session != session_number)
			d->disc->session[session_number - 1]->firsttrack = i+1;
		d->disc->session[session_number - 1]->lasttrack = i+1;
		prev_session = session_number;
	}
	if (prev_session > 0 && prev_session <= d->disc->sessions) {
		/* leadout entry of last session of closed disc */
		tdata = c->page->data + 4 + d->last_track_no * 8;
		entry = &(d->toc_entry[(d->last_track_no - 1) + prev_session]);
		memcpy(start_data, tdata + 4, 4);
		mmc_int_to_four_char(size_data, 0);
		mmc_int_to_four_char(end_data,
					mmc_four_char_to_int(start_data) - 1);
		mmc_fake_toc_entry(entry, prev_session, 0xA2,
				 size_data, start_data, end_data);
		entry->min= entry->sec= entry->frame= 0;
		d->disc->session[prev_session - 1]->leadout_entry = entry;
	}
	ret = 1;
ex:;
	BURN_FREE_MEM(buf);
	BURN_FREE_MEM(c);
	return ret;
}


/* ts A71128 : for DVD-ROM drives which offer no reliable track information */
static int mmc_read_toc_fmt0(struct burn_drive *d)
{
	int alloc_len = 4, ret;

	mmc_start_if_needed(d, 1);
	if (mmc_function_spy(d, "mmc_read_toc_fmt0") <= 0)
		return -1;
	ret = mmc_read_toc_fmt0_al(d, &alloc_len);
	if (alloc_len >= 12)
		ret = mmc_read_toc_fmt0_al(d, &alloc_len);
	return ret;
}


/* ts A70131 : compose a disc TOC structure from d->complete_sessions
               and 52h READ TRACK INFORMATION */
int mmc_fake_toc(struct burn_drive *d)
{
	struct burn_track *track;
	struct burn_session *session;
	struct burn_toc_entry *entry;
	struct buffer *buf = NULL;
	int i, session_number, prev_session = -1, ret, lba, alloc_len = 34;
	unsigned char *tdata, size_data[4], start_data[4], end_data[4];
	char *msg = NULL;

	if (mmc_function_spy(d, "mmc_fake_toc") <= 0)
		{ret = -1; goto ex;}
	BURN_ALLOC_MEM(buf, struct buffer, 1);

#ifdef Libburn_disc_with_incomplete_sessioN

	if (d->last_track_no <= 0 ||
            d->complete_sessions  + d->incomplete_sessions <= 0 ||
	    d->status == BURN_DISC_BLANK)
		{ret = 2; goto ex;}

#else

	if (d->last_track_no <= 0 || d->complete_sessions <= 0 ||
	    d->status == BURN_DISC_BLANK)
		{ret = 2; goto ex;}

#endif /* ! Libburn_disc_with_incomplete_sessioN */


	if (d->last_track_no > BURN_MMC_FAKE_TOC_MAX_SIZE) {
		msg = calloc(1, 160);
		if (msg != NULL) {
			sprintf(msg,
			  "Too many logical tracks recorded (%d , max. %d)\n",
			  d->last_track_no, BURN_MMC_FAKE_TOC_MAX_SIZE);
			libdax_msgs_submit(libdax_messenger, d->global_index,
				 0x0002012c,
				 LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				 msg, 0,0);
			free(msg);
		}
		{ret = 0; goto ex;}
	}
	/* ts A71128 : My DVD-ROM drive issues no reliable track info.
			One has to try 43h READ TOC/PMA/ATIP Form 0. */
	if ((d->current_profile == 0x10) && d->last_track_no <= 1) {
		ret = mmc_read_toc_fmt0(d);
		goto ex;
	}
	d->disc = burn_disc_create();
	if (d->disc == NULL)
		{ret = -1; goto ex;}
	d->toc_entries = d->last_track_no
	                 + d->complete_sessions + d->incomplete_sessions;
	d->toc_entry = calloc(d->toc_entries, sizeof(struct burn_toc_entry));
	if (d->toc_entry == NULL)
		{ret = -1; goto ex;}
	memset(d->toc_entry, 0,d->toc_entries * sizeof(struct burn_toc_entry));

#ifdef Libburn_disc_with_incomplete_sessioN

	for (i = 0; i < d->complete_sessions + d->incomplete_sessions; i++) {

#else

	for (i = 0; i < d->complete_sessions; i++) {

#endif

		session = burn_session_create();
		if (session == NULL)
			{ret = -1; goto ex;}
		burn_disc_add_session(d->disc, session, BURN_POS_END);
		burn_session_free(session);
	}

#ifdef Libburn_disc_with_incomplete_sessioN
	d->disc->incomplete_sessions = d->incomplete_sessions;
#endif

	memset(size_data, 0, 4);
	memset(start_data, 0, 4);


	/* Entry Layout :
		session 1   track 1     entry 0
		...
		session 1   track N     entry N-1
		leadout 1               entry N
		session 2   track N+1   entry N+1
		...
		session 2   track M+1   entry M+1
		leadout 2               entry M+2
		session X   track K     entry (K-1)+(X-1)
		...
		session X   track i+1   entry i+(X-1)
		leadout X               entry i+X
	*/
	for (i = 0; i < d->last_track_no; i++) {
		ret = mmc_read_track_info(d, i+1, buf, alloc_len);
		if (ret <= 0)
			goto ex;
		tdata = buf->data;
		session_number = (tdata[33] << 8) | tdata[3];
		if (session_number <= 0)
	continue;

		if (session_number != prev_session && prev_session > 0) {
			/* leadout entry previous session */
			entry = &(d->toc_entry[(i - 1) + prev_session]);
			lba = mmc_four_char_to_int(start_data) +
			      mmc_four_char_to_int(size_data);
			mmc_int_to_four_char(start_data, lba);
			mmc_int_to_four_char(size_data, 0);
			mmc_int_to_four_char(end_data, lba - 1);
			mmc_fake_toc_entry(entry, prev_session, 0xA2,
					 size_data, start_data, end_data);
			entry->min= entry->sec= entry->frame= 0;
			d->disc->session[prev_session - 1]->leadout_entry =
									entry;
		}

#ifdef Libburn_disc_with_incomplete_sessioN

		if (session_number > d->complete_sessions) {

#else

		if (session_number > d->disc->sessions) {

#endif

			if (i == d->last_track_no - 1) {
				/* ts A70212 : Last track field Free Blocks */
				burn_drive_set_media_capacity_remaining(d,
				  ((off_t) mmc_four_char_to_int(tdata + 16)) *
				  ((off_t) 2048));
				d->media_lba_limit = 0;
			}	

#ifdef Libburn_disc_with_incomplete_sessioN

			if (session_number > d->disc->sessions )
	continue;

#else

	continue;

#endif

		}

		entry = &(d->toc_entry[i + session_number - 1]);
 		track = burn_track_create();
		if (track == NULL)
			{ret = -1; goto ex;}
		burn_session_add_track(
			d->disc->session[session_number - 1],
			track, BURN_POS_END);
		track->entry = entry;
		burn_track_free(track);

		memcpy(size_data, tdata + 24, 4);
		memcpy(start_data, tdata + 8, 4);
		memcpy(end_data, tdata + 28, 4);
		mmc_fake_toc_entry(entry, session_number, i + 1,
					 size_data, start_data, end_data);
		entry->track_status_bits = tdata[5] | (tdata[6] << 8) |
		                           (tdata[7] << 16);
		entry->extensions_valid |= 4;

		if (prev_session != session_number)
			d->disc->session[session_number - 1]->firsttrack = i+1;
		d->disc->session[session_number - 1]->lasttrack = i+1;
		prev_session = session_number;
	}

	if (prev_session > 0 && prev_session <= d->disc->sessions) {
		/* leadout entry of last session of closed disc */
		entry = &(d->toc_entry[(d->last_track_no - 1) + prev_session]);
		lba = mmc_four_char_to_int(start_data) +
		      mmc_four_char_to_int(size_data);
		mmc_int_to_four_char(start_data, lba);
		mmc_int_to_four_char(size_data, 0);
		mmc_int_to_four_char(end_data, lba - 1);
		mmc_fake_toc_entry(entry, prev_session, 0xA2,
				 size_data, start_data, end_data);
		entry->min= entry->sec= entry->frame= 0;
		d->disc->session[prev_session - 1]->leadout_entry = entry;
	}
	ret = 1;
ex:;
	BURN_FREE_MEM(buf);
	return ret;
}


static int mmc_read_toc_al(struct burn_drive *d, int *alloc_len)
{
/* read full toc, all sessions, in m/s/f form, 4k buffer */
/* ts A70201 : or fake a toc from track information */
	struct burn_track *track;
	struct burn_session *session;
	struct buffer *buf = NULL;
	struct command *c = NULL;
	int dlen;
	int i, old_alloc_len, t_idx, ret;
	unsigned char *tdata;
	char *msg = NULL;

	if (*alloc_len < 4)
		{ret = 0; goto ex;}

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	BURN_ALLOC_MEM(c, struct command, 1);
	BURN_ALLOC_MEM(msg, char, 321);

	if (!(d->current_profile == -1 || d->current_is_cd_profile)) {
		/* ts A70131 : MMC_GET_TOC uses Response Format 2 
		   For DVD this fails with 5,24,00 */
		/* mmc_read_toc_fmt0() uses
                   Response Format 0: mmc5r03.pdf 6.26.3.2
		   which does not yield the same result with the same disc
		   on different drives.
		*/
		/* ts A70201 :
		   This uses the session count from 51h READ DISC INFORMATION
		   and the track records from 52h READ TRACK INFORMATION.
		   mmc_read_toc_fmt0() is used as fallback for dull DVD-ROM.
		*/
		mmc_fake_toc(d);

		if (d->status == BURN_DISC_UNREADY)
			d->status = BURN_DISC_FULL;
		{ret = 1; goto ex;}
	}

	/* ts A90823:
	   SanDisk Cruzer U3 memory stick stalls on format 2.
	   Format 0 seems to be more conservative with read-only drives.
	*/
	if (!(d->mdata->cdrw_write || d->current_profile != 0x08)) {
		ret = mmc_read_toc_fmt0(d);
		goto ex;
	}

	scsi_init_command(c, MMC_GET_TOC, sizeof(MMC_GET_TOC));
	c->dxfer_len = *alloc_len;
	c->opcode[7] = (c->dxfer_len >> 8) & 0xff;
	c->opcode[8] = c->dxfer_len & 0xff;
	c->retry = 1;
	c->page = buf;
	c->page->bytes = 0;
	c->page->sectors = 0;
	c->dir = FROM_DRIVE;
	d->issue_command(d, c);

	if (c->error) {

		/* ts A61020 : this snaps on non-blank DVD media */
		/* ts A61106 : also snaps on CD with unclosed track/session */
		/* Very unsure wether this old measure is ok.
		   Obviously higher levels do not care about this.
		   outdated info: DVD+RW burns go on after passing through here.

		d->busy = BURN_DRIVE_IDLE;
		*/
		libdax_msgs_submit(libdax_messenger, d->global_index,
			 0x0002010d,
			 LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			 "Could not inquire TOC", 0,0);
		d->status = BURN_DISC_UNSUITABLE;
		d->toc_entries = 0;
		/* Prefering memory leaks over fandangos */
		d->toc_entry = calloc(1, sizeof(struct burn_toc_entry));
		{ret = 0; goto ex;}
	}

	dlen = c->page->data[0] * 256 + c->page->data[1];
	old_alloc_len = *alloc_len;
	*alloc_len = dlen + 2;
	if (old_alloc_len < 15)
		{ret = 1; goto ex;}
	if (dlen + 2 > old_alloc_len)
		dlen = old_alloc_len - 2;
	d->toc_entries = (dlen - 2) / 11;
	if (d->toc_entries < 1)
		{ret = 0; goto ex;}
/*
	some drives fail this check.

	ts A61007 : if re-enabled then not via Assert.
	a ssert(((dlen - 2) % 11) == 0);
*/
	/* ts A81202: plus number of sessions as reserve for leadout default */
	d->toc_entry = calloc(d->toc_entries + (unsigned char) c->page->data[3],
				 sizeof(struct burn_toc_entry));
	if(d->toc_entry == NULL) /* ts A70825 */
		{ret = 0; goto ex;}
	tdata = c->page->data + 4;

	d->disc = burn_disc_create();
	if (d->disc == NULL) /* ts A70825 */
		{ret = 0; goto ex;}

	for (i = 0; i < c->page->data[3]; i++) {
		session = burn_session_create();
		if (session == NULL) /* ts A70825 */
			{ret = 0; goto ex;}
		burn_disc_add_session(d->disc, session, BURN_POS_END);
		burn_session_free(session);
	}

	/* ts A61022 */

	for (i = 0; i < d->toc_entries; i++, tdata += 11) {

/*
		fprintf(stderr, "libburn_experimental: toc entry #%d : %d %d %d\n",i,tdata[8], tdata[9], tdata[10]); 
*/

#ifdef Libburn_allow_first_hiddeN
		/* ts B00430 : this causes problems because the track has
		               no entry. One would have to coordinate this
		               with other parts of libburn.
		*/
		if (tdata[3] == 1) {
			if (burn_msf_to_lba(tdata[8], tdata[9], tdata[10])) {
				d->disc->session[0]->hidefirst = 1;
				track = burn_track_create();
				burn_session_add_track(d->disc->
						       session[tdata[0] - 1],
						       track, BURN_POS_END);
				burn_track_free(track);
			}
		}
#endif /* Libburn_allow_first_hiddeN */

		if (tdata[0] <= 0 || tdata[0] > d->disc->sessions)
			tdata[0] = d->disc->sessions;
		if (tdata[3] < 100 && tdata[0] > 0) {
			track = burn_track_create();
			burn_session_add_track(d->disc->session[tdata[0] - 1],
					       track, BURN_POS_END);
			track->entry = &d->toc_entry[i];
			burn_track_free(track);
		}
		d->toc_entry[i].session = tdata[0];
		d->toc_entry[i].adr = tdata[1] >> 4;
		d->toc_entry[i].control = tdata[1] & 0xF;
		d->toc_entry[i].tno = tdata[2];
		d->toc_entry[i].point = tdata[3];
		d->toc_entry[i].min = tdata[4];
		d->toc_entry[i].sec = tdata[5];
		d->toc_entry[i].frame = tdata[6];
		d->toc_entry[i].zero = tdata[7];
		d->toc_entry[i].pmin = tdata[8];
		d->toc_entry[i].psec = tdata[9];
		d->toc_entry[i].pframe = tdata[10];
		if (tdata[3] == 0xA0)
			d->disc->session[tdata[0] - 1]->firsttrack = tdata[8];
		if (tdata[3] == 0xA1)
			d->disc->session[tdata[0] - 1]->lasttrack = tdata[8];
		if (tdata[3] == 0xA2)
			d->disc->session[tdata[0] - 1]->leadout_entry =
				&d->toc_entry[i];
	}

	/* ts A70131 : was (d->status != BURN_DISC_BLANK) */
	if (d->status == BURN_DISC_UNREADY)
		d->status = BURN_DISC_FULL;
	toc_find_modes(d);

	/* ts A81202 ticket 146 : a drive reported a session with no leadout */
	for (i = 0; i < d->disc->sessions; i++) {
		if (d->disc->session[i]->leadout_entry != NULL)
	continue;
		sprintf(msg, "Session %d of %d encountered without leadout",
			i + 1, d->disc->sessions);
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020160,
				LIBDAX_MSGS_SEV_WARNING, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);

		/* Produce default leadout entry from last track of session
		   which will thus get its size set to 0 */;
		if (d->disc->session[i]->track != NULL &&
		    d->disc->session[i]->tracks > 0) {
			t_idx = d->toc_entries++;
			memcpy(d->toc_entry + t_idx,
				d->disc->session[i]->track[
				       d->disc->session[i]->tracks - 1]->entry,
				sizeof(struct burn_toc_entry));
			d->toc_entry[t_idx].point = 0xA2;
			d->disc->session[i]->leadout_entry =
							 d->toc_entry + t_idx;
		} else {
			burn_disc_remove_session(d->disc, d->disc->session[i]);
			sprintf(msg,
				"Empty session %d deleted. Now %d sessions.",
				i + 1, d->disc->sessions);
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020161,
				LIBDAX_MSGS_SEV_WARNING, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
			i--;
		}
	}

	/* A80808 */
	burn_disc_cd_toc_extensions(d, 0);

	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	BURN_FREE_MEM(c);
	BURN_FREE_MEM(buf);
	return ret;
}


void mmc_read_toc(struct burn_drive *d)
{
	int alloc_len = 4, ret;

	mmc_start_if_needed(d, 1);
	if (mmc_function_spy(d, "mmc_read_toc") <= 0)
		return;

	ret = mmc_read_toc_al(d, &alloc_len);
/*
	fprintf(stderr,
		"LIBBURN_DEBUG: 43h READ TOC alloc_len = %d , ret = %d\n",
		alloc_len, ret);
*/
	if (alloc_len >= 15)
		ret = mmc_read_toc_al(d, &alloc_len);
	if (ret <= 0)
		return;
}


/* ts A70131 : This tries to get the start of the last complete session */
/* man mkisofs , option -C :
   The first number is the sector number of the first sector in
   the last session of the disk that should be appended to.
*/
int mmc_read_multi_session_c1(struct burn_drive *d, int *trackno, int *start)
{
	struct buffer *buf = NULL;
	struct command *c = NULL;
	unsigned char *tdata;
	int num_sessions, session_no, num_tracks, alloc_len = 12, ret;
	struct burn_disc *disc;
	struct burn_session **sessions;
	struct burn_track **tracks;
	struct burn_toc_entry toc_entry;

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	BURN_ALLOC_MEM(c, struct command, 1);
	mmc_start_if_needed(d, 1);
	if (mmc_function_spy(d, "mmc_read_multi_session_c1") <= 0)
		{ret = 0; goto ex;}

	/* First try to evaluate the eventually loaded TOC before issueing
	   a MMC command. This search obtains the first track of the last
	   complete session which has a track.
	*/
	*trackno = 0;
	disc = burn_drive_get_disc(d);
	if (disc == NULL)
		goto inquire_drive;
	sessions = burn_disc_get_sessions(disc, &num_sessions);
	for (session_no = 0; session_no<num_sessions; session_no++) {
		tracks = burn_session_get_tracks(sessions[session_no],
						&num_tracks);
		if (tracks == NULL || num_tracks <= 0)
	continue;
		burn_track_get_entry(tracks[0], &toc_entry);
		if (toc_entry.extensions_valid & 1) { /* DVD extension valid */
			*start = toc_entry.start_lba;
			*trackno = (toc_entry.point_msb << 8)| toc_entry.point;
		} else {
			*start = burn_msf_to_lba(toc_entry.pmin,
					toc_entry.psec, toc_entry.pframe);
			*trackno = toc_entry.point;
		}
	}
	burn_disc_free(disc);
	if(*trackno > 0)
		{ret = 1; goto ex;}

inquire_drive:;
	/* mmc5r03.pdf 6.26.3.3.3 states that with non-CD this would
	   be a useless fake always starting at track 1, lba 0.
	   My drives return useful data, though.
	   MMC-3 states that DVD had no tracks. So maybe this mandatory fake
	   is a forgotten legacy ?
	*/
	scsi_init_command(c, MMC_GET_MSINFO, sizeof(MMC_GET_MSINFO));
	c->dxfer_len = alloc_len;
	c->opcode[7]= (c->dxfer_len >> 8) & 0xff;
	c->opcode[8]= c->dxfer_len & 0xff;
	c->retry = 1;
	c->page = buf;
	c->page->bytes = 0;
	c->page->sectors = 0;
	c->dir = FROM_DRIVE;
	d->issue_command(d, c);

	if (c->error)
		{ret = 0; goto ex;}

	tdata = c->page->data + 4;
	*trackno = tdata[2];
	*start = mmc_four_char_to_int(tdata + 4);
	ret = 1;
ex:;
	BURN_FREE_MEM(buf);
	BURN_FREE_MEM(c);
	return ret;
}


/* ts A61201 */
char *mmc_obtain_profile_name(int profile_number)
{
	static char *texts[0x53] = {NULL};
	int i, max_pno = 0x53;
	
	if (texts[0] == NULL) {
		for (i = 0; i<max_pno; i++)
			texts[i] = "";
		/* mmc5r03c.pdf , Table 89, Spelling: guessed cdrecord style */
		texts[0x01] = "Non-removable disk";
		texts[0x02] = "Removable disk";
		texts[0x03] = "MO erasable";
		texts[0x04] = "Optical write once";
		texts[0x05] = "AS-MO";
		texts[0x08] = "CD-ROM";
		texts[0x09] = "CD-R";
		texts[0x0a] = "CD-RW";
		texts[0x10] = "DVD-ROM";
		texts[0x11] = "DVD-R sequential recording";
		texts[0x12] = "DVD-RAM";
		texts[0x13] = "DVD-RW restricted overwrite";
		texts[0x14] = "DVD-RW sequential recording";
		texts[0x15] = "DVD-R/DL sequential recording";
		texts[0x16] = "DVD-R/DL layer jump recording";
		texts[0x1a] = "DVD+RW";
		texts[0x1b] = "DVD+R";
		texts[0x2a] = "DVD+RW/DL";
		texts[0x2b] = "DVD+R/DL";
		texts[0x40] = "BD-ROM";
		texts[0x41] = "BD-R sequential recording";
		texts[0x42] = "BD-R random recording";
		texts[0x43] = "BD-RE";
		texts[0x50] = "HD-DVD-ROM";
		texts[0x51] = "HD-DVD-R";
		texts[0x52] = "HD-DVD-RAM";
	}
	if (profile_number<0 || profile_number>=max_pno)
		return "";
	return texts[profile_number];
}


/* ts A90603 : to be used if the drive knows no GET CONFIGURATION
*/
static int mmc_guess_profile(struct burn_drive *d, int flag)
{
	int cp;

	cp = 0;
	if (d->status == BURN_DISC_BLANK ||
	    d->status == BURN_DISC_APPENDABLE) {
		cp = 0x09;
	} else if (d->status == BURN_DISC_FULL) {
		cp = 0x08;
	}
	if (cp)
		if (d->erasable)
			cp = 0x0a;
	d->current_profile = cp;
	if (cp == 0)
		return 0;
	d->current_is_cd_profile = 1;
	d->current_is_supported_profile = 1;
	strcpy(d->current_profile_text, mmc_obtain_profile_name(cp));
	return 1;
}


static int mmc_read_disc_info_al(struct burn_drive *d, int *alloc_len)
{
	struct buffer *buf = NULL;
	unsigned char *data;
	struct command *c = NULL;
	char *msg = NULL;
	/* ts A70131 : had to move mmc_read_toc() to end of function */
	int do_read_toc = 0, disc_status, len, old_alloc_len;
	int ret, number_of_sessions = -1;
	int key, asc, ascq;

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	BURN_ALLOC_MEM(c, struct command, 1);

	/* ts A61020 */
	d->start_lba = d->end_lba = -2000000000;
	d->erasable = 0;
	d->last_track_no = 1;

	/* ts B10730 */
	d->sent_default_page_05 = 0;
	/* ts A70212 - A70215 */
	d->media_capacity_remaining = 0;
	d->media_lba_limit = 0;

	/* ts A81210 */
	d->media_read_capacity = 0x7fffffff;

	/* ts A61202 */
	d->toc_entries = 0;
	if (d->status == BURN_DISC_EMPTY)
		{ret = 1; goto ex;}

	mmc_get_configuration(d);

	scsi_init_command(c, MMC_GET_DISC_INFO, sizeof(MMC_GET_DISC_INFO));
	c->dxfer_len = *alloc_len;
	c->opcode[7]= (c->dxfer_len >> 8) & 0xff;
	c->opcode[8]= c->dxfer_len & 0xff;
	c->retry = 1;
	c->page = buf;
	c->page->sectors = 0;
	c->page->bytes = 0;
	c->dir = FROM_DRIVE;
	d->issue_command(d, c);

	if (c->error) {
		spc_decode_sense(c->sense, 0, &key, &asc, &ascq);
		if (key == 5 && asc == 0x20 && ascq == 0) {
			/* ts B11031 : qemu -cdrom does not know
			               051h  READ DISC INFORMATION
			*/
			ret = mmc_read_toc_fmt0(d);
			if (ret > 0) {

				/* >>> ??? anything more to be set ? */;

				mmc_read_capacity(d);
				*alloc_len = 0;
				goto ex;
			}
		}

		d->busy = BURN_DRIVE_IDLE;
		{ret = 0; goto ex;}
	}

	data = c->page->data;
	len = (data[0] << 8) | data[1];
	old_alloc_len = *alloc_len;
	*alloc_len = len + 2;
	if (old_alloc_len < 34)
		{ret = 1; goto ex;}
	if (*alloc_len < 24) /* data[23] is the last mandatory byte here */
		{ret = 0; goto ex;}
	if (len + 2 > old_alloc_len)
		len = old_alloc_len - 2;

	d->erasable = !!(data[2] & 16);

	/* ts A90908 */
	d->disc_type = data[8];
	d->disc_info_valid = 1;
	d->disc_id = mmc_four_char_to_int(data + 12);
	d->disc_info_valid |= (!!(data[7] & 128)) << 1;
	if (len + 2 > 31 && (data[7] & 64)) {
		memcpy(d->disc_bar_code, data + 24, 8);
		d->disc_bar_code[8] = 0;
		d->disc_info_valid |= 4;
	}
	if (len + 2 > 32 && (data[7] & 16)) {
		d->disc_app_code = data[32];
		d->disc_info_valid |= 8;
	}
	if (data[7] & 32)
		d->disc_info_valid |= 16;
	if (data[2] & 16)
		d->disc_info_valid |= 32;

 	disc_status = data[2] & 3;
	d->state_of_last_session = (data[2] >> 2) & 3;
	number_of_sessions = (data[9] << 8) | data[4];

	if (d->current_profile == 0x10 || d->current_profile == 0x40) {
							 /* DVD-ROM , BD-ROM */
		disc_status = 2; /* always full and finalized */
		d->erasable = 0; /* never erasable */
	}

#ifdef Libburn_support_bd_r_readonlY
	/* <<< For now: declaring BD-R read-only
	*/
#ifndef Libburn_support_bd_plus_r_srM
	if (d->current_profile == 0x41) {
					/* BD-R seq as readonly dummy */
		disc_status = 2; /* always full and finalized */
		d->erasable = 0; /* never erasable */
	}
#endif
	if (d->current_profile == 0x42) {
						 /* BD-R rnd */
		disc_status = 2; /* always full and finalized */
		d->erasable = 0; /* never erasable */
	}
#endif /* Libburn_support_bd_r_readonlY */

	/* MMC-5 6.22.3.1.16:
	   Last Session Lead-in Start Address bytes 16 to 19
	   Last Possible Lead-out Start Address bytes 20 to 23
	   MSF for CD, LBA else
	*/
	if(d->current_profile == 0x08 || d->current_profile == 0x09 ||
	   d->current_profile == 0x0a) {
		d->last_lead_in =
			burn_msf_to_lba(data[17], data[18], data[19]);
		d->last_lead_out =
			burn_msf_to_lba(data[21], data[22], data[23]);
	} else {
		d->last_lead_in = mmc_four_char_to_int(data + 16);
		d->last_lead_out = mmc_four_char_to_int(data + 20);
	}

	switch (disc_status) {
	case 0:
regard_as_blank:;
		d->toc_entries = 0;

/*
		fprintf(stderr, "libburn_experimental: start_lba = %d (%d %d %d) , end_lba = %d (%d %d %d)\n",
			d->start_lba, data[17], data[18], data[19],
			d->end_lba, data[21], data[22], data[23]);
*/

		d->status = BURN_DISC_BLANK;
		d->start_lba = d->last_lead_in;
		d->end_lba = d->last_lead_out;
		break;
	case 1:
		d->status = BURN_DISC_APPENDABLE;

	case 2:
		if (disc_status == 2)
			d->status = BURN_DISC_FULL;

		/* ts A81210 */
		ret = mmc_read_capacity(d);
		/* Freshly formatted, unwritten BD-R pretend to be appendable
		   but in our model they need to be regarded as blank.
		   Criterion: BD-R seq, read capacity known and 0,
		              declared appendable, single empty session
		*/
		if (d->current_profile == 0x41 &&
		    d->status == BURN_DISC_APPENDABLE &&
		    ret > 0 && d->media_read_capacity == 0 &&
		    d->state_of_last_session == 0 && number_of_sessions == 1)
			goto regard_as_blank;

		if (d->current_profile == 0x41 &&
		    d->status == BURN_DISC_APPENDABLE &&
		    d->state_of_last_session == 1) {

			/* ??? apply this test to other media types ? */

			libdax_msgs_submit(libdax_messenger, d->global_index,
				 0x00020169,
				 LIBDAX_MSGS_SEV_WARNING, LIBDAX_MSGS_PRIO_HIGH,
				 "Last session on media is still open.", 0, 0);
		}

		do_read_toc = 1;
		break;
	case 3:
		/* ts A91009 : DVD-RAM has disc status "others" */
		mmc_read_capacity(d);
		break;
	}

	/* ts A90603 : An MMC-1 drive might not know the media type yet */
	if (d->current_is_guessed_profile && d->current_profile == 0)
		mmc_guess_profile(d, 0);

	if ((d->current_profile != 0 || d->status != BURN_DISC_UNREADY) 
		&& ! d->current_is_supported_profile) {
		if (!d->silent_on_scsi_error) {
			msg = calloc(1, 160);
			if (msg != NULL) {
				sprintf(msg,
				"Unsuitable media detected. Profile %4.4Xh  %s",
				d->current_profile, d->current_profile_text);
				libdax_msgs_submit(libdax_messenger,
				  d->global_index, 0x0002011e,
				  LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				  msg, 0,0);
				free(msg);
			}
		}
		d->status = BURN_DISC_UNSUITABLE;
		{ret = 0; goto ex;}
	}

	/* ts A61217 :
	   growisofs performs OPC if (data[0]<<8)|data[1]<=32
	   which indicates no OPC entries are attached to the
	   reply from the drive.
	   ts A91104 :
	   Actually growisofs performs OPC only on DVD-R[W].
	*/
	d->num_opc_tables = 0;
	if(((data[0] << 8) | data[1]) > 32) /* i.e. > 34 bytes are available */
		d->num_opc_tables = data[33];

	/* ts A61219 : mmc5r03c.pdf 6.22.3.1.13 BG Format Status
	   0=blank (not yet started)
           1=started but neither running nor complete
	   2=in progress
	   3=completed
	*/
	d->bg_format_status = data[7] & 3;

	/* Preliminarily declare blank:
	   ts A61219 : DVD+RW (is not bg_format_status==0 "blank")
	   ts A61229 : same for DVD-RW Restricted overwrite
	   ts A70112 : same for DVD-RAM
	*/
	if (d->current_profile == 0x1a || d->current_profile == 0x13 ||
	    d->current_profile == 0x12 || d->current_profile == 0x43)
		d->status = BURN_DISC_BLANK;

#ifdef Libburn_disc_with_incomplete_sessioN
	/* ts B30112 : number of open sessions */
	d->incomplete_sessions = 0;
#endif

	if (d->status == BURN_DISC_BLANK) {
                d->last_track_no = 1; /* The "incomplete track" */
		d->complete_sessions = 0;
	} else {
		/* ts A70131 : number of closed sessions */
		d->complete_sessions = number_of_sessions;
		/* mmc5r03c.pdf 6.22.3.1.3 State of Last Session: 3=complete */
		if (d->state_of_last_session != 3 &&
		    d->complete_sessions >= 1) {
			d->complete_sessions--;

#ifdef Libburn_disc_with_incomplete_sessioN
			d->incomplete_sessions++;
#endif

		}

		/* ts A70129 : mmc5r03c.pdf 6.22.3.1.7
		   This includes the "incomplete track" if the disk is
		   appendable. I.e number of complete tracks + 1. */
		d->last_track_no = (data[11] << 8) | data[6];
	}
	if (d->current_profile != 0x0a && d->current_profile != 0x13 &&
	    d->current_profile != 0x14 && d->status != BURN_DISC_FULL)
		d->erasable = 0; /* stay in sync with burn_disc_erase() */

	if (do_read_toc)
		mmc_read_toc(d);
	ret = 1;
ex:
	BURN_FREE_MEM(buf);
	BURN_FREE_MEM(c);
	return ret;
}


void mmc_read_disc_info(struct burn_drive *d)
{
	int alloc_len = 34, ret;

	mmc_start_if_needed(d, 1);
	if (mmc_function_spy(d, "mmc_read_disc_info") <= 0)
		return;

	ret = mmc_read_disc_info_al(d, &alloc_len);
/*
	fprintf(stderr,"LIBBURN_DEBUG: 51h alloc_len = %d , ret = %d\n",
			alloc_len, ret);
*/
	if (ret <= 0)
		return;

	/* for now there is no need to inquire the variable lenght part */
}


/* @param flag bit= do not allocate text_packs
*/
static int mmc_get_leadin_text_al(struct burn_drive *d,
                                  unsigned char **text_packs, int *alloc_len,
                                  int flag)
{
	struct buffer *buf = NULL;
	struct command *c = NULL;
	unsigned char *data;
	int ret, data_length;

	*text_packs = NULL;

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	BURN_ALLOC_MEM(c, struct command, 1);

	scsi_init_command(c, MMC_GET_LEADTEXT, sizeof(MMC_GET_LEADTEXT));
	c->dxfer_len = *alloc_len;
	c->opcode[7]= (c->dxfer_len >> 8) & 0xff;
	c->opcode[8]= c->dxfer_len & 0xff;
	c->retry = 1;
	c->page = buf;
	c->page->bytes = 0;
	c->page->sectors = 0;

	c->dir = FROM_DRIVE;
	d->issue_command(d, c);
	if (c->error)
		{ret = 0; goto ex;}

	data = c->page->data;	
	data_length = (data[0] << 8) + data[1];
	*alloc_len = data_length + 2;
	if (*alloc_len >= 22 && !(flag & 1)) {
		BURN_ALLOC_MEM(*text_packs, unsigned char, *alloc_len - 4);
		memcpy(*text_packs, data + 4, *alloc_len - 4);
	}
	ret = 1;	
ex:;
	BURN_FREE_MEM(c);
	BURN_FREE_MEM(buf);
	return ret;
}


/* ts B11201 */
/* Read the CD-TEXT data from the Lead-in of an Audio CD
*/
int mmc_get_leadin_text(struct burn_drive *d,
                        unsigned char **text_packs, int *num_packs, int flag)
{
	int alloc_len = 4, ret;

	*num_packs = 0;
	if (mmc_function_spy(d, "mmc_get_leadin_text") <= 0)
		return -1;
	ret = mmc_get_leadin_text_al(d, text_packs, &alloc_len, 1);
	if (ret <= 0 || alloc_len < 22)
		return (ret > 0 ? 0 : ret);
	ret = mmc_get_leadin_text_al(d, text_packs, &alloc_len, 0);
	if (ret <= 0 || alloc_len < 22) {
		if (*text_packs != NULL)
			free(*text_packs);
		*text_packs = NULL;
		return (ret > 0 ? 0 : ret);
	}
	*num_packs = (alloc_len - 4) / 18;
	return ret;
}


void mmc_read_atip(struct burn_drive *d)
{
	struct buffer *buf = NULL;
	struct command *c = NULL;
	int alloc_len = 28;

	/* ts A61021 */
	unsigned char *data;
	/* Speed values from A1: 
	   With 4 cdrecord tells "10" or "8" where MMC-1 says "8".
	   cdrecord "8" appear on 4xCD-RW and thus seem to be quite invalid.
	   My CD-R (>=24 speed) tell no A1.
	   The higher non-MMC-1 values are hearsay.
	*/
	                          /*  0,   2,   4,    6,   10,  -,   16,  -, */
        static int speed_value[16]= { 0, 353, 706, 1059, 1764, -5, 2824, -7,
	                   4234, 5646, 7056, 8468, -12, -13, -14, -15};
	               /*    24,   32,   40,   48,   -,   -,   -,   - */

	BURN_ALLOC_MEM_VOID(buf, struct buffer, 1);
	BURN_ALLOC_MEM_VOID(c, struct command, 1);
	mmc_start_if_needed(d, 1);
	if (mmc_function_spy(d, "mmc_read_atip") <= 0)
		goto ex;

	scsi_init_command(c, MMC_GET_ATIP, sizeof(MMC_GET_ATIP));
	c->dxfer_len = alloc_len;
	c->opcode[7] = (c->dxfer_len >> 8) & 0xff;
	c->opcode[8] = c->dxfer_len & 0xff;
	c->retry = 1;
	c->page = buf;
	c->page->bytes = 0;
	c->page->sectors = 0;

	c->dir = FROM_DRIVE;
	d->issue_command(d, c);
	/* ts B00501 : now caring for error */
	if (c->error) {
		d->erasable = 0;
		d->start_lba = 0;
		d->end_lba = 0;
		goto ex;
	}

	/* ts A61021 */
	data = c->page->data;
	d->erasable = !!(data[6]&64);
	d->start_lba = burn_msf_to_lba(data[8],data[9],data[10]);
	d->end_lba = burn_msf_to_lba(data[12],data[13],data[14]);

	/* ts B21124 : LITE-ON LTR-48125S returns crap on pressed
	               audio CD and CD-ROM
	*/
	if (d->start_lba >= d->end_lba) {
		d->start_lba = 0;
		d->end_lba = 0;
	}

	if (data[6]&4) {
		if (speed_value[(data[16]>>4)&7] > 0) {
			d->mdata->min_write_speed = 
				speed_value[(data[16]>>4)&7];
			if (speed_value[(data[16])&15] <= 0)
				d->mdata->max_write_speed = 
					speed_value[(data[16]>>4)&7];
		}
		if (speed_value[(data[16])&15] > 0) {
			d->mdata->max_write_speed = 
				speed_value[(data[16])&15];
			if (speed_value[(data[16]>>4)&7] <= 0)
				d->mdata->min_write_speed = 
					speed_value[(data[16])&15];
		}
	}

#ifdef Burn_mmc_be_verbous_about_atiP
	{ int i;
	fprintf(stderr,"libburn_experimental: Returned ATIP Data\n");
	for(i= 0; i<28; i++)
		fprintf(stderr,"%3.3d (0x%2.2x)%s",
			data[i],data[i],(((i + 1) % 5) ? "  " : "\n"));
	fprintf(stderr,"\n");

	fprintf(stderr,
		"libburn_experimental: Indicative Target Writing Power= %d\n",
		(data[4]>>4)&7);
	fprintf(stderr,
		"libburn_experimental: Reference speed= %d ->%d\n",
		data[4]&7, speed_value[data[4]&7]);
	fprintf(stderr,
		"libburn_experimental: Is %sunrestricted\n",
		(data[5]&64?"":"not "));
	fprintf(stderr,
		"libburn_experimental: Is %serasable, sub-type %d\n",
		(data[6]&64?"":"not "),(data[6]>>3)&3);
	fprintf(stderr,
		"libburn_experimental: lead in: %d (%-2.2d:%-2.2d/%-2.2d)\n",
		burn_msf_to_lba(data[8],data[9],data[10]),
		data[8],data[9],data[10]);
	fprintf(stderr,
		"libburn_experimental: lead out: %d (%-2.2d:%-2.2d/%-2.2d)\n",
		burn_msf_to_lba(data[12],data[13],data[14]),
		data[12],data[13],data[14]);
	if(data[6]&4)
	  fprintf(stderr,
		"libburn_experimental: A1 speed low %d   speed high %d\n",
		speed_value[(data[16]>>4)&7], speed_value[(data[16])&7]);
	if(data[6]&2)
	  fprintf(stderr,
		"libburn_experimental: A2 speed low %d   speed high %d\n",
		speed_value[(data[20]>>4)&7], speed_value[(data[20])&7]);
	if(data[6]&1)
	  fprintf(stderr,
		"libburn_experimental: A3 speed low %d   speed high %d\n",
		speed_value[(data[24]>>4)&7], speed_value[(data[24])&7]);
	}

#endif /* Burn_mmc_be_verbous_about_atiP */

/* ts A61020
http://www.t10.org/ftp/t10/drafts/mmc/mmc-r10a.pdf , table 77 :

 0 ATIP Data Length MSB
 1 ATIP Data Length LSB
 2 Reserved
 3 Reserved
 4 bit7=1, bit4-6="Indicative Target Writing Power", bit3=reserved ,
   bit0-2="Reference speed"
 5 bit7=0, bit6="URU" , bit0-5=reserved
 6 bit7=1, bit6="Disc Type", bit3-4="Disc Sub-Type", 
   bit2="A1", bit1="A2", bit0="A3"
 7 reserved
 8 ATIP Start Time of lead-in (Min)
 9 ATIP Start Time of lead-in (Sec)
10 ATIP Start Time of lead-in (Frame)
11 reserved
12 ATIP Last Possible Start Time of lead-out (Min)
13 ATIP Last Possible Start Time of lead-out (Sec)
14 ATIP Last Possible Start Time of lead-out (Frame)
15 reserved
16 bit7=0, bit4-6="Lowest Usable CLV Recording speed"
   bit0-3="Highest Usable CLV Recording speed"
17 bit7=0, bit4-6="Power Multiplication Factor p", 
   bit1-3="Target y value of the Modulation/Power function", bit0=reserved
18 bit7=1, bit4-6="Recommended Erase/Write Power Ratio (P(inf)/W(inf))"
   bit0-3=reserved
19 reserved
20-22 A2 Values
23 reserved
24-26 A3 Values
27 reserved

Disc Type - zero indicates CD-R media; one indicates CD-RW media.

Disc Sub-Type - shall be set to zero.

A1 - when set to one, indicates that bytes 16-18 are valid.

Lowest Usable CLV Recording Speed
000b Reserved
001b 2X
010b - 111b Reserved

Highest CLV Recording Speeds
000b Reserved
001b 2X
010b 4X
011b 6X
100b 8X
101b - 111b Reserved

MMC-3 seems to recommend MODE SENSE (5Ah) page 2Ah rather than A1, A2, A3.
This page is loaded in libburn function  spc_sense_caps() .
Speed is given in kbytes/sec there. But i suspect this to be independent
of media. So one would habe to associate the speed descriptor blocks with
the ATIP media characteristics ? How ?

*/

ex:;
	BURN_FREE_MEM(buf);
	BURN_FREE_MEM(c);
}


int mmc_eval_read_error(struct burn_drive *d, struct command *c, char *what,
                        int start_m, int start_s, int start_f,
                        int end_m, int end_s, int end_f, int flag)
{
	char *msg = NULL;
	int key, asc, ascq, silent;

	if (!c->error)
		return 0;

	msg = calloc(1, 256);
	if (msg != NULL) {
		if (start_s < 0 || start_f < 0 || end_s < 0 || end_f < 0) {
			sprintf(msg,
		           "SCSI error on %s(%d,%d): ", what, start_m, end_m);
		} else {
			sprintf(msg, "SCSI error on %s(%dm%ds%df,%dm%ds%df): ",
			      what,
			      start_m, start_s, start_f, end_m, end_s, end_f);
		}
		scsi_error_msg(d, c->sense, 14, msg + strlen(msg),
				&key, &asc, &ascq);
		silent = (d->silent_on_scsi_error == 1);
		if (key == 5 && asc == 0x64 && ascq == 0x0) {
			d->had_particular_error |= 1;
			silent = 1;
		}
		if(!silent)
			libdax_msgs_submit(libdax_messenger,
				d->global_index,
				0x00020144,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
		free(msg);
	}
	return BE_CANCELLED;
}


/* ts B21119 : Derived from older mmc_read_sectors() 
   @param flag bit0= set DAP bit (also with o->dap_bit)
*/
int mmc_read_cd_msf(struct burn_drive *d,
		int start_m, int start_s, int start_f,
		int end_m, int end_s, int end_f,
		int sec_type, int main_ch,
		const struct burn_read_opts *o, struct buffer *buf, int flag)
{
	int req, ret, dap_bit;
	int report_recovered_errors = 0, subcodes_audio = 0, subcodes_data = 0;
	struct command *c;

	c = &(d->casual_command);
	mmc_start_if_needed(d, 0);
	if (mmc_function_spy(d, "mmc_read_cd_msf") <= 0)
		return -1;

	dap_bit = flag & 1;
	if (o != NULL) {
		report_recovered_errors = o->report_recovered_errors;
		subcodes_audio = o->subcodes_audio;	
		subcodes_data = o->subcodes_data;
		dap_bit |= o->dap_bit;
	}

	scsi_init_command(c, MMC_READ_CD_MSF, sizeof(MMC_READ_CD_MSF));
	c->retry = 1;
	c->opcode[1] = ((sec_type & 7) << 2) | ((!!dap_bit) << 1);
	c->opcode[3] = start_m;
	c->opcode[4] = start_s;
	c->opcode[5] = start_f;
	c->opcode[6] = end_m;
	c->opcode[7] = end_s;
	c->opcode[8] = end_f;

	req = main_ch & 0xf8;

	/* ts A61106 : LG GSA-4082B dislikes this. key=5h asc=24h ascq=00h

	if (d->busy == BURN_DRIVE_GRABBING || report_recovered_errors)
		req |= 2;
	*/
	c->opcode[9] = req;

	c->opcode[10] = 0;
/* always read the subcode, throw it away later, since we don't know
   what we're really reading
*/
/* >>> ts B21125 : This is very obscure:
                   MMC-3 has sub channel selection 001b as "RAW"
                   MMC-5 does neither mention 001b nor "RAW".
                   And why should a non-grabbed drive get here ?
*/
	if (d->busy == BURN_DRIVE_GRABBING || subcodes_audio || subcodes_data)
		c->opcode[10] = 1;

/* <<< ts B21125 : test with sub channel selection 100b
                   no data, only sub channel
	c->opcode[9] = 0;
	c->opcode[10] = 4;
	Did not help either with reading before LBA -150
*/
/* <<< ts B21125 : test with sub channel selection 001b and no user data
	c->opcode[9] = 0;
	c->opcode[10] = 1;
*/

	c->page = buf;
	c->dir = FROM_DRIVE;
	d->issue_command(d, c);
	ret = mmc_eval_read_error(d, c, "read_cd_msf",
	                          start_m, start_s, start_f,
				  end_m, end_s, end_f, 0);
	return ret;
}


/* ts B21119 : Derived from older mmc_read_sectors() 
   @param flag bit0= set DAP bit (also with o->dap_bit)
*/
int mmc_read_cd(struct burn_drive *d, int start, int len,
                int sec_type, int main_ch,
		const struct burn_read_opts *o, struct buffer *buf, int flag)
{
	int temp, req, ret, dap_bit;
	int report_recovered_errors = 0, subcodes_audio = 0, subcodes_data = 0;
	struct command *c;

/* # define Libburn_read_cd_by_msF 1 */
#ifdef Libburn_read_cd_by_msF

	int start_m, start_s, start_f, end_m, end_s, end_f;

	burn_lba_to_msf(start, &start_m, &start_s, &start_f);
	burn_lba_to_msf(start + len, &end_m, &end_s, &end_f);
	ret = mmc_read_cd_msf(d, start_m, start_s, start_f,
	                      end_m, end_s, end_f,
	                      sec_type, main_ch, o, buf, flag);
	return ret;

#endif /* Libburn_read_cd_by_msF */

	c = &(d->casual_command);
	mmc_start_if_needed(d, 0);
	if (mmc_function_spy(d, "mmc_read_cd") <= 0)
		return -1;

	dap_bit = flag & 1;
	if (o != NULL) {
		report_recovered_errors = o->report_recovered_errors;
		subcodes_audio = o->subcodes_audio;	
		subcodes_data = o->subcodes_data;
		dap_bit |= o->dap_bit;
	}

	scsi_init_command(c, MMC_READ_CD, sizeof(MMC_READ_CD));
	c->retry = 1;
	c->opcode[1] = ((sec_type & 7) << 2) | ((!!dap_bit) << 1);
	temp = start;
	c->opcode[5] = temp & 0xFF;
	temp >>= 8;
	c->opcode[4] = temp & 0xFF;
	temp >>= 8;
	c->opcode[3] = temp & 0xFF;
	temp >>= 8;
	c->opcode[2] = temp & 0xFF;
	c->opcode[8] = len & 0xFF;
	len >>= 8;
	c->opcode[7] = len & 0xFF;
	len >>= 8;
	c->opcode[6] = len & 0xFF;
	req = main_ch & 0xf8;

	/* ts A61106 : LG GSA-4082B dislikes this. key=5h asc=24h ascq=00h

	if (d->busy == BURN_DRIVE_GRABBING || report_recovered_errors)
		req |= 2;
	*/

	c->opcode[9] = req;
	c->opcode[10] = 0;
/* always read the subcode, throw it away later, since we don't know
   what we're really reading
*/
/* >>> ts B21125 : This is very obscure:
                   MMC-3 has sub channel selection 001b as "RAW"
                   MMC-5 does neither mention 001b nor "RAW".
                   And why should a non-grabbed drive get here ?
*/
	if (d->busy == BURN_DRIVE_GRABBING || subcodes_audio || subcodes_data)
		c->opcode[10] = 1;

/* <<< ts B21125 : test with sub channel selection 100b
	c->opcode[10] = 4;
*/
/* <<< ts B21125 : test with sub channel selection 001b and no user data
	c->opcode[9] = 0;
	c->opcode[10] = 1;
*/

	c->page = buf;
	c->dir = FROM_DRIVE;
	d->issue_command(d, c);
	ret = mmc_eval_read_error(d, c, "read_cd", start, -1, -1,
	                          len, -1, -1, 0);
	return ret;
}

void mmc_erase(struct burn_drive *d, int fast)
{
	struct command *c;

	c = &(d->casual_command);
	mmc_start_if_needed(d, 0);
	if (mmc_function_spy(d, "mmc_erase") <= 0)
		return;

	scsi_init_command(c, MMC_BLANK, sizeof(MMC_BLANK));
	c->opcode[1] = 16;	/* IMMED set to 1 */
	c->opcode[1] |= !!fast;
	c->retry = 1;
	c->page = NULL;
	c->dir = NO_TRANSFER;
	c->timeout = Libburn_mmc_blank_timeouT;
	d->issue_command(d, c);
}

void mmc_read_lead_in(struct burn_drive *d, struct buffer *buf)
{
	struct command *c;

	c = &(d->casual_command);
	mmc_start_if_needed(d, 0);
	if (mmc_function_spy(d, "mmc_read_lead_in") <= 0)
		return;

	scsi_init_command(c, MMC_READ_CD, sizeof(MMC_READ_CD));
	c->retry = 1;
	c->opcode[5] = 0;
	c->opcode[4] = 0;
	c->opcode[3] = 0;
	c->opcode[2] = 0xF0;
	c->opcode[8] = 1;
	c->opcode[7] = 0;
	c->opcode[6] = 0;
	c->opcode[9] = 0;
	c->opcode[10] = 2;
	c->page = buf;
	c->dir = FROM_DRIVE;
	d->issue_command(d, c);
}

void mmc_perform_opc(struct burn_drive *d)
{
	struct command *c;

	c = &(d->casual_command);
	mmc_start_if_needed(d, 0);
	if (mmc_function_spy(d, "mmc_perform_opc") <= 0)
		return;

	scsi_init_command(c, MMC_SEND_OPC, sizeof(MMC_SEND_OPC));
	c->retry = 1;
	c->opcode[1] = 1;
	c->page = NULL;
	c->dir = NO_TRANSFER;
	c->timeout = Libburn_mmc_opc_timeouT;
	d->issue_command(d, c);
}


/* ts A61221 : Learned much from dvd+rw-tools-7.0 set_speed_B6h() but then
   made own experiments on base of mmc5r03c.pdf 6.8.3 and 6.39 in the hope
   to achieve a leaner solution
   ts A70712 : That leaner solution does not suffice for my LG GSA-4082B.
   Meanwhile there is a speed descriptor list anyway.
*/
int mmc_set_streaming(struct burn_drive *d,
			 int r_speed, int w_speed, int end_lba)
{
	struct buffer *buf = NULL;
	struct command *c = NULL;
	int b, eff_end_lba, ret;
	char *msg = NULL;
	unsigned char *pd;
	int key, asc, ascq;

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	BURN_ALLOC_MEM(c, struct command, 1);
	BURN_ALLOC_MEM(msg, char, 256);
	mmc_start_if_needed(d, 1);
	if (mmc_function_spy(d, "mmc_set_streaming") <= 0)
		{ret = 0; goto ex;}

	scsi_init_command(c, MMC_SET_STREAMING, sizeof(MMC_SET_STREAMING));
	c->retry = 1;
	c->page = buf;
	c->page->bytes = 28;
	c->opcode[9] = (c->page->bytes >> 8) & 0xff;
	c->opcode[10] = c->page->bytes & 0xff;
	c->page->sectors = 0;
	c->dir = TO_DRIVE;
	memset(c->page->data, 0, c->page->bytes);
	pd = c->page->data;

	pd[0] = 0; /* WRC=0 (Default Rotation Control), RDD=Exact=RA=0 */

	if (w_speed == 0)
		w_speed = 0x10000000; /* ~ 2 TB/s */
	else if (w_speed < 0)
		w_speed = 177; /* 1x CD */
	if (r_speed == 0)
		r_speed = 0x10000000; /* ~ 2 TB/s */
	else if (r_speed < 0)
		r_speed = 177; /* 1x CD */
	if (end_lba == 0) {
		/* Default computed from 4.7e9 */
		eff_end_lba = 2294921 - 1;
		if (d->mdata->max_end_lba > 0)
			eff_end_lba = d->mdata->max_end_lba - 1;
	} else
		eff_end_lba = end_lba;

	sprintf(msg, "mmc_set_streaming: end_lba=%d ,  r=%d ,  w=%d",
		eff_end_lba, r_speed, w_speed);
	libdax_msgs_submit(libdax_messenger, d->global_index, 0x00000002,
			   LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
			   msg, 0, 0);

	/* start_lba is 0 , 1000 = 1 second as base time for data rate */
	for (b = 0; b < 4 ; b++) {
		pd[8+b] = (eff_end_lba >> (24 - 8 * b)) & 0xff;
		pd[12+b] = (r_speed >> (24 - 8 * b)) & 0xff;
		pd[16+b] = (1000 >> (24 - 8 * b)) & 0xff;
		pd[20+b] = (w_speed >> (24 - 8 * b)) & 0xff;
		pd[24+b] = (1000 >> (24 - 8 * b)) & 0xff;
	}

/* <<<
	fprintf(stderr,"LIBBURN_EXPERIMENTAL : B6h Performance descriptor:\n");
	for (b = 0; b < 28 ; b++)
		fprintf(stderr, "%2.2X%c", pd[b], ((b+1)%4 ? ' ' : '\n'));
*/

	
	d->issue_command(d, c);
	if (c->error) {
		spc_decode_sense(c->sense, 0, &key, &asc, &ascq);
		if (key != 0 && !d->silent_on_scsi_error) {
			sprintf(msg,
				"SCSI error on set_streaming(%d): ", w_speed);
			scsi_error_msg(d, c->sense, 14, msg + strlen(msg), 
					&key, &asc, &ascq);
		}
		{ret = 0; goto ex;}
	}
	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	BURN_FREE_MEM(c);
	BURN_FREE_MEM(buf);
	return ret;
}


void mmc_set_speed(struct burn_drive *d, int r, int w)
{
	struct command *c;
	int ret, end_lba = 0;
	struct burn_speed_descriptor *best_sd = NULL;

	c = &(d->casual_command);
	mmc_start_if_needed(d, 1);
	if (mmc_function_spy(d, "mmc_set_speed") <= 0)
		return;

	if (r <= 0 || w <= 0) {
		/* ts A70712 : now searching for best speed descriptor */
		if (w > 0 && r <= 0) 
			burn_drive_get_best_speed(d, r, &best_sd, 1 | 2);
		else
			burn_drive_get_best_speed(d, w, &best_sd, 2);
		if (best_sd != NULL) {
			w = best_sd->write_speed;
			d->nominal_write_speed = w;
			r = best_sd->read_speed;
			end_lba = best_sd->end_lba;
		}
	}

	/* A70711 */
	d->nominal_write_speed = w;

	/* ts A61221 : try to set DVD speed via command B6h */
	if (strstr(d->current_profile_text, "DVD") == d->current_profile_text
	    ||
	    strstr(d->current_profile_text, "BD") == d->current_profile_text) {
		ret = mmc_set_streaming(d, r, w, end_lba);
		if (ret != 0)
			return; /* success or really fatal failure */ 
	}

	/* ts A61112 : MMC standards prescribe FFFFh as max speed.
			But libburn.h prescribes 0.
	   ts A70715 : <0 now means minimum speed */
	if (r == 0 || r > 0xffff)
		r = 0xffff;
	else if (r < 0)
		r = 177; /* 1x CD */
	if (w == 0 || w > 0xffff)
		w = 0xffff;
	else if (w < 0)
		w = 177; /* 1x CD */

	scsi_init_command(c, MMC_SET_SPEED, sizeof(MMC_SET_SPEED));
	c->retry = 1;
	c->opcode[2] = r >> 8;
	c->opcode[3] = r & 0xFF;
	c->opcode[4] = w >> 8;
	c->opcode[5] = w & 0xFF;
	c->page = NULL;
	c->dir = NO_TRANSFER;
	d->issue_command(d, c);
}


/* ts A61201 : found in unfunctional state
 */
static int mmc_get_configuration_al(struct burn_drive *d, int *alloc_len)
{
	struct buffer *buf = NULL;
	int len, cp, descr_len = 0, feature_code, only_current = 1, i;
	int old_alloc_len, only_current_profile = 0, key, asc, ascq, ret;
	int feature_is_current;
	unsigned char *descr, *prf, *up_to, *prf_end;
	struct command *c = NULL;
	int phys_if_std = 0;
	char *phys_name = "";

/* Enable this to get loud and repeated reports about the feature set :
 # define Libburn_print_feature_descriptorS 1
*/
#ifdef Libburn_print_feature_descriptorS
	int prf_number;

	only_current = 0;
#endif

	if (*alloc_len < 8)
		{ret = 0; goto ex;}

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	BURN_ALLOC_MEM(c, struct command, 1);
	d->current_profile = 0;
        d->current_profile_text[0] = 0;
	d->current_is_cd_profile = 0;
	d->current_is_supported_profile = 0;
        d->current_is_guessed_profile = 0;
	d->num_profiles = 0;
	d->current_has_feat21h = 0;
	d->current_feat21h_link_size = -1;
	d->current_feat23h_byte4 = 0;
	d->current_feat23h_byte8 = 0;
	d->current_feat2fh_byte4 = -1;

	scsi_init_command(c, MMC_GET_CONFIGURATION,
			 sizeof(MMC_GET_CONFIGURATION));
	c->dxfer_len= *alloc_len;
	c->retry = 1;
	c->opcode[7] = (c->dxfer_len >> 8) & 0xff;
	c->opcode[8] = c->dxfer_len & 0xff;
	c->page = buf;
	c->page->sectors = 0;
	c->page->bytes = 0;
	c->dir = FROM_DRIVE;
	d->issue_command(d, c);

#ifdef Libisofs_simulate_old_mmc1_drivE
	c->error = 1;
	c->sense[0] = 0x70; /* Fixed format sense data */
	c->sense[2] = 0x5;
	c->sense[12] = 0x20;
	c->sense[13] = 0x0;
#endif /* Libisofs_simulate_old_mmc1_drivE */

	if (c->error) {
		/* ts A90603 : MMC-1 drive do not know 46h GET CONFIGURATION */
		spc_decode_sense(c->sense, 0, &key, &asc, &ascq);
		if (key == 0x5 && asc == 0x20 && ascq == 0x0) {
			d->current_is_guessed_profile = 1;
			/* Will yield a non-zero profile only after
			   mmc_read_disc_info_al() was called */
			mmc_guess_profile(d, 0);
		}
		{ret = 0; goto ex;}
	}
	old_alloc_len = *alloc_len;
	*alloc_len = len = mmc_four_char_to_int(c->page->data) + 4;
	if (len > old_alloc_len)
		len = old_alloc_len;
	if (len < 8 || len > 4096)
		{ret = 0; goto ex;}
	cp = (c->page->data[6]<<8) | c->page->data[7];

#ifdef Libburn_rom_as_profilE
	if (cp == 0x08 || cp == 0x10 || cp==0x40)
		cp = Libburn_rom_as_profilE;
#endif /* Libburn_rom_as_profilE */

	d->current_profile = cp;
	strcpy(d->current_profile_text, mmc_obtain_profile_name(cp));

	/* Read-only supported media */

	if (cp == 0x08) /* CD-ROM */
		d->current_is_supported_profile = d->current_is_cd_profile = 1;
	if (cp == 0x10) /* DVD-ROM */
		d->current_is_supported_profile = 1;
	if (cp == 0x40) /* BD-ROM */
		d->current_is_supported_profile = 1;

#ifdef Libburn_support_bd_r_readonlY
#ifndef Libburn_support_bd_plus_r_srM
	if (cp == 0x41) /* BD-R sequential (here as read-only dummy) */
		d->current_is_supported_profile = 1;
#endif
	if (cp == 0x42) /* BD-R random recording */
		d->current_is_supported_profile = 1;
#endif


	/* Write supported media (they get declared suitable in
	                          burn_disc_get_multi_caps) */

	if (cp == 0x09 || cp == 0x0a)
		d->current_is_supported_profile = d->current_is_cd_profile = 1;

#ifdef Libburn_support_dvd_plus_rW
	if (cp == 0x1a)
		d->current_is_supported_profile = 1;
#endif
#ifdef Libburn_support_dvd_minusrw_overW
	if (cp == 0x13)
		d->current_is_supported_profile = 1;
#endif
#ifdef Libburn_support_dvd_raM
	if (cp == 0x12 || cp == 0x43) {                  /* DVD-RAM , BD-RE */
		d->current_is_supported_profile = 1;

#ifdef Libburn_dvd_ram_as_bd_rE
		cp = d->current_profile = 0x43;
		strcpy(d->current_profile_text, mmc_obtain_profile_name(cp));
#endif

	}
#endif
#ifdef Libburn_support_dvd_r_seQ
	if (cp == 0x11 || cp == 0x14) /* DVD-R, DVD-RW */
		d->current_is_supported_profile = 1;
	if (cp == 0x15) /* DVD-R/DL */
		d->current_is_supported_profile = 1;
#endif
#ifdef Libburn_support_dvd_plus_R
	if (cp == 0x1b || cp == 0x2b) /* DVD+R , DVD+R/DL */
		d->current_is_supported_profile = 1;
#endif
#ifdef Libburn_support_bd_plus_r_srM
	if (cp == 0x41) /* BD-R SRM */
		d->current_is_supported_profile = 1;
#endif

	/* ts A70127 : Interpret list of profile and feature descriptors.
 	see mmc5r03c.pdf 5.2
	>>> Ouch: What to do if list is larger than buffer size.
	          Specs state that the call has to be repeated.
	*/
	up_to = c->page->data + (len < BUFFER_SIZE ? len : BUFFER_SIZE);

#ifdef Libburn_print_feature_descriptorS
	fprintf(stderr,
	"-----------------------------------------------------------------\n");
	fprintf(stderr,
	  "LIBBURN_EXPERIMENTAL : feature list length = %d , shown = %d\n",
		len, (int) (up_to - c->page->data));
#endif /* Libburn_print_feature_descriptorS */

	for (descr = c->page->data + 8; descr + 3 < up_to; descr += descr_len){
		descr_len = 4 + descr[3];
		feature_code = (descr[0] << 8) | descr[1];
		feature_is_current = descr[2] & 1;
		if (only_current && !feature_is_current)
	continue;

#ifdef Libburn_print_feature_descriptorS
		fprintf(stderr,
			"LIBBURN_EXPERIMENTAL : %s feature %4.4Xh :",
			(descr[2] & 1) ? "+" : "-",
			feature_code);
		if (feature_code != 0x00)
			for (i = 2; i < descr_len; i++)
				fprintf(stderr, " %2.2X", descr[i]);
		fprintf(stderr, "\n");
#endif /* Libburn_print_feature_descriptorS */

		if (feature_code == 0x0) {
			prf_end = descr + 4 + descr[3];
			d->num_profiles = descr[3] / 4;
			if (d->num_profiles > 64)
				d->num_profiles = 64;
			if (d->num_profiles > 0)
				memcpy(d->all_profiles, descr + 4,
							d->num_profiles * 4);
			for (prf = descr + 4; prf + 2 < prf_end; prf += 4) {
				if (only_current_profile && !(prf[2] & 1))
			continue;

#ifdef Libburn_print_feature_descriptorS
				prf_number =  (prf[0] << 8) | prf[1];
				fprintf(stderr,
			"LIBBURN_EXPERIMENTAL :   %s profile %4.4Xh  \"%s\"\n",
					prf[2] & 1 ? "+" : "-",
					prf_number,
					mmc_obtain_profile_name(prf_number));
#endif /* Libburn_print_feature_descriptorS */

			}

		} else if (feature_code == 0x21) {

			d->current_has_feat21h = feature_is_current;
			for (i = 0; i < descr[7]; i++) {
				if (i == 0 || descr[8 + i] == 16)
					d->current_feat21h_link_size = 
								descr[8 + i];

#ifdef Libburn_print_feature_descriptorS
				fprintf(stderr,
				"LIBBURN_EXPERIMENTAL :   + Link Size = %d\n",
					descr[8 + i]);
#endif /* Libburn_print_feature_descriptorS */

			}

		} else if (feature_code == 0x23) {
			if (feature_is_current) {
				d->current_feat23h_byte4 = descr[4];
				d->current_feat23h_byte8 = descr[8];
			}
#ifdef Libburn_print_feature_descriptorS
			if (cp >= 0x41 && cp <= 0x43) 
				fprintf(stderr,
			"LIBBURN_EXPERIMENTAL : BD formats: %s%s%s%s%s\n",
					descr[4] & 1 ? " Cert" : "",
					descr[4] & 2 ? " QCert" : "",
					descr[4] & 4 ? " Expand" : "",
					descr[4] & 8 ? " RENoSA" : "",
					descr[8] & 1 ? " RRM" : "");
#endif /* Libburn_print_feature_descriptorS */

		} else if (feature_code == 0x2F) {
			if (feature_is_current)
				d->current_feat2fh_byte4 = descr[4];

#ifdef Libburn_print_feature_descriptorS
			fprintf(stderr, "LIBBURN_EXPERIMENTAL :     BUF = %d , Test Write = %d , DVD-RW = %d\n",
				!!(descr[4] & 64), !!(descr[4] & 4),
				!!(descr[4] & 2));
#endif /* Libburn_print_feature_descriptorS */
			
		} else if (feature_code == 0x01) {
			phys_if_std = (descr[4] << 24) | (descr[5] << 16) |
					(descr[6] << 8) | descr[7];
			if (phys_if_std == 1)
				phys_name = "SCSI Family";
			else if(phys_if_std == 2)
				phys_name = "ATAPI";
			else if(phys_if_std == 3 || phys_if_std == 4 ||
				 phys_if_std == 6)
				phys_name = "IEEE 1394 FireWire";
			else if(phys_if_std == 7)
				phys_name = "Serial ATAPI";
			else if(phys_if_std == 8)
				phys_name = "USB";
			
			d->phys_if_std = phys_if_std;
			strcpy(d->phys_if_name, phys_name);

#ifdef Libburn_print_feature_descriptorS

			fprintf(stderr,
	"LIBBURN_EXPERIMENTAL :     Phys. Interface Standard %Xh \"%s\"\n",
				phys_if_std, phys_name);

		} else if (feature_code == 0x107) {

			fprintf(stderr, "LIBBURN_EXPERIMENTAL :     CD SPEED = %d , page 2Ah = %d , SET STREAMING = %d\n",
				!!(descr[4] & 8), !!(descr[4] & 4),
				!!(descr[4] & 2));

		} else if (feature_code == 0x108 || feature_code == 0x10c) {
			int i, c_limit;

			fprintf(stderr, "LIBBURN_EXPERIMENTAL :     %s = ", 
				feature_code == 0x108 ? 
				"Drive Serial Number" : "Drive Firmware Date");
			c_limit = descr[3] - 2 * (feature_code == 0x10c);
			for (i = 0; i < c_limit; i++)
				if (descr[4 + i] < 0x20 || descr[4 + i] > 0x7e
					|| descr[4 + i] == '\\')
					fprintf(stderr,"\\%2.2X",descr[4 + i]);
				else
					fprintf(stderr, "%c", descr[4 + i]);
			fprintf(stderr, "\n");

#endif /* Libburn_print_feature_descriptorS */

		}
	}
	ret = 1;
ex:
	BURN_FREE_MEM(buf);
	BURN_FREE_MEM(c);
	return ret;
}


void mmc_get_configuration(struct burn_drive *d)
{
	int alloc_len = 8, ret;

	mmc_start_if_needed(d, 1);
	if (mmc_function_spy(d, "mmc_get_configuration") <= 0)
		return;

	/* first command execution to learn Allocation Length */
	ret = mmc_get_configuration_al(d, &alloc_len);
/*
	fprintf(stderr,"LIBBURN_DEBUG: 46h alloc_len = %d , ret = %d\n",
			alloc_len, ret);
*/
	if (alloc_len > 8 && ret > 0)
		/* second execution with announced length */
		mmc_get_configuration_al(d, &alloc_len);
}


/* ts A70108 */
/* mmc5r03c.pdf 6.24 */
static int mmc_read_format_capacities_al(struct burn_drive *d,
					int *alloc_len, int top_wanted)
{
	struct buffer *buf = NULL;
	int len, type, score, num_descr, max_score = -2000000000, i, sign = 1;
	int old_alloc_len, ret;
	off_t size, num_blocks;
	struct command *c = NULL;
	unsigned char *dpt;

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	BURN_ALLOC_MEM(c, struct command, 1);
	if (*alloc_len < 4)
		{ret = 0; goto ex;}

	d->format_descr_type = 3;
	d->format_curr_max_size = 0;
	d->format_curr_blsas = 0;
	d->best_format_type = -1;
	d->best_format_size = 0;

	scsi_init_command(c, MMC_READ_FORMAT_CAPACITIES,
			 sizeof(MMC_READ_FORMAT_CAPACITIES));
	c->dxfer_len = *alloc_len;
	c->retry = 1;
	c->opcode[7]= (c->dxfer_len >> 8) & 0xff;
	c->opcode[8]= c->dxfer_len & 0xff;
	c->page = buf;
	c->page->sectors = 0;
	c->page->bytes = 0;
	c->dir = FROM_DRIVE;

	d->issue_command(d, c);
	if (c->error)
		{ret = 0; goto ex;}

	len = c->page->data[3];
	old_alloc_len = *alloc_len;
	*alloc_len = len + 4;
	if (old_alloc_len < 12)
		{ret = 1; goto ex;}
	if (len + 4 > old_alloc_len)
		len = old_alloc_len - 4;
	if (len < 8)
		{ret = 0; goto ex;}

	dpt = c->page->data + 4;
	/* decode 6.24.3.2 Current/Maximum Capacity Descriptor */
	d->format_descr_type = dpt[4] & 3;
	d->format_curr_max_size = (((off_t) dpt[0]) << 24)
		 		  + (dpt[1] << 16) + (dpt[2] << 8) + dpt[3];
	if (d->format_descr_type == BURN_FORMAT_IS_UNKNOWN)
		d->format_curr_max_size = 0;
	d->format_curr_blsas = (dpt[5] << 16) + (dpt[6] << 8) + dpt[7];
	d->format_curr_max_size *= (off_t) 2048;
	if((d->current_profile == 0x12 || d->current_profile == 0x43)
	   && d->media_capacity_remaining == 0) {
		burn_drive_set_media_capacity_remaining(d,
						d->format_curr_max_size);
		d->media_lba_limit = d->format_curr_max_size / 2048;
	}


#ifdef Libburn_dvd_ram_as_bd_rE
	/* <<< dummy format descriptor list as obtained from
	       dvd+rw-mediainfo by Giulio Orsero in April 2008
	*/
	d->num_format_descr = 5;
	d->format_descriptors[0].type = 0x00;
	d->format_descriptors[0].size = (off_t) 11826176 * (off_t) 2048;
	d->format_descriptors[0].tdp = 0x3000;
	d->format_descriptors[1].type = 0x30;
	d->format_descriptors[1].size = (off_t) 11826176 * (off_t) 2048;
	d->format_descriptors[1].tdp = 0x3000;
	d->format_descriptors[2].type = 0x30;
	d->format_descriptors[2].size = (off_t) 11564032 * (off_t) 2048;
	d->format_descriptors[2].tdp = 0x5000;
	d->format_descriptors[3].type = 0x30;
	d->format_descriptors[3].size = (off_t) 12088320 * (off_t) 2048;
	d->format_descriptors[3].tdp = 0x1000;
	d->format_descriptors[4].type = 0x31;
	d->format_descriptors[4].size = (off_t) 12219392 * (off_t) 2048;
	d->format_descriptors[4].tdp = 0x800;
	d->best_format_type = 0x00;
	d->best_format_size = (off_t) 11826176 * (off_t) 2048;

	/* silencing compiler warnings about unused variables */
	num_blocks = size = sign = i = max_score = num_descr = score = type = 0;

	if (d->current_profile == 0x12 || d->current_profile == 0x43)
		{ret = 1; goto ex;}
	d->num_format_descr = 0;

#endif /* Libburn_dvd_ram_as_bd_rE */

	if (top_wanted == 0x00 || top_wanted == 0x10)
		sign = -1; /* the caller clearly desires full format */

	/* 6.24.3.3 Formattable Capacity Descriptors */
	num_descr = (len - 8) / 8;
	for (i = 0; i < num_descr; i++) {
		dpt = c->page->data + 12 + 8 * i;
		num_blocks = mmc_four_char_to_int(dpt);
		size = num_blocks * (off_t) 2048;
		type = dpt[4] >> 2;

		if (i < 32) {
			d->format_descriptors[i].type = type;
			d->format_descriptors[i].size = size;
			d->format_descriptors[i].tdp =
				(dpt[5] << 16) + (dpt[6] << 8) + dpt[7];
			d->num_format_descr = i + 1;
		}
		/* Criterion is proximity to quick intermediate state */
		if (type == 0x00) { /* full format (with lead out) */
			score = 1 * sign;
		} else if (type == 0x10) { /* DVD-RW full format */
			score = 10 * sign;
		} else if(type == 0x13) { /* DVD-RW quick grow last session */
			score = 100 * sign;
		} else if(type == 0x15) { /* DVD-RW Quick */
			score = 50 * sign;
			if(d->current_profile == 0x13) {
				burn_drive_set_media_capacity_remaining(d,
									size);
				d->media_lba_limit = num_blocks;
			}
		} else if(type == 0x26) { /* DVD+RW */
			score = 1 * sign;
			burn_drive_set_media_capacity_remaining(d, size);
			d->media_lba_limit = num_blocks;
		} else {
	continue;
		}
		if (type == top_wanted)
			score += 1000000000;
		if (score > max_score) {
			d->best_format_type = type;
			d->best_format_size = size;
			max_score = score;
		}
	}
	ret = 1;
ex:
	BURN_FREE_MEM(buf);
	BURN_FREE_MEM(c);
	return ret;
}


int mmc_read_format_capacities(struct burn_drive *d, int top_wanted)
{
	int alloc_len = 4, ret;

	mmc_start_if_needed(d, 1);
	if (mmc_function_spy(d, "mmc_read_format_capacities") <= 0)
		return 0;

	ret = mmc_read_format_capacities_al(d, &alloc_len, top_wanted);
/*
	fprintf(stderr,"LIBBURN_DEBUG: 23h alloc_len = %d , ret = %d\n",
		 alloc_len, ret);
*/
	if (alloc_len >= 12 && ret > 0)
		ret = mmc_read_format_capacities_al(d, &alloc_len, top_wanted);

	return ret;
}


void mmc_sync_cache(struct burn_drive *d)
{
	struct command *c = NULL;
	char *msg = NULL;
	int key, asc, ascq;

	if (mmc_function_spy(d, "mmc_sync_cache") <= 0)
		goto ex;

	BURN_ALLOC_MEM_VOID(c, struct command, 1);
	BURN_ALLOC_MEM_VOID(msg, char, 256);

	scsi_init_command(c, MMC_SYNC_CACHE, sizeof(MMC_SYNC_CACHE));
	c->retry = 1;
	c->opcode[1] |= 2; /* ts A70918 : Immed */
	c->page = NULL;
	c->dir = NO_TRANSFER;
	c->timeout = Libburn_mmc_sync_timeouT;

	libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			   LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
			   "syncing cache", 0, 0);
	if(d->wait_for_buffer_free) {
		sprintf(msg,
			"Checked buffer %u times. Waited %u+%u times = %.3f s",
			d->pessimistic_writes, d->waited_writes,
			d->waited_tries - d->waited_writes,
			((double) d->waited_usec) / 1.0e6);
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x0002013f,
				LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_LOW,
				msg, 0,0);
	}

	d->issue_command(d, c);

	/* ts A70918 */
	if (c->error) {
		sprintf(msg, "Failed to synchronize drive cache");
		sprintf(msg + strlen(msg), ". SCSI error : ");
		scsi_error_msg(d, c->sense, 14, msg + strlen(msg), 
					&key, &asc, &ascq);
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x0002017f,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
		d->cancel = 1;
		goto ex;
	}

	if (spc_wait_unit_attention(d, 3600, "SYNCHRONIZE CACHE", 0) <= 0)
		d->cancel = 1;
	else
		d->needs_sync_cache = 0;
ex:
	BURN_FREE_MEM(msg);
	BURN_FREE_MEM(c);
}


/* ts A61023 : http://libburn.pykix.org/ticket/14
               get size and free space of drive buffer
*/
int mmc_read_buffer_capacity(struct burn_drive *d)
{
	struct buffer *buf = NULL;
	struct command *c = NULL;
	unsigned char *data;
	int alloc_len = 12, ret;

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	BURN_ALLOC_MEM(c, struct command, 1);
	if (mmc_function_spy(d, "mmc_read_buffer_capacity") <= 0)
		{ret = 0; goto ex;}

	scsi_init_command(c, MMC_READ_BUFFER_CAPACITY,
			 sizeof(MMC_READ_BUFFER_CAPACITY));
	c->dxfer_len = alloc_len;
	c->opcode[7] = (c->dxfer_len >> 8) & 0xff;
	c->opcode[8] = c->dxfer_len & 0xff;
	c->retry = 1;
	c->page = buf;
	memset(c->page->data, 0, alloc_len);
	c->page->bytes = 0;
	c->page->sectors = 0;

	c->dir = FROM_DRIVE;
	d->issue_command(d, c);

	/* >>> ??? error diagnostics */
	if (c->error)
		{ret = 0; goto ex;}

	data = c->page->data;

	d->progress.buffer_capacity =
			(data[4]<<24)|(data[5]<<16)|(data[6]<<8)|data[7];
	d->progress.buffer_available =
			(data[8]<<24)|(data[9]<<16)|(data[10]<<8)|data[11];
	d->pessimistic_buffer_free = d->progress.buffer_available;
	d->pbf_altered = 0;
	if (d->progress.buffered_bytes >= d->progress.buffer_capacity){
		double fill;

		fill = d->progress.buffer_capacity
			      - d->progress.buffer_available;
		if (fill < d->progress.buffer_min_fill && fill>=0)
			d->progress.buffer_min_fill = fill;
	}
	ret = 1;
ex:;
	BURN_FREE_MEM(c);
	BURN_FREE_MEM(buf);
	return ret;
}


/* ts A61219 : learned much from dvd+rw-tools-7.0: plus_rw_format()
               and mmc5r03c.pdf, 6.5 FORMAT UNIT */
/*
   @param size The size (in bytes) to be sent with the FORMAT comand
   @param flag bit1+2: size mode
                 0 = use parameter size as far as it makes sense
                 1 = insist in size 0 even if there is a better default known
                 2 = without bit7: format to maximum available size
                     with bit7   : take size from indexed format descriptor
                 3 = format to default size
               bit3= expand format up to at least size
               bit4= enforce re-format of (partly) formatted media
               bit5= try to disable eventual defect management
               bit6= try to avoid lengthy media certification
               bit7= bit8 to bit15 contain the index of the format to use
               bit8-bit15 = see bit7
              bit16= enable POW on blank BD-R
*/
int mmc_format_unit(struct burn_drive *d, off_t size, int flag)
{
	struct buffer *buf = NULL;
	struct command *c = NULL;
	int ret, tolerate_failure = 0, return_immediately = 0, i, format_type;
	int index, format_sub_type = 0, format_00_index, size_mode;
	int accept_count = 0;
	off_t num_of_blocks = 0, diff, format_size, i_size, format_00_max_size;
	off_t min_size = -1, max_size = -1;
	char *msg = NULL, descr[80];
	int key, asc, ascq;
	int full_format_type = 0x00; /* Full Format (or 0x10 for DVD-RW ?) */

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	BURN_ALLOC_MEM(c, struct command, 1);
	BURN_ALLOC_MEM(msg, char, 256);
	mmc_start_if_needed(d, 0);
	if (mmc_function_spy(d, "mmc_format_unit") <= 0)
		{ret = 0; goto ex;}
	size_mode = (flag >> 1) & 3;

	scsi_init_command(c, MMC_FORMAT_UNIT, sizeof(MMC_FORMAT_UNIT));
	c->retry = 1;
	c->page = buf;
	c->page->bytes = 12;
	c->page->sectors = 0;
	c->dir = TO_DRIVE;
	c->timeout = Libburn_mmc_blank_timeouT;
	memset(c->page->data, 0, c->page->bytes);

	descr[0] = 0;
	c->page->data[1] = 0x02;                  /* Immed */
	c->page->data[3] = 8;                     /* Format descriptor length */
	num_of_blocks = size / 2048;
	mmc_int_to_four_char(c->page->data + 4, num_of_blocks);

	if (flag & 128) { /* explicitely chosen format descriptor */
		/* use case: the app knows what to do */

		ret = mmc_read_format_capacities(d, -1);
		if (ret <= 0)
			goto selected_not_suitable;
		index = (flag >> 8) & 0xff;
		if(index < 0 || index >= d->num_format_descr) {
selected_not_suitable:;
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020132,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				"Selected format is not suitable for libburn",
				0, 0);
			{ret = 0; goto ex;}
		}
		if (!(d->current_profile == 0x13 ||
			d->current_profile == 0x14 ||
			d->current_profile == 0x1a ||
			d->current_profile == 0x12 ||
			d->current_profile == 0x41 ||
			d->current_profile == 0x43))
			goto unsuitable_media;
		      
		format_type = d->format_descriptors[index].type;
		if (!(format_type == 0x00 || format_type == 0x01 ||
		      format_type == 0x10 ||
		      format_type == 0x11 || format_type == 0x13 ||
		      format_type == 0x15 || format_type == 0x26 ||
 		      format_type == 0x30 || format_type == 0x31 ||
		      format_type == 0x32))
			goto selected_not_suitable;
		if (flag & 4) {
			num_of_blocks =
				d->format_descriptors[index].size / 2048;
			mmc_int_to_four_char(c->page->data + 4, num_of_blocks);
		}
		if (format_type != 0x26)
			for (i = 0; i < 3; i++)
				 c->page->data[9 + i] =
					( d->format_descriptors[index].tdp >>
					  (16 - 8 * i)) & 0xff;
		if (format_type == 0x30 || format_type == 0x31) {
			format_sub_type = 0;
			if (flag & 64) {
				if (d->current_feat23h_byte4 & 2)
					/* Quick certification */
					format_sub_type = 3;
			} else {
				if (d->current_feat23h_byte4 & 1)
					/* Full certification */
					format_sub_type = 2;
			}
		} else if (format_type == 0x32 ||
		         (format_type == 0x00 && d->current_profile == 0x41)) {
			if (flag & (1 << 16))
				format_sub_type = 0; /* SRM + POW  */
			else
				format_sub_type = 1; /* SRM  (- POW) */
		}
		if (d->current_profile == 0x12 && format_type !=0x01 &&
		    (flag & 64)) {
			/* DCRT and CmpList, see below */
			c->page->data[1] |= 0x20;
			c->opcode[1] |= 0x08;
		}
		c->page->data[1] |= 0x80; /* FOV = this flag vector is valid */
		sprintf(descr, "%s (descr %d)", d->current_profile_text,index);
		return_immediately = 1; /* caller must do the waiting */

	} else if (d->current_profile == 0x1a) { /* DVD+RW */
		/* use case: background formatting during write     !(flag&4)
	                     de-icing as explicit formatting action (flag&4)
		*/

		/* mmc5r03c.pdf , 6.5.4.2.14, DVD+RW Basic Format */
		format_type = 0x26;

					/* >>> ??? is this "| 8" a bug ? */

		if ((size <= 0 && !(flag & 2)) || (flag & (4 | 8))) {
			/* maximum capacity */
			memset(c->page->data + 4, 0xff, 4); 
			num_of_blocks = 0xffffffff;
		}

		if(d->bg_format_status == 2 ||
			(d->bg_format_status == 3 && !(flag & 16))) {
			sprintf(msg,"FORMAT UNIT ignored. Already %s.",
				(d->bg_format_status == 2 ? "in progress" :
							"completed"));
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020120,
				LIBDAX_MSGS_SEV_NOTE, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0,0);
			{ret = 2; goto ex;}
		}
		if (!(flag & 16))             /* if not re-format is desired */
			if (d->bg_format_status == 1) /* is partly formatted */
				c->page->data[11] = 1;        /* Restart bit */
		sprintf(descr, "DVD+RW (fs=%d,rs=%d)",
			d->bg_format_status, (c->page->data[11] == 1));
		if (flag & 4)
			return_immediately = 1;/* caller must do the waiting */

	} else if (d->current_profile == 0x13 && !(flag & 16)) {
		/*DVD-RW restricted overwrite*/
		/* use case: quick grow formatting during write */

		ret = mmc_read_format_capacities(d, 0x13);
		if (ret > 0) {
			if (d->best_format_type == 0x13) {
				if (d->best_format_size <= 0)
					{ret = 1; goto ex;}
			} else {
				if (d->format_descr_type == 2) /* formatted */
					{ret = 1; goto ex;}
				if (d->format_descr_type == 3){/*intermediate*/
					d->needs_close_session = 1;
					{ret = 1; goto ex;}
				}
				/* does trying make sense at all ? */
				tolerate_failure = 1;
			}
		}
		if (d->best_format_type == 0x13 && (flag & (4 | 8))) {
			num_of_blocks = d->best_format_size / 2048;
			if (flag & 8) {
				/* num_of_blocks needed to reach size */
				diff = (size - d->format_curr_max_size) /32768;
				if ((size - d->format_curr_max_size) % 32768)
					diff++;
				diff *= 16;
				if (diff < num_of_blocks)
					num_of_blocks = diff;
			}
			if (num_of_blocks > 0)
				mmc_int_to_four_char(c->page->data + 4,
							num_of_blocks);
		}
		/* 6.5.4.2.8 , DVD-RW Quick Grow Last Border */
		format_type = 0x13;
		c->page->data[11] = 16;              /* block size * 2k */
		sprintf(descr, "DVD-RW quick grow");

	} else if (d->current_profile == 0x14 ||
			(d->current_profile == 0x13 && (flag & 16))) {
		/* DVD-RW sequential recording (or Overwrite for re-format) */
		/* use case : transition from Sequential to Overwrite
	                      re-formatting of Overwrite media  */

		/* To Restricted Overwrite */
		/*    6.5.4.2.10 Format Type = 15h (DVD-RW Quick) */
		/* or 6.5.4.2.1  Format Type = 00h (Full Format) */
		/* or 6.5.4.2.5  Format Type = 10h (DVD-RW Full Format) */
		mmc_read_format_capacities(d,
					(flag & 4) ? full_format_type : 0x15);
		if (d->best_format_type == 0x15 ||
		    d->best_format_type == full_format_type) {
			if ((flag & 4)
				|| d->best_format_type == full_format_type) {
				num_of_blocks = d->best_format_size / 2048;
				mmc_int_to_four_char(c->page->data + 4,
							num_of_blocks);
			}

		} else {
no_suitable_formatting_type:;
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020131,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				"No suitable formatting type offered by drive",
				0, 0);
			{ret = 0; goto ex;}
		}
		format_type = d->best_format_type;
		sprintf(descr, "DVD-RW %s",
			format_type == 0x15 ? "quick" : "full");
		return_immediately = 1; /* caller must do the waiting */

	} else if (d->current_profile == 0x12) {
		/* ts A80417 : DVD-RAM */
		/*  6.5.4.2.1  Format Type = 00h (Full Format)
		    6.5.4.2.2  Format Type = 01h (Spare Area Expansion)
		*/
		index = format_00_index = -1;
		format_size = format_00_max_size = -1;
		for (i = 0; i < d->num_format_descr; i++) {
			format_type = d->format_descriptors[i].type;
			i_size = d->format_descriptors[i].size;
			if (format_type != 0x00 && format_type != 0x01)
		continue;
			if (flag & 32) { /* No defect mgt */
				/* Search for largest 0x00 format descriptor */
				if (format_type != 0x00)
		continue;
				if (i_size < format_size)
		continue;
				format_size = i_size;
				index = i;
		continue;
			} else if (flag & 4) { /*Max or default size with mgt*/
				/* Search for second largest 0x00
				   format descriptor. For max size allow
				   format type 0x01.
				 */
				if (format_type == 0x00) {
					if (i_size < format_size) 
		continue;
					if (i_size < format_00_max_size) {
						format_size = i_size;
						index = i;
		continue;
					}
					format_size = format_00_max_size;
					index = format_00_index;
					format_00_max_size = i_size;
					format_00_index = i;
		continue;
				}
				if (size_mode==3)
		continue;
				if (i_size > format_size) {
					format_size = i_size;
					index = i;
				}
		continue;
			} 
			/* Search for smallest 0x0 or 0x01
			   descriptor >= size */;
			if (d->format_descriptors[i].size >= size &&
			    (format_size < 0 || i_size < format_size)
			   ) {
				format_size = i_size;
				index = i;
			}
		}
		if(index < 0 && (flag & 4) && !(flag & 32)) {
			format_size = format_00_max_size;
			index = format_00_index;
		}
		if(index < 0)
			goto no_suitable_formatting_type;
		format_type = d->format_descriptors[index].type;
		num_of_blocks = d->format_descriptors[index].size / 2048;
		mmc_int_to_four_char(c->page->data + 4, num_of_blocks);
		for (i = 0; i < 3; i++)
			 c->page->data[9 + i] =
				( d->format_descriptors[index].tdp >>
					  (16 - 8 * i)) & 0xff;
		sprintf(descr, "%s", d->current_profile_text);
		return_immediately = 1; /* caller must do the waiting */
		c->page->data[1] |= 0x80; /* FOV = this flag vector is valid */

		if ((flag & 64) && format_type != 0x01) {
			/* MMC-5 6.5.3.2 , 6.5.4.2.1.2
			   DCRT: Disable Certification and maintain number
			         of blocks
   		           CmpList: Override maintaining of number of blocks
			            with DCRT
			*/
			/* ts A80426 : prevents change of formatted size
		               with PHILIPS SPD3300L and Verbatim 3x DVD-RAM
			       and format_type 0x00. Works on TSSTcorp SH-S203B
			*/
			c->page->data[1] |= 0x20;
			c->opcode[1] |= 0x08;
		}

	} else if (d->current_profile == 0x41) {
		/* BD-R SRM */

		index = -1;
		format_size = -1;
		if (d->num_format_descr <= 0)
			goto no_suitable_formatting_type;
		if (d->format_descriptors[0].type != 0)
			goto no_suitable_formatting_type;
		for (i = 0; i < d->num_format_descr; i++) {
			format_type = d->format_descriptors[i].type;
			i_size = d->format_descriptors[i].size;
			if (format_type != 0x00 && format_type != 0x32)
		continue;
			if (flag & 32) { /* No defect mgt */
				/* ts A81211 : MMC-5 6.5.4.2.17.1
				   When formatted with Format Type 32h,
				   the BD-R disc is required to allocate
				   a non-zero number of spares.
				*/
				goto no_suitable_formatting_type;

			} else if(size_mode == 2) { /* max payload size */
				/* search largest 0x32 format descriptor */
				if(format_type != 0x32)
		continue;
			} else if(size_mode == 3) { /* default payload size */
				if (format_type == 0x00) {
					index = i;
		break;
				}
		continue;
			} else { /* defect managed format with size wish */

#ifdef Libburn_bd_r_format_olD

				/* search for smallest 0x32 >= size */
				if(format_type != 0x32)
		continue;
				if (i_size < size)
		continue;
				if (format_size >= 0 && i_size >= format_size)
		continue;
				index = i;
				format_size = i_size;
		continue;

#else /* Libburn_bd_r_format_olD */

				/* search largest and smallest 0x32 */
				if(format_type != 0x32)
		continue;
				if (i_size < min_size || min_size < 0)
					min_size = i_size;
				if (i_size > max_size)
					max_size = i_size;

#endif /* ! Libburn_bd_r_format_olD */

			}
			/* common for all cases which search largest
			   descriptors */
			if (i_size > format_size) {
				format_size = i_size;
				index = i;
			}
		}
		if (size_mode == 2 && index < 0 && !(flag & 32))
			index = 0;
		if (index < 0)
			goto no_suitable_formatting_type;
		format_type = d->format_descriptors[index].type;
		if (flag & (1 << 16))
			format_sub_type = 0; /* SRM + POW  */
		else
			format_sub_type = 1; /* SRM  (- POW) */

#ifdef Libburn_bd_r_format_olD
		if (0) {
#else
		if (size_mode == 0 || size_mode == 1) {
#endif /* ! Libburn_bd_r_format_olD */

			if (min_size < 0 || max_size < 0)
				goto no_suitable_formatting_type;
			if (size <= 0)
				size = min_size;
			if (size % 0x10000)
				size += 0x10000 - (size % 0x10000);
			if (size < min_size)
				goto no_suitable_formatting_type;
			else if(size > max_size)
				goto no_suitable_formatting_type;
			num_of_blocks = size / 2048;
			mmc_int_to_four_char(c->page->data + 4, num_of_blocks);
			for (i = 0; i < 3; i++)
				 c->page->data[9 + i] = 0;
		} else {
			num_of_blocks = 
				d->format_descriptors[index].size / 2048;
			mmc_int_to_four_char(c->page->data + 4, num_of_blocks);
			for (i = 0; i < 3; i++)
				 c->page->data[9 + i] =
					( d->format_descriptors[index].tdp >>
						  (16 - 8 * i)) & 0xff;
		}
		sprintf(descr, "%s", d->current_profile_text);
		return_immediately = 1; /* caller must do the waiting */
		c->page->data[1] |= 0x80; /* FOV = this flag vector is valid */

	} else if (d->current_profile == 0x43) {
		/* BD-RE */
		index = -1;
		format_size = -1;
		if (d->num_format_descr <= 0)
			goto no_suitable_formatting_type;
		if (d->format_descriptors[0].type != 0)
			goto no_suitable_formatting_type;
		for (i = 0; i < d->num_format_descr; i++) {
			format_type = d->format_descriptors[i].type;
			i_size = d->format_descriptors[i].size;
			if (format_type != 0x00 && format_type != 0x30 &&
			    format_type != 0x31)
		continue;
			if (flag & 32) { /* No defect mgt */
				/* search largest format 0x31 */
				if(format_type != 0x31)
		continue;
			} else if(size_mode == 2) { /* max payload size */
				/* search largest 0x30 format descriptor */
				if(format_type != 0x30)
		continue;
			} else if(size_mode == 3) { /* default payload size */
				if (accept_count < 1)
					index = 0; /* this cannot certify */

				/* ts A81129
				   LG GGW-H20L YL03 refuses on 0x30 with 
				   "Quick certification". dvd+rw-format
				   does 0x00 by default and succeeds quickly.
				*/
				if ((flag & 64) && format_type == 0x00) {
					index = i;
		break;
				}

				if(format_type != 0x30)
		continue;
				accept_count++;
				if (accept_count == 1)
					index = i;
		continue;
			} else { /* defect managed format with size wish */

#ifdef Libburn_bd_re_format_olD

				/* search for smallest 0x30 >= size */
				if(format_type != 0x30)
		continue;
				if (i_size < size)
		continue;
				if (format_size >= 0 && i_size >= format_size)
		continue;
				index = i;
				format_size = i_size;
		continue;

#else /* Libburn_bd_re_format_olD */

				/* search largest and smallest 0x30 */
				if(format_type != 0x30)
		continue;
				if (i_size < min_size || min_size < 0)
					min_size = i_size;
				if (i_size > max_size)
					max_size = i_size;

#endif /* ! Libburn_bd_re_format_olD */

			}
			/* common for all cases which search largest
			   descriptors */
			if (i_size > format_size) {
				format_size = i_size;
				index = i;
			}
		}

		if (size_mode == 2 && index < 0 && !(flag & 32))
			index = 0;
		if (index < 0)
			goto no_suitable_formatting_type;
		format_type = d->format_descriptors[index].type;
		if (format_type == 0x30 || format_type == 0x31) {
			if ((flag & 64) || !(d->current_feat23h_byte4 & 3)) {
				format_sub_type = 0;
				if (!(flag & 64))
					libdax_msgs_submit(libdax_messenger,
					    d->global_index, 0x0002019e,
					    LIBDAX_MSGS_SEV_NOTE,
					    LIBDAX_MSGS_PRIO_HIGH,
				"Drive does not support media certification",
					    0, 0);
			} else {
				/* With Certification */
				if (d->current_feat23h_byte4 & 1)
					format_sub_type = 2; /* Full */
				else
					format_sub_type = 3; /* Quick */
			}
		}

#ifdef Libburn_bd_re_format_olD
		if (0) {
#else
		if (size_mode == 0 || size_mode == 1) {
#endif /* ! Libburn_bd_re_format_olD */

			if (min_size < 0 || max_size < 0)
				goto no_suitable_formatting_type;
			if (size <= 0)
				size = min_size;
			if (size % 0x10000)
				size += 0x10000 - (size % 0x10000);
			if (size < min_size)
				goto no_suitable_formatting_type;
			else if(size > max_size)
				goto no_suitable_formatting_type;
			num_of_blocks = size / 2048;
			mmc_int_to_four_char(c->page->data + 4, num_of_blocks);
			for (i = 0; i < 3; i++)
				 c->page->data[9 + i] = 0;
		} else {
			num_of_blocks = 
				d->format_descriptors[index].size / 2048;
			mmc_int_to_four_char(c->page->data + 4, num_of_blocks);
			for (i = 0; i < 3; i++)
				 c->page->data[9 + i] =
					( d->format_descriptors[index].tdp >>
						  (16 - 8 * i)) & 0xff;
		}
		sprintf(descr, "%s", d->current_profile_text);
		return_immediately = 1; /* caller must do the waiting */
		c->page->data[1] |= 0x80; /* FOV = this flag vector is valid */
		
	} else { 

		/* >>> other formattable types to come */

unsuitable_media:;
		sprintf(msg, "Unsuitable media detected. Profile %4.4Xh  %s",
			d->current_profile, d->current_profile_text);
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x0002011e,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
		{ret = 0; goto ex;}
	}
	c->page->data[8] = (format_type << 2) | (format_sub_type & 3);

	/* MMC-5 Table 253 , column Type Dependent Parameter */
	if (format_type == 0x00 || format_type == 0x01 ||
	    format_type == 0x31) {
		/* Block Length 0x0800 = 2k */
		c->page->data[ 9] = 0x00;
		c->page->data[10] = 0x08;
		c->page->data[11] = 0x00;
	} else if (format_type >= 0x10 && format_type <= 0x15) {
          	/* ECC block size = 16 * 2k */
		c->page->data[ 9] =  0;
		c->page->data[10] =  0;
		c->page->data[11] = 16;
	}

	sprintf(msg, "Format type %2.2Xh \"%s\", blocks = %.f",
		format_type, descr, (double) num_of_blocks);
	libdax_msgs_submit(libdax_messenger, d->global_index, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
			msg, 0, 0);
	sprintf(msg, "CDB: ");
	for (i = 0; i < 6; i++)
		sprintf(msg + strlen(msg), "%2.2X ", c->opcode[i]);
	libdax_msgs_submit(libdax_messenger, d->global_index, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
			msg, 0, 0);
	sprintf(msg, "Format list: ");
	for (i = 0; i < 12; i++)
		sprintf(msg + strlen(msg), "%2.2X ", c->page->data[i]);
	strcat(msg, "\n");
	libdax_msgs_submit(libdax_messenger, d->global_index, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
			msg, 0, 0);
	
#ifdef Libburn_do_not_format_dvd_ram_or_bd_rE
	if(d->current_profile == 0x43 || d->current_profile == 0x12) {
		sprintf(msg,
		   "Formatting of %s not implemented yet - This is a dummy",
		   d->current_profile_text);
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00000002,
			LIBDAX_MSGS_SEV_WARNING, LIBDAX_MSGS_PRIO_ZERO,
			msg, 0, 0);
		{ret = 1; goto ex;}
	}
#endif /* Libburn_do_not_format_dvd_ram_or_bd_rE */

/* <<<
fprintf(stderr, "\nlibburn_DEBUG: FORMAT UNIT temporarily disabled.\n");
ret = 1; goto ex;
 */

	d->issue_command(d, c);
	if (c->error && !tolerate_failure) {
		spc_decode_sense(c->sense, 0, &key, &asc, &ascq);
		if (key != 0) {
			sprintf(msg, "SCSI error on format_unit(%s): ", descr);
			scsi_error_msg(d, c->sense, 14, msg + strlen(msg), 
					&key, &asc, &ascq);
			libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020122, 
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);

		}
		{ret = 0; goto ex;}
	} else if ((!c->error) && (format_type == 0x13 || format_type == 0x15))
		d->needs_close_session = 1;
	if (return_immediately)
		{ret = 1; goto ex;}
	usleep(1000000); /* there seems to be a little race condition */
	for (ret = 0; ret <= 0 ;) {
		usleep(50000);
		ret = spc_test_unit_ready(d);
	}
	mmc_sync_cache(d);
	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	BURN_FREE_MEM(c);
	BURN_FREE_MEM(buf);
	return ret;
}


/* ts A61225 */
/* @param flag bit0= register speed descriptors
*/
static int mmc_get_write_performance_al(struct burn_drive *d,
		 int *alloc_len, int *max_descr, int flag)
{
	struct buffer *buf = NULL;
	int len, i, b, num_descr, ret, old_alloc_len;
	int exact_bit, read_speed, write_speed;

	/* >>> ts B10702: This rule seems questionable:
	       TSST SH-203 delivers here for CD only 7040k
	       whereas mode page 2Ah gives 1412k to 7056k
	*/
	/* if this call delivers usable data then they should override
	   previously recorded min/max speed and not compete with them */
	int min_write_speed = 0x7fffffff, max_write_speed = 0;
	int min_read_speed = 0x7fffffff, max_read_speed = 0;

	struct command *c = NULL;
	unsigned long end_lba;
	unsigned char *pd;
	struct burn_speed_descriptor *sd;

	/* A61225 : 1 = report about speed descriptors */
	static int speed_debug = 0;

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	BURN_ALLOC_MEM(c, struct command, 1);

	if (d->current_profile <= 0)
		mmc_get_configuration(d);

	if (*alloc_len < 8)
		{ret = 0; goto ex;}

	scsi_init_command(c, MMC_GET_PERFORMANCE,
			 sizeof(MMC_GET_PERFORMANCE));

	/* >>> future: maintain a list of write descriptors 
	if (max_descr > d->max_write_descr - d->num_write_descr)
		max_descr = d->max_write_descr;
	*/
	c->dxfer_len = *alloc_len;

	c->opcode[8] = ( *max_descr >> 8 ) & 0xff;
	c->opcode[9] = ( *max_descr >> 0 ) & 0xff;
	c->opcode[10] = 3;
	c->retry = 1;
	c->page = buf;
	c->page->sectors = 0;
	c->page->bytes = 0;
	c->dir = FROM_DRIVE;
	d->issue_command(d, c);

#ifdef Libisofs_simulate_old_mmc1_drivE
	c->error = 1;
	c->sense[0] = 0x70; /* Fixed format sense data */
	c->sense[2] = 0x5;
	c->sense[12] = 0x20;
	c->sense[13] = 0x0;
#endif /* Libisofs_simulate_old_mmc1_drivE */	

	if (c->error)
		{ret = 0; goto ex;}
        len = mmc_four_char_to_int(c->page->data);
	old_alloc_len = *alloc_len;
        *alloc_len = len + 4;
	if (len + 4 > old_alloc_len)
		len = old_alloc_len - 4;
	num_descr = ( *alloc_len - 8 ) / 16;
	if (*max_descr == 0) {
		*max_descr = num_descr;
		{ret = 1; goto ex;}
	}
	if (old_alloc_len < 16)
		{ret = 1; goto ex;}
	if (len < 12)
		{ret = 0; goto ex;}

	/* ts B10702 : overriding the questionable override rule */
	min_write_speed = d->mdata->min_write_speed;
	max_write_speed = d->mdata->max_write_speed;

	pd = c->page->data;
	if (num_descr > *max_descr)
		num_descr = *max_descr;
	for (i = 0; i < num_descr && (flag & 1); i++) {
		exact_bit = !!(pd[8 + i*16] & 2);
		end_lba = read_speed = write_speed = 0;
		for (b = 0; b < 4 ; b++) {
			end_lba     += pd[8 + i*16 +  4 + b] << (24 - 8 * b);
			read_speed  += pd[8 + i*16 +  8 + b] << (24 - 8 * b);
			write_speed += pd[8 + i*16 + 12 + b] << (24 - 8 * b);
		}
		if (end_lba > 0x7ffffffe)
			end_lba = 0x7ffffffe;

		if (speed_debug)
			fprintf(stderr,
		"LIBBURN_DEBUG: kB/s: write=%d  read=%d  end=%lu  exact=%d\n",
				write_speed, read_speed, end_lba, exact_bit);

		/* ts A61226 */
		ret = burn_speed_descriptor_new(&(d->mdata->speed_descriptors),
				 NULL, d->mdata->speed_descriptors, 0);
		if (ret > 0) {
			sd = d->mdata->speed_descriptors;
			sd->source = 2;
			if (d->current_profile > 0) {
				sd->profile_loaded = d->current_profile;
				strcpy(sd->profile_name,
					d->current_profile_text);
			}
			sd->wrc = (pd[8 + i*16] >> 3 ) & 3;
			sd->exact = exact_bit;
			sd->mrw = pd[8 + i*16] & 1;
			sd->end_lba = end_lba;
			sd->write_speed = write_speed;
			sd->read_speed = read_speed;
		}

		if ((int) end_lba > d->mdata->max_end_lba)
			d->mdata->max_end_lba = end_lba;
		if ((int) end_lba < d->mdata->min_end_lba)
			d->mdata->min_end_lba = end_lba;
		if (write_speed < min_write_speed)
			min_write_speed = write_speed;
		if (write_speed > max_write_speed)
                        max_write_speed = write_speed;
		if (read_speed < min_read_speed)
			min_read_speed = read_speed;
		if (read_speed > max_read_speed)
                        max_read_speed = read_speed;
	}
	if (min_write_speed < 0x7fffffff)
		d->mdata->min_write_speed = min_write_speed;
	if (max_write_speed > 0)
		d->mdata->max_write_speed = max_write_speed;
	/* there is no mdata->min_read_speed yet 
	if (min_read_speed < 0x7fffffff)
		d->mdata->min_read_speed = min_read_speed;
	*/
	if (max_read_speed > 0)
		d->mdata->max_read_speed = max_read_speed;
	ret = num_descr;
ex:;
	BURN_FREE_MEM(buf);
	BURN_FREE_MEM(c);
	return ret;
}


int mmc_get_write_performance(struct burn_drive *d)
{
	int alloc_len = 8, max_descr = 0, ret;

	mmc_start_if_needed(d, 1);
	if (mmc_function_spy(d, "mmc_get_write_performance") <= 0)
		return 0;

	/* first command execution to learn number of descriptors and 
           dxfer_len
	*/
	ret = mmc_get_write_performance_al(d, &alloc_len, &max_descr, 0);
	if (max_descr > 0 && ret > 0) {
		/* Some drives announce only 1 descriptor if asked for 0.
		   So ask twice for non-0 descriptors.
		*/
		ret = mmc_get_write_performance_al(d, &alloc_len, &max_descr,
		                                   0);
	}
/*
	fprintf(stderr,"LIBBURN_DEBUG: ACh alloc_len = %d , ret = %d\n",
			alloc_len, ret);
*/
	if (max_descr > 0 && ret > 0)
		/* final execution with announced length */
		max_descr = (alloc_len - 8) / 16;
		ret = mmc_get_write_performance_al(d, &alloc_len, &max_descr,
		                                   1);
	return ret; 
}


/* ts A61229 : outsourced from spc_select_write_params() */
/* Note: Page data is not zeroed here to allow preset defaults. Thus
           memset(pd, 0, 2 + d->mdata->write_page_length);
         is the eventual duty of the caller.
*/
int mmc_compose_mode_page_5(struct burn_drive *d, struct burn_session *s,
				int tnum, const struct burn_write_opts *o,
				unsigned char *pd)
{
	unsigned char *catalog = NULL;
	char isrc_text[13];
	struct isrc *isrc;

	pd[0] = 5;
	pd[1] = d->mdata->write_page_length;

	if (d->current_profile == 0x13) {
		/* A61229 : DVD-RW restricted overwrite */
		/* learned from transport.hxx : page05_setup()
		   and mmc3r10g.pdf table 347 */
 		/* BUFE (burnproof), no LS_V (i.e. default Link Size, i hope),
		   no simulate, write type 0 = packet */
		pd[2] = (1 << 6);
		/* no multi, fixed packet, track mode 5 */
		pd[3] = (1 << 5) | 5;
		/* Data Block Type */
		pd[4] = 8;
		/* Link size dummy */
		pd[5] = 0;
	} else if ((d->current_profile == 0x14 || d->current_profile == 0x11 ||
			d->current_profile == 0x15)
			&& o->write_type == BURN_WRITE_SAO) {
		/* ts A70205 : DVD-R[W][/DL] : Disc-at-once, DAO */
		/* Learned from dvd+rw-tools and mmc5r03c.pdf .
		   See doc/cookbook.txt for more detailed references. */

		/* BUFE , LS_V = 0, Test Write, Write Type = 2 SAO (DAO) */
		pd[2] = ((!!o->underrun_proof) << 6)
			| ((!!o->simulate) << 4)
			| 2;

		/* No multi-session , FP = 0 , Copy = 0, Track Mode = 5 */
		pd[3] = 5;

#ifdef Libburn_pioneer_dvr_216d_load_mode5

		/* >>> use track mode from mmc_get_nwa() */
		/* >>> pd[3] = (pd[3] & ~0xf) | (d->track_inf[5] & 0xf); */

#endif

		/* Data Block Type = 8 */
		pd[4] = 8;

	} else if (d->current_profile == 0x14 || d->current_profile == 0x11 ||
			d->current_profile == 0x15) {
		/* ts A70128 : DVD-R[W][/DL] Incremental Streaming */
		/* Learned from transport.hxx : page05_setup()
		   and mmc5r03c.pdf 7.5, 4.2.3.4 Table 17
		   and spc3r23.pdf 6.8, 7.4.3 */

		/* BUFE , LS_V = 1, Test Write,
		   Write Type = 0 Packet/Incremental */
		pd[2] = ((!!o->underrun_proof) << 6)
			| (1 << 5)
			| ((!!o->simulate) << 4);
		/* Multi-session , FP = 1 , Track Mode = 5 */
		pd[3] = ((3 * !!o->multi) << 6) | (1 << 5) | 5;
		/* Data Block Type = 8 */
		pd[4] = 8;
		/* Link Size */
		if (d->current_feat21h_link_size >= 0)
			pd[5] = d->current_feat21h_link_size;
		else
			pd[5] = 16;
		if (d->current_feat21h_link_size != 16) {
			char msg[80];

			sprintf(msg,
				"Feature 21h Link Size = %d (expected 16)\n",
				d->current_feat21h_link_size);
			libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
				LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
				msg, 0, 0);
		}
		/* Packet Size */
		pd[13] = 16;

	} else if (d->current_profile == 0x1a || d->current_profile == 0x1b ||
	           d->current_profile == 0x2b || d->current_profile == 0x12 ||
		   d->current_profile == 0x41 || d->current_profile == 0x42 ||
		   d->current_profile == 0x43) {
		/* not with DVD+R[W][/DL] or DVD-RAM or BD-R[E] */;
		return 0;
	} else {
		/* Traditional setup for CD */

		pd[2] = ((!!o->underrun_proof) << 6)
			| ((!!o->simulate) << 4)
			| (o->write_type & 0x0f);

		/* ts A61106 : MMC-1 table 110 : multi==0 or multi==3 */
		pd[3] = ((3 * !!o->multi) << 6) | (o->control & 0x0f);

		pd[4] = spc_block_type(o->block_type);

/*
fprintf(stderr, "libburn_EXPERIMENTAL: block_type = %d, pd[4]= %u\n",
                 o->block_type, (unsigned int) pd[4]);
*/

		/* ts A61104 */
		if(!(o->control&4)) /* audio (MMC-1 table 61) */
			if(o->write_type == BURN_WRITE_TAO)
				pd[4] = 0; /* Data Block Type: Raw Data */

		pd[14] = 0;     /* audio pause length MSB */
		pd[15] = 150;	/* audio pause length LSB */

/*XXX need session format! */
/* ts A61229 : but session format (pd[8]) = 0 seems ok */

		/* Media Catalog Number at byte 16 to 31,
		   MMC-5, 7.5, Tables 664, 670
		*/
		if (o->has_mediacatalog)
			catalog = (unsigned char *) o->mediacatalog;
		else if (s != NULL) {
			if (s->mediacatalog[0])
				catalog = s->mediacatalog;
		}
		if (catalog != NULL && d->mdata->write_page_length >= 30) {
			pd[16] = 0x80; /* MCVAL */
			memcpy(pd + 17, catalog, 13);
		}

		/* ISRC at bytes 32 to 47. Tables 664, 671 */
		/* SCMS at byte 3 bit 4 */
		isrc_text[0] = 0;
		if (s != NULL && o->write_type == BURN_WRITE_TAO) {
			if (tnum >= 0 && tnum < s->tracks) {
				if (s->track[tnum]->isrc.has_isrc) {
					isrc = &(s->track[tnum]->isrc);
					isrc_text[0] = isrc->country[0];
					isrc_text[1] = isrc->country[1];
					isrc_text[2] = isrc->owner[0];
					isrc_text[3] = isrc->owner[1];
					isrc_text[4] = isrc->owner[2];
					sprintf(isrc_text + 5, "%-2.2u%-5.5u",
						(unsigned int) isrc->year,
						isrc->serial);
				}
				if ((s->track[tnum]->mode & BURN_SCMS) &&
				    !(s->track[tnum]->mode & BURN_COPY))
					pd[3] |= 0x10;
			}
		}
		if (isrc_text[0] != 0 && d->mdata->write_page_length >= 46) {
			pd[32] = 0x80; /* TCVAL */
			memcpy(pd + 33, isrc_text, 12);
		}
	}
	return 1;
}


/* A70812 ts */
int mmc_read_10(struct burn_drive *d, int start,int amount, struct buffer *buf)
{
	struct command *c;
	char *msg = NULL;
	int key, asc, ascq, silent;

	c = &(d->casual_command);
	mmc_start_if_needed(d, 0);
	if (mmc_function_spy(d, "mmc_read_10") <= 0)
		return -1;

	if (amount > BUFFER_SIZE / 2048)
		return -1;

	scsi_init_command(c, MMC_READ_10, sizeof(MMC_READ_10));
	c->dxfer_len = amount * 2048;
	c->retry = 1;
	mmc_int_to_four_char(c->opcode + 2, start);
	c->opcode[7] = (amount >> 8) & 0xFF;
	c->opcode[8] = amount & 0xFF;
	c->page = buf;
	c->page->bytes = 0;
	c->page->sectors = 0;
	c->dir = FROM_DRIVE;
	d->issue_command(d, c);

	/* <<< replace by mmc_eval_read_error */;
	if (c->error) {
		msg = calloc(1, 256);
		if (msg != NULL) {
			sprintf(msg,
			  "SCSI error on read_10(%d,%d): ", start, amount);
			scsi_error_msg(d, c->sense, 14, msg + strlen(msg), 
					&key, &asc, &ascq);
			silent = (d->silent_on_scsi_error == 1);
			if (key == 5 && asc == 0x64 && ascq == 0x0) {
				d->had_particular_error |= 1;
				silent = 1;
			}
			if(!silent)
				libdax_msgs_submit(libdax_messenger,
				d->global_index,
				0x00020144,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
			free(msg);
		}
		return BE_CANCELLED;
	}

	buf->sectors = amount;
	buf->bytes = amount * 2048;
	return 0;
}


#ifdef Libburn_develop_quality_scaN

/* B21108 ts : Vendor specific command REPORT ERROR RATE, see
               http://liggydee.cdfreaks.com/ddl/errorcheck.pdf
*/
int mmc_nec_optiarc_f3(struct burn_drive *d, int sub_op,
                       int start_lba, int rate_period,
                       int *ret_lba, int *error_rate1, int *error_rate2)
{
	struct buffer *buf = NULL;
	struct command *c;
	char *msg = NULL;
	int key, asc, ascq, ret;
	static unsigned char MMC_NEC_OPTIARC_F3[] =
		{ 0xF3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	BURN_ALLOC_MEM(c, struct command, 1);
	mmc_start_if_needed(d, 0);
	if (mmc_function_spy(d, "mmc_nec_optiarc_f3") <= 0)
		return -1;

	scsi_init_command(c, MMC_NEC_OPTIARC_F3, sizeof(MMC_NEC_OPTIARC_F3));
	if (sub_op == 3) {
		c->dxfer_len = 8;
		c->dir = FROM_DRIVE;
	} else {
		c->dxfer_len = 0;
		c->dir = NO_TRANSFER;
	}
	c->retry = 0;
	c->opcode[1] = sub_op;
	mmc_int_to_four_char(c->opcode + 2, start_lba);
	c->opcode[8] = rate_period;
	c->page = buf;
	c->page->bytes = 0;
	c->page->sectors = 0;
	d->issue_command(d, c);
	if (c->error) {
		msg = calloc(1, 256);
		if (msg != NULL) {
			sprintf(msg,
			  "SCSI error on nec_optiarc_f3(%d, %d, %d): ",
                           sub_op, start_lba, rate_period);
			scsi_error_msg(d, c->sense, 14, msg + strlen(msg), 
					&key, &asc, &ascq);
			libdax_msgs_submit(libdax_messenger,
				d->global_index, 0x00020144,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
			free(msg);
		}
		return BE_CANCELLED;
	}

	if (sub_op == 3) {
		*ret_lba = mmc_four_char_to_int(c->page->data);
		*error_rate1 = c->page->data[4] * 256 + c->page->data[5];
		*error_rate2 = c->page->data[6] * 256 + c->page->data[7];
	}

	ret = 1;
ex:;
	BURN_FREE_MEM(c);
	BURN_FREE_MEM(buf);
	return ret;
}

#endif /* Libburn_develop_quality_scaN */


/* ts A81210 : Determine the upper limit of readable data size */
int mmc_read_capacity(struct burn_drive *d)
{
	struct buffer *buf = NULL;
	struct command *c = NULL;
	int alloc_len= 8, ret;

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	BURN_ALLOC_MEM(c, struct command, 1);
	d->media_read_capacity = 0x7fffffff;
	mmc_start_if_needed(d, 1);
	if (mmc_function_spy(d, "mmc_read_capacity") <= 0)
		{ret = 0; goto ex;}

	scsi_init_command(c, MMC_READ_CAPACITY, sizeof(MMC_READ_CAPACITY));
	c->dxfer_len = alloc_len;
	c->retry = 1;
	c->page = buf;
	c->page->bytes = 0;
	c->page->sectors = 0;
	c->dir = FROM_DRIVE;
	d->issue_command(d, c);
	d->media_read_capacity = mmc_four_char_to_int(c->page->data);
	if (d->media_read_capacity < 0) {
		d->media_read_capacity = 0x7fffffff;
		{ret = 0; goto ex;}
	}
	ret = 1;
ex:;
	BURN_FREE_MEM(c);
	BURN_FREE_MEM(buf);
	return ret;
}


/* ts A90903 */
/* mmc5r03c.pdf 6.23 ADh READ DISC STRUCTURE obtains media specific information
*/
static int mmc_read_disc_structure_al(struct burn_drive *d, int *alloc_len,
				int media_type, int layer_number, int format,
				int min_len, char **reply, int *reply_len,
				int flag)
{
	struct buffer *buf = NULL;
	int old_alloc_len, len, ret;
	struct command *c = NULL;
	unsigned char *dpt;

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	BURN_ALLOC_MEM(c, struct command, 1);
	*reply = NULL;
	*reply_len = 0;

	if (*alloc_len < 4)
		{ret = 0; goto ex;}

	scsi_init_command(c, MMC_READ_DISC_STRUCTURE,
			 sizeof(MMC_READ_DISC_STRUCTURE));
	c->dxfer_len = *alloc_len;
	c->retry = 1;
	c->opcode[1]= media_type;
	c->opcode[7]= format;
	c->opcode[8]= (c->dxfer_len >> 8) & 0xff;
	c->opcode[9]= c->dxfer_len & 0xff;
	c->page = buf;
	c->page->sectors = 0;
	c->page->bytes = 0;
	c->dir = FROM_DRIVE;

	d->issue_command(d, c);
	if (c->error)
		{ret = 0; goto ex;}

	len = (c->page->data[0] << 8) | (c->page->data[1]);
	old_alloc_len = *alloc_len;
	*alloc_len = len + 2;
	if (old_alloc_len <= 4)
		{ret = 1; goto ex;}
	if (len + 2 > old_alloc_len)
		len = old_alloc_len - 2;
	if (len < 4)
		{ret = 0; goto ex;}

	dpt = c->page->data + 4;
	if (len - 2 < min_len)
		{ret = 0; goto ex;}
	*reply = calloc(len - 2, 1);
	if (*reply == NULL)
		{ret = 0; goto ex;}
	*reply_len = len - 2;
	memcpy(*reply, dpt, len - 2);
	ret = 1;
ex:;
	BURN_FREE_MEM(c);
	BURN_FREE_MEM(buf);
	return ret;
}


int mmc_read_disc_structure(struct burn_drive *d,
		int media_type, int layer_number, int format, int min_len,
		char **reply, int *reply_len, int flag)
{
	int alloc_len = 4, ret;
	char msg[80];

	mmc_start_if_needed(d, 1);
	if (mmc_function_spy(d, "mmc_read_disc_structure") <= 0)
		return 0;

	ret = mmc_read_disc_structure_al(d, &alloc_len,
				media_type, layer_number, format, min_len,
				reply, reply_len, 0);
/*
	fprintf(stderr,"LIBBURN_DEBUG: ADh alloc_len = %d , ret = %d\n",
		 alloc_len, ret);
*/
	if (ret <= 0)
		return ret;
	if (alloc_len < 12) {
		sprintf(msg,
		    "READ DISC STRUCTURE announces only %d bytes of reply\n", 
		    alloc_len);
		libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
			msg, 0, 0);
		ret = 0;

/* ts A91205
   LG GH22LS30 revision 1.00 returns for DVD-R format
   code 0x0E an allocation length of 4 (= 0 payload).
   A MS-Windows tool can inquire media code "RITEKF1",
   though.
   This macro causes a try to unconditionally read the
   desired payload bytes. The drive then returns 35
   bytes as requested and the media id is "RITEKF1".
   Nevertheless this is not a generally usable gesture
   because older GNU/Linux USB dislikes requests to fetch
   more bytes than the drive will deliver.

   # define Libburn_enforce_structure_code_0x0E 1
*/

#ifdef Libburn_enforce_structure_code_0x0E
		if (format == 0x0E) {
			alloc_len = min_len + 4;
			ret = mmc_read_disc_structure_al(d, &alloc_len,
				media_type, layer_number, format, min_len,
				reply, reply_len, 0);
			if (*reply_len < min_len || *reply == NULL)
				ret = 0;
			sprintf(msg, "READ DISC STRUCTURE returns %d bytes of required %d\n", 
				*reply_len + 4, min_len + 4);
			libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
				LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
				msg, 0, 0);
		}
#endif

	} else
		ret = mmc_read_disc_structure_al(d, &alloc_len,
				media_type, layer_number, format, min_len,
				reply, reply_len, 0);
	return ret;
}

/* ts A90903 */
/*
    @param flag    bit0= set bit1 in flag for burn_util_make_printable_word
                         and do not append media revision
*/
static int mmc_set_product_id(char *reply,
	 int manuf_idx, int type_idx, int rev_idx,
	char **product_id, char **media_code1, char **media_code2, int flag)
{
	int ret;

	*product_id = calloc(17, 1);
	*media_code1 = calloc(9, 1);
	*media_code2 = calloc(8, 1);
	if (*product_id == NULL ||
	    *media_code1 == NULL || *media_code2 == NULL)
		return -1;
	sprintf(*media_code1, "%.8s", reply + manuf_idx);
	ret = burn_util_make_printable_word(media_code1,
						 1 | ((flag & 1) << 1));
	if (ret <= 0)
		return -1;
	sprintf(*media_code2, "%.3s%s", reply + type_idx,
					 (flag & 1) ? "" : "xxxx");
	ret = burn_util_make_printable_word(media_code2,
						 1 | ((flag & 1) << 1));
	if (ret <= 0)
		return -1;
	if (!(flag & 1)) {
		sprintf(*media_code2 + strlen(*media_code2) - 4, "/%d",
			(int) ((unsigned char *) reply)[rev_idx]);
	}
	sprintf(*product_id, "%s/%s", *media_code1, *media_code2);
	return 1;
}


/* ts A90903 */
/* MMC backend of API call burn_disc_get_media_id()
   See also doc/mediainfo.txt
    @param flag        Bitfield for control purposes
                       bit0= do not escape " _/" (not suitable for
                             burn_guess_manufacturer())

*/
int mmc_get_media_product_id(struct burn_drive *d,
	char **product_id, char **media_code1, char **media_code2,
	char **book_type, int flag)
{
	int prf, ret, reply_len, i, has_11h = -1, bt, start_lba, end_lba;
	int min, sec, fr, media_type = 0;
	char *reply = NULL, *wpt;

	static char *books[16] = {
		"DVD-ROM", "DVD-RAM", "DVD-R", "DVD-RW",
		"HD DVD-ROM", "HD DVD-RAM", "HD DVD-R", "unknown",
		"unknown", "DVD+RW", "DVD+R", "unknown",
		"unknown", "DVD+RW DL" "DVD+R DL", "unknown"};

	*product_id = *media_code1 = *media_code2 = *book_type = NULL;
	prf = d->current_profile;
	if (prf == 0x09 || prf == 0x0A) {

		*product_id = calloc(20, 1);
		*media_code1 = calloc(10, 1);
		*media_code2 = calloc(10, 1);
		if (*product_id == NULL ||
		    *media_code1 == NULL || *media_code2 == NULL) {
			ret = -1;
			goto ex;
		}
		ret = burn_disc_read_atip(d);
		if (ret <= 0)
			goto ex;
		ret = burn_drive_get_start_end_lba(d, &start_lba, &end_lba, 0);
		if (ret <= 0)
			goto ex;
		burn_lba_to_msf(start_lba, &min, &sec, &fr);
		sprintf(*media_code1, "%2.2dm%2.2ds%2.2df", min, sec, fr);
		burn_lba_to_msf(end_lba, &min, &sec, &fr);
		sprintf(*media_code2, "%2.2dm%2.2ds%2.2df", min, sec, fr);
		sprintf(*product_id, "%s/%s", *media_code1, *media_code2);
		ret = 1;
		goto ex; /* No booktype with CD media */

        } else if (prf == 0x11 || prf == 0x13 || prf == 0x14 || prf == 0x15) {
								 /* DVD-R */

		ret = mmc_read_disc_structure(d, 0, 0, 0x0E, 31, &reply,
							 &reply_len, 0);
		if (ret <= 0)
			goto ex;
		/* ECMA-279 for DVD-R promises a third sixpack in field 5,
		   but ECMA-338 for DVD-RW defines a different meaning.
		   DVD-R and DVD-RW bear unprintable characters in there.
		 */
		if (reply[16] != 3 || reply[24] != 4) {
			ret = 0;
			goto ex;
		}
		*media_code1 = calloc(19, 1);
		*media_code2 = strdup("");
		if (*media_code1 == NULL || *media_code2 == NULL) {
			ret = -1;
			goto ex;
		}
		memcpy(*media_code1, reply + 17, 6);
		memcpy(*media_code1 + 6, reply + 25, 6);

		/* Clean out 0 bytes */
		wpt = *media_code1;
		for (i = 0; i < 18; i++)
			if ((*media_code1)[i])
				*(wpt++) = (*media_code1)[i];
		*wpt = 0;
		ret = burn_util_make_printable_word(media_code1,
						 1 | ((flag & 1) << 1));
		if (ret <= 0)
			goto ex;
		*product_id = strdup(*media_code1);
		if (*product_id == NULL) {
			ret = -1;
			goto ex;
		}

        } else if (prf == 0x1a || prf == 0x1b || prf == 0x2b) { /* DVD+R[W] */

		/* Check whether the drive supports format 11h */
		has_11h = 0;
		ret = mmc_read_disc_structure(d, 0, 0, 4, 0xff, &reply,
							 &reply_len, 0);
		if (ret > 0) {
			for (i = 0; i < reply_len; i += 4) {
				if (reply[i] == 0x11 && (reply[i + 1] & 64))
					has_11h = 1;
			}
		}
		if (reply != NULL)
			free(reply);
		reply = NULL;
		ret = mmc_read_disc_structure(d, 0, 0, 0x11, 29, &reply,
							 &reply_len, 0);
		if (ret <= 0) {
			/* Hope for format 00h */
			has_11h = 0;
		} else {
			/* Dig out manufacturer, media type and revision */
			ret = mmc_set_product_id(reply, 19, 27, 28,
				product_id, media_code1, media_code2,
				flag & 1);
			if (ret <= 0)
				goto ex;
		}
        } else if (prf == 0x41 || prf == 0x43 || prf == 0x40 || prf == 0x42) {
								/* BD */
		media_type = 1;
		ret = mmc_read_disc_structure(d, 1, 0, 0x00, 112, &reply,
							 &reply_len, 0);
		if (ret <= 0)
			goto ex;
		if (reply[0] != 'D' || reply[1] != 'I') {
			ret = 0;
			goto ex;
		}
		/* Dig out manufacturer, media type and revision */
		ret = mmc_set_product_id(reply, 100, 106, 111,
			 	product_id, media_code1, media_code2,
				flag & 1);
		if (ret <= 0)
			goto ex;

	} else {

		/* Source of DVD-RAM manufacturer and media id not found yet */
		ret = 0;
		goto ex;
	}

	if (reply != NULL)
		free(reply);
	reply = NULL;
	ret = mmc_read_disc_structure(d, media_type, 0, 0x00, 1, 
							&reply, &reply_len, 0);
	if (ret <= 0)
		goto ex;
	bt = (reply[0] >> 4) & 0xf;
	*book_type = calloc(80 + strlen(books[bt]), 1);
	if (*book_type == NULL) {
		ret = -1;
		goto ex;
	}
	sprintf(*book_type, "%2.2Xh, %s book [revision %d]",
		bt, books[bt], reply[0] & 0xf);

	if (has_11h == 0 && *product_id == NULL && reply_len > 28) {
		/* DVD+ with no format 11h */
		/* Get manufacturer and media type from bytes 19 and 27 */
		ret = mmc_set_product_id(reply, 19, 27, 28, product_id,
						media_code1, media_code2,
						flag & 1);
		if (*product_id == NULL) {
			ret = 0;
			goto ex;
		}
	}

	ret = 1;
ex:;
	if (reply != NULL)
		free(reply);
	if (ret <= 0) {
		if (*product_id != NULL)
			free(*product_id);
		if (*media_code1 != NULL)
			free(*media_code1);
		if (*media_code2 != NULL)
			free(*media_code2);
		if (*book_type != NULL)
			free(*book_type);
		*product_id = *media_code1 = *media_code2 = *book_type = NULL;
	}
	return ret;
}


/* ts B00924
   MMC-5, 6.23.3.3.4 Format Code 0Ah: Spare Area Information
*/
int mmc_get_bd_spare_info(struct burn_drive *d,
				int *alloc_blocks, int *free_blocks, int flag)
{
	int ret, reply_len, prf;
	char *reply = NULL;

	prf = d->current_profile;
	if (!(prf == 0x41 || prf == 0x43 || prf == 0x42))
		return 0; /* Not a BD loaded */

	ret = mmc_read_disc_structure(d, 1, 0, 0x0a, 12, &reply,
							 &reply_len, 0);
	if (ret <= 0)
		goto ex;
	*alloc_blocks = mmc_four_char_to_int((unsigned char *) reply + 8);
	*free_blocks = mmc_four_char_to_int((unsigned char *) reply + 4);
	ret = 1;
ex:;
	if (reply != NULL)
		free(reply);
	return ret;
}


/* ts B10801
   MMC-5, 6.23.3.2.1 Format Code 00h: Physical Format Information
          6.23.3.2.16 Format Code 10h: Format Information of
                                       Control Data Zone in the Lead-in
   disk_category
*/
int mmc_get_phys_format_info(struct burn_drive *d, int *disk_category,
			char **book_name, int *part_version, int *num_layers,
			int *num_blocks, int flag)
{
	int ret, reply_len, prf;
	char *reply = NULL;
	static char book_names[][16] = {
		"DVD-ROM", "DVD-RAM", "DVD-R", "DVD-RW",
		"HD DVD-ROM", "HD DVD-RAM", "HD DVD-R", "unknown",
		"unknown", "DVD+RW", "DVD+R", "unknown", "unknown",
		"unknown", "DVD+RW DL", "DVD+R DL", "unknown"
	};

	prf = d->current_profile;
	if (!(prf == 0x11 || prf == 0x13 || prf == 0x14 || prf == 0x15 ||
	      prf == 0x51))
		return 0; /* Not a [HD] DVD-R[W] loaded */
	ret = mmc_read_disc_structure(d, 0, 0, 0x10, 12, &reply,
							 &reply_len, 0);
	if (ret <= 0)
		goto ex;
	if(reply_len < 12) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
			"READ DISC STRUCTURE format 10h: Less than 12 bytes",
			0, 0);
		{ret = 0; goto ex;}
	}
	*disk_category = (reply[0] >> 4) & 0xf;
	*book_name = book_names[*disk_category];
	*part_version = reply[0] & 0xf;
	*num_layers = ((reply[2] >> 5) & 0x3) + 1;
	*num_blocks = ((reply[9] << 16) | (reply[10] << 8) | reply[11]) -
	              ((reply[5] << 16) | (reply[6] << 8) | reply[7]) + 1;
	ret = 1;
ex:;
	if (reply != NULL)
		free(reply);
	return ret;
}



/* ts A61021 : the mmc specific part of sg.c:enumerate_common()
*/
int mmc_setup_drive(struct burn_drive *d)
{
	d->read_atip = mmc_read_atip;
	d->read_toc = mmc_read_toc;
	d->write = mmc_write;
	d->erase = mmc_erase;
	d->read_cd = mmc_read_cd;
	d->perform_opc = mmc_perform_opc;
	d->set_speed = mmc_set_speed;
	d->send_cue_sheet = mmc_send_cue_sheet;
	d->reserve_track = mmc_reserve_track;
	d->sync_cache = mmc_sync_cache;
	d->get_nwa = mmc_get_nwa;
	d->read_multi_session_c1 = mmc_read_multi_session_c1;
	d->close_disc = mmc_close_disc;
	d->close_session = mmc_close_session;
	d->close_track_session = mmc_close;
	d->read_buffer_capacity = mmc_read_buffer_capacity;
	d->format_unit = mmc_format_unit;
	d->read_format_capacities = mmc_read_format_capacities;
	d->read_10 = mmc_read_10;


	/* ts A70302 */
	d->phys_if_std = -1;
	d->phys_if_name[0] = 0;

	/* ts A61020 */
	d->start_lba = -2000000000;
	d->end_lba = -2000000000;

	/* ts A61201 - A90815*/
	d->erasable = 0;
	d->current_profile = -1;
	d->current_profile_text[0] = 0;
	d->current_is_cd_profile = 0;
	d->current_is_supported_profile = 0;
	d->current_is_guessed_profile = 0;
	memset(d->all_profiles, 0, 256);
	d->num_profiles = 0;
	d->current_has_feat21h = 0;
	d->current_feat21h_link_size = -1;
	d->current_feat23h_byte4 = 0;
	d->current_feat23h_byte8 = 0;
	d->current_feat2fh_byte4 = -1;
	d->next_track_damaged = 0;
	d->needs_close_session = 0;
	d->needs_sync_cache = 0;
	d->bg_format_status = -1;
	d->num_opc_tables = -1;
	d->last_lead_in = -2000000000;
	d->last_lead_out = -2000000000;
	d->disc_type = 0xff;
	d->disc_id = 0;
	memset(d->disc_bar_code, 0, 9);
	d->disc_app_code = 0;
	d->disc_info_valid = 0;
	d->num_format_descr = 0;
	d->complete_sessions = 0;

#ifdef Libburn_disc_with_incomplete_sessioN
	d->incomplete_sessions = 0;
#endif

	d->state_of_last_session = -1;
	d->last_track_no = 1;
	d->media_capacity_remaining = 0;
	d->media_lba_limit = 0;
	d->media_read_capacity = 0x7fffffff;
	d->pessimistic_buffer_free = 0;
	d->pbf_altered = 0;
	d->wait_for_buffer_free = Libburn_wait_for_buffer_freE;
	d->nominal_write_speed = 0;
	d->pessimistic_writes = 0;
	d->waited_writes = 0;
	d->waited_tries = 0;
	d->waited_usec = 0;
	d->wfb_min_usec = Libburn_wait_for_buffer_min_useC;
	d->wfb_max_usec = Libburn_wait_for_buffer_max_useC;
	d->wfb_timeout_sec = Libburn_wait_for_buffer_tio_seC;
	d->wfb_min_percent = Libburn_wait_for_buffer_min_perC;
	d->wfb_max_percent = Libburn_wait_for_buffer_max_perC;

	return 1;
}

