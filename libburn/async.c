/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2012 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/

/* ts A71019 */

/* Standard measure should be: Threads are created detached.
   According to the man pages they should then care for disposing themselves.

   >>> ??? It is yet unclear why the threads vanish from the process list
           even if joinable and even if never joined.

   To be activated after release of libburn-0.4.0
*/
#define Libburn_create_detached_threadS 1

/* Alternative : Threads are created joinable.
   Threads get detached in remove_worker() and thus should dispose themselves.

#define Libburn_detach_done_workeR 1
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "libburn.h"
#include "transport.h"
#include "drive.h"
#include "write.h"
#include "options.h"
#include "file.h"
#include "async.h"
#include "init.h"
#include "back_hacks.h"

#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

/*
#include <a ssert.h>
*/
#include "libdax_msgs.h"
extern struct libdax_msgs *libdax_messenger;

/* ts A80714 : introduced type codes for the worker list */
#define Burnworker_type_scaN    0
#define Burnworker_type_erasE   1
#define Burnworker_type_formaT  2
#define Burnworker_type_writE   3
#define Burnworker_type_fifO    4

#define SCAN_GOING() (workers != NULL && \
			workers->w_type == Burnworker_type_scaN)

typedef void *(*WorkerFunc) (void *);

struct scan_opts
{
	struct burn_drive_info **drives;
	unsigned int *n_drives;

	int done;
};

struct erase_opts
{
	struct burn_drive *drive;
	int fast;
};

/* ts A61230 */
struct format_opts
{
	struct burn_drive *drive;
	off_t size;
	int flag;
};

struct write_opts
{
	struct burn_drive *drive;
	struct burn_write_opts *opts;
	struct burn_disc *disc;
};

struct fifo_opts
{
	struct burn_source *source;
	int flag;
};


struct w_list
{
	/* ts A80714 */
	int w_type; /* see above define Burnworker_type_* */

	struct burn_drive *drive;
	pthread_t thread;

	struct w_list *next;

	union w_list_data
	{
		struct scan_opts scan;
		struct erase_opts erase;
		struct format_opts format;
		struct write_opts write;
		struct fifo_opts fifo;
	} u;
};

static struct w_list *workers = NULL;


static struct w_list *find_worker(struct burn_drive *d)
{
	struct w_list *a;

	for (a = workers; a; a = a->next)
		if (a->drive == d)
			return a;
	return NULL;
}

static void add_worker(int w_type, struct burn_drive *d,
			WorkerFunc f, void *data)
{
	struct w_list *a;
	struct w_list *tmp;
	pthread_attr_t *attr_pt = NULL;

#ifdef Libburn_create_detached_threadS
	pthread_attr_t attr;
#endif

	a = calloc(1, sizeof(struct w_list));
	a->w_type = w_type;
	a->drive = d;
	a->u = *(union w_list_data *)data;

	/* insert at front of the list */
	a->next = workers;
	tmp = workers;
	workers = a;

	if (d != NULL)
		d->busy = BURN_DRIVE_SPAWNING;

#ifdef Libburn_create_detached_threadS
	/* ts A71019 :
	   Trying to start the threads detached to get rid of the zombies
	   which do neither react on pthread_join() nor on pthread_detach().
	*/
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	attr_pt= &attr;
/*
	libdax_msgs_submit(libdax_messenger, -1, 0x00020158,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_LOW,
			"add_worker(): Creating detached thread.", 0, 0);
*/
#endif

	if (pthread_create(&a->thread, attr_pt, f, a)) {
		free(a);
		workers = tmp;
		return;
	}
}


static void remove_worker(pthread_t th)
{
	struct w_list *a, *l = NULL;

	for (a = workers; a; l = a, a = a->next)
		if (a->thread == th) {
			if (l)
				l->next = a->next;
			else
				workers = a->next;

#ifdef Libburn_detach_done_workeR
			/* ts A71019 : burry dead puppy before forgetting it */
			/* Alternative : threads get detached and thus should
					dispose themselves.
			*/
			pthread_detach(th);
/*
			int ret;
			char msg[80];

			ret = pthread_detach(th);
			sprintf(msg,
			 "remove_workers(): pid= %lu  pthread_detach(%lu)= %d",
			 (unsigned long) getpid(), (unsigned long) th, ret);
			libdax_msgs_submit(libdax_messenger, -1, 0x00020158,
				LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_LOW,
				msg, 0, 0);
*/
			
#endif /* Libburn_detach_done_workeR */

			free(a);
			break;
		}

	/* ts A61006 */
	/* a ssert(a != NULL);/ * wasn't found.. this should not be possible */
	if (a == NULL)
		libdax_msgs_submit(libdax_messenger, -1, 0x00020101,
			LIBDAX_MSGS_SEV_WARNING, LIBDAX_MSGS_PRIO_HIGH,
			"remove_worker() cannot find given worker item", 0, 0);
}

