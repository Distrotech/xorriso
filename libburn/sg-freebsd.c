/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* 
   Copyright (c) 2006 - 2011 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later
        and under FreeBSD license revised, i.e. without advertising clause.
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <camlib.h>
#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_pass.h>

#include <err.h> /* XXX */

/* ts A70909 */
#include <sys/statvfs.h>

/* ts B00121 */
#include <sys/disk.h> /* DIOCGMEDIASIZE */


/* ts B00326 : For use of CAM_PASS_ERR_RECOVER with ahci */
#define Libburn_for_freebsd_ahcI yes

/* ts B00327 : for debugging of cam_send_cdb() failures
 # define Libburn_ahci_verbouS yes
*/

/* ts B00327 : Apply CAM_PASS_ERR_RECOVER to drives even if not ahci
 # define libburn_ahci_style_for_alL yes
*/


#include "transport.h"
#include "drive.h"
#include "sg.h"
#include "spc.h"
#include "mmc.h"
#include "sbc.h"
#include "debug.h"
#include "toc.h"
#include "util.h"
#include "init.h"

#include "libdax_msgs.h"
extern struct libdax_msgs *libdax_messenger;

struct burn_drive_enumeration_state {
	int fd;
	union ccb ccb;
	unsigned int i;
	int skip_device;
};

static void enumerate_common(char *fname, int bus_no, int host_no,
			     int channel_no, int target_no, int lun_no);

/* ts A51221 */
int burn_drive_is_banned(char *device_address);


/* ts A60821
   debug: for tracing calls which might use open drive fds
          or for catching SCSI usage of emulated drives. */
int mmc_function_spy(struct burn_drive *d, char * text);


/* ts B00113
   Whether to log SCSI commands:
   bit0= log in /tmp/libburn_sg_command_log
   bit1= log to stderr
   bit2= flush every line
*/
extern int burn_sg_log_scsi;

/* ts B00114 */
/* Storage object is in libburn/init.c
   whether to strive for exclusive access to the drive
*/
extern int burn_sg_open_o_excl;


/* ts A91227 */
/** Returns the id string  of the SCSI transport adapter and eventually
    needed operating system facilities.
    This call is usable even if sg_initialize() was not called yet. In that
    case a preliminary constant message might be issued if detailed info is
    not available yet.
    @param msg   returns id string
    @param flag  unused yet, submit 0
    @return      1 = success, <=0 = failure
*/
int sg_id_string(char msg[1024], int flag)
{
	strcpy(msg, "internal FreeBSD CAM adapter sg-freebsd");
	return 1;
}


/* ts A91227 */
/** Performs global initialization of the SCSI transport adapter and eventually
    needed operating system facilities. Checks for compatibility supporting
    software components.
    @param msg   returns ids and/or error messages of eventual helpers
    @param flag  unused yet, submit 0
    @return      1 = success, <=0 = failure
*/
int sg_initialize(char msg[1024], int flag)
{
	return sg_id_string(msg, 0);
}


/* ts A91227 */
/** Performs global finalization of the SCSI transport adapter and eventually
    needed operating system facilities. Releases globally aquired resources.
    @param flag  unused yet, submit 0
    @return      1 = success, <=0 = failure
*/  
int sg_shutdown(int flag)
{
	return 1;
}


/** Finalizes BURN_OS_TRANSPORT_DRIVE_ELEMENTS, the components of
    struct burn_drive which are defined in os-*.h.
    The eventual initialization of those components was made underneath
    scsi_enumerate_drives().
    This will be called when a burn_drive gets disposed.
    @param d     the drive to be finalized
    @param flag  unused yet, submit 0
    @return      1 = success, <=0 = failure
*/
int sg_dispose_drive(struct burn_drive *d, int flag)
{
        return 1;
}


/* ts A61021 : Moved most code from scsi_enumerate_drives under
               sg_give_next_adr() */
/* Some helper functions for scsi_give_next_adr() */

