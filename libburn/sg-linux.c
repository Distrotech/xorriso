/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2011 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/



/* <<< ts A91112 : experiments to get better speed with USB
#define Libburn_sgio_as_growisofS 1
*/


/*

This is the main operating system dependent SCSI part of libburn. It implements
the transport level aspects of SCSI control and command i/o.

Present implementation: GNU/Linux SCSI Generic (sg)


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
                        that can be used in 2 kB granularity by lseek(2) and
                        read(2), and possibly write(2) if not read-only.
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

Hint: You should also look into sg-freebsd-port.c, which is a younger and
      in some aspects more straightforward implementation of this interface.

*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif


/** PORTING : ------- OS dependent headers and definitions ------ */


#ifdef Libburn_read_o_direcT
# ifndef _GNU_SOURCE
#  define _GNU_SOURCE
# endif
#endif /* Libburn_read_o_direcT */

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/poll.h>
#include <linux/hdreg.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <scsi/scsi.h>
#include <sys/statvfs.h>

/* for ioctl(BLKGETSIZE) */
#include <linux/fs.h>

/* for mmap() */
#include <sys/mman.h>


#include <scsi/sg.h>
/* Values within sg_io_hdr_t indicating success after ioctl(SG_IO) : */
/* .host_status : from http://tldp.org/HOWTO/SCSI-Generic-HOWTO/x291.html */
#define Libburn_sg_host_oK 0
/* .driver_status : from http://tldp.org/HOWTO/SCSI-Generic-HOWTO/x322.html */
#define Libburn_sg_driver_oK 0


/* ts A61211 : to eventually recognize CD devices on /dev/sr* */
#include <limits.h>
#include <linux/cdrom.h>


/** Indication of the Linux kernel this software is running on */
/* -1 = not evaluated , 0 = unrecognizable , 1 = 2.4 , 2 = 2.6 */
static int sg_kernel_age = -1;


/** PORTING : Device file families for bus scanning and drive access.
    Both device families must support the following ioctls:
      SG_IO,
      SG_GET_SCSI_ID
      SCSI_IOCTL_GET_BUS_NUMBER
      SCSI_IOCTL_GET_IDLUN
    as well as mutual exclusively locking with open(O_EXCL).
    If a device family is left empty, then it will not be used.

    To avoid misunderstandings: both families are used via identical
    transport methods as soon as a device file is accepted as CD drive
    by the family specific function <family>_enumerate().
    One difference remains throughout usage: Host,Channel,Id,Lun and Bus
    address parameters of ATA devices are considered invalid.
*/

/* Set this to 1 in order to get on stderr messages from sg_enumerate() */
static int linux_sg_enumerate_debug = 0;


/* The device file family to use for (emulated) generic SCSI transport.
   This must be a printf formatter with one single placeholder for int
   in the range of 0 to 31 . The resulting addresses must provide SCSI
   address parameters Host, Channel, Id, Lun and also Bus.
   E.g.: "/dev/sg%d"
   sr%d is supposed to map only CD-ROM style devices. Additionally a test
   with ioctl(CDROM_DRIVE_STATUS) is made to assert that it is such a drive,
   If no such assertion is made, then this adapter performs INQUIRE and
   looks for first reply byte 0x05.

   This initial setting may be overridden in sg_select_device_family() by 
   settings made via burn_preset_device_open().
*/
static char linux_sg_device_family[80] = {"/dev/sg%d"};

/* Set this to 1 if you want the default linux_sg_device_family chosen
   depending on kernel release: sg for <2.6 , sr for >=2.6
*/
static int linux_sg_auto_family = 1;


/* Set this to 1 in order to accept any TYPE_* (see scsi/scsi.h) */
/* But try with 0 first. There is hope via CDROM_DRIVE_STATUS. */
/* !!! DO NOT SET TO 1 UNLESS YOU PROTECTED ALL INDISPENSIBLE DEVICES
       chmod -rw !!! */
static int linux_sg_accept_any_type = 0;


/* The device file family to use for SCSI transport over ATA.
   This must be a printf formatter with one single placeholder for a
   _single_ char in the range of 'a' to 'z'. This placeholder _must_ be
   at the end of the formatter string.
   E.g. "/dev/hd%c"
*/
static char linux_ata_device_family[80] = {"/dev/hd%c"};

/* Set this to 1 in order to get on stderr messages from ata_enumerate()
*/
static int linux_ata_enumerate_verbous = 0;


/* The waiting time before eventually retrying a failed SCSI command.
   Before each retry wait Libburn_sg_linux_retry_incR longer than with
   the previous one.
*/
#define Libburn_sg_linux_retry_usleeP 100000
#define Libburn_sg_linux_retry_incR   100000


/** PORTING : ------ libburn portable headers and definitions ----- */

#include "libburn.h"
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

/* ts A51221 */
int burn_drive_is_banned(char *device_address);


/* ------------------------------------------------------------------------ */
/* PORTING:   Private definitions. Port only if needed by public functions. */
/*            (Public functions are listed below)                           */
/* ------------------------------------------------------------------------ */


static void enumerate_common(char *fname, int fd_in, int bus_no, int host_no,
			     int channel_no, int target_no, int lun_no);

static int sg_obtain_scsi_adr_fd(char *path, int fd_in,
				 int *bus_no, int *host_no, int *channel_no,
				 int *target_no, int *lun_no);


/* ts A60813 : storage objects are in libburn/init.c
   whether to use O_EXCL with open(2) of devices
   whether to use fcntl(,F_SETLK,) after open(2) of devices
   what device family to use : 0=default, 1=sr, 2=scd, (3=st), 4=sg
   whether to use O_NOBLOCK with open(2) on devices
   whether to take O_EXCL rejection as fatal error
*/
extern int burn_sg_open_o_excl;
extern int burn_sg_fcntl_f_setlk;
extern int burn_sg_use_family;
extern int burn_sg_open_o_nonblock;
extern int burn_sg_open_abort_busy;

/* ts A91111 :
   whether to log SCSI commands:
   bit0= log in /tmp/libburn_sg_command_log
   bit1= log to stderr
   bit2= flush every line
*/
extern int burn_sg_log_scsi;

/* ts A60821
   debug: for tracing calls which might use open drive fds
          or for catching SCSI usage of emulated drives. */
int mmc_function_spy(struct burn_drive *d, char * text);


/* ------------------------------------------------------------------------ */
/* PORTING:   Private functions. Port only if needed by public functions    */
/*            (Public functions are listed below)                           */
/* ------------------------------------------------------------------------ */

/* ts A70413 */
/* This finds out wether the software is running on kernel >= 2.6
*/
static void sg_evaluate_kernel(void)
{
	struct utsname buf;
	if (sg_kernel_age >= 0)
		return;

	sg_kernel_age = 0;
	if (uname(&buf) == -1)
		return;
	sg_kernel_age = 1;
	if (strcmp(buf.release, "2.6") >= 0)
		sg_kernel_age = 2;
}


/* ts A70314 */
/* This installs the device file family if one was chosen explicitely
   by burn_preset_device_open()
*/
static void sg_select_device_family(void)
{

	/* >>> ??? do we need a mutex here ? */
	/* >>> (It might be concurrent but is supposed to have always
	        the same effect. Any race condition should be harmless.) */

	if (burn_sg_use_family == 1)
		strcpy(linux_sg_device_family, "/dev/sr%d");
	else if (burn_sg_use_family == 2)
		strcpy(linux_sg_device_family, "/dev/scd%d");
	else if (burn_sg_use_family == 3)
		strcpy(linux_sg_device_family, "/dev/st%d");
	else if (burn_sg_use_family == 4)
		strcpy(linux_sg_device_family, "/dev/sg%d");
	else if (linux_sg_auto_family) {
		sg_evaluate_kernel();
		if (sg_kernel_age >= 2)
			strcpy(linux_sg_device_family, "/dev/sr%d");
		else
			strcpy(linux_sg_device_family, "/dev/sg%d");
		linux_sg_auto_family = 0;
	}
}


/* ts A80701 */
/* This cares for the case that no /dev/srNN but only /dev/scdNN exists.
   A theoretical case which has its complement in SuSE 10.2 having
   /dev/sr but not /dev/scd.
*/
static int sg_exchange_scd_for_sr(char *fname, int flag)
{
	struct stat stbuf;
	char scd[17], *msg = NULL;

	if (burn_sg_use_family != 0 || strncmp(fname, "/dev/sr", 7)!=0 ||
	    strlen(fname)>9 || strlen(fname)<8)
		return 2;
	if (fname[7] < '0' || fname[7] > '9')
		return 2;
	if (fname [8] != 0 && (fname[7] < '0' || fname[7] > '9'))
		return 2;
	if (stat(fname, &stbuf) != -1)
		return 2;
	strcpy(scd, "/dev/scd");
	strcpy(scd + 8, fname + 7);
	if (stat(scd, &stbuf) == -1)
		return 2;
	msg = calloc(strlen(scd) + strlen(fname) + 80, 1);
	if (msg != NULL) {
		sprintf(msg, "%s substitutes for non-existent %s", scd, fname);
		libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
		free(msg);
	}
	strcpy(fname, scd);
	return 1;
}


