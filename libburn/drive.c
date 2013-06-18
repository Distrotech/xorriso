/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2013 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/


#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>

/* ts A61007 */
/* #include <a ssert.h> */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include "libburn.h"
#include "init.h"
#include "drive.h"
#include "transport.h"
#include "debug.h"
#include "init.h"
#include "toc.h"
#include "util.h"
#include "sg.h"
#include "structure.h"

/* ts A70107 : to get BE_CANCELLED */
#include "error.h"

/* ts A70219 : for burn_disc_get_write_mode_demands() */
#include "options.h"

/* A70225 : to learn about eventual Libburn_dvd_r_dl_multi_no_close_sessioN */
#include "write.h"

/* A70903 : for burn_scsi_setup_drive() */
#include "spc.h"

/* A90815 : for mmc_obtain_profile_name() */
#include "mmc.h"

#include "libdax_msgs.h"
extern struct libdax_msgs *libdax_messenger;

static struct burn_drive drive_array[255];
static int drivetop = -1;

/* ts A80410 : in init.c */
extern int burn_support_untested_profiles;

/* ts B10312 : in init.c */
extern int burn_drive_role_4_allowed;


/* ts A61021 : the unspecific part of sg.c:enumerate_common()
*/
int burn_setup_drive(struct burn_drive *d, char *fname)
{
	d->devname = strdup(fname);
	memset(&d->params, 0, sizeof(struct params));
	d->idata = NULL;
	d->mdata = NULL;
	d->toc_entry = NULL;
	d->released = 1;
	d->stdio_fd = -1;
	d->status = BURN_DISC_UNREADY;
	d->do_stream_recording = 0;
        d->stream_recording_start= 0;
	d->role_5_nwa = 0;
	return 1;
}


/* ts A70903 */
void burn_drive_free_subs(struct burn_drive *d)
{
	if (d->idata != NULL)
		free((void *) d->idata);
	d->idata = NULL;
	if (d->mdata != NULL) {
		burn_mdata_free_subs(d->mdata);
		free((void *) d->mdata);
	}
	d->mdata = NULL;
	if(d->toc_entry != NULL)
		free((void *) d->toc_entry);
	d->toc_entry = NULL;
	if (d->devname != NULL)
		free(d->devname);
	d->devname = NULL;
	if (d->stdio_fd >= 0)
		close (d->stdio_fd);
	d->stdio_fd = -1;
	sg_dispose_drive(d, 0);
}


/* ts A60904 : ticket 62, contribution by elmom */
/* splitting former burn_drive_free() (which freed all, into two calls) */
void burn_drive_free(struct burn_drive *d)
{
	if (d->global_index == -1)
		return;
	/* ts A60822 : close open fds before forgetting them */
	if (d->drive_role == 1)
		if (burn_drive_is_open(d)) {
			d->unlock(d);
			d->release(d);
		}
	burn_drive_free_subs(d);
	d->global_index = -1;
}

void burn_drive_free_all(void)
{
	int i;

	for (i = 0; i < drivetop + 1; i++)
		burn_drive_free(&(drive_array[i]));
	drivetop = -1;
	memset(drive_array, 0, sizeof(drive_array));
}


/* ts A60822 */
int burn_drive_is_open(struct burn_drive *d)
{
	if (d->drive_role != 1)
		return (d->stdio_fd >= 0);
	/* ts A61021 : moved decision to sg.c */
	return d->drive_is_open(d);
}


/* ts A60906 */
int burn_drive_force_idle(struct burn_drive *d)
{
	d->busy = BURN_DRIVE_IDLE;
	return 1;
}


/* ts A60906 */
int burn_drive_is_released(struct burn_drive *d)
{
        return !!d->released;
}


/* ts A60906 */
/** Inquires drive status in respect to degree of app usage.
    @param return -2 = drive is forgotten 
                  -1 = drive is closed (i.e. released explicitely)
                   0 = drive is open, not grabbed (after scan, before 1st grab)
                   1 = drive is grabbed but BURN_DRIVE_IDLE
                   2 = drive is grabbed, synchronous read/write interrupted
                  10 = drive is grabbing (BURN_DRIVE_GRABBING)
                 100 = drive is busy in cancelable state
                1000 = drive is in non-cancelable state 
           Expect a monotonous sequence of usage severity to emerge in future.
*/
int burn_drive_is_occupied(struct burn_drive *d)
{
	if(d->global_index < 0)
		return -2;
	if(!burn_drive_is_open(d))
		return -1;
	if(d->busy == BURN_DRIVE_GRABBING)
		return 10;
	if(d->released)
		return 0;
	if(d->busy == BURN_DRIVE_IDLE)
		return 1;
	if(d->busy == BURN_DRIVE_READING_SYNC ||
		 d->busy == BURN_DRIVE_WRITING_SYNC)
		return 2;
	if(d->busy == BURN_DRIVE_WRITING ||
	   d->busy == BURN_DRIVE_WRITING_LEADIN ||
	   d->busy == BURN_DRIVE_WRITING_LEADOUT ||
	   d->busy == BURN_DRIVE_WRITING_PREGAP) {

		/* ts A70928 */
		/* >>> how do i learn whether the writer thread is still
			 alive ? */;
			/* >>> what to do if writer is dead ? 
				At least sync disc ?*/;
		return 50;
	}
	if(d->busy == BURN_DRIVE_READING) {
		return 50;
	}
	return 1000;
}


/*
void drive_read_lead_in(int dnum)
{
	mmc_read_lead_in(&drive_array[dnum], get_4k());
}
*/
unsigned int burn_drive_count(void)
{
	return drivetop + 1;
}


/* ts A80801 */
int burn_drive_is_listed(char *path, struct burn_drive **found, int flag)
{
	int i, ret;
	char *drive_adr = NULL, *off_adr = NULL;

	BURN_ALLOC_MEM(drive_adr, char, BURN_DRIVE_ADR_LEN);
	BURN_ALLOC_MEM(off_adr, char, BURN_DRIVE_ADR_LEN);

	ret = burn_drive_convert_fs_adr(path, off_adr);
	if (ret <= 0)
		strcpy(off_adr, path);
	for (i = 0; i <= drivetop; i++) {
		if (drive_array[i].global_index < 0)
	continue;
		ret = burn_drive_d_get_adr(&(drive_array[i]), drive_adr);
		if (ret <= 0)
	continue;
		if(strcmp(off_adr, drive_adr) == 0) {
			if (found != NULL)
				*found= &(drive_array[i]);
			{ret= 1; goto ex;}
		}
	}
	ret= 0;
ex:;
	BURN_FREE_MEM(drive_adr);
	BURN_FREE_MEM(off_adr);
	return ret;
}


/* ts A61125 : media status aspects of burn_drive_grab() */
int burn_drive_inquire_media(struct burn_drive *d)
{

	/* ts A61225 : after loading the tray, mode page 2Ah can change */
	d->getcaps(d);

	/* ts A61020 : d->status was set to BURN_DISC_BLANK as pure guess */

        /* ts A71128 : run read_disc_info() for any recognizeable profile */
	if (d->current_profile > 0 || d->current_is_guessed_profile ||
	    d->mdata->cdr_write || d->mdata->cdrw_write ||
	    d->mdata->dvdr_write || d->mdata->dvdram_write) {

#define Libburn_knows_correct_state_after_loaD 1
#ifdef Libburn_knows_correct_state_after_loaD

		d->read_disc_info(d);

#else
		/* ts A61227 : This repeated read_disc_info seems
		               to be obsoleted by above d->getcaps(d).
		*/
		/* ts A60908 */
		/* Trying to stabilize the disc status after eventual load
		   without closing and re-opening the drive */
		/* This seems to work for burn_disc_erasable() .
		   Speed values on RIP-14 and LITE-ON 48125S are stable
		   and false, nevertheless. */
		int was_equal = 0, must_equal = 3, max_loop = 20;
		int loop_count, old_speed = -1234567890, new_speed= -987654321;
		int old_erasable = -1234567890, new_erasable = -987654321;

		fprintf(stderr,"LIBBURN_DEBUG: read_disc_info()\n");
		for (loop_count = 0; loop_count < max_loop; loop_count++){
			old_speed = new_speed;
			old_erasable = new_erasable;

			d->read_disc_info(d);
			if(d->status == BURN_DISC_UNSUITABLE)
		break;

			new_speed = burn_drive_get_write_speed(d);
			new_erasable = burn_disc_erasable(d);
		        if (new_speed == old_speed &&
			    new_erasable == old_erasable) {
				was_equal++;
				if (was_equal >= must_equal)
		break;
			} else
				was_equal = 0;
			/*
			if (loop_count >= 1 && was_equal == 0)
			*/
				fprintf(stderr,"LIBBURN_DEBUG: %d : speed %d:%d   erasable %d:%d\n",
					loop_count,old_speed,new_speed,old_erasable,new_erasable);
			usleep(100000);
		}
#endif /* ! Libburn_knows_correct_state_after_loaD */

	} else {
		if (d->current_profile == -1 || d->current_is_cd_profile)
			d->read_toc(d);

		/* ts A70314 , B10712 */
		if (d->status != BURN_DISC_EMPTY)
			d->status = BURN_DISC_UNSUITABLE;
	}
	return 1;
}

/* ts B10730 */
/* Send a default mode page 05 to CD and DVD-R-oids */
int burn_drive_send_default_page_05(struct burn_drive *d, int flag)
{
	struct burn_write_opts *opts;

	if (d->sent_default_page_05)
		return 0;
	if (!((d->status == BURN_DISC_APPENDABLE ||
               d->status == BURN_DISC_BLANK) &&
              (d->current_is_cd_profile || d->current_profile == 0x11 ||
	       d->current_profile == 0x14 || d->current_profile == 0x15)))
		return 0;
	opts = burn_write_opts_new(d);
	if (opts == NULL)
		return -1;
	if (d->status == BURN_DISC_APPENDABLE)
		burn_write_opts_set_write_type(opts,
			BURN_WRITE_TAO, BURN_BLOCK_MODE1);
	else
		burn_write_opts_set_write_type(opts,
			BURN_WRITE_SAO, BURN_BLOCK_SAO);
	d->send_write_parameters(d, NULL, -1, opts);
	burn_write_opts_free(opts);
	d->sent_default_page_05 = 1;
	return 1;
}


/* ts A70924 */
int burn_drive__fd_from_special_adr(char *adr)
{
	int fd = -1, i;

	if (strcmp(adr, "-") == 0)
		fd = 1;
	if(strncmp(adr, "/dev/fd/", 8) == 0) {
		for (i = 8; adr[i]; i++)
			if (!isdigit(adr[i]))
		break;
		if (i> 8 && adr[i] == 0)
			fd = atoi(adr + 8);
	}
	return fd;
}

/* @param flag bit0= accept read-only files and return 2 in this case
               bit1= accept write-only files and return 3 in this case
*/
static int burn_drive__is_rdwr(char *fname, int *stat_ret, 
                               struct stat *stbuf_ret,
                               off_t *read_size_ret, int flag)
{
	int fd, is_rdwr = 1, ret, getfl_ret, st_ret, mask;
	struct stat stbuf;
        off_t read_size = 0;

	memset(&stbuf, 0, sizeof(struct stat));
	fd = burn_drive__fd_from_special_adr(fname);
	if (fd >= 0)
		st_ret = fstat(fd, &stbuf);
	else
		st_ret = stat(fname, &stbuf);
	if (st_ret != -1) {
		is_rdwr = burn_os_is_2k_seekrw(fname, 0);
		ret = 1;
		if (S_ISREG(stbuf.st_mode))
			read_size = stbuf.st_size;
		else if (is_rdwr)
			ret = burn_os_stdio_capacity(fname, &read_size);
		if (ret <= 0 ||
		    read_size / (off_t) 2048 >= (off_t) 0x7ffffff0) 
			read_size = (off_t) 0x7ffffff0 * (off_t) 2048;  
	}

	if (is_rdwr && fd >= 0) {
		getfl_ret = fcntl(fd, F_GETFL);

/*
fprintf(stderr, "LIBBURN_DEBUG: burn_drive__is_rdwr: getfl_ret = %lX , O_RDWR = %lX , & = %lX , O_RDONLY = %lX\n", (unsigned long) getfl_ret, (unsigned long) O_RDWR, (unsigned long) (getfl_ret & O_RDWR), (unsigned long) O_RDONLY);
*/

		mask = O_RDWR | O_WRONLY | O_RDONLY;

		if (getfl_ret == -1 || (getfl_ret & mask) != O_RDWR)
			is_rdwr = 0;
		if ((flag & 1) && getfl_ret != -1 &&
		    (getfl_ret & mask) == O_RDONLY)
			is_rdwr = 2;
		if ((flag & 2) && getfl_ret != -1 &&
		    (getfl_ret & mask) == O_WRONLY)
			is_rdwr = 3;
	}
	if (stat_ret != NULL)
		*stat_ret = st_ret;
	if (stbuf_ret != NULL)
		memcpy(stbuf_ret, &stbuf, sizeof(struct stat));
	if (read_size_ret != NULL)
		*read_size_ret = read_size;
	return is_rdwr;
}