static void *scan_worker_func(struct w_list *w)
{
	int ret;

	ret = burn_drive_scan_sync(w->u.scan.drives, w->u.scan.n_drives, 1);
	if (ret <= 0)
		w->u.scan.done = -1;
	else
		w->u.scan.done = 1;
	return NULL;
}

static void reset_progress(struct burn_drive *d, int sessions, int tracks,
				int indices, int sectors, int flag)
{
	/* reset the progress indicator */
	d->progress.session = 0;
	d->progress.sessions = sessions;
	d->progress.track = 0;
	d->progress.tracks = tracks;
	d->progress.index = 0;
	d->progress.indices = indices;
	d->progress.start_sector = 0;
	d->progress.sectors = sectors;
	d->progress.sector = 0;
}


int burn_drive_scan(struct burn_drive_info *drives[], unsigned int *n_drives)
{
	struct scan_opts o;
	int ret = 0;

	/* ts A61006 : moved up from burn_drive_scan_sync , former Assert */
	if (!burn_running) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00020109,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Library not running (on attempt to scan)", 0, 0);
		*drives = NULL;
		*n_drives = 0;
		return -1;
	}

	/* cant be anything working! */

	/* ts A61006 */
	/* a ssert(!(workers && workers->drive)); */
	if (workers != NULL && workers->drive != NULL) {
drive_is_active:;
		libdax_msgs_submit(libdax_messenger, -1, 0x00020102,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"A drive operation is still going on (want to scan)",
			0, 0);
		*drives = NULL;
		*n_drives = 0;
		return -1;
	}

	if (workers == NULL) {
		/* start it */

		/* ts A61007 : test moved up from burn_drive_scan_sync()
				was burn_wait_all() */
		/* ts A70907 : now demanding freed drives, not only released */
		if (!burn_drives_are_clear(1))
			goto drive_is_active;
		*drives = NULL;
		*n_drives = 0;

		o.drives = drives;
		o.n_drives = n_drives;
		o.done = 0;
		add_worker(Burnworker_type_scaN, NULL,
				 (WorkerFunc) scan_worker_func, &o);
	} else if (workers->u.scan.done) {
		/* its done */
		ret = workers->u.scan.done;
		remove_worker(workers->thread);

		/* ts A61006 */
		/* a ssert(workers == NULL); */
		if (workers != NULL) {
			libdax_msgs_submit(libdax_messenger, -1, 0x00020101,
				LIBDAX_MSGS_SEV_WARNING, LIBDAX_MSGS_PRIO_HIGH,
			     "After scan a drive operation is still going on",
				0, 0);
			return -1;
		}

	} else {
		/* still going */
	}
	return ret;
}

static void *erase_worker_func(struct w_list *w)
{

#define Libburn_protect_erase_threaD 1

#ifdef Libburn_protect_erase_threaD
	sigset_t sigset, oldset;

	/* Protect blank thread from being interrupted by external signals */
	sigfillset(&sigset);
	sigdelset(&sigset, SIGSEGV);
	sigdelset(&sigset, SIGILL);
	pthread_sigmask(SIG_SETMASK, &sigset, &oldset);
#endif /* Libburn_protect_erase_threaD */

	burn_disc_erase_sync(w->u.erase.drive, w->u.erase.fast);
	remove_worker(pthread_self());

#ifdef Libburn_protect_erase_threaD
	/* (just in case it would not end with all signals blocked) */
	pthread_sigmask(SIG_SETMASK, &oldset, NULL);
#endif /* Libburn_protect_erase_threaD */

	return NULL;
}

