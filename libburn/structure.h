
/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2011 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/

#ifndef BURN__STRUCTURE_H
#define BURN__STRUCTURE_H

struct isrc
{
	int has_isrc;
	char country[2];	/* each must be 0-9, A-Z */
	char owner[3];		/* each must be 0-9, A-Z */
	unsigned char year;	/* must be 0-99 */
	unsigned int serial;	/* must be 0-99999 */
};

/* ts B11206 */
#define Libburn_pack_type_basE  0x80
#define Libburn_pack_num_typeS  0x10
#define Libburn_pack_type_nameS \
	"TITLE", "PERFORMER", "SONGWRITER", "COMPOSER", \
	"ARRANGER", "MESSAGE", "DISCID", "GENRE", \
	"TOC", "TOC2",  "", "", \
	"", "CLOSED", "UPC_ISRC", "BLOCKSIZE"

struct burn_cdtext
{
	unsigned char *(payload[Libburn_pack_num_typeS]);
	int length[Libburn_pack_num_typeS];
	int flags;                   /* bit0 - bit15= double byte characters */
};

struct burn_track
{
	int refcnt;
	struct burn_toc_entry *entry;
	unsigned char indices;
	/* lba address of the index. CD only. 0x7fffffff means undefined index.
	   To be programmed relative to track source start before burning,
           but to hold absolute addresses after burning or reading.
	*/
	int index[100];
	/** number of 0 bytes to write before data */
	int offset;
	/** how much offset has been used */
	int offsetcount;
	/** Number of zeros to write after data */
	int tail;
	/** how much tail has been used */
	int tailcount;
	/** 1 means Pad with zeros, 0 means start reading the next track */
	int pad;

	/* ts A70213 : wether to expand this track to full available media */
	int fill_up_media;

	/* ts A70218 : a track size to use if it is mandarory to have some */
	off_t default_size;

	/** Data source */
	struct burn_source *source;
	/** End of Source flag */
	int eos;

	/* ts A61101 */
	off_t sourcecount;
	off_t writecount;
	off_t written_sectors;

	/* ts A61031 */
	/** Source is of undefined length */
	int open_ended;
	/** End of open ended track flag : offset+payload+tail are delivered */
	int track_data_done;
	/* ts B10103 */
	/** End track writing on premature End-of-input if source is of
	    defined length.
	    0= normal operation in case of eoi
	    1= be ready to end track writing on eoi
	    2= eoi was encountered with previously set value of 1
	*/
	int end_on_premature_eoi;

	/** The audio/data mode for the entry. Derived from control and
	    possibly from reading the track's first sector. */
	int mode;
	/** The track contains interval one of a pregap */
	int pregap1;
	/** The track contains interval two of a pregap */
	int pregap2;

	/* ts B20110 */
	/** The number of sectors in pre-gap 2, if .pregap2 is set */
	int pregap2_size;

	/** The track contains a postgap */
	int postgap;

	/* ts B20111 */
	/** The number of sectors in post-gap, if .postgap is set */
	int postgap_size;

	struct isrc isrc;

	/* ts A61024 */
	/** Byte swapping on source data stream : 0=none , 1=pairwise */
	int swap_source_bytes;

	/* ts A90910 : conversions from CD XA prepared input */
	int cdxa_conversion; /* 0=none, 1=remove -xa1 headers (first 8 bytes)*/

	/* ts B11206 */
	struct burn_cdtext *cdtext[8];

};

struct burn_session
{
	unsigned char firsttrack;
	unsigned char lasttrack;
	int hidefirst;
	unsigned char start_m;
	unsigned char start_s;
	unsigned char start_f;
	struct burn_toc_entry *leadout_entry;

	int tracks;
	struct burn_track **track;
	int refcnt;

	/* ts B11206 */
	struct burn_cdtext *cdtext[8];
	unsigned char cdtext_char_code[8];
	unsigned char cdtext_copyright[8];
	unsigned char cdtext_language[8];

	/* ts B11226 */
	unsigned char mediacatalog[14]; /* overrideable by burn_write_opts */
};

struct burn_disc
{
	int sessions;
	struct burn_session **session;

#ifdef Libburn_disc_with_incomplete_sessioN
        int incomplete_sessions;
#endif

	int refcnt;
};

int burn_track_get_shortage(struct burn_track *t);


/* ts A61031 : might go to libburn.h */
int burn_track_is_open_ended(struct burn_track *t);
int burn_track_is_data_done(struct burn_track *t);

/* ts A70125 : sets overall sectors of a track: offset+payload+padding */
int burn_track_set_sectors(struct burn_track *t, int sectors);

/* ts A70218 : sets the payload size alone */
int burn_track_set_size(struct burn_track *t, off_t size);

/* ts A70213 */
int burn_track_set_fillup(struct burn_track *t, int fill_up_media);
int burn_track_apply_fillup(struct burn_track *t, off_t max_size, int flag);

/* ts A70218 */
off_t burn_track_get_default_size(struct burn_track *t);


/* ts A80808 : Enhance CD toc to DVD toc */
int burn_disc_cd_toc_extensions(struct burn_drive *drive, int flag);


/* ts B11206 */
struct burn_cdtext *burn_cdtext_create(void);
void burn_cdtext_free(struct burn_cdtext **cdtext);

/* ts B20119 */
/* @param flag bit0= do not add post-gap
*/
int burn_track_get_sectors_2(struct burn_track *t, int flag);


#endif /* BURN__STRUCTURE_H */