/* ts B11110 */
/* This is an early stage version of scsi_log_cmd.
   >>> It will become obsolete when the /tmp file handler is moved into
   >>> scsi_log_command().
*/
static int sgio_log_cmd(unsigned char *cmd, int cmd_len, FILE *fp_in, int flag)
{
	FILE *fp = fp_in;
	int ret;

	/* >>> ts B11110 : move this into scsi_log_command() */
	if (fp == NULL && (burn_sg_log_scsi & 1)) {
		fp= fopen("/tmp/libburn_sg_command_log", "a");
		fprintf(fp, "\n=========================================\n");
	}

	ret = scsi_log_command(cmd, cmd_len, NO_TRANSFER, NULL, 0, fp, flag);
	if (fp_in == NULL && fp != NULL)
		fclose(fp);
	return ret;
}


/* ts B11110 */
static int sgio_log_reply(unsigned char *opcode, int data_dir,
                          unsigned char *data, int dxfer_len,
                          void *fp_in, unsigned char sense[18],
                          int sense_len, int duration, int flag)
{
	int ret;

	ret = scsi_log_reply(opcode, data_dir, data, dxfer_len, fp_in,
	                     sense, sense_len, duration, flag);
	return ret;
}


static int sgio_test(int fd)
{
	unsigned char test_ops[] = { 0, 0, 0, 0, 0, 0 };
	sg_io_hdr_t s;
	int ret;

	memset(&s, 0, sizeof(sg_io_hdr_t));
	s.interface_id = 'S';
	s.dxfer_direction = SG_DXFER_NONE;
	s.cmd_len = 6;
	s.cmdp = test_ops;
	s.timeout = 12345;

	sgio_log_cmd(s.cmdp, s.cmd_len, NULL, 0);

	ret= ioctl(fd, SG_IO, &s);

	sgio_log_reply(s.cmdp, NO_TRANSFER, NULL, 0,
                       NULL, s.sbp, s.sb_len_wr, s.duration, 0);
	return ret;
}


static int sgio_inquiry_cd_drive(int fd, char *fname)
{
	unsigned char test_ops[] = { 0x12, 0, 0, 0, 36, 0 };
	sg_io_hdr_t s;
	struct buffer *buf = NULL;
	unsigned char *sense = NULL;
	char *msg = NULL, *msg_pt;
	int ret = 0, i;

	BURN_ALLOC_MEM(buf, struct buffer, 1);
	BURN_ALLOC_MEM(sense, unsigned char, 128);
	BURN_ALLOC_MEM(msg, char, strlen(fname) + 1024);

	memset(&s, 0, sizeof(sg_io_hdr_t));
	s.interface_id = 'S';
	s.dxfer_direction = SG_DXFER_FROM_DEV;
	s.cmd_len = 6;
	s.cmdp = test_ops;
	s.mx_sb_len = 32;
	s.sbp = sense;
	s.timeout = 30000;
	s.dxferp = buf;
	s.dxfer_len = 36;
	s.usr_ptr = NULL;

	sgio_log_cmd(s.cmdp, s.cmd_len, NULL, 0);

	ret = ioctl(fd, SG_IO, &s);
	if (ret == -1) {
		sprintf(msg,
			 "INQUIRY on '%s' : ioctl(SG_IO) failed , errno= %d",
			 fname, errno);
		libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
		goto ex;
	}

	sgio_log_reply(s.cmdp, FROM_DRIVE, buf->data, s.dxfer_len,
                       NULL, s.sbp, s.sb_len_wr, s.duration, 0);

	if (s.sb_len_wr > 0 || s.host_status != Libburn_sg_host_oK ||
	    s.driver_status != Libburn_sg_driver_oK) {
		sprintf(msg, "INQUIRY failed on '%s' : host_status= %hd , driver_status= %hd", fname, s.host_status, s.driver_status);
		if (s.sb_len_wr > 0) {
			sprintf(msg + strlen(msg), " , sense data=");
			msg_pt = msg + strlen(msg);
			for (i = 0 ; i < s.sb_len_wr; i++)
				sprintf(msg_pt + i * 3, " %2.2X", s.sbp[i]);
		}
		libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
		ret = -1;
		goto ex;
	}
	ret = 0;
	if (buf->data[0] == 0x5) {
		/* Peripheral qualifier 0, device type 0x5 = CD/DVD device.
		   SPC-3 tables 82 and 83  */
		ret = 1;
	} else {
		sprintf(msg, "INQUIRY on '%s' : byte 0 = 0x%2.2X",
			 fname, buf->data[0]);
		libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
			LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
	}

ex:;
	BURN_FREE_MEM(msg);
	BURN_FREE_MEM(sense);
	BURN_FREE_MEM(buf);
	return ret;
}


/* ts A60924 */
static int sg_handle_busy_device(char *fname, int os_errno)
{
	char *msg = NULL;
	struct stat stbuf;
	int looks_like_hd= 0, fd, ret;

	BURN_ALLOC_MEM(msg, char, 4096);

	/* ts A80713 :
	   check existence of /dev/hdX1 as hint for hard disk rather than CD
	   Hint by Giulio Orsero: check /proc/ide/hdX/media for "disk"
	*/
	if (strncmp(fname, "/dev/hd", 7)==0) {
		sprintf(msg, "%s1", fname);
		if (stat(msg, &stbuf) != -1)
			looks_like_hd= 1;
		sprintf(msg, "/proc/ide/hd%c/media", fname[7]);
		fd = open(msg, O_RDONLY);
		if (fd != -1) {
			ret = read(fd, msg, 10);
			if (ret < 0)
				ret = 0;
			msg[ret]= 0;
			close(fd);
			if (strncmp(msg, "disk\n", 5) == 0 ||
			    strcmp(msg, "disk") == 0)
				looks_like_hd= 2;
			else if (strncmp(msg, "cdrom\n", 6) == 0 ||
			         strcmp(msg, "cdrom") == 0)
				looks_like_hd= 0;
		}
	}

	/* ts A60814 : i saw no way to do this more nicely */ 
	if (burn_sg_open_abort_busy) {
		fprintf(stderr,
	"\nlibburn: FATAL : Application triggered abort on busy device '%s'\n",
			fname);

		/* ts A61007 */
		abort();
		/* a ssert("drive busy" == "non fatal"); */
	}

	/* ts A60924 : now reporting to libdax_msgs */
	if (looks_like_hd == 2) { /* is surely hard disk */
		;
	} else if (looks_like_hd) {
		sprintf(msg, "Could not examine busy device '%s'", fname);
		libdax_msgs_submit(libdax_messenger, -1, 0x0002015a,
				LIBDAX_MSGS_SEV_NOTE, LIBDAX_MSGS_PRIO_LOW,
				msg, os_errno, 0);
		sprintf(msg,
	"Busy '%s' seems to be a hard disk, as '%s1' exists. But better check.",
				fname, fname);
		libdax_msgs_submit(libdax_messenger, -1, 0x0002015b,
				LIBDAX_MSGS_SEV_HINT, LIBDAX_MSGS_PRIO_LOW,
				msg, 0, 0);

	} else {
		sprintf(msg, "Cannot open busy device '%s'", fname);
		libdax_msgs_submit(libdax_messenger, -1, 0x00020001,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_LOW,
				msg, os_errno, 0);
	}
	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	return ret;
}


/* ts A60925 : ticket 74 */
static int sg_close_drive_fd(char *fname, int driveno, int *fd, int sorry)
{
	int ret, os_errno, sevno= LIBDAX_MSGS_SEV_DEBUG;
	char *msg = NULL;

	if(*fd < 0)
		{ret = 0; goto ex;}
	BURN_ALLOC_MEM(msg, char, 4096 + 100);

#ifdef CDROM_MEDIA_CHANGED_disabled_because_not_helpful
#ifdef CDSL_CURRENT
	/* ts A80217 : wondering whether the os knows about our activities */
	ret = ioctl(*fd, CDROM_MEDIA_CHANGED, CDSL_CURRENT);
	sprintf(msg, "ioctl(CDROM_MEDIA_CHANGED) == %d", ret);
	libdax_msgs_submit(libdax_messenger, driveno, 0x00000002,
		LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH, msg, 0, 0);

#ifdef BLKFLSBUF_disabled_because_not_helpful
	ret = ioctl(*fd, BLKFLSBUF, 0);
	sprintf(msg, "ioctl(BLKFLSBUF) == %d", ret);
	os_errno = 0;
	if(ret == -1)
		os_errno = errno;
	libdax_msgs_submit(libdax_messenger, driveno, 0x00000002,
		LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH, msg, os_errno,0);
#endif /* BLKFLSBUF */

#endif /* CDSL_CURRENT */
#endif /* CDROM_MEDIA_CHANGED */

	ret = close(*fd);
	*fd = -1337;
	if(ret != -1) {
		/* ts A70409 : DDLP-B */
		/* >>> release single lock on fname */
		{ret = 1; goto ex;}
	}
	os_errno= errno;

	sprintf(msg, "Encountered error when closing drive '%s'", fname);
	if (sorry)
		sevno = LIBDAX_MSGS_SEV_SORRY;
	libdax_msgs_submit(libdax_messenger, driveno, 0x00020002,
			sevno, LIBDAX_MSGS_PRIO_HIGH, msg, os_errno, 0);
	ret = 0;
ex:;
	BURN_FREE_MEM(msg);
	return ret;	
}


