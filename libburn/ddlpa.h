
/* ddlpa
   Implementation of Delicate Device Locking Protocol level A.
   Copyright (C) 2007 Thomas Schmitt <scdbackup@gmx.net>
   Provided under any of the following licenses: GPL, LGPL, BSD. Choose one.

   See ../doc/ddlp.txt for a description of the protocol.
*/

#ifndef DDLPA_H_INCLUDED
#define DDLPA_H_INCLUDED 1


/* An upper limit for the length of standard paths and sibling paths */
#define DDLPA_MAX_STD_LEN 15

/* An upper limit for the number of siblings */
#define DDLPA_MAX_SIBLINGS 5

struct ddlpa_lock {

	/* Recorded input parameters of locking call */
	char *path;
	int  path_is_valid;
	int  in_bus, in_target, in_lun;
	int  inbtl_is_valid;
	int  ddlpa_flags;
	int  o_flags;

	/* Result of locking call */
	char std_path[DDLPA_MAX_STD_LEN + 1];
	int  fd;
	dev_t rdev;
	dev_t dev;
	ino_t ino;
	int  host, channel, id, lun, bus;
	int  hcilb_is_valid;
	int  num_siblings;
	char sibling_paths[DDLPA_MAX_SIBLINGS][DDLPA_MAX_STD_LEN + 1];
	int  sibling_fds[DDLPA_MAX_SIBLINGS];
	dev_t sibling_rdevs[DDLPA_MAX_SIBLINGS];
	dev_t sibling_devs[DDLPA_MAX_SIBLINGS];
	ino_t sibling_inodes[DDLPA_MAX_SIBLINGS];

	/* Is NULL if all goes well. Else it may contain a text message. */
	char *errmsg;
};



/** Lock a recorder by naming a device file path. Allocate a new container.
    @param path        Gives the file system path of the recorder
                       as known to the calling program.
    @param o_flags     flags for open(2). Do not use O_EXCL here because this
                       is done automatically whenever appropriate.
                       Advised is O_RDWR | O_LARGEFILE, eventually | O_NDELAY.
    @param ddlpa_flags 0 = default behavior: the standard path will be opened
                           and treated by fcntl(F_SETLK)
                       DDLPA_OPEN_GIVEN_PATH causes the input parameter "path"
                       to be used with open(2) and fcntl(2).
                       DDLPA_ALLOW_MISSING_SGRCD allows to grant a lock
                       although not all three, a sg, a sr and a scd device
                       file have been found during sibling search. Normally
                       this is counted as failure due to EBUSY. 
    @param lockbundle  gets allocated and then represents the locking state
    @param errmsg      if *errmsg is not NULL after the call, it contains an
                       error message. Then to be released by free(3).
                       It is NULL in case of success or lack of memory.
    @return            0=success , else an errno compatible error number
*/
int ddlpa_lock_path(char *path, int  o_flags, int ddlpa_flags, 
                    struct ddlpa_lock **lockbundle, char **errmsg);


/** Lock a recorder by naming a Bus,Target,Lun number triple.
    Allocate a new container.
    @param bus         parameter to match ioctl(SCSI_IOCTL_GET_BUS_NUMBER)
    @param target      parameter to match ioctl(SCSI_IOCTL_GET_IDLUN) &0xff
    @param lun         parameter to match ioctl(SCSI_IOCTL_GET_IDLUN) &0xff00
    @param o_flags     see ddlpa_lock_path().
    @param ddlpa_flags see ddlpa_lock_path(). Flag DDLPA_OPEN_GIVEN_PATH
                       will be ignored.
    @param lockbundle  see ddlpa_lock_path().
    @param errmsg      see ddlpa_lock_path().
    @return            0=success , else an errno compatible error number
*/
int ddlpa_lock_btl(int bus, int target, int lun,
                   int  o_flags, int ddlpa_flags,
                   struct ddlpa_lock **lockbundle, char **errmsg);


/** Release the lock by closing all filedescriptors and freeing memory.
    @param lockbundle  the lock which is to be released. 
                       *lockbundle will be set to NULL by this call.
    @return            0=success , 1=failure
*/
int ddlpa_destroy(struct ddlpa_lock **lockbundle);



/** Definitions of macros used in above functions */

#define DDLPA_OPEN_GIVEN_PATH 1
#define DDLPA_ALLOW_MISSING_SGRCD 2


#endif /* DDLPA_H_INCLUDED */