static int sg_init_enumerator(burn_drive_enumerator_t *idx_)
{
	struct burn_drive_enumeration_state *idx;
	int bufsize;

	idx = calloc(1, sizeof(*idx));
	if (idx == NULL) {
		warnx("cannot allocate memory for enumerator");
		return -1;
	}
	idx->skip_device = 0;

	if ((idx->fd = open(XPT_DEVICE, O_RDWR)) == -1) {
		warn("could not open %s", XPT_DEVICE);
		free(idx);
		idx = NULL;
		return -1;
	}

	memset(&(idx->ccb), 0, sizeof(union ccb));

	idx->ccb.ccb_h.path_id = CAM_XPT_PATH_ID;
	idx->ccb.ccb_h.target_id = CAM_TARGET_WILDCARD;
	idx->ccb.ccb_h.target_lun = CAM_LUN_WILDCARD;

	idx->ccb.ccb_h.func_code = XPT_DEV_MATCH;
	bufsize = sizeof(struct dev_match_result) * 100;
	idx->ccb.cdm.match_buf_len = bufsize;
	idx->ccb.cdm.matches = (struct dev_match_result *) calloc(1, bufsize);
	if (idx->ccb.cdm.matches == NULL) {
		warnx("cannot allocate memory for matches");
		close(idx->fd);
		free(idx);
		return -1;
	}
	idx->ccb.cdm.num_matches = 0;
	idx->i = idx->ccb.cdm.num_matches; /* to trigger buffer load */

	/*
	 * We fetch all nodes, since we display most of them in the default
	 * case, and all in the verbose case.
	 */
	idx->ccb.cdm.num_patterns = 0;
	idx->ccb.cdm.pattern_buf_len = 0;

	*idx_ = idx;

	return 1;
}

static void sg_destroy_enumerator(burn_drive_enumerator_t *idx_)
{
	struct burn_drive_enumeration_state *idx = *idx_;

	if(idx->fd != -1)
		close(idx->fd);

	free(idx->ccb.cdm.matches);
	free(idx);

	*idx_ = NULL;
}

static int sg_next_enumeration_buffer(burn_drive_enumerator_t *idx_)
{
	struct burn_drive_enumeration_state *idx = *idx_;

	/*
	 * We do the ioctl multiple times if necessary, in case there are
	 * more than 100 nodes in the EDT.
	 */
	if (ioctl(idx->fd, CAMIOCOMMAND, &(idx->ccb)) == -1) {
		warn("error sending CAMIOCOMMAND ioctl");
		return -1;
	}

	if ((idx->ccb.ccb_h.status != CAM_REQ_CMP)
	    || ((idx->ccb.cdm.status != CAM_DEV_MATCH_LAST)
		&& (idx->ccb.cdm.status != CAM_DEV_MATCH_MORE))) {
		warnx("got CAM error %#x, CDM error %d\n",
		      idx->ccb.ccb_h.status, idx->ccb.cdm.status);
		return -1;
	}
	return 1;
}


/** Returns the next index object state and the next enumerated drive address.
    @param idx An opaque handle. Make no own theories about it.
    @param adr Takes the reply
    @param adr_size Gives maximum size of reply including final 0
    @param initialize  1 = start new,
                       0 = continue, use no other values for now
                      -1 = finish
    @return 1 = reply is a valid address , 0 = no further address available
           -1 = severe error (e.g. adr_size too small)
*/
int sg_give_next_adr(burn_drive_enumerator_t *idx_,
		     char adr[], int adr_size, int initialize)
{
	struct burn_drive_enumeration_state *idx;
	int ret;

	if (initialize == 1) {
		ret = sg_init_enumerator(idx_);
		if (ret<=0)
			return ret;
	} else if (initialize == -1) {
		sg_destroy_enumerator(idx_);
		return 0;
	}

	idx = *idx_;

	do {
		if (idx->i >= idx->ccb.cdm.num_matches) {
			ret = sg_next_enumeration_buffer(idx_);
			if (ret<=0)
				return -1;
			idx->i = 0;
		} else
			(idx->i)++;

		while (idx->i < idx->ccb.cdm.num_matches) {
			switch (idx->ccb.cdm.matches[idx->i].type) {
			case DEV_MATCH_BUS:
				break;
			case DEV_MATCH_DEVICE: {
				struct device_match_result* result;

				result = &(idx->ccb.cdm.matches[idx->i].result.device_result);
				if (result->flags & DEV_RESULT_UNCONFIGURED)
					idx->skip_device = 1;
				else
					idx->skip_device = 0;
				break;
			}
			case DEV_MATCH_PERIPH: {
				struct periph_match_result* result;

				result = &(idx->ccb.cdm.matches[idx->i].result.periph_result);
/* ts B00112 : we really want only "cd" devices.

				if (idx->skip_device || 
				    strcmp(result->periph_name, "pass") == 0)
					break;
*/
				if (idx->skip_device || 
				    strcmp(result->periph_name, "cd") != 0)
					break;
				ret = snprintf(adr, adr_size, "/dev/%s%d",
					 result->periph_name, result->unit_number);
				if(ret >= adr_size)
					return -1;

				/* Found next enumerable address */
				return 1;

			}
			default:
				/* fprintf(stderr, "unknown match type\n"); */
				break;
			}
			(idx->i)++;
		}
	} while ((idx->ccb.ccb_h.status == CAM_REQ_CMP)
		&& (idx->ccb.cdm.status == CAM_DEV_MATCH_MORE));

	return 0;
}