/* ts A70401 : 
   fcntl() has the unappealing property to work only after open().
   So libburn will by default use open(O_EXCL) first and afterwards
   as second assertion will use fcntl(F_SETLK). One lock more should not harm.
*/
static int sg_fcntl_lock(int *fd, char *fd_name, int l_type, int verbous)
{
	struct flock lockthing;
	char msg[81];
	int ret;

	if (!burn_sg_fcntl_f_setlk)
		return 1;

	memset(&lockthing, 0, sizeof(lockthing));
	lockthing.l_type = l_type;
	lockthing.l_whence = SEEK_SET;
	lockthing.l_start = 0;
	lockthing.l_len = 0;
/*
        fprintf(stderr,"LIBBURN_EXPERIMENTAL: fcntl(%d, F_SETLK, %s)\n",
                *fd, l_type == F_WRLCK ? "F_WRLCK" : "F_RDLCK");
*/

	ret = fcntl(*fd, F_SETLK, &lockthing);
	if (ret == -1) {
		if (verbous) {
			sprintf(msg, "Device busy. Failed to fcntl-lock '%s'",
					fd_name);
			libdax_msgs_submit(libdax_messenger, -1, 0x00020008,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				msg, errno, 0);
		}
		close(*fd);
		*fd = -1;

		/* ts A70409 : DDLP-B */
		/* >>> release single lock on fd_name */

		return(0);
	}
	return 1;
}


/* ts A60926 */
/* @param scan_mode  0= open for drivce aquiration
                     1= open for scanning with guessed names
                     2= open for scanning with /proc/sys/dev/cdrom/info names
*/
static int sg_open_drive_fd(char *fname, int scan_mode)
{
	int open_mode = O_RDWR, fd, tries= 0, is_std_adr, report_as_note = 0;
	char msg[81];
	struct stat stbuf;

	/* ts A70409 : DDLP-B */
	/* >>> obtain single lock on fname */

	/* ts A60813 - A60927
	   O_EXCL with devices is a non-POSIX feature
	   of Linux kernels. Possibly introduced 2002.
	   Mentioned in "The Linux SCSI Generic (sg) HOWTO" */
	if(burn_sg_open_o_excl)
		open_mode |= O_EXCL;
	/* ts A60813
	   O_NONBLOCK was already hardcoded in ata_ but not in sg_.
	   There must be some reason for this. So O_NONBLOCK is
	   default mode for both now. Disable on own risk.
           ts B10904: O_NONBLOCK is prescribed by <linux/cdrom.h>
	   ts A70411
           Switched to O_NDELAY for LKML statement 2007/4/11/141 by Alan Cox:
	   "open() has side effects. The CD layer allows you to open
            with O_NDELAY if you want to avoid them." 
	*/
	if(burn_sg_open_o_nonblock)
		open_mode |= O_NDELAY;

/* <<< debugging
	fprintf(stderr,
		"\nlibburn: experimental: o_excl= %d , o_nonblock= %d, abort_on_busy= %d\n",
	burn_sg_open_o_excl,burn_sg_open_o_nonblock,burn_sg_open_abort_busy);
	fprintf(stderr,
		"libburn: experimental: O_EXCL= %d , O_NDELAY= %d\n",
		!!(open_mode&O_EXCL),!!(open_mode&O_NDELAY));
*/

try_open:;
	fd = open(fname, open_mode);
	if (fd == -1) {
/* <<< debugging
		fprintf(stderr,
		"\nlibburn: experimental: fname= %s , errno= %d\n",
			fname,errno);
*/
		if (errno == EBUSY) {
			tries++;

/* <<< debugging
			fprintf(stderr,
				"\nlibburn_DEBUG: EBUSY , tries= %d\n", tries);
*/

			if (tries < 4) {
				usleep(2000000);
				goto try_open;
			}
			sg_handle_busy_device(fname, errno);
			return -1;
			
		}
		sprintf(msg, "Failed to open device '%s'",fname);
		if (scan_mode) {
			is_std_adr = (strncmp(fname, "/dev/sr", 7) == 0 ||
			              strncmp(fname, "/dev/scd", 8) == 0);
			if(scan_mode == 1 && is_std_adr &&
			   stat(fname, &stbuf) != -1)
				report_as_note = 1;
			else if(scan_mode == 2 && (!is_std_adr) &&
			        stat(fname, &stbuf) != -1)
				report_as_note = 1;
			if (report_as_note)
				libdax_msgs_submit(libdax_messenger, -1,
				   0x0002000e,
				   LIBDAX_MSGS_SEV_NOTE, LIBDAX_MSGS_PRIO_HIGH,
				   msg, errno, 0);
		} else {
			libdax_msgs_submit(libdax_messenger, -1, 0x00020005,
				LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				msg, errno, 0);
		}
		return -1;
	}
	sg_fcntl_lock(&fd, fname, F_WRLCK, 1);
	return fd;
}


/* ts A60926 */
static int sg_release_siblings(int sibling_fds[],
				char sibling_fnames[][BURN_OS_SG_MAX_NAMELEN],
				int *sibling_count)
{
	int i;
	char msg[81];

	for(i= 0; i < *sibling_count; i++)
		sg_close_drive_fd(sibling_fnames[i], -1, &(sibling_fds[i]), 0);
	if(*sibling_count > 0) {
		sprintf(msg, "Closed %d O_EXCL scsi siblings", *sibling_count);
		libdax_msgs_submit(libdax_messenger, -1, 0x00020007,
			LIBDAX_MSGS_SEV_NOTE, LIBDAX_MSGS_PRIO_HIGH, msg, 0,0);
	}
	*sibling_count = 0;
	return 1;
}


/* ts A60926 */
static int sg_close_drive(struct burn_drive *d)
{
	int ret;

	if (!burn_drive_is_open(d))
		return 0;
	sg_release_siblings(d->sibling_fds, d->sibling_fnames,
				&(d->sibling_count));
	ret = sg_close_drive_fd(d->devname, d->global_index, &(d->fd), 0);
	return ret;
}