int burn_drive_grab_stdio(struct burn_drive *d, int flag)
{
	int stat_ret = -1, is_rdwr, ret;
	struct stat stbuf;
	off_t read_size= 0, size= 0;
	char fd_name[40], *name_pt = NULL;

	if(d->stdio_fd >= 0) {
		sprintf(fd_name, "/dev/fd/%d", d->stdio_fd);
		name_pt = fd_name;
	} else if (d->devname[0]) {
		name_pt = d->devname;
	}
	if (name_pt != NULL) {
		/* re-assess d->media_read_capacity and free space */
		is_rdwr = burn_drive__is_rdwr(name_pt, &stat_ret, &stbuf,
							 &read_size, 1 | 2);
		/* despite its name : last valid address, not size */
		d->media_read_capacity =
					read_size / 2048 - !(read_size % 2048);
		if ((stat_ret == -1 || is_rdwr) && d->devname[0]) { 
		       	ret = burn_os_stdio_capacity(d->devname, &size);
			if (ret > 0)
				burn_drive_set_media_capacity_remaining(d,
									size);
		}
	}

	d->released = 0;
	d->current_profile = 0xffff;
	if(d->drive_role == 2 || d->drive_role == 3) {
		d->status = BURN_DISC_BLANK;
	} else if(d->drive_role == 4) {
		if (d->media_read_capacity > 0)
			d->status = BURN_DISC_FULL;
		else
			d->status = BURN_DISC_EMPTY;
	} else if(d->drive_role == 5) {
		if (stat_ret != -1 && S_ISREG(stbuf.st_mode) &&
		    stbuf.st_size > 0) {
			d->status = BURN_DISC_APPENDABLE;
			if (stbuf.st_size / (off_t) 2048
			    >= 0x7ffffff0) {
				d->status = BURN_DISC_FULL;
				d->role_5_nwa = 0x7ffffff0;
			} else 
				d->role_5_nwa = stbuf.st_size / 2048 +
				              !!(stbuf.st_size % 2048);
		} else
			d->status = BURN_DISC_BLANK;
	} else {
		d->status = BURN_DISC_EMPTY;
		d->current_profile = 0;
	}
	d->busy = BURN_DRIVE_IDLE;
	return 1;
}


int burn_drive_grab(struct burn_drive *d, int le)
{
	int errcode;
	/* ts A61125 - B20122 */
	int ret, sose, signal_action_mem = -1;

	sose = d->silent_on_scsi_error;
	if (!d->released) {
                libdax_msgs_submit(libdax_messenger, d->global_index,
                                0x00020189, LIBDAX_MSGS_SEV_FATAL,
                                LIBDAX_MSGS_PRIO_LOW,
				"Drive is already grabbed by libburn", 0, 0);
		return 0;
	}
	if(d->drive_role != 1) {
		ret = burn_drive_grab_stdio(d, 0);
		return ret;
	}

	d->status = BURN_DISC_UNREADY;
	errcode = d->grab(d);
	if (errcode == 0)
		return 0;

	burn_grab_prepare_sig_action(&signal_action_mem, 0);
	d->busy = BURN_DRIVE_GRABBING;

	if (le)
		d->load(d);
	if (d->cancel || burn_is_aborting(0))
		{ret = 0; goto ex;}

	d->lock(d);
	if (d->cancel || burn_is_aborting(0))
		{ret = 0; goto ex;}

	/* ts A61118 */
	d->start_unit(d);
	if (d->cancel || burn_is_aborting(0))
		{ret = 0; goto ex;}

	/* ts A61202 : gave bit1 of le a meaning */
	if (!le)
		d->silent_on_scsi_error = 1;
	/* ts A61125 : outsourced media state inquiry aspects */
	ret = burn_drive_inquire_media(d);
	if (d->cancel || burn_is_aborting(0))
		{ret = 0; goto ex;}

	burn_drive_send_default_page_05(d, 0);
	if (d->cancel || burn_is_aborting(0))
		{ret = 0; goto ex;}

ex:;
	d->silent_on_scsi_error = sose;
	d->busy = BURN_DRIVE_IDLE;
	burn_grab_restore_sig_action(signal_action_mem, 0);
	return ret;
}


/* ts A71015 */
#define Libburn_ticket_62_re_register_is_possiblE 1

struct burn_drive *burn_drive_register(struct burn_drive *d)
{
#ifdef Libburn_ticket_62_re_register_is_possiblE
	int i;
#endif

	d->block_types[0] = 0;
	d->block_types[1] = 0;
	d->block_types[2] = 0;
	d->block_types[3] = 0;
	d->toc_temp = 0;
	d->nwa = 0;
	d->alba = 0;
	d->rlba = 0;
	d->cancel = 0;
	d->busy = BURN_DRIVE_IDLE;
	d->thread_pid = 0;
	d->thread_pid_valid = 0;
	memset(&(d->thread_tid), 0, sizeof(d->thread_tid));
	d->toc_entries = 0;
	d->toc_entry = NULL;
	d->disc = NULL;
	d->erasable = 0;

#ifdef Libburn_ticket_62_re_register_is_possiblE
	/* ts A60904 : ticket 62, contribution by elmom */
	/* Not yet accepted because no use case seen yet */
        /* ts A71015 : xorriso dialog imposes a use case now */

	/* This is supposed to find an already freed drive struct among
	   all the the ones that have been used before */
	for (i = 0; i < drivetop + 1; i++)
		if (drive_array[i].global_index == -1)
			break;
	d->global_index = i;
	memcpy(&drive_array[i], d, sizeof(struct burn_drive));
	pthread_mutex_init(&drive_array[i].access_lock, NULL);
	if (drivetop < i)
		drivetop = i;
	return &(drive_array[i]);

#else /* Libburn_ticket_62_re_register_is_possiblE */
	/* old A60904 : */
	/* Still active by default */

	d->global_index = drivetop + 1;
	memcpy(&drive_array[drivetop + 1], d, sizeof(struct burn_drive));
	pthread_mutex_init(&drive_array[drivetop + 1].access_lock, NULL);
	return &drive_array[++drivetop];

#endif /* ! Libburn_ticket_62_re_register_is_possiblE */

}


/* unregister most recently registered drive */
int burn_drive_unregister(struct burn_drive *d)
{
	if(d->global_index != drivetop)
		return 0;
	burn_drive_free(d);
	drivetop--;
	return 1;
}


/* ts A61021 : after-setup activities from sg.c:enumerate_common()
*/
struct burn_drive *burn_drive_finish_enum(struct burn_drive *d)
{
	struct burn_drive *t = NULL;
	char *msg = NULL;
	int ret;

	BURN_ALLOC_MEM(msg, char, BURN_DRIVE_ADR_LEN + 160);

	d->drive_role = 1; /* MMC drive */

	t = burn_drive_register(d);

	/* ts A60821 */
	mmc_function_spy(NULL, "enumerate_common : -------- doing grab");

	/* try to get the drive info */
	ret = t->grab(t);
	if (ret) {
	        t->getcaps(t);
	        t->unlock(t);
	        t->released = 1;
	} else {
		/* ts A90602 */
		d->mdata->valid = -1;
                sprintf(msg, "Unable to grab scanned drive %s", d->devname);
                libdax_msgs_submit(libdax_messenger, d->global_index,
                                0x0002016f, LIBDAX_MSGS_SEV_DEBUG,
                                LIBDAX_MSGS_PRIO_LOW, msg, 0, 0);
	        burn_drive_unregister(t);
		t = NULL;
	}

	/* ts A60821 */
	mmc_function_spy(NULL, "enumerate_common : ----- would release ");

ex:
	BURN_FREE_MEM(msg);
	return t;
}


/* ts A61125 : model aspects of burn_drive_release */
/* @param flag bit3= do not close d->stdio_fd
*/
int burn_drive_mark_unready(struct burn_drive *d, int flag)
{
	/* ts A61020 : mark media info as invalid */
	d->start_lba= -2000000000;
	d->end_lba= -2000000000;

	/* ts A61202 */
	d->current_profile = -1;
	d->current_has_feat21h = 0;
	d->current_feat2fh_byte4 = -1;

	d->status = BURN_DISC_UNREADY;
	if (d->toc_entry != NULL)
		free(d->toc_entry);
	d->toc_entry = NULL;
	d->toc_entries = 0;
	if (d->disc != NULL) {
		burn_disc_free(d->disc);
		d->disc = NULL;
	}
	if (!(flag & 8)) {
		if (d->stdio_fd >= 0)
			close (d->stdio_fd);
		d->stdio_fd = -1;
	}
	return 1;
}


/* ts A70918 : outsourced from burn_drive_release() and enhanced */
/** @param flag bit0-2 = mode : 0=unlock , 1=unlock+eject , 2=leave locked
                bit3= do not call d->release()
*/
int burn_drive_release_fl(struct burn_drive *d, int flag)
{
	if (d->released) {
		/* ts A61007 */
		libdax_msgs_submit(libdax_messenger,
				d->global_index, 0x00020105,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				"Drive is already released", 0, 0);
		return 0;
	}

	/* ts A61007 */
	/* ts A60906: one should not assume BURN_DRIVE_IDLE == 0 */
	/* a ssert(d->busy == BURN_DRIVE_IDLE); */
	if (d->busy != BURN_DRIVE_IDLE) {
		libdax_msgs_submit(libdax_messenger,
				d->global_index, 0x00020106,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				"Drive is busy on attempt to close", 0, 0);
		return 0;
	}

	if (d->drive_role == 1) {
		if (d->needs_sync_cache)
			d->sync_cache(d);
		if ((flag & 7) != 2)
			d->unlock(d);
		if ((flag & 7) == 1)
			d->eject(d);
		if (!(flag & 8)) {
			burn_drive_snooze(d, 0);
			d->release(d);
		}
	}

	d->needs_sync_cache = 0; /* just to be sure */
	d->released = 1;

	/* ts A61125 : outsourced model aspects */
	burn_drive_mark_unready(d, flag & 8);
	return 1;
}


/* API */
/* ts A90824
   @param flag bit0= wake up (else start snoozing)
*/
int burn_drive_snooze(struct burn_drive *d, int flag)
{
	if (d->drive_role != 1)
		return 0;
	if (flag & 1)
		d->start_unit(d);
	else
		d->stop_unit(d);
	return 1;
}


/* API */
void burn_drive_release(struct burn_drive *d, int le)
{
	burn_drive_release_fl(d, !!le);
}


/* ts B11002 */
/* API */
int burn_drive_re_assess(struct burn_drive *d, int flag)
{
	int ret, signal_action_mem;

	if (d->released) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
			     0x00020108,
			     LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			     "Drive is not grabbed on burn_drive_re_assess()",
			     0, 0);
		return 0;
	}
	burn_drive_release_fl(d, 2 | 8);

	if(d->drive_role != 1) {
		ret = burn_drive_grab_stdio(d, 0);
		return ret;
	}

	burn_grab_prepare_sig_action(&signal_action_mem, 0);
	d->busy = BURN_DRIVE_GRABBING;
	ret = burn_drive_inquire_media(d);
	burn_drive_send_default_page_05(d, 0);
	d->busy = BURN_DRIVE_IDLE;
	burn_grab_restore_sig_action(signal_action_mem, 0);
	d->released = 0;
	return ret;
}


/* ts A70918 */
/* API */
int burn_drive_leave_locked(struct burn_drive *d, int flag)
{
	return burn_drive_release_fl(d, 2);
}


/* ts A61007 : former void burn_wait_all() */
/* @param flag  bit0= demand freed drives (else released drives) */
int burn_drives_are_clear(int flag)
{
	int i;

	for (i = burn_drive_count() - 1; i >= 0; --i) {
		/* ts A60904 : ticket 62, contribution by elmom */
		if (drive_array[i].global_index == -1)
	continue;
		if (drive_array[i].released && !(flag & 1))
	continue;
		return 0;
	}
	return 1;
}


#if 0
void burn_wait_all(void)
{
	unsigned int i;
	int finished = 0;
	struct burn_drive *d;

	while (!finished) {
		finished = 1;
		d = drive_array;
		for (i = burn_drive_count(); i > 0; --i, ++d) {

			/* ts A60904 : ticket 62, contribution by elmom */
			if (d->global_index==-1)
				continue;

			a ssert(d->released); 
		}
		if (!finished)
			sleep(1);
	}
}
#endif


void burn_disc_erase_sync(struct burn_drive *d, int fast)
{
	int ret;

	if (d->drive_role == 5) { /* Random access write-only drive */
		ret = truncate(d->devname, (off_t) 0);
		if (ret == -1) {
			libdax_msgs_submit(libdax_messenger, -1,
			     0x00020182,
			     LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			     "Cannot truncate disk file for pseudo blanking",
			     0, 0);
			return;
		}
		d->role_5_nwa = 0;
		d->cancel = 0;
		d->status = BURN_DISC_BLANK;
		d->busy = BURN_DRIVE_IDLE;
		d->progress.sector = 0x10000;
		return;
	}

	d->cancel = 0;

#ifdef Libburn_reset_progress_asynC
	/* <<< This is now done in async.c */
	/* reset the progress */
	d->progress.session = 0;
	d->progress.sessions = 1;
	d->progress.track = 0;
	d->progress.tracks = 1;
	d->progress.index = 0;
	d->progress.indices = 1;
	d->progress.start_sector = 0;
	d->progress.sectors = 0x10000;
	d->progress.sector = 0;
#endif /* Libburn_reset_progress_asynC */

	d->erase(d, fast);
	d->busy = BURN_DRIVE_ERASING;

#ifdef Libburn_old_progress_looP

	/* read the initial 0 stage */
	while (!d->test_unit_ready(d) && d->get_erase_progress(d) == 0)
		sleep(1);
	while ((d->progress.sector = d->get_erase_progress(d)) > 0 ||
		!d->test_unit_ready(d))
		sleep(1);

#else /* Libburn_old_progress_looP */

	while (1) {
		ret = d->get_erase_progress(d);
		if (ret == -2 || ret > 0)
	break;
		sleep(1);
	}
	while (1) {
		ret = d->get_erase_progress(d);
		if(ret == -2)
	break;
		if (ret >= 0)
			d->progress.sector = ret;
		sleep(1);
        }

#endif /* ! Libburn_old_progress_looP */

	d->progress.sector = 0x10000;

	/* ts A61125 : update media state records */
	burn_drive_mark_unready(d, 0);
	if (d->drive_role == 1)
		burn_drive_inquire_media(d);
	d->busy = BURN_DRIVE_IDLE;
}