int sg_is_enumerable_adr(char* adr)
{
	burn_drive_enumerator_t idx;
	int ret;
	char buf[64];

	ret = sg_init_enumerator(&idx);
	if (ret <= 0)
		return 0;
	while(1) {
		ret = sg_give_next_adr(&idx, buf, sizeof(buf), 0);
		if (ret <= 0)
			break;
		if (strcmp(adr, buf) == 0) {
			sg_destroy_enumerator(&idx);
			return 1;
		}
	}
	sg_destroy_enumerator(&idx);
	return (0);
}


/** Try to obtain SCSI address parameters.
    @return  1 is success , 0 is failure
*/
int sg_obtain_scsi_adr(char *path, int *bus_no, int *host_no, int *channel_no,
                       int *target_no, int *lun_no)
{
	burn_drive_enumerator_t idx;
	int ret;
	char buf[64];
	struct periph_match_result* result;

	ret = sg_init_enumerator(&idx);
	if (ret <= 0)
		return 0;
	while(1) {
		ret = sg_give_next_adr(&idx, buf, sizeof(buf), 0);
		if (ret <= 0)
			break;
		if (strcmp(path, buf) == 0) {
			result = &(idx->ccb.cdm.matches[idx->i].result.periph_result);
			*bus_no = result->path_id;
			*host_no = result->path_id;
			*channel_no = 0;
			*target_no = result->target_id;
			*lun_no = result->target_lun;
			sg_destroy_enumerator(&idx);
			return 1;
		}
	}
	sg_destroy_enumerator(&idx);
	return (0);
}


int sg_close_drive(struct burn_drive * d)
{
	if (d->cam != NULL) {
		cam_close_device(d->cam);
		d->cam = NULL;
	}
	if (d->lock_fd > 0) {
		close(d->lock_fd);
		d->lock_fd = -1;
	}
	return 0;
}

int sg_drive_is_open(struct burn_drive * d)
{
	return (d->cam != NULL);
}

int scsi_enumerate_drives(void)
{
	burn_drive_enumerator_t idx;
	int ret;
	char buf[64];
	struct periph_match_result* result;

	ret = sg_init_enumerator(&idx);
	if (ret <= 0)
		return 0;
	while(1) {
		ret = sg_give_next_adr(&idx, buf, sizeof(buf), 0);
		if (ret <= 0)
			break;
		if (burn_drive_is_banned(buf))
			continue; 
		result = &idx->ccb.cdm.matches[idx->i].result.periph_result;
		enumerate_common(buf, result->path_id, result->path_id,
				0, result->target_id, 
				result->target_lun);
	}
	sg_destroy_enumerator(&idx);

	return 1;
}


#ifdef Scsi_freebsd_make_own_enumeratE

/* ts A61021: The old version which mixes SCSI and operating system adapter
*/
static void enumerate_common(char *fname, int bus_no, int host_no,
			     int channel_no, int target_no, int lun_no)
{
	struct burn_drive *t;
	struct burn_drive out;

	/* ts A60923 */
	out.bus_no = bus_no;
	out.host = host_no;
	out.id = target_no;
	out.channel = channel_no;
	out.lun = lun_no;

	out.devname = strdup(fname);

	out.cam = NULL;
	out.lock_fd = -1;
	out.is_ahci = 0;

	out.start_lba= -2000000000;
	out.end_lba= -2000000000;
	out.read_atip = mmc_read_atip;

	out.grab = sg_grab;
	out.release = sg_release;
	out.drive_is_open= sg_drive_is_open;
	out.issue_command = sg_issue_command;
	out.getcaps = spc_getcaps;
	out.released = 1;
	out.status = BURN_DISC_UNREADY;

	out.eject = sbc_eject;
	out.load = sbc_load;
	out.lock = spc_prevent;
	out.unlock = spc_allow;
	out.read_disc_info = spc_sense_write_params;
	out.get_erase_progress = spc_get_erase_progress;
	out.test_unit_ready = spc_test_unit_ready;
	out.probe_write_modes = spc_probe_write_modes;
	out.read_toc = mmc_read_toc;
	out.write = mmc_write;
	out.erase = mmc_erase;
	out.read_cd = mmc_read_cd;
	out.perform_opc = mmc_perform_opc;
	out.set_speed = mmc_set_speed;
	out.send_parameters = spc_select_error_params;
	out.send_write_parameters = spc_select_write_params;
	out.send_cue_sheet = mmc_send_cue_sheet;
	out.sync_cache = mmc_sync_cache;
	out.get_nwa = mmc_get_nwa;
	out.close_disc = mmc_close_disc;
	out.close_session = mmc_close_session;
	out.close_track_session = mmc_close;
	out.read_buffer_capacity = mmc_read_buffer_capacity;
	out.idata = calloc(1, sizeof(struct burn_scsi_inquiry_data));
	out.idata->valid = 0;
	out.mdata = calloc(1, sizeof(struct scsi_mode_data));
	out.mdata->valid = 0;
	if (out.idata == NULL || out.mdata == NULL) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00020108,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
			"Could not allocate new drive object", 0, 0);
		return;
	}
	memset(&out.params, 0, sizeof(struct params));
	t = burn_drive_register(&out);

