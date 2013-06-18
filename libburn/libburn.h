/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2013 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.

   This is the official API definition of libburn.

*/
/* Important: If you add a public API function then add its name to file
                 libburn/libburn.ver
*/


#ifndef LIBBURN_H
#define LIBBURN_H

/* 

Applications must use 64 bit off_t. E.g. by defining
  #define _LARGEFILE_SOURCE
  #define _FILE_OFFSET_BITS 64
or take special precautions to interface with the library by 64 bit integers
where this .h files prescribe off_t.

To prevent 64 bit file i/o in the library would keep the application from
processing tracks of more than 2 GB size.

*/
#include <sys/types.h>

#ifndef DOXYGEN

#if defined(__cplusplus)
#define BURN_BEGIN_DECLS \
	namespace burn { \
		extern "C" {
#define BURN_END_DECLS \
		} \
	}
#else
#define BURN_BEGIN_DECLS
#define BURN_END_DECLS
#endif

BURN_BEGIN_DECLS

#endif

/** References a physical drive in the system */
struct burn_drive;

/** References a whole disc */
struct burn_disc;

/** References a single session on a disc */
struct burn_session;

/** References a single track on a disc */
struct burn_track;

/* ts A61111 */
/** References a set of write parameters */
struct burn_write_opts;

/** Session format for normal audio or data discs */
#define BURN_CDROM	0
/** Session format for obsolete CD-I discs */
#define BURN_CDI	0x10
/** Session format for CDROM-XA discs */
#define BURN_CDXA	0x20

#define BURN_POS_END 100

/** Mask for mode bits */
#define BURN_MODE_BITS 127

/** Track mode - mode 0 data
    0 bytes of user data.  it's all 0s.  mode 0.  get it?  HAH
*/
#define BURN_MODE0		(1 << 0)
/** Track mode - mode "raw" - all 2352 bytes supplied by app
    FOR DATA TRACKS ONLY!
*/
#define BURN_MODE_RAW		(1 << 1)
/** Track mode - mode 1 data
    2048 bytes user data, and all the LEC money can buy
*/
#define BURN_MODE1		(1 << 2)
/** Track mode - mode 2 data
    defaults to formless, 2336 bytes of user data, unprotected
    | with a data form if required.
*/
#define BURN_MODE2		(1 << 3)
/** Track mode modifier - Form 1, | with MODE2 for reasonable results
    2048 bytes of user data, 4 bytes of subheader
*/
#define BURN_FORM1		(1 << 4)
/** Track mode modifier - Form 2, | with MODE2 for reasonable results
    lots of user data.  not much LEC.
*/
#define BURN_FORM2		(1 << 5)
/** Track mode - audio
    2352 bytes per sector.  may be | with 4ch or preemphasis.
    NOT TO BE CONFUSED WITH BURN_MODE_RAW
    Audio data must be 44100Hz 16bit stereo with no riff or other header at
    beginning.  Extra header data will cause pops or clicks.  Audio data should
    also be in little-endian byte order.  Big-endian audio data causes static.
*/
#define BURN_AUDIO		(1 << 6)
/** Track mode modifier - 4 channel audio. */
#define BURN_4CH		(1 << 7)
/** Track mode modifier - Digital copy permitted, can be set on any track.*/
#define BURN_COPY		(1 << 8)
/** Track mode modifier - 50/15uS pre-emphasis */
#define BURN_PREEMPHASIS	(1 << 9)
/** Input mode modifier - subcodes present packed 16 */
#define BURN_SUBCODE_P16	(1 << 10)
/** Input mode modifier - subcodes present packed 96 */
#define BURN_SUBCODE_P96	(1 << 11)
/** Input mode modifier - subcodes present raw 96 */
#define BURN_SUBCODE_R96	(1 << 12)

/* ts B11230 */
/** Track mode modifier - Serial Copy Management System, SAO only
    If this is set and BURN_COPY is not set, then copying the emerging
    track will be forbidden.
    @since 1.2.0
*/
#define BURN_SCMS		(1 << 13)


/** Possible disc writing style/modes */
enum burn_write_types
{
	/** Packet writing.
	      currently unsupported, (for DVD Incremental Streaming use TAO)
	*/
	BURN_WRITE_PACKET,

	/** With CD:                     Track At Once recording
	      2s gaps between tracks, no fonky lead-ins

	    With sequential DVD-R[W]:    Incremental Streaming
	    With DVD+R and BD-R:         Track of open size
	    With DVD-RAM, DVD+RW, BD-RE: Random Writeable (used sequentially)
	    With overwriteable DVD-RW:   Rigid Restricted Overwrite 
	*/
	BURN_WRITE_TAO,

	/** With CD:                     Session At Once
	      Block type MUST be BURN_BLOCK_SAO
	      ts A70122: Currently not capable of mixing data and audio tracks.

	    With sequential DVD-R[W]:    Disc-at-once, DAO
	      Single session, single track, fixed size mandatory, (-dvd-compat)
	    With other DVD or BD media:  same as BURN_WRITE_TAO but may demand
	                                 that track size is known in advance.
	*/
	BURN_WRITE_SAO,

	/** With CD: Raw disc at once recording.
	      all subcodes must be provided by lib or user
	      only raw block types are supported
	    With DVD and BD media: not supported.

	    ts A90901: This had been disabled because its implementation
	               relied on code from cdrdao which is not understood
	               currently.
	               A burn run will abort with "FATAL" error message
	               if this mode is attempted.
	               @since 0.7.2
	    ts A91016: Re-implemented according to ECMA-130 Annex A and B.
	               Now understood, explained and not stemming from cdrdao.
	               @since 0.7.4
	*/
	BURN_WRITE_RAW,

	/** In replies this indicates that not any writing will work.
	    As parameter for inquiries it indicates that no particular write
            mode shall is specified.
	    Do not use for setting a write mode for burning. It will not work.
	*/
	BURN_WRITE_NONE
};

/** Data format to send to the drive */
enum burn_block_types
{
	/** sync, headers, edc/ecc provided by lib/user */
	BURN_BLOCK_RAW0 = 1,
	/** sync, headers, edc/ecc and p/q subs provided by lib/user */
	BURN_BLOCK_RAW16 = 2,
	/** sync, headers, edc/ecc and packed p-w subs provided by lib/user */
	BURN_BLOCK_RAW96P = 4,
	/** sync, headers, edc/ecc and raw p-w subs provided by lib/user */
	BURN_BLOCK_RAW96R = 8,
	/** only 2048 bytes of user data provided by lib/user */
	BURN_BLOCK_MODE1 = 256,
	/** 2336 bytes of user data provided by lib/user */
	BURN_BLOCK_MODE2R = 512,
	/** 2048 bytes of user data provided by lib/user
	    subheader provided in write parameters
	    are we ever going to support this shit?  I vote no.
	    (supposed to be supported on all drives...)
	*/
	BURN_BLOCK_MODE2_PATHETIC = 1024,
	/** 2048 bytes of data + 8 byte subheader provided by lib/user
	    hey, this is also dumb
	*/
	BURN_BLOCK_MODE2_LAME = 2048,
	/** 2324 bytes of data provided by lib/user
	    subheader provided in write parameters
	    no sir, I don't like it.
	*/
	BURN_BLOCK_MODE2_OBSCURE = 4096,
	/** 2332 bytes of data supplied by lib/user
	    8 bytes sub header provided in write parameters
	    this is the second least suck mode2, and is mandatory for
	    all drives to support.
	*/
	BURN_BLOCK_MODE2_OK = 8192,
	/** SAO block sizes are based on cue sheet, so use this. */
	BURN_BLOCK_SAO = 16384
};

/** Possible status of the drive in regard to the disc in it. */
enum burn_disc_status
{
	/** The current status is not yet known */
	BURN_DISC_UNREADY,

	/** The drive holds a blank disc. It is ready for writing from scratch.
	    Unused multi-session media:
	      CD-R, CD-RW, DVD-R, DVD-RW, DVD+R, BD-R
	    Blanked multi-session media (i.e. treated by burn_disc_erase())
	      CD-RW, DVD-RW
	    Overwriteable media with or without valid data
	      DVD-RAM, DVD+RW, formatted DVD-RW, BD-RE
	*/
	BURN_DISC_BLANK,

	/** There is no disc at all in the drive */
	BURN_DISC_EMPTY,

	/** There is an incomplete disc in the drive. It is ready for appending
	    another session.
	    Written but not yet closed multi-session media
	      CD-R, CD-RW, DVD-R, DVD-RW, DVD+R, BD-R
	*/
	BURN_DISC_APPENDABLE,

	/** There is a disc with data on it in the drive. It is usable only for
	    reading.
	    Written and closed multi-session media
	      CD-R, CD-RW, DVD-R, DVD-RW, DVD+R, BD-R
	    Read-Only media
	      CD-ROM, DVD-ROM, BD-ROM
	    Note that many DVD-ROM drives report any written media
	    as Read-Only media and not by their real media types.
	*/
	BURN_DISC_FULL,

	/* ts A61007 */
        /* @since 0.2.4 */
	/** The drive was not grabbed when the status was inquired */
	BURN_DISC_UNGRABBED,

	/* ts A61020 */
        /* @since 0.2.6 */
	/** The media seems to be unsuitable for reading and for writing */
	BURN_DISC_UNSUITABLE
};


/** Possible data source return values */
enum burn_source_status
{
	/** The source is ok */
	BURN_SOURCE_OK,
	/** The source is at end of file */
	BURN_SOURCE_EOF,
	/** The source is unusable */
	BURN_SOURCE_FAILED
};


/** Possible busy states for a drive */
enum burn_drive_status
{
	/** The drive is not in an operation */
	BURN_DRIVE_IDLE,
	/** The library is spawning the processes to handle a pending
	    operation (A read/write/etc is about to start but hasn't quite
	    yet) */
	BURN_DRIVE_SPAWNING,
	/** The drive is reading data from a disc */
	BURN_DRIVE_READING,
	/** The drive is writing data to a disc */
	BURN_DRIVE_WRITING,
	/** The drive is writing Lead-In */
	BURN_DRIVE_WRITING_LEADIN,
	/** The drive is writing Lead-Out */
	BURN_DRIVE_WRITING_LEADOUT,
	/** The drive is erasing a disc */
	BURN_DRIVE_ERASING,
	/** The drive is being grabbed */
	BURN_DRIVE_GRABBING,

	/* ts A61102 */
        /* @since 0.2.6 */
	/** The drive gets written zeroes before the track payload data */
	BURN_DRIVE_WRITING_PREGAP,
	/** The drive is told to close a track (TAO only) */
	BURN_DRIVE_CLOSING_TRACK,
	/** The drive is told to close a session (TAO only) */
	BURN_DRIVE_CLOSING_SESSION,

	/* ts A61223 */
        /* @since 0.3.0 */
	/** The drive is formatting media */
	BURN_DRIVE_FORMATTING,

	/* ts A70822 */
        /* @since 0.4.0 */
	/** The drive is busy in synchronous read (if you see this then it
	    has been interrupted) */
	BURN_DRIVE_READING_SYNC,
	/** The drive is busy in synchronous write (if you see this then it
	    has been interrupted) */
	BURN_DRIVE_WRITING_SYNC
	
};

    
/** Information about a track on a disc - this is from the q sub channel of the
    lead-in area of a disc.  The documentation here is very terse.
    See a document such as mmc3 for proper information.

    CAUTION : This structure is prone to future extension !

    Do not restrict your application to unsigned char with any counter like
    "session", "point", "pmin", ...
    Do not rely on the current size of a burn_toc_entry. 

*/
struct burn_toc_entry
{
	/** Session the track is in */
	unsigned char session;
	/** Type of data.  for this struct to be valid, it must be 1 */
	unsigned char adr;
	/** Type of data in the track */
	unsigned char control;
	/** Zero.  Always.  Really. */
	unsigned char tno;
	/** Track number or special information */
	unsigned char point;
	unsigned char min;
	unsigned char sec;
	unsigned char frame;
	unsigned char zero;
	/** Track start time minutes for normal tracks */
	unsigned char pmin;
	/** Track start time seconds for normal tracks */
	unsigned char psec;
	/** Track start time frames for normal tracks */
	unsigned char pframe;

	/* Indicates whether extension data are valid and eventually override
	   older elements in this structure:
	     bit0= DVD extension is valid @since 0.3.2
                   @since 0.5.2 : DVD extensions are made valid for CD too
             bit1= LRA extension is valid @since 0.7.2
             bit2= Track status bits extension is valid @since 1.2.8
	*/
	unsigned char extensions_valid;  

	/* ts A70201 : DVD extension. extensions_valid:bit0
	   If invalid the members are guaranteed to be 0. */
        /* @since 0.3.2 */
	/* Tracks and session numbers are 16 bit. Here are the high bytes. */
	unsigned char session_msb;
	unsigned char point_msb;
	/* pmin, psec, and pframe may be too small if DVD extension is valid */
	int start_lba; 
	/* min, sec, and frame may be too small if DVD extension is valid */
	int track_blocks;
	
	/* ts A90909 : LRA extension. extensions_valid:bit1 */
	/* @since 0.7.2 */
	/* MMC-5 6.27.3.18 : The Last Recorded Address is valid for DVD-R,
	                  DVD-R DL when LJRS = 00b, DVD-RW, HD DVD-R, and BD-R.
	   This would mean profiles: 0x11, 0x15, 0x13, 0x14, 0x51, 0x41, 0x42 
	*/
	int last_recorded_address;

	/* ts B30112 : Track status bits extension. extensions_valid:bit2 */
	/* @since 1.2.8 */
	/* Names as of READ TRACK INFORMATION, MMC-5 6.27.3 :
	    bit0 -  bit3 = Track Mode
	    bit4         = Copy
	    bit5         = Damage
	    bit6 -  bit7 = LJRS
	    bit8 - bit11 = Data Mode
	   bit12         = FP
	   bit13         = Packet/Inc
	   bit14         = Blank
	   bit15         = RT
	   bit16         = NWA_V
	   bit17         = LRA_V
	*/
	int track_status_bits;

};


/** Data source interface for tracks.
    This allows to use arbitrary program code as provider of track input data.

    Objects compliant to this interface are either provided by the application
    or by API calls of libburn: burn_fd_source_new() , burn_file_source_new(),
    and burn_fifo_source_new().

    The API calls allow to use any file object as data source. Consider to feed
    an eventual custom data stream asynchronously into a pipe(2) and to let
    libburn handle the rest. 
    In this case the following rule applies:
    Call burn_source_free() exactly once for every source obtained from
    libburn API. You MUST NOT otherwise use or manipulate its components.

    In general, burn_source objects can be freed as soon as they are attached
    to track objects. The track objects will keep them alive and dispose them
    when they are no longer needed. With a fifo burn_source it makes sense to
    keep the own reference for inquiring its state while burning is in
    progress.

    ---

    The following description of burn_source applies only to application
    implemented burn_source objects. You need not to know it for API provided
    ones.

    If you really implement an own passive data producer by this interface,
    then beware: it can do anything and it can spoil everything.

    In this case the functions (*read), (*get_size), (*set_size), (*free_data)
    MUST be implemented by the application and attached to the object at
    creation time.
    Function (*read_sub) is allowed to be NULL or it MUST be implemented and
    attached.

    burn_source.refcount MUST be handled properly: If not exactly as many
    references are freed as have been obtained, then either memory leaks or
    corrupted memory are the consequence.
    All objects which are referred to by *data must be kept existent until
    (*free_data) is called via burn_source_free() by the last referer.
*/
struct burn_source {

	/** Reference count for the data source. MUST be 1 when a new source
            is created and thus the first reference is handed out. Increment
            it to take more references for yourself. Use burn_source_free()
            to destroy your references to it. */
	int refcount;


	/** Read data from the source. Semantics like with read(2), but MUST
	    either deliver the full buffer as defined by size or MUST deliver
	    EOF (return 0) or failure (return -1) at this call or at the
	    next following call. I.e. the only incomplete buffer may be the
	    last one from that source.
	    libburn will read a single sector by each call to (*read).
	    The size of a sector depends on BURN_MODE_*. The known range is
	    2048 to 2352.

            If this call is reading from a pipe then it will learn
            about the end of data only when that pipe gets closed on the
            feeder side. So if the track size is not fixed or if the pipe
            delivers less than the predicted amount or if the size is not
            block aligned, then burning will halt until the input process
            closes the pipe.

	    IMPORTANT:
	    If this function pointer is NULL, then the struct burn_source is of
	    version >= 1 and the job of .(*read)() is done by .(*read_xt)().
	    See below, member .version.
	*/
	int (*read)(struct burn_source *, unsigned char *buffer, int size);


	/** Read subchannel data from the source (NULL if lib generated) 
	    WARNING: This is an obscure feature with CD raw write modes.
	    Unless you checked the libburn code for correctness in that aspect
	    you should not rely on raw writing with own subchannels.
	    ADVICE: Set this pointer to NULL.
	*/
	int (*read_sub)(struct burn_source *, unsigned char *buffer, int size);


	/** Get the size of the source's data. Return 0 means unpredictable
	    size. If application provided (*get_size) allows return 0, then
	    the application MUST provide a fully functional (*set_size).
	*/
	off_t (*get_size)(struct burn_source *); 


	/* ts A70125 : BROKE BINARY BACKWARD COMPATIBILITY AT libburn-0.3.1. */
        /* @since 0.3.2 */
	/** Program the reply of (*get_size) to a fixed value. It is advised
	    to implement this by a attribute  off_t fixed_size;  in *data .
	    The read() function does not have to take into respect this fake
	    setting. It is rather a note of libburn to itself. Eventually
	    necessary truncation or padding is done in libburn. Truncation
	    is usually considered a misburn. Padding is considered ok.

	    libburn is supposed to work even if (*get_size) ignores the
            setting by (*set_size). But your application will not be able to
	    enforce fixed track sizes by  burn_track_set_size() and possibly
	    even padding might be left out.
	*/
	int (*set_size)(struct burn_source *source, off_t size);


	/** Clean up the source specific data. This function will be called
	    once by burn_source_free() when the last referer disposes the
	    source.
	*/
	void (*free_data)(struct burn_source *);


	/** Next source, for when a source runs dry and padding is disabled
	    WARNING: This is an obscure feature. Set to NULL at creation and
	             from then on leave untouched and uninterpreted.
	*/
	struct burn_source *next;


	/** Source specific data. Here the various source classes express their
	    specific properties and the instance objects store their individual
	    management data.
            E.g. data could point to a struct like this:
		struct app_burn_source
		{
			struct my_app *app_handle;
			... other individual source parameters ...
			off_t fixed_size;
		};

	    Function (*free_data) has to be prepared to clean up and free
	    the struct.
	*/
	void *data;


	/* ts A71222 : Supposed to be binary backwards compatible extension. */
        /* @since 0.4.2 */
	/** Valid only if above member .(*read)() is NULL. This indicates a
	    version of struct burn_source younger than 0.
	    From then on, member .version tells which further members exist
	    in the memory layout of struct burn_source. libburn will only touch
	    those announced extensions.

	    Versions:
	     0  has .(*read)() != NULL, not even .version is present.
             1  has .version, .(*read_xt)(), .(*cancel)()
	*/
	int version;

	/** This substitutes for (*read)() in versions above 0. */
	int (*read_xt)(struct burn_source *, unsigned char *buffer, int size);

	/** Informs the burn_source that the consumer of data prematurely
	    ended reading. This call may or may not be issued by libburn
	    before (*free_data)() is called.
	*/
	int (*cancel)(struct burn_source *source);
};


/** Information on a drive in the system */
struct burn_drive_info
{
	/** Name of the vendor of the drive */
	char vendor[9];
	/** Name of the drive */
	char product[17];
	/** Revision of the drive */
	char revision[5];

	/** Invalid: Was: "Location of the drive in the filesystem." */
	/** This string has no meaning any more. Once it stored the drive
            device file address. Now always use function burn_drive_d_get_adr()
            to inquire a device file address.            ^^^^^ ALWAYS ^^^^^^^*/
	char location[17];

