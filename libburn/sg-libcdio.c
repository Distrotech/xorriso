/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/*
   Copyright (c) 2009 - 2011 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/


/*

This is the main operating system dependent SCSI part of libburn. It implements
the transport level aspects of SCSI control and command i/o.

Present implementation: GNU libcdio , for X/Open compliant operating systems


PORTING:

Porting libburn typically will consist of adding a new operating system case
to the following switcher files:
  os.h    Operating system specific libburn definitions and declarations.
  sg.c    Operating system dependent transport level modules.
and of deriving the following system specific files from existing examples:
  os-*.h  Included by os.h. You will need some general system knowledge
          about signals and knowledge about the storage object needs of your
          transport level module sg-*.c.

  sg-*.c  This source module. You will need special system knowledge about
          how to detect all potentially available drives, how to open them,
          eventually how to exclusively reserve them, how to perform
          SCSI transactions, how to inquire the (pseudo-)SCSI driver.
          You will not need to care about CD burning, MMC or other high-level
          SCSI aspects.

Said sg-*.c operations are defined by a public function interface, which has
to be implemented in a way that provides libburn with the desired services:

sg_id_string()          returns an id string of the SCSI transport adapter.
                        It may be called before initialization but then may
                        return only a preliminary id.

sg_initialize()         performs global initialization of the SCSI transport
                        adapter and eventually needed operating system
                        facilities. Checks for compatibility of supporting
                        software components.

sg_shutdown()           performs global finalizations and releases golbally
                        aquired resources.

sg_give_next_adr()      iterates over the set of potentially useful drive 
                        address strings.

scsi_enumerate_drives() brings all available, not-whitelist-banned, and
                        accessible drives into libburn's list of drives.

sg_dispose_drive()      finalizes adapter specifics of struct burn_drive
                        on destruction. Releases resources which were aquired
                        underneath scsi_enumerate_drives().
 
sg_drive_is_open()      tells wether libburn has the given drive in use.

sg_grab()               opens the drive for SCSI commands and ensures
                        undisturbed access.

sg_release()            closes a drive opened by sg_grab()

sg_issue_command()      sends a SCSI command to the drive, receives reply,
                        and evaluates wether the command succeeded or shall
                        be retried or finally failed.

sg_obtain_scsi_adr()    tries to obtain SCSI address parameters.


burn_os_is_2k_seekrw()  tells whether the given path leads to a file object
                        that can be used in 2 kB granularity by lseek(2),
                        read(2), and possibly write(2) if not read-only..
                        E.g. a USB stick or a hard disk.

burn_os_stdio_capacity()  estimates the emulated media space of stdio-drives.

burn_os_open_track_src()  opens a disk file in a way that allows best
                        throughput with file reading and/or SCSI write command
                        transmission.

burn_os_alloc_buffer()  allocates a memory area that is suitable for file
                        descriptors issued by burn_os_open_track_src().
                        The buffer size may be rounded up for alignment
                        reasons.

burn_os_free_buffer()   delete a buffer obtained by burn_os_alloc_buffer().

Porting hints are marked by the text "PORTING:".
Send feedback to libburn-hackers@pykix.org .

*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif


/** PORTING : ------- OS dependent headers and definitions ------ */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>

#ifdef Libburn_os_has_statvfS
#include <sys/statvfs.h>
#endif /* Libburn_os_has_stavtfS */

#ifdef __linux
/* for ioctl(BLKGETSIZE) */
#include <sys/ioctl.h>
#include <linux/fs.h>
#endif

#ifdef __FreeBSD__
#define Libburn_is_on_freebsD 1
#endif
#ifdef __FreeBSD_kernel__
#define Libburn_is_on_freebsD 1
#endif
#ifdef Libburn_is_on_freebsD
/* To avoid ATAPI devices */
#define Libburn_guess_freebsd_atapi_devicE 1
/* To obtain size of disk-like devices */
#include <sys/disk.h> /* DIOCGMEDIASIZE */
#endif /* Libburn_is_on_freebsD */

