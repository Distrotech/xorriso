
/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2012 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/

#ifndef BURN__OPTIONS_H
#define BURN__OPTIONS_H

#include "libburn.h"

/** Options for disc writing operations. This should be created with
    burn_write_opts_new() and freed with burn_write_opts_free(). */
struct burn_write_opts
{
	/** Drive the write opts are good for */
	struct burn_drive *drive;

	/** For internal use. */
	int refcount;

	/** The method/style of writing to use. */
	enum burn_write_types write_type;
	/** format of the data to send to the drive */
	enum burn_block_types block_type;

	/** Number of toc entries.  if this is 0, they will be auto generated*/
	int toc_entries;
	/** Toc entries for the disc */
	struct burn_toc_entry *toc_entry;

	/** Simulate the write so that the disc is not actually written */
	unsigned int simulate:1;
	/** If available, enable a drive feature which prevents buffer
	    underruns if not enough data is available to keep up with the
	    drive. */
	unsigned int underrun_proof:1;
	/** Perform calibration of the drive's laser before beginning the
	    write. */
	unsigned int perform_opc:1;

	/* ts A61219 : Output block size to trigger buffer flush if hit.
			 -1 with CD, 32 kB with DVD */
	int obs;
	int obs_pad; /* >0 pad up last block to obs, 0 do not
	                2 indicates burn_write_opts_set_obs_pad(,1)
	             */

	/* ts A61222 : Start address for media which allow a choice */
	off_t start_byte;

	/* ts A70213 : Wether to fill up the available space on media */
	int fill_up_media;

	/* ts A70303 : Wether to override conformance checks:
	   - the check wether CD write+block type is supported by the drive 
	*/
	int force_is_set;

	/* ts A80412 : whether to use WRITE12 with Streaming bit set
	   rather than WRITE10. Speeds up DVD-RAM. Might help with BD-RE.
	   This gets transferred to burn_drive.do_stream_recording
	*/
	int do_stream_recording;

	/* ts A91115 : override value for .obs on DVD media.
	   Only values 0, 32K and 64K are allowed for now. */
	int dvd_obs_override;

	/* ts A91115 : size of the fsync() interval for stdio writing.
	   Values 0 or >= 32 counted in 2 KB blocks. */
	int stdio_fsync_size;

	/* ts B11203 : CD-TEXT */
	unsigned char *text_packs;
	int num_text_packs;
	int no_text_pack_crc_check;

	/** A disc can have a media catalog number */
	int has_mediacatalog;
	unsigned char mediacatalog[13];
	/** Session format */
	int format;
	/* internal use only */
	unsigned char control;
	unsigned char multi;
};

/* Default value for burn_write_opts.stdio_flush_size
*/
#define Libburn_stdio_fsync_limiT 8192

/* Maximum number of Lead-in text packs.
   READ TOC/PMA/ATIP can at most return 3640.7 packs.
   The sequence counters of the packs have 8 bits. There are 8 blocks at most.
   Thus max 2048 packs. 
 */
#define Libburn_leadin_cdtext_packs_maX 2048


/** Options for disc reading operations. This should be created with
    burn_read_opts_new() and freed with burn_read_opts_free(). */
struct burn_read_opts
{
	/** Drive the read opts are good for */
	struct burn_drive *drive;

	/** For internal use. */
	int refcount;

	/** Read in raw mode, so that everything in the data tracks on the
	    disc is read, including headers. Not needed if just reading a
	    filesystem off a disc, but it should usually be used when making a
	    disc image or copying a disc. */
	unsigned int raw:1;
	/** Report c2 errors. Useful for statistics reporting */
	unsigned int c2errors:1;
	/** Read subcodes from audio tracks on the disc */
	unsigned int subcodes_audio:1;
	/** Read subcodes from data tracks on the disc */
	unsigned int subcodes_data:1;
	/** Have the drive recover errors if possible */
	unsigned int hardware_error_recovery:1;
	/** Report errors even when they were recovered from */
	unsigned int report_recovered_errors:1;
	/** Read blocks even when there are unrecoverable errors in them */
	unsigned int transfer_damaged_blocks:1;

	/** The number of retries the hardware should make to correct
	    errors. */
	unsigned char hardware_error_retries;

	/* ts B21119 */
	/* >>> Needs API access */
	/** Whether to set DAP bit which allows drive to apply
	    "flaw obscuring mechanisms like audio data mute and interpolate"
	*/
	unsigned int dap_bit;

};

#endif /* BURN__OPTIONS_H */
