/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/*
   Copyright (c) 2010 - 2011 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/


/*

This is the main operating system dependent SCSI part of libburn. It implements
the transport level aspects of SCSI control and command i/o.

Present implementation: Solaris uscsi, e.g. for SunOS 5.11


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
#include <stropts.h>
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#ifdef Libburn_os_has_statvfS
#include <sys/statvfs.h>
#endif /* Libburn_os_has_stavtfS */

#include <sys/dkio.h>
#include <sys/vtoc.h>

#include <sys/scsi/impl/uscsi.h>


/* The waiting time before eventually retrying a failed SCSI command.
   Before each retry wait Libburn_sg_linux_retry_incR longer than with
   the previous one.
*/
#define Libburn_sg_solaris_retry_usleeP 100000
#define Libburn_sg_solaris_retry_incR   100000


/** PORTING : ------ libburn portable headers and definitions ----- */

#include "transport.h"
#include "drive.h"
#include "sg.h"
#include "spc.h"
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
	if (d->fd != -1) {
		close(d->fd);
		d->fd = -1;
		return 1;
	}
	return 0;
}


static int decode_btl_number(char **cpt, int stopper, int *no)
{
  *no = 0;
  for ((*cpt)++; **cpt != stopper; (*cpt)++) {
    if (**cpt < '0' || **cpt > '9')
      return 0;
    *no = *no * 10 + **cpt - '0';
  }
  return 1;
}


/* Read bus, target, lun from name "cXtYdZs2".
   Return 0 if name is not of the desired form.
*/
static int decode_btl_solaris(char *name, int *busno, int *tgtno, int *lunno,
                        int flag)
{
  char *cpt;
  int ret;

  *busno = *tgtno = *lunno = -1;
  cpt = name;
  if (*cpt != 'c')
    return 0;
  ret = decode_btl_number(&cpt, 't', busno);
  if (ret <= 0)
    return ret;
  ret = decode_btl_number(&cpt, 'd', tgtno);
  if (ret <= 0)
    return ret;
  ret = decode_btl_number(&cpt, 's', lunno);
  if (ret <= 0)
    return ret;
  cpt++;
  if (*cpt != '2' || *(cpt + 1) != 0)
    return 0;
  return 1;
}


static int start_enum_cXtYdZs2(burn_drive_enumerator_t *idx, int flag)
{
	DIR *dir;

	idx->dir = NULL;
	dir = opendir("/dev/rdsk");
	if (dir == NULL) {
		libdax_msgs_submit(libdax_messenger, -1,
		    0x0002000c, LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
	"Cannot start device file enumeration. opendir(\"/dev/rdsk\") failed.",
		    errno, 0);
		return 0;
	}
	idx->dir = dir;
	return 1;
}


static int next_enum_cXtYdZs2(burn_drive_enumerator_t *idx,
				char adr[], int adr_size, int flag)
{ 
	int busno, tgtno, lunno, ret, fd = -1, volpath_size = 160;
	char *volpath = NULL;
	struct dirent *entry;
	struct dk_cinfo cinfo;
	DIR *dir;

	BURN_ALLOC_MEM(volpath, char, volpath_size);

	dir = idx->dir;
	while (1) {
		errno = 0;
		entry = readdir(dir);
		if (entry == NULL) {
			if (errno) {
				libdax_msgs_submit(libdax_messenger,
						-1, 0x0002000d,
				LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
	"Cannot enumerate next device. readdir() from \"/dev/rdsk\" failed.",
						errno, 0);
				{ret = -1; goto ex;}
			}
	break;
		}
		if (strlen(entry->d_name) > (size_t) (volpath_size - 11))
	continue;
		ret = decode_btl_solaris(entry->d_name,
					&busno, &tgtno, &lunno, 0);
		if (ret <= 0)
	continue; /* not cXtYdZs2 */

		sprintf(volpath, "/dev/rdsk/%s", entry->d_name);
		if (burn_drive_is_banned(volpath))
	continue; 

		fd = open(volpath, O_RDONLY | O_NDELAY);
		if (fd < 0)
	continue;
		/* See man dkio */
		ret = ioctl(fd, DKIOCINFO, &cinfo);
		close(fd);
		if (ret < 0)
	continue;
		if (cinfo.dki_ctype != DKC_CDROM)
	continue;
		if (adr_size <= (int) strlen(volpath))
			{ret = -1; goto ex;}
		strcpy(adr, volpath);
		{ret = 1; goto ex;}
	}
	ret = 0;
ex:;
	BURN_FREE_MEM(volpath);
	return ret;
}