/* ts A60821
   <<< debug: for tracing calls which might use open drive fds */
	mmc_function_spy(NULL, "enumerate_common : -------- doing grab");

/* try to get the drive info */
	if (t->grab(t)) {
		t->getcaps(t);
		t->unlock(t);
		t->released = 1;
	}

/* ts A60821
   <<< debug: for tracing calls which might use open drive fds */
	mmc_function_spy(NULL, "enumerate_common : ----- would release ");

}

#else /* Scsi_freebsd_make_own_enumeratE */

/* The new, more concise version of enumerate_common */
static void enumerate_common(char *fname, int bus_no, int host_no,
			     int channel_no, int target_no, int lun_no)
{
	int ret;
	struct burn_drive out;

	/* General libburn drive setup */
	burn_setup_drive(&out, fname);

	/* This transport adapter uses SCSI-family commands and models
	   (seems the adapter would know better than its boss, if ever) */
	ret = burn_scsi_setup_drive(&out, bus_no, host_no, channel_no,
                                 target_no, lun_no, 0);
        if (ret<=0)
                return;

	/* Operating system adapter is CAM */
	/* Adapter specific handles and data */
	out.cam = NULL;
	out.lock_fd = -1;
	out.is_ahci = 0;

	/* Adapter specific functions */
	out.grab = sg_grab;
	out.release = sg_release;
	out.drive_is_open = sg_drive_is_open;
	out.issue_command = sg_issue_command;

	/* Finally register drive and inquire drive information */
	burn_drive_finish_enum(&out);
}

#endif /* ! Scsi_freebsd_make_own_enumeratE */


