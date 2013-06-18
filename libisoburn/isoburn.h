
/*
  Class struct of libisoburn.

  Copyright 2007 - 2012 Vreixo Formoso Lopes <metalpain2002@yahoo.es>
                    and Thomas Schmitt <scdbackup@gmx.net>
  Provided under GPL version 2 or later.
*/

#ifndef Isoburn_includeD
#define Isoburn_includeD


/* for uint8_t */
#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif

/* For emulated TOC of overwriteable media.
   Provides minimal info for faking a struct burn_toc_entry.
*/
struct isoburn_toc_entry {
 int session;
 int track_no; /* point */
 int start_lba;
 int track_blocks;
 char *volid; /* For caching a volume id from emulated toc on overwriteables */

 struct isoburn_toc_entry *next;
};

int isoburn_toc_entry_new(struct isoburn_toc_entry **objpt,
                          struct isoburn_toc_entry *boss, int flag);

/* @param flag bit0= delete all subordinates too
*/
int isoburn_toc_entry_destroy(struct isoburn_toc_entry **o, int flag);


/* Minimal size of target_iso_head which is to be written during
   isoburn_activate_session().
   Within this size there is everything that is needed for image access with
   no partition offset. The actual target_iso_head buffer must be larger by
   the evential partition offset.
*/
#define Libisoburn_target_head_sizE (32*2048)


/* Maximum number of appended partitions. Effectively usable number depends
   on system area type.
*/
#define Libisoburn_max_appended_partitionS 8

/*
  Maximum length of a disc label text plus 1.
*/
#define Libisoburn_disc_label_sizE 129


struct isoburn {


 /* The libburn drive to which this isoburn object is related
    Most isoburn calls will use a burn_drive as object handle */
 struct burn_drive *drive;

 /* -1= inappropriate medium state detected
    0= libburn multi-session medium, resp. undecided yet
    1= random access medium */
 int emulation_mode;

 /* Although rarely used, libburn can operate on several
    drives simultaneously. */
 struct isoburn *prev;
 struct isoburn *next;


 /* If >= 0, this address is used as reply for isoburn_disc_get_msc1()
 */
 int fabricated_msc1;

 /* If >= 0, this address is used in isoburn_disc_track_lba_nwa()
    as reply parameter nwa.
    (The other nwa parameters below apply only to the effective write address
     on random access media. msc2 is handed to libisofs but not to libburn.)
 */
 int fabricated_msc2;
 

 /* The nwa to be used for a first session on the present kind of overwriteable
    media (usually Libisoburn_overwriteable_starT, but might be forced to 0)
 */
 int zero_nwa;

 /* Start address as given by image examination (bytes, not blocks) */
 off_t min_start_byte;

 /* Aligned start address to be used for processing (counted in blocks) */
 int nwa;


 /* Truncate to .nwa an eventual regular file serving as output drive */
 int truncate;

 /* Eventual freely fabricated isoburn_disc_get_status().
    BURN_DISC_UNREADY means that this variable is disabled
    and normally emulated status is in effect.
 */
 enum burn_disc_status fabricated_disc_status;

 /* To be set if read errors occured during media evaluation.
 */
 int media_read_error;

 /* Eventual emulated table of content read from the chain of ISO headers
    on overwriteable media.
 */ 
 struct isoburn_toc_entry *toc;

 /* Indicator wether the most recent burn run worked :
    -1 = undetermined, ask libburn , 0 = failure , 1 = success
    To be inquired by isoburn_drive_wrote_well()
 */
 int wrote_well;

 
 /* ISO head buffer to be filled by write run */
 int target_iso_head_size;
 uint8_t *target_iso_head;

 /* The 2k offset which was read from a loaded image.
 */
 uint32_t loaded_partition_offset;
 

 /* Libisofs image context */
 IsoImage *image;

 /* The start LBA of the image */
 int image_start_lba;

 /* The block data source from which the existing image is read.
 */
 IsoDataSource *iso_data_source;

 /* The burn source which transfers data from libisofs to libburn.
    It has its own fifo.
 */
 struct burn_source *iso_source;

 /* For iso_tree_set_report_callback() */
 int (*read_pacifier)(IsoImage*, IsoFileSource*);

 /* For iso_image_attach_data() */
 void *read_pacifier_handle;