	/** Can the drive read DVD-RAM discs */
	unsigned int read_dvdram:1;
	/** Can the drive read DVD-R discs */
	unsigned int read_dvdr:1;
	/** Can the drive read DVD-ROM discs */
	unsigned int read_dvdrom:1;
	/** Can the drive read CD-R discs */
	unsigned int read_cdr:1;
	/** Can the drive read CD-RW discs */
	unsigned int read_cdrw:1;

	/** Can the drive write DVD-RAM discs */
	unsigned int write_dvdram:1;
	/** Can the drive write DVD-R discs */
	unsigned int write_dvdr:1;
	/** Can the drive write CD-R discs */
	unsigned int write_cdr:1;
	/** Can the drive write CD-RW discs */
	unsigned int write_cdrw:1;

	/** Can the drive simulate a write */
	unsigned int write_simulate:1;

	/** Can the drive report C2 errors */
	unsigned int c2_errors:1;

	/** The size of the drive's buffer (in kilobytes) */
	int buffer_size;
	/** 
	 * The supported block types in tao mode.
	 * They should be tested with the desired block type.
	 * See also burn_block_types.
	 */
	int tao_block_types;
	/** 
	 * The supported block types in sao mode.
	 * They should be tested with the desired block type.
	 * See also burn_block_types.
	 */
	int sao_block_types;
	/** 
	 * The supported block types in raw mode.
	 * They should be tested with the desired block type.
	 * See also burn_block_types.
	 */
	int raw_block_types;
	/** 
	 * The supported block types in packet mode.
	 * They should be tested with the desired block type.
	 * See also burn_block_types.
	 */
	int packet_block_types;

	/** The value by which this drive can be indexed when using functions
	    in the library. This is the value to pass to all libbburn functions
	    that operate on a drive. */
	struct burn_drive *drive;
};


/** Operation progress report. All values are 0 based indices. 
 * */
struct burn_progress {
	/** The total number of sessions */
	int sessions;
	/** Current session.*/
	int session;
	/** The total number of tracks */
	int tracks;
	/** Current track. */
	int track;
	/** The total number of indices */
	int indices;
	/** Curent index. */
	int index;
	/** The starting logical block address */
	int start_sector;
	/** On write: The number of sectors.
	    On blank: 0x10000 as upper limit for relative progress steps */
	int sectors;
	/** On write: The current sector being processed.
	    On blank: Relative progress steps 0 to 0x10000 */
	int sector;

	/* ts A61023 */
        /* @since 0.2.6 */
	/** The capacity of the drive buffer */
	unsigned buffer_capacity;
	/** The free space in the drive buffer (might be slightly outdated) */
	unsigned buffer_available;

	/* ts A61119 */
        /* @since 0.2.6 */
	/** The number of bytes sent to the drive buffer */
	off_t buffered_bytes;
	/** The minimum number of bytes stored in buffer during write.
            (Caution: Before surely one buffer size of bytes was processed,
                      this value is 0xffffffff.) 
	*/
	unsigned buffer_min_fill;
};


/* ts A61226 */
/* @since 0.3.0 */
/** Description of a speed capability as reported by the drive in conjunction
    with eventually loaded media. There can be more than one such object per
    drive. So they are chained via .next and .prev , where NULL marks the end
    of the chain. This list is set up by burn_drive_scan() and gets updated
    by burn_drive_grab().
    A copy may be obtained by burn_drive_get_speedlist() and disposed by
    burn_drive_free_speedlist().
    For technical background info see SCSI specs MMC and SPC:
    mode page 2Ah (from SPC 5Ah MODE SENSE) , mmc3r10g.pdf , 6.3.11 Table 364
    ACh GET PERFORMANCE, Type 03h , mmc5r03c.pdf , 6.8.5.3 Table 312
*/
struct burn_speed_descriptor {

	/** Where this info comes from : 
	    0 = misc , 1 = mode page 2Ah , 2 = ACh GET PERFORMANCE */
	int source;

	/** The media type that was current at the time of report
	    -2 = state unknown, -1 = no media was loaded , else see
	    burn_disc_get_profile() */
	int profile_loaded;
	char profile_name[80];

	/** The attributed capacity of appropriate media in logical block units
	    i.e. 2352 raw bytes or 2048 data bytes. -1 = capacity unknown. */
	int end_lba;

	/** Speed is given in 1000 bytes/s , 0 = invalid. The numbers
	    are supposed to be usable with burn_drive_set_speed() */
	int write_speed;
	int read_speed;

	/** Expert info from ACh GET PERFORMANCE and/or mode page 2Ah.
	    Expect values other than 0 or 1 to get a meaning in future.*/
	/* Rotational control: 0 = CLV/default , 1 = CAV */
	int wrc;
	/* 1 = drive promises reported performance over full media */
	int exact;
	/* 1 = suitable for mixture of read and write */
	int mrw;

	/** List chaining. Use .next until NULL to iterate over the list */
	struct burn_speed_descriptor *prev;
	struct burn_speed_descriptor *next;
};


/** Initialize the library.
    This must be called before using any other functions in the library. It
    may be called more than once with no effect.
    It is possible to 'restart' the library by shutting it down and
    re-initializing it. Once this was necessary if you follow the older and
    more general way of accessing a drive via burn_drive_scan() and
    burn_drive_grab(). See burn_drive_scan_and_grab() with its strong
    urges and its explanations.
    @return Nonzero if the library was able to initialize; zero if
            initialization failed.
*/
int burn_initialize(void);

/** Shutdown the library.
    This should be called before exiting your application. Make sure that all
    drives you have grabbed are released <i>before</i> calling this.
*/
void burn_finish(void);


/* ts A61002 */
/** Abort any running drive operation and eventually call burn_finish().

    You MUST shut down the busy drives if an aborting event occurs during a
    burn run. For that you may call this function either from your own signal
    handling code or indirectly by activating the built-in signal handling:
      burn_set_signal_handling("my_app_name : ", NULL, 0);
    Else you may eventually call burn_drive_cancel() on the active drives and
    wait for them to assume state BURN_DRIVE_IDLE.
    @param patience      Maximum number of seconds to wait for drives to
                         finish.
                         @since 0.7.8 :
                         If this is -1, then only the cancel operations will
                         be performed and no burn_finish() will happen.
    @param pacifier_func If not NULL: a function to produce appeasing messages.
                         See burn_abort_pacifier() for an example.
    @param handle        Opaque handle to be used with pacifier_func
    @return 1  ok, all went well
            0  had to leave a drive in unclean state
            <0 severe error, do no use libburn again
    @since 0.2.6
*/
int burn_abort(int patience, 
               int (*pacifier_func)(void *handle, int patience, int elapsed),
               void *handle);

/** A pacifier function suitable for burn_abort.
    @param handle If not NULL, a pointer to a text suitable for printf("%s")
    @param patience Maximum number of seconds to wait
    @param elapsed  Elapsed number of seconds
*/
int burn_abort_pacifier(void *handle, int patience, int elapsed);


/** ts A61006 : This is for development only. Not suitable for applications.
    Set the verbosity level of the library. The default value is 0, which means
    that nothing is output on stderr. The more you increase this, the more
    debug output should be displayed on stderr for you.
    @param level The verbosity level desired. 0 for nothing, higher positive
                 values for more information output.
*/
void burn_set_verbosity(int level);

/* ts A91111 */
/** Enable resp. disable logging of SCSI commands.
    This call can be made at any time - even before burn_initialize().
    It is in effect for all active drives and currently not very thread
    safe for multiple drives.
    @param flag  Bitfield for control purposes. The default is 0.
                 bit0= log to file /tmp/libburn_sg_command_log
                 bit1= log to stderr
                 bit2= flush output after each line
    @since 0.7.4
*/
void burn_set_scsi_logging(int flag);

/* ts A60813 */
/** Set parameters for behavior on opening device files. To be called early
    after burn_initialize() and before any bus scan. But not mandatory at all.
    Parameter value 1 enables a feature, 0 disables.  
    Default is (1,0,0). Have a good reason before you change it.
    @param exclusive
                     0 = no attempt to make drive access exclusive.
                     1 = Try to open only devices which are not marked as busy
                     and try to mark them busy if opened sucessfully. (O_EXCL
                     on GNU/Linux , flock(LOCK_EX) on FreeBSD.)
                     2 = in case of a SCSI device, also try to open exclusively
                         the matching /dev/sr, /dev/scd and /dev/st .
                     One may select a device SCSI file family by adding
                      0 = default family
                      4 = /dev/sr%d
                      8 = /dev/scd%d
                     16 = /dev/sg%d
                     Do not use other values !
                     Add 32 to demand on GNU/Linux an exclusive lock by
                     fcntl(,F_SETLK,) after open() has succeeded.
    @param blocking  Try to wait for drives which do not open immediately but
                     also do not return an error as well. (O_NONBLOCK)
                     This might stall indefinitely with /dev/hdX hard disks.
    @param abort_on_busy  Unconditionally abort process when a non blocking
                          exclusive opening attempt indicates a busy drive.
                          Use this only after thorough tests with your app.
    @since 0.2.2
*/
void burn_preset_device_open(int exclusive, int blocking, int abort_on_busy);


/* ts A70223 */
/** Allows the use of media types which are implemented in libburn but not yet
    tested. The list of those untested profiles is subject to change.
             - Currently no media types are under test reservation -
    If you really test such media, then please report the outcome on
    libburn-hackers@pykix.org
    If ever then this call should be done soon after burn_initialize() before
    any drive scanning.
    @param yes 1=allow all implemented profiles, 0=only tested media (default)
    @since 0.3.4
*/
void burn_allow_untested_profiles(int yes);


/* ts A60823 */
/** Aquire a drive with known device file address.

    This is the sysadmin friendly way to open one drive and to leave all
    others untouched. It bundles the following API calls to form a
    non-obtrusive way to use libburn:
      burn_drive_add_whitelist() , burn_drive_scan() , burn_drive_grab()
    You are *strongly urged* to use this call whenever you know the drive
    address in advance.

    If not, then you have to use directly above calls. In that case, you are
    *strongly urged* to drop any unintended drive which will be exclusively
    occupied and not closed by burn_drive_scan().
    This can be done by shutting down the library including a call to
    burn_finish(). You may later start a new libburn session and should then
    use the function described here with an address obtained after
    burn_drive_scan() via burn_drive_d_get_adr(drive_infos[driveno].drive,adr).
    Another way is to drop the unwanted drives by burn_drive_info_forget().

    Operating on multiple drives:

    Different than with burn_drive_scan() it is allowed to call
    burn_drive_scan_and_grab() without giving up any other scanned drives. So
    this call can be used to get a collection of more than one aquired drives.
    The attempt to aquire the same drive twice will fail, though.

    Pseudo-drives:

    burn_drive_scan_and_grab() is able to aquire virtual drives which will
    accept options much like a MMC burner drive. Many of those options will not
    cause any effect, though. The address of a pseudo-drive begins with
    prefix "stdio:" followed by a path.
    Examples:  "stdio:/tmp/pseudo_drive" , "stdio:/dev/null" , "stdio:-"

    If the path is empty, the result is a null-drive = drive role 0.
    It pretends to have loaded no media and supports no reading or writing.

    If the path leads to an existing regular file, or to a not yet existing
    file, or to an existing block device, then the result is a random access
    stdio-drive capable of reading and writing = drive role 2.

    If the path leads to an existing file of any type other than directory,
    then the result is a sequential write-only stdio-drive = drive role 3.

    The special address form "stdio:/dev/fd/{number}" is interpreted literally
    as reference to open file descriptor {number}. This address form coincides
    with real files on some systems, but it is in fact hardcoded in libburn.
    Special address "stdio:-" means stdout = "stdio:/dev/fd/1".
    The role of such a drive is determined by the file type obtained via
    fstat({number}).
   
    Roles 2 and 3 perform all their eventual data transfer activities on a file
    via standard i/o functions open(2), lseek(2), read(2), write(2), close(2).
    The media profile is reported as 0xffff. Write space information from those
    media is not necessarily realistic.

    The capabilities of role 2 resemble DVD-RAM but it can simulate writing.
    If the path does not exist in the filesystem yet, it is attempted to create
    it as a regular file as soon as write operations are started.

    The capabilities of role 3 resemble a blank DVD-R. Nevertheless each
    burn_disc_write() run may only write a single track.

    One may distinguish pseudo-drives from MMC drives by call
    burn_drive_get_drive_role().

    @param drive_infos On success returns a one element array with the drive
                  (cdrom/burner). Thus use with driveno 0 only. On failure
                  the array has no valid elements at all.
                  The returned array should be freed via burn_drive_info_free()
                  when it is no longer needed.
                  This is a result from call burn_drive_scan(). See there.
                  Use with driveno 0 only.
    @param adr    The device file address of the desired drive. Either once
                  obtained by burn_drive_d_get_adr() or composed skillfully by
                  application resp. its user. E.g. "/dev/sr0".
                  Consider to preprocess it by burn_drive_convert_fs_adr().
    @param load   Nonzero to make the drive attempt to load a disc (close its
                  tray door, etc).
    @return       1 = success , 0 = drive not found , -1 = other error
    @since 0.2.2
*/    
int burn_drive_scan_and_grab(struct burn_drive_info *drive_infos[],
                             char* adr, int load);


/* ts A51221 */
/* @since 0.2.2 */
/** Maximum number of particularly permissible drive addresses */
#define BURN_DRIVE_WHITELIST_LEN 255

/** Add a device to the list of permissible drives. As soon as some entry is in
    the whitelist all non-listed drives are banned from scanning.
    @return 1 success, <=0 failure
    @since 0.2.2
*/
int burn_drive_add_whitelist(char *device_address);

/** Remove all drives from whitelist. This enables all possible drives. */
void burn_drive_clear_whitelist(void);


/** Scan for drives. This function MUST be called until it returns nonzero.
    In case of re-scanning:
    All pointers to struct burn_drive and all struct burn_drive_info arrays
    are invalidated by using this function. Do NOT store drive pointers across
    calls to this function !
    To avoid invalid pointers one MUST free all burn_drive_info arrays
    by burn_drive_info_free() before calling burn_drive_scan() a second time.
    If there are drives left, then burn_drive_scan() will refuse to work.

    After this call all drives depicted by the returned array are subject
    to eventual (O_EXCL) locking. See burn_preset_device_open(). This state
    ends either with burn_drive_info_forget() or with burn_drive_release().
    It is unfriendly to other processes on the system to hold drives locked
    which one does not definitely plan to use soon.
    @param drive_infos Returns an array of drive info items (cdroms/burners).
                  The returned array must be freed by burn_drive_info_free()
                  before burn_finish(), and also before calling this function
                  burn_drive_scan() again.
    @param n_drives Returns the number of drive items in drive_infos.
    @return 0 while scanning is not complete
            >0 when it is finished sucessfully,
            <0 when finished but failed.
*/
int burn_drive_scan(struct burn_drive_info *drive_infos[],
		    unsigned int *n_drives);

/* ts A60904 : ticket 62, contribution by elmom */
/** Release memory about a single drive and any exclusive lock on it.
    Become unable to inquire or grab it. Expect FATAL consequences if you try.
    @param drive_info pointer to a single element out of the array
                      obtained from burn_drive_scan() : &(drive_infos[driveno])
    @param force controls degree of permissible drive usage at the moment this
                 function is called, and the amount of automatically provided
                 drive shutdown : 
                  0= drive must be ungrabbed and BURN_DRIVE_IDLE
                  1= try to release drive resp. accept BURN_DRIVE_GRABBING 
                 Use these two only. Further values are to be defined.
    @return 1 on success, 2 if drive was already forgotten,
            0 if not permissible, <0 on other failures, 
    @since 0.2.2
*/
int burn_drive_info_forget(struct burn_drive_info *drive_info, int force);


/** When no longer needed, free a whole burn_drive_info array which was
    returned by burn_drive_scan().
    For freeing single drive array elements use burn_drive_info_forget().
*/
void burn_drive_info_free(struct burn_drive_info drive_infos[]);


/* ts A60823 */
/* @since 0.2.2 */
/** Maximum length+1 to expect with a drive device file address string */
#define BURN_DRIVE_ADR_LEN 1024

/* ts A70906 */
/** Inquire the device file address of the given drive.
    @param drive The drive to inquire.
    @param adr   An application provided array of at least BURN_DRIVE_ADR_LEN
                 characters size. The device file address gets copied to it.
    @return >0 success , <=0 error (due to libburn internal problem)
    @since 0.4.0
*/
int burn_drive_d_get_adr(struct burn_drive *drive, char adr[]);

/* A60823 */
/** Inquire the device file address of a drive via a given drive_info object.
    (Note: This is a legacy call.)
    @param drive_info The drive to inquire.Usually some &(drive_infos[driveno])
    @param adr   An application provided array of at least BURN_DRIVE_ADR_LEN
                 characters size. The device file address gets copied to it.
    @return >0 success , <=0 error (due to libburn internal problem)
    @since 0.2.6
*/
int burn_drive_get_adr(struct burn_drive_info *drive_info, char adr[]);


/* ts A60922 ticket 33 */
/** Evaluate whether the given address would be a drive device file address
    which could be listed by a run of burn_drive_scan(). No check is made
    whether a device file with this address exists or whether it leads
    to a usable MMC drive.
    @return 1 means yes, 0 means no
    @since 0.2.6
*/
int burn_drive_is_enumerable_adr(char *adr);

/* ts A60922 ticket 33 */
/** Try to convert a given existing filesystem address into a drive device file
    address. This succeeds with symbolic links or if a hint about the drive's
    system address can be read from the filesystem object and a matching drive
    is found.
    @param path The address of an existing file system object
    @param adr  An application provided array of at least BURN_DRIVE_ADR_LEN
                characters size. The device file address gets copied to it.
    @return     1 = success , 0 = failure , -1 = severe error
    @since 0.2.6
*/
int burn_drive_convert_fs_adr(char *path, char adr[]);

/* ts A60923 */
/** Try to convert a given SCSI address of bus,host,channel,target,lun into
    a drive device file address. If a SCSI address component parameter is < 0
    then it is not decisive and the first enumerated address which matches
    the >= 0 parameters is taken as result.
    Note: bus and (host,channel) are supposed to be redundant.
    @param bus_no "Bus Number" (something like a virtual controller)
    @param host_no "Host Number" (something like half a virtual controller)
    @param channel_no "Channel Number" (other half of "Host Number")
    @param target_no "Target Number" or "SCSI Id" (a device)
    @param lun_no "Logical Unit Number" (a sub device)
    @param adr  An application provided array of at least BURN_DRIVE_ADR_LEN
                characters size. The device file address gets copied to it.
    @return     1 = success , 0 = failure , -1 = severe error
    @since 0.2.6
*/
int burn_drive_convert_scsi_adr(int bus_no, int host_no, int channel_no,
				 int target_no, int lun_no, char adr[]);

/* ts B10728 */
/** Try to convert a given drive device file address into the address of a
    symbolic link that points to this drive address.
    Modern GNU/Linux systems may shuffle drive addresses from boot to boot.
    The udev daemon is supposed to create links which always point to the
    same drive, regardless of its system address.
    This call tries to find such links.
    @param dev_adr     Should contain a drive address as returned by
                       burn_drive_scan().
    @param link_adr    An application provided array of at least
                       BURN_DRIVE_ADR_LEN characters size. The found link
                       address gets copied to it.
    @param dir_adr     The address of the directory where to look for links.
                       Normally: "/dev"
    @param templ       An array of pointers to name templates, which
                       links have to match. A symbolic link in dir_adr matches
                       a name template if it begins by that text. E.g.
                       link address "/dev/dvdrw1" matches template "dvdrw".
                       If templ is NULL, then the default array gets used:
                        {"dvdrw", "cdrw", "dvd", "cdrom", "cd"}
                       If several links would match, then a link will win,
                       which matches the template with the lowest array index.
                       Among these candidates, the one with the lowest strcmp()
                       rank will be chosen as link_adr.
    @param num_templ   Number of array elements in templ.
    @param flag        Bitfield for control purposes. Unused yet. Submit 0.
    @return            <0 severe error, 0 failed to search, 2 nothing found
                       1 success, link_adr is valid
    @since 1.1.4
*/
int burn_lookup_device_link(char *dev_adr, char link_adr[],
                         char *dir_adr, char **templ, int num_templ, int flag);

