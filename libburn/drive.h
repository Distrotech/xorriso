/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2011 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/


#ifndef __DRIVE
#define __DRIVE

#include "libburn.h"
#include "toc.h"
#include "structure.h"
#include <pthread.h>

struct burn_drive;
struct command;
struct mempage;
struct scsi_mode_data;
struct burn_speed_descriptor;

#define LEAD_IN 1
#define GAP 2
#define USER_DATA 3
#define LEAD_OUT 4
#define SYNC 5

#define SESSION_LEADOUT_ENTRY(d,s) (d)->toc->session[(s)].leadout_entry

#define CURRENT_SESSION_START(d) \
	burn_msf_to_lba(d->toc->session[d->currsession].start_m, \
	                   d->toc->session[d->currsession].start_s, \
	                   d->toc->session[d->currsession].start_f)

#define SESSION_END(d,s) \
	TOC_ENTRY_PLBA((d)->toc, SESSION_LEADOUT_ENTRY((d), (s)))

#define PREVIOUS_SESSION_END(d) \
	TOC_ENTRY_PLBA((d)->toc, SESSION_LEADOUT_ENTRY((d), (d)->currsession-1))

#define LAST_SESSION_END(d) \
	TOC_ENTRY_PLBA((d)->toc, \
	               SESSION_LEADOUT_ENTRY((d), (d)->toc->sessions-1))

struct burn_drive *burn_drive_register(struct burn_drive *);
int burn_drive_unregister(struct burn_drive *d);

unsigned int burn_drive_count(void);

/* ts A61007 */
/* void burn_wait_all(void); */
/* @param flag  bit0= demand freed drives (else released drives) */
int burn_drives_are_clear(int flag);

int burn_sector_length_write(struct burn_drive *d);
int burn_track_control(struct burn_drive *d, int);
void burn_write_empty_sector(int fd);
void burn_write_empty_subcode(int fd);
void burn_drive_free(struct burn_drive *d);
void burn_drive_free_all(void);

/* @param flag bit0= reset global drive list */
int burn_drive_scan_sync(struct burn_drive_info *drives[],
			 unsigned int *n_drives, int flag);

void burn_disc_erase_sync(struct burn_drive *d, int fast);
int burn_drive_get_block_types(struct burn_drive *d,
			       enum burn_write_types write_type);

int burn_drive_is_open(struct burn_drive *d);
int burn_drive_is_occupied(struct burn_drive *d);
int burn_drive_forget(struct burn_drive *d, int force);
int burn_drive_convert_fs_adr_sub(char *path, char adr[], int *rec_count);

/* ts A61021 : the unspecific part of sg.c:enumerate_common()
*/
int burn_setup_drive(struct burn_drive *d, char *fname);

/* ts A61021 : after-setup activities from sg.c:enumerate_common()
*/
struct burn_drive *burn_drive_finish_enum(struct burn_drive *d);

/* ts A61125 : media status aspects of burn_drive_grab() */
int burn_drive_inquire_media(struct burn_drive *d);

/* ts A61125 : model aspects of burn_drive_release */
int burn_drive_mark_unready(struct burn_drive *d, int flag);


/* ts A61226 */
int burn_speed_descriptor_new(struct burn_speed_descriptor **s,
				struct burn_speed_descriptor *prev,
				struct burn_speed_descriptor *next, int flag);

/* ts A61226 */
/* @param flag bit0= destroy whole next-chain of descriptors */
int burn_speed_descriptor_destroy(struct burn_speed_descriptor **s, int flag);


/* ts A61226 : free dynamically allocated sub data of struct scsi_mode_data */
int burn_mdata_free_subs(struct scsi_mode_data *m);


/* ts A61230 */
void burn_disc_format_sync(struct burn_drive *d, off_t size, int flag);


/* ts A70207 : evaluate write mode related peculiarities of a disc */
struct burn_disc_mode_demands {
	int multi_session;
	int multi_track;
	int unknown_track_size; /* 0=known, 1=unknown, 2=unknown+defaulted */
	int mixed_mode;
	int audio;
	int exotic_track;
	int block_types;
	int will_append; /* because of media state or multi session disc */
};
int burn_disc_get_write_mode_demands(struct burn_disc *disc,
			struct burn_write_opts *opts,
			struct burn_disc_mode_demands *result, int flag);


/* ts A70924 : convert a special stdio address into fd number.
   @return >0 is a valid fd , -1 indicates unsuitable address string. 
*/
int burn_drive__fd_from_special_adr(char *adr);


/* ts A70929 : Find the drive which is being worked on by pid , tid */
int burn_drive_find_by_thread_pid(struct burn_drive **d, pid_t pid,
					pthread_t tid);


/* ts A51221 - A80731 : Whitelist inquiry functions */
int burn_drive_is_banned(char *device_address);
int burn_drive_whitelist_count(void);
char *burn_drive_whitelist_item(int idx, int flag);


/* ts A80801 */
int burn_drive_is_listed(char *path, struct burn_drive **found, int flag);


/* ts B00226 : Outsourced backend of burn_abort()
   @param elapsed  to be subtracted from start time
   @param flag     bit0= do not shutdown the library
*/ 
int burn_abort_5(int patience,
               int (*pacifier_func)(void *handle, int patience, int elapsed),
               void *handle, int elapsed, int flag);

/* ts B10730 */
/* Send a default mode page 05 to CD and DVD-R-oids */
int burn_drive_send_default_page_05(struct burn_drive *d, int flag);

#endif /* __DRIVE */