/* Lock the inode associated to dev_fd and the inode associated to devname.
   Return OS errno, number of pass device of dev_fd, locked fd to devname,
   error message.
   A return value of > 0 means success, <= 0 means failure.
*/
static int freebsd_dev_lock(int dev_fd, char *devname,
	 int *os_errno, int *pass_dev_no, int *lock_fd, char msg[4096],
	 int flag)
{
	int lock_denied = 0, fd_stbuf_valid, name_stbuf_valid, i, pass_l = 100;
	int max_retry = 3, tries = 0;
	struct stat fd_stbuf, name_stbuf;
	char pass_name[16], *lock_name;

	*os_errno = 0;
	*pass_dev_no = -1;
	*lock_fd = -1;
	msg[0] = 0;

	fd_stbuf_valid = !fstat(dev_fd, &fd_stbuf);

	/* Try to find name of pass device by inode number */
	lock_name = (char *) "effective device";
	if(fd_stbuf_valid) {
		for (i = 0; i < pass_l; i++) {
			sprintf(pass_name, "/dev/pass%d", i);
			if (stat(pass_name, &name_stbuf) != -1)
				if(fd_stbuf.st_ino == name_stbuf.st_ino &&
			   	fd_stbuf.st_dev == name_stbuf.st_dev)	
		break;
		}
		if (i < pass_l) {
			lock_name = pass_name;
			*pass_dev_no = i;
		}
	}

	name_stbuf_valid = !stat(devname, &name_stbuf);
	for (tries= 0; tries <= max_retry; tries++) {
		lock_denied = flock(dev_fd, LOCK_EX | LOCK_NB);
		*os_errno = errno;
		if (lock_denied) {
			if (errno == EAGAIN && tries < max_retry) {
				/* <<< debugging
				fprintf(stderr,
				"\nlibcdio_DEBUG: EAGAIN pass, tries= %d\n",
					tries);
				*/
				usleep(2000000);
	continue;
			}
			sprintf(msg,
			    "Device busy. flock(LOCK_EX) failed on %s of %s",
			    strlen(lock_name) > 2000 || *pass_dev_no < 0 ?
						 "pass device" : lock_name,
			    strlen(devname) > 2000 ? "drive" : devname);
			return 0;
		}
	break;
	}

	/*
	fprintf(stderr, "libburn_DEBUG: flock obtained on %s of %s\n",
			lock_name, devname);
	*/

	/* Eventually lock the official device node too */
	if (fd_stbuf_valid && name_stbuf_valid &&
		(fd_stbuf.st_ino != name_stbuf.st_ino ||
		 fd_stbuf.st_dev != name_stbuf.st_dev)) {

		*lock_fd = open(devname, O_RDONLY);
		if (*lock_fd == 0) {
			close(*lock_fd);
			*lock_fd = -1;
		} if (*lock_fd > 0) {
			for (tries = 0; tries <= max_retry; tries++) {
				lock_denied = 
					flock(*lock_fd, LOCK_EX | LOCK_NB);
				if (lock_denied) {
					if (errno == EAGAIN &&
							 tries < max_retry) {
						/* <<< debugging
						fprintf(stderr,
				"\nlibcdio_DEBUG: EAGAIN dev, tries= %d\n",
							tries);
						*/

						usleep(2000000);
			continue;
					}
					close(*lock_fd);
					*lock_fd = -1;
					sprintf(msg,
				 "Device busy. flock(LOCK_EX) failed on %s",
				 strlen(devname) > 4000 ? "drive" : devname);
					return 0;
				}
			break;
			}
		}

/*
		fprintf(stderr, "libburn_DEBUG: flock obtained on %s\n",
				devname);
*/

	}
	return 1;
}


static int sg_lock(struct burn_drive *d, int flag)
{
	int ret, os_errno, pass_dev_no = -1, flock_fd = -1;
	char *msg = NULL;

	BURN_ALLOC_MEM(msg, char, 4096);
	ret = freebsd_dev_lock(d->cam->fd, d->devname,
				&os_errno, &pass_dev_no, &flock_fd, msg, 0);
	if (ret <= 0) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020008,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			msg, os_errno, 0);
		sg_close_drive(d);
		{ret = 0; goto ex;}
	}
	if (d->lock_fd > 0)
		close(d->lock_fd);
	d->lock_fd = flock_fd;
	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	return ret;
}


int sg_grab(struct burn_drive *d)
{
	struct cam_device *cam;
	char path_string[80];

	if (mmc_function_spy(d, "sg_grab") <= 0)
		return 0;

	if (burn_drive_is_open(d)) {
		d->released = 0;
		return 1;
	}

	cam = cam_open_device(d->devname, O_RDWR);
	if (cam == NULL) {
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x00020003,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				"Could not grab drive", errno, 0);
		return 0;
	}
	d->cam = cam;
	if (burn_sg_open_o_excl & 63)
		if (sg_lock(d, 0) <= 0)
			return 0;
	fcntl(cam->fd, F_SETOWN, getpid());

	cam_path_string(d->cam, path_string, sizeof(path_string));

#ifdef Libburn_ahci_verbouS
	fprintf(stderr, "libburn_EXPERIMENTAL: CAM path = %s\n", path_string);
#endif

	if (strstr(path_string, ":ahcich") != NULL)
		d->is_ahci = 1;
	else
		d->is_ahci = -1;

	d->released = 0;
	return 1;
}


/*
	non zero return means you still have the drive and it's not
	in a state to be released? (is that even possible?)
*/

int sg_release(struct burn_drive *d)
{
	if (mmc_function_spy(d, "sg_release") <= 0)
		return 0;

	if (d->cam == NULL)
		return 0;

	mmc_function_spy(NULL, "sg_release ----------- closing.");

	sg_close_drive(d);
	d->released = 1;
	return 0;
}