/* ts A60923 - A61005 */
/** Try to obtain bus,host,channel,target,lun from path. If there is an SCSI
    address at all, then this call should succeed with a drive device file
    address obtained via burn_drive_d_get_adr(). It is also supposed to
    succeed with any device file of a (possibly emulated) SCSI device.
    @return     1 = success , 0 = failure , -1 = severe error
    @since 0.2.6
*/
int burn_drive_obtain_scsi_adr(char *path, int *bus_no, int *host_no,
				int *channel_no, int *target_no, int *lun_no);

/** Grab a drive. This must be done before the drive can be used (for reading,
    writing, etc).
    @param drive The drive to grab. This is found in a returned
                 burn_drive_info struct.
    @param load Nonzero to make the drive attempt to load a disc (close its
                tray door, etc).
    @return 1 if it was possible to grab the drive, else 0
*/
int burn_drive_grab(struct burn_drive *drive, int load);

/* ts B00114 */
/* Probe available CD write modes and block types. In earlier versions this
   was done unconditionally on drive examination or aquiration. But it is
   lengthy and obtrusive, up to spoiling burn runs on the examined drives.
   So now this probing is omitted by default. All drives which announce to be
   capable of CD or DVD writing, get blindly attributed the capability for
   SAO and TAO. Applications which are interested in RAW modes or want to
   rely on the traditional write mode information, may use this call.
   @param drive_info  drive object to be inquired
   @return            >0 indicates success, <=0 means failure
   @since 0.7.6
*/
int burn_drive_probe_cd_write_modes(struct burn_drive_info *drive_info);

/* ts A90824 */
/** Calm down or alert a drive. Some drives stay alert after reading for
    quite some time. This saves time with the startup for the next read
    operation but also causes noise and consumes extra energy. It makes
    sense to calm down the drive if no read operation is expected for the
    next few seconds. The drive will get alert automatically if operations
    are required.
    @param d      The drive to influence.
    @param flag   Bitfield for control purposes
                  bit0= become alert (else start snoozing)
                        This is not mandatory to allow further drive operations
    @return       1= success , 0= drive role not suitable for calming
    @since 0.7.0
*/
int burn_drive_snooze(struct burn_drive *d, int flag);


/** Re-assess drive and media status. This should be done after a drive
    underwent a status change and shall be further used without intermediate
    burn_drive_release(), burn_drive_grab(). E.g. after blanking or burning.
    @param drive  The already grabbed drive to re-assess.
    @param flag   Unused yet. Submit 0.
    @return       1 success , <= 0 could not determine drive and media state
    @since 1.1.8
*/
int burn_drive_re_assess(struct burn_drive *d, int flag);


/** Release a drive. This should not be done until the drive is no longer
    busy (see burn_drive_get_status).
	@param drive The drive to release.
	@param eject Nonzero to make the drive eject the disc in it.
*/
void burn_drive_release(struct burn_drive *drive, int eject);


/* ts A70918 */
/** Like burn_drive_release() but keeping the drive tray closed and its
    eject button disabled. This physically locked drive state will last until
    the drive is grabbed again and released via burn_drive_release().
    Programs like eject, cdrecord, growisofs will break that ban too.
    @param d    The drive to release and leave locked.
    @param flag Bitfield for control purposes (unused yet, submit 0)
    @return 1 means success, <=0 means failure
    @since 0.4.0
*/
int burn_drive_leave_locked(struct burn_drive *d, int flag);


/** Returns what kind of disc a drive is holding. This function may need to be
    called more than once to get a proper status from it. See burn_disc_status
    for details.
    @param drive The drive to query for a disc.
    @return The status of the drive, or what kind of disc is in it.
            Note: BURN_DISC_UNGRABBED indicates wrong API usage
*/
enum burn_disc_status burn_disc_get_status(struct burn_drive *drive);


/* ts A61020 */
/** WARNING: This revives an old bug-like behavior that might be dangerous.
    Sets the drive status to BURN_DISC_BLANK if it is BURN_DISC_UNREADY
    or BURN_DISC_UNSUITABLE. Thus marking media as writable which actually
    failed to declare themselves either blank or (partially) filled.
    @return 1 drive status has been set , 0 = unsuitable drive status
    @since 0.2.6
*/
int burn_disc_pretend_blank(struct burn_drive *drive);


/* ts A61106 */
/** WARNING: This overrides the safety measures against unsuitable media.
    Sets the drive status to BURN_DISC_FULL if it is BURN_DISC_UNREADY
    or BURN_DISC_UNSUITABLE. Thus marking media as blankable which actually
    failed to declare themselves either blank or (partially) filled.
    @since 0.2.6
*/
int burn_disc_pretend_full(struct burn_drive *drive);


/* ts A61021 */
/** Reads ATIP information from inserted media. To be obtained via
    burn_drive_get_write_speed(), burn_drive_get_min_write_speed(),
    burn_drive_get_start_end_lba(). The drive must be grabbed for this call.
    @param drive The drive to query.
    @return 1=sucess, 0=no valid ATIP info read, -1 severe error
    @since 0.2.6
*/
int burn_disc_read_atip(struct burn_drive *drive);


/* ts A61020 */
/** Returns start and end lba of the media which is currently inserted
    in the given drive. The drive has to be grabbed to have hope for reply.
    Shortcomming (not a feature): unless burn_disc_read_atip() was called 
    only blank media will return valid info.
    @param drive The drive to query.
    @param start_lba Returns the start lba value
    @param end_lba Returns the end lba value
    @param flag Bitfield for control purposes (unused yet, submit 0)
    @return 1 if lba values are valid , 0 if invalid
    @since 0.2.6
*/
int burn_drive_get_start_end_lba(struct burn_drive *drive,
                                 int *start_lba, int *end_lba, int flag);


/* ts A90902 */
/** Guess the manufacturer name of CD media from the ATIP addresses of lead-in
    and lead-out. (Currently only lead-in is interpreted. Lead-out may in
    future be used to identify the media type in more detail.)
    The parameters of this call should be obtained by burn_disc_read_atip(d),
    burn_drive_get_start_end_lba(d, &start_lba, &end_lba, 0),
    burn_lba_to_msf(start_lba, &m_li, &s_li, &f_li) and
    burn_lba_to_msf(end_lba, &m_lo, &s_lo, &f_lo).
    @param m_li  "minute" part of ATIP lead-in resp. start_lba
    @param s_li  "second" of lead-in resp. start_lba
    @param f_li  "frame" of lead-in
    @param m_lo  "minute" part of ATIP lead-out
    @param s_lo  "second" of lead-out
    @param f_lo  "frame" of lead-out
    @param flag  Bitfield for control purposes,
                 bit0= append a text "(aka ...)" to reply if other brands or
                       vendor names are known.
    @return      Printable text or NULL on memory shortage.
                 Dispose by free() when no longer needed.
    @since 0.7.2
*/
char *burn_guess_cd_manufacturer(int m_li, int s_li, int f_li,
                                 int m_lo, int s_lo, int f_lo, int flag);

/* ts A90909 */
/** Retrieve some media information which is mainly specific to CD. For other
    media only the bits in reply parameter valid are supposed to be meaningful.
    @param d         The drive to query.
    @param disc_type A string saying either "CD-DA or CD-ROM", or "CD-I",
                     or ""CD-ROM XA", or "undefined".
    @param disc_id   A 32 bit number read from the media. (Meaning unclear yet)
    @param bar_code  8 hex digits from a barcode on media read by the drive
                     (if the drive has a bar code reader built in).
    @param app_code  The Host Application Code which must be set in the Write
                     Parameters Page if the media is not unrestricted (URU==0).
    @param valid     Replies bits which indicate the validity of other reply
                     parameters or the state of certain CD info bits:
                     bit0= disc_type is valid
                     bit1= disc_id is valid
                     bit2= bar_code is valid
                     bit3= disc_app_code is valid
                     bit4= Disc is unrestricted (URU bit, 51h READ DISC INFO)
                           This seems to be broken with my drives. The bit is
                           0 and the validity bit for disc_app_code is 0 too.
                     bit5= Disc is nominally erasable (Erasable bit)
                           This will be set with overwriteable media which
                           libburn normally considers to be unerasable blank.
    @return          1 success, <= 0 an error occured
    @since 0.7.2
*/
int burn_disc_get_cd_info(struct burn_drive *d, char disc_type[80],
                        unsigned int *disc_id, char bar_code[9], int *app_code,
			int *valid);

/* ts B11201 */
/** Read the array of CD-TEXT packs from the Lead-in of an audio CD.
    Each pack consists of 18 bytes, of which 4 are header. 12 bytes are pieces
    of 0-terminated texts or binary data. 2 bytes hold a CRC.
    For a description of the format of the array, see file doc/cdtext.txt.
    @param d          The drive to query.
    @param text_packs  Will point to an allocated memory buffer with CD-TEXT.
                      It will only contain text packs, and not be prepended
                      by the TOC header of four bytes, which gets stored with
                      file cdtext.dat by cdrecord -vv -toc. (The first two of
                      these bytes are supposed to hold the number of CD-TEXT
                      bytes + 2. The other two bytes are supposed to be 0.)
                      Dispose this buffer by free(), when no longer needed.
    @param num_packs  Will tell the number of text packs, i.e. the number of
                      bytes in text_packs divided by 18.
    @param flag       Bitfield for control purposes,
                      Unused yet. Submit 0.
    @return           1 success, 0= no CD-TEXT found, < 0 an error occured
    @since 1.2.0
*/
int burn_disc_get_leadin_text(struct burn_drive *d,
                              unsigned char **text_packs, int *num_packs,
                              int flag);

/* ts B00924 */
/** Read the current usage of the eventual BD Spare Area. This area gets
    reserved on BD media during formatting. During writing it is used to
    host replacements of blocks which failed the checkread immediately after
    writing.
    This call applies only to recordable BD media. I.e. profiles 0x41 to 0x43.
    @param d            The drive to query.
    @param alloc_blocks Returns the number of blocks reserved as Spare Area
    @param free_blocks  Returns the number of yet unused blocks in that area
    @param flag         Bitfield for control purposes (unused yet, submit 0)
    @return             1 = reply prarameters are valid,
                        <=0 = reply is invalid (e.g. because no BD profile)
    @since 0.8.8
*/
int burn_disc_get_bd_spare_info(struct burn_drive *d,
                                int *alloc_blocks, int *free_blocks, int flag);

/* ts B10801 */
/** Retrieve some media information which is mainly specific to media of
    the DVD-R family: DVD-R , DVD-RW , DVD-R DL , HD DVD-R
    Currently the information cannot be retrieved from other media types.
    @param d              The drive to query.
    @param disk_category  returns DVD Book to which the media complies
    @param book_name      returns a pointer to the book name of disk_category.
                          This memory is static. Do not alter or free it !
    @param part_version   returns the Media Version in the DVD Book
    @param num_layers     returns the number of media layers
    @param num_blocks     returns the number of blocks between pysical start
                          and physical end of the media
    @param flag           Bitfield for control purposes (unused yet, submit 0)
    @return               1 = reply prarameters are valid,
                          <=0 = reply is invalid (e.g. because no DVD-R)
    @since 1.1.4
*/
int burn_disc_get_phys_format_info(struct burn_drive *d, int *disk_category,
                        char **book_name, int *part_version, int *num_layers,
                        int *num_blocks, int flag);

/* ts A61110 */
/** Read start lba and Next Writeable Address of a track from media.
    Usually a track lba is obtained from the result of burn_track_get_entry().
    This call retrieves an updated lba, eventual nwa, and can address the
    invisible track to come.
    The drive must be grabbed for this call. One may not issue this call
    during ongoing burn_disc_write() or burn_disc_erase().
    @param d The drive to query.
    @param o If not NULL: write parameters to be set on drive before query
    @param trackno 0=next track to come, >0 number of existing track
                   The first existing track on a CD may have a number higher
                   than 1. Use burn_session_get_start_tno() to inquire this
                   start number.
    @param lba return value: start lba
    @param nwa return value: Next Writeable Address
    @return 1=nwa is valid , 0=nwa is not valid , -1=error
    @since 0.2.6
*/
int burn_disc_track_lba_nwa(struct burn_drive *d, struct burn_write_opts *o,
				int trackno, int *lba, int *nwa);

/* ts B10525 */
/** Tells whether a previous attempt to determine the Next Writeable Address
    of the upcomming track reveiled that the READ TRACK INFORMATION Damage Bit
    is set for this track, resp. that no valid writable address is available. 
    See MMC-5 6.27.3.7 Damage Bit, 6.27.3.11 NWA_V (NWA valid)
    @param d     The drive to query.
    @param flag  Bitfield for control purposes (unused yet, submit 0)
    @return      0= Looks ok: Damage Bit is not set, NWA_V is set
                 1= Damaged and theoretically writable (NWA_V is set)
                 2= Not writable: NWA_V is not set
                 3= Damaged and not writable (NWA_V is not set),
    @since 1.1.0
*/
int burn_disc_next_track_is_damaged(struct burn_drive *d, int flag);

/* ts B10527 */
/** Try to close the last track and session of media which have bit0 set in
    the return value of call burn_disc_next_track_is_damaged().
    Whether it helps depends much on the reason why the media is reported
    as damaged by the drive.
    This call works only for profiles 0x09 CD-R, 0x0a CD-RW, 0x11 DVD-R,
    0x14 DVD-RW sequential, 0x1b DVD+R, 0x2b DVD+R DL, 0x41 BD-R sequential.
    Note: After writing it is advised to give up the drive and to grab it again
          in order to learn about its view on the new media state.
    @param o     Write options created by burn_write_opts_new() and
                 manipulated by burn_write_opts_set_multi().
                 burn_write_opts_set_write_type() should be set to
                 BURN_WRITE_TAO, burn_write_opts_set_simulate() should be
                 set to 0.
    @param flag  Bitfield for control purposes
                 bit0= force close, even if no damage was seen
    @return      <=0 media not marked as damaged, or media type not suitable,
                     or closing attempted but failed
                 1= attempt finished without error indication
    @since 1.1.0
*/
int burn_disc_close_damaged(struct burn_write_opts *o, int flag);


/* ts A70131 */
/** Read start lba of the first track in the last complete session.
    This is the first parameter of mkisofs option -C. The second parameter
    is nwa as obtained by burn_disc_track_lba_nwa() with trackno 0.
    @param d The drive to query.
    @param start_lba returns the start address of that track
    @return <= 0 : failure, 1 = ok 
    @since 0.3.2
*/
int burn_disc_get_msc1(struct burn_drive *d, int *start_lba);


/* ts A70213 */
/** Return the best possible estimation of the currently available capacity of
    the media. This might depend on particular write option settings. For
    inquiring the space with such a set of options, the drive has to be
    grabbed and BURN_DRIVE_IDLE. If not, then one will only get a canned value
    from the most recent automatic inquiry (e.g. during last drive grabbing).
    An eventual start address from burn_write_opts_set_start_byte() will be
    subtracted from the obtained capacity estimation. Negative results get
    defaulted to 0.
    If the drive is actually a file in a large filesystem or a large block
    device, then the capacity is curbed to a maximum of 0x7ffffff0 blocks
    = 4 TB - 32 KB.
    @param d The drive to query.
    @param o If not NULL: write parameters to be set on drive before query
    @return number of most probably available free bytes
    @since 0.3.4
*/
off_t burn_disc_available_space(struct burn_drive *d,
                                struct burn_write_opts *o);

/* ts A61202 */
/** Tells the MMC Profile identifier of the loaded media. The drive must be
    grabbed in order to get a non-zero result.
    libburn currently writes only to profiles 
      0x09 "CD-R"
      0x0a "CD-RW"
      0x11 "DVD-R sequential recording"
      0x12 "DVD-RAM"
      0x13 "DVD-RW restricted overwrite"
      0x14 "DVD-RW sequential recording",
      0x15 "DVD-R/DL sequential recording",
      0x1a "DVD+RW"
      0x1b "DVD+R",
      0x2b "DVD+R/DL",
      0x41 "BD-R sequential recording",
      0x43 "BD-RE",
      0xffff "stdio file"
    Note: 0xffff is not a MMC profile but a libburn invention.
    Read-only are the profiles
      0x08 "CD-ROM",
      0x10 "DVD-ROM",
      0x40 "BD-ROM",
    Read-only for now is this BD-R profile (testers wanted)
      0x42 "BD-R random recording"
    Empty drives are supposed to report
      0x00 ""
    @param d The drive where the media is inserted.
    @param pno Profile Number. See also mmc5r03c.pdf, table 89
    @param name Profile Name (see above list, unknown profiles have empty name)
    @return 1 profile is valid, 0 no profile info available 
    @since 0.3.0
*/
int burn_disc_get_profile(struct burn_drive *d, int *pno, char name[80]);


/* ts A90903 : API */
/** Obtain product id and standards defined media codes.
    The product id is a printable string which is supposed to be the same
    for identical media but should vary with non-identical media. Some media
    do not allow to obtain such an id at all. 
    The pair (profile_number, product_id) should be the best id to identify
    media with identical product specifications.
    The reply parameters media_code1 and media_code2 can be used with
    burn_guess_manufacturer()
    The reply parameters have to be disposed by free() when no longer needed.
    @param d           The drive where the media is inserted.
    @param product_id  Reply: Printable text depicting manufacturer and
                       eventually media id.
    @param media_code1 Reply: The eventual manufacturer identification as read
                       from DVD/BD media or a text "XXmYYsZZf" from CD media
                       ATIP lead-in.
    @param media_code2 The eventual media id as read from DVD+/BD media or a
                       text "XXmYYsZZf" from CD ATIP lead-out.
    @param book_type   Book type text for DVD and BD.
                       Caution: is NULL with CD, even if return value says ok.
    @param flag        Bitfield for control purposes
                       bit0= do not escape " _/" (not suitable for
                             burn_guess_manufacturer())
    @return            1= ok, product_id and media codes are valid,
                       0= no product id_available, reply parameters are NULL
                      <0= error
    @since 0.7.2
*/
int burn_disc_get_media_id(struct burn_drive *d,
	char **product_id, char **media_code1, char **media_code2,
	char **book_type, int flag);


/* ts A90904 */
/** Guess the name of a manufacturer by profile number, manufacturer code
    and media code. The profile number can be obtained by
    burn_disc_get_profile(), the other two parameters can be obtained as
    media_code1 and media_code2 by burn_get_media_product_id().
    @param profile_no   Profile number (submit -1 if not known)
    @param manuf_code   Manufacturer code from media (e.g. "RICOHJPN")
    @param media_code   Media ID code from media (e.g. "W11")
    @param flag  Bitfield for control purposes, submit 0
    @return      Printable text or NULL on memory shortage.
                 If the text begins with "Unknown " then no item of the
                 manufacturer list matched the codes.
                 Dispose by free() when no longer needed.
    @since 0.7.2
*/
char *burn_guess_manufacturer(int profile_no,
				 char *manuf_code, char *media_code, int flag);


/** Tells whether a disc can be erased or not
    @param d The drive to inquire.
    @return Non-zero means erasable
*/
int burn_disc_erasable(struct burn_drive *d);

/** Returns the progress and status of a drive.
    @param drive The drive to query busy state for.
    @param p Returns the progress of the operation, NULL if you don't care
    @return the current status of the drive. See also burn_drive_status.
*/
enum burn_drive_status burn_drive_get_status(struct burn_drive *drive,
					     struct burn_progress *p);

/** Creates a write_opts struct for burning to the specified drive.
    The returned object must later be freed with burn_write_opts_free().
    @param drive The drive to write with
    @return The write_opts, NULL on error
*/
struct burn_write_opts *burn_write_opts_new(struct burn_drive *drive);


/* ts A70901 */
/** Inquires the drive associated with a burn_write_opts object.
    @param opts object to inquire
    @return pointer to drive
    @since 0.4.0
*/
struct burn_drive *burn_write_opts_get_drive(struct burn_write_opts *opts);


/** Frees a write_opts struct created with burn_write_opts_new
    @param opts write_opts to free
*/
void burn_write_opts_free(struct burn_write_opts *opts);

/** Creates a read_opts struct for reading from the specified drive
    must be freed with burn_read_opts_free
    @param drive The drive to read from
    @return The read_opts
*/
struct burn_read_opts *burn_read_opts_new(struct burn_drive *drive);