/* ts A60926 */
static int sg_open_scsi_siblings(char *path, int driveno,
			int sibling_fds[],
			char sibling_fnames[][BURN_OS_SG_MAX_NAMELEN],
			int *sibling_count,
			int host_no, int channel_no, int id_no, int lun_no)
{
	int tld, i, ret, fd, i_bus_no = -1;
	int i_host_no = -1, i_channel_no = -1, i_target_no = -1, i_lun_no = -1;
	char *msg = NULL, fname[40];
	struct stat stbuf;
	dev_t last_rdev = 0, path_rdev;

	static char tldev[][20]= {"/dev/sr%d", "/dev/scd%d", "/dev/sg%d", ""};
					/* ts A70609: removed "/dev/st%d" */

	if (strlen(path) > BURN_MSGS_MESSAGE_LEN - 160)
		{ret = 0; goto ex;}

	BURN_ALLOC_MEM(msg, char, BURN_MSGS_MESSAGE_LEN);

	if(stat(path, &stbuf) == -1)
		{ret = 0; goto ex;}
	path_rdev = stbuf.st_rdev;

        sg_select_device_family();
	if (linux_sg_device_family[0] == 0)
		{ret = 1; goto ex;}

	if(host_no < 0 || id_no < 0 || channel_no < 0 || lun_no < 0)
		{ret = 2; goto ex;}
	if(*sibling_count > 0)
		sg_release_siblings(sibling_fds, sibling_fnames,
					sibling_count);
		
	for (tld = 0; tldev[tld][0] != 0; tld++) {
		if (strcmp(tldev[tld], linux_sg_device_family)==0)
	continue;
		for (i = 0; i < 32; i++) {
			sprintf(fname, tldev[tld], i);
			if(stat(fname, &stbuf) == -1)
		continue;
			if (path_rdev == stbuf.st_rdev)
		continue;
			if (*sibling_count > 0 && last_rdev == stbuf.st_rdev)
		continue;
			ret = sg_obtain_scsi_adr(fname, &i_bus_no, &i_host_no,
				&i_channel_no, &i_target_no, &i_lun_no);
			if (ret <= 0)
		continue;
			if (i_host_no != host_no || i_channel_no != channel_no)
		continue;
			if (i_target_no != id_no || i_lun_no != lun_no)
		continue;

			fd = sg_open_drive_fd(fname, 0);
			if (fd < 0)
				goto failed;

			if (*sibling_count>=BURN_OS_SG_MAX_SIBLINGS) {
				sprintf(msg, "Too many scsi siblings of '%s'",
					path);
				libdax_msgs_submit(libdax_messenger,
					driveno, 0x00020006,
					LIBDAX_MSGS_SEV_FATAL,
					LIBDAX_MSGS_PRIO_HIGH, msg, 0, 0);
				goto failed;
			}
			sprintf(msg, "Opened O_EXCL scsi sibling '%s' of '%s'",
				fname, path);
			libdax_msgs_submit(libdax_messenger, driveno,
				0x00020004,
				LIBDAX_MSGS_SEV_NOTE, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
			sibling_fds[*sibling_count] = fd;
			strcpy(sibling_fnames[*sibling_count], fname);
			(*sibling_count)++;
			last_rdev= stbuf.st_rdev;
		}
	}
	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	return ret;
failed:;
	sg_release_siblings(sibling_fds, sibling_fnames, sibling_count);
	ret = 0;
	goto ex;
}


/* ts A80731 */
static int is_ata_drive(char *fname, int fd_in)
{
	int fd;
	struct hd_driveid tm;

	if (fd_in >= 0)
		fd = fd_in;
	else
		fd = sg_open_drive_fd(fname, 1);
	if (fd == -1) {
		if (linux_ata_enumerate_verbous)
			fprintf(stderr,"open failed, errno=%d  '%s'\n",
				errno, strerror(errno));
		return 0;
	}

	memset(&tm, 0, sizeof(tm));
	ioctl(fd, HDIO_GET_IDENTITY, &tm);

		/* not atapi */
	if (!(tm.config & 0x8000) || (tm.config & 0x4000)) {
		if (linux_ata_enumerate_verbous)
			fprintf(stderr, "not marked as ATAPI\n");
		if (fd_in < 0)
			sg_close_drive_fd(fname, -1, &fd, 0);
		return 0;
	}

	/* if SG_IO fails on an atapi device, we should stop trying to 
	   use hd* devices */
	if (sgio_test(fd) == -1) {
		if (linux_ata_enumerate_verbous)
		  fprintf(stderr,
			 "FATAL: sgio_test() failed: errno=%d  '%s'\n",
			 errno, strerror(errno));
		if (fd_in < 0)
			sg_close_drive_fd(fname, -1, &fd, 0);
		return 0;
	}
	if (fd_in >= 0)
		return 1;
	if (sg_close_drive_fd(fname, -1, &fd, 1) <= 0) {
		if (linux_ata_enumerate_verbous)
			fprintf(stderr,
				"cannot close properly, errno=%d  '%s'\n",
				errno, strerror(errno));
		return 0;
	}
	return 1;
}


static int is_scsi_drive(char *fname, int fd_in, int *bus_no, int *host_no,
			 int *channel_no, int *target_no, int *lun_no)
{
	int fd = -1, sid_ret = 0, ret, fail_sev_sorry = 0;
	struct sg_scsi_id sid;
	int *sibling_fds = NULL, sibling_count= 0;
	typedef char burn_sg_sibling_fname[BURN_OS_SG_MAX_NAMELEN];
	burn_sg_sibling_fname *sibling_fnames = NULL;

	BURN_ALLOC_MEM(sibling_fds, int, BURN_OS_SG_MAX_SIBLINGS);
	BURN_ALLOC_MEM(sibling_fnames, burn_sg_sibling_fname,
			BURN_OS_SG_MAX_SIBLINGS);

	if (fd_in >= 0)
		fd = fd_in;
	else
		fd = sg_open_drive_fd(fname, 1);
	if (fd == -1) {
		if (linux_sg_enumerate_debug)
			fprintf(stderr, "open failed, errno=%d  '%s'\n",
				errno, strerror(errno));
		{ret = 0; goto ex;}
	}
	sid_ret = ioctl(fd, SG_GET_SCSI_ID, &sid);
	if (sid_ret == -1) {
		sid.scsi_id = -1; /* mark SCSI address as invalid */
		if(linux_sg_enumerate_debug) 
			fprintf(stderr,
			"ioctl(SG_GET_SCSI_ID) failed, errno=%d  '%s' , ",
			errno, strerror(errno));

		if (sgio_test(fd) == -1) {
			if (linux_sg_enumerate_debug)
		  		fprintf(stderr,
				 "FATAL: sgio_test() failed: errno=%d  '%s'",
				errno, strerror(errno));

			{ret = 0; goto ex;}
		}

#ifdef CDROM_DRIVE_STATUS
		/* http://developer.osdl.org/dev/robustmutexes/
			  src/fusyn.hg/Documentation/ioctl/cdrom.txt */
		sid_ret = ioctl(fd, CDROM_DRIVE_STATUS, 0);
		if(linux_sg_enumerate_debug)
			  fprintf(stderr,
				"ioctl(CDROM_DRIVE_STATUS) = %d , ",
				sid_ret);
		if (sid_ret != -1 && sid_ret != CDS_NO_INFO)
			sid.scsi_type = TYPE_ROM;
		else
			sid_ret = -1;
#endif /* CDROM_DRIVE_STATUS */

	}

	if (sid_ret == -1) {
		/* ts B11109 : Try device type from INQUIRY byte 0 */
		if (sgio_inquiry_cd_drive(fd, fname) == 1) {
			sid_ret = 0;
			sid.scsi_type = TYPE_ROM;
		}
	}


#ifdef SCSI_IOCTL_GET_BUS_NUMBER
	/* Hearsay A61005 */
	if (ioctl(fd, SCSI_IOCTL_GET_BUS_NUMBER, bus_no) == -1)
		*bus_no = -1;
#endif

	fail_sev_sorry = (sid.scsi_type == TYPE_ROM);
	if ( (sid_ret == -1 || sid.scsi_type != TYPE_ROM)
	     && !linux_sg_accept_any_type) {
		if (linux_sg_enumerate_debug)
			fprintf(stderr, "sid.scsi_type = %d (!= TYPE_ROM)\n",
				sid.scsi_type); 
		{ret = 0; goto ex;}
	}

	if (sid_ret == -1 || sid.scsi_id < 0) {
		/* ts A61211 : employ a more general ioctl */
		/* ts B11001 : re-use fd */
		ret = sg_obtain_scsi_adr_fd(fname, fd, bus_no, host_no,
					   channel_no, target_no, lun_no);
		if (ret>0) {
			sid.host_no = *host_no;
			sid.channel = *channel_no;
			sid.scsi_id = *target_no;
			sid.lun = *lun_no;
		} else {
			if (linux_sg_enumerate_debug)
				fprintf(stderr,
					"sg_obtain_scsi_adr_fd() failed\n");
			{ret = 0; goto ex;}
		}
	}

	/* ts A60927 : trying to do locking with growisofs */
	if(burn_sg_open_o_excl>1) {
		ret = sg_open_scsi_siblings(
				fname, -1, sibling_fds, sibling_fnames,
				&sibling_count,
				sid.host_no, sid.channel,
				sid.scsi_id, sid.lun);
		if (ret<=0) {
			if (linux_sg_enumerate_debug)
				fprintf(stderr, "cannot lock siblings\n"); 
			sg_handle_busy_device(fname, 0);
			{ret = 0; goto ex;}
		}
		/* the final occupation will be done in sg_grab() */
		sg_release_siblings(sibling_fds, sibling_fnames,
							&sibling_count);
	}
#ifdef SCSI_IOCTL_GET_BUS_NUMBER
	if(*bus_no == -1)
		*bus_no = 1000 * (sid.host_no + 1) + sid.channel;
#else
	*bus_no = sid.host_no;
#endif
	*host_no= sid.host_no;
	*channel_no= sid.channel;
	*target_no= sid.scsi_id;
	*lun_no= sid.lun;
	ret = 1;
ex:;
	if (fd_in < 0 && fd >= 0) {
		if (sg_close_drive_fd(fname, -1, &fd, fail_sev_sorry) <= 0) {
			if (linux_sg_enumerate_debug)
				fprintf(stderr,
				"cannot close properly, errno=%d  '%s'\n",
				errno, strerror(errno)); 
			if (ret > 0)
				ret = 0;
		}
	}
	BURN_FREE_MEM(sibling_fds);
	BURN_FREE_MEM(sibling_fnames);
	return ret;
}	


/* @param flag bit0= do not complain about failure to open /dev/sr /dev/scd */
static int sg_open_for_enumeration(char *fname, int flag)
{
	int fd;

	fd = sg_open_drive_fd(fname, 1 + (flag & 1));
	if (fd < 0) {
		if (linux_sg_enumerate_debug || linux_ata_enumerate_verbous)
			fprintf(stderr, "open failed, errno=%d  '%s'\n",
				errno, strerror(errno));
		return -1;
	}
	return fd;
}


/** Speciality of GNU/Linux: detect non-SCSI ATAPI (EIDE) which will from
   then on used used via generic SCSI as is done with (emulated) SCSI drives */ 
static void ata_enumerate(void)
{
	int ret, i, fd = -1;
	char fname[10];

	if (linux_ata_enumerate_verbous)
	  fprintf(stderr, "libburn_debug: linux_ata_device_family = %s\n",
		  linux_ata_device_family);

	if (linux_ata_device_family[0] == 0)
		return;

	for (i = 0; i < 26; i++) {
		sprintf(fname, linux_ata_device_family, 'a' + i);
		if (linux_ata_enumerate_verbous)
		  fprintf(stderr, "libburn_debug: %s : ", fname);

		/* ts A51221 */
		if (burn_drive_is_banned(fname)) {
			if (linux_ata_enumerate_verbous)
				fprintf(stderr, "not in whitelist\n");
	continue;
		}
		fd = sg_open_for_enumeration(fname, 0);
		if (fd < 0)
	continue;
		ret = is_ata_drive(fname, fd);
		if (ret < 0)
	break;
		if (ret == 0)
	continue;
		if (linux_ata_enumerate_verbous)
		  fprintf(stderr, "accepting as drive without SCSI address\n");
		enumerate_common(fname, fd, -1, -1, -1, -1, -1);
	}
}


/** Detects (probably emulated) SCSI drives */
static void sg_enumerate(void)
{
	int i, ret, fd = -1;
	int bus_no= -1, host_no= -1, channel_no= -1, target_no= -1, lun_no= -1;
	char fname[17];

        sg_select_device_family();

	if (linux_sg_enumerate_debug)
	  fprintf(stderr, "libburn_debug: linux_sg_device_family = %s\n",
		  linux_sg_device_family);

	if (linux_sg_device_family[0] == 0)
		return;

	for (i = 0; i < 32; i++) {
		sprintf(fname, linux_sg_device_family, i);

		/* ts A80702 */
		sg_exchange_scd_for_sr(fname, 0);

		if (linux_sg_enumerate_debug)
		  fprintf(stderr, "libburn_debug: %s : ", fname);

		/* ts A51221 */
		if (burn_drive_is_banned(fname)) {
			if (linux_sg_enumerate_debug)
			  fprintf(stderr, "not in whitelist\n"); 
	continue;
		}
		fd = sg_open_for_enumeration(fname, 0);
		if (fd < 0)
	continue;

		ret = is_scsi_drive(fname, fd, &bus_no, &host_no, &channel_no,
							&target_no, &lun_no);
		if (ret < 0)
	break;
		if (ret == 0)
	continue;
		if (linux_sg_enumerate_debug)
		  fprintf(stderr, "accepting as SCSI %d,%d,%d,%d bus=%d\n",
			  host_no, channel_no, target_no, lun_no, bus_no);
		enumerate_common(fname, fd, bus_no, host_no, channel_no, 
				target_no, lun_no);

	}
}



/* ts A80805 : eventually produce the other official name of a device file */
static int fname_other_name(char *fname, char other_name[80], int flag)
{
	if(strncmp(fname, "/dev/sr", 7) == 0 &&
	   (fname[7] >= '0' && fname[7] <= '9') &&
           (fname[8] == 0 ||
	    (fname[8] >= '0' && fname[8] <= '9' && fname[9] == 0))) {
		sprintf(other_name, "/dev/scd%s", fname + 7);
		return 1;
	}
	if(strncmp(fname, "/dev/scd", 8) == 0 &&
	   (fname[8] >= '0' && fname[8] <= '9') &&
           (fname[9] == 0 ||
	    (fname[9] >= '0' && fname[9] <= '9' && fname[10] == 0))) {
		sprintf(other_name, "/dev/sr%s", fname + 8);
		return 1;
	}
	return 0;
}


/* ts A80805 */
static int fname_drive_is_listed(char *fname, int flag)
{
	char other_fname[80];

	if (burn_drive_is_listed(fname, NULL, 0))
		return 1;
	if (fname_other_name(fname, other_fname, 0) > 0)
		if (burn_drive_is_listed(other_fname, NULL, 0))
			return 2;
	return 0;
}


/* ts A80731 : Directly open the given address.
   @param flag bit0= do not complain about missing file
               bit1= do not check whether drive is already listed
               bit2= do not complain about failure to open /dev/sr /dev/scd
*/
static int fname_enumerate(char *fname, int flag)
{
	int is_ata= 0, is_scsi= 0, ret, fd = -1;
	int bus_no= -1, host_no= -1, channel_no= -1, target_no= -1, lun_no= -1;
	char *msg = NULL;
	struct stat stbuf;

	BURN_ALLOC_MEM(msg, char, BURN_DRIVE_ADR_LEN + 80);

	if (!(flag & 2))
		if (fname_drive_is_listed(fname, 0))
			{ret = 2; goto ex;}
	if (stat(fname, &stbuf) == -1) {
		sprintf(msg, "File object '%s' not found", fname);
		if (!(flag & 1))
			libdax_msgs_submit(libdax_messenger, -1, 0x0002000b,
			  LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			  msg, 0, 0);
		{ret = -1; goto ex;}
	}

	fd = sg_open_for_enumeration(fname, !!(flag & 4));
	if (fd < 0)
		{ret = 0; goto ex;}
	is_ata = is_ata_drive(fname, fd);
	if (is_ata < 0)
		{ret = -1; goto ex;}
	if (!is_ata)
		is_scsi = is_scsi_drive(fname, fd, &bus_no, &host_no,
					&channel_no, &target_no, &lun_no);
	if (is_scsi < 0)
		{ret = -1; goto ex;}
	if (is_ata == 0 && is_scsi == 0)
		{ret = 0; goto ex;}

	if (linux_sg_enumerate_debug)
		  fprintf(stderr,
			"(single) accepting as SCSI %d,%d,%d,%d bus=%d\n",
			host_no, channel_no, target_no, lun_no, bus_no);

	enumerate_common(fname, fd, bus_no, host_no, channel_no, 
				target_no, lun_no);
	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	return ret;
}


/* ts A80731 : Directly open the given address from a single-item whitlist */
static int single_enumerate(int flag)
{
	int ret, wl_count;
	char *fname, *msg = NULL;

        wl_count= burn_drive_whitelist_count();
	if (wl_count != 1)
		{ret = 0; goto ex;}
	fname= burn_drive_whitelist_item(0, 0);
	if (fname == NULL)
		{ret = 0; goto ex;}
	ret = fname_enumerate(fname, 2);
	if (ret <= 0) {
		BURN_ALLOC_MEM(msg, char, BURN_DRIVE_ADR_LEN + 80);
		sprintf(msg, "Cannot access '%s' as SG_IO CDROM drive", fname);
		libdax_msgs_submit(libdax_messenger, -1, 0x0002000a,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
			msg, 0, 0);
		ret = -1;
	}
ex:;
	BURN_FREE_MEM(msg);
	return ret;
}


/* ts A80801 : looking up drives listed in /proc/sys/dev/cdrom/info line like:
                drive name:             sr1     hdc     hda     sr0
   @parm flag bit0= release list memory and exit
*/
static int proc_sys_dev_cdrom_info(char ***list, int *count, int flag)
{
	FILE *fp;
	char *line = NULL, *fname = NULL, *cpt, *retpt, *list_data;
	int maxl= 0, pass, i, line_size = 1024, ret;

	BURN_ALLOC_MEM(line, char, line_size);
	BURN_ALLOC_MEM(fname, char, line_size + 5);

	if (*list != NULL) {
		if ((*list)[0] != NULL)
			free((*list)[0]);
		free(*list);
		*list = NULL;
		*count = 0;
	}
	if (flag & 1) 
		{ret = 1; goto ex;}

	*count = 0;
	sg_evaluate_kernel();
	if (sg_kernel_age < 2) /* addresses are not suitable for kernel 2.4 */
		{ret = 1; goto ex;}
	fp = fopen("/proc/sys/dev/cdrom/info", "r");
	if (fp == NULL)
		{ret = 0; goto ex;}
	while (1) {
		retpt = fgets(line, line_size, fp);
		if (retpt == NULL)
	break;
		if(strncmp(line, "drive name:", 11) == 0)
	break;
	}
	fclose(fp);
	if (retpt == NULL)
		{ret = 0; goto ex;}
	strcpy(fname, "/dev/");
	for(pass = 0; pass < 2; pass++) {
		*count = 0;
		cpt = line + 11;
		while (*cpt != 0) {
			for(; *cpt == ' ' || *cpt == '\t'; cpt++);
			if (*cpt == 0 || *cpt == '\n')
		break;
			sscanf(cpt, "%s", fname + 5);
			if ((int) strlen(fname) > maxl)
				maxl = strlen(fname);
			if (pass == 1)
				strcpy((*list)[*count], fname);
			(*count)++;
			for(cpt++; *cpt != ' ' && *cpt != '\t'
					 && *cpt != 0 && *cpt != '\n'; cpt++);
		}
		if (pass == 0) {
			list_data = calloc(*count + 1, maxl+1);
			*list = calloc(*count + 1, sizeof(char *));
			if(list_data == NULL || *list == NULL) {
				libdax_msgs_submit(libdax_messenger, -1,
				0x00000003,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				"Out of virtual memory", 0, 0);
				if (list_data != NULL)
					free(list_data);
				if (*list != NULL)
					free((char *) *list);
				{ret = -1; goto ex;}
			}
			for (i = 0; i <= *count; i++)
				(*list)[i] = list_data + i * (maxl + 1);
		}
	}
	ret = 1;
ex:;
	BURN_FREE_MEM(line);
	BURN_FREE_MEM(fname);
	return ret;
}


static int add_proc_info_drives(int flag)
{
	int ret, list_count, count = 0, i;
	char **list= NULL;

	if (burn_sg_use_family != 0)
		return(1); /* Looking only for sr resp. scd resp. sg */

	ret = proc_sys_dev_cdrom_info(&list, &list_count, 0);
	if (ret <= 0)
		return ret;
	for (i = 0; i < list_count; i++) {
		if (burn_drive_is_banned(list[i]))
	continue;
		ret = fname_enumerate(list[i], 1 | 4);
		if (ret == 1)
			count++;
	}
	proc_sys_dev_cdrom_info(&list, &list_count, 1); /* free memory */
	return 1 + count;
}


/* ts A61115 */
/* ----------------------------------------------------------------------- */
/* PORTING: Private functions which contain publicly needed functionality. */
/*          Their portable part must be performed. So it is probably best  */
/*          to replace the non-portable part and to call these functions   */
/*          in your port, too.                                             */
/* ----------------------------------------------------------------------- */


/** Wraps a detected drive into libburn structures and hands it over to
    libburn drive list.
*/
/* ts A60923 - A61005 : introduced new SCSI parameters */
/* ts A61021 : moved non os-specific code to spc,sbc,mmc,drive */
static void enumerate_common(char *fname, int fd_in, int bus_no, int host_no,
			     int channel_no, int target_no, int lun_no)
{
	int ret, i;
	struct burn_drive out;

	/* General libburn drive setup */
	burn_setup_drive(&out, fname);

	/* This transport adapter uses SCSI-family commands and models
           (seems the adapter would know better than its boss, if ever) */
	ret = burn_scsi_setup_drive(&out, bus_no, host_no, channel_no,
				    target_no, lun_no, 0);
	if (ret<=0)
		return;

	/* PORTING: ------------------- non portable part --------------- */

	/* Operating system adapter is GNU/Linux Generic SCSI (sg) */
	/* Adapter specific handles and data */
	out.fd = -1337;
	out.sibling_count = 0;
	for(i= 0; i<BURN_OS_SG_MAX_SIBLINGS; i++)
		out.sibling_fds[i] = -1337;

	/* PORTING: ---------------- end of non portable part ------------ */

	/* Adapter specific functions with standardized names */
	out.grab = sg_grab;
	out.release = sg_release;
	out.drive_is_open= sg_drive_is_open;
	out.issue_command = sg_issue_command;
	if (fd_in >= 0)
		out.fd = fd_in;

	/* Finally register drive and inquire drive information.
	   out is an invalid copy afterwards. Do not use it for anything.
	 */
	burn_drive_finish_enum(&out);
}


/* ts A61115 */
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
	strcpy(msg, "internal GNU/Linux SG_IO adapter sg-linux");
	return 1;
}


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