int sg_issue_command(struct burn_drive *d, struct command *c)
{
	int done = 0, err, sense_len = 0, ret, ignore_error, i;
	int cam_pass_err_recover = 0, key, asc, ascq, timeout_ms;
	union ccb *ccb;
	static FILE *fp = NULL;
	time_t start_time;

	mmc_function_spy(NULL, "sg_issue_command");

	c->error = 0;
	memset(c->sense, 0, sizeof(c->sense));

	if (d->cam == NULL)
		return 0;
	if (burn_sg_log_scsi & 1) {
		if (fp == NULL) {
			fp= fopen("/tmp/libburn_sg_command_log", "a");
			fprintf(fp,
			    "\n-----------------------------------------\n");
		}
	}
	if (burn_sg_log_scsi & 3)
		scsi_log_cmd(c,fp,0);

	c->error = 0;
	if (c->timeout > 0)
		timeout_ms = c->timeout;
	else
		timeout_ms = 200000;

	ccb = cam_getccb(d->cam);
	cam_fill_csio(&ccb->csio,
				  1,                              /* retries */
				  NULL,                           /* cbfncp */
				  CAM_DEV_QFRZDIS,                /* flags */
				  MSG_SIMPLE_Q_TAG,               /* tag_action */
				  NULL,                           /* data_ptr */
				  0,                              /* dxfer_len */
				  sizeof (ccb->csio.sense_data),  /* sense_len */
				  0,                              /* cdb_len */
				  timeout_ms);                    /* timeout */
	switch (c->dir) {
	case TO_DRIVE:
		ccb->csio.ccb_h.flags |= CAM_DIR_OUT;
		break;
	case FROM_DRIVE:
		ccb->csio.ccb_h.flags |= CAM_DIR_IN;
		break;
	case NO_TRANSFER:
		ccb->csio.ccb_h.flags |= CAM_DIR_NONE;
		break;
	}

#ifdef Libburn_for_freebsd_ahcI
	/* ts B00325 : Advise by Alexander Motin */
        /* Runs well on 8-STABLE (23 Mar 2003)
	   But on 8-RELEASE cam_send_ccb() returns non-zero with errno 6
           on eject. Long lasting TEST UNIT READY cycles break with
           errno 16.
        */
#ifdef Libburn_ahci_style_for_alL
	{
#else
	if (d->is_ahci > 0) {
#endif
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;
		cam_pass_err_recover = 1;
	}
#endif /* Libburn_for_freebsd_ahcI */

	ccb->csio.cdb_len = c->oplen;
	memcpy(&ccb->csio.cdb_io.cdb_bytes, &c->opcode, c->oplen);
	
	if (c->page) {
		ccb->csio.data_ptr  = c->page->data;
		if (c->dir == FROM_DRIVE) {

			/* ts A90430 : Ticket 148 , by jwehle :
			   "On ... FreeBSD 6.4 which has a usb memory reader in
			    addition to a ATAPI DVD burner sg_issue_command
			    will hang while the SCSI bus is being scanned"
			*/
			if (c->dxfer_len >= 0)
				ccb->csio.dxfer_len = c->dxfer_len;
			else
				ccb->csio.dxfer_len = BUFFER_SIZE;

/* touch page so we can use valgrind */
			memset(c->page->data, 0, BUFFER_SIZE);
		} else {
			ccb->csio.dxfer_len = c->page->bytes;
		}
	} else {
		ccb->csio.data_ptr  = NULL;
		ccb->csio.dxfer_len = 0;
	}

	start_time = time(NULL);
	for (i = 0; !done; i++) {

		memset(&ccb->csio.sense_data, 0, sizeof(ccb->csio.sense_data));
		memset(c->sense, 0, sizeof(c->sense));
		err = cam_send_ccb(d->cam, ccb);

		ignore_error = sense_len = 0;
		/* ts B00325 : CAM_AUTOSNS_VALID advised by Alexander Motin */
		if (ccb->ccb_h.status & CAM_AUTOSNS_VALID) {
			/* ts B00110 */
			/* Better curb sense_len */
			sense_len = ccb->csio.sense_len;
			if (sense_len > (int) sizeof(c->sense))
				sense_len = sizeof(c->sense);
			memcpy(c->sense, &ccb->csio.sense_data, sense_len);
			spc_decode_sense(c->sense, sense_len,
							&key, &asc, &ascq);
			if (sense_len >= 14 && cam_pass_err_recover && key)
				ignore_error = 1;
		}

		if (err == -1 && cam_pass_err_recover && ! ignore_error) {

#ifdef Libburn_ahci_verbouS
			fprintf(stderr, "libburn_EXPERIMENTAL: errno = %d . cam_errbuf = '%s'\n", errno, cam_errbuf);
#endif

			if (errno == ENXIO && c->opcode[0] != 0) {
				/* Operations on empty or ejected tray */
				/* MEDIUM NOT PRESENT */

#ifdef Libburn_ahci_verbouS
				fprintf(stderr, "libburn_EXPERIMENTAL: Emulating [2,3A,00] MEDIUM NOT PRESENT\n");
#endif

				c->sense[0] = 0x70; /*Fixed format sense data*/
				c->sense[2] = 0x02;
				c->sense[12] = 0x3A;
				c->sense[13] = 0x00;
				sense_len = 14;
				ignore_error = 1;
			} else if (c->opcode[0] == 0 && 
					(errno == EBUSY || errno == ENXIO)) {
				/* Timeout of TEST UNIT READY loop */
				/* Inquiries while tray is being loaded */
				/*LOGICAL UNIT NOT READY,CAUSE NOT REPORTABLE*/

#ifdef Libburn_ahci_verbouS
				fprintf(stderr, "libburn_EXPERIMENTAL: Emulating [2,04,00] LOGICAL UNIT NOT READY,CAUSE NOT REPORTABLE\n");
#endif

				c->sense[0] = 0x70; /*Fixed format sense data*/
				c->sense[2] = 0x02;
				c->sense[12] = 0x04;
				c->sense[13] = 0x00;
				sense_len = 14;
				ignore_error = 1;
			} else if (errno == EINVAL) {
				/* Inappropriate MODE SENSE */
				/* INVALID FIELD IN CDB */

#ifdef Libburn_ahci_verbouS
				fprintf(stderr, "libburn_EXPERIMENTAL: Emulating [5,24,00] INVALID FIELD IN CDB\n");
#endif

				c->sense[0] = 0x70; /*Fixed format sense data*/
				c->sense[2] = 0x05;
				c->sense[12] = 0x24;
				c->sense[13] = 0x00;
				sense_len = 14;
				ignore_error = 1;
			}
		}

		if (err == -1 && !ignore_error) {
			libdax_msgs_submit(libdax_messenger,
				d->global_index, 0x0002010c,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				"Failed to transfer command to drive",
				errno, 0);
			sg_close_drive(d);
			d->released = 1;
			d->busy = BURN_DRIVE_IDLE;
			c->error = 1;
			{ret = -1; goto ex;}
		}
		/* XXX */

		if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			if (sense_len < 14) {
				/*LOGICAL UNIT NOT READY,CAUSE NOT REPORTABLE*/

#ifdef Libburn_ahci_verbouS
				fprintf(stderr, "libburn_EXPERIMENTAL: CAM_STATUS= %d .Emulating [2,04,00] LOGICAL UNIT NOT READY,CAUSE NOT REPORTABLE\n", (ccb->ccb_h.status & CAM_STATUS_MASK));
#endif

				c->sense[0] = 0x70; /*Fixed format sense data*/
				c->sense[2] = 0x02;
				c->sense[12] = 0x04;
				c->sense[13] = 0x00;
				done = 1;
			}

			/* >>> Need own duration time measurement.
			       Then remove bit1 from flag.
			*/
			done = scsi_eval_cmd_outcome(d, c, fp, c->sense,
						sense_len, 0, start_time,
						timeout_ms, i,
						2 | !!ignore_error);
			if (d->cancel)
				done = 1;
		} else {
			done = 1;
		}
	} while (!done);
	ret = 1;
ex:;
	cam_freeccb(ccb);
	return ret;
}