/** Frees a read_opts struct created with burn_read_opts_new
    @param opts write_opts to free
*/
void burn_read_opts_free(struct burn_read_opts *opts);

/** Erase a disc in the drive. The drive must be grabbed successfully BEFORE
    calling this functions. Always ensure that the drive reports a status of
    BURN_DISC_FULL before calling this function. An erase operation is not
    cancellable, as control of the operation is passed wholly to the drive and
    there is no way to interrupt it safely.
    @param drive The drive with which to erase a disc.
                 Only drive roles 1 (MMC) and 5 (stdio random write-only)
                 support erasing.
    @param fast Nonzero to do a fast erase, where only the disc's headers are
                erased; zero to erase the entire disc.
                With DVD-RW, fast blanking yields media capable only of DAO.
*/
void burn_disc_erase(struct burn_drive *drive, int fast);


/* ts A70101 - A70417 */
/** Format media for use with libburn. This currently applies to DVD-RW
    in state "Sequential Recording" (profile 0014h) which get formatted to
    state "Restricted Overwrite" (profile 0013h). DVD+RW can be "de-iced"
    by setting bit2 of flag. DVD-RAM and BD-RE may get formatted initially
    or re-formatted to adjust their Defect Managment.
    This function usually returns while the drive is still in the process
    of formatting. The formatting is done, when burn_drive_get_status()
    returns BURN_DRIVE_IDLE. This may be immediately after return or may
    need several thousand seconds to occur.
    @param drive The drive with the disc to format.
    @param size The size in bytes to be used with the format command. It should
                be divisible by 32*1024. The effect of this parameter may
                depend on the media profile and on parameter flag.
    @param flag Bitfield for control purposes:
                bit0= after formatting, write the given number of zero-bytes
                      to the media and eventually perform preliminary closing.
                bit1+2: size mode
                   0 = use parameter size as far as it makes sense
                   1 = insist in size 0 even if there is a better default known
                       (on DVD-RAM or BD-R identical to size mode 0,
                        i.e. they never get formatted with payload size 0)
                   2 = without bit7: format to maximum available size
                       with bit7   : take size from indexed format descriptor
                   3 = without bit7: format to default size
                       with bit7   : take size from indexed format descriptor
                bit3= -reserved-
                bit4= enforce re-format of (partly) formatted media
                bit5= try to disable eventual defect management
		bit6= try to avoid lengthy media certification
                bit7, bit8 to bit15 =
                      bit7 enables MMC expert application mode (else libburn
                      tries to choose a suitable format type):
                      If it is set then bit8 to bit15 contain the index of
                      the format to use. See burn_disc_get_formats(),
                      burn_disc_get_format_descr().
                      Acceptable types are: 0x00, 0x01, 0x10, 0x11, 0x13,
                      0x15, 0x26, 0x30, 0x31, 0x32.
                      If bit7 is set, then bit4 is set automatically.
               bit16= enable POW on blank BD-R
    @since 0.3.0
*/
void burn_disc_format(struct burn_drive *drive, off_t size, int flag);


/* ts A70112 */
/* @since 0.3.0 */
/** Possible formatting status values */
#define BURN_FORMAT_IS_UNFORMATTED 1
#define BURN_FORMAT_IS_FORMATTED   2
#define BURN_FORMAT_IS_UNKNOWN     3

/* ts A70112 */
/** Inquire the formatting status, the associated sizes and the number of
    available formats.  The info is media specific and stems from MMC command
    23h READ FORMAT CAPACITY. See mmc5r03c.pdf 6.24 for background details.
    Media type can be determined via burn_disc_get_profile().
    @param drive The drive with the disc to format.
    @param status The current formatting status of the inserted media.
                  See BURN_FORMAT_IS_* macros. Note: "unknown" is the
                  legal status for quick formatted, yet unwritten DVD-RW.
    @param size The size in bytes associated with status.
                unformatted: the maximum achievable size of the media
                formatted:   the currently formatted capacity
                unknown:     maximum capacity of drive or of media
    @param bl_sas Additional info "Block Length/Spare Area Size".
                  Expected to be constantly 2048 for non-BD media.
    @param num_formats The number of available formats. To be used with
                       burn_disc_get_format_descr() to obtain such a format
                       and eventually with burn_disc_format() to select one.
    @return 1 reply is valid , <=0 failure
    @since 0.3.0
*/
int burn_disc_get_formats(struct burn_drive *drive, int *status, off_t *size,
				unsigned *bl_sas, int *num_formats);

/* ts A70112 */
/** Inquire parameters of an available media format.
    @param drive The drive with the disc to format.
    @param index The index of the format item. Beginning with 0 up to reply
                 parameter from burn_disc_get_formats() : num_formats - 1
    @param type  The format type.  See mmc5r03c.pdf, 6.5, 04h FORMAT UNIT.
                 0x00=full, 0x10=CD-RW/DVD-RW full, 0x11=CD-RW/DVD-RW grow,
                 0x15=DVD-RW quick, 0x13=DVD-RW quick grow,
                 0x26=DVD+RW background, 0x30=BD-RE with spare areas,
                 0x31=BD-RE without spare areas
    @param size  The maximum size in bytes achievable with this format.
    @param tdp   Type Dependent Parameter. See mmc5r03c.pdf.
    @return 1 reply is valid , <=0 failure
    @since 0.3.0
*/
int burn_disc_get_format_descr(struct burn_drive *drive, int index,
				int *type, off_t *size, unsigned *tdp);



/* ts A61109 : this was and is defunct */
/** Read a disc from the drive and write it to an fd pair. The drive must be
    grabbed successfully BEFORE calling this function. Always ensure that the
    drive reports a status of BURN_DISC_FULL before calling this function.
    @param drive The drive from which to read a disc.
    @param o The options for the read operation.
*/
void burn_disc_read(struct burn_drive *drive, const struct burn_read_opts *o);



/* ts A70222 */
/* @since 0.3.4 */
/** The length of a rejection reasons string for burn_precheck_write() and
    burn_write_opts_auto_write_type() .
*/
#define BURN_REASONS_LEN 4096


/* ts A70219 */
/** Examines a completed setup for burn_disc_write() whether it is permissible
    with drive and media. This function is called by burn_disc_write() but
    an application might be interested in this check in advance.
    @param o The options for the writing operation.
    @param disc The descrition of the disc to be created
    @param reasons Eventually returns a list of rejection reason statements
    @param silent 1= do not issue error messages , 0= report problems
    @return 1 ok, -1= no recordable media detected, 0= other failure
    @since 0.3.4
*/
int burn_precheck_write(struct burn_write_opts *o, struct burn_disc *disc,
                        char reasons[BURN_REASONS_LEN], int silent);


/** Write a disc in the drive. The drive must be grabbed successfully before
    calling this function. Always ensure that the drive reports a status of
    BURN_DISC_BLANK ot BURN_DISC_APPENDABLE before calling this function.
    Note: write_type BURN_WRITE_SAO is currently not capable of writing a mix
    of data and audio tracks. You must use BURN_WRITE_TAO for such sessions.
    To be set by burn_write_opts_set_write_type(). 
    Note: This function is not suitable for overwriting data in the middle of
          a valid data area because it is allowed to append trailing data.
          For exact random access overwriting use burn_random_access_write().
    Note: After writing it is advised to give up the drive and to grab it again
          in order to learn about its view on the new media state.
    Note: Before mounting the written media it might be necessary to eject
          and reload in order to allow the operating system to notice the new
          media state.
    @param o The options for the writing operation.
    @param disc The struct burn_disc * that described the disc to be created
*/
void burn_disc_write(struct burn_write_opts *o, struct burn_disc *disc);


/* ts A90227 */
/** Control stream recording during the write run and eventually set the start
    LBA for stream recording.
    Stream recording is set from struct burn_write_opts when the write run
    gets started. See burn_write_opts_set_stream_recording().
    The call described here can be used later to override this setting and
    to program automatic switching at a given LBA. It also affects subsequent
    calls to burn_random_access_write().
    @param drive    The drive which performs the write operation.
    @param recmode  -1= disable stream recording
                     0= leave setting as is
                     1= enable stream recording
    @param start    The LBA where actual stream recording shall start.
                    (0 means unconditional stream recording)
    @param flag     Bitfield for control purposes (unused yet, submit 0).
    @return         1=success , <=0 failure
    @since 0.6.4
*/
int burn_drive_set_stream_recording(struct burn_drive *drive, int recmode,
                                    int start, int flag);

/** Cancel an operation on a drive.
    This will only work when the drive's busy state is BURN_DRIVE_READING or
    BURN_DRIVE_WRITING.
    @param drive The drive on which to cancel the current operation.
*/
void burn_drive_cancel(struct burn_drive *drive);


/* ts A61223 */
/** Inquire whether the most recent asynchronous media job was successful.
    This applies to burn_disc_erase(), burn_disc_format(), burn_disc_write().
    Reasons for non-success may be: rejection of burn parameters, abort due to
    fatal errors during write, blank or format, a call to burn_drive_cancel()
    by the application thread.
    @param d The drive to inquire.
    @return 1=burn seems to have went well, 0=burn failed 
    @since 0.2.6
*/
int burn_drive_wrote_well(struct burn_drive *d);


/** Convert a minute-second-frame (MSF) value to sector count
    @param m Minute component
    @param s Second component
    @param f Frame component
    @return The sector count
*/
int burn_msf_to_sectors(int m, int s, int f);

/** Convert a sector count to minute-second-frame (MSF)
    @param sectors The sector count
    @param m Returns the minute component
    @param s Returns the second component
    @param f Returns the frame component
*/
void burn_sectors_to_msf(int sectors, int *m, int *s, int *f);

/** Convert a minute-second-frame (MSF) value to an lba
    @param m Minute component
    @param s Second component
    @param f Frame component
    @return The lba
*/
int burn_msf_to_lba(int m, int s, int f);

/** Convert an lba to minute-second-frame (MSF)
    @param lba The lba
    @param m Returns the minute component
    @param s Returns the second component
    @param f Returns the frame component
*/
void burn_lba_to_msf(int lba, int *m, int *s, int *f);

/** Create a new disc
    @return Pointer to a burn_disc object or NULL on failure.
*/
struct burn_disc *burn_disc_create(void);

/** Delete disc and decrease the reference count on all its sessions
	@param d The disc to be freed
*/
void burn_disc_free(struct burn_disc *d);

/** Create a new session
    @return Pointer to a burn_session object or NULL on failure.
 */
struct burn_session *burn_session_create(void);

/** Free a session (and decrease reference count on all tracks inside)
	@param s Session to be freed
*/
void burn_session_free(struct burn_session *s);

/** Add a session to a disc at a specific position, increasing the 
    sessions's reference count.
	@param d Disc to add the session to
	@param s Session to add to the disc
	@param pos position to add at (BURN_POS_END is "at the end")
	@return 0 for failure, 1 for success
*/
int burn_disc_add_session(struct burn_disc *d, struct burn_session *s,
			  unsigned int pos);

/** Remove a session from a disc
	@param d Disc to remove session from
	@param s Session pointer to find and remove
*/
int burn_disc_remove_session(struct burn_disc *d, struct burn_session *s);


/* ts B11219 */
/** Read a CDRWIN cue sheet file and equip the session object by tracks and
    CD-TEXT according to the content of the file.
    For a description of CDRWIN file format see
      http://digitalx.org/cue-sheet/syntax/
    Fully supported commands are:
      CATALOG , CDTEXTFILE , FLAGS , INDEX , ISRC , PERFORMER ,
      POSTGAP , PREGAP , REM , SONGWRITER , TITLE
    Further supported commands introduced by cdrecord (usage like PERFORMER):
      ARRANGER , COMPOSER , MESSAGE
    Partly supported commands are:
      FILE which supports only types BINARY , MOTOROLA , WAVE
      TRACK which supports only datatypes AUDIO , MODE1/2048
    Unsupported types of FILE or TRACK lead to failure of the call.
    libburn does not yet support mixing of AUDIO and MODE1/2048. So this call
    will fail if such a mix is found.
    CD-TEXT information is allowed only if all tracks are of datatype AUDIO.
    Empty lines and lines which start by '#' are ignored.
    @param session     Session where to attach tracks. It must not yet have
                       tracks or else this call will fail.
    @param path        Filesystem address of the CDRWIN cue sheet file.
                       Normally with suffix .cue
    @param fifo_size   Number of bytes in fifo. This will be rounded up by
                       the block size of the track mode. <= 0 means no fifo.
    @param fifo        Returns a reference to the burn_source object that
                       was installed as fifo between FILE and the track
                       burn sources. One may use this to inquire the fifo
                       state. Dispose it by burn_source_free() when no longer
                       needed. It is permissible to pass this parameter to
                       libburn as NULL, in order to immediately drop ownership
                       on the fifo.
    @param text_packs  Returns pre-formatted CD-TEXT packs resulting from
                       cue sheet command CDTEXTFILE. To be used with call
                       burn_write_opts_set_leadin_text().
                       It is permissible to pass this parameter to libburn
                       as NULL, in order to disable CDTEXTFILE.
    @param num_packs   Returns the number of 18 byte records in text_packs.
    @param flag        Bitfield for control purposes.
                       bit0= Do not attach CD-TEXT information to session and
                             tracks. Do not load text_packs.
                       bit1= Do not use media catalog string of session or ISRC
                             strings of tracks for writing to Q sub-channel.
    @return            > 0 indicates success, <= 0 indicates failure
    @since 1.2.0
*/
int burn_session_by_cue_file(struct burn_session *session,
			char *path, int fifo_size, struct burn_source **fifo,
                        unsigned char **text_packs, int *num_packs, int flag);


/** Create a track */
struct burn_track *burn_track_create(void);

/** Free a track
	@param t Track to free
*/
void burn_track_free(struct burn_track *t);

/** Add a track to a session at specified position
	@param s Session to add to
	@param t Track to insert in session
	@param pos position to add at (BURN_POS_END is "at the end")
	@return 0 for failure, 1 for success
*/
int burn_session_add_track(struct burn_session *s, struct burn_track *t,
			   unsigned int pos);

/** Remove a track from a session
	@param s Session to remove track from
	@param t Track pointer to find and remove
	@return 0 for failure, 1 for success
*/
int burn_session_remove_track(struct burn_session *s, struct burn_track *t);


/* ts B20107 */
/** Set the number which shall be written as CD track number with the first
    track of the session. The following tracks will then get written with
    consecutive CD track numbers. The resulting number of the last track
    must not exceed 99. The lowest possible start number is 1, which is also
    the default. This setting applies only to CD SAO writing.
    @param session   The session to be manipulated
    @param tno       A number between 1 and 99
    @param flag      Bitfield for control purposes. Unused yet. Submit 0.
    @return          > 0 indicates success, <= 0 indicates failure
    @since 1.2.0
*/
int burn_session_set_start_tno(struct burn_session *session, int tno,
                               int flag);

/* ts B20108 */
/** Inquire the CD track start number, as set by default or by 
    burn_session_set_start_tno().
    @param session   The session to be inquired
    @return          > 0 is the currently set CD track start number
                     <= 0 indicates failure
    @since 1.2.0
*/
int burn_session_get_start_tno(struct burn_session *session, int flag);



/* ts B11206 */
/** Set the Character Codes, the Copyright bytes, and the Language Codes
    for CD-TEXT blocks 0 to 7. They will be used in the block summaries
    of text packs which get generated from text or binary data submitted
    by burn_session_set_cdtext() and burn_track_set_cdtext().
    Character Code value can be
      0x00 = ISO-8859-1
      0x01 = 7 bit ASCII
      0x80 = MS-JIS (japanesei Kanji, double byte characters)
    Copyright byte value can be
      0x00 = not copyrighted
      0x03 = copyrighted
    Language Code value will typically be 0x09 = English or 0x69 = Japanese.
    See below macros BURN_CDTEXT_LANGUAGES_0X00 and BURN_CDTEXT_LANGUAGES_0X45,
    but be aware that many of these codes have never been seen on CD, and that
    many of them do not have a character representation among the above
    Character Codes. 
    Default is 0x09 = English for block 0 and 0x00 = Unknown for block 1 to 7.
    Copyright and Character Code are 0x00 for all blocks by default.
    See also file doc/cdtext.txt, "Format of a CD-TEXT packs array",
    "Pack type 0x8f".

    Parameter value -1 leaves the current setting of the session parameter
    unchanged.
    @param s            Session where to change settings
    @param char_codes   Character Codes for block 0 to 7
    @param copyrights   Copyright bytes for block 0 to 7
    @param languages    Language Codes for block 0 to 7
    @param flag         Bitfiled for control purposes. Unused yet. Submit 0.
    @return             <=0 failure, > 0 success
    @since 1.2.0
*/
int burn_session_set_cdtext_par(struct burn_session *s,
                                int char_codes[8], int copyrights[8],
                                int languages[8], int flag);

/** This is the first list of languages sorted by their Language codes,
    which start at 0x00.  They stem from from EBU Tech 3264, appendix 3.
    E.g. language 0x00 is "Unknown", 0x08 is "German", 0x10 is "Frisian",
    0x18 is "Latvian", 0x20 is "Polish", 0x28 is "Swedish", 0x2b is "Wallon".
    See also file doc/cdtext.txt.
    @since 1.2.0
*/
#define BURN_CDTEXT_LANGUAGES_0X00 \
        "Unknown", "Albanian", "Breton", "Catalan", \
        "Croatian", "Welsh", "Czech", "Danish", \
        "German", "English", "Spanish", "Esperanto", \
        "Estonian", "Basque", "Faroese", "French", \
        "Frisian", "Irish", "Gaelic", "Galician", \
        "Icelandic", "Italian", "Lappish", "Latin", \
        "Latvian", "Luxembourgian", "Lithuanian", "Hungarian", \
        "Maltese", "Dutch", "Norwegian", "Occitan", \
        "Polish", "Portuguese", "Romanian", "Romansh", \
        "Serbian", "Slovak", "Slovenian", "Finnish", \
        "Swedish", "Turkish", "Flemish", "Wallon" 

/** This is the second list of languages sorted by their Language codes,
    which start at 0x45.  They stem from from EBU Tech 3264, appendix 3.
    E.g. language 0x45 is "Zulu", 0x50 is "Sranan Tongo", 0x58 is "Pushtu",
    0x60 is "Moldavian", 0x68 is "Kannada", 0x70 is "Greek", 0x78 is "Bengali",
    0x7f is "Amharic".
    See also file doc/cdtext.txt.
    @since 1.2.0
*/
#define BURN_CDTEXT_LANGUAGES_0X45 \
                 "Zulu", "Vietnamese", "Uzbek", \
        "Urdu", "Ukrainian", "Thai", "Telugu", \
        "Tatar", "Tamil", "Tadzhik", "Swahili", \
        "Sranan Tongo", "Somali", "Sinhalese", "Shona", \
        "Serbo-croat", "Ruthenian", "Russian", "Quechua", \
        "Pushtu", "Punjabi", "Persian", "Papamiento", \
        "Oriya", "Nepali", "Ndebele", "Marathi", \
        "Moldavian", "Malaysian", "Malagasay", "Macedonian", \
        "Laotian", "Korean", "Khmer", "Kazakh", \
        "Kannada", "Japanese", "Indonesian", "Hindi", \
        "Hebrew", "Hausa", "Gurani", "Gujurati", \
        "Greek", "Georgian", "Fulani", "Dari", \
        "Churash", "Chinese", "Burmese", "Bulgarian", \
        "Bengali", "Bielorussian", "Bambora", "Azerbaijani", \
        "Assamese", "Armenian", "Arabic", "Amharic"

/* This is the list of empty languages names between 0x30 and 0x44.
   Together the three macros fill an array of 128 char pointers.
    static char *languages[] = {
      BURN_CDTEXT_LANGUAGES_0X00,
      BURN_CDTEXT_FILLER,
      BURN_CDTEXT_LANGUAGES_0X45
    };
*/
#define BURN_CDTEXT_FILLER \
         "", "", "", "", \
         "", "", "", "", \
         "", "", "", "", \
         "", "", "", "", \
         "", "", "", "", \
         "", "", "", "", \
         ""