static int end_enum_cXtYdZs2(burn_drive_enumerator_t *idx, int flag)
{
	DIR *dir;

	dir = idx->dir;
	if(dir != NULL)
		closedir(dir);
	idx->dir = NULL;
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
static void enumerate_common(char *fname,
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

	/* Transport adapter is Solaris uscsi */
	/* Adapter specific handles and data */
	out.fd = -1;

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
	sprintf(msg, "internal Solaris uscsi adapter sg-solaris");
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
	return sg_id_string(msg, 0);
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
	int ret;

	if (initialize == 1) {
		ret = start_enum_cXtYdZs2(idx, 0);
		if (ret <= 0)
			return ret;
	} else if (initialize == -1) {
		ret = end_enum_cXtYdZs2(idx, 0);
		return 0;
	}
	ret = next_enum_cXtYdZs2(idx, adr, adr_size, 0);
	return ret;
}


/** Brings all available, not-whitelist-banned, and accessible drives into
    libburn's list of drives.
*/
int scsi_enumerate_drives(void)
{
	burn_drive_enumerator_t idx;
	int initialize = 1, ret, i_bus_no = -1, buf_size = 4096;
        int i_host_no = -1, i_channel_no = -1, i_target_no = -1, i_lun_no = -1;
	char *buf = NULL;

	BURN_ALLOC_MEM(buf, char, buf_size);

	while(1) {
		ret = sg_give_next_adr(&idx, buf, buf_size, initialize);
		initialize = 0;
		if (ret <= 0)
	break;
		if (burn_drive_is_banned(buf))
	continue; 
		sg_obtain_scsi_adr(buf, &i_bus_no, &i_host_no,
				&i_channel_no, &i_target_no, &i_lun_no);
		enumerate_common(buf,
				i_bus_no, i_host_no, i_channel_no,
				i_target_no, i_lun_no);
	}
	sg_give_next_adr(&idx, buf, buf_size, -1);
	ret = 1;
ex:;
	BURN_FREE_MEM(buf);
	return ret;
}


/** Tells whether libburn has the given drive in use or exclusively reserved.
    If it is "open" then libburn will eventually call sg_release() on it when
    it is time to give up usage resp. reservation.
*/
/** Published as burn_drive.drive_is_open() */
int sg_drive_is_open(struct burn_drive * d)
{
	return (d->fd != -1);
}


/** Opens the drive for SCSI commands and - if burn activities are prone
    to external interference on your system - obtains an exclusive access lock
    on the drive. (Note: this is not physical tray locking.)
    A drive that has been opened with sg_grab() will eventually be handed
    over to sg_release() for closing and unreserving. 
*/  
int sg_grab(struct burn_drive *d)
{
	char *msg = NULL;
	int os_errno, ret;
	struct dk_cinfo cinfo;

	BURN_ALLOC_MEM(msg, char, 4096);

	if (d->fd != -1) {
		d->released = 0;
		{ret = 1; goto ex;}
	}
	d->fd = open(d->devname, O_RDONLY | O_NDELAY);
	if (d->fd == -1) {
		os_errno = errno;
		sprintf(msg, "Could not grab drive '%s'", d->devname);
		libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020003,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			msg, os_errno, 0);
		{ret = 0; goto ex;}
	}
	ret = ioctl(d->fd, DKIOCINFO, &cinfo);
	if (ret < 0)
		goto revoke;
	if (cinfo.dki_ctype != DKC_CDROM)
		goto revoke;

	/* >>> obtain eventual locks */;

	d->released = 0;
	{ret = 1; goto ex;}
revoke:;
	sprintf(msg, "Could not grab drive '%s'. Not a CDROM device.",
		 d->devname);
	libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020003,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
	ret = 0;
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
	if (d->fd < 0)
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
	int i, timeout_ms, ret, key, asc, ascq, done = 0, sense_len;
	time_t start_time;
	struct uscsi_cmd cgc;
	char msg[80];
        static FILE *fp = NULL;

	c->error = 0;
	memset(c->sense, 0, sizeof(c->sense));

	if (d->fd == -1)
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

	if (c->timeout > 0)
		timeout_ms = c->timeout;
	else
		timeout_ms = 200000;
	memset (&cgc, 0, sizeof (struct uscsi_cmd));
	/* No error messages, no retries,
           do not execute with other commands, request sense data
	*/
	cgc.uscsi_flags = USCSI_SILENT | USCSI_DIAGNOSE | USCSI_ISOLATE
				| USCSI_RQENABLE;
	cgc.uscsi_timeout = timeout_ms / 1000;
	cgc.uscsi_cdb = (caddr_t) c->opcode;
	cgc.uscsi_bufaddr = (caddr_t) c->page->data;
	if (c->dir == TO_DRIVE) {
		cgc.uscsi_flags |= USCSI_WRITE;
		cgc.uscsi_buflen = c->page->bytes;
	} else if (c->dir == FROM_DRIVE) {
		cgc.uscsi_flags |= USCSI_READ;
		if (c->dxfer_len >= 0)
			cgc.uscsi_buflen = c->dxfer_len;
		else
			cgc.uscsi_buflen = BUFFER_SIZE;
		/* touch page so we can use valgrind */
		memset(c->page->data, 0, BUFFER_SIZE);
	} else {
		cgc.uscsi_buflen = 0;
	}
	cgc.uscsi_cdblen  = c->oplen;
	cgc.uscsi_rqlen = sizeof(c->sense);
	cgc.uscsi_rqbuf = (caddr_t) c->sense;

	/* retry-loop */
	start_time = time(NULL);
	for(i = 0; !done; i++) {

		memset(c->sense, 0, sizeof(c->sense));
		ret = ioctl(d->fd, USCSICMD, &cgc);

		/* For cgc.uscsi_status see SAM-3 5.3.1, Table 22
		   0 = GOOD , 2 = CHECK CONDITION : Sense Data are delivered
		   8 = BUSY
		*/
		if (ret != 0 && cgc.uscsi_status != 2) {
			sprintf(msg,
		"Failed to transfer command to drive. (uscsi_status = 0x%X)",
				(unsigned int) cgc.uscsi_status),
			libdax_msgs_submit(libdax_messenger,
				d->global_index, 0x0002010c,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				msg, errno, 0);
			sg_close_drive(d);
			d->released = 1;
			d->busy = BURN_DRIVE_IDLE;
			c->error = 1;
			return -1;
		}


		/* >>> Should replace "18" by realistic sense length.
		       What's about following older remark ?
		*/
		/* >>> valid sense:  cgc.uscsi_rqlen - cgc.uscsi_rqresid */;

		spc_decode_sense(c->sense, 0, &key, &asc, &ascq);
		if (key || asc || ascq)
			sense_len = 18;
		else
			sense_len = 0;
		done = scsi_eval_cmd_outcome(d, c, fp, c->sense, sense_len, 0,
						start_time, timeout_ms, i, 2);
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
	int ret;

	/* Try to guess from path */
	if (strncmp("/dev/rdsk/", path, 10) == 0) {
		ret = decode_btl_solaris(path + 10,
					bus_no, target_no, lun_no, 0);
		if (ret > 0) {
			*host_no = *bus_no;
			*channel_no = 0;
			return 1;
		}
	}
	*bus_no = *host_no = *channel_no = *target_no = *lun_no = -1;

	/* >>> Could need a ioctl which gives SCSI numbers */;

	return (0);
}


/** Tells wether a text is a persistent address as listed by the enumeration
    functions.
*/

#ifndef NIX

int sg_is_enumerable_adr(char* path)
{
	int ret;
	int bus_no, target_no, lun_no;
	struct stat stbuf;

	if (strncmp("/dev/rdsk/", path, 10) != 0)
		return 0;
	ret = decode_btl_solaris(path + 10, &bus_no, &target_no, &lun_no, 0);
	if (ret <= 0)
		return 0;
	if (stat(path, &stbuf) == -1)
		return 0;
	return 1;
}

#else /* ! NIX */

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
#endif /* NIX */




/* Return 1 if the given path leads to a regular file or a device that can be
   seeked, read, and possibly written with 2 kB granularity. 
*/
int burn_os_is_2k_seekrw(char *path, int flag)
{
	struct stat stbuf;

	if (stat(path, &stbuf) == -1)
		return 0;
	if (S_ISREG(stbuf.st_mode))
		return 1;
	if (S_ISBLK(stbuf.st_mode))
		return 1;
	return 0;
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
	int ret;

#ifdef Libburn_os_has_statvfS
	struct statvfs vfsbuf;
#endif

	char *testpath = NULL, *cpt;
	off_t add_size = 0;

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