/*
   @param flag: bit0 = fill formatted size with zeros
                bit1, bit2 , bit4, bit5, bit7 - bit15 are for d->format_unit()
*/
void burn_disc_format_sync(struct burn_drive *d, off_t size, int flag)
{
	int ret, buf_secs, err, i, stages = 1, pbase, pfill, pseudo_sector;
	off_t num_bufs;
	char msg[80];
	struct buffer *buf = NULL, *buf_mem = d->buffer;

	BURN_ALLOC_MEM(buf, struct buffer, 1);

#ifdef Libburn_reset_progress_asynC
	/* <<< This is now done in async.c */
	/* reset the progress */
	d->progress.session = 0;
	d->progress.sessions = 1;
	d->progress.track = 0;
	d->progress.tracks = 1;
	d->progress.index = 0;
	d->progress.indices = 1;
	d->progress.start_sector = 0;
	d->progress.sectors = 0x10000;
	d->progress.sector = 0;
#endif /* Libburn_reset_progress_asynC */

	stages = 1 + ((flag & 1) && size > 1024 * 1024);
	d->cancel = 0;
	d->busy = BURN_DRIVE_FORMATTING;

	ret = d->format_unit(d, size, flag & 0xfff6); /* forward bits */
	if (ret <= 0)
		d->cancel = 1;

#ifdef Libburn_old_progress_looP

	while (!d->test_unit_ready(d) && d->get_erase_progress(d) == 0)
		sleep(1);
	while ((pseudo_sector = d->get_erase_progress(d)) > 0 ||
		!d->test_unit_ready(d)) {
		d->progress.sector = pseudo_sector / stages;
		sleep(1);
        }

#else /* Libburn_old_progress_looP */

	while (1) {
		ret = d->get_erase_progress(d);
		if (ret == -2 || ret > 0)
	break;
		sleep(1);
	}
	while (1) {
		pseudo_sector = d->get_erase_progress(d);
		if(pseudo_sector == -2)
	break;
		if (pseudo_sector >= 0)
			d->progress.sector = pseudo_sector / stages;
		sleep(1);
        }

#endif /* ! Libburn_old_progress_looP */

	d->sync_cache(d);

	if (size <= 0)
		goto ex;

	/* update media state records */
	burn_drive_mark_unready(d, 0);
	burn_drive_inquire_media(d);
	if (flag & 1) {
		/* write size in zeros */;
		pbase = 0x8000 + 0x7fff * (stages == 1);
		pfill = 0xffff - pbase;
		buf_secs = 16; /* Must not be more than 16 */
		num_bufs = size / buf_secs / 2048;
		if (num_bufs > 0x7fffffff) {
			d->cancel = 1;
			goto ex;
		}

		/* <<< */
		sprintf(msg,
			"Writing %.f sectors of zeros to formatted media",
			(double) num_bufs * (double) buf_secs);
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00000002,
				LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
				msg, 0, 0);

		d->buffer = buf;
		memset(d->buffer, 0, sizeof(struct buffer));
		d->buffer->bytes = buf_secs * 2048;
		d->buffer->sectors = buf_secs;
		d->busy = BURN_DRIVE_WRITING;
		for (i = 0; i < num_bufs; i++) {
			d->nwa = i * buf_secs;
			err = d->write(d, d->nwa, d->buffer);
			if (err == BE_CANCELLED || d->cancel) {
				d->cancel = 1;
		break;
			}
			d->progress.sector = pbase
				+ pfill * ((double) i / (double) num_bufs);
		}
		d->sync_cache(d);
		if (d->current_profile == 0x13 || d->current_profile == 0x1a) {
			/* DVD-RW or DVD+RW */
			d->busy = BURN_DRIVE_CLOSING_SESSION;
			/* CLOSE SESSION, 010b */
			d->close_track_session(d, 1, 0);
			d->busy = BURN_DRIVE_WRITING;
		}
	}
ex:;
	d->progress.sector = 0x10000;
	d->busy = BURN_DRIVE_IDLE;
	d->buffer = buf_mem;
	BURN_FREE_MEM(buf);
}


/* ts A70112 API */
int burn_disc_get_formats(struct burn_drive *d, int *status, off_t *size,
				unsigned *bl_sas, int *num_formats)
{
	int ret;

	*status = 0;
	*size = 0;
	*bl_sas = 0;
	*num_formats = 0;
	if (d->drive_role != 1)
		return 0;
	ret = d->read_format_capacities(d, 0x00);
	if (ret <= 0)
		return 0;
	*status = d->format_descr_type;
	*size = d->format_curr_max_size;
	*bl_sas = d->format_curr_blsas;
	*num_formats = d->num_format_descr;
	return 1;
}


/* ts A70112 API */
int burn_disc_get_format_descr(struct burn_drive *d, int index,
				int *type, off_t *size, unsigned *tdp)
{
	*type = 0;
	*size = 0;
	*tdp = 0;
	if (index < 0 || index >= d->num_format_descr)
		return 0;
	*type = d->format_descriptors[index].type;
	*size = d->format_descriptors[index].size;
	*tdp = d->format_descriptors[index].tdp;
	return 1;
}


enum burn_disc_status burn_disc_get_status(struct burn_drive *d)
{
	/* ts A61007 */
	/* a ssert(!d->released); */
	if (d->released) {
		libdax_msgs_submit(libdax_messenger,
				d->global_index, 0x00020108,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				"Drive is not grabbed on disc status inquiry",
				0, 0);
		return BURN_DISC_UNGRABBED;
	}

	return d->status;
}

int burn_disc_erasable(struct burn_drive *d)
{
	return d->erasable;
}
enum burn_drive_status burn_drive_get_status(struct burn_drive *d,
					     struct burn_progress *p)
{
	/* --- Part of asynchronous signal handling --- */
	/* This frequently used call may be used to react on messages from
	   the libburn built-in signal handler.
	*/

	/* ts B00225 :
	   If aborting with action 2:
	   catch control thread after it returned from signal handler.
	   Let it run burn_abort(4440,...) 
	*/
	burn_init_catch_on_abort(0);

	/* ts A70928 : inform control thread of signal in sub-threads */
	if (burn_builtin_triggered_action < 2 && burn_global_abort_level > 0)
		burn_global_abort_level++;
	if (burn_builtin_triggered_action < 2 && burn_global_abort_level > 5) {
		if (burn_global_signal_handler == NULL)
			kill(getpid(), burn_global_abort_signum);
		else
			(*burn_global_signal_handler)
				(burn_global_signal_handle,
				 burn_global_abort_signum, 0);
		burn_global_abort_level = -1;
	}

	/* --- End of asynchronous signal handling --- */


	if (p != NULL) {
		memcpy(p, &(d->progress), sizeof(struct burn_progress));
		/* TODO: add mutex */
	}
	return d->busy;
}

int burn_drive_set_stream_recording(struct burn_drive *d, int recmode,
                                    int start, int flag)
{

	if (recmode == 1)
		d->do_stream_recording = 1;
	else if (recmode == -1)
		d->do_stream_recording = 0;
	d->stream_recording_start = start;
	return(1);
}

void burn_drive_cancel(struct burn_drive *d)
{
/* ts B00225 : these mutexes are unnecessary because "= 1" is atomar.
	pthread_mutex_lock(&d->access_lock);
*/
	if (!d->cancel) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
				LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
				"burn_drive_cancel() was called", 0, 0);
	}
	d->cancel = 1;
/*
	pthread_mutex_unlock(&d->access_lock);
*/
}


static void strip_spaces(char *str)
{
	char *tmp;

	tmp = str + strlen(str) - 1;
	while (isspace(*tmp))
		*(tmp--) = '\0';

	tmp = str;
	while (*tmp) {
		if (isspace(*tmp) && isspace(*(tmp + 1))) {
			char *tmp2;

			for (tmp2 = tmp + 1; *tmp2; ++tmp2)
				*(tmp2 - 1) = *tmp2;
			*(tmp2 - 1) = '\0';
		} else
			++tmp;
	}
}

static int drive_getcaps(struct burn_drive *d, struct burn_drive_info *out)
{
	struct burn_scsi_inquiry_data *id;

	/* ts A61007 : now prevented in enumerate_common() */
#if 0
	a ssert(d->idata);
	a ssert(d->mdata);
#endif

	if(d->idata->valid <= 0 || d->mdata->valid <= 0)
		return 0;

	id = (struct burn_scsi_inquiry_data *)d->idata;

	memcpy(out->vendor, id->vendor, sizeof(id->vendor));
	strip_spaces(out->vendor);
	memcpy(out->product, id->product, sizeof(id->product));
	strip_spaces(out->product);
	memcpy(out->revision, id->revision, sizeof(id->revision));
	strip_spaces(out->revision);
	strncpy(out->location, d->devname, 16);
	out->location[16] = '\0';
	out->buffer_size = d->mdata->buffer_size;
	out->read_dvdram = !!d->mdata->dvdram_read;
	out->read_dvdr = !!d->mdata->dvdr_read;
	out->read_dvdrom = !!d->mdata->dvdrom_read;
	out->read_cdr = !!d->mdata->cdr_read;
	out->read_cdrw = !!d->mdata->cdrw_read;
	out->write_dvdram = !!d->mdata->dvdram_write;
	out->write_dvdr = !!d->mdata->dvdr_write;
	out->write_cdr = !!d->mdata->cdr_write;
	out->write_cdrw = !!d->mdata->cdrw_write;
	out->write_simulate = !!d->mdata->simulate;
	out->c2_errors = !!d->mdata->c2_pointers;
	out->drive = d;

#ifdef Libburn_dummy_probe_write_modeS

	/* ts A91112 */
	/* Set default block types. The call d->probe_write_modes() is quite
	   obtrusive. It may be performed explicitely by new API call
             burn_drive_probe_cd_write_modes().
	*/
	if (out->write_dvdram || out->write_dvdr ||
	    out->write_cdrw || out->write_cdr) {
		out->tao_block_types = d->block_types[BURN_WRITE_TAO] =
					BURN_BLOCK_MODE1 | BURN_BLOCK_RAW0;
		out->sao_block_types = d->block_types[BURN_WRITE_SAO] =
					BURN_BLOCK_SAO;
	} else {
		out->tao_block_types = d->block_types[BURN_WRITE_TAO] = 0;
		out->sao_block_types = d->block_types[BURN_WRITE_SAO] = 0;
	}
	out->raw_block_types = d->block_types[BURN_WRITE_RAW] = 0;
	out->packet_block_types = 0;

#else /* Libburn_dummy_probe_write_modeS */

	/* update available block types for burners */
	if (out->write_dvdram || out->write_dvdr ||
	    out->write_cdrw || out->write_cdr)
		d->probe_write_modes(d);
	out->tao_block_types = d->block_types[BURN_WRITE_TAO];
	out->sao_block_types = d->block_types[BURN_WRITE_SAO];
	out->raw_block_types = d->block_types[BURN_WRITE_RAW];
	out->packet_block_types = d->block_types[BURN_WRITE_PACKET];

#endif /* ! Libburn_dummy_probe_write_modeS */

	return 1;
}



/* ts A91112 - B00114 API */
/* Probe available CD write modes and block types.
*/
int burn_drive_probe_cd_write_modes(struct burn_drive_info *dinfo)
{
	struct burn_drive *d = dinfo->drive;

	if (d == NULL)
		return 0;
	if (dinfo->write_dvdram || dinfo->write_dvdr ||
	    dinfo->write_cdrw || dinfo->write_cdr)
		d->probe_write_modes(d);
	dinfo->tao_block_types = d->block_types[BURN_WRITE_TAO];
	dinfo->sao_block_types = d->block_types[BURN_WRITE_SAO];
	dinfo->raw_block_types = d->block_types[BURN_WRITE_RAW];
	dinfo->packet_block_types = d->block_types[BURN_WRITE_PACKET];
	return 1;
}


