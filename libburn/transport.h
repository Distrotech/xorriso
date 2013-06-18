/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2012 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/


#ifndef __TRANSPORT
#define __TRANSPORT

#include "libburn.h"
#include "os.h"

#include <pthread.h>
/* sg data structures */
#include <sys/types.h>


/* see os.h for name of particular os-*.h where this is defined */
#define BUFFER_SIZE BURN_OS_TRANSPORT_BUFFER_SIZE


enum transfer_direction
{ TO_DRIVE, FROM_DRIVE, NO_TRANSFER };

/* end of sg data structures */

/* generic 'drive' data structures */

struct cue_sheet
{
	int count;
	unsigned char *data;
};

struct params
{
	int speed;
	int retries;
};

struct buffer
{
	/* ts A61219: 
	   Added 4096 bytes reserve against possible buffer overflows.
	   (Changed in sector.c buffer flush test from >= to > BUFFER_SIZE .
	    This can at most cause a 1 sector overlap. Sometimes an offset
	    of 16 byte is applied to the output data (in some RAW mode). )
	    burn_write_opts.cdxa_conversion can imply an offset of 8 bytes.
	 */
	unsigned char data[BUFFER_SIZE + 4096];
	int sectors;
	int bytes;
};

struct command
{
	unsigned char opcode[16];
	int oplen;
	int dir;
	int dxfer_len;
	unsigned char sense[128];
	int error;
	int retry;
	struct buffer *page;
	int timeout; /* milliseconds */
};

struct burn_scsi_inquiry_data
{
	char vendor[9];
	char product[17];
	char revision[5];
	int valid;
};


struct scsi_mode_data
{
	int buffer_size;
	int dvdram_read;
	int dvdram_write;
	int dvdr_read;
	int dvdr_write;
	int dvdrom_read;
	int cdrw_read;
	int cdrw_write;
	int cdr_read;
	int cdr_write;
	int simulate;
	int max_read_speed;
	int max_write_speed;

	/* ts A61021 */
	int min_write_speed;

	/* ts A61225 : Results from ACh GET PERFORMANCE, Type 03h
	               Speed values go into *_*_speed */
	int min_end_lba;
	int max_end_lba;
	struct burn_speed_descriptor *speed_descriptors;

	int cur_read_speed;
	int cur_write_speed;
	int retry_page_length;
	int retry_page_valid;
	int write_page_length;
	int write_page_valid;
	int c2_pointers;
	int valid;
	int underrun_proof;
};


/* ts A70112 : represents a single Formattable Capacity Descriptor as of
               mmc5r03c.pdf 6.24.3.3 . There can at most be 32 of them. */
struct burn_format_descr {
	/* format type: e.g 0x00 is "Full", 0x15 is "Quick" */
	int type;

	/* the size in bytes derived from Number of Blocks */
	off_t size;

	/* the Type Dependent Parameter (usually the write alignment size) */
	unsigned int tdp;
};


/** Gets initialized in enumerate_common() and burn_drive_register() */
struct burn_drive
{
	/* ts A70902:
		0=null-emulation
		1=MMC drive ,
		2=stdio random read-write
		3=stdio sequential write-only
                4=stdio random read-only
                5=stdio random write-only
	*/
	int drive_role;

	int bus_no;
	int host;
	int id;
	int channel;
	int lun;
	char *devname;

	/* ts A70302: mmc5r03c.pdf 5.3.2 Physical Interface Standard */
	int phys_if_std;   /* 1=SCSI, 2=ATAPI, 3,4,6=FireWire, 7=SATA, 8=USB */
	char phys_if_name[80];  /* MMC-5 5.3.2 table 91 , e.g. "SCSI Family" */ 

	/* see os.h for name of particular os-*.h where this is defined */
	BURN_OS_TRANSPORT_DRIVE_ELEMENTS	


	/* ts A60904 : ticket 62, contribution by elmom */
	/**
	    Tells the index in scanned burn_drive_info array.
	    -1 if fallen victim to burn_drive_info_forget()
	*/
	int global_index;

	pthread_mutex_t access_lock;

	enum burn_disc_status status;
	int erasable;

	/* ts A61201 from 46h GET CONFIGURATION  */
	int current_profile;
	char current_profile_text[80];
	int current_is_cd_profile;
	int current_is_supported_profile;
	/* ts A90603 */
	int current_is_guessed_profile;
	/* ts A90815 */
	unsigned char all_profiles[256];
	int num_profiles;

	/* ts A70128 : MMC-to-MMC feature info from 46h for DVD-RW.
           Quite internal. Regard as opaque :)
	*/
	/* 1 = incremental recording available, 0 = not available */
	int current_has_feat21h;

	/* Link Size item number 0 from feature 0021h descriptor */
	int current_feat21h_link_size;