void burn_disc_erase(struct burn_drive *drive, int fast)
{
	struct erase_opts o;

	/* ts A61006 */
	/* a ssert(drive); */
	/* a ssert(!SCAN_GOING()); */
	/* a ssert(!find_worker(drive)); */

	if((drive == NULL)) {
		libdax_msgs_submit(libdax_messenger, -1,
			0x00020104,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"NULL pointer caught in burn_disc_erase", 0, 0);
		return;
	}
	if ((SCAN_GOING()) || find_worker(drive) != NULL) {
		libdax_msgs_submit(libdax_messenger, drive->global_index,
			0x00020102,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"A drive operation is still going on (want to erase)",
			0, 0);
		return;
	}

	reset_progress(drive, 1, 1, 1, 0x10000, 0);

	/* A70103 : will be set to 0 by burn_disc_erase_sync() */
	drive->cancel = 1;

	/* ts A70103 moved up from burn_disc_erase_sync() */
	/* ts A60825 : allow on parole to blank appendable CDs */
	/* ts A70131 : allow blanking of overwriteable DVD-RW (profile 0x13) */
	/* ts A70216 : allow blanking of CD-RW or DVD-RW in any regular state
	               and of any kind of full media */
	/* ts A70909 : the willingness to burn any BURN_DISC_FULL media is
	               inappropriate. One would rather need a -force option
	               Note: keep this in sync with mmc_read_disc_info() */
	/* ts B10321 : Allowed role 5 to be blanked */
	if ((drive->drive_role == 1 &&
	     drive->current_profile != 0x0a &&
	     drive->current_profile != 0x13 &&
	     drive->current_profile != 0x14 &&
	     drive->status != BURN_DISC_FULL)
	    ||
	    (drive->status != BURN_DISC_FULL &&
	     drive->status != BURN_DISC_APPENDABLE &&
	     drive->status != BURN_DISC_BLANK)
	    ||
	    (drive->drive_role != 1 && drive->drive_role != 5)
	   ) {
		char msg[160];

		sprintf(msg, "Drive and media state unsuitable for blanking. (role= %d , profile= 0x%x , status= %d)",
			drive->drive_role,
			(unsigned int) drive->current_profile,
			drive->status);
		libdax_msgs_submit(libdax_messenger, drive->global_index,
			0x00020130,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
		return;
	}

	o.drive = drive;
	o.fast = fast;
	add_worker(Burnworker_type_erasE, drive,
			(WorkerFunc) erase_worker_func, &o);
}


/* ts A61230 */
static void *format_worker_func(struct w_list *w)
{

#define Libburn_protect_format_threaD 1

#ifdef Libburn_protect_format_threaD
	sigset_t sigset, oldset;

	/* Protect format thread from being interrupted by external signals */
	sigfillset(&sigset);
	sigdelset(&sigset, SIGSEGV);
	sigdelset(&sigset, SIGILL);
	pthread_sigmask(SIG_SETMASK, &sigset, &oldset);
#endif /* Libburn_protect_format_threaD */

	burn_disc_format_sync(w->u.format.drive, w->u.format.size,
				w->u.format.flag);
	remove_worker(pthread_self());

#ifdef Libburn_protect_format_threaD
	/* (just in case it would not end with all signals blocked) */
	pthread_sigmask(SIG_SETMASK, &oldset, NULL);
#endif /* Libburn_protect_format_threaD */

	return NULL;
}


/* ts A61230 */
void burn_disc_format(struct burn_drive *drive, off_t size, int flag)
{
	struct format_opts o;
	int ok = 0, ret;
	char msg[40];

	reset_progress(drive, 1, 1, 1, 0x10000, 0);

	if ((SCAN_GOING()) || find_worker(drive) != NULL) {
		libdax_msgs_submit(libdax_messenger, drive->global_index,
			0x00020102,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"A drive operation is still going on (want to format)",
			0, 0);
		return;
	}
	if (drive->drive_role != 1) {
		libdax_msgs_submit(libdax_messenger, drive->global_index,
			0x00020146,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Drive is a virtual placeholder", 0, 0);
		drive->cancel = 1;
		return;
	}
	if (flag & 128)     /* application prescribed format type */
		flag |= 16; /* enforce re-format */

	if (drive->current_profile == 0x14)
		ok = 1; /* DVD-RW sequential */
	else if (drive->current_profile == 0x13 && (flag & 16)) 
		ok = 1; /* DVD-RW Restricted Overwrite with force bit */
	else if (drive->current_profile == 0x1a) {
		ok = 1;         /* DVD+RW */
		size = 0;
		flag &= ~(2|8); /* no insisting in size 0, no expansion */
		flag |= 4;      /* format up to maximum size */
	} else if (drive->current_profile == 0x12) {
		ok = 1; /* DVD-RAM */

	} else if (drive->current_profile == 0x41) {
		/* BD-R SRM */
		ok= 1;
		ret = drive->read_format_capacities(drive, 0x00);
		if (ret > 0 &&
		    drive->format_descr_type == BURN_FORMAT_IS_FORMATTED)
			ok = 0;
		if (drive->status != BURN_DISC_BLANK)
			ok = 0;
		if (!ok) {
			libdax_msgs_submit(libdax_messenger,
				drive->global_index, 0x00020162,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			 "BD-R not unformatted blank any more. Cannot format.",
				0, 0);
			drive->cancel = 1;
			return;
		}
		if (flag & 32) {
			libdax_msgs_submit(libdax_messenger,
				drive->global_index, 0x00020163,
				LIBDAX_MSGS_SEV_NOTE, LIBDAX_MSGS_PRIO_HIGH,
			"Blank BD-R left unformatted for zero spare capacity.",
				0, 0);
			return;
		}
	} else if (drive->current_profile == 0x43) {
		ok = 1; /* BD-RE */

		if ((flag & 32) && !(drive->current_feat23h_byte4 & 8)) {
			libdax_msgs_submit(libdax_messenger,
				drive->global_index, 0x00020164,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				"Drive does not format BD-RE without spares.",
				0, 0);
			drive->cancel = 1;
			return;

		} 
	}

	if (!ok) {
		sprintf(msg,"Will not format media type %4.4Xh",
			drive->current_profile);
		libdax_msgs_submit(libdax_messenger, drive->global_index,
			0x00020129,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
		drive->cancel = 1;
		return;
	}
	o.drive = drive;
	o.size = size;
	o.flag = flag;
	add_worker(Burnworker_type_formaT, drive,
			(WorkerFunc) format_worker_func, &o);
}


static void *write_disc_worker_func(struct w_list *w)
{
	struct burn_drive *d = w->u.write.drive;
	char msg[80];

#define Libburn_protect_write_threaD 1

#ifdef Libburn_protect_write_threaD
	sigset_t sigset, oldset;

	/* Protect write thread from being interrupted by external signals */
	sigfillset(&sigset);
	sigdelset(&sigset, SIGSEGV);
	sigdelset(&sigset, SIGILL);
	pthread_sigmask(SIG_SETMASK, &sigset, &oldset);
#endif /* Libburn_protect_write_threaD */

	d->thread_pid = getpid();
	d->thread_tid = pthread_self();
	d->thread_pid_valid= 1;
	burn_disc_write_sync(w->u.write.opts, w->u.write.disc);
	d->thread_pid_valid= 0;
	d->thread_pid = 0;

	/* the options are refcounted, free out ref count which we added below 
	 */
	burn_write_opts_free(w->u.write.opts);

	sprintf(msg, "Write thread on drive %d ended", d->global_index);
	libdax_msgs_submit(libdax_messenger, d->global_index, 0x00020178,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);

	remove_worker(pthread_self());
	d->busy = BURN_DRIVE_IDLE;

#ifdef Libburn_protect_write_threaD
	/* (just in case it would not end with all signals blocked) */
	pthread_sigmask(SIG_SETMASK, &oldset, NULL);
#endif /* Libburn_protect_write_threaD */

	return NULL;
}

void burn_disc_write(struct burn_write_opts *opts, struct burn_disc *disc)
{
	struct write_opts o;
	char *reasons= NULL;
	struct burn_drive *d;
	int mvalid;

	d = opts->drive;

	/* ts A61006 */
	/* a ssert(!SCAN_GOING()); */
	/* a ssert(!find_worker(opts->drive)); */
	if ((SCAN_GOING()) || find_worker(opts->drive) != NULL) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020102,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"A drive operation is still going on (want to write)",
			0, 0);
		return;
	}

	reset_progress(d, disc->sessions, disc->session[0]->tracks,
			 disc->session[0]->track[0]->indices, 0, 0);

	/* For the next lines any return indicates failure */
	d->cancel = 1;

	/* ts A70203 : people have been warned in API specs */
	if (opts->write_type == BURN_WRITE_NONE) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x0002017c,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			"No valid write type selected", 0, 0);
		return;
	}

	if (d->drive_role == 0) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020146,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Drive is a virtual placeholder (null-drive)", 0, 0);
		return;
	}
	if (d->drive_role == 4) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020181,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			"Pseudo-drive is a read-only file. Cannot write.",
			0, 0);
		return;
	}

	/* ts A61007 : obsolete Assert in spc_select_write_params() */
	if (d->drive_role == 1) {
		mvalid = 0;
		if (d->mdata != NULL)
			if (d->mdata->valid > 0)
				mvalid = 1;
		if (!mvalid) {
			libdax_msgs_submit(libdax_messenger,
				d->global_index, 0x00020113,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				"Drive capabilities not inquired yet", 0, 0);
			return;
		}
	}

	/* ts A70219 : intended to replace all further tests here and many
	               tests in burn_*_write_sync()
	*/

        BURN_ALLOC_MEM_VOID(reasons, char, BURN_REASONS_LEN + 80);
	strcpy(reasons, "Write job parameters are unsuitable:\n");
	if (burn_precheck_write(opts, disc, reasons + strlen(reasons), 1)
	     <= 0) {
		libdax_msgs_submit(libdax_messenger,
				d->global_index, 0x00020139,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				reasons, 0, 0);
		goto ex;
	}
        BURN_FREE_MEM(reasons); reasons= NULL;

	/* ts A90106 : early catching of unformatted BD-RE */
	if (d->current_profile == 0x43) 
		if (d->read_format_capacities(d, 0x00) > 0 &&
		    d->format_descr_type != BURN_FORMAT_IS_FORMATTED) {
			libdax_msgs_submit(libdax_messenger,
				d->global_index, 0x00020168,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
				"Media not properly formatted. Cannot write.",
				0, 0);
			return;
		}

	d->cancel = 0; /* End of the return = failure area */

	o.drive = d;
	o.opts = opts;
	o.disc = disc;

	opts->refcount++;

	add_worker(Burnworker_type_writE, d,
			(WorkerFunc) write_disc_worker_func, &o);