/* ts A70907 : added parameter flag */
/* @param flag bit0= reset global drive list */
int burn_drive_scan_sync(struct burn_drive_info *drives[],
			 unsigned int *n_drives, int flag)
{
	/* ts A70907 :
	   There seems to have been a misunderstanding about the role of
	   burn_drive_scan_sync(). It needs no static state because it
	   is only started once during an asynchronous scan operation.
	   Its starter, burn_drive_scan(), is the one which ends immediately
	   and gets called repeatedly. It acts on start of scanning by
	   calling burn_drive_scan_sync(), returns idle while scanning is
	   not done and finally removes the worker object which represented
	   burn_drive_scan_sync().
	   The scanning itself is not parallel but enumerates sequentially
	   drive by drive (within scsi_enumerate_drives()).

	   I will use "scanned" for marking drives found by previous runs.
           It will not be static any more.
	*/
	/* ts A71015 : this makes only trouble : static int scanning = 0; */
	/* ts A70907 :
	   These variables are too small anyway. We got up to 255 drives.
	   static int scanned = 0, found = 0;
	   Variable "found" was only set but never read.
	*/
	unsigned char scanned[32];
	unsigned count = 0;
	int i, ret;

	/* ts A61007 : moved up to burn_drive_scan() */
	/* a ssert(burn_running); */


	/* ts A61007 : test moved up to burn_drive_scan()
		               burn_wait_all() is obsoleted */
#if 0
	/* make sure the drives aren't in use */
	burn_wait_all();	/* make sure the queue cleans up
				   before checking for the released
				   state */
#endif /* 0 */

	*n_drives = 0;

	/* ts A70907 : wether to scan from scratch or to extend */
	for (i = 0; i < (int) sizeof(scanned); i++)
		scanned[i] = 0;
	if (flag & 1) {
		burn_drive_free_all();
	} else {
		for (i = 0; i <= drivetop; i++) 
			if (drive_array[i].global_index >= 0)
				scanned[i / 8] |=  (1 << (i % 8));
	}

	/* refresh the lib's drives */

	/* ts A61115 : formerly sg_enumerate(); ata_enumerate(); */
	scsi_enumerate_drives();

	count = burn_drive_count();
	if (count) {
		/* ts A70907 :
		   Extra array element marks end of array. */
		*drives = calloc(count + 1,
				sizeof(struct burn_drive_info));
		if (*drives == NULL) {
	       		libdax_msgs_submit(libdax_messenger, -1, 0x00000003,
	               			LIBDAX_MSGS_SEV_FATAL,
					LIBDAX_MSGS_PRIO_HIGH,
					"Out of virtual memory", 0, 0);
			return -1;
		} else
			for (i = 0; i <= (int) count; i++) /* invalidate */
				(*drives)[i].drive = NULL;
	} else
		*drives = NULL;

	for (i = 0; i < (int) count; ++i) {
		if (scanned[i / 8] & (1 << (i % 8)))
	continue;		/* device already scanned by previous run */
		if (drive_array[i].global_index < 0)
	continue;		/* invalid device */

		/* ts A90602 : This old loop is not plausible. See A70907.
		  while (!drive_getcaps(&drive_array[i],
		         &(*drives)[*n_drives])) {
			sleep(1);
		  }
		*/
		/* ts A90602 : A single call shall do (rather than a loop) */
		ret = drive_getcaps(&drive_array[i], &(*drives)[*n_drives]);
		if (ret > 0)
			(*n_drives)++;
		scanned[i / 8] |= 1 << (i % 8);
	}
	if (*drives != NULL && *n_drives == 0) {
		free ((char *) *drives);
		*drives = NULL;
	}

	return(1);
}

/* ts A61001 : internal call */
int burn_drive_forget(struct burn_drive *d, int force)
{
	int occup;

	occup = burn_drive_is_occupied(d);
/*
	fprintf(stderr, "libburn: experimental: occup == %d\n",occup);
*/
	if(occup <= -2)
		return 2;
	if(occup > 0)
		if(force < 1)
			return 0; 
	if(occup >= 10)
		return 0;

	/* >>> do any drive calming here */;


	burn_drive_force_idle(d);
	if(occup > 0 && !burn_drive_is_released(d))
		burn_drive_release(d,0);
	burn_drive_free(d);
	return 1;
}

/* API call */
int burn_drive_info_forget(struct burn_drive_info *info, int force)
{
	return burn_drive_forget(info->drive, force);
}


void burn_drive_info_free(struct burn_drive_info drive_infos[])
{
#ifndef Libburn_free_all_drives_on_infO
	int i;
#endif

/* ts A60904 : ticket 62, contribution by elmom */
/* clarifying the meaning and the identity of the victim */

	if(drive_infos == NULL)
		return;

#ifndef Libburn_free_all_drives_on_infO

#ifdef Not_yeT
	int new_drivetop;

	/* ts A71015: compute reduced drivetop counter */
	new_drivetop = drivetop;
	for (i = 0; drive_infos[i].drive != NULL; i++)
		if (drive_infos[i].global_index == new_drivetop
		    && new_drivetop >= 0) {
			new_drivetop--;
			i = 0;
		}
#endif /* Not_yeT */

	/* ts A70907 : Solution for wrong behavior below */
	for (i = 0; drive_infos[i].drive != NULL; i++)
		burn_drive_free(drive_infos[i].drive);

#ifdef Not_yeT
	drivetop = new_drivetop;
#endif /* Not_yeT */

#endif /* ! Libburn_free_all_drives_on_infO */

	/* ts A60904 : This looks a bit weird. [ts A70907 : not any more]
	   burn_drive_info is not the manager of burn_drive but only its
	   spokesperson. To my knowlege drive_infos from burn_drive_scan()
	   are not memorized globally. */
	free((void *) drive_infos);

#ifdef Libburn_free_all_drives_on_infO
	/* ts A70903 : THIS IS WRONG !      (disabled now)
	   It endangers multi drive usage.
	   This call is not entitled to delete all drives, only the
	   ones of the array which it recieves a parmeter.

	   Problem:  It was unclear how many items are listed in drive_infos
           Solution: Added a end marker element to any burn_drive_info array 
                     The mark can be recognized by having drive == NULL
	*/
	burn_drive_free_all();
#endif
}


struct burn_disc *burn_drive_get_disc(struct burn_drive *d)
{
	/* ts A61022: SIGSEGV on calling this function with blank media */
	if(d->disc == NULL)
		return NULL;

	d->disc->refcnt++;
	return d->disc;
}

void burn_drive_set_speed(struct burn_drive *d, int r, int w)
{
	d->nominal_write_speed = w;
	if(d->drive_role != 1)
		return;
	d->set_speed(d, r, w);
}


/* ts A70711  API function */
int burn_drive_set_buffer_waiting(struct burn_drive *d, int enable,
				int min_usec, int max_usec, int timeout_sec,
				int min_percent, int max_percent)
{

	if (enable >= 0)
		d->wait_for_buffer_free = !!enable;
	if (min_usec >= 0)
		d->wfb_min_usec = min_usec;
	if (max_usec >= 0)
		d->wfb_max_usec = max_usec;
	if (timeout_sec >= 0)
		d->wfb_timeout_sec = timeout_sec;
        if (min_percent >= 0) {
		if (min_percent < 25 || min_percent > 100)
			return 0;
		d->wfb_min_percent = min_percent;
	}
	if (max_percent >= 0) {
		if (max_percent < 25 || max_percent > 100)
			return 0;
		d->wfb_max_percent = max_percent;
	}
	return 1;
}


int burn_msf_to_sectors(int m, int s, int f)
{
	return (m * 60 + s) * 75 + f;
}

void burn_sectors_to_msf(int sectors, int *m, int *s, int *f)
{
	*m = sectors / (60 * 75);
	*s = (sectors - *m * 60 * 75) / 75;
	*f = sectors - *m * 60 * 75 - *s * 75;
}

int burn_drive_get_read_speed(struct burn_drive *d)
{
	if(d->mdata->valid <= 0)
		return 0;
	return d->mdata->max_read_speed;
}

int burn_drive_get_write_speed(struct burn_drive *d)
{
	if(d->mdata->valid <= 0)
		return 0;
	return d->mdata->max_write_speed;
}

/* ts A61021 : New API function */
int burn_drive_get_min_write_speed(struct burn_drive *d)
{
	if(d->mdata->valid <= 0)
		return 0;
	return d->mdata->min_write_speed;
}


/* ts A51221 */
static char *enumeration_whitelist[BURN_DRIVE_WHITELIST_LEN];
static int enumeration_whitelist_top = -1;

/** Add a device to the list of permissible drives. As soon as some entry is in
    the whitelist all non-listed drives are banned from enumeration.
    @return 1 success, <=0 failure
*/
int burn_drive_add_whitelist(char *device_address)
{
	char *new_item;
	if(enumeration_whitelist_top+1 >= BURN_DRIVE_WHITELIST_LEN)
		return 0;
	enumeration_whitelist_top++;
	new_item = calloc(1, strlen(device_address) + 1);
	if (new_item == NULL)
		return -1;
	strcpy(new_item, device_address);
	enumeration_whitelist[enumeration_whitelist_top] = new_item;
	return 1;
}

/** Remove all drives from whitelist. This enables all possible drives. */
void burn_drive_clear_whitelist(void)
{
	int i;
	for (i = 0; i <= enumeration_whitelist_top; i++)
		free(enumeration_whitelist[i]);
	enumeration_whitelist_top = -1;
}

int burn_drive_is_banned(char *device_address)
{
	int i;
	if(enumeration_whitelist_top<0)
		return 0;
	for (i = 0; i <= enumeration_whitelist_top; i++) 
		if (strcmp(enumeration_whitelist[i], device_address) == 0)
			return 0;
	return 1;
}


/* ts A80731 */
int burn_drive_whitelist_count(void)
{
	return enumeration_whitelist_top + 1;
}

char *burn_drive_whitelist_item(int idx, int flag)
{
	if (idx < 0 || idx > enumeration_whitelist_top)
		return NULL;
	return enumeration_whitelist[idx];
}


static int burn_role_by_access(char *fname, int flag)
{
/* We normally need _LARGEFILE64_SOURCE defined by the build system.
   Nevertheless the system might use large address integers by default.
*/
#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
	int fd;
        
	fd = open(fname, O_RDWR | O_LARGEFILE);
	if (fd != -1) {
		close(fd);
		return 2;
	}
	fd = open(fname, O_RDONLY | O_LARGEFILE);
        if (fd != -1) {
		close(fd);
		return 4;
	}
	fd = open(fname, O_WRONLY | O_LARGEFILE);
	if (fd != -1) {
		close(fd);
		return 5;
	}
	if (flag & 1)
		return 0;
	return 2;
}


/* ts A70903 : Implements adquiration of pseudo drives */
int burn_drive_grab_dummy(struct burn_drive_info *drive_infos[], char *fname)
{
	int ret = -1, role = 0, fd;
	int is_rdwr = 0, stat_ret = -1;
	/* divided by 512 it needs to fit into a signed long integer */
	off_t size = ((off_t) (512 * 1024 * 1024 - 1) * (off_t) 2048);
	off_t read_size = -1;
	struct burn_drive *d= NULL, *regd_d;
	struct stat stbuf;

	if (fname[0] != 0) {
		fd = burn_drive__fd_from_special_adr(fname);
		is_rdwr = burn_drive__is_rdwr(fname, &stat_ret, &stbuf,
						&read_size, 1 | 2);
		if (stat_ret == -1 || is_rdwr) {
			ret = burn_os_stdio_capacity(fname, &size);
			if (ret == -1) {
				libdax_msgs_submit(libdax_messenger, -1,
				 0x00020009,
				 LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				 "Neither stdio-path nor its directory exist",
				 0, 0);
				return 0;
			} else if (ret == -2) {
				libdax_msgs_submit(libdax_messenger, -1,
				0x00020005,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				"Failed to open device (a pseudo-drive)",
				errno, 0);
				return 0;
			}
			if (fname[0] != 0) {
				if (is_rdwr == 2 &&
				    (burn_drive_role_4_allowed & 1))
					role = 4;
				else if (is_rdwr == 3 &&
				    (burn_drive_role_4_allowed & 1))
					role = 5;
				else
					role = 2;
				if (stat_ret != -1 && role == 2 && fd == -1 &&
				    (burn_drive_role_4_allowed & 3) == 3)
					role = burn_role_by_access(fname,
					    !!(burn_drive_role_4_allowed & 4));
			} else
				role = 0;
		} else {
			role = 3;
		}
	}
	d= (struct burn_drive *) calloc(1, sizeof(struct burn_drive));
	if (d == NULL)
		return 0;
	burn_setup_drive(d, fname);
	d->status = BURN_DISC_EMPTY;

	d->drive_role = role;
	ret = burn_scsi_setup_drive(d, -1, -1, -1, -1, -1, 0);
	if (ret <= 0)
		goto ex;
	regd_d = burn_drive_register(d);
	if (regd_d == NULL) {
		ret = -1;
		goto ex;
	}
	free((char *) d); /* all sub pointers have been copied to *regd_d */
	d = regd_d;
	if (d->drive_role >= 2 && d->drive_role <= 5) {
		if (d->drive_role == 4) {
			if (read_size > 0)
				d->status = BURN_DISC_FULL;
			else
				d->status = BURN_DISC_EMPTY;
			d->block_types[BURN_WRITE_TAO] = 0;
			d->block_types[BURN_WRITE_SAO] = 0;
		} else {
			if (d->drive_role == 5 && stat_ret != -1 &&
			    S_ISREG(stbuf.st_mode) && stbuf.st_size > 0 &&
			    (burn_drive_role_4_allowed & 8)) {
				d->status = BURN_DISC_APPENDABLE;
				d->block_types[BURN_WRITE_SAO] = 0;
				if (stbuf.st_size / (off_t) 2048
				    >= 0x7ffffff0) {
					d->status = BURN_DISC_FULL;
					d->role_5_nwa = 0x7ffffff0;
				} else 
					d->role_5_nwa = stbuf.st_size / 2048 +
					              !!(stbuf.st_size % 2048);
			} else {
				d->status = BURN_DISC_BLANK;
				d->block_types[BURN_WRITE_SAO] =
								BURN_BLOCK_SAO;
				d->role_5_nwa = 0;
			}
			d->block_types[BURN_WRITE_TAO] = BURN_BLOCK_MODE1;
		}
		d->current_profile = 0xffff; /* MMC for non-compliant drive */
		strcpy(d->current_profile_text,"stdio file");
		d->current_is_cd_profile = 0;
		d->current_is_supported_profile = 1;
		if (read_size >= 0)
			/* despite its name : last valid address, not size */
			d->media_read_capacity =
				read_size / 2048 - !(read_size % 2048);
		burn_drive_set_media_capacity_remaining(d, size);
	} else
		d->current_profile = 0; /* Drives return this if empty */

	*drive_infos = calloc(2, sizeof(struct burn_drive_info));
	if (*drive_infos == NULL)
		goto ex;
	(*drive_infos)[0].drive = d;
	(*drive_infos)[1].drive = NULL; /* End-Of-List mark */
	(*drive_infos)[0].tao_block_types = d->block_types[BURN_WRITE_TAO];
	(*drive_infos)[0].sao_block_types = d->block_types[BURN_WRITE_SAO];
	if (d->drive_role == 2) {
		strcpy((*drive_infos)[0].vendor,"YOYODYNE");
		strcpy((*drive_infos)[0].product,"WARP DRIVE");
		strcpy((*drive_infos)[0].revision,"FX01");
	} else if (d->drive_role == 3) {
		strcpy((*drive_infos)[0].vendor,"YOYODYNE");
		strcpy((*drive_infos)[0].product,"BLACKHOLE");
		strcpy((*drive_infos)[0].revision,"FX02");
	} else if (d->drive_role == 4) {
		strcpy((*drive_infos)[0].vendor,"YOYODYNE");
		strcpy((*drive_infos)[0].product,"WARP DRIVE");
		strcpy((*drive_infos)[0].revision,"FX03");
	} else if (d->drive_role == 5) {
		strcpy((*drive_infos)[0].vendor,"YOYODYNE");
		strcpy((*drive_infos)[0].product,"WARP DRIVE");
		strcpy((*drive_infos)[0].revision,"FX04");
	} else {
		strcpy((*drive_infos)[0].vendor,"FERENGI");
		strcpy((*drive_infos)[0].product,"VAPORWARE");
		strcpy((*drive_infos)[0].revision,"0000");
	}
	d->released = 0;
	ret = 1;
ex:;
	if (ret <= 0 && d != NULL) {
		burn_drive_free_subs(d);
		free((char *) d);
	}
	return ret;
}