 /* An application provided method to immediately deliver messages */
 int (*msgs_submit)(void *handle, int error_code, char msg_text[],
                    int os_errno, char severity[], int flag);
 void *msgs_submit_handle;  /* specific to application method */
 int msgs_submit_flag;      /* specific to application method */

 /* Forwarding an image generation option to the burn wrapper */
 int do_tao;
 
};


/* Creation and disposal function */
int isoburn_new(struct isoburn **objpt, int flag);
int isoburn_destroy(struct isoburn **objpt, int flag);

/* Eventual readers for public attributes */
/* ( put into separate .h file then ) */
int isoburn_get_emulation_mode(struct isoburn *o, int *pt, int flag);
int isoburn_get_target_volset(struct isoburn *o, IsoImage **pt, int flag);

/* List management */
int isoburn_get_prev(struct isoburn *o, struct isoburn **pt, int flag);
int isoburn_get_next(struct isoburn *o, struct isoburn **pt, int flag);
int isoburn_destroy_all(struct isoburn **objpt, int flag);
int isoburn_link(struct isoburn *o, struct isoburn *link, int flag);
int isoburn_count(struct isoburn *o, int flag);
int isoburn_by_idx(struct isoburn *o, int idx, struct isoburn **pt, int flag);
int isoburn_find_by_drive(struct isoburn **pt, struct burn_drive *d, int flag);


/* Non API inner interfaces */

/* Submit a libisofs error to the libburn messenger. An application message
   reader shall recognize the error code range and attribute it to the
   libisofs message channel to which one cannot submit via API.
   @param iso_error_code   return value <= 0 from a libisofs API call.
   @param default_msg_text is to be put out if iso_error_code leads to no
                           error message
   @param os_errno         operating system errno, submit 0 if none is known
   @param min_severity     minimum severity, might be be increased if libisofs
                           error severity surpasses min_severity.
   @param flag             Bitfield, submit 0 for now
*/
int isoburn_report_iso_error(int iso_error_code, char default_msg_text[],
                             int os_errno, char min_severity[], int flag);

/* Calls from burn_wrap.c into isofs_wrap.c */ 

int isoburn_start_emulation(struct isoburn *o, int flag);
int isoburn_invalidate_iso(struct isoburn *o, int flag);


/* Calls from isofs_wrap.c into burn_wrap.c */

/** Get an eventual isoburn object which is wrapped around the drive.
    @param pt    Eventually returns a pointer to the found object.
                 It is allowed to become NULL if return value is -1 or 0.
                 In this case, the drive is a genuine libburn drive
                 with no emulation activated by isoburn.
    @param drive The drive to be searched for
    @param flag  unused yet
    @return -1 unsuitable medium, 0 generic medium, 1 emulated medium.
*/
int isoburn_find_emulator(struct isoburn **pt,
                          struct burn_drive *drive, int flag);

/* Deliver an event message. Either via a non-NULL o->msgs_submit() method 
   or via burn_msgs_submit() of libburn.
*/
int isoburn_msgs_submit(struct isoburn *o, int error_code, char msg_text[], 
                        int os_errno, char severity[], int flag);

/** Set the start address for an emulated add-on session. The value will
    be rounded up to the alignment necessary for the medium. The aligned
    value will be divided by 2048 and then put into  o->nwa .
    @param o     The isoburn object to be programmed.
    @param value The start address in bytes
    @param flag  unused yet
    @return <=0 is failure , >0 success
*/
int isoburn_set_start_byte(struct isoburn *o, off_t value, int flag);

/** Obtains the image address offset to be used with image generation.
    This is either the (emulated) drive nwa or a value set by
    isoburn_prepare_blind_grow().
    In any case this is the address to tell to iso_write_opts_set_ms_block().
    @param o     The isoburn object to be inquired
    @param opts  If not NULL: write parameters to be set on drive before query
    @param msc2  The value to be used with iso_write_opts_set_ms_block()
    @param flag  unused yet
    @return <=0 is failure , >0 success
*/
int isoburn_get_msc2(struct isoburn *o,
                     struct burn_write_opts *opts, int *msc2, int flag);

/** Get a data source suitable for read from a drive using burn_read_data()
    function.
    @param d drive to read from. Must be grabbed.
    @param displacement will be added or subtracted to any block address
    @param displacement_sign  +1 = add , -1= subtract , else keep unaltered
    @return the data source, NULL on error. Must be freed with libisofs
            iso_data_source_unref() function. Note: this doesn't release
            the drive.
*/
IsoDataSource *
isoburn_data_source_new(struct burn_drive *d,
                         uint32_t displacement, int displacement_sign,
                         int cache_tiles, int tile_blocks);