#define Libburn_guess_freebsd_atapi_devicE 1

#ifdef sun
#define Libburn_is_on_solariS 1
#endif
#ifdef __sun
#define Libburn_is_on_solariS 1
#endif

/* Proposal by Rocky Bernstein to avoid macro clashes with cdio_config.h */
#define __CDIO_CONFIG_H__ 1

#include <cdio/cdio.h>
#include <cdio/logging.h>
#include <cdio/mmc.h>


/* The waiting time before eventually retrying a failed SCSI command.
   Before each retry wait Libburn_sg_linux_retry_incR longer than with
   the previous one.
*/
#define Libburn_sg_libcdio_retry_usleeP 100000
#define Libburn_sg_libcdio_retry_incR   100000


/** PORTING : ------ libburn portable headers and definitions ----- */

#include "transport.h"
#include "drive.h"
#include "sg.h"
#include "spc.h"
/* collides with symbols of <cdio/mmc.h>
 #include "mmc.h"
*/
#include "sbc.h"
#include "debug.h"
#include "toc.h"
#include "util.h"
#include "init.h"

#include "libdax_msgs.h"
extern struct libdax_msgs *libdax_messenger;


/* is in portable part of libburn */
int burn_drive_is_banned(char *device_address);
int burn_drive_resolve_link(char *path, char adr[],
			 int *recursion_count, int flag); /* drive.c */

/* Whether to log SCSI commands:
   bit0= log in /tmp/libburn_sg_command_log
   bit1= log to stderr
   bit2= flush every line
*/
extern int burn_sg_log_scsi;


/* ------------------------------------------------------------------------ */
/* PORTING:   Private definitions. Port only if needed by public functions. */
/*            (Public functions are listed below)                           */
/* ------------------------------------------------------------------------ */


/* Storage object is in libburn/init.c
   whether to strive for exclusive access to the drive
*/
extern int burn_sg_open_o_excl;


/* ------------------------------------------------------------------------ */
/* PORTING: Private functions. Port only if needed by public functions      */
/*          (Public functions are listed below)                             */
/* ------------------------------------------------------------------------ */


static int sg_close_drive(struct burn_drive * d)
{
	CdIo_t *p_cdio;

	if (d->p_cdio != NULL) {
		p_cdio = (CdIo_t *) d->p_cdio;
		cdio_destroy(p_cdio);
		d->p_cdio = NULL;
	}
	return 0;
}


static int sg_give_next_adr_raw(burn_drive_enumerator_t *idx,
				     char adr[], int adr_size, int initialize)
{
	char **pos;
	int count = 0;

	if (initialize == 1) {
		idx->pos = idx->ppsz_cd_drives =
					cdio_get_devices(DRIVER_DEVICE);
		if (idx->ppsz_cd_drives == NULL)
			return 0;

		for (pos = idx->ppsz_cd_drives ; pos != NULL; pos++) {
			if (*pos == NULL)
		break;
			count++;
		}

	} else if (initialize == -1) {
		if (idx->ppsz_cd_drives != NULL)
			if (*(idx->ppsz_cd_drives) != NULL)
				cdio_free_device_list(idx->ppsz_cd_drives);
		idx->ppsz_cd_drives = NULL;
	}

#ifdef Libburn_guess_freebsd_atapi_devicE
try_next:;
#endif

	if (idx->pos == NULL)
		return 0;
	if (*(idx->pos) == NULL)
		return 0;

#ifdef Libburn_guess_freebsd_atapi_devicE
	if (strncmp(*(idx->pos), "/dev/acd", 8) == 0) {
		(idx->pos)++;
		goto try_next;
	}
#endif

	if ((ssize_t) strlen(*(idx->pos)) >= adr_size)
		return -1;
	strcpy(adr, *(idx->pos));
	(idx->pos)++;
	return 1;
}


/* ----------------------------------------------------------------------- */
/* PORTING: Private functions which contain publicly needed functionality. */
/*          Their portable part must be performed. So it is probably best  */
/*          to replace the non-portable part and to call these functions   */
/*          in your port, too.                                             */
/* ----------------------------------------------------------------------- */