/* ts A60823 */
/** Aquire a drive with known persistent address. 
*/
int burn_drive_scan_and_grab(struct burn_drive_info *drive_infos[], char* adr,
			     int load)
{
	unsigned int n_drives;
	int ret, i;

	/* check wether drive adress is already registered */
	for (i = 0; i <= drivetop; i++)
		if (drive_array[i].global_index >= 0)
			if (strcmp(drive_array[i].devname, adr) == 0)
	break;
	if (i <= drivetop) {
		libdax_msgs_submit(libdax_messenger, i,
				0x0002014b,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				"Drive is already registered resp. scanned",
				0, 0);
		return -1;
	}

	if (strncmp(adr, "stdio:", 6) == 0) {
		ret = burn_drive_grab_dummy(drive_infos, adr + 6);
		return ret;
	}

	burn_drive_clear_whitelist();
	burn_drive_add_whitelist(adr);
/*
	fprintf(stderr,"libburn: experimental: burn_drive_scan_and_grab(%s)\n",
		adr);
*/

	/* ts A70907 : now calling synchronously rather than looping */
	ret = burn_drive_scan_sync(drive_infos, &n_drives, 0);
	if (ret < 0)
		return -1;

	if (n_drives == 0)
		return 0;
/*
	fprintf(stderr, "libburn: experimental: n_drives %d , drivetop %d\n",
		n_drives, drivetop);
	if (n_drives > 0)
		fprintf(stderr, "libburn: experimental: global_index %d\n",
			drive_infos[0]->drive->global_index);
*/

	ret = burn_drive_grab(drive_infos[0]->drive, load);
	if (ret != 1)
		return -1;
	return 1;
}

/* ts A60925 */
/** Simple debug message frontend to libdax_msgs_submit().
    If arg is not NULL, then fmt MUST contain exactly one %s and no
    other sprintf() %-formatters.
*/
int burn_drive_adr_debug_msg(char *fmt, char *arg)
{
	int ret;
	char *msg = NULL, *msgpt;

	BURN_ALLOC_MEM(msg, char, 4096);
	msgpt = msg;
	if(arg != NULL)
		sprintf(msg, fmt, arg);
	else
		msgpt = fmt;
	if(libdax_messenger == NULL)
		return 0;
	ret = libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
				LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_ZERO,
				msgpt, 0, 0);
ex:;
	BURN_FREE_MEM(msg);
	return ret;
}

/* ts A60923 */ /* ts A70906 : promoted to API */
/** Inquire the persistent address of the given drive. */
int burn_drive_d_get_adr(struct burn_drive *d, char adr[])
{
	if (strlen(d->devname) >= BURN_DRIVE_ADR_LEN) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020110,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				"Persistent drive address too long", 0, 0);
		return -1;
	}
	strcpy(adr,d->devname);
	return 1;
}

/* ts A60823 - A60923 */ /* A70906 : Now legacy API call */
/** Inquire the persistent address of the given drive. */
int burn_drive_get_adr(struct burn_drive_info *drive_info, char adr[])
{
	int ret;

	ret = burn_drive_d_get_adr(drive_info->drive, adr);
	return ret;
}




/* ts A60922 ticket 33 */
/** Evaluate wether the given address would be enumerated by libburn */
int burn_drive_is_enumerable_adr(char *adr)
{
	return sg_is_enumerable_adr(adr);
}

#define BURN_DRIVE_MAX_LINK_DEPTH 20

/* ts A60922 ticket 33 */
/* @param flag  bit0= no debug messages
                bit1= resolve only links,
                      do not rely on drive list for resolving via st_rdev
*/
int burn_drive_resolve_link(char *path, char adr[], int *recursion_count,
				int flag)
{
	int ret, link_target_size = 4096;
	char *link_target = NULL, *msg = NULL, *link_adr = NULL, *adrpt;
	struct stat stbuf;

	BURN_ALLOC_MEM(link_target, char, link_target_size);
	BURN_ALLOC_MEM(msg, char, link_target_size + 100);
	BURN_ALLOC_MEM(link_adr, char, link_target_size);

	if (flag & 1)
		burn_drive_adr_debug_msg("burn_drive_resolve_link( %s )",
									path);
	if (*recursion_count >= BURN_DRIVE_MAX_LINK_DEPTH) {
		if (flag & 1)
			burn_drive_adr_debug_msg(
			"burn_drive_resolve_link aborts because link too deep",
			NULL);
		{ret = 0; goto ex;}
	}
	(*recursion_count)++;
	ret = readlink(path, link_target, link_target_size);
	if (ret == -1) {
		if (flag & 1)
			burn_drive_adr_debug_msg("readlink( %s ) returns -1",
									path);
		{ret = 0; goto ex;}
	}
	if (ret >= link_target_size - 1) {
		sprintf(msg,"readlink( %s ) returns %d (too much)", path, ret);
		if (flag & 1)
			burn_drive_adr_debug_msg(msg, NULL);
		{ret = -1; goto ex;}
	}
	link_target[ret] = 0;
	adrpt = link_target;
	if (link_target[0] != '/') {
		strcpy(link_adr, path);
		if ((adrpt = strrchr(link_adr, '/')) != NULL) {
			strcpy(adrpt + 1, link_target);
			adrpt = link_adr;
		} else
			adrpt = link_target;
	}
	if (flag & 2) {
		/* Link-only recursion */
		if (lstat(adrpt, &stbuf) == -1) {
			;
		} else if((stbuf.st_mode & S_IFMT) == S_IFLNK) {
			ret = burn_drive_resolve_link(adrpt, adr,
						 recursion_count, flag);
		} else {
			strcpy(adr, adrpt);
		}
	} else {
		/* Link and device number recursion */
		ret = burn_drive_convert_fs_adr_sub(adrpt, adr,
							recursion_count);
		sprintf(msg,"burn_drive_convert_fs_adr( %s ) returns %d",
			link_target, ret);
	}
	if (flag & 1)
		burn_drive_adr_debug_msg(msg, NULL);
ex:;
	BURN_FREE_MEM(link_target);
	BURN_FREE_MEM(msg);
	BURN_FREE_MEM(link_adr);
	return ret;
}

/* ts A60922 - A61014 ticket 33 */
/* Try to find an enumerated address with the given stat.st_rdev number */
int burn_drive_find_devno(dev_t devno, char adr[])
{
	char *fname = NULL, *msg = NULL;
	int ret = 0, first = 1, fname_size = 4096;
	struct stat stbuf;
	burn_drive_enumerator_t enm;

	BURN_ALLOC_MEM(fname, char, fname_size);
	BURN_ALLOC_MEM(msg, char, fname_size + 100);

	while (1) {
		ret = sg_give_next_adr(&enm, fname, fname_size, first);
		if(ret <= 0)
	break;
		first = 0;
		ret = stat(fname, &stbuf);
		if(ret == -1)
	continue;
		if(devno != stbuf.st_rdev)
	continue;
		if(strlen(fname) >= BURN_DRIVE_ADR_LEN)
			{ret= -1; goto ex;}

		sprintf(msg, "burn_drive_find_devno( 0x%lX ) found %s",
			 (long) devno, fname);
		burn_drive_adr_debug_msg(msg, NULL);
		strcpy(adr, fname);
		{ ret = 1; goto ex;}
	}
	ret = 0;
ex:;
	if (first == 0)
		sg_give_next_adr(&enm, fname, fname_size, -1);
	BURN_FREE_MEM(fname);
	BURN_FREE_MEM(msg);
	return ret;
}

/* ts A60923 */
/** Try to obtain host,channel,target,lun from path.
    @return     1 = success , 0 = failure , -1 = severe error
*/
int burn_drive_obtain_scsi_adr(char *path,
			       int *bus_no, int *host_no, int *channel_no,
			       int *target_no, int *lun_no)
{
	int ret, i;
	char *adr = NULL;

	BURN_ALLOC_MEM(adr, char, BURN_DRIVE_ADR_LEN);

	/* open drives cannot be inquired by sg_obtain_scsi_adr() */
	for (i = 0; i < drivetop + 1; i++) {
		if (drive_array[i].global_index < 0)
	continue;
		ret = burn_drive_d_get_adr(&(drive_array[i]),adr);
		if (ret < 0)
			{ret = 1; goto ex;}
		if (ret == 0)
	continue;
		if (strcmp(adr, path) == 0) {
			*host_no = drive_array[i].host;
			*channel_no = drive_array[i].channel;
			*target_no = drive_array[i].id;
			*lun_no = drive_array[i].lun;
			*bus_no = drive_array[i].bus_no;
			if (*host_no < 0 || *channel_no < 0 ||
			    *target_no < 0 || *lun_no < 0)
				{ret = 0; goto ex;}
			{ret = 1; goto ex;}
		}
	}

	ret = sg_obtain_scsi_adr(path, bus_no, host_no, channel_no,
				 target_no, lun_no);
ex:;
	BURN_FREE_MEM(adr);
	return ret;
}

/* ts A60923 */
int burn_drive_convert_scsi_adr(int bus_no, int host_no, int channel_no,
				int target_no, int lun_no, char adr[])
{
	char *fname = NULL, *msg = NULL;
	int ret = 0, first = 1, i_bus_no = -1, fname_size = 4096;
	int i_host_no = -1, i_channel_no = -1, i_target_no = -1, i_lun_no = -1;
	burn_drive_enumerator_t enm;

	BURN_ALLOC_MEM(fname, char, fname_size);
	BURN_ALLOC_MEM(msg, char, fname_size + 100);

	sprintf(msg,"burn_drive_convert_scsi_adr( %d,%d,%d,%d,%d )",
		bus_no, host_no, channel_no, target_no, lun_no);
	burn_drive_adr_debug_msg(msg, NULL);

	while (1) {
		ret= sg_give_next_adr(&enm, fname, fname_size, first);
		if(ret <= 0)
	break;
		first = 0;
		ret = burn_drive_obtain_scsi_adr(fname, &i_bus_no, &i_host_no,
				 &i_channel_no, &i_target_no, &i_lun_no);
		if(ret <= 0)
	continue;
		if(bus_no >=0 && i_bus_no != bus_no)
	continue;
		if(host_no >=0 && i_host_no != host_no)
	continue;
		if(channel_no >= 0 && i_channel_no != channel_no)
	continue;
		if(target_no >= 0 && i_target_no != target_no)
	continue;
		if(lun_no >= 0 && i_lun_no != lun_no)
	continue;
		if(strlen(fname) >= BURN_DRIVE_ADR_LEN)
			{ ret = -1; goto ex;}
		burn_drive_adr_debug_msg(
			"burn_drive_convert_scsi_adr() found %s", fname);
		strcpy(adr, fname);
		{ ret = 1; goto ex;}
	}
	ret = 0;
ex:;
	if (first == 0)
		sg_give_next_adr(&enm, fname, fname_size, -1);
	BURN_FREE_MEM(fname);
	BURN_FREE_MEM(msg);
	return ret;
}

/* ts A60922 ticket 33 */
/* Try to find an enumerated address with the same host,channel,target,lun
   as path */
int burn_drive_find_scsi_equiv(char *path, char adr[])
{
	int ret = 0;
	int bus_no, host_no, channel_no, target_no, lun_no;
	char msg[4096];

	ret = burn_drive_obtain_scsi_adr(path, &bus_no, &host_no, &channel_no,
					 &target_no, &lun_no);
	if(ret <= 0) {
		sprintf(msg,"burn_drive_obtain_scsi_adr( %s ) returns %d",
			path, ret);
		burn_drive_adr_debug_msg(msg, NULL);
		return 0;
	}
	sprintf(msg, "burn_drive_find_scsi_equiv( %s ) : (%d),%d,%d,%d,%d",
		path, bus_no, host_no, channel_no, target_no, lun_no);
	burn_drive_adr_debug_msg(msg, NULL);

	ret= burn_drive_convert_scsi_adr(-1, host_no, channel_no, target_no,
					 lun_no, adr);
	return ret;
}


/* ts A60922 ticket 33 */
/** Try to convert a given existing filesystem address into a persistent drive
    address.  */
