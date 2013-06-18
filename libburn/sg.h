/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (C) 2009 Thomas Schmitt <scdbackup@gmx.net>, provided under GPLv2+
*/

#ifndef __SG
#define __SG


#include "os.h"


/* see os.h for name of particular os-*.h where this is defined */
BURN_OS_DEFINE_DRIVE_ENUMERATOR_T


struct burn_drive;
struct command;


/* ts A60922 ticket 33 */
int sg_give_next_adr(burn_drive_enumerator_t *enm_context,
		     char adr[], int adr_size, int initialize);
int sg_is_enumerable_adr(char *adr);
int sg_obtain_scsi_adr(char *path, int *bus_no, int *host_no, int *channel_no,
                       int *target_no, int *lun_no);

int sg_grab(struct burn_drive *);
int sg_release(struct burn_drive *);
int sg_issue_command(struct burn_drive *, struct command *);

/* ts A61115 : formerly sg_enumerate();ata_enumerate() */
int scsi_enumerate_drives(void);

int sg_drive_is_open(struct burn_drive * d);

int burn_os_is_2k_seekrw(char *path, int flag);

int burn_os_stdio_capacity(char *path, off_t *bytes);

/* ts A91227 */
/** Returns the id string of the SCSI transport adapter and eventually
    needed operating system facilities.
    This call is usable even if sg_initialize() was not called yet. In that
    case a preliminary constant message might be issued if detailed info is
    not available yet.
    @param msg   returns id string
    @param flag  unused yet, submit 0
    @return      1 = success, <=0 = failure
*/
int sg_id_string(char msg[1024], int flag);

/* ts A91225 */
/** Performs global initialization of the SCSI transport adapter and eventually
    needed operating system facilities. Checks for compatibility supporting
    software components.
    @param msg   returns ids and/or error messages of eventual helpers
    @param flag  unused yet, submit 0
    @return      1 = success, <=0 = failure 
*/      
int sg_initialize(char msg[1024], int flag);

/* ts A91227 */
/** Performs global finalization of the SCSI transport adapter and eventually
    needed operating system facilities. Releases globally aquired resources.
    @param flag  unused yet, submit 0
    @return      1 = success, <=0 = failure
*/
int sg_shutdown(int flag);

/* ts A91227 */
/** Finalizes BURN_OS_TRANSPORT_DRIVE_ELEMENTS, the components of
    struct burn_drive which are defined in os-*.h.
    The eventual initialization of those components was made underneath
    scsi_enumerate_drives().
    This will be called when a burn_drive gets disposed.
    @param d     the drive to be finalized
    @param flag  unused yet, submit 0
    @return      1 = success, <=0 = failure
*/
int sg_dispose_drive(struct burn_drive *d, int flag);


#endif /* __SG */