/** Default settings for above cache_tiles, tile_blocks in newly created
    struct isoburn_read_opts.
*/
#define Libisoburn_default_cache_tileS 32
#define Libisoburn_default_tile_blockS 32

/** Maximum size of the cache in 2 kB blocks (1 GB)
*/
#define Libisoburn_cache_max_sizE (1024 * 512)


/** Disable read capabilities of a data source which was originally created
    by isoburn_data_source_new(). After this any attempt to read will yield
    a FATAL programming error event.
    This is usually done to allow libburn to release the drive while libisofs
    still holds a reference to the data source object. libisofs is not supposed
    to use this object for reading any more, nevertheless. The disabled state
    of the data source is a safety fence around this daring situation.
    @param src The data source to be disabled
    @param flag  unused yet
    @return <=0 is failure , >0 success
*/
int isoburn_data_source_shutdown(IsoDataSource *src, int flag);


/** Check whether the size of target_iso_head matches the given partition
    offset. Eventually adjust size.
*/
int isoburn_adjust_target_iso_head(struct isoburn *o,
                                   uint32_t offst, int flag);


/** Initialize the root directory attributes of a freshly created image.
*/
int isoburn_root_defaults(IsoImage *image, int flag);


/**
 * Options for image reading.
   (Comments here may be outdated. API getter/setter function descriptions
    may override the descriptions here. Any difference is supposed to be a
    minor correction only.)
 */
struct isoburn_read_opts {
    int cache_tiles; /* number of cache tiles */
    int cache_tile_blocks;

    unsigned int norock:1; /*< Do not read Rock Ridge extensions */
    unsigned int nojoliet:1; /*< Do not read Joliet extensions */
    unsigned int noiso1999:1; /*< Do not read ISO 9660:1999 enhanced tree */

    /* ts A90121 */
    unsigned int noaaip:1;   /* Do not read AAIP for ACL and EA */
    unsigned int noacl:1;    /* Do not read ACL from external file objects */
    unsigned int noea:1;     /* Do not read XFS-style EA from externals */

    /* ts A90508 */
    unsigned int noino:1;    /* Discard eventual PX inode numbers */

    /* ts A90810 */
    unsigned int nomd5:2;    /* Do not read eventual MD5 array */

    unsigned int preferjoliet:1; 
                /*< When both Joliet and RR extensions are present, the RR
                 *  tree is used. If you prefer using Joliet, set this to 1. */
    uid_t uid; /**< Default uid when no RR */
    gid_t gid; /**< Default uid when no RR */
    mode_t mode; /**< Default mode when no RR (only permissions) */
    mode_t dirmode; /**< Default mode for directories 
                         when no RR (only permissions) */

    /**
     * Input charset for RR file names. NULL to use default locale charset.
     */
    char *input_charset;

    /**
     * Enable or disable methods to automatically choose an input charset.
     * This eventually overrides input_charset.
     *
     * bit0= allow to set the input character set automatically from
     *       attribute "isofs.cs" of root directory
     */
    int auto_input_charset;

    /* modified by the function isoburn_read_image */
    unsigned int hasRR:1; /*< It will be set to 1 if RR extensions are present,
                             to 0 if not. */
    unsigned int hasJoliet:1; /*< It will be set to 1 if Joliet extensions are 
                                  present, to 0 if not. */

    /**
     * It will be set to 1 if the image is an ISO 9660:1999, i.e. it has
     * a version 2 Enhanced Volume Descriptor.
     */
    unsigned int hasIso1999:1;

    /** It will be set to 1 if El-Torito boot record is present, to 0 if not.*/
    unsigned int hasElTorito:1;

    uint32_t size; /**< Will be filled with the size (in 2048 byte block) of 
                    *   the image, as reported in the PVM. */
    unsigned int pretend_blank:1; /* always create empty image */

    uint32_t displacement;
    int displacement_sign;
};


/**
 * Options for image generation by libisofs and image transport to libburn.
   (Comments here may be outdated. API getter/setter function descriptions
    may override the descriptions here. Any difference is supposed to be a
    minor correction only.)
 */
struct isoburn_imgen_opts {

    /* Options for image generation */

    int will_cancel :1;

    int level;  /**< ISO level to write at. */
    