/** Wraps a detected drive into libburn structures and hands it over to
    libburn drive list.
*/
static void enumerate_common(char *fname, char *cdio_name,
				int bus_no, int host_no,
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
        if (ret <= 0)
                return;

	/* PORTING: ------------------- non portable part --------------- */

	/* Transport adapter is libcdio */
	/* Adapter specific handles and data */
	out.p_cdio = NULL;
	strcpy(out.libcdio_name, fname);
	if (strlen(cdio_name) < sizeof(out.libcdio_name))
		strcpy(out.libcdio_name, cdio_name);

	/* PORTING: ---------------- end of non portable part ------------ */

	/* Adapter specific functions with standardized names */
	out.grab = sg_grab;
	out.release = sg_release;
	out.drive_is_open = sg_drive_is_open;
	out.issue_command = sg_issue_command;
	/* Finally register drive and inquire drive information */
	burn_drive_finish_enum(&out);
}


/* ------------------------------------------------------------------------ */
/* PORTING:           Public functions. These MUST be ported.               */
/* ------------------------------------------------------------------------ */


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
	char *version_text;

	sprintf(msg, "sg-libcdio h%d with libcdio ", LIBCDIO_VERSION_NUM);

 #if LIBCDIO_VERSION_NUM < 83 

LIBBURN_MISCONFIGURATION = 0;
INTENTIONAL_ABORT_OF_COMPILATION__HEADERFILE_cdio_version_dot_h_TOO_OLD__NEED_libcdio_VERSION_NUM_83 = 0;
LIBBURN_MISCONFIGURATION_ = 0;

 #endif /* LIBCDIO_VERSION_NUM < 83  */

	version_text = (char *) cdio_version_string;
	strncat(msg, version_text, 800);
	return 1;
}


/** Performs global initialization of the SCSI transport adapter and eventually
    needed operating system facilities. Checks for compatibility of supporting
    software components.
    @param msg   returns ids and/or error messages of eventual helpers
    @param flag  unused yet, submit 0
    @return      1 = success, <=0 = failure
*/ 
int sg_initialize(char msg[1024], int flag)
{
	int cdio_ver;
	char *msg_pt;

	cdio_loglevel_default = CDIO_LOG_ASSERT;

	msg[0] = 0;
	sg_id_string(msg, 0);
	cdio_ver = libcdio_version_num;
	libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
		LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
		msg , 0, 0);
	if (cdio_ver < LIBCDIO_VERSION_NUM) {
		strcat(msg, " ---> ");
		msg_pt = msg + strlen(msg);
		sprintf(msg_pt,
		    "libcdio TOO OLD: numeric version %d , need at least %d",
		    cdio_ver, LIBCDIO_VERSION_NUM);
		libdax_msgs_submit(libdax_messenger, -1,
			0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			msg_pt, 0, 0);
		return 0;
	}
	return 1;
}


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


/** Returns the next index number and the next enumerated drive address.
    The enumeration has to cover all available and accessible drives. It is
    allowed to return addresses of drives which are not available but under
    some (even exotic) circumstances could be available. It is on the other
    hand allowed, only to hand out addresses which can really be used right
    in the moment of this call. (This implementation chooses the latter.)
    @param idx An opaque handle. Make no own theories about it.
    @param adr Takes the reply
    @param adr_size Gives maximum size of reply including final 0
    @param initialize  1 = start new,
                       0 = continue, use no other values for now
                      -1 = finish
    @return 1 = reply is a valid address , 0 = no further address available
           -1 = severe error (e.g. adr_size too small)
*/
int sg_give_next_adr(burn_drive_enumerator_t *idx,
		     char adr[], int adr_size, int initialize)
{
	int ret, recursion_count = 0, path_size = 4096;
	char *path = NULL;
#ifdef Libburn_is_on_solariS
	int l;
#endif
	BURN_ALLOC_MEM(path, char, path_size);

	ret = sg_give_next_adr_raw(idx, adr, adr_size, initialize);
	if (ret <= 0)
		goto ex;
	if ((ssize_t) strlen(adr) >= path_size)
		goto ex;

#ifdef Libburn_is_on_solariS
	/* >>> provisory : preserve Solaris /dev/rdsk/cXtYdZs2 addresses */
	l = strlen(adr);
	if (l >= 18)
		if (strncmp(adr, "/dev/rdsk/c", 11) == 0 && adr[11] >= '0' &&
		    adr[11] <= '9' && strcmp(adr + (l - 2), "s2") == 0)
			{ret = 1; goto ex;}
#endif /* Libburn_is_on_solariS */

	ret = burn_drive_resolve_link(adr, path, &recursion_count, 2);
        if(ret > 0 && (ssize_t) strlen(path) < adr_size)
		strcpy(adr, path);
	ret = (ret >= 0);
ex:
	BURN_FREE_MEM(path);
	return ret;
}