	/* Flags from feature 0023h for formatting BD mmc5r03c.pdf 5.3.13 
           Byte 4 BD-RE:
             bit0= Cert   format 30h sub-type 10b
             bit1= QCert  format 30h sub-type 11b
             bit2= Expand format 01h
             bit3= RENoSA format 31h
           Byte 8 BD-R:
             bit0= RRM    format 32h sub-type 10b
        */
	int current_feat23h_byte4;
	int current_feat23h_byte8;


	/* Flags from feature 002Fh feature descriptor mmc5r03c.pdf 5.3.25 :
	     bit1= DVD-RW supported
	     bit2= Test Write available
	     bit3= DVD-R DL supported
	     bit6= Buffer Under-run Free recording available (page 05h BUFE)
	   Value -1 indicates that no 002Fh was current in the features list.
	*/
	int current_feat2fh_byte4;

	/* ts B10524 : whether the damage bit was set for the future track.
	               bit0= damage bit , bit1= nwa valid bit
	*/
	int next_track_damaged;

	/* ts A70114 : whether a DVD-RW media holds an incomplete session
	               (which could need closing after write) */
	int needs_close_session;
	/* ts A71003 : whether a random write operation was done and no
	               synchronize chache has happened yet */
	int needs_sync_cache;

	/* ts A80412 : whether to use WRITE12 with Streaming bit set
	               rather than WRITE10. Speeds up DVD-RAM. Might help
	               with BD-RE */
	int do_stream_recording;

        /* ts A90227 : the LBA where stream recording shall start.
                       Writing to lower LBA will be done without streaming.
        */
        int stream_recording_start;

	/* ts A61218 from 51h READ DISC INFORMATION */
	int last_lead_in;
	int last_lead_out;
	int num_opc_tables;   /* ts A91104: -1= not yet known */
	int bg_format_status; /* 0=needs format start, 1=needs format restart*/
	int disc_type; /* 0="CD-DA or CD-ROM", 0x10="CD-I", 0x20="CD-ROM XA" */
	unsigned int disc_id; /* a "32 bit binary integer" */
	char disc_bar_code[9];
	int disc_app_code;
	int disc_info_valid;  /* bit0= disc_type ,     bit1= disc_id ,
				 bit2= disc_bar_code , bit3= disc_app_code
				 bit4= URU bit is set (= unrestricted use)
				 bit5= Erasable bit was set in reply
			       */

	/* ts A70108 from 23h READ FORMAT CAPACITY mmc5r03c.pdf 6.24 */
	int format_descr_type;      /* 1=unformatted, 2=formatted, 3=unclear */
	off_t format_curr_max_size;  /* meaning depends on format_descr_type */
	unsigned int format_curr_blsas;  /* dito */
	int best_format_type;
	off_t best_format_size;

	/* The complete list of format descriptors as read with 23h */
	int num_format_descr;
	struct burn_format_descr format_descriptors[32];
	

	volatile int released;

	/* ts A61106 */
	/* 0= report errors
	   1= do not report errors
	   2= do not report errors which the libburn function indicates in
	      member .had_particular_error
	*/
	int silent_on_scsi_error;

	/* ts B21023 */
	/* bit0= 5 64 00 occured with READ10 in mmc_read_10()
	*/
	int had_particular_error;

	int stdio_fd;

	int nwa;		/* next writeable address */
	int alba;		/* absolute lba */
	int rlba;		/* relative lba in section */
	int start_lba;
	int end_lba;


	/* ts A70131 : from 51h READ DISC INFORMATION Number of Sessions (-1)*/
	int complete_sessions;
	/* ts A90107 */
	int state_of_last_session;

#ifdef Libburn_disc_with_incomplete_sessioN
	/* ts B30112 */
	int incomplete_sessions;
#endif


	/* ts A70129 :
	   from 51h READ DISC INFORMATION Last Track Number in Last Session */
	int last_track_no;

	/* ts B10730 : whether a default mode page 05 was already sent.
	*/
	int sent_default_page_05;
	/* ts A70212 : from various sources : free space on media (in bytes)
	               With CD this might change after particular write
	               parameters have been set and nwa has been inquired.
	               (e.g. by d->send_write_parameters() ; d->get_nwa()).
	*/
	off_t media_capacity_remaining;
	/* ts A70215 : if > 0 : first lba on media that is too high for write*/
	int media_lba_limit;

	/* ts A81210 : Upper limit of readable data size,
	               0x7fffffff = unknown
	               0x7ffffff0 = 32 bit overflow, or unknown stdio size
	 */
	int media_read_capacity;

	/* ts B10314 : Next Writeable Adress for drive_role == 5 */
        int role_5_nwa;

	int toc_temp;
	struct burn_disc *disc;	/* disc structure */
	int block_types[4];
	struct buffer *buffer;
	struct burn_progress progress;