/** PORTING:
    In this GNU/Linux implementation, this function mirrors the enumeration
    done in sg_enumerate and ata_enumerate(). It would be better to base those
    functions on this sg_give_next_adr() but the situation is not inviting. 
*/
/* ts A60922 ticket 33 : called from drive.c */
/** Returns the next index number and the next enumerated drive address.
    The enumeration has to cover all available and accessible drives. It is
    allowed to return addresses of drives which are not available but under
    some (even exotic) circumstances could be available. It is on the other
    hand allowed, only to hand out addresses which can really be used right
    in the moment of this call. (This implementation chooses the former.)
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
	/* os-linux.h : typedef int burn_drive_enumerator_t; */
	static int sg_limit = 32, ata_limit = 26;
	int baseno = 0, i;
	char other_name[80];

	if (initialize == -1) {
		proc_sys_dev_cdrom_info(&(idx->info_list), &(idx->info_count),
					1);
		return 0;
	}

        sg_select_device_family();
	if (linux_sg_device_family[0] == 0)
		sg_limit = 0;
	if (linux_ata_device_family[0] == 0)
		ata_limit = 0;

	if (initialize  == 1) {
		idx->pos = -1;
		idx->info_count= 0;
		idx->info_list= NULL;
		proc_sys_dev_cdrom_info(&(idx->info_list), &(idx->info_count),
					0);
	}
	(idx->pos)++;
	if (idx->pos >= sg_limit)
		goto next_ata;
	if (adr_size < 11)
		return -1;
	sprintf(adr, linux_sg_device_family, idx->pos);

	sg_exchange_scd_for_sr(adr, 0);
	goto return_1_pre_proc;