    /** Which extensions to support. */
    unsigned int rockridge :1;
    unsigned int joliet :1;
    unsigned int iso1999 :1;
    unsigned int hfsplus :1;
    unsigned int fat :1;

    /* Whether to mark suitable IsoNode as hardlinks in RRIP PX */
    unsigned int hardlinks :1;

    /* Write eventual AAIP info containing ACL and EA */
    unsigned int aaip :1;

    /* Produce and write a MD5 checksum of the whole session stream. */
    unsigned int session_md5 :1;

    /* Produce and write MD5 checksums for each single IsoFile.
       See parameter "files" of iso_write_opts_set_record_md5().
     */
    unsigned int file_md5 :2;

    /* On overwriteable media or random access files do not write the first
       session to LBA 32, but rather to LBA 0 directly.
     */
    unsigned int no_emul_toc :1;

    /* For empty files, symbolic links, and devices use the old ECMA-119 block
       addresses in the range [0,31] rather than the address of the dedicated
       empty block.
    */
    unsigned int old_empty :1;


    /* relaxed constraints */

    /*
     * Relaxed constraints. Setting any of these to 1 break the specifications,
     * but it is supposed to work on most moderns systems. Use with caution.
     */

    /*
     * Extra Caution: This option breaks any assumptions about names that
     *                are supported by ECMA-119 specifications.
     * Omit any translation which would make a file name compliant to the
     * ECMA-119 rules. This includes and exceeds omit_version_numbers,
     * max_37_char_filenames, no_force_dots bit0, allow_lowercase.
     */
    unsigned int untranslated_name_len;

    /*
     * Convert directory names for ECMA-119 similar to other file names, but do
     * not force a dot or add a version number.
     * This violates ECMA-119 by allowing one "." and especially ISO level 1
     * by allowing DOS style 8.3 names rather than only 8 characters.
     * (mkisofs and its clones seem to do this violation.)
     */
    unsigned int allow_dir_id_ext :1;

    /**
     * Omit the version number (";1") at the end of the ISO-9660 identifiers.
     * Version numbers are usually not used.
     *      bit0= omit version number with ECMA-119 and Joliet
     *      bit1= omit version number with Joliet alone
     */
    unsigned int omit_version_numbers :2;

    /**
     * Allow ISO-9660 directory hierarchy to be deeper than 8 levels.
     */
    unsigned int allow_deep_paths :1;

    /**
     * If not allow_deep_paths is in effect, then it may become
     * necessary to relocate directories so that no ECMA-119 file path
     * has more than 8 components. These directories are grafted into either
     * the root directory of the ISO image or into a dedicated relocation
     * directory. For details see libisofs.h, iso_write_opts_set_rr_reloc().
     */
    char *rr_reloc_dir;       /* IsoNode name in root directory. NULL or
                                 empty text means root itself. */
    int rr_reloc_flags;       /* bit0= mark auto-created rr_reloc_dir by RE
                                 bit1= not settable via API (used internally)
                              */



    /**
     * Allow path in the ISO-9660 tree to have more than 255 characters.
     */
    unsigned int allow_longer_paths :1;

    /**
     * Allow a single file or directory hierarchy to have up to 37 characters.
     * This is larger than the 31 characters allowed by ISO level 2, and the
     * extra space is taken from the version number, so this also forces
     * omit_version_numbers.
     */
    unsigned int max_37_char_filenames :1;

    /**
     * ISO-9660 forces filenames to have a ".", that separates file name from
     * extension. libisofs adds it if original filename doesn't has one. Set
     * this to 1 to prevent this behavior
     *      bit0= no forced dot with ECMA-119
     *      bit1= no forced dot with Joliet
     */
    unsigned int no_force_dots :2;

    /**
     * Allow lowercase characters in ISO-9660 filenames. By default, only
     * uppercase characters, numbers and a few other characters are allowed.
     */
    unsigned int allow_lowercase :1;

    /**
     * Allow all ASCII characters to be appear on an ISO-9660 filename. Note
     * that "/" and "\0" characters are never allowed, even in RR names.
     */
    unsigned int allow_full_ascii :1;

    /**
     * Like allow_full_ascii, but only allowing 7-bit characters.
     * Lowercase letters get mapped to uppercase if not allow_lowercase is set.
     * Gets overridden if allow_full_ascii is enabled.
     */
    unsigned int allow_7bit_ascii :1;

    /**
     * Allow paths in the Joliet tree to have more than 240 characters.
     */
    unsigned int joliet_longer_paths :1;