/** Brings all available, not-whitelist-banned, and accessible drives into
    libburn's list of drives.
*/
int scsi_enumerate_drives(void)
{
	burn_drive_enumerator_t idx;
	int initialize = 1, ret, i_bus_no = -1, recursion_count = 0;
        int i_host_no = -1, i_channel_no = -1, i_target_no = -1, i_lun_no = -1;
	int buf_size = 4096;
	char *buf = NULL, *target = NULL;
#ifdef Libburn_is_on_solariS
	int l;
#endif

	BURN_ALLOC_MEM(buf, char, buf_size);
	BURN_ALLOC_MEM(target, char, buf_size);

	while(1) {
		ret = sg_give_next_adr_raw(&idx, buf, buf_size, initialize);
		initialize = 0;
		if (ret <= 0)
	break;
		ret = 1;

#ifdef Libburn_is_on_solariS 
		/* >>> provisory : preserve Solaris /dev/rdsk/cXtYdZs2 */
		l = strlen(buf);
		if (l >= 18)
			if (strncmp(buf, "/dev/rdsk/c", 11) == 0 &&
			    buf[11] >= '0' && buf[11] <= '9' &&
			    strcmp(buf + (l - 2), "s2") == 0)
				ret = 0;
#endif /* Libburn_is_on_solariS */

		if (ret == 1) {
			ret = burn_drive_resolve_link(buf, target,
							 &recursion_count,2);
		}
		if (ret <= 0)
			strcpy(target, buf);
		if (burn_drive_is_banned(target))
	continue; 
		sg_obtain_scsi_adr(buf, &i_bus_no, &i_host_no,
				&i_channel_no, &i_target_no, &i_lun_no);
		enumerate_common(target, buf,
				i_bus_no, i_host_no, i_channel_no,
				i_target_no, i_lun_no);
	}
	sg_give_next_adr(&idx, buf, buf_size, -1);
	ret = 1;
ex:;
	BURN_FREE_MEM(buf);
	BURN_FREE_MEM(target);
	return ret;
}


/** Tells whether libburn has the given drive in use or exclusively reserved.
    If it is "open" then libburn will eventually call sg_release() on it when
    it is time to give up usage resp. reservation.
*/
/** Published as burn_drive.drive_is_open() */
int sg_drive_is_open(struct burn_drive * d)
{
	return (d->p_cdio != NULL);
}