	/* To be used by mmc.c, sbc.c, spc.c for SCSI commands where the struct
	   content surely does not have to persist while another command gets
	   composed and executed.
	   (Inherently, sending SCSI commands to the same drive cannot be
	    thread-safe. But there are functions which send SCSI commands
	    and also call other such functions. These shall use own allocated
	    command structs and not this struct here.)
	*/
	struct command casual_command;

	/* ts A70711 : keeping an eye on the drive buffer */
	off_t pessimistic_buffer_free;
	int pbf_altered;
	int wait_for_buffer_free;
	int nominal_write_speed;
	unsigned int wfb_min_usec;
	unsigned int wfb_max_usec;
	unsigned int wfb_timeout_sec;
	unsigned int wfb_min_percent;
	unsigned int wfb_max_percent;
	unsigned int pessimistic_writes;
	unsigned int waited_writes;
	unsigned int waited_tries;
	unsigned int waited_usec;

	volatile int cancel;
	volatile enum burn_drive_status busy;

	/* ts A70929 */
	pid_t thread_pid;
	int thread_pid_valid;
	/* ts B00225 */
	pthread_t thread_tid;


/* transport functions */
	int (*grab) (struct burn_drive *);
	int (*release) (struct burn_drive *);

	/* ts A61021 */
	int (*drive_is_open) (struct burn_drive *);

	int (*issue_command) (struct burn_drive *, struct command *);

/* lower level functions */
	void (*erase) (struct burn_drive *, int);
	void (*getcaps) (struct burn_drive *);

	/* ts A61021 */
	void (*read_atip) (struct burn_drive *);

	int (*write) (struct burn_drive *, int, struct buffer *);
	void (*read_toc) (struct burn_drive *);
	void (*lock) (struct burn_drive *);
	void (*unlock) (struct burn_drive *);
	void (*eject) (struct burn_drive *);
	void (*load) (struct burn_drive *);
	int (*start_unit) (struct burn_drive *);

	/* ts A90824 : Calming down noisy drives */
	int (*stop_unit) (struct burn_drive *);
	int is_stopped;

	void (*read_disc_info) (struct burn_drive *);
	int (*read_cd) (struct burn_drive *, int start, int len,
	                int sec_type, int main_ch,
	                const struct burn_read_opts *, struct buffer *,
	                int flag);
	void (*perform_opc) (struct burn_drive *);
	void (*set_speed) (struct burn_drive *, int, int);
	void (*send_parameters) (struct burn_drive *,
				 const struct burn_read_opts *);
	void (*send_write_parameters) (struct burn_drive *,
				       struct burn_session *, int tno,
				       const struct burn_write_opts *);
	int (*send_cue_sheet) (struct burn_drive *, struct cue_sheet *);

	/* ts A70205 : Announce size of a DVD-R[W] DAO session. */
	int (*reserve_track) (struct burn_drive *d, off_t size);

	void (*sync_cache) (struct burn_drive *);
	int (*get_erase_progress) (struct burn_drive *);
	int (*get_nwa) (struct burn_drive *, int trackno, int *lba, int *nwa);

	/* ts A70131 : obtain (possibly fake) TOC number and start lba of
			first track in last complete session */
	int (*read_multi_session_c1)(struct burn_drive *d,
				     int *trackno, int *start);

	/* ts A61009 : removed d in favor of o->drive */
	/* void (*close_disc) (struct burn_drive * d,
				 struct burn_write_opts * o);
	   void (*close_session) (struct burn_drive * d,
			       struct burn_write_opts * o);
	*/
	void (*close_disc) (struct burn_write_opts * o);
	void (*close_session) ( struct burn_write_opts * o);

	/* ts A61029 */
	void (*close_track_session) ( struct burn_drive *d,
				int session, int track);

	int (*test_unit_ready) (struct burn_drive * d);
	void (*probe_write_modes) (struct burn_drive * d);
	struct params params;
	struct burn_scsi_inquiry_data *idata;
	struct scsi_mode_data *mdata;
	int toc_entries;
	struct burn_toc_entry *toc_entry;

	/* ts A61023 : get size and free space of drive buffer */
	int (*read_buffer_capacity) (struct burn_drive *d);

	/* ts A61220 : format media (e.g. DVD+RW) */
	int (*format_unit) (struct burn_drive *d, off_t size, int flag);

	/* ts A70108 */
	/* mmc5r03c.pdf 6.24 : get list of available formats */
	int (*read_format_capacities) (struct burn_drive *d, int top_wanted);

	/* ts A70812 */
	/* mmc5r03c.pdf 6.15 : read data sectors (start and amount in LBA) */
	int (*read_10) (struct burn_drive *d, int start, int amount,
	                struct buffer *buf);

};

/* end of generic 'drive' data structures */

/* ts A80422 : centralizing this setting for debugging purposes
*/
int burn_drive_set_media_capacity_remaining(struct burn_drive *d, off_t value);


#endif /* __TRANSPORT */