/* ts B11206 */
/** Obtain the current settings as of burn_session_set_cdtext_par() resp.
    by default.
    @param s            Session which to inquire
    @param char_codes   Will return Character Codes for block 0 to 7
    @param copyrights   Will return Copyright bytes for block 0 to 7
    @param languages    Will return Language Codes for block 0 to 7
    @param flag         Bitfiled for control purposes. Unused yet. Submit 0.
    @return             <=0 failure, reply invalid, > 0 success, reply valid
    @since 1.2.0
*/
int burn_session_get_cdtext_par(struct burn_session *s,
                                int char_codes[8], int copyrights[8],
                                int block_languages[8], int flag);


/* ts B11206 */
/** Attach text or binary data as CD-TEXT attributes to a session.
    They can be used to generate CD-TEXT packs by burn_cdtext_from_session()
    or to write CD-TEXT packs into the lead-in of a CD SAO session.
    The latter happens only if no array of CD-TEXT packs is attached to
    the write options by burn_write_opts_set_leadin_text().
    For details of the CD-TEXT format and of the payload content, see file
    doc/cdtext.txt .
    @param s            Session where to attach CD-TEXT attribute
    @param block        Number of the language block in which the attribute
                        shall appear. Possible values: 0 to 7.
    @param pack_type    Pack type number. 0x80 to 0x8e. Used if pack_type_name
                        is NULL or empty text. Else submit 0 and a name.
                        Pack type 0x8f is generated automatically and may not
                        be set by applications.
    @param pack_type_name  The pack type by name. Defined names are:
                          0x80 = "TITLE"         0x81 = "PERFORMER"
                          0x82 = "SONGWRITER"    0x83 = "COMPOSER"
                          0x84 = "ARRANGER"      0x85 = "MESSAGE"
                          0x86 = "DISCID"        0x87 = "GENRE"
                          0x88 = "TOC"           0x89 = "TOC2"
                          0x8d = "CLOSED"        0x8e = "UPC_ISRC"
                        Names are recognized uppercase and lowercase.
    @param payload      Text or binary bytes. The data will be copied to
                        session-internal memory.
                        Pack types 0x80 to 0x85 contain 0-terminated cleartext
                        encoded according to the block's Character Code.
                        If double byte characters are used, then two 0-bytes
                        terminate the cleartext.
                        Pack type 0x86 is 0-terminated ASCII cleartext.
                        Pack type 0x87 consists of two byte big-endian
                        Genre code (see below BURN_CDTEXT_GENRE_LIST), and
                        0-terminated ASCII cleartext of genre description.
                        Pack type 0x88 mirrors the session table-of-content.
                        Pack type 0x89 is not understood yet.
                        Pack types 0x8a to 0x8c are reserved.
                        Pack type 0x8d contains ISO-8859-1 cleartext which is
                        not to be shown by commercial audio CD players.
                        Pack type 0x8e is ASCII cleartext with UPC/EAN code.
    @pram length        Number of bytes in payload. Including terminating
                        0-bytes.
    @param flag         Bitfield for control purposes.
                        bit0= payload contains double byte characters
                              (with character code 0x80 MS-JIS japanese Kanji)
    @return             > 0 indicates success , <= 0 is failure
    @since 1.2.0
*/
int burn_session_set_cdtext(struct burn_session *s, int block,
                            int pack_type, char *pack_type_name,
                            unsigned char *payload, int length, int flag);


/** This is the list of Genres sorted by their Genre codes.
    E.g. genre code 0x0000 is "No Used", 0x0008 is "Dance, 0x0010 is "Musical",
    0x0018 is "Rhythm & Blues", 0x001b is "World Music".
    See also file doc/cdtext.txt.
    @since 1.2.0
*/
#define BURN_CDTEXT_GENRE_LIST \
        "Not Used", "Not Defined", "Adult Contemporary", "Alternative Rock", \
        "Childrens Music", "Classical", "Contemporary Christian", "Country", \
        "Dance", "Easy Listening", "Erotic", "Folk", \
        "Gospel", "Hip Hop", "Jazz", "Latin", \
        "Musical", "New Age", "Opera", "Operetta", \
        "Pop Music", "Rap", "Reggae", "Rock Music", \
        "Rhythm & Blues", "Sound Effects", "Spoken Word", "World Music"

/* The number of genre names in BURN_CDTEXT_GENRE_LIST.
*/
#define BURN_CDTEXT_NUM_GENRES 28


/* ts B11206 */
/** Obtain a CD-TEXT attribute that was set by burn_session_set_cdtext()
    @param s            Session to inquire
    @param block        Number of the language block to inquire.
    @param pack_type    Pack type number to inquire. Used if pack_type_name
                        is NULL or empty text. Else submit 0 and a name.
                        Pack type 0x8f is generated automatically and may not
                        be inquire in advance. Use burn_cdtext_from_session()
                        to generate all packs including type 0x8f packs.
    @param pack_type_name  The pack type by name.
                        See above burn_session_set_cdtext().
    @param payload      Will return a pointer to text or binary bytes.
                        Not a copy of data. Do not free() this address.
                        If no text attribute is attached for pack type and
                        block, then payload is returned as NULL. The return
                        value will not indicate error in this case.
    @pram length        Will return the number of bytes pointed to by payload.
                        Including terminating 0-bytes.
    @param flag         Bitfield for control purposes. Unused yet. Submit 0.
    @return             1 single byte char, 2 double byte char, <=0 error
    @since 1.2.0
*/
int burn_session_get_cdtext(struct burn_session *s, int block,
                            int pack_type, char *pack_type_name,
                            unsigned char **payload, int *length, int flag);


/* ts B11215 */
/** Read a Sony CD-TEXT Input Sheet Version 0.7T file and attach its text
    attributes to the given session and its tracks for the given CD-TEXT
    block number. This overrides previous settings made by
    burn_session_set_cdtext(), burn_track_set_cdtext(), burn_track_set_isrc(),
    burn_session_set_start_tno(). It can later be overridden by said function
    calls.
    The media catalog number from purpose specifier "UPC / EAN" gets into
    effect only if burn_write_opts_set_has_mediacatalog() is set to 0.
    The format of a v07t sheet file is documented in doc/cdtext.txt.
    @param s           Session where to attach CD-TEXT attributes
    @param path        Local filesystem address of the sheet file which
                       shall be read and interpreted.
    @param block       Number of the language block in which the attributes
                       shall appear. Possible values: 0 to 7.
    @param flag        Bitfield for control purposes.
                       bit1= Do not use media catalog string of session or ISRC
                             strings of tracks for writing to Q sub-channel.
    @return            > 0 indicates success , <= 0 is failure
    @since 1.2.0
*/
int burn_session_input_sheet_v07t(struct burn_session *session,
                                  char *path, int block, int flag);


/* ts B11210 */
/** Produce an array of CD-TEXT packs that could be submitted to
    burn_write_opts_set_leadin_text() or stored as *.cdt file.
    For a description of the format of the array, see file doc/cdtext.txt.
    The input data stem from burn_session_set_cdtext_par(),
    burn_session_set_cdtext(), and burn_track_set_cdtext().
    @param s            Session from which to produce CD-TEXT packs.
    @param text_packs   Will return the buffer with the CD-TEXT packs.
                        Dispose by free() when no longer needed.
    @param num_packs    Will return the number of 18 byte text packs.
    @param flag         Bitfield for control purposes.
                        bit0= do not return generated CD-TEXT packs,
                              but check whether production would work and
                              indicate the number of packs by the call return
                              value. This happens also if
                              (text_packs == NULL || num_packs == NULL).
    @return             Without flag bit0: > 0 is success, <= 0 failure
                        With flag bit0: > 0 is number of packs,
                                          0 means no packs will be generated,
                                        < 0 means failure  
    @since 1.2.0
*/
int burn_cdtext_from_session(struct burn_session *s,
                             unsigned char **text_packs, int *num_packs,
                             int flag);


/* ts B11206 */
/** Remove all CD-TEXT attributes of the given block from the session.
    They were attached by burn_session_set_cdtext().
    @param s            Session where to remove the CD-TEXT attribute
    @param block        Number of the language block in which the attribute
                        shall appear. Possible values: 0 to 7.
                        -1 causes text packs of all blocks to be removed.
    @return             > 0 is success, <= 0 failure
    @since 1.2.0
*/
int burn_session_dispose_cdtext(struct burn_session *s, int block);


/* ts B11221*/ 
/** Read an array of CD-TEXT packs from a file. This array should be suitable
    for burn_write_opts_set_leadin_text().
    The function tolerates and removes 4-byte headers as produced by
    cdrecord -vv -toc, if this header tells the correct number of bytes which
    matches the file size. If no 4-byte header is present, then the function
    tolerates and removes a trailing 0-byte as of Sony specs.
    @param path         Filesystem address of the CD-TEXT pack file.
                        Normally with suffix .cdt or .dat
    @param text_packs   Will return the buffer with the CD-TEXT packs.
                        Dispose by free() when no longer needed.
    @param num_packs    Will return the number of 18 byte text packs.
    @param flag         Bitfield for control purposes. Unused yet.Submit 0.
    @return             0 is success, <= 0 failure
    @since 1.2.0
*/
int burn_cdtext_from_packfile(char *path, unsigned char **text_packs,
                              int *num_packs, int flag);


/** Define the data in a track
	@param t the track to define
	@param offset The lib will write this many 0s before start of data
	@param tail The number of extra 0s to write after data
	@param pad 1 means the lib should pad the last sector with 0s if the
	       track isn't exactly sector sized.  (otherwise the lib will
	       begin reading from the next track)
	@param mode data format (bitfield)
*/
void burn_track_define_data(struct burn_track *t, int offset, int tail,
			    int pad, int mode);


/* ts B11206 */
/** Attach text or binary data as CD-TEXT attributes to a track.
    The payload will be used to generate CD-TEXT packs by
    burn_cdtext_from_session() or to write CD-TEXT packs into the lead-in
    of a CD SAO session. This happens if the CD-TEXT attribute of the session
    gets generated, which has the same block number and pack type. In this
    case, each track should have such a CD-TEXT attribute, too.
    See burn_session_set_cdtext().
    Be cautious not to exceed the maximum number of 253 payload packs per
    language block. Use burn_cdtext_from_session() to learn whether a valid
    array of CD-TEXT packs can be generated from your attributes.
    @param t            Track where to attach CD-TEXT attribute.
    @param block        Number of the language block in which the attribute
                        shall appear. Possible values: 0 to 7.
    @param pack_type    Pack type number. 0x80 to 0x85 or 0x8e. Used if
                        pack_type_name is NULL or empty text. Else submit 0
                        and a name.
    @param pack_type_name  The pack type by name. Applicable names are:
                          0x80 = "TITLE"         0x81 = "PERFORMER"
                          0x82 = "SONGWRITER"    0x83 = "COMPOSER"
                          0x84 = "ARRANGER"      0x85 = "MESSAGE"
                          0x8e = "UPC_ISRC"
    @param payload      0-terminated cleartext. If double byte characters
                        are used, then two 0-bytes terminate the cleartext.
    @pram length        Number of bytes in payload. Including terminating
                        0-bytes.
    @param flag         Bitfield for control purposes.
                        bit0= payload contains double byte characters
                              (with character code 0x80 MS-JIS japanese Kanji)
    @return             > 0 indicates success , <= 0 is failure
    @since 1.2.0
*/
int burn_track_set_cdtext(struct burn_track *t, int block,
                          int pack_type, char *pack_type_name,
                          unsigned char *payload, int length, int flag);

/* ts B11206 */
/** Obtain a CD-TEXT attribute that was set by burn_track_set_cdtext().
    @param t            Track to inquire
    @param block        Number of the language block to inquire.
    @param pack_type    Pack type number to inquire. Used if pack_type_name
                        is NULL or empty text. Else submit 0 and a name.
    @param pack_type_name  The pack type by name.
                        See above burn_track_set_cdtext().
    @param payload      Will return a pointer to text bytes.
                        Not a copy of data. Do not free() this address.
                        If no text attribute is attached for pack type and
                        block, then payload is returned as NULL. The return
                        value will not indicate error in this case.
    @pram length        Will return the number of bytes pointed to by payload.
                        Including terminating 0-bytes.
    @param flag         Bitfield for control purposes. Unused yet. Submit 0.
    @return             1=single byte char , 2= double byte char , <=0 error
    @since 1.2.0
*/
int burn_track_get_cdtext(struct burn_track *t, int block,
                          int pack_type, char *pack_type_name,
                          unsigned char **payload, int *length, int flag);

/* ts B11206 */
/** Remove all CD-TEXT attributes of the given block from the track.
    They were attached by burn_track_set_cdtext().
    @param t            Track where to remove the CD-TEXT attribute.
    @param block        Number of the language block in which the attribute
                        shall appear. Possible values: 0 to 7. 
                        -1 causes text packs of all blocks to be removed.
    @return             > 0 is success, <= 0 failure 
    @since 1.2.0
*/
int burn_track_dispose_cdtext(struct burn_track *t, int block);


/* ts A90910 */
/** Activates CD XA compatibility modes.
    libburn currently writes data only in CD mode 1. Some programs insist in
    sending data with additional management bytes. These bytes have to be
    stripped in order to make the input suitable for BURN_MODE1.
    @param t     The track to manipulate
    @param value 0= no conversion
                 1= strip 8 byte sector headers of CD-ROM XA mode 2 form 1
                    see MMC-5 4.2.3.8.5.3 Block Format for Mode 2 form 1 Data
                 all other values are reserved
    @return 1=success , 0=unacceptable value
    @since 0.7.2
*/
int burn_track_set_cdxa_conv(struct burn_track *t, int value);


/** Set the ISRC details for a track. When writing to CD media, ISRC will get
    written into the Q sub-channel.
	@param t The track to change
	@param country the 2 char country code. Each character must be
	       only numbers or letters.
	@param owner 3 char owner code. Each character must be only numbers
	       or letters.
	@param year 2 digit year. A number in 0-99 (Yep, not Y2K friendly).
	@param serial 5 digit serial number. A number in 0-99999.
*/
void burn_track_set_isrc(struct burn_track *t, char *country, char *owner,
			 unsigned char year, unsigned int serial);

/* ts B11226 */
/** Set the composed ISRC string for a track. This is an alternative to
    burn_track_set_isrc().
    @param t      The track to be manipulated
    @param isrc   12 characters which are composed from ISRC details.
                  Format is CCOOOYYSSSSS, terminated by a 0-byte:
                  Country, Owner, Year(decimal digits), Serial(decimal digits).
    @param flag   Bitfield for control purposes. Unused yet. Submit 0.
    @return       > 0 indicates success, <= 0 means failure
    @since 1.2.0
*/
int burn_track_set_isrc_string(struct burn_track *t, char isrc[13], int flag);

/** Disable ISRC parameters for a track
	@param t The track to change
*/
void burn_track_clear_isrc(struct burn_track *t);


/* ts B20103 */
/** Define an index start address within a track. The index numbers inside a
    track have to form sequence starting at 0 or 1 with no gaps up to the
    highest number used. They affect only writing of CD SAO sessions.
    The first index start address of a track must be 0.
    Blocks between index 0 and index 1 are considered to be located before the
    track start as of the table-of-content.
    @param t             The track to be manipulated
    @param index_number  A number between 0 and 99
    @param relative_lba  The start address relative to the start of the
                         burn_source of the track. It will get mapped to the
                         appropriate absolute block address.
    @param flag          Bitfield for control purposes. Unused yet. Submit 0.
    @return              > 0 indicates success, <= 0 means failure
    @since 1.2.0
*/
int burn_track_set_index(struct burn_track *t, int index_number,
                                        unsigned int relative_lba, int flag);

/* ts B20103 */
/** Remove all index start addresses and reset to the default indexing of
    CD SAO sessions. This means index 0 of track 1 reaches from LBA -150
    to LBA -1. Index 1 of track 1 reaches from LBA 0 to track end. Index 1
    of track 2 follows immediately. The same happens for all further tracks
    after the end of their predecessor.
    @param t             The track to be manipulated
    @param flag          Bitfield for control purposes. Unused yet. Submit 0.
    @return              > 0 indicates success, <= 0 means failure
    @since 1.2.0
*/
int burn_track_clear_indice(struct burn_track *t, int flag);


/* ts B20110 */
/** Define whether a pre-gap shall be written before the track and how many
    sectors this pre-gap shall have. A pre-gap is written in the range of track
    index 0 and contains zeros resp. silence. No bytes from the track source
    will be read for writing the pre-gap.
    This setting affects only CD SAO write runs.
    The first track automatically gets a pre-gap of at least 150 sectors. Its
    size may be enlarged by this call. Further pre-gaps are demanded by MMC
    for tracks which follow tracks of a different mode. (But Mode mixing in
    CD SAO sessions is currently not supported by libburn.)
    @param t             The track to change
    @param size          Number of sectors in the pre-gap.
                         -1 disables pre-gap, except for the first track.
                         libburn allows 0, but MMC does not propose this.
    @param flag          Bitfield for control purposes. Unused yet. Submit 0.
    @return              > 0 indicates success, <= 0 means failure
    @since 1.2.0
*/
int burn_track_set_pregap_size(struct burn_track *t, int size, int flag);

/* ts B20111 */
/** Define whether a post-gap shall be written at the end of the track and
    how many sectors this gap shall have. A post-gap occupies the range of
    an additional index of the track. It contains zeros. No bytes from the
    track source will be read for writing the post-gap.
    This setting affects only CD SAO write runs.
    MMC prescribes to add a post-gap to a data track which is followed by
    a non-data track. (But libburn does not yet support mixed mode CD SAO
    sessions.)
    @param t             The track to change
    @param size          Number of sectors in the post-gap.
                         -1 disables post-gap.
                         libburn allows 0, but MMC does not propose this.
    @param flag          Bitfield for control purposes. Unused yet. Submit 0.
    @return              > 0 indicates success, <= 0 means failure
    @since 1.2.0
*/
int burn_track_set_postgap_size(struct burn_track *t, int size, int flag);


/* ts A61024 */
/** Define whether a track shall swap bytes of its input stream.
    @param t The track to change
    @param swap_source_bytes 0=do not swap, 1=swap byte pairs
    @return 1=success , 0=unacceptable value
    @since 0.2.6
*/
int burn_track_set_byte_swap(struct burn_track *t, int swap_source_bytes);


/** Hide the first track in the "pre gap" of the disc
	@param s session to change
	@param onoff 1 to enable hiding, 0 to disable
*/
void burn_session_hide_first_track(struct burn_session *s, int onoff);

/** Get the drive's disc struct - free when done
	@param d drive to query
	@return the disc struct or NULL on failure
*/
struct burn_disc *burn_drive_get_disc(struct burn_drive *d);

/** Set the track's data source
	@param t The track to set the data source for
	@param s The data source to use for the contents of the track
	@return An error code stating if the source is ready for use for
	        writing the track, or if an error occured
    
*/
enum burn_source_status burn_track_set_source(struct burn_track *t,
					      struct burn_source *s);


/* ts A70218 */
/** Set a default track size to be used only if the track turns out to be of
    unpredictable length and if the effective write type demands a fixed size.
    This can be useful to enable write types CD SAO or DVD DAO together with
    a track source like stdin. If the track source delivers fewer bytes than
    announced then the track will be padded up with zeros.
    @param t The track to change
    @param size The size to set
    @return 0=failure 1=sucess
    @since 0.3.4
*/
int burn_track_set_default_size(struct burn_track *t, off_t size);

/** Free a burn_source (decrease its refcount and maybe free it)
	@param s Source to free
*/
void burn_source_free(struct burn_source *s);

/** Creates a data source for an image file (and maybe subcode file)
    @param path The file address for the main channel payload.
    @param subpath Eventual address for subchannel data. Only used in exotic
                   raw write modes. Submit NULL for normal tasks.
    @return Pointer to a burn_source object, NULL indicates failure
*/
struct burn_source *burn_file_source_new(const char *path,
					 const char *subpath);


/* ts A91122 : An interface to open(O_DIRECT) or similar OS tricks. */