/** Opens the drive for SCSI commands and - if burn activities are prone
    to external interference on your system - obtains an exclusive access lock
    on the drive. (Note: this is not physical tray locking.)
    A drive that has been opened with sg_grab() will eventually be handed
    over to sg_release() for closing and unreserving. 
*/  
int sg_grab(struct burn_drive *d)
{
	CdIo_t *p_cdio;
	char *am_eff, *msg = NULL, *am_wanted;
	int os_errno, second_try = 0, ret;

	if (d->p_cdio != NULL) {
		d->released = 0;
		{ret = 1; goto ex;}
	}
	if (d->libcdio_name[0] == 0) /* just to be sure it is initialized */
		strcpy(d->libcdio_name, d->devname);
	am_wanted = (burn_sg_open_o_excl & 63) ?  "MMC_RDWR_EXCL" : "MMC_RDWR";
try_to_open:;
	p_cdio = cdio_open_am(d->libcdio_name, DRIVER_DEVICE, am_wanted);
	if (p_cdio == NULL) {
		BURN_ALLOC_MEM(msg, char, 4096);
		os_errno = errno;
		sprintf(msg, "Could not grab drive '%s'", d->devname);
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020003,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			msg, os_errno, 0);
		{ret = 0; goto ex;}
	}
	am_eff = (char *) cdio_get_arg(p_cdio, "access-mode");
        if (strncmp(am_eff, "MMC_RDWR", 8) != 0) {
		cdio_destroy(p_cdio);
		if (!second_try) {
			am_wanted = (burn_sg_open_o_excl & 63) ?
						"MMC_RDWR" : "MMC_RDWR_EXCL";
			second_try = 1;
			goto try_to_open;
		}
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020003,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"libcdio provides no MMC_RDWR access mode", 0, 0);
		{ret = 0; goto ex;}
        }

	d->p_cdio = p_cdio;
	d->released = 0;
	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	return ret;
}


/** PORTING: Is mainly about the call to sg_close_drive() and whether it
             implements the demanded functionality.
*/
/** Gives up the drive for SCSI commands and releases eventual access locks.
    (Note: this is not physical tray locking.) 
*/
int sg_release(struct burn_drive *d)
{
	if (d->p_cdio == NULL)
		return 0;
	sg_close_drive(d);
	return 0;
}


/** Sends a SCSI command to the drive, receives reply and evaluates wether
    the command succeeded or shall be retried or finally failed.
    Returned SCSI errors shall not lead to a return value indicating failure.
    The callers get notified by c->error. An SCSI failure which leads not to
    a retry shall be notified via scsi_notify_error().
    The Libburn_log_sg_commandS facility might be of help when problems with
    a drive have to be examined. It shall stay disabled for normal use.
    @return: 1 success , <=0 failure
*/
int sg_issue_command(struct burn_drive *d, struct command *c)
{
	int sense_valid = 0, i, timeout_ms, sense_len;
	int key = 0, asc = 0, ascq = 0, done = 0;
	time_t start_time;
        driver_return_code_t i_status;
	unsigned int dxfer_len;
        static FILE *fp = NULL;
	mmc_cdb_t cdb = {{0, }};
	cdio_mmc_direction_t e_direction;
	CdIo_t *p_cdio;
	cdio_mmc_request_sense_t *sense_pt = NULL;

	c->error = 0;
	memset(c->sense, 0, sizeof(c->sense));

	if (d->p_cdio == NULL) {
		return 0;
	}
	p_cdio = (CdIo_t *) d->p_cdio;
	if (burn_sg_log_scsi & 1) {
		if (fp == NULL) {
			fp= fopen("/tmp/libburn_sg_command_log", "a");
			fprintf(fp,
			    "\n-----------------------------------------\n");
		}
	}
	if (burn_sg_log_scsi & 3)
		scsi_log_cmd(c,fp,0);

	memcpy(cdb.field, c->opcode, c->oplen);
	if (c->dir == TO_DRIVE) {
		dxfer_len = c->page->bytes;
		e_direction = SCSI_MMC_DATA_WRITE;
	} else if (c->dir == FROM_DRIVE) {
		if (c->dxfer_len >= 0)
			dxfer_len = c->dxfer_len;
		else
			dxfer_len = BUFFER_SIZE;
		e_direction = SCSI_MMC_DATA_READ;
		/* touch page so we can use valgrind */
		memset(c->page->data, 0, BUFFER_SIZE);
	} else {
		dxfer_len = 0;
		e_direction = SCSI_MMC_DATA_NONE;
	}
		
	/* retry-loop */
	start_time = time(NULL);
	if (c->timeout > 0)
		timeout_ms = c->timeout;
	else
		timeout_ms = 200000;
	for(i = 0; !done; i++) {

		memset(c->sense, 0, sizeof(c->sense));
		i_status = mmc_run_cmd(p_cdio, timeout_ms, &cdb, e_direction,
				 	dxfer_len, c->page->data);

		sense_valid = mmc_last_cmd_sense(p_cdio, &sense_pt);
		if (sense_valid >= 18) {
			memcpy(c->sense, (unsigned char *) sense_pt,
				(size_t) sense_valid >= sizeof(c->sense) ?
				sizeof(c->sense) : (size_t) sense_valid );
			spc_decode_sense(c->sense, 0, &key, &asc, &ascq);
		} else
			key = asc = ascq = 0;
		if (sense_pt != NULL)
			free(sense_pt);

/* Regrettably mmc_run_cmd() does not clearly distinguish between transport
   failure and SCSI error reply.
   This reaction here would be for transport failure:

		if (i_status != 0 && i_status != DRIVER_OP_ERROR) {
			libdax_msgs_submit(libdax_messenger,
				d->global_index, 0x0002010c,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				"Failed to transfer command to drive",
				errno, 0);
			sg_close_drive(d);
			d->released = 1;
			d->busy = BURN_DRIVE_IDLE;
			c->error = 1;
			return -1;
		}
*/

		if ((!sense_valid) || (key == 0 && asc == 0 && ascq == 0)) {
			memset(c->sense, 0, sizeof(c->sense));
			if (i_status != 0) { /* set dummy sense */
				/*LOGICAL UNIT NOT READY,
					CAUSE NOT REPORTABLE*/
				c->sense[0] = 0x70; /*Fixed format sense data*/
				c->sense[2] = 0x02;
				c->sense[12] = 0x04;
				done = 1;
			}
		} 
		if (key || asc || ascq)
			sense_len = 18;
		else
			sense_len = 0;
		done = scsi_eval_cmd_outcome(d, c, fp,  c->sense, sense_len,
					0, start_time, timeout_ms, i, 2);
		if (d->cancel)
			done = 1;

	} /* end of retry-loop */

	return 1;
}