next_ata:;
	baseno += sg_limit;
	if (idx->pos - baseno >= ata_limit)
		goto next_proc_info;
	if (adr_size < 9)
		return -1;
	sprintf(adr, linux_ata_device_family, 'a' + (idx->pos - baseno));
	goto return_1_pre_proc;

next_proc_info:;
	baseno += ata_limit;
	for (i = 0; i < idx->info_count; i++) {
		if ((idx->info_list)[i][0] == 0)
	continue;
		if (baseno == idx->pos) {
			if (adr_size < (int) strlen((idx->info_list)[i]) + 1)
				return -1;
			strcpy(adr, (idx->info_list)[i]);
			return 1;
		}
		baseno++;
	}
	return 0;

return_1_pre_proc:;
	for (i = 0; i < idx->info_count; i++) {
		if (strcmp((idx->info_list)[i], adr) == 0)
			(idx->info_list)[i][0] = 0;
	        if (fname_other_name(adr, other_name, 0) > 0)
			if (strcmp((idx->info_list)[i], other_name) == 0)
				(idx->info_list)[i][0] = 0;
	}
	return 1;
}


/** Brings all available, not-whitelist-banned, and accessible drives into
    libburn's list of drives.
*/
/** PORTING:
    If not stricken with an incompletely unified situation like in GNU/Linux
    one would rather implement this by a loop calling sg_give_next_adr().
    If needed with your sg_give_next_adr() results, do a test for existence
    and accessability. If burn activities are prone to external interference
    on your system it is also necessary to obtain exclusive access locks on
    the drives.
    Hand over each accepted drive to enumerate_common() resp. its replacement
    within your port.

    See FreeBSD port sketch sg-freebsd-port.c for such an implementation.
*/
/* ts A61115: replacing call to sg-implementation internals from drive.c */
int scsi_enumerate_drives(void)
{
	int ret;

	/* Direct examination of eventually single whitelisted name */
	ret = single_enumerate(0);
	if (ret < 0)
		return -1;
	if (ret > 0)
		return 1;

	sg_enumerate();
	ata_enumerate();
	add_proc_info_drives(0);
	return 1;
}


/** Tells wether libburn has the given drive in use or exclusively reserved.
    If it is "open" then libburn will eventually call sg_release() on it when
    it is time to give up usage resp. reservation.
*/
/** Published as burn_drive.drive_is_open() */
int sg_drive_is_open(struct burn_drive * d)
{
	/* a bit more detailed case distinction than needed */
	if (d->fd == -1337)
		return 0;
	if (d->fd < 0)
		return 0;
	return 1;
}