/** Opens a file with eventual acceleration preparations which may depend
    on the operating system and on compile time options of libburn.
    You may use this call instead of open(2) for opening file descriptors
    which shall be handed to burn_fd_source_new().
    This should only be done for tracks with BURN_BLOCK_MODE1 (2048 bytes
    per block).

    If you use this call then you MUST allocate the buffers which you use
    with read(2) by call burn_os_alloc_buffer(). Read sizes MUST be a multiple
    of a safe buffer amount. Else you risk that track data get altered during
    transmission.
    burn_disk_write() will allocate a suitable read/write buffer for its own
    operations. A fifo created by burn_fifo_source_new() will allocate
    suitable memory for its buffer if called with flag bit0 and a multiple
    of a safe buffer amount. 
    @param path       The file address to open
    @param open_flags The flags as of man 2 open. Normally just O_RDONLY.
    @param flag       Bitfield for control purposes (unused yet, submit 0).
    @return           A file descriptor as of open(2). Finally to be disposed
                      by close(2).
                      -1 indicates failure.
    @since 0.7.4
*/
int burn_os_open_track_src(char *path, int open_flags, int flag);

/** Allocate a memory area that is suitable for reading with a file descriptor
    opened by burn_os_open_track_src().
    @param amount     Number of bytes to allocate. This should be a multiple
                      of the operating system's i/o block size. 32 KB is
                      guaranteed by libburn to be safe.
    @param flag       Bitfield for control purposes (unused yet, submit 0).
    @return           The address of the allocated memory, or NULL on failure.
                      A non-NULL return value has finally to be disposed via
                      burn_os_free_buffer().
    @since 0.7.4
*/
void *burn_os_alloc_buffer(size_t amount, int flag);

/** Dispose a memory area which was obtained by burn_os_alloc_buffer(),
    @param buffer     Memory address to be freed.
    @param amount     The number of bytes which was allocated at that
                      address.
    @param flag       Bitfield for control purposes (unused yet, submit 0).
    @return           1 success , <=0 failure
    @since 0.7.4
*/
int burn_os_free_buffer(void *buffer, size_t amount, int flag);


/** Creates a data source for an image file (a track) from an open
    readable filedescriptor, an eventually open readable subcodes file
    descriptor and eventually a fixed size in bytes.
    @param datafd The source of data.
    @param subfd The eventual source of subchannel data. Only used in exotic
                 raw write modes. Submit -1 for normal tasks.
    @param size The eventual fixed size of eventually both fds. 
                If this value is 0, the size will be determined from datafd.
    @return Pointer to a burn_source object, NULL indicates failure
*/
struct burn_source *burn_fd_source_new(int datafd, int subfd, off_t size);


/* ts B00922 */
/** Creates an offset source which shall provide a byte interval of a stream
    to its consumer. It is supposed to be chain-linked with other offset
    sources which serve neighboring consumers. The chronological sequence
    of consumers and the sequence of offset sources must match. The intervals
    of the sources must not overlap.

    A chain of these burn_source objects may be used to feed multiple tracks
    from one single stream of input bytes.
    Each of the offset sources will skip the bytes up to its start address and
    provide the prescribed number of bytes to the track. Skipping takes into
    respect the bytes which have been processed by eventual predecessors in the
    chain.
    Important: It is not allowed to free an offset source before its successor
               has ended its work. Best is to keep them all until all tracks
               are done.

    @param inp   The burn_source object from which to read stream data.
                 E.g. created by burn_file_source_new().
    @param prev  The eventual offset source object which shall read data from
                 inp before the new offset source will begin its own work.
                 This must either be a result of  burn_offst_source_new()  or
                 it must be NULL.
    @param start The byte address where to start reading bytes for the
                 consumer. inp bytes may get skipped to reach this address.
    @param size  The number of bytes to be delivered to the consumer.
                 If size is <= 0 then it may be set later by a call of method
                 set_size(). If it is >= 0, then it can only be changed if
                 flag bit0 was set with burn_offst_source_new().
    @param flag  Bitfield for control purposes
                 bit0 = Prevent set_size() from overriding interval sizes > 0.
                        If such a size is already set, then the new one will
                        only affect the reply of get_size().
                        See also above struct burn_source.
                        @since 1.2.0
    @return      Pointer to a burn_source object, later to be freed by
                 burn_source_free(). NULL indicates failure.
    @since 0.8.8
*/
struct burn_source *burn_offst_source_new(
                struct burn_source *inp, struct burn_source *prev,
                off_t start, off_t size, int flag);

/* ts A70930 */
/** Creates a fifo which acts as proxy for an already existing data source.
    The fifo provides a ring buffer which shall smoothen the data stream
    between burn_source and writer thread. Each fifo serves only for one
    data source. It may be attached to one track as its only data source
    by burn_track_set_source(), or it may be used as input for other burn
    sources.
    A fifo starts its life in "standby" mode with no buffer space allocated.
    As soon as its consumer requires bytes, the fifo establishes a worker
    thread and allocates its buffer. After input has ended and all buffer
    content is consumed, the buffer space gets freed and the worker thread
    ends. This happens asynchronously. So expect two buffers and worker threads
    to exist for a short time between tracks. Be modest in your size demands if
    multiple tracks are to be expected. 
    @param inp        The burn_source for which the fifo shall act as proxy.
                      It can be disposed by burn_source_free() immediately
                      after this call.
    @param chunksize  The size in bytes of a chunk.
                      Use 2048 for sources suitable for BURN_BLOCK_MODE1,
                      2352 for sources which deliver for BURN_BLOCK_AUDIO,
                      2056 for sources which shall get treated by 
                      burn_track_set_cdxa_conv(track, 1).
                      Some variations of burn_source might work only with
                      a particular chunksize. E.g. libisofs demands 2048.
    @param chunks     The number of chunks to be allocated in ring buffer.
                      This value must be >= 2.
    @param flag       Bitfield for control purposes:
                      bit0= The read method of inp is capable of delivering
                            arbitrary amounts of data per call. Not only one
                            sector.
                            Suitable for inp from burn_file_source_new()
                            and burn_fd_source_new() if not the fd has
                            exotic limitations on read size.
                            You MUST use this on inp which uses an fd opened
                            with burn_os_open_track_src().
                            Better do not use with other inp types.
                            @since 0.7.4
    @return           A pointer to the newly created burn_source.
                      Later both burn_sources, inp and the returned fifo, have
                      to be disposed by calling burn_source_free() for each.
                      inp can be freed immediately, the returned fifo may be
                      kept as handle for burn_fifo_inquire_status().
    @since 0.4.0
*/
struct burn_source *burn_fifo_source_new(struct burn_source *inp,
                                         int chunksize, int chunks, int flag);

/* ts A71003 */
/** Inquires state and fill parameters of a fifo burn_source which was created
    by burn_fifo_source_new() . Do not use with other burn_source variants.
    @param fifo  The fifo object to inquire
    @param size  The total size of the fifo
    @param free_bytes  The current free capacity of the fifo
    @param status_text  Returns a pointer to a constant text, see below
    @return  <0 reply invalid, >=0 fifo status code:
             bit0+1=input status, bit2=consumption status, i.e:
             0="standby"   : data processing not started yet
             1="active"    : input and consumption are active
             2="ending"    : input has ended without error
             3="failing"   : input had error and ended,
             4="unused"    : ( consumption has ended before processing start )
             5="abandoned" : consumption has ended prematurely
             6="ended"     : consumption has ended without input error
             7="aborted"   : consumption has ended after input error
    @since 0.4.0
*/
int burn_fifo_inquire_status(struct burn_source *fifo, int *size, 
                            int *free_bytes, char **status_text);

/* ts A91125 */
/** Inquire various counters which reflect the fifo operation.
    @param fifo              The fifo object to inquire
    @param total_min_fill    The minimum number of bytes in the fifo. Beginning
                             from the moment when fifo consumption is enabled.
    @param interval_min_fill The minimum byte number beginning from the moment
                             when fifo consumption is enabled or from the
                             most recent moment when burn_fifo_next_interval()
                             was called.
    @param put_counter       The number of data transactions into the fifo.
    @param get_counter       The number of data transactions out of the fifo.
    @param empty_counter     The number of times the fifo was empty.
    @param full_counter      The number of times the fifo was full.
    @since 0.7.4
*/
void burn_fifo_get_statistics(struct burn_source *fifo,
                             int *total_min_fill, int *interval_min_fill,
                             int *put_counter, int *get_counter,
                             int *empty_counter, int *full_counter);

/* ts A91125 */
/** Inquire the fifo minimum fill counter for intervals and reset that counter.
    @param fifo              The fifo object to inquire
    @param interval_min_fill The minimum number of bytes in the fifo. Beginning
                             from the moment when fifo consumption is enabled
                             or from the most recent moment when
                             burn_fifo_next_interval() was called.
    @since 0.7.4
*/
void burn_fifo_next_interval(struct burn_source *fifo, int *interval_min_fill);

/* ts A80713 */
/** Obtain a preview of the first input data of a fifo which was created
    by burn_fifo_source_new(). The data will later be delivered normally to
    the consumer track of the fifo.
    bufsize may not be larger than the fifo size (chunk_size * chunks) - 32k.
    This call will succeed only if data consumption by the track has not
    started yet, i.e. best before the call to burn_disc_write().
    It will start the worker thread of the fifo with the expectable side
    effects on the external data source. Then it waits either until enough
    data have arrived or until it becomes clear that this will not happen.
    The call may be repeated with increased bufsize. It will always yield
    the bytes beginning from the first one in the fifo.
    @param fifo     The fifo object to inquire resp. start
    @param buf      Pointer to memory of at least bufsize bytes where to
                    deliver the peeked data.
    @param bufsize  Number of bytes to peek from the start of the fifo data
    @param flag     Bitfield for control purposes (unused yet, submit 0).
    @return <0 on severe error, 0 if not enough data, 1 if bufsize bytes read
    @since 0.5.0
*/
int burn_fifo_peek_data(struct burn_source *fifo, char *buf, int bufsize,
                        int flag);

/* ts A91125 */
/** Start the fifo worker thread and wait either until the requested number
    of bytes have arrived or until it becomes clear that this will not happen.
    Filling will go on asynchronously after burn_fifo_fill() returned.
    This call and burn_fifo_peek_data() do not disturb each other.
    @param fifo     The fifo object to start
    @param fill     Number of bytes desired. Expect to get return 1 if 
                    at least fifo size - 32k were read.
    @param flag     Bitfield for control purposes.
                    bit0= fill fifo to maximum size
    @return <0 on severe error, 0 if not enough data,
             1 if desired amount or fifo full
    @since 0.7.4
*/
int burn_fifo_fill(struct burn_source *fifo, int fill, int flag);


/* ts A70328 */
/** Sets a fixed track size after the data source object has already been
    created.
    @param t The track to operate on
    @param size the number of bytes to use as track size
    @return <=0 indicates failure , >0 success
    @since 0.3.6
*/
int burn_track_set_size(struct burn_track *t, off_t size);


/** Tells how many sectors a track will have on disc, resp. already has on
    disc. This includes offset, payload, tail, and post-gap, but not pre-gap.
    The result is NOT RELIABLE with tracks of undefined length
*/
int burn_track_get_sectors(struct burn_track *);


/* ts A61101 */
/** Tells how many source bytes have been read and how many data bytes have
    been written by the track during burn.
    @param t The track to inquire
    @param read_bytes Number of bytes read from the track source
    @param written_bytes Number of bytes written to track
    @since 0.2.6
*/
int burn_track_get_counters(struct burn_track *t, 
                            off_t *read_bytes, off_t *written_bytes);


/** Sets drive read and write speed
    Note: "k" is 1000, not 1024. 1xCD = 176.4 k/s, 1xDVD = 1385 k/s.
          Fractional speeds should be rounded up. Like 4xCD = 706.
    @param d The drive to set speed for
    @param read Read speed in k/s (0 is max, -1 is min).
    @param write Write speed in k/s (0 is max, -1 is min). 
*/
void burn_drive_set_speed(struct burn_drive *d, int read, int write);


/* ts A70711 */
/** Controls the behavior with writing when the drive buffer is suspected to
    be full. To check and wait for enough free buffer space before writing
    will move the task of waiting from the operating system's device driver
    to libburn. While writing is going on and waiting is enabled, any write
    operation will be checked whether it will fill the drive buffer up to
    more than max_percent. If so, then waiting will happen until the buffer
    fill is predicted with at most min_percent.
    Thus: if min_percent < max_percent then transfer rate will oscillate. 
    This may allow the driver to operate on other devices, e.g. a disk from
    which to read the input for writing. On the other hand, this checking might
    reduce maximum throughput to the drive or even get misled by faulty buffer
    fill replies from the drive.
    If a setting parameter is < 0, then this setting will stay unchanged
    by the call.
    Known burner or media specific pitfalls:
    To have max_percent larger than the burner's best reported buffer fill has
    the same effect as min_percent==max_percent. Some burners do not report
    their full buffer with all media types. Some are not suitable because
    they report their buffer fill with delay.
    @param d The drive to control
    @param enable 0= disable , 1= enable waiting , (-1 = do not change setting)
    @param min_usec Shortest possible sleeping period (given in micro seconds)
    @param max_usec Longest possible sleeping period (given in micro seconds)
    @param timeout_sec If a single write has to wait longer than this number
                       of seconds, then waiting gets disabled and mindless
                       writing starts. A value of 0 disables this timeout.
    @param min_percent Minimum of desired buffer oscillation: 25 to 100
    @param max_percent Maximum of desired buffer oscillation: 25 to 100
    @return 1=success , 0=failure
    @since 0.3.8
*/
int burn_drive_set_buffer_waiting(struct burn_drive *d, int enable,
                                int min_usec, int max_usec, int timeout_sec,
                                int min_percent, int max_percent);


/* these are for my [Derek Foreman's ?] debugging, they will disappear */
/* ts B11012 :
   Of course, API symbols will not disappear. But these functions are of
   few use, as they only print DEBUG messages.
*/
void burn_structure_print_disc(struct burn_disc *d);
void burn_structure_print_session(struct burn_session *s);
void burn_structure_print_track(struct burn_track *t);

/** Sets the write type for the write_opts struct.
    Note: write_type BURN_WRITE_SAO is currently not capable of writing a mix
    of data and audio tracks. You must use BURN_WRITE_TAO for such sessions.
    @param opts The write opts to change
    @param write_type The write type to use
    @param block_type The block type to use
    @return Returns 1 on success and 0 on failure.
*/
int burn_write_opts_set_write_type(struct burn_write_opts *opts,
				   enum burn_write_types write_type,
				   int block_type);


/* ts A70207 */
/** As an alternative to burn_write_opts_set_write_type() this function tries
    to find a suitable write type and block type for a given write job
    described by opts and disc. To be used after all other setups have been
    made, i.e. immediately before burn_disc_write().
    @param opts The nearly complete write opts to change
    @param disc The already composed session and track model
    @param reasons This text string collects reasons for decision resp. failure
    @param flag Bitfield for control purposes:
                bit0= do not choose type but check the one that is already set
                bit1= do not issue error messages via burn_msgs queue
                      (is automatically set with bit0)
    @return Chosen write type. BURN_WRITE_NONE on failure.
    @since 0.3.2
*/
enum burn_write_types burn_write_opts_auto_write_type(
          struct burn_write_opts *opts, struct burn_disc *disc,
          char reasons[BURN_REASONS_LEN], int flag);


/** Supplies toc entries for writing - not normally required for cd mastering
    @param opts The write opts to change
    @param count The number of entries
    @param toc_entries
*/
void burn_write_opts_set_toc_entries(struct burn_write_opts *opts,
				     int count,
				     struct burn_toc_entry *toc_entries);

/** Sets the session format for a disc
    @param opts The write opts to change
    @param format The session format to set
*/
void burn_write_opts_set_format(struct burn_write_opts *opts, int format);

/** Sets the simulate value for the write_opts struct . 
    This corresponds to the Test Write bit in MMC mode page 05h. Several media
    types do not support this. See struct burn_multi_caps.might_simulate for
    actual availability of this feature. 
    If the media is suitable, the drive will perform burn_disc_write() as a
    simulation instead of effective write operations. This means that the
    media content and burn_disc_get_status() stay unchanged.
    Note: With stdio-drives, the target file gets eventually created, opened,
          lseeked, and closed, but not written. So there are effects on it.
    Warning: Call burn_random_access_write() will never do simulation because
             it does not get any burn_write_opts.
    @param opts The write opts to change
    @param sim  Non-zero enables simulation, 0 enables real writing
    @return Returns 1 on success and 0 on failure.
*/
int  burn_write_opts_set_simulate(struct burn_write_opts *opts, int sim);

/** Controls buffer underrun prevention. This is only needed with CD media
    and possibly with old DVD-R drives. All other media types are not
    vulnerable to burn failure due to buffer underrun.
    @param opts The write opts to change
    @param underrun_proof if non-zero, buffer underrun protection is enabled
    @return Returns 1 if the drive announces to be capable of underrun
                      prevention,
            Returns 0 if not.
*/
int burn_write_opts_set_underrun_proof(struct burn_write_opts *opts,
				       int underrun_proof);

/** Sets whether to use opc or not with the write_opts struct
    @param opts The write opts to change
    @param opc If non-zero, optical power calibration will be performed at
               start of burn
	 
*/
void burn_write_opts_set_perform_opc(struct burn_write_opts *opts, int opc);


/** The Q sub-channel of a CD may contain a Media Catalog Number of 13 decimal
    digits. This call sets the string of digits, but does not yet activate it
    for writing.
    @param opts          The write opts to change
    @param mediacatalog  The 13 decimal digits as ASCII bytes. I.e. '0' = 0x30.
*/
void burn_write_opts_set_mediacatalog(struct burn_write_opts *opts,
                                      unsigned char mediacatalog[13]);

/** This call activates the Media Catalog Number for writing. The digits of
    that number have to be set by call burn_write_opts_set_mediacatalog().
    @param opts             The write opts to change
    @param has_mediacatalog 1= activate writing of catalog to Q sub-channel
                            0= deactivate it
*/
void burn_write_opts_set_has_mediacatalog(struct burn_write_opts *opts,
                                          int has_mediacatalog);


/* ts A61106 */
/** Sets the multi flag which eventually marks the emerging session as not
    being the last one and thus creating a BURN_DISC_APPENDABLE media.
    Note: DVD-R[W] in write mode BURN_WRITE_SAO are not capable of this.
          DVD-R DL are not capable of this at all.
          libburn will refuse to write if burn_write_opts_set_multi() is
          enabled under such conditions.
    @param opts The option object to be manipulated
    @param multi 1=media will be appendable, 0=media will be closed (default) 
    @since 0.2.6
*/
void burn_write_opts_set_multi(struct burn_write_opts *opts, int multi);

/* ts B11204 */
/** Submit an array of CD-TEXT packs which shall be written to the Lead-in
    of a SAO write run on CD.
    @param opts        The option object to be manipulated
    @param text_packs  Array of bytes which form CD-TEXT packs of 18 bytes
                       each. For a description of the format of the array,
                       see file doc/cdtext.txt.
                       No header of 4 bytes must be prepended which would
                       tell the number of pack bytes + 2.
                       This parameter may be NULL if the currently attached
                       array of packs shall be removed.
    @param num_packs   The number of 18 byte packs in text_packs.
                       This parameter may be 0 if the currently attached
                       array of packs shall be removed.
    @param flag        Bitfield for control purposes.
                       bit0= do not verify checksums
                       bit1= repair mismatching checksums
                       bit2= repair checksums if they are 00 00 with each pack
    @return            1 on success, <= 0 on failure
    @since 1.2.0
*/
int burn_write_opts_set_leadin_text(struct burn_write_opts *opts,
                                    unsigned char *text_packs,
                                    int num_packs, int flag);


/* ts A61222 */
/** Sets a start address for writing to media and write modes which allow to
    choose this address at all (for now: DVD+RW, DVD-RAM, formatted DVD-RW).
    now). The address is given in bytes. If it is not -1 then a write run
    will fail if choice of start address is not supported or if the block
    alignment of the address is not suitable for media and write mode.
    Alignment to 32 kB blocks is supposed to be safe with DVD media.
    Call burn_disc_get_multi_caps() can obtain the necessary media info. See
    resulting struct burn_multi_caps elements .start_adr , .start_alignment ,
    .start_range_low , .start_range_high .
    @param opts The write opts to change
    @param value The address in bytes (-1 = start at default address)
    @since 0.3.0
*/
void burn_write_opts_set_start_byte(struct burn_write_opts *opts, off_t value);