/** Tries to obtain SCSI address parameters.
    @return  1 is success , 0 is failure
*/
int sg_obtain_scsi_adr(char *path, int *bus_no, int *host_no, int *channel_no,
                       int *target_no, int *lun_no)
{
	CdIo_t *p_cdio;
	char *tuple;

	*bus_no = *host_no = *channel_no = *target_no = *lun_no = -1;

	p_cdio = cdio_open(path, DRIVER_DEVICE);
	if (p_cdio == NULL)
		return 0;

	/* Try whether a bus,host,channel,target,lun address tuple is
	   available */
	tuple = (char *) cdio_get_arg(p_cdio, "scsi-tuple");
        if (tuple != NULL) if (tuple[0]) {
		sscanf(tuple, "%d,%d,%d,%d,%d",
			bus_no, host_no, channel_no, target_no, lun_no);
	}

	cdio_destroy(p_cdio);
	return (*bus_no >= 0);
}


/** Tells wether a text is a persistent address as listed by the enumeration
    functions.
*/
int sg_is_enumerable_adr(char* adr)
{
	burn_drive_enumerator_t idx;
	int initialize = 1, ret;
	char buf[64];

	while(1) {
		ret = sg_give_next_adr(&idx, buf, sizeof(buf), initialize);
		initialize = 0;
		if (ret <= 0)
	break;
		if (strcmp(adr, buf) == 0) {
			sg_give_next_adr(&idx, buf, sizeof(buf), -1);
			return 1;
		}
	}
	sg_give_next_adr(&idx, buf, sizeof(buf), -1);
	return (0);
}


#ifdef __FreeBSD__
#define Libburn_guess_block_devicE 1
#endif
#ifdef __FreeBSD_kernel__
#define Libburn_guess_block_devicE 1
#endif

#ifdef Libburn_guess_block_devicE