    /**
     * Allow leaf names in the Joliet tree to have up to 103 characters
     * rather than 64.
     */
    unsigned int joliet_long_names :1;

    /**
     * Store timestamps as GMT rather than in local time.
     */
    unsigned int always_gmt :1;

    /**
     * Write Rock Ridge info as of specification RRIP-1.10 rather than
     * RRIP-1.12: signature "RRIP_1991A" rather than "IEEE_1282",
     *            field PX without file serial number
     */
    unsigned int rrip_version_1_10 :1;

    /**
     * Store as ECMA-119 Directory Record timestamp the mtime
     * of the source rather than the image creation time.
     * The same can be ordered for Joliet and ISO 9660:1999
     */
    unsigned int dir_rec_mtime :1;
    unsigned int joliet_rec_mtime :1;
    unsigned int iso1999_rec_mtime :1;

    /**
     * Write AAIP as extension according to SUSP 1.10 rather than SUSP 1.12.
     * I.e. without announcing it by an ER field and thus without the need
     * to preceed the RRIP fields by an ES and to preceed the AA field by ES.
     */
    unsigned int aaip_susp_1_10 :1;

    unsigned int sort_files:1;
                /**< If files should be sorted based on their weight. */

    /**
     * The following options set the default values for files and directory
     * permissions, gid and uid. All these take one of three values: 0, 1 or 2.
     * If 0, the corresponding attribute will be kept as set in the IsoNode.
     * Unless you have changed it, it corresponds to the value on disc, so it
     * is suitable for backup purposes. If set to 1, the corresponding attrib.
     * will be changed by a default suitable value. Finally, if you set it to
     * 2, the attrib. will be changed with the value specified in the options
     * below. Note that for mode attributes, only the permissions are set, the
     * file type remains unchanged.
     */
    unsigned int replace_dir_mode :2; 
    unsigned int replace_file_mode :2;
    unsigned int replace_uid :2;
    unsigned int replace_gid :2;
    
    mode_t dir_mode; /** Mode to use on dirs when replace_dir_mode == 2. */
    mode_t file_mode; /** Mode to use on files when replace_file_mode == 2. */
    uid_t uid; /** uid to use when replace_uid == 2. */
    gid_t gid; /** gid to use when replace_gid == 2. */

    char *output_charset; /**< NULL to use default charset */


    /* Options for image transport */

    /** The number of bytes to be used for the fifo which decouples libisofs
        and libburn for better throughput and for reducing the risk of
        interrupting signals hitting the libburn thread which operates the
        MMC drive.
        The size will be rounded up to the next full 2048.
        Minimum is 64kiB, maximum is 1 GiB (but that is too much anyway).
    */
    int fifo_size;


    /** Output value: Block address of session start as evaluated from medium
                      and other options by libisoburn and libburn.
        If <0 : Invalid
        If >=0: Valid block number. Block size is always 2 KiB.
    */
    int effective_lba;

    /** Output value: Block address of data section start as predicted by
                      libisofs.
        If < 16: Invalid
        If >=16: Valid block number. Block size is always 2 KiB.
    */
    int data_start_lba;

    /**
     * If not empty: Parameters "name" and "timestamp" for a scdbackup stream
     * checksum tag. See scdbackup/README appendix VERIFY.
     * It makes sense only for single session images which start at LBA 0.
     * Such a tag may be part of a libisofs checksum tag block after the
     * session tag line. It then covers the whole session up to its own start
     * position.
     * If scdbackup_tag_written is not NULL then it is a pointer to an
     * application provided array with at least 512 characters. The effectively
     * written scdbackup tag will be copied to this memory location.
     */
    char scdbackup_tag_name[81];
    char scdbackup_tag_time[19];
    char *scdbackup_tag_written;


    /* Content of an embedded boot image. Valid if not NULL.
     * In that case it must point to a memory buffer at least 32 kB.
     */
    char *system_area_data;
    /*
     * bit0= make bytes 446 - 512 of the system area a partition
     *       table which reserves partition 1 from byte 63*512 to the
     *       end of the ISO image. Assumed are 63 secs/hed, 255 head/cyl.
     *       (GRUB protective msdos label.)
     *       This works with and without system_area_data.
     */
    int system_area_options;

    /* User settable PVD time stamps */
    time_t vol_creation_time;
    time_t vol_modification_time;
    time_t vol_expiration_time;
    time_t vol_effective_time;
    /* To eventually override vol_modification_time by unconverted string
       and timezone 0 */
    char vol_uuid[17];