ex:;
	BURN_FREE_MEM(reasons);
}


static void *fifo_worker_func(struct w_list *w)
{
	int old;

#define Libburn_protect_fifo_threaD 1

#ifdef Libburn_protect_fifo_threaD
	sigset_t sigset, oldset;

	/* Protect fifo thread from being interrupted by external signals */
	sigfillset(&sigset);
	sigdelset(&sigset, SIGSEGV);
	sigdelset(&sigset, SIGILL);
	pthread_sigmask(SIG_SETMASK, &sigset, &oldset);
#endif /* Libburn_protect_fifo_threaD */

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old);
		/* Note: Only burn_fifo_abort() shall cancel the fifo thread */

	burn_fifo_source_shoveller(w->u.fifo.source, w->u.fifo.flag);
	remove_worker(pthread_self());

#ifdef Libburn_protect_fifo_threaD
	/* (just in case it would not end with all signals blocked) */
	pthread_sigmask(SIG_SETMASK, &oldset, NULL);
#endif /* Libburn_protect_fifo_threaD */

	return NULL;
}


int burn_fifo_start(struct burn_source *source, int flag)
{
	struct fifo_opts o;
	struct burn_source_fifo *fs = source->data;

	fs->is_started = -1;

	/* create and set up ring buffer */;
	fs->buf = burn_os_alloc_buffer(
			((size_t) fs->chunksize) * (size_t) fs->chunks, 0);
	if (fs->buf == NULL) {
		/* >>> could not start ring buffer */;
		return -1;
	}

	o.source = source;
	o.flag = flag;
	add_worker(Burnworker_type_fifO, NULL,
			(WorkerFunc) fifo_worker_func, &o);
	fs->is_started = 1;

	return 1;
}


int burn_fifo_abort(struct burn_source_fifo *fs, int flag)
{
	int ret;
	pthread_t pt;

	if (fs->thread_is_valid <= 0 || fs->thread_handle == NULL)
		return(2);

#ifdef NIX
	libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			"Aborting running burn_source_fifo thread", 0, 0);
#endif /* NIX */

	pt= *((pthread_t *) fs->thread_handle);
	remove_worker(pt);
	ret = pthread_cancel(pt);
	return (ret == 0);
}


#ifdef Libburn_has_burn_async_join_alL

/* ts A71019 : never used */
void burn_async_join_all(void)
{
	void *ret;

	while (workers)
		pthread_join(workers->thread, &ret);
}

#endif /* Libburn_has_burn_async_join_alL */