/* ts B00115 */
/* The FreeBSD implementation of burn_os_is_2k_seekrw().
   On FreeBSD there are no block devices.
*/
static int freebsd_is_2k_seekrw(char *path, int flag)
{
        struct stat stbuf;
	char *spt;
	int i, e;

        if (stat(path, &stbuf) == -1)
                return 0;
        if (S_ISREG(stbuf.st_mode))
                return 1;
	if (!S_ISCHR(stbuf.st_mode))
		return 0;
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
}

#endif /* Libburn_guess_block_devicE */


/* Return 1 if the given path leads to a regular file or a device that can be
   seeked, read, and possibly written with 2 kB granularity. 
*/
int burn_os_is_2k_seekrw(char *path, int flag)
{
#ifdef Libburn_guess_block_devicE
	return freebsd_is_2k_seekrw(path, flag);
#else 
	struct stat stbuf;

	if (stat(path, &stbuf) == -1)
		return 0;
	if (S_ISREG(stbuf.st_mode))
		return 1;
	if (S_ISBLK(stbuf.st_mode))
		return 1;
	return 0;
#endif /* ! Libburn_guess_block_devicE */
}


/** Estimate the potential payload capacity of a file address.
    @param path  The address of the file to be examined. If it does not
                 exist yet, then the directory will be inquired.
    @param bytes The pointed value gets modified, but only if an estimation is
                 possible.
    @return      -2 = cannot perform necessary operations on file object
                 -1 = neither path nor dirname of path exist
                  0 = could not estimate size capacity of file object
                  1 = estimation has been made, bytes was set
*/
int burn_os_stdio_capacity(char *path, off_t *bytes)
{
	struct stat stbuf;

#ifdef Libburn_os_has_statvfS
	struct statvfs vfsbuf;
#endif

	char *testpath = NULL, *cpt;
	off_t add_size = 0;
	int ret;

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

#ifdef __linux

	/* GNU/Linux specific determination of block device size */
	} else if(S_ISBLK(stbuf.st_mode)) {
		int open_mode = O_RDONLY, fd;
		long blocks;

		blocks = *bytes / 512;
		fd = open(path, open_mode);
		if (fd == -1)
			{ret = -2; goto ex;}
		ret = ioctl(fd, BLKGETSIZE, &blocks);
		close(fd);
		if (ret == -1)
			{ret = -2; goto ex;}
		*bytes = ((off_t) blocks) * (off_t) 512;

#endif /* __linux */

#ifdef Libburn_is_on_freebsD

	} else if(S_ISCHR(stbuf.st_mode)) {
		int fd;

		fd = open(path, O_RDONLY);
		if (fd == -1)
			{ret = -2; goto ex;}
		ret = ioctl(fd, DIOCGMEDIASIZE, &add_size);
		close(fd);
		if (ret == -1)
			{ret = -2; goto ex;}
		*bytes = add_size;

#endif /* Libburn_is_on_freebsD */

#ifdef Libburn_is_on_solariS

	} else if(S_ISBLK(stbuf.st_mode)) {
		int open_mode = O_RDONLY, fd;
		
		fd = open(path, open_mode);
		if (fd == -1)
			{ret = -2; goto ex;}
		*bytes = lseek(fd, 0, SEEK_END);
		close(fd);
		if (*bytes == -1) {
			*bytes = 0;
			{ret = 0; goto ex;}
		}
		
#endif /* Libburn_is_on_solariS */

	} else if(S_ISREG(stbuf.st_mode)) {
		add_size = stbuf.st_blocks * (off_t) 512;
		strcpy(testpath, path);
	} else
		{ret = 0; goto ex;}

	if (testpath[0]) {	

#ifdef Libburn_os_has_statvfS

		if (statvfs(testpath, &vfsbuf) == -1)
			{ret = -2; goto ex;}
		*bytes = add_size + ((off_t) vfsbuf.f_frsize) *
						(off_t) vfsbuf.f_bavail;

#else /* Libburn_os_has_statvfS */

		{ret = 0; goto ex;}

#endif /* ! Libburn_os_has_stavtfS */

	}
	ret = 1;
ex:;
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