    /* The number of unclaimed 2K blocks before start of partition 1 as of
       the MBR in system area. If not 0 this will cause double volume
       descriptor sets and double tree.
    */
    uint32_t partition_offset;
    /* Partition table parameter: 1 to 63, 0= disabled/default */
    int partition_secs_per_head; 
    /* 1 to 255, 0= disabled/default */
    int partition_heads_per_cyl;

    /* Parameters and state of Jigdo Template Export environment.
    */
    void *libjte_handle;

    /* A trailing padding of zero bytes which belongs to the image
    */
    uint32_t tail_blocks;

    /* Disk file paths of content of PreP partition and EFI system partition */
    char *prep_partition;
    char *efi_boot_partition;

    /* Eventual disk file paths of prepared images which shall be appended
       after the ISO image and described by partiton table entries in a MBR.
    */
    char *appended_partitions[Libisoburn_max_appended_partitionS];
    uint8_t appended_part_types[Libisoburn_max_appended_partitionS];

    /* Eventual name of the non-ISO aspect of the image. E.g. SUN ASCII label.
     */
    char ascii_disc_label[Libisoburn_disc_label_sizE];

    /* HFS+ image serial number.
     * 00...00 means that it shall be generated by libisofs.
     */
    uint8_t hfsp_serial_number[8];

    /* Allocation block size of HFS+ : 0= auto , 512, or 2048
     */
    int hfsp_block_size;

    /* Block size of and in APM : 0= auto , 512, or 2048
     */
    int apm_block_size;

    /* Write mode for optical media:
     *   0 = auto
     *   1 = TAO, Incremental, no RESERVE TRACK
     *  -1 = SAO, DAO, RESERVE TRACK
     */
    int do_tao;

};


/* Alignment for session starts on overwriteable media.
   (Increased from 16 to 32 blocks for aligning to BD-RE clusters.)
*/
#define Libisoburn_nwa_alignemenT 32


/* Alignment for outer session scanning with -ROM drives.
   (E.g. my DVD-ROM drive shows any DVD type as 0x10 "DVD-ROM" with
    more or less false capacity and TOC.)
*/
#define Libisoburn_toc_scan_alignemenT 16

/* Maximum gap to be bridged during a outer TOC scan. Gaps appear between the
   end of a session and the start of the next session.
   The longest gap found so far was about 38100 after the first session of a
   DVD-R.
*/
#define Libisoburn_toc_scan_max_gaP 65536


/* Creating a chain of image headers which form a TOC:

   The header of the first session is written after the LBA 0 header.
   So it persists and can give the end of its session. By help of
   Libisoburn_nwa_alignemenT it should be possible to predict the start
   of the next session header.
   The LBA 0 header is written by isoburn_activate_session() already
   with the first session. So the medium is mountable.
   A problem arises with DVD-RW in Intermediate State. They cannot be
   written by random access before they were written sequentially.
   In this case, no copy of the session  1 header is maintained and no TOC
   will be possible. Thus writing begins sequentially at LBA 0.

   IMPORTANT: This macro gives the minimal size of an image header. 
              It has to be enlarged by the eventual partition offset.
*/
#define Libisoburn_overwriteable_starT \
                                   ((off_t) (Libisoburn_target_head_sizE/2048))


/* Wrappers for emulation of TOC on overwriteable media */

struct isoburn_toc_track {
 /* Either track or toc_entry are supposed to be NULL */
 struct burn_track *track;
 struct isoburn_toc_entry *toc_entry;
};

struct isoburn_toc_session {
 /* Either session or tracks and toc_entry are supposed to be NULL */
 struct burn_session *session;
 struct isoburn_toc_track **track_pointers;
 int track_count;
 struct isoburn_toc_entry *toc_entry;
};

struct isoburn_toc_disc {
 /* Either disc or sessions and toc are supposed to be NULL */
 struct burn_disc *disc; 
 struct isoburn_toc_session *sessions;           /* storage array */
 struct isoburn_toc_session **session_pointers;  /* storage array */
 struct isoburn_toc_track *tracks;               /* storage array */
 struct isoburn_toc_track **track_pointers;      /* storage array */
 int session_count;
 int incomplete_session_count;
 int track_count;
 struct isoburn_toc_entry *toc;
};

#endif /* Isoburn_includeD */