/* ts A70213 */
/** Caution: still immature and likely to change. Problems arose with
    sequential DVD-RW on one drive.

    Controls whether the whole available space of the media shall be filled up
    by the last track of the last session.
    @param opts The write opts to change
    @param fill_up_media If 1 : fill up by last track, if 0 = do not fill up
    @since 0.3.4
*/
void burn_write_opts_set_fillup(struct burn_write_opts *opts,
                                int fill_up_media);


/* ts A70303 */
/** Eventually makes libburn ignore the failure of some conformance checks:
    - the check whether CD write+block type is supported by the drive
    - the check whether the media profile supports simulated burning 
    @param opts The write opts to change
    @param use_force 1=ignore above checks, 0=refuse work on failed check
    @since 0.3.4
*/
void burn_write_opts_set_force(struct burn_write_opts *opts, int use_force);


/* ts A80412 */
/** Eventually makes use of the more modern write command AAh WRITE12 and
    sets the Streaming bit. With DVD-RAM and BD this can override the
    traditional slowdown to half nominal speed. But if it speeds up writing
    then it also disables error management and correction. Weigh your
    priorities. This affects the write operations of burn_disc_write()
    and subsequent calls of burn_random_access_write().
    @param opts The write opts to change
    @param value  0=use 2Ah WRITE10, 1=use AAh WRITE12 with Streaming bit
                  @since 0.6.4:
                  >=16 use WRITE12 but not before the LBA given by value
    @since 0.4.6
*/
void burn_write_opts_set_stream_recording(struct burn_write_opts *opts, 
                                         int value);

/* ts A91115 */
/** Overrides the write chunk size for DVD and BD media which is normally
    determined according to media type and setting of stream recording.
    A chunk size of 64 KB may improve throughput with bus systems which show
    latency problems.
    @param opts The write opts to change
    @param obs  Number of bytes which shall be sent by a single write command.
                0 means automatic size, 32768 and 65336 are the only other
                accepted sizes for now.
    @since 0.7.4
*/
void burn_write_opts_set_dvd_obs(struct burn_write_opts *opts, int obs);


/* ts B20406 */
/** Overrides the automatic decision whether to pad up the last write chunk to
    its full size. This applies to DVD, BD and stdio: pseudo-drives.
    Note: This override may get enabled fixely already at compile time by
          defining macro  Libburn_dvd_always_obs_paD .
    @param opts The write opts to change
    @param pad  1 means to pad up in any case, 0 means automatic decision.
    @since 1.2.4
*/  
void burn_write_opts_set_obs_pad(struct burn_write_opts *opts, int pad);


/* ts A91115 */
/** Sets the rythm by which stdio pseudo drives force their output data to
    be consumed by the receiving storage device. This forcing keeps the memory
    from being clogged with lots of pending data for slow devices.
    @param opts   The write opts to change
    @param rythm  Number of 2KB output blocks after which fsync(2) is
                  performed. -1 means no fsync(), 0 means default,
                  elsewise the value must be >= 32.
                  Default is currently 8192 = 16 MB.
    @since 0.7.4
*/
void burn_write_opts_set_stdio_fsync(struct burn_write_opts *opts, int rythm);


/** Sets whether to read in raw mode or not
    @param opts The read opts to change
    @param raw_mode If non-zero, reading will be done in raw mode, so that everything in the data tracks on the
            disc is read, including headers.
*/
void burn_read_opts_set_raw(struct burn_read_opts *opts, int raw_mode);

/** Sets whether to report c2 errors or not 
    @param opts The read opts to change
    @param c2errors If non-zero, report c2 errors.
*/
void burn_read_opts_set_c2errors(struct burn_read_opts *opts, int c2errors);

/** Sets whether to read subcodes from audio tracks or not
    @param opts The read opts to change
    @param subcodes_audio If non-zero, read subcodes from audio tracks on the disc.
*/
void burn_read_opts_read_subcodes_audio(struct burn_read_opts *opts,
					int subcodes_audio);

/** Sets whether to read subcodes from data tracks or not 
    @param opts The read opts to change
    @param subcodes_data If non-zero, read subcodes from data tracks on the disc.
*/
void burn_read_opts_read_subcodes_data(struct burn_read_opts *opts,
				       int subcodes_data);

/** Sets whether to recover errors if possible
    @param opts The read opts to change
    @param hardware_error_recovery If non-zero, attempt to recover errors if possible.
*/
void burn_read_opts_set_hardware_error_recovery(struct burn_read_opts *opts,
						int hardware_error_recovery);

/** Sets whether to report recovered errors or not
    @param opts The read opts to change
    @param report_recovered_errors If non-zero, recovered errors will be reported.
*/
void burn_read_opts_report_recovered_errors(struct burn_read_opts *opts,
					    int report_recovered_errors);

/** Sets whether blocks with unrecoverable errors should be read or not
    @param opts The read opts to change
    @param transfer_damaged_blocks If non-zero, blocks with unrecoverable errors will still be read.
*/
void burn_read_opts_transfer_damaged_blocks(struct burn_read_opts *opts,
					    int transfer_damaged_blocks);

/** Sets the number of retries to attempt when trying to correct an error
    @param opts The read opts to change
    @param hardware_error_retries The number of retries to attempt when correcting an error.
*/
void burn_read_opts_set_hardware_error_retries(struct burn_read_opts *opts,
					       unsigned char hardware_error_retries);


/* ts A90815 */
/** Gets the list of profile codes supported by the drive.
    Profiles depict the feature sets which constitute media types. For
    known profile codes and names see burn_disc_get_profile().
    @param d            is the drive to query
    @param num_profiles returns the number of supported profiles
    @param profiles     returns the profile codes
    @param is_current   returns the status of the corresponding profile code:
                        1= current, i.e. the matching media is loaded
                        0= not current, i.e. the matching media is not loaded
    @return  always 1 for now
    @since 0.7.0
*/
int burn_drive_get_all_profiles(struct burn_drive *d, int *num_profiles,
                                int profiles[64], char is_current[64]);


/* ts A90815 */
/** Obtains the profile name associated with a profile code.
    @param profile_code the profile code to be translated
    @param name         returns the profile name (e.g. "DVD+RW")  
    @return             1= known profile code , 0= unknown profile code
    @since 0.7.0
*/
int burn_obtain_profile_name(int profile_code, char name[80]);


/** Gets the maximum write speed for a drive and eventually loaded media.
    The return value might change by the media type of already loaded media,
    again by call burn_drive_grab() and again by call burn_disc_read_atip(). 
    @param d Drive to query
    @return Maximum write speed in K/s
*/
int burn_drive_get_write_speed(struct burn_drive *d);


/* ts A61021 */
/** Gets the minimum write speed for a drive and eventually loaded media.
    The return value might change by the media type of already loaded media, 
    again by call burn_drive_grab() and again by call burn_disc_read_atip().
    @param d Drive to query
    @return Minimum write speed in K/s
    @since 0.2.6
*/
int burn_drive_get_min_write_speed(struct burn_drive *d);


/** Gets the maximum read speed for a drive
    @param d Drive to query
    @return Maximum read speed in K/s
*/
int burn_drive_get_read_speed(struct burn_drive *d);


/* ts A61226 */
/** Obtain a copy of the current speed descriptor list. The drive's list gets
    updated on various occasions such as burn_drive_grab() but the copy
    obtained here stays untouched. It has to be disposed via
    burn_drive_free_speedlist() when it is not longer needed. Speeds
    may appear several times in the list. The list content depends much on
    drive and media type. It seems that .source == 1 applies mostly to CD media
    whereas .source == 2 applies to any media.
    @param d Drive to query
    @param speed_list The copy. If empty, *speed_list gets returned as NULL.
    @return 1=success , 0=list empty , <0 severe error
    @since 0.3.0
*/
int burn_drive_get_speedlist(struct burn_drive *d,
                             struct burn_speed_descriptor **speed_list);

/* ts A70713 */
/** Look up the fastest speed descriptor which is not faster than the given
    speed_goal. If it is 0, then the fastest one is chosen among the
    descriptors with the highest end_lba. If it is -1 then the slowest speed
    descriptor is chosen regardless of end_lba. Parameter flag decides whether
    the speed goal means write speed or read speed.
    @param d Drive to query
    @param speed_goal Upper limit for speed,
                      0=search for maximum speed , -1 search for minimum speed
    @param best_descr Result of the search, NULL if no match
    @param flag Bitfield for control purposes
                bit0= look for best read speed rather than write speed
                bit1= look for any source type (else look for source==2 first
	              and for any other source type only with CD media)
    @return >0 indicates a valid best_descr, 0 = no valid best_descr
    @since 0.3.8
*/
int burn_drive_get_best_speed(struct burn_drive *d, int speed_goal,
                        struct burn_speed_descriptor **best_descr, int flag);


/* ts A61226 */
/** Dispose a speed descriptor list copy which was obtained by
    burn_drive_get_speedlist().
    @param speed_list The list copy. *speed_list gets set to NULL.
    @return 1=list disposed , 0= *speedlist was already NULL
    @since 0.3.0
*/
int burn_drive_free_speedlist(struct burn_speed_descriptor **speed_list);


/* ts A70203 */
/* @since 0.3.2 */
/** The reply structure for burn_disc_get_multi_caps()
*/
struct burn_multi_caps {

	/* Multi-session capability allows to keep the media appendable after
	   writing a session. It also guarantees that the drive will be able
	   to predict and use the appropriate Next Writeable Address to place
	   the next session on the media without overwriting the existing ones.
	   It does not guarantee that the selected write type is able to do
	   an appending session after the next session. (E.g. CD SAO is capable
	   of multi-session by keeping a disc appendable. But .might_do_sao
	   will be 0 afterwards, when checking the appendable media.)
	    1= media may be kept appendable by burn_write_opts_set_multi(o,1)
 	    0= media will not be appendable
	*/
	int multi_session;

	/* Multi-track capability allows to write more than one track source
	   during a single session. The written tracks can later be found in
	   libburn's TOC model with their start addresses and sizes.
	    1= multiple tracks per session are allowed
	    0= only one track per session allowed
	*/
	int multi_track;

	/* Start-address capability allows to set a non-zero address with
	   burn_write_opts_set_start_byte(). Eventually this has to respect
	   .start_alignment and .start_range_low, .start_range_high in this
	   structure.
	    1= non-zero start address is allowed
            0= only start address 0 is allowed (to depict the drive's own idea
               about the appropriate write start)
	*/
	int start_adr;

	/** The alignment for start addresses.
	    ( start_address % start_alignment ) must be 0.
	*/
	off_t start_alignment;

	/** The lowest permissible start address.
	*/
	off_t start_range_low;

	/** The highest addressable start address.
	*/
	off_t start_range_high;

	/** Potential availability of write modes
	     4= needs no size prediction, not to be chosen automatically
	     3= needs size prediction, not to be chosen automatically
  	     2= available, no size prediction necessary
	     1= available, needs exact size prediction
	     0= not available
	    With CD media (profiles 0x09 and 0x0a) check also the elements
	    *_block_types of the according write mode.
	*/
	int might_do_tao;
	int might_do_sao;
	int might_do_raw;

	/** Generally advised write mode.
	    Not necessarily the one chosen by burn_write_opts_auto_write_type()
	    because the burn_disc structure might impose particular demands.
	*/
	enum burn_write_types advised_write_mode;

	/** Write mode as given by parameter wt of burn_disc_get_multi_caps().
	*/
	enum burn_write_types selected_write_mode;

	/** Profile number which was current when the reply was generated */
	int current_profile;

	/** Wether the current profile indicates CD media. 1=yes, 0=no */
	int current_is_cd_profile;

        /* ts A70528 */
        /* @since 0.3.8 */
	/** Wether the current profile is able to perform simulated write */
	int might_simulate;
};

/** Allocates a struct burn_multi_caps (see above) and fills it with values
    which are appropriate for the drive and the loaded media. The drive
    must be grabbed for this call. The returned structure has to be disposed
    via burn_disc_free_multi_caps() when no longer needed.
    @param d The drive to inquire
    @param wt With BURN_WRITE_NONE the best capabilities of all write modes
              get returned. If set to a write mode like BURN_WRITE_SAO the
              capabilities with that particular mode are returned and the
              return value is 0 if the desired mode is not possible.
    @param caps returns the info structure
    @param flag Bitfield for control purposes (unused yet, submit 0)
    @return < 0 : error , 0 : writing seems impossible , 1 : writing possible 
    @since 0.3.2
*/
int burn_disc_get_multi_caps(struct burn_drive *d, enum burn_write_types wt,
			 struct burn_multi_caps **caps, int flag);

/** Removes from memory a multi session info structure which was returned by
    burn_disc_get_multi_caps(). The pointer *caps gets set to NULL.
    @param caps the info structure to dispose (note: pointer to pointer)
    @return 0 : *caps was already NULL, 1 : memory object was disposed
    @since 0.3.2
*/
int burn_disc_free_multi_caps(struct burn_multi_caps **caps);


/** Gets a copy of the toc_entry structure associated with a track
    @param t Track to get the entry from
    @param entry Struct for the library to fill out
*/
void burn_track_get_entry(struct burn_track *t, struct burn_toc_entry *entry);

/** Gets a copy of the toc_entry structure associated with a session's lead out
    @param s Session to get the entry from
    @param entry Struct for the library to fill out
*/
void burn_session_get_leadout_entry(struct burn_session *s,
                                    struct burn_toc_entry *entry);

/** Gets an array of all complete sessions for the disc
    THIS IS NO LONGER VALID AFTER YOU ADD OR REMOVE A SESSION
    The result array contains *num + burn_disc_get_incomplete_sessions()
    elements. All above *num are incomplete sessions.
    Typically there is at most one incomplete session with one empty track.
    DVD+R and BD-R seem to allow more than one track with even readable data.
    @param d Disc to get session array for
    @param num Returns the number of sessions in the array
    @return array of sessions
*/
struct burn_session **burn_disc_get_sessions(struct burn_disc *d,
                                             int *num);

/* ts B30112 */
/* @since 1.2.8 */
/** Obtains the number of incomplete sessions which are recorded in the
    result array of burn_disc_get_sessions() after the complete sessions.
    See above.
    @param d Disc object to inquire
    @return  Number of incomplete sessions
*/
int burn_disc_get_incomplete_sessions(struct burn_disc *d);


int burn_disc_get_sectors(struct burn_disc *d);

/** Gets an array of all the tracks for a session
    THIS IS NO LONGER VALID AFTER YOU ADD OR REMOVE A TRACK
    @param s session to get track array for
    @param num Returns the number of tracks in the array
    @return array of tracks
*/
struct burn_track **burn_session_get_tracks(struct burn_session *s,
                                            int *num);

int burn_session_get_sectors(struct burn_session *s);

/** Gets the mode of a track
    @param track the track to query
    @return the track's mode
*/
int burn_track_get_mode(struct burn_track *track);

/** Returns whether the first track of a session is hidden in the pregap
    @param session the session to query
    @return non-zero means the first track is hidden
*/
int burn_session_get_hidefirst(struct burn_session *session);

/** Returns the library's version in its parts.
    This is the runtime counterpart of the three build time macros 
    burn_header_version_* below.
    @param major The major version number
    @param minor The minor version number
    @param micro The micro version number
*/
void burn_version(int *major, int *minor, int *micro);


/* ts A80129 */
/* @since 0.4.4 */
/** These three release version numbers tell the revision of this header file
    and of the API it describes. They are memorized by applications at build
    time.
    Immediately after burn_initialize() an application should do this check:
      burn_version(&major, &minor, &micro);
      if(major > burn_header_version_major
         || (major == burn_header_version_major
             && (minor > burn_header_version_minor
                 || (minor == burn_header_version_minor
                     && micro >= burn_header_version_micro)))) {
          ... Young enough. Go on with program run ....
      } else {
          ... Too old. Do not use this libburn version ...
      }

*/
#define burn_header_version_major  1
#define burn_header_version_minor  3
#define burn_header_version_micro  0
/** Note:
    Above version numbers are also recorded in configure.ac because libtool
    wants them as parameters at build time.
    For the library compatibility check, BURN_*_VERSION in configure.ac
    are not decisive. Only the three numbers above do matter.
*/
/** Usage discussion:

Some developers of the libburnia project have differing
opinions how to ensure the compatibility of libaries
and applications.

It is about whether to use at compile time and at runtime
the version numbers isoburn_header_version_* provided here.
Thomas Schmitt advises to use them.
Vreixo Formoso advises to use other means.

At compile time:

Vreixo Formoso advises to leave proper version matching
to properly programmed checks in the the application's
build system, which will eventually refuse compilation.

Thomas Schmitt advises to use the macros defined here
for comparison with the application's requirements of
library revisions and to eventually break compilation.

Both advises are combinable. I.e. be master of your
build system and have #if checks in the source code
of your application, nevertheless.

At runtime (via *_is_compatible()):

Vreixo Formoso advises to compare the application's
requirements of library revisions with the runtime
library. This is to allow runtime libraries which are
young enough for the application but too old for
the lib*.h files seen at compile time.

Thomas Schmitt advises to compare the header
revisions defined here with the runtime library.
This is to enforce a strictly monotonous chain
of revisions from app to header to library,
at the cost of excluding some older libraries.

These two advises are mutually exclusive.

*/

/* ts A91226 */
/** Obtain the id string of the SCSI transport interface.
    This interface may be a system specific adapter module of libburn or
    an adapter to a supporting library like libcdio.
    @param flag  Bitfield for control puposes, submit 0 for now
    @return      A pointer to the id string. Do not alter the string content.
    @since 0.7.6
*/
char *burn_scsi_transport_id(int flag);

/* ts A60924 : ticket 74 */
/** Control queueing and stderr printing of messages from libburn.
    Severity may be one of "NEVER", "ABORT", "FATAL", "FAILURE", "SORRY",
    "WARNING", "HINT", "NOTE", "UPDATE", "DEBUG", "ALL".
    @param queue_severity Gives the minimum limit for messages to be queued.
                          Default: "NEVER". If you queue messages then you
                          must consume them by burn_msgs_obtain().
    @param print_severity Does the same for messages to be printed directly
                          to stderr. Default: "FATAL".
    @param print_id       A text prefix to be printed before the message.
    @return               >0 for success, <=0 for error
    @since 0.2.6
*/
int burn_msgs_set_severities(char *queue_severity,
                             char *print_severity, char *print_id);

/* ts A60924 : ticket 74 */
/*  @since 0.2.6 */
#define BURN_MSGS_MESSAGE_LEN 4096

/** Obtain the oldest pending libburn message from the queue which has at
    least the given minimum_severity. This message and any older message of
    lower severity will get discarded from the queue and is then lost forever.
    @param minimum_severity  may be one of "NEVER", "ABORT", "FATAL",
                      "FAILURE", "SORRY", "WARNING", "HINT", "NOTE", "UPDATE",
                      "DEBUG", "ALL".
                      To call with minimum_severity "NEVER" will discard the
                      whole queue.
    @param error_code Will become a unique error code as listed in
                      libburn/libdax_msgs.h
    @param msg_text   Must provide at least BURN_MSGS_MESSAGE_LEN bytes.
    @param os_errno   Will become the eventual errno related to the message
    @param severity   Will become the severity related to the message and
                      should provide at least 80 bytes.
    @return 1 if a matching item was found, 0 if not, <0 for severe errors
    @since 0.2.6
*/
int burn_msgs_obtain(char *minimum_severity,
                     int *error_code, char msg_text[], int *os_errno,
                     char severity[]);