/* ts B00115 */
/* Return 1 if the given path leads to a regular file or a device that can be
   seeked, read and eventually written with 2 kB granularity.
*/
int burn_os_is_2k_seekrw(char *path, int flag)
{
        struct stat stbuf;
#ifdef Libburn_DIOCGMEDIASIZE_ISBLK
	int fd, ret;
	off_t add_size;
#else
	char *spt;
	int i, e;
#endif /* ! Libburn_DIOCGMEDIASIZE_ISBLK */

        if (stat(path, &stbuf) == -1)
                return 0;
        if (S_ISREG(stbuf.st_mode))
                return 1;
	if (!S_ISCHR(stbuf.st_mode))
		return 0;

#ifdef Libburn_DIOCGMEDIASIZE_ISBLK

	/* If it throws no error with DIOCGMEDIASIZE then it is a
	   'block device'
	*/
	fd = open(path, O_RDONLY);
	if (fd == -1)
		return 0;
	ret = ioctl(fd, DIOCGMEDIASIZE, &add_size);
	close(fd);

	return (ret != -1);

#else /* Libburn_DIOCGMEDIASIZE_ISBLK */

	spt = strrchr(path, '/');
	if (spt == NULL)
	        spt = path;
	else
	        spt++;
	e = strlen(spt);
	for (i = strlen(spt) - 1; i > 0; i--)
		if (spt[i] >= '0' && spt[i] <= '9')
			e = i;
	if (strncmp(spt, "da", e) == 0) /* SCSI disk. E.g. USB stick. */
		return 1;
	if (strncmp(spt, "cd", e) == 0) /* SCSI CD drive might be writeable. */
		return 1;
	if (strncmp(spt, "ad", e) == 0) /* IDE hard drive */
		return 1;
	if (strncmp(spt, "acd", e) == 0) /* IDE CD drive might be writeable */
		return 1;
	if (strncmp(spt, "fd", e) == 0) /* Floppy disk */
		return 1;
	if (strncmp(spt, "fla", e) == 0) /* Flash drive */
		return 1;
	return 0;

#endif /* ! Libburn_DIOCGMEDIASIZE_ISBLK */

}