int burn_drive_convert_fs_adr_sub(char *path, char adr[], int *rec_count)
{
	int ret;
	struct stat stbuf;

	burn_drive_adr_debug_msg("burn_drive_convert_fs_adr( %s )", path);
	if (strncmp(path, "stdio:", 6) == 0 ||
	    burn_drive_is_enumerable_adr(path)) {
		if(strlen(path) >= BURN_DRIVE_ADR_LEN)
			return -1;
		if (strncmp(path, "stdio:", 6) != 0)
			burn_drive_adr_debug_msg(
			   "burn_drive_is_enumerable_adr( %s ) is true", path);
		strcpy(adr, path);
		return 1;
	}

	if(lstat(path, &stbuf) == -1) {
		burn_drive_adr_debug_msg("lstat( %s ) returns -1", path);
		return 0;
	}
	if((stbuf.st_mode & S_IFMT) == S_IFLNK) {
		ret = burn_drive_resolve_link(path, adr, rec_count, 0);
		if(ret > 0)
			return 1;
		burn_drive_adr_debug_msg("link fallback via stat( %s )", path);
		if(stat(path, &stbuf) == -1) {
			burn_drive_adr_debug_msg("stat( %s ) returns -1",path);
			return 0;
		}
	}
	if((stbuf.st_mode&S_IFMT) == S_IFBLK ||
	   (stbuf.st_mode&S_IFMT) == S_IFCHR) {
		ret = burn_drive_find_devno(stbuf.st_rdev, adr);
		if(ret > 0)
			return 1;
		ret = burn_drive_find_scsi_equiv(path, adr);
		if(ret > 0)
			return 1;
	}
	burn_drive_adr_debug_msg("Nothing found for %s", path);
	return 0;
}

/* API */
/** Try to convert a given existing filesystem address into a persistent drive
    address.  */
int burn_drive_convert_fs_adr(char *path, char adr[])
{
	int ret, rec_count = 0;

	ret = burn_drive_convert_fs_adr_sub(path, adr, &rec_count);
	return ret;
}


/* API */
int burn_lookup_device_link(char *dev_adr, char link_adr[],
			char *dir_adr, char **ranks, int rank_count, int flag)
{
	DIR *dirpt= NULL;
	struct dirent *entry;
	struct stat link_stbuf;
	char *adr= NULL, *namept, *sys_adr= NULL;
	int ret, name_rank, found_rank= 0x7fffffff, dirlen, i, rec_count = 0;
	static char default_ranks_data[][8] =
		{"dvdrw", "cdrw", "dvd", "cdrom", "cd"};
	char *default_ranks[5];

	link_adr[0] = 0;
	if (ranks == NULL) {
		for (i = 0; i < 5; i++)
			default_ranks[i] = default_ranks_data[i]; 
		ranks = default_ranks;
		rank_count= 5;
        }
	dirlen= strlen(dir_adr) + 1;
	if (strlen(dir_adr) + 1 >= BURN_DRIVE_ADR_LEN) {

		/* >>> Issue warning about oversized directory address */;

		{ret = 0; goto ex;}
	}
	BURN_ALLOC_MEM(adr, char, BURN_DRIVE_ADR_LEN);
        BURN_ALLOC_MEM(sys_adr, char, BURN_DRIVE_ADR_LEN);

	dirpt = opendir(dir_adr);
	if (dirpt == NULL)
		{ret = 0; goto ex;}
	strcpy(adr, dir_adr);
        strcat(adr, "/");
	namept = adr + strlen(dir_adr) + 1;
	while(1) {
		entry = readdir(dirpt);
		if(entry == NULL)
	break;
		if (strlen(entry->d_name) + dirlen >= BURN_DRIVE_ADR_LEN)
	continue;
	  	strcpy(namept, entry->d_name);
		if(lstat(adr, &link_stbuf) == -1)
	continue;
		if((link_stbuf.st_mode & S_IFMT) != S_IFLNK)
	continue;
		/* Determine rank and omit uninteresting ones */
		for(name_rank= 0; name_rank < rank_count; name_rank++)
			if(strncmp(namept, ranks[name_rank],
					 strlen(ranks[name_rank])) == 0)
		break;
		/* we look for lowest rank */
		if(name_rank >= rank_count ||
		   name_rank > found_rank ||
		   (name_rank == found_rank &&
		    strcmp(namept, link_adr + dirlen) >= 0))
	continue; 

		/* Does name point to the same device as dev_adr ? */
		ret= burn_drive_resolve_link(adr, sys_adr, &rec_count, 2);
		if(ret < 0)
			goto ex;
		if(ret == 0)
	continue;
		if(strcmp(dev_adr, sys_adr) == 0) {
			strcpy(link_adr, adr); 
			found_rank= name_rank;
		}
	}
	ret= 2;
	if(found_rank < 0x7fffffff)
		ret= 1;
ex:;
	if(dirpt != NULL)
		closedir(dirpt);
	BURN_FREE_MEM(adr);
	BURN_FREE_MEM(sys_adr);
	return(ret);
}


/** A pacifier function suitable for burn_abort.
    @param handle If not NULL, a pointer to a text suitable for printf("%s")
*/
int burn_abort_pacifier(void *handle, int patience, int elapsed)
{
 char *prefix= "libburn : ";

 if(handle!=NULL)
	prefix= handle;
 fprintf(stderr,
         "\r%sABORT : Waiting for drive to finish ( %d s, %d max)",
         (char *) prefix, elapsed, patience);
 return(1);
}


/* ts B00226 : Outsourced backend of burn_abort()
   @param flag  bit0= do not call burn_finish()
*/
int burn_abort_5(int patience, 
               int (*pacifier_func)(void *handle, int patience, int elapsed),
               void *handle, int elapsed, int flag)
{
	int ret, i, occup, still_not_done= 1, pacifier_off= 0, first_round= 1;
	unsigned long wait_grain= 100000;
	time_t start_time, current_time, pacifier_time, end_time;

#ifndef NIX
	time_t stdio_patience = 3;
#endif


/*
 fprintf(stderr, 
 "libburn_EXPERIMENTAL: burn_abort_5(%d,%d)\n", patience, flag);
*/

	current_time = start_time = pacifier_time = time(0);
	start_time -= elapsed;
	end_time = start_time + patience;

	/* >>> ts A71002 : are there any threads at work ?
		If not, then one can force abort because the drives will not
		change status on their own.
	 */

	while(current_time < end_time || (patience <= 0 && first_round)) {
		still_not_done = 0;

		for(i = 0; i < drivetop + 1; i++) {
			occup = burn_drive_is_occupied(&(drive_array[i]));
			if(occup == -2)
		continue;

			if(drive_array[i].drive_role != 1) {

#ifdef NIX

				/* ts A90302
				   <<< this causes a race condition with drive
				       usage and drive disposal.
			  	*/
				drive_array[i].busy = BURN_DRIVE_IDLE;
				burn_drive_forget(&(drive_array[i]), 1);
		continue;

#else /* NIX */

				/* ts A90318
				   >>> but if a pipe breaks then the drive
				       never gets idle.
				   So for now with a short patience timespan
				   and eventually a deliberate memory leak.
			  	*/
				if (current_time - start_time >
				    stdio_patience) {
					drive_array[i].global_index = -1;
		continue;
				}

#endif /* ! NIX */

			}

			if(occup < 10) {
				if (!drive_array[i].cancel)
					burn_drive_cancel(&(drive_array[i]));
				if (drive_array[i].drive_role != 1)
		 			/* occup == -1 comes early */
					usleep(1000000);
				burn_drive_forget(&(drive_array[i]), 1);
			} else if(occup <= 100) {
				if (!drive_array[i].cancel)
					burn_drive_cancel(&(drive_array[i]));
				still_not_done++;
			} else if(occup <= 1000) {
				still_not_done++;
			}
		}
		first_round = 0;

		if(still_not_done == 0 || patience <= 0)
	break;
		usleep(wait_grain);
		current_time = time(0);
		if(current_time>pacifier_time) {
			if(pacifier_func != NULL && !pacifier_off) {
				ret = (*pacifier_func)(handle, patience,
						current_time-start_time);
				pacifier_off = (ret <= 0);
			}
			pacifier_time = current_time;
		}
	}
	if (!(flag & 1))
		burn_finish();
	return(still_not_done == 0); 
}


/** Abort any running drive operation and finish libburn.
    @param patience Maximum number of seconds to wait for drives to finish
    @param pacifier_func Function to produce appeasing messages. See
                         burn_abort_pacifier() for an example.
    @return 1  ok, all went well
            0  had to leave a drive in unclean state
            <0 severe error, do no use libburn again
*/
int burn_abort(int patience, 
               int (*pacifier_func)(void *handle, int patience, int elapsed),
               void *handle)
{
	int ret, flg = 0;

	if (patience < 0) {
		patience = 0;
		flg |= 1;
	}
	ret = burn_abort_5(patience, pacifier_func, handle, 0, flg);
	return ret;
}


/* ts A61020 API function */
int burn_drive_get_start_end_lba(struct burn_drive *d, 
				int *start_lba, int *end_lba, int flag)
{
	if (d->start_lba == -2000000000 || d->end_lba == -2000000000)
		return 0;
	*start_lba = d->start_lba;
	*end_lba= d->end_lba;
	return 1;
}


/* ts A61020 API function */
int burn_disc_pretend_blank(struct burn_drive *d)
{
	if (d->drive_role == 0)
		return 0;
	if (d->status != BURN_DISC_UNREADY && 
	    d->status != BURN_DISC_UNSUITABLE)
		return 0;
	d->status = BURN_DISC_BLANK;
	return 1;
}

/* ts A61106 API function */
int burn_disc_pretend_full(struct burn_drive *d)
{
	if (d->drive_role == 0)
		return 0;
	if (d->status != BURN_DISC_UNREADY && 
	    d->status != BURN_DISC_UNSUITABLE)
		return 0;
	d->status = BURN_DISC_FULL;
	return 1;
}

/* ts A61021: new API function */
int burn_disc_read_atip(struct burn_drive *d)
{
	if (burn_drive_is_released(d)) {
		libdax_msgs_submit(libdax_messenger,
				d->global_index, 0x0002010e,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				"Attempt to read ATIP from ungrabbed drive",
				0, 0);
		return -1;
	}
	if(d->drive_role != 1)
		return 0;
	if ((d->current_profile == -1 || d->current_is_cd_profile)
	    && (d->mdata->cdrw_write || d->current_profile != 0x08)) {
		d->read_atip(d);
		/* >>> some control of success would be nice :) */
	} else {
		/* mmc5r03c.pdf 6.26.3.6.3 : ATIP is undefined for non-CD
		   (and it seems meaningless for non-burners).
		   ts A90823: Pseudo-CD U3 memory stick stalls with ATIP.
		              It is !cdrw_write and profile is 0x08.
		*/
		return 0;
	}
	return 1;
}

/* ts A61110 : new API function */
int burn_disc_track_lba_nwa(struct burn_drive *d, struct burn_write_opts *o,
			    int trackno, int *lba, int *nwa)
{
	int ret;

	if (burn_drive_is_released(d)) {
		libdax_msgs_submit(libdax_messenger,
			d->global_index, 0x0002011b,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Attempt to read track info from ungrabbed drive",
			0, 0);
		return -1;
	}
	if (d->busy != BURN_DRIVE_IDLE) {
		libdax_msgs_submit(libdax_messenger,
			d->global_index, 0x0002011c,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Attempt to read track info from busy drive",
			0, 0);
		return -1;
	}
	*lba = *nwa = 0;
	if (d->drive_role == 5 && trackno == 0 &&
	    d->status == BURN_DISC_APPENDABLE) {
		*lba = *nwa = d->role_5_nwa;
		return 1;
	}
	if (d->drive_role != 1)
		return 0;
	if (o != NULL)
		d->send_write_parameters(d, NULL, -1, o);
	ret = d->get_nwa(d, trackno, lba, nwa);
	return ret;
}


/* ts A70131 : new API function */
int burn_disc_get_msc1(struct burn_drive *d, int *start)
{
	int ret, trackno;

	if (burn_drive_is_released(d)) {
		libdax_msgs_submit(libdax_messenger,
			d->global_index, 0x0002011b,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Attempt to read track info from ungrabbed drive",
			0, 0);
		return -1;
	}
	if (d->busy != BURN_DRIVE_IDLE) {
		libdax_msgs_submit(libdax_messenger,
			d->global_index, 0x0002011c,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Attempt to read track info from busy drive",
			0, 0);
		return -1;
	}
	*start = 0;
	if (d->drive_role != 1)
		return 0;
	ret = d->read_multi_session_c1(d, &trackno, start);
	return ret;
}


/* ts A70213 : new API function */
off_t burn_disc_available_space(struct burn_drive *d,
				 struct burn_write_opts *o)
{
	int lba, nwa;

	if (burn_drive_is_released(d))
		return 0;
	if (d->busy != BURN_DRIVE_IDLE)
		return 0;
	if (d->drive_role == 0)
		return 0;
	if (d->drive_role != 1) {
		if (d->media_capacity_remaining <= 0)
			burn_drive_set_media_capacity_remaining(d,
			  (off_t) (512 * 1024 * 1024 - 1) * (off_t) 2048);
	} else {
		if (o != NULL)
			d->send_write_parameters(d, NULL, -1, o);
		d->get_nwa(d, -1, &lba, &nwa);
	}
	if (o != NULL) {
		if (o->start_byte > 0) {
			if (o->start_byte > d->media_capacity_remaining)
				return 0;
			return d->media_capacity_remaining - o->start_byte;
		}
	}
	return d->media_capacity_remaining;
}