/* ts A70922 */
/** Submit a message to the libburn queueing system. It will be queued or
    printed as if it was generated by libburn itself.
    @param error_code The unique error code of your message.
                      Submit 0 if you do not have reserved error codes within
                      the libburnia project.
    @param msg_text   Not more than BURN_MSGS_MESSAGE_LEN characters of
                      message text.
    @param os_errno   Eventual errno related to the message. Submit 0 if
                      the message is not related to a operating system error.
    @param severity   One of "ABORT", "FATAL", "FAILURE", "SORRY", "WARNING",
                      "HINT", "NOTE", "UPDATE", "DEBUG". Defaults to "FATAL".
    @param d          An eventual drive to which the message shall be related.
                      Submit NULL if the message is not specific to a
                      particular drive object.
    @return           1 if message was delivered, <=0 if failure
    @since 0.4.0
*/
int burn_msgs_submit(int error_code, char msg_text[], int os_errno,
                     char severity[], struct burn_drive *d);


/* ts A71016 */
/** Convert a severity name into a severity number, which gives the severity
    rank of the name.
    @param severity_name A name as with burn_msgs_submit(), e.g. "SORRY".
    @param severity_number The rank number: the higher, the more severe.
    @param flag Bitfield for control purposes (unused yet, submit 0)
    @return >0 success, <=0 failure
    @since 0.4.0
*/
int burn_text_to_sev(char *severity_name, int *severity_number, int flag);


/* ts A80202 */
/** Convert a severity number into a severity name
    @param severity_number The rank number: the higher, the more severe.
    @param severity_name A name as with burn_msgs_submit(), e.g. "SORRY".
    @param flag Bitfield for control purposes (unused yet, submit 0)
    @return >0 success, <=0 failure
    @since 0.4.4
*/
int burn_sev_to_text(int severity_number, char **severity_name, int flag);


/* ts B21214 */
/** Return a blank separated list of severity names. Sorted from low
    to high severity.
    @param flag Bitfield for control purposes (unused yet, submit 0)
    @return  A constant string with the severity names
    @since 1.2.6
*/
char *burn_list_sev_texts(int flag);


/* ts A70915 */
/** Replace the messenger object handle of libburn by a compatible handle
    obtained from a related library. 
    See also: libisofs, API function iso_get_messenger().
    @param messenger The foreign but compatible message handle.
    @return 1 : success, <=0 : failure
    @since 0.4.0
*/
int burn_set_messenger(void *messenger);


/* ts A61002 */
/* @since 0.2.6 */
/** The prototype of a handler function suitable for burn_set_signal_handling()
    Such a function has to return -2 if it does not want the process to
    exit with value 1.
*/
typedef int (*burn_abort_handler_t)(void *handle, int signum, int flag);

/** Control built-in signal handling. Either by setting an own handler or
    by activating the built-in signal handler.

    A function parameter handle of NULL activates the built-in abort handler. 
    Depending on mode it may cancel all drive operations, wait for all drives
    to become idle, exit(1). It may also prepare function
    burn_drive_get_status() for waiting and performing exit(1). 
    If parameter handle may be NULL or a text that shall be used as prefix for
    pacifier messages of burn_abort_pacifier(). Other than with an application
    provided handler, the prefix char array does not have to be kept existing
    until the eventual signal event.
    Before version 0.7.8 only action 0 was available. I.e. the built-in handler
    waited for the drives to become idle and then performed exit(1) directly.
    But during burn_disc_write() onto real CD or DVD, FreeBSD 8.0 pauses the
    other threads until the signal handler returns.
    The new actions try to avoid this deadlock. It is advised to use action 3
    at least during burn_disc_write(), burn_disc_erase(), burn_disc_format():
      burn_set_signal_handling(text, NULL, 0x30);
    and to call burn_is_aborting(0) when the drive is BURN_DRIVE_IDLE.
    If burn_is_aborting(0) returns 1, then call burn_abort() and exit(1).

    @param handle Opaque handle eventually pointing to an application
                  provided memory object
    @param handler A function to be called on signals, if the handling bits
                  in parameter mode are set 0.
                  It will get parameter handle as argument. flag will be 0.
                  It should finally call burn_abort(). See there.
                  If the handler function returns 2 or -2, then the wrapping
                  signal handler of libburn will return and let the program
                  continue its operations. Any other return value causes
                  exit(1).
    @param mode : bit0 - bit3: Handling of received signals:
                    0 Install libburn wrapping signal handler, which will call
                      handler(handle, signum, 0) on nearly all signals
                    1 Enable system default reaction on all signals
                    2 Try to ignore nearly all signals
                   10 like mode 2 but handle SIGABRT like with mode 0
                  bit4 - bit7: With handler == NULL :
                    Action of built-in handler. "control thread" is the one
                    which called burn_set_signal_handling().
                    All actions activate receive mode 2 to ignore further
                    signals.
                    0 Same as 1 (for pre-0.7.8 backward compatibility)
                    @since 0.7.8
                    1 Catch the control thread in abort handler, call
                      burn_abort(>0) and finally exit(1).
                      Does not always work with FreeBSD.
                    2 Call burn_abort(-1) and return from handler. When the
                      control thread calls burn_drive_get_status(), then do
                      burn_abort(>0) instead, and finally exit(1).
                      Does not always work with FreeBSD.
                    3 Call burn_abort(-1), return from handler. It is duty of
                      the application to detect a pending abort condition
                      by calling burn_is_aborting() and to wait for all
                      drives to become idle. E.g. by calling burn_abort(>0).
                    4 Like 3, but without calling burn_abort(-1). Only the
                      indicator of burn_is_aborting() gets set.
    @since 0.2.6
*/
void burn_set_signal_handling(void *handle, burn_abort_handler_t handler, 
			     int mode);


/* ts B00304 */
/* Inquire whether the built-in abort handler was triggered by a signal.
   This has to be done to detect pending abort handling if signal handling
   was set to the built-in handler and action was set to 2 or 3.
   @param flag  Bitfield for control purposes (unused yet, submit 0)
   @return    0 = no abort was triggered
             >0 = action that was triggered (action 0 is reported as 1)
   @since 0.7.8
*/
int burn_is_aborting(int flag);


/* ts A70811 */
/** Write data in random access mode.
    The drive must be grabbed successfully before calling this function which
    circumvents usual libburn session processing and rather writes data without
    preparations or finalizing. This will work only with overwriteable media
    which are also suitable for burn_write_opts_set_start_byte(). The same
    address alignment restrictions as with this function apply. I.e. for DVD
    it is best to align to 32 KiB blocks (= 16 LBA units). The amount of data
    to be written is subject to the same media dependent alignment rules.
    Again, 32 KiB is most safe.
    Call burn_disc_get_multi_caps() can obtain the necessary media info. See
    resulting struct burn_multi_caps elements .start_adr , .start_alignment ,
    .start_range_low , .start_range_high .
    Other than burn_disc_write() this is a synchronous call which returns
    only after the write transaction has ended (sucessfully or not). So it is
    wise not to transfer giant amounts of data in a single call.
    Important: Data have to fit into the already formatted area of the media.
    @param d            The drive to which to write 
    @param byte_address The start address of the write in byte
                        (1 LBA unit = 2048 bytes) (do respect media alignment)
    @param data         The bytes to be written
    @param data_count   The number of those bytes (do respect media alignment)
                        data_count == 0 is permitted (e.g. to flush the
                        drive buffer without further data transfer).
    @param flag         Bitfield for control purposes:
                        bit0 = flush the drive buffer after eventual writing
    @return 1=sucessful , <=0 : number of transfered bytes * -1
    @since 0.4.0
*/
int burn_random_access_write(struct burn_drive *d, off_t byte_address,
                             char *data, off_t data_count, int flag);


/* ts A81215 */
/** Inquire the maximum amount of readable data.
    It is supposed that all LBAs in the range from 0 to media_read_acpacity-1
    can be read via burn_read_data() although some of them may never have been
    recorded. If tracks are recognizable then it is better to only read
    LBAs which are part of some track.
    If the drive is actually a large file or block device, then the capacity
    is curbed to a maximum of 0x7ffffff0 blocks = 4 TB - 32 KB.
    @param d            The drive from which to read
    @param capacity     Will return the result if valid
    @param flag         Bitfield for control purposes: Unused yet, submit 0.
    @return 1=sucessful , <=0 an error occured
    @since 0.6.0
*/
int burn_get_read_capacity(struct burn_drive *d, int *capacity, int flag);


/* ts A70812 */
/** Read data in random access mode.
    The drive must be grabbed successfully before calling this function.
    With all currently supported drives and media the byte_address has to
    be aligned to 2048 bytes. Only data tracks with 2048 bytes per sector
    can be read this way. I.e. not CD-audio, not CD-video-stream ...
    This is a synchronous call which returns only after the full read job
    has ended (sucessfully or not). So it is wise not to read giant amounts
    of data in a single call.
    @param d            The drive from which to read
    @param byte_address The start address of the read in byte (aligned to 2048)
    @param data         A memory buffer capable of taking data_size bytes
    @param data_size    The amount of data to be read. This does not have to
                        be aligned to any block size.
    @param data_count   The amount of data actually read (interesting on error)
    @param flag         Bitfield for control purposes:
                        bit0= - reserved -
                        bit1= do not submit error message if read error
                        bit2= on error do not try to read a second time
                              with single block steps.
                              @since 0.5.2 
                        bit3= return -2 on permission denied error rather than
                              issueing a warning message.
                              @since 1.0.6
                        bit4= return -3 on SCSI error
                                5 64 00 ILLEGAL MODE FOR THIS TRACK
                              and prevent this error from being reported as
                              event message. Do not retry reading in this case.
                              (Useful to try the last two blocks of a CD
                               track which might be non-data because of TAO.)
                              @since 1.2.6
    @return 1=sucessful , <=0 an error occured
                          with bit3:  -2= permission denied error
    @since 0.4.0
*/
int burn_read_data(struct burn_drive *d, off_t byte_address,
                   char data[], off_t data_size, off_t *data_count, int flag);


/* ts B21119 */
/** Read CD audio sectors in random access mode.
    The drive must be grabbed successfully before calling this function.
    Only CD audio tracks with 2352 bytes per sector can be read this way.
    I.e. not data tracks, not CD-video-stream, ...

    Note that audio data do not have exact block addressing. If you read a
    sequence of successive blocks then you will get a seamless stream
    of data. But the actual start and end position of this audio stream
    will differ by a few dozens of milliseconds, depending on individual
    CD and individual drive.
    Expect leading and trailing zeros, as well as slight truncation. 

    @param d            The drive from which to read.
                        It must be a real MMC drive (i.e. not a stdio file)
                        and it must have a CD loaded (i.e. not DVD or BD).
    @param sector_no    The sector number (Logical Block Address)
                        It may be slightly below 0, depending on drive and
                        medium. -150 is a lower limit.
    @param data         A memory buffer capable of taking data_size bytes
    @param data_size    The amount of data to be read. This must be aligned
                        to full multiples of 2352.
    @param data_count   The amount of data actually read (interesting on error)
    @param flag         Bitfield for control purposes:
                        bit0= - reserved -
                        bit1= do not submit error message if read error
                        bit2= on error do not try to read a second time
                              with single block steps.
                        bit3= Enable DAP : "flaw obscuring mechanisms like
                                            audio data mute and interpolate"
                        bit4= return -3 on SCSI error
                                5 64 00 ILLEGAL MODE FOR THIS TRACK
                              and prevent this error from being reported as
                              event message. Do not retry reading in this case.
                              (Useful to try the last two blocks of a CD
                               track which might be non-audio because of TAO.)
    @return 1=sucessful , <=0 an error occured
                          with bit3:  -2= permission denied error
    @since 1.2.6
*/
int burn_read_audio(struct burn_drive *d, int sector_no,
                    char data[], off_t data_size, off_t *data_count, int flag);


/* ts A70904 */
/** Inquire whether the drive object is a real MMC drive or a pseudo-drive
    created by a stdio: address.
    @param d      The drive to inquire
    @return       0= null-drive
                  1= real MMC drive
                  2= stdio-drive, random access, read-write
                  3= stdio-drive, sequential, write-only
                  4= stdio-drive, random access, read-only
                     (only if enabled by burn_allow_drive_role_4())
                  5= stdio-drive, random access, write-only
                     (only if enabled by burn_allow_drive_role_4())
    @since 0.4.0
*/
int burn_drive_get_drive_role(struct burn_drive *d);


/* ts B10312 */
/** Allow drive role 4 "random access read-only"
    and drive role 5 "random access write-only".
    By default a random access file assumes drive role 2 "read-write"
    regardless whether it is actually readable or writeable.
    If enabled, random-access file objects which recognizably allow no
    writing will be classified as role 4 and those which allow no reading
    will get role 5.
    Candidates are drive addresses of the form stdio:/dev/fd/# , where # is
    the integer number of an open file descriptor. If this descriptor was
    opened read-only resp. write-only, then it gets role 4 resp. role 5.
    Other paths may get tested by an attempt to open them for read-write
    (role 2) resp. read-only (role 4) resp. write-only (role 5). See bit1.
    @param allowed      Bitfield for control purposes:
                        bit0= Enable roles 4 and 5 for drives which get
                              aquired after this call
                        bit1= with bit0:
                              Test whether the file can be opened for
                              read-write resp. read-only resp. write-only.
                              Classify as roles 2 resp. 4 resp. 5.
                        bit2= with bit0 and bit1:
                              Classify files which cannot be opened at all
                              as role 0 : useless dummy.
                              Else classify as role 2.
                        bit3= Classify non-empty role 5 drives as
                              BURN_DISC_APPENDABLE with Next Writeable Address
                              after the end of the file. It is nevertheless
                              possible to change this address by call
                              burn_write_opts_set_start_byte().
    @since 1.0.6
*/
void burn_allow_drive_role_4(int allowed);


/* ts A70923 */
/** Find out whether a given address string would lead to the given drive
    object. This should be done in advance for track source addresses
    with parameter drive_role set to 2. 
    Although a real MMC drive should hardly exist as two drive objects at
    the same time, this can easily happen with stdio-drives. So if more than
    one drive is used by the application, then this gesture is advised:
      burn_drive_d_get_adr(d2, adr2);
      if (burn_drive_equals_adr(d1, adr2, burn_drive_get_drive_role(d2)))
        ... Both drive objects point to the same storage facility ...
 
    @param d1      Existing drive object
    @param adr2    Address string to be tested. Prefix "stdio:" overrides
                   parameter drive_role2 by either 0 or 2 as appropriate.
                   The string must be shorter than BURN_DRIVE_ADR_LEN.
    @param drive_role2  Role as burn_drive_get_drive_role() would attribute
                   to adr2 if it was a drive. Use value 2 for checking track
                   sources resp. pseudo-drive addresses without "stdio:".
                   Use 1 for checking drive addresses including those with
                   prefix "stdio:".
    @return        1= adr2 leads to d1 , 0= adr2 seems not to lead to d1,
                   -1 = adr2 is bad
    @since 0.4.0
*/
int burn_drive_equals_adr(struct burn_drive *d1, char *adr2, int drive_role2);



/*
  Audio track data extraction facility.
*/

/* Maximum size for address paths and fmt_info strings */
#define LIBDAX_AUDIOXTR_STRLEN 4096


/** Extractor object encapsulating intermediate states of extraction.
    The clients of libdax_audioxtr shall only allocate pointers to this
    struct and get a storage object via libdax_audioxtr_new().
    Appropriate initial value for the pointer is NULL.
*/
struct libdax_audioxtr;


/** Open an audio file, check wether suitable, create extractor object.
    @param xtr Opaque handle to extractor. Gets attached extractor object.
    @param path Address of the audio file to extract. "-" is stdin (but might
                be not suitable for all futurely supported formats).
    @param flag Bitfield for control purposes (unused yet, submit 0)
    @return >0 success
             0 unsuitable format
            -1 severe error
            -2 path not found
    @since 0.2.4
*/
int libdax_audioxtr_new(struct libdax_audioxtr **xtr, char *path, int flag);


/** Obtain identification parameters of opened audio source.
    @param xtr Opaque handle to extractor
    @param fmt Gets pointed to the audio file format id text: ".wav" , ".au"
    @param fmt_info Gets pointed to a format info text telling parameters
    @param num_channels     e.g. 1=mono, 2=stereo, etc
    @param sample_rate      e.g. 11025, 44100
    @param bits_per_sample  e.g. 8= 8 bits per sample, 16= 16 bits ...
    @param msb_first Byte order of samples: 0= Intel    = Little Endian
                                            1= Motorola = Big Endian
    @param flag Bitfield for control purposes (unused yet, submit 0)
    @return >0 success, <=0 failure
    @since 0.2.4
*/
int libdax_audioxtr_get_id(struct libdax_audioxtr *xtr,
                           char **fmt, char **fmt_info,
                           int *num_channels, int *sample_rate,
                           int *bits_per_sample, int *msb_first, int flag);


/** Obtain a prediction about the extracted size based on internal information
    of the formatted file.
    @param xtr Opaque handle to extractor
    @param size Gets filled with the predicted size
    @param flag Bitfield for control purposes (unused yet, submit 0)
    @return 1 prediction was possible , 0 no prediction could be made
    @since 0.2.4
*/
int libdax_audioxtr_get_size(struct libdax_audioxtr *o, off_t *size, int flag);


/** Obtain next buffer full of extracted data in desired format (only raw audio
    for now).
    @param xtr Opaque handle to extractor
    @param buffer Gets filled with extracted data
    @param buffer_size Maximum number of bytes to be filled into buffer
    @param flag Bitfield for control purposes
                bit0= do not stop at predicted end of data
    @return >0 number of valid buffer bytes,
             0 End of file
            -1 operating system reports error
            -2 usage error by application
    @since 0.2.4
*/
int libdax_audioxtr_read(struct libdax_audioxtr *xtr,
                         char buffer[], int buffer_size, int flag);


/** Try to obtain a file descriptor which will deliver extracted data
    to normal calls of read(2). This may fail because the format is
    unsuitable for that, but ".wav" is ok. If this call succeeds the xtr
    object will have forgotten its file descriptor and libdax_audioxtr_read()
    will return a usage error. One may use *fd after libdax_audioxtr_destroy()
    and will have to close it via close(2) when done with it.
    @param xtr Opaque handle to extractor
    @param fd Eventually returns the file descriptor number
    @param flag Bitfield for control purposes
                bit0= do not dup(2) and close(2) but hand out original fd
    @return 1 success, 0 cannot hand out fd , -1 severe error
    @since 0.2.4
*/
int libdax_audioxtr_detach_fd(struct libdax_audioxtr *o, int *fd, int flag);


/** Clean up after extraction and destroy extractor object.
    @param xtr Opaque handle to extractor, *xtr is allowed to be NULL,
               *xtr is set to NULL by this function
    @param flag Bitfield for control purposes (unused yet, submit 0)
    @return 1 = destroyed object, 0 = was already destroyed
    @since 0.2.4
*/
int libdax_audioxtr_destroy(struct libdax_audioxtr **xtr, int flag);


#ifndef DOXYGEN

BURN_END_DECLS

#endif


/* ts A91205 */
/* The following experiments may be interesting in future:
*/

/* Perform OPC explicitely.
   # define Libburn_pioneer_dvr_216d_with_opC 1
*/

/* Load mode page 5 and modify it rather than composing from scratch.
   # define Libburn_pioneer_dvr_216d_load_mode5 1
*/

/* Inquire drive events and react by reading configuration or starting unit.
   # define Libburn_pioneer_dvr_216d_get_evenT 1
*/

/* ts A91112 */
/* Do not probe CD modes but declare only data and audio modes supported.
   For other modes resp. real probing one has to call
   burn_drive_probe_cd_write_modes().

*/
#define Libburn_dummy_probe_write_modeS 1

/* ts B30112 */
/* Handle DVD+R with reserved tracks in incomplete first session
   by loading info about the incomplete session into struct burn_disc
*/
#define Libburn_disc_with_incomplete_sessioN 1


/* Early experimental:
   Do not define Libburn_develop_quality_scaN unless you want to work
   towards a usable implementation.
   If it gets enabled, then the call must be published in libburn/libburn.ver
*/
#ifdef Libburn_develop_quality_scaN

/* ts B21108 */
/* Experiments mit quality scan command F3 on Optiarc drive */
int burn_nec_optiarc_rep_err_rate(struct burn_drive *d,
                                  int start_lba, int rate_period, int flag);

#endif /* Libburn_develop_quality_scaN */



#endif /*LIBBURN_H*/