/* ts A70909 */
/** Estimate the potential payload capacity of a file address.
    @param path  The address of the file to be examined. If it does not
                 exist yet, then the directory will be inquired.
    @param bytes This value gets modified if an estimation is possible
    @return      -2 = cannot perform necessary operations on file object
                 -1 = neither path nor dirname of path exist
                  0 = could not estimate size capacity of file object
                  1 = estimation has been made, bytes was set
*/
int burn_os_stdio_capacity(char *path, off_t *bytes)
{
	struct stat stbuf;
	struct statvfs vfsbuf;
	char *testpath = NULL, *cpt;
	off_t add_size = 0;
	int fd, ret;

	BURN_ALLOC_MEM(testpath, char, 4096);
	testpath[0] = 0;
	if (stat(path, &stbuf) == -1) {
		strcpy(testpath, path);
		cpt = strrchr(testpath, '/');
		if(cpt == NULL)
			strcpy(testpath, ".");
		else if(cpt == testpath)
			testpath[1] = 0;
		else
			*cpt = 0;
		if (stat(testpath, &stbuf) == -1)
			{ret = -1; goto ex;}

#ifdef Libburn_if_this_was_linuX

	} else if(S_ISBLK(stbuf.st_mode)) {
		int open_mode = O_RDWR, fd, ret;
		long blocks;

		blocks = *bytes / 512;
		if(burn_sg_open_o_excl)
			open_mode |= O_EXCL;
		fd = open(path, open_mode);
		if (fd == -1)
			{ret = -2; goto ex;}
		ret = ioctl(fd, BLKGETSIZE, &blocks);
		close(fd);
		if (ret == -1)
			{ret = -2; goto ex;}
		*bytes = ((off_t) blocks) * (off_t) 512;

#endif /* Libburn_if_this_was_linuX */


	} else if(S_ISCHR(stbuf.st_mode)) {
		fd = open(path, O_RDONLY);
		if (fd == -1)
			{ret = -2; goto ex;}
		ret = ioctl(fd, DIOCGMEDIASIZE, &add_size);
		close(fd);
		if (ret == -1)
			{ret = -2; goto ex;}
		*bytes = add_size;
	} else if(S_ISREG(stbuf.st_mode)) {
		add_size = stbuf.st_blocks * (off_t) 512;
		strcpy(testpath, path);
	} else
		{ret = 0; goto ex;}

	if (testpath[0]) {	
		if (statvfs(testpath, &vfsbuf) == -1)
			{ret = -2; goto ex;}
		*bytes = add_size + ((off_t) vfsbuf.f_frsize) *
						(off_t) vfsbuf.f_bavail;
	}
	ret = 1;
ex:
	BURN_FREE_MEM(testpath);
	return ret;
}


/* ts A91122 : an interface to open(O_DIRECT) or similar OS tricks. */

#ifdef Libburn_read_o_direcT

	/* No special O_DIRECT-like precautions are implemented here */

#endif /* Libburn_read_o_direcT */


int burn_os_open_track_src(char *path, int open_flags, int flag)
{
	int fd;

	fd = open(path, open_flags);
	return fd;
}


void *burn_os_alloc_buffer(size_t amount, int flag)
{
	void *buf = NULL;

	buf = calloc(1, amount);
	return buf;
}


int burn_os_free_buffer(void *buffer, size_t amount, int flag)
{
	if (buffer == NULL)
		return 0;
	free(buffer);
	return 1;
}