/* ts A61202 : New API function */
int burn_disc_get_profile(struct burn_drive *d, int *pno, char name[80])
{
	*pno = d->current_profile;
	strcpy(name,d->current_profile_text);
	return *pno >= 0;
}


/* ts A90815 : New API function */
int burn_drive_get_all_profiles(struct burn_drive *d, int *num_profiles,
				int profiles[64], char is_current[64])
{
	int i;

	*num_profiles = d->num_profiles;
	for (i = 0; i < d->num_profiles; i++) {
		profiles[i] = (d->all_profiles[i * 4] << 8) |
				d->all_profiles[i * 4 + 1];
		is_current[i] = d->all_profiles[i * 4 + 2] & 1;
	}
	return 1;
}


/* ts A90815 : New API function */
int burn_obtain_profile_name(int profile_number, char name[80])
{
	strcpy(name, mmc_obtain_profile_name(profile_number));
	return(name[0] != 0);
}


/* ts A61223 : New API function */
int burn_drive_wrote_well(struct burn_drive *d)
{
	return !d->cancel;
}


/* ts A61226 */
int burn_speed_descriptor_new(struct burn_speed_descriptor **s,
			struct burn_speed_descriptor *prev,
			struct burn_speed_descriptor *next, int flag)
{
	struct burn_speed_descriptor *o;

	(*s) = o = calloc(1, sizeof(struct burn_speed_descriptor));
	if (o == NULL)
		return -1;
	o->source = 0;
	o->profile_loaded = -2;
	o->profile_name[0] = 0;
	o->wrc = 0;
	o->exact = 0;
	o->mrw = 0;
	o->end_lba = -1;
	o->write_speed = 0;
	o->read_speed = 0;

	o->prev = prev;
	if (prev != NULL) {
		next = prev->next;
		prev->next = o;
	} 
	o->next = next;
	if (next != NULL)
		next->prev = o;
	return 1;
}


/* ts A61226 */
/* @param flag bit0= destroy whole next-chain of descriptors */
int burn_speed_descriptor_destroy(struct burn_speed_descriptor **s, int flag)
{
	struct burn_speed_descriptor *next, *o;

	if ((*s) == NULL)
		return 0;
	if (flag&1)
		for (o = (*s); o->prev != NULL; o = o->prev);
	else
		o = (*s);
	next = o->next;
	if (next != NULL)
		next->prev = o->prev;
	if (o->prev != NULL)
		o->prev->next = next;
	free((char *) (*s));
	(*s) = NULL;
	if (flag&1)
		return burn_speed_descriptor_destroy(&next, flag&1);
	return 1;
}


/* ts A61226  */
int burn_speed_descriptor_copy(struct burn_speed_descriptor *from,
			struct burn_speed_descriptor *to, int flag)
{
	to->source = from->source;
	to->profile_loaded = from->profile_loaded;
	strcpy(to->profile_name, from->profile_name);
	to->wrc = from->wrc;
	to->exact = from->exact;
	to->mrw = from->mrw;
	to->end_lba = from->end_lba;
	to->write_speed = from->write_speed;
	to->read_speed = from->read_speed;
	return 1;
}


/* ts A61226 : free dynamically allocated sub data of struct scsi_mode_data */
int burn_mdata_free_subs(struct scsi_mode_data *m)
{
	if(!m->valid)
		return 0;
	burn_speed_descriptor_destroy(&(m->speed_descriptors), 1);
	return 1;
}


/* ts A61226 : API function */
int burn_drive_get_speedlist(struct burn_drive *d,
				 struct burn_speed_descriptor **speed_list)
{
	int ret;
	struct burn_speed_descriptor *sd, *csd = NULL;

	(*speed_list) = NULL;
	if(d->mdata->valid <= 0)
		return 0;
	for (sd = d->mdata->speed_descriptors; sd != NULL; sd = sd->next) {
		ret = burn_speed_descriptor_new(&csd, NULL, csd, 0);
		if (ret <= 0)
			return -1;
		burn_speed_descriptor_copy(sd, csd, 0);
	}
	(*speed_list) = csd;
	return (csd != NULL);
}


/* ts A70713 : API function */
int burn_drive_get_best_speed(struct burn_drive *d, int speed_goal,
			struct burn_speed_descriptor **best_descr, int flag)
{
	struct burn_speed_descriptor *sd;
	int best_speed = 0, best_lba = 0, source= 2, speed;

	if (flag & 2)
		source = -1;
	if (speed_goal < 0)
		best_speed = 2000000000;
	*best_descr = NULL;
	if(d->mdata->valid <= 0)
		return 0;
	for (sd = d->mdata->speed_descriptors; sd != NULL; sd = sd->next) {
		if (flag & 1)
			speed = sd->read_speed;
		else
			speed = sd->write_speed;
		if ((source >= 0 && sd->source != source) || 
		    speed <= 0)
	continue;
		if (speed_goal < 0) {
			if (speed < best_speed) {
				best_speed = speed;
				*best_descr = sd;
			}
		} else if (speed_goal == 0) {
			if ((source == 2 && sd->end_lba > best_lba) ||
			    ((source !=2 || sd->end_lba == best_lba) &&
			     speed > best_speed)) {
				best_lba = sd->end_lba;
				best_speed = speed;
				*best_descr = sd;
			}
		} else if (speed <= speed_goal) {
			if (speed > best_speed) {
				best_speed = speed;
				*best_descr = sd;
			}
		}
	}
	if (d->current_is_cd_profile && *best_descr == NULL && ! (flag & 2))
		/* Mode page 2Ah is deprecated in MMC-5 although all known
		   burners still support it with CD media. */
		return burn_drive_get_best_speed(d, speed_goal, best_descr,
						 flag | 2);
	return (*best_descr != NULL);
}


/* ts A61226 : API function */
int burn_drive_free_speedlist(struct burn_speed_descriptor **speed_list)
{
	return burn_speed_descriptor_destroy(speed_list, 1);
}


/* ts A70203 : API function */
int burn_disc_get_multi_caps(struct burn_drive *d, enum burn_write_types wt,
                         struct burn_multi_caps **caps, int flag)
{
	enum burn_disc_status s;
	struct burn_multi_caps *o;
	int status, num_formats, ret, type, i;
	off_t size;
	unsigned dummy;

	*caps = NULL;
	s = burn_disc_get_status(d);
	if(s == BURN_DISC_UNGRABBED)
		return -1;
	*caps = o = (struct burn_multi_caps *)
		calloc(1, sizeof(struct burn_multi_caps));
	if(*caps == NULL)
		return -1;
	/* Default says nothing is available */
	o->multi_session = o->multi_track = 0;
	o-> start_adr = 0;
	o->start_alignment = o->start_range_low = o->start_range_high = 0;
	o->might_do_tao = o->might_do_sao = o->might_do_raw = 0;
	o->advised_write_mode = BURN_WRITE_NONE;
	o->selected_write_mode = wt;
	o->current_profile = d->current_profile;
	o->current_is_cd_profile = d->current_is_cd_profile;
        o->might_simulate = 0;
	
	if (d->drive_role == 0 || d->drive_role == 4)
		return 0;
	if (d->drive_role == 2) {
		/* stdio file drive : random access read-write */
		o->start_adr = 1;
		size = d->media_capacity_remaining;
		burn_os_stdio_capacity(d->devname, &size);
		burn_drive_set_media_capacity_remaining(d, size);
		o->start_range_high = d->media_capacity_remaining;
		o->start_alignment = 2048; /* imposting a drive, not a file */
		o->might_do_sao = 4;
		o->might_do_tao = 2;
		o->advised_write_mode = BURN_WRITE_TAO;
        	o->might_simulate = 1;
	} else if (d->drive_role == 5) {
		/* stdio file drive : random access write-only */
		o->start_adr = 1;
		size = d->media_capacity_remaining;
		burn_os_stdio_capacity(d->devname, &size);
		burn_drive_set_media_capacity_remaining(d, size);

		/* >>> start_range_low = file size rounded to 2048 */;

		o->start_range_high = d->media_capacity_remaining;
		o->start_alignment = 2048; /* imposting a drive, not a file */
		if (s == BURN_DISC_APPENDABLE) {
			if (wt == BURN_WRITE_SAO || wt == BURN_WRITE_RAW)
				return 0;
			o->might_do_sao = 0;
		} else
			o->might_do_sao = 4;
		o->might_do_tao = 2;
		o->advised_write_mode = BURN_WRITE_TAO;
        	o->might_simulate = 1;
	} else if (d->drive_role != 1) {
		/* stdio file drive : sequential access write-only */
		o->might_do_sao = 4;
		o->might_do_tao = 2;
		o->advised_write_mode = BURN_WRITE_TAO;
        	o->might_simulate = 1;
	} else if (s != BURN_DISC_BLANK && s != BURN_DISC_APPENDABLE) {
		return 0;
	} else if (s == BURN_DISC_APPENDABLE &&
		 (wt == BURN_WRITE_SAO || wt == BURN_WRITE_RAW)) {
		return 0;
	} else if (wt == BURN_WRITE_RAW && !d->current_is_cd_profile) {
		return 0;
	} else if (d->current_profile == 0x09 || d->current_profile == 0x0a) {
		 /* CD-R , CD-RW */
		if (d->block_types[BURN_WRITE_TAO]) {
			o->multi_session = o->multi_track = 1;
			o->might_do_tao = 2;
			if (o->advised_write_mode == BURN_WRITE_NONE)
				o->advised_write_mode = BURN_WRITE_TAO;
		}
		if (d->block_types[BURN_WRITE_SAO]) {
			o->multi_session = o->multi_track = 1;
			o->might_do_sao = 1;
			if (o->advised_write_mode == BURN_WRITE_NONE)
				o->advised_write_mode = BURN_WRITE_SAO;
		}
		if (d->block_types[BURN_WRITE_RAW]) {
			o->might_do_raw = 1;
			if (o->advised_write_mode == BURN_WRITE_NONE)
				o->advised_write_mode = BURN_WRITE_RAW;
		}
		if (wt == BURN_WRITE_RAW)
			o->multi_session = o->multi_track = 0;
		else if(wt == BURN_WRITE_NONE || wt == BURN_WRITE_SAO ||
			wt == BURN_WRITE_TAO)
			o->might_simulate = !!d->mdata->simulate;
	} else if (d->current_profile == 0x11 || d->current_profile == 0x14 ||
			d->current_profile == 0x15) {
		/* DVD-R , sequential DVD-RW , DVD-R/DL Sequential */
		if (s == BURN_DISC_BLANK) {
			o->might_do_sao = 1;
			o->advised_write_mode = BURN_WRITE_SAO;
		}
		if (d->current_has_feat21h) {
#ifndef Libburn_dvd_r_dl_multi_no_close_sessioN
			if (d->current_profile != 0x15)
#endif
				o->multi_session = 1;
			o->multi_track = 1;
			o->might_do_tao = 2;
			o->advised_write_mode = BURN_WRITE_TAO;
		}
		if (wt == BURN_WRITE_SAO)
			o->multi_session = o->multi_track = 0;
		if (wt == BURN_WRITE_NONE || wt == BURN_WRITE_SAO ||
		    wt == BURN_WRITE_TAO)
			o->might_simulate = 1;
	} else if (d->current_profile == 0x12 ||
			d->current_profile == 0x13 ||
			d->current_profile == 0x1a ||
			d->current_profile == 0x43 
	          ) {
		/* DVD-RAM, overwriteable DVD-RW, DVD+RW, BD-RE */
		o->start_adr = 1;
		ret = burn_disc_get_formats(d, &status, &size, &dummy,
					&num_formats);
		if (ret == 1) {
			if (status == BURN_FORMAT_IS_FORMATTED)
				o->start_range_high = size;
			if (d->current_profile == 0x13) {
				o->start_alignment = 32 * 1024;
				for (i = 0; i < num_formats; i++) {
					ret = burn_disc_get_format_descr(d, i,
						&type, &size, &dummy);
					if (ret <= 0)
				continue;
					if (type == 0x13) /* expandable */
				break;
				}
				if (i >= num_formats) /* not expandable */
					o->start_range_high -= 32 * 1024;
				if (o->start_range_high < 0)
					o->start_range_high = 0;
			} else {
				o->start_alignment = 2 * 1024;
				if (d->best_format_size - 2048 >
							 o->start_range_high)
					o->start_range_high =
						d->best_format_size - 2048;
			}
		}
		o->might_do_sao = 4;
		o->might_do_tao = 2;
		o->advised_write_mode = BURN_WRITE_TAO;
	} else if (d->current_profile == 0x1b || d->current_profile == 0x2b ||
		   d->current_profile == 0x41) {
		/* DVD+R , DVD+R/DL , BD-R SRM */
		o->multi_session = o->multi_track = 1;
		o->might_do_tao = 2;
		o->might_do_sao = 1;
		o->advised_write_mode = BURN_WRITE_TAO;
	} else /* unknown media */
		return 0;
		
	if (s == BURN_DISC_APPENDABLE)
		o->might_do_sao = o->might_do_raw = 0;

	if (wt == BURN_WRITE_TAO && !o->might_do_tao)
		return 0;
	else if (wt == BURN_WRITE_SAO && !o->might_do_sao)
		return 0;
	else if (wt == BURN_WRITE_RAW && !o->might_do_raw)
		return 0;
	return 1;
}


/* ts A70203 : API function */
int burn_disc_free_multi_caps(struct burn_multi_caps **caps)
{
	if (*caps == NULL)
		return 0;
	free((char *) *caps);
	*caps = NULL;
	return 1;
}