/** Opens the drive for SCSI commands and - if burn activities are prone
    to external interference on your system - obtains an exclusive access lock
    on the drive. (Note: this is not physical tray locking.)
    A drive that has been opened with sg_grab() will eventually be handed
    over to sg_release() for closing and unreserving.
*/
int sg_grab(struct burn_drive *d)
{
	int fd, os_errno= 0, ret;
	int max_tries = 3, tries = 0;

	/* ts A60813 */
	int open_mode = O_RDWR;

/* ts A60821
   <<< debug: for tracing calls which might use open drive fds */
	if (mmc_function_spy(d, "sg_grab") <= 0)
		return 0;


	/* ts A60813 - A60927
	   O_EXCL with devices is a non-POSIX feature
	   of Linux kernels. Possibly introduced 2002.
	   Mentioned in "The Linux SCSI Generic (sg) HOWTO".
	*/
	if(burn_sg_open_o_excl)
		open_mode |= O_EXCL;

	/* ts A60813
	   O_NONBLOCK was hardcoded here. So it should stay default mode.
	   ts A70411
           Switched to O_NDELAY for LKML statement 2007/4/11/141
	*/
	if(burn_sg_open_o_nonblock)
		open_mode |= O_NDELAY;

	/* ts A60813 - A60822
	   After enumeration the drive fd is probably still open.
	   -1337 is the initial value of burn_drive.fd and the value after
	   relase of drive. Unclear why not the official error return
	   value -1 of open(2) war used. */
	if(! burn_drive_is_open(d)) {
		char msg[120];

/* >>> SINGLE_OPEN : This case should be impossible now, since enumeration
                     transfers the fd from scanning to drive.
                     So if close-wait-open is desired, then it has to
                     be done unconditionally.
*/

#ifndef Libburn_udev_wait_useC
#define Libburn_udev_wait_useC 100000
#endif

#ifndef Libburn_udev_extra_open_cyclE

	if (Libburn_udev_wait_useC > 0) {
		/* ts B10921 : workaround for udev which might get
				a kernel event from open() and might
				remove links if it cannot inspect the
				drive. This waiting period shall allow udev
				to act after it was woken up by the drive scan
				activities.
		*/
		sprintf(msg,
	    "To avoid collision with udev: Waiting %lu usec before grabbing",
				(unsigned long) Libburn_udev_wait_useC);
		libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
				LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
		usleep(Libburn_udev_wait_useC);
	}

#endif /* Libburn_udev_extra_open_cyclE */


try_open:;
		/* ts A60821
   		<<< debug: for tracing calls which might use open drive fds */
		mmc_function_spy(NULL, "sg_grab ----------- opening");

		/* ts A70409 : DDLP-B */
		/* >>> obtain single lock on d->devname */

		/* ts A60926 */
		if(burn_sg_open_o_excl>1) {
			fd = -1;
			ret = sg_open_scsi_siblings(d->devname,
					d->global_index,d->sibling_fds,
					d->sibling_fnames,&(d->sibling_count),
					d->host, d->channel, d->id, d->lun);
			if(ret <= 0)
				goto drive_is_in_use;
		}
		fd = open(d->devname, open_mode);
		os_errno = errno;

#ifdef Libburn_udev_extra_open_cyclE

		/* ts B10920 : workaround for udev which might get
				a kernel event from open() and might
				remove links if it cannot inspect the
				drive.
		   ts B10921 : this is more obtrusive than above waiting
				before open(). The drive scan already has
				opened and closed the drive several times.
				So it seems to be merely about giving an
				opportunity to udev, before long term grabbing
				happens.
		*/
		if (fd >= 0 && Libburn_udev_wait_useC > 0) {
			close(fd);
			sprintf(msg,
	    "To avoid collision with udev: Waiting %lu usec before re-opening",
				(unsigned long) Libburn_udev_wait_useC);
			libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
				LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
			usleep(Libburn_udev_wait_useC);
			fd = open(d->devname, open_mode);
			os_errno = errno;
		}
#endif /* Libburn_udev_extra_open_cyclE */

		if (fd >= 0) {
			sg_fcntl_lock(&fd, d->devname, F_WRLCK, 1);
			if (fd < 0)
				goto drive_is_in_use;
		}
	} else
		fd= d->fd;

	if (fd >= 0) {
		d->fd = fd;
		fcntl(fd, F_SETOWN, getpid());
		d->released = 0;
		return 1;
	} else if (errno == EBUSY)
		goto drive_is_in_use;
	libdax_msgs_submit(libdax_messenger, d->global_index, 0x00020003,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"Could not grab drive", os_errno, 0);
	return 0;

drive_is_in_use:;
	tries++;
	if (tries < max_tries) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
				LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
			"Drive is in use. Waiting 2 seconds before re-try", 
				0, 0);
		usleep(2000000);
		goto try_open;
	}
	libdax_msgs_submit(libdax_messenger, d->global_index,
			0x00020003,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
			"Could not grab drive - already in use", 0, 0);
	sg_close_drive(d);
	d->fd = -1337;
	return 0;
}


/** PORTING: Is mainly about the call to sg_close_drive() and wether it
             implements the demanded functionality.
*/
/** Gives up the drive for SCSI commands and releases eventual access locks.
    (Note: this is not physical tray locking.)
*/
int sg_release(struct burn_drive *d)
{
	/* ts A60821
   	<<< debug: for tracing calls which might use open drive fds */
	if (mmc_function_spy(d, "sg_release") <= 0)
		return 0;

	if (d->fd < 1)
		return 0;

	/* ts A60821
   	<<< debug: for tracing calls which might use open drive fds */
	mmc_function_spy(NULL, "sg_release ----------- closing");

	sg_close_drive(d);
	return 0;
}


/** Sends a SCSI command to the drive, receives reply and evaluates wether
    the command succeeded or shall be retried or finally failed.
    Returned SCSI errors shall not lead to a return value indicating failure.
    The callers get notified by c->error. An SCSI failure which leads not to
    a retry shall be notified via scsi_notify_error().
    @return: 1 success , <=0 failure
*/
int sg_issue_command(struct burn_drive *d, struct command *c)
{
	int done = 0, no_c_page = 0, i, ret;
	int err;
	time_t start_time;
	sg_io_hdr_t s;
	/* ts A61030 */
	static FILE *fp= NULL;
	char *msg = NULL;

	BURN_ALLOC_MEM(msg, char, 161);

	c->error = 0;
	memset(c->sense, 0, sizeof(c->sense));

	/* <<< ts A60821
	   debug: for tracing calls which might use open drive fds */
	sprintf(msg, "sg_issue_command   d->fd= %d  d->released= %d\n",
		d->fd, d->released);
	mmc_function_spy(NULL, msg);

	/* >>> ts B11110 : move this into scsi_log_cmd() together with the
	                    static fp */
	/* ts A61030 */
	if (burn_sg_log_scsi & 1) {
		if (fp == NULL) {
			fp= fopen("/tmp/libburn_sg_command_log", "a");
			fprintf(fp,
			    "\n-----------------------------------------\n");
		}
	}
	if (burn_sg_log_scsi & 3)
		scsi_log_cmd(c,fp,0);

	/* ts A61010 : with no fd there is no chance to send an ioctl */
	if (d->fd < 0) {
		c->error = 1;
		{ret = 0; goto ex;}
	}

	c->error = 0;
	memset(&s, 0, sizeof(sg_io_hdr_t));

	s.interface_id = 'S';

#ifdef Libburn_sgio_as_growisofS
	/* ??? ts A91112 : does this speed up USB ? (from growisofs)
	--- did not help
	 */
	s.flags = SG_FLAG_DIRECT_IO;
#endif /* Libburn_sgio_as_growisofS */

	if (c->dir == TO_DRIVE)
		s.dxfer_direction = SG_DXFER_TO_DEV;
	else if (c->dir == FROM_DRIVE)
		s.dxfer_direction = SG_DXFER_FROM_DEV;
	else if (c->dir == NO_TRANSFER) {
		s.dxfer_direction = SG_DXFER_NONE;

		/* ts A61007 */
		/* a ssert(!c->page); */
		no_c_page = 1;
	}
	s.cmd_len = c->oplen;
	s.cmdp = c->opcode;
	s.mx_sb_len = 32;
	s.sbp = c->sense;
	if (c->timeout > 0)
		s.timeout = c->timeout;
	else
		s.timeout = Libburn_scsi_default_timeouT;
	if (c->page && !no_c_page) {
		s.dxferp = c->page->data;
		if (c->dir == FROM_DRIVE) {

			/* ts A70519 : kernel 2.4 usb-storage seems to
					expect exact dxfer_len for data
					fetching commands.
			*/
			if (c->dxfer_len >= 0)
				s.dxfer_len = c->dxfer_len;
			else
				s.dxfer_len = BUFFER_SIZE;
/* touch page so we can use valgrind */
			memset(c->page->data, 0, BUFFER_SIZE);
		} else {

			/* ts A61010 */
			/* a ssert(c->page->bytes > 0); */
			if (c->page->bytes <= 0) {
				c->error = 1;
				{ret = 0; goto ex;}
			}

			s.dxfer_len = c->page->bytes;
		}
	} else {
		s.dxferp = NULL;
		s.dxfer_len = 0;
	}
	s.usr_ptr = c;

	start_time = time(NULL);
	for(i = 0; !done; i++) {

		memset(c->sense, 0, sizeof(c->sense));
		err = ioctl(d->fd, SG_IO, &s);

		/* ts A61010 */
		/* a ssert(err != -1); */
		if (err == -1) {
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
                done = scsi_eval_cmd_outcome(d, c, fp, s.sbp, s.sb_len_wr,
				s.duration, start_time, s.timeout, i, 0);
		if (d->cancel)
			done = 1;
	}

	if (s.host_status != Libburn_sg_host_oK || 
	    (s.driver_status != Libburn_sg_driver_oK && !c->error)) {
		sprintf(msg,
			"SCSI command %2.2Xh indicates host or driver error:",
			(unsigned int) c->opcode[0]);
		sprintf(msg+strlen(msg),
			" host_status= %xh , driver_status= %xh",
			(unsigned int) s.host_status,
			(unsigned int) s.driver_status);
		libdax_msgs_submit(libdax_messenger, d->global_index,
				0x0002013b,
				LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
				msg, 0, 0);
	}
	ret = 1;
ex:;
	BURN_FREE_MEM(msg);
	return ret;
}


/* ts B11001 : outsourced from non-static sg_obtain_scsi_adr() */
/** Tries to obtain SCSI address parameters.
    @return  1 is success , 0 is failure
*/
static int sg_obtain_scsi_adr_fd(char *path, int fd_in,
				 int *bus_no, int *host_no, int *channel_no,
				 int *target_no, int *lun_no)
{
	int fd, ret, l, open_mode = O_RDONLY;
	struct my_scsi_idlun {
		int x;
		int host_unique_id;
	};
 	struct my_scsi_idlun idlun;

	/* valgrind called idlun unitialized because it is blind for ioctl */
	idlun.x = 0;
	idlun.host_unique_id = 0;

	l = strlen(linux_ata_device_family) - 2;
	if (l > 0 && strncmp(path, linux_ata_device_family, l) == 0 
	    && path[7] >= 'a' && path[7] <= 'z' && path[8] == 0)
		return 0; /* on RIP 14 all hdx return SCSI adr 0,0,0,0 */

	/* ts A70409 : DDLP-B */
	/* >>> obtain single lock on path */

	if(burn_sg_open_o_nonblock)
		open_mode |= O_NDELAY;
	if(burn_sg_open_o_excl) {
		/* O_EXCL | O_RDONLY does not work with /dev/sg* on 
		   SuSE 9.0 (kernel 2.4) and SuSE 9.3 (kernel 2.6) */
		/* so skip it for now */;
	}
	if (fd_in >= 0)
		fd = fd_in;
	else
		fd = open(path, open_mode);
	if(fd < 0)
		return 0;
	sg_fcntl_lock(&fd, path, F_RDLCK, 0);
	if(fd < 0)
		return 0;

#ifdef SCSI_IOCTL_GET_BUS_NUMBER
	/* Hearsay A61005 */
	if (ioctl(fd, SCSI_IOCTL_GET_BUS_NUMBER, bus_no) == -1)
		*bus_no = -1;
#endif

	/* http://www.tldp.org/HOWTO/SCSI-Generic-HOWTO/scsi_g_idlun.html */
	ret = ioctl(fd, SCSI_IOCTL_GET_IDLUN, &idlun);

	if (fd_in < 0)
		sg_close_drive_fd(path, -1, &fd, 0);
	if (ret == -1)
		return(0);
	*host_no= (idlun.x>>24)&255;
	*channel_no= (idlun.x>>16)&255;
	*target_no= (idlun.x)&255;
	*lun_no= (idlun.x>>8)&255;
#ifdef SCSI_IOCTL_GET_BUS_NUMBER
	if(*bus_no == -1)
		*bus_no = 1000 * (*host_no + 1) + *channel_no;
#else
	*bus_no= *host_no;
#endif
	return 1;
}


/* ts A60922 */
/** Tries to obtain SCSI address parameters.
    @return  1 is success , 0 is failure
*/
int sg_obtain_scsi_adr(char *path, int *bus_no, int *host_no, int *channel_no,
                       int *target_no, int *lun_no)
{
	return sg_obtain_scsi_adr_fd(path, -1, bus_no, host_no, channel_no,
					 target_no, lun_no);
}


/* ts A60922 ticket 33 : called from drive.c */
/** Tells wether a text is a persistent address as listed by the enumeration
    functions.
*/
int sg_is_enumerable_adr(char *adr)
{
	char *fname = NULL;
	int ret = 0, first = 1, fname_size = 4096;
	burn_drive_enumerator_t idx;

	BURN_ALLOC_MEM(fname, char, fname_size);
	while (1) {
		ret= sg_give_next_adr(&idx, fname, fname_size, first);
		if(ret <= 0)
	break;
		first = 0;
		if (strcmp(adr, fname) == 0) {
			sg_give_next_adr(&idx, fname, fname_size, -1);
			{ret = 1; goto ex;}
		}
	}
	ret = 0;
ex:;
	if (first == 0)
		sg_give_next_adr(&idx, fname, fname_size, -1);
	BURN_FREE_MEM(fname);
	return ret;
}


/* ts B00115 */
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


/* ts A70909 */
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
	struct statvfs vfsbuf;
	char *testpath = NULL, *cpt;
	long blocks;
	int open_mode = O_RDONLY, fd, ret;
	off_t add_size = 0;

	BURN_ALLOC_MEM(testpath, char, 4096);
	testpath[0] = 0;
	blocks = *bytes / 512;
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
		fd = open(path, open_mode);
		if (fd == -1)
			{ret = -2; goto ex;}
		ret = ioctl(fd, BLKGETSIZE, &blocks);
		close(fd);
		if (ret == -1)
			{ret = -2; goto ex;}
		*bytes = ((off_t) blocks) * (off_t) 512;
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

#ifdef NIX
/* <<< */
	fprintf(stderr, "libburn_DEBUG: Faking 4.5 TB of disk space\n");
	*bytes = ((off_t) 2415919104) * (off_t) 2048;
	if (*bytes / (off_t) 2048 > (off_t) 0x7ffffff0) {
		*bytes = ((off_t) 0x7ffffff0) * (off_t) 2048;
		fprintf(stderr, "libburn_DEBUG: Reducing disk space to 4 TB - 2 kB\n");
	}
/* <<< */
#endif


	ret = 1;
ex:;
	BURN_FREE_MEM(testpath);
	return ret;
}


/* ts A91122 : an interface to open(O_DIRECT) or similar OS tricks. */

#ifdef PROT_READ
#ifdef PROT_WRITE
#ifdef MAP_SHARED
#ifdef MAP_ANONYMOUS
#ifdef MAP_FAILED
#define Libburn_linux_do_mmaP 1
#endif
#endif
#endif
#endif
#endif

#ifdef Libburn_read_o_direcT
#ifdef O_DIRECT
#define Libburn_linux_do_o_direcT 1
#endif
#endif /* Libburn_read_o_direcT */


int burn_os_open_track_src(char *path, int open_flags, int flag)
{
	int fd;

#ifdef Libburn_linux_do_o_direcT
	libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
		LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
		"Opening track source with O_DIRECT" , 0, 0);
	fd = open(path, open_flags | O_DIRECT);
#else
	fd = open(path, open_flags);
#endif
	return fd;
}


void *burn_os_alloc_buffer(size_t amount, int flag)
{
	void *buf = NULL;

#ifdef Libburn_linux_do_mmaP

	/* >>> check whether size is suitable */;

	libdax_msgs_submit(libdax_messenger, -1, 0x00000002,
		LIBDAX_MSGS_SEV_DEBUG, LIBDAX_MSGS_PRIO_HIGH,
		"Allocating buffer via mmap()" , 0, 0);
	buf = mmap(NULL, amount, PROT_READ | PROT_WRITE,
			 	MAP_SHARED | MAP_ANONYMOUS, -1, (off_t) 0);
	if (buf == MAP_FAILED)
		buf = NULL;
	else
		memset(buf, 0, amount);
#else
	buf = calloc(1, amount);
#endif /* ! Libburn_linux_do_mmaP */

	return buf;
}


int burn_os_free_buffer(void *buffer, size_t amount, int flag)
{
	int ret = 0;

	if (buffer == NULL)
		return 0;
#ifdef Libburn_linux_do_mmaP
	ret = munmap(buffer, amount);
#else
	free(buffer);
#endif
	return (ret == 0);
}