/* ts A70207 : evaluate write mode related peculiarities of a disc
   @param flag bit0= fill_up_media is active
*/
int burn_disc_get_write_mode_demands(struct burn_disc *disc,
			struct burn_write_opts *opts,
			struct burn_disc_mode_demands *result, int flag)
{
	struct burn_session *session;
	struct burn_track *track;
	int i, j, mode, unknown_track_sizes = 0, last_track_is_unknown = 0;
	enum burn_disc_status s;
	

	memset((char *) result, 0, sizeof(struct burn_disc_mode_demands));
	if (disc == NULL)
		return 2;
	s = burn_disc_get_status(opts->drive);
	if (s == BURN_DISC_APPENDABLE || disc->sessions > 1)
		result->will_append = 1;
	if (disc->sessions > 1)
		result->multi_session = 1;
	for (i = 0; i < disc->sessions; i++) {
		session = disc->session[i];
		if (session->tracks <= 0)
	continue;
		mode = session->track[0]->mode;
		if (session->tracks > 1)
			result->multi_track = 1;
		for (j = 0; j < session->tracks; j++) {
			track = session->track[j];
			if (burn_track_is_open_ended(track)) {
				if (burn_track_get_default_size(track) > 0) {
					if (result->unknown_track_size == 0)
						result->unknown_track_size = 2;
				} else
					result->unknown_track_size = 1;
				unknown_track_sizes++;
				last_track_is_unknown = 1;
			} else
				last_track_is_unknown = 0;
			if ((mode & BURN_MODE_BITS) !=
						(track->mode & BURN_MODE_BITS))
				result->mixed_mode = 1;
			if (track->mode & BURN_MODE1) {
				result->block_types |= BURN_BLOCK_MODE1;
			} else if (track->mode & BURN_AUDIO) {
				result->audio = 1;
				result->block_types |= BURN_BLOCK_RAW0;
				result->exotic_track = 1;
			} else {
				result->block_types |= opts->block_type;
				result->exotic_track = 1;
			}
		}
	}
	if (flag&1) {/* fill_up_media will define the size of the last track */
		if (unknown_track_sizes == 1 && last_track_is_unknown)
			result->unknown_track_size = 0;
	}
	return (disc->sessions > 0);
}


/* ts A70903 : API */
int burn_drive_get_drive_role(struct burn_drive *d)
{
	return d->drive_role;
}


/* ts A70923
   Hands out pointers *dpt to directory path and *npt to basename.
   Caution: the last '/' in adr gets replaced by a 0.
*/
static int burn__split_path(char *adr, char **dpt, char **npt)
{
	*dpt = adr;
	*npt = strrchr(*dpt, '/');
	if (*npt == NULL) {
		*npt = *dpt;
		*dpt = ".";
		return 1;
	}
	**npt = 0;
	if(*npt == *dpt) 
		*dpt = "/";
	(*npt)++;
	return 2;
}


/* ts A70923 : API */
int burn_drive_equals_adr(struct burn_drive *d1, char *adr2_in, int role2)
{
	struct stat stbuf1, stbuf2;
	char *adr1 = NULL, *adr2 = adr2_in;
	char *conv_adr1 = NULL, *conv_adr2 = NULL;
	char *npt1, *dpt1, *npt2, *dpt2;
	int role1, stat_ret1, stat_ret2, conv_ret2, exact_role_matters = 0, fd;
	int ret;

	BURN_ALLOC_MEM(adr1, char, BURN_DRIVE_ADR_LEN);
	BURN_ALLOC_MEM(conv_adr1, char, BURN_DRIVE_ADR_LEN);
	BURN_ALLOC_MEM(conv_adr2, char, BURN_DRIVE_ADR_LEN);

	role1 = burn_drive_get_drive_role(d1);
	burn_drive_d_get_adr(d1, adr1);
	stat_ret1 = stat(adr1, &stbuf1);

	/* If one of the candidate paths depicts an open file descriptor then
	   its read-write capability decides about its role and the difference
	   between roles 2 and 3 does matter.
	*/
	fd = burn_drive__fd_from_special_adr(d1->devname);
	if (fd != -1)
		exact_role_matters = 1;
	if (strncmp(adr2, "stdio:", 6) == 0) {
		adr2+= 6;
		if (adr2[0] == 0) {
			role2 = 0;
		} else {
			fd = burn_drive__fd_from_special_adr(adr2);
			if (fd != -1)
				exact_role_matters = 1;
			ret = burn_drive__is_rdwr(adr2, NULL, NULL, NULL,
									1 | 2);
			if (ret == 2 && (burn_drive_role_4_allowed & 1))
				role2 = 4;
			else if (ret == 3 && (burn_drive_role_4_allowed & 1))
				role2 = 5;
			else if (ret > 0)
				role2 = 2;
			else
				role2 = 3;
			if (fd == -1 &&
			    role2 == 2 && (burn_drive_role_4_allowed & 3) == 3)
				role2 = burn_role_by_access(adr2,
					    !!(burn_drive_role_4_allowed & 4));
		}
	}

	if (strlen(adr2) >= BURN_DRIVE_ADR_LEN)
		{ret = -1; goto ex;}
	stat_ret2 = stat(adr2, &stbuf2);
	conv_ret2 = burn_drive_convert_fs_adr(adr2, conv_adr2);

	if (!exact_role_matters) {
		/* roles >= 2 have the same name space and object
		   interpretation */
		if (role1 >= 2)
			role1 = 2;
		if (role2 >= 2)
			role2 = 2;
	}

	if (strcmp(adr1, adr2) == 0 && role1 == role2)
		{ret = 1; goto ex;}		/* equal role and address */
	if (role1 == 1 && role2 == 1) {
					/* MMC drive meets wannabe MMC drive */
		if (conv_ret2 <= 0)
			{ret = 0; goto ex;}	/* no MMC drive at adr2 */
		if (strcmp(adr1, conv_adr2) == 0)
			{ret = 1; goto ex;}	/* equal real MMC drives */
		{ret = 0; goto ex;}

	} else if (role1 == 0 || role2 == 0)
		{ret = 0; goto ex;}		/* one null-drive, one not */

	else if (role1 != 1 && role2 != 1) {
					/* pseudo-drive meets file object */

		if (role1 != role2)
			{ret = 0; goto ex;}
		if (stat_ret1 == -1 || stat_ret2 == -1) {
			if (stat_ret1 != -1 || stat_ret2 != -1)
				 {ret = 0; goto ex;}
					/* one adress existing, one not */

			/* Two non-existing file objects */

			strcpy(conv_adr1, adr1);
			burn__split_path(conv_adr1, &dpt1, &npt1);
			strcpy(conv_adr2, adr2);
			burn__split_path(conv_adr2, &dpt2, &npt2);
			if (strcmp(npt1, npt2))
				{ret = 0; goto ex;}	/* basenames differ */
			stat_ret1= stat(adr1, &stbuf1);
			stat_ret2= stat(adr2, &stbuf2);
			if (stat_ret1 != stat_ret2)
				 {ret = 0; goto ex;}
						/* one dir existing, one not */

			/* Both directories exist. The basenames are equal.
			   So the adresses are equal if the directories are
			   equal.*/
		}
		if (stbuf1.st_ino == stbuf2.st_ino &&
	 		stbuf1.st_dev == stbuf2.st_dev)
			{ret = 1; goto ex;}	/* same filesystem object */

		if (S_ISBLK(stbuf1.st_mode) && S_ISBLK(stbuf2.st_mode) &&
			 stbuf1.st_rdev == stbuf2.st_rdev)
			{ret = 1; goto ex;}/* same major,minor device number */
		if (S_ISCHR(stbuf1.st_mode) && S_ISCHR(stbuf2.st_mode) &&
			 stbuf1.st_rdev == stbuf2.st_rdev)
			{ret = 1; goto ex;}/* same major,minor device number */

		/* Are both filesystem objects related to the same MMC drive */
		if (conv_ret2 <= 0)
			{ret = 0; goto ex;}	/* no MMC drive at adr2 */
		if (burn_drive_convert_fs_adr(adr1, conv_adr1) <= 0)
			{ret = 0; goto ex;}	/* no MMC drive at adr1 */
		if (strcmp(conv_adr1, conv_adr2) == 0)
			{ret = 1; goto ex;}	/* same MMC drive */

		{ret = 0; goto ex;}  /* all filesystem disguises are checked */

	} else if (role1 == 1 && role2 != 1) {
	                          /* MMC drive meets file object */

		if (conv_ret2 <= 0)
			{ret = 0; goto ex;}	/* no MMC drive at adr2 */
		if (strcmp(adr1, conv_adr2) == 0)
			{ret = 1; goto ex;}	/* same MMC drive */
		{ret = 0; goto ex;}

	} else if (role1 != 1 && role2 == 1) {
	                          /* stdio-drive meets wannabe MMC drive */

		if (conv_ret2 <= 0)
			{ret = 0; goto ex;}  /* no MMC drive at adr2 */
		if (burn_drive_convert_fs_adr(adr1, conv_adr1) <= 0)
			{ret = 0; goto ex;}  /* no MMC drive at adr1 */
		if (strcmp(conv_adr1, conv_adr2) == 0)
			{ret = 1; goto ex;}  /* same MMC drive */
		{ret = 0; goto ex;}

	}
	ret = 0;
ex:;
	BURN_FREE_MEM(adr1);
	BURN_FREE_MEM(conv_adr1);
	BURN_FREE_MEM(conv_adr2);
	return ret;
}


int burn_drive_find_by_thread_pid(struct burn_drive **d, pid_t pid,
								 pthread_t tid)
{
	int i;

	for (i = 0; i < drivetop + 1; i++) {

/*
		if (drive_array[i].thread_pid_valid)
			fprintf(stderr, "libburn_EXPERIMENTAL : drive %d , thread_pid %d\n", i, drive_array[i].thread_pid);
*/

		if (drive_array[i].thread_pid_valid &&
		    drive_array[i].thread_pid == pid &&
		    pthread_equal(drive_array[i].thread_tid, tid)) {
			*d = &(drive_array[i]);
			return 1;
		}
	}
	return 0;
}


/* ts A80422 : centralizing this setting for debugging purposes
*/
int burn_drive_set_media_capacity_remaining(struct burn_drive *d, off_t value)
{
	if (value / (off_t) 2048 > (off_t) 0x7ffffff0)
		value = ((off_t) 0x7ffffff0) * (off_t) 2048;
	d->media_capacity_remaining = value;
	return 1;
}


/* ts A81215 : API */
int burn_get_read_capacity(struct burn_drive *d, int *capacity, int flag)
{
	*capacity = d->media_read_capacity + 1;
	return (d->media_read_capacity != 0x7fffffff);
}


/* ts A90903 : API */
int burn_disc_get_media_id(struct burn_drive *d,
	char **product_id, char **media_code1, char **media_code2,
	char **book_type, int flag)
{
	int ret;

	*product_id = *media_code1 = *media_code2 = *book_type = NULL;
	if (burn_drive_get_drive_role(d) != 1)
		return 0;
	ret = mmc_get_media_product_id(d,
			 product_id, media_code1, media_code2, book_type,
			 flag & 1);
	return ret;
}


/* ts A90909 : API */
/**
    @param valid   Replies bits which indicate the validity of other reply
                   parameters or the state of certain CD info bits:
                   bit0= disc_type valid
                   bit1= disc_id valid
                   bit2= bar_code valid
                   bit3= disc_app_code valid
                   bit4= Disc is unrestricted (URU bit)
                   bit5= Disc is nominally erasable (Erasable bit)
                         This will be set with overwriteable media which
                         libburn normally considers to be unerasable blank.
*/
int burn_disc_get_cd_info(struct burn_drive *d, char disc_type[80],
			unsigned int *disc_id, char bar_code[9], int *app_code,
	 		int *valid)
{
	if (d->disc_type == 0x00) {
		strcpy(disc_type, "CD-DA or CD-ROM");
	} else if (d->disc_type == 0x10) {
		strcpy(disc_type, "CD-I");
	} else if (d->disc_type == 0x20) {
		strcpy(disc_type, "CD-ROM XA");
	} else {
		strcpy(disc_type, "undefined");
	}
	*disc_id = d->disc_id;
	memcpy(bar_code, d->disc_bar_code, 8);
	bar_code[8]= 0;
	*app_code = d->disc_app_code;
	*valid =  d->disc_info_valid;
	return 1;
}


/* ts B00924 : API */
int burn_disc_get_bd_spare_info(struct burn_drive *d,
				int *alloc_blocks, int *free_blocks, int flag)
{
	int ret;

	if (burn_drive_get_drive_role(d) != 1)
		return 0;
	*alloc_blocks = *free_blocks = 0;
	ret = mmc_get_bd_spare_info(d, alloc_blocks, free_blocks, 0);
	return ret;
}


/* ts B10801 : API */
int burn_disc_get_phys_format_info(struct burn_drive *d, int *disk_category,
                        char **book_name, int *part_version, int *num_layers,
                        int *num_blocks, int flag)
{
	int ret;

	if (burn_drive_get_drive_role(d) != 1)
		return 0;
	*disk_category = *part_version = *num_layers = *num_blocks = 0;
	ret = mmc_get_phys_format_info(d, disk_category, book_name,
				part_version, num_layers, num_blocks, 0);
	return ret;
}



/* ts B10525 : API */
int burn_disc_next_track_is_damaged(struct burn_drive *d, int flag)
{
	return d->next_track_damaged;
}


/* ts B11201 : API */
/* Read the CD-TEXT data from the Lead-in of an Audio CD
*/
int burn_disc_get_leadin_text(struct burn_drive *d,
                              unsigned char **text_packs, int *num_packs,
                              int flag)
{
	int ret;

	ret = mmc_get_leadin_text(d, text_packs, num_packs, 0);
	return ret;
}

