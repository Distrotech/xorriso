
/* Command line oriented batch and dialog tool which creates, loads,
   manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2013 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains inner declarations of xorriso.
   The public interface is in xorriso.h
*/


/* For now, #ifdef Xorriso_is_xorriso_selF  has no meaning.
   But it is already now to be set only by the xorriso.c module.
*/

#ifndef Xorriso_private_includeD
#define Xorriso_private_includeD yes


/* <<< Disable this to disable pthread_mutex locking on message and result
       output.
*/
#define Xorriso_fetch_with_msg_queueS yes


/* for uint32_t */
#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif


/** The source code release timestamp */
#include "xorriso_timestamp.h"
#ifndef Xorriso_timestamP
#define Xorriso_timestamP "-none-given-"
#endif

/** The binary build timestamp is to be set externally by the compiler
    or by a macro definition in xorriso_buildstamp.h.
*/
#include "xorriso_buildstamp.h"
#ifndef Xorriso_build_timestamP
#define Xorriso_build_timestamP "-none-given-"
#endif


#include "sfile.h"
#include "misc_funct.h"


struct ExclusionS;        /* List of -not_* conditions */
struct PermiteM;          /* Stack of temporarily altered access permissions */
struct CheckmediajoB;     /* Parameters for Xorriso_check_media() */
struct SectorbitmaP;      /* Distiniction between valid and invalid sectors */
struct FindjoB;           /* Program and status of a find run */


/* maximum number of history lines to be reported with -status:long_history */
#define Xorriso_status_history_maX 100


/** The list of startup file names */
#define Xorriso_rc_nuM 4


/* Default setting for the size limit of single data files:
   100 extents with 4 GB - 2 kB each = 400 GB - 200 kB
*/
#define Xorriso_default_file_size_limiT \
        (((off_t) 400) * ((off_t) 1024*1024*1024) - (off_t) 204800)


/* Maximum number of appended partitions. Effectively usable number depends
   on system area type.
*/
#define Xorriso_max_appended_partitionS 8


/*
  Maximum length of a disc label text plus 1.
*/
#define Xorriso_disc_label_sizE 129


struct XorrisO { /* the global context of xorriso */

 int libs_are_started;

 /* source */
 char progname[SfileadrL];
 char initial_wdx[SfileadrL];
 int no_rc;

 /* Command line argument emulations:
      0=xorriso mode
      1=mkisofs mode
      2=cdrecord mode
 */
 int argument_emulation;

 /** List of startupfiles */
 char rc_filenames[Xorriso_rc_nuM][SfileadrL];
 int rc_filename_count;

 int arrange_args;

 /* Whether .mkisofsrc has already been read */
 int mkisofsrc_done;

 char wdi[SfileadrL];
 char wdx[SfileadrL];
 int did_something_useful;

 int add_plainly;
 off_t split_size;

 char list_delimiter[81];

 /* >>> put libisofs aspects here <<< */

 int ino_behavior; /* bit0= at image load time:
                            Do not load PX inode numbers but generate new
                            unique ones for all loaded IsoNode.
                      bit1= at image generation time:
                            Do not consolidate suitable nodes to hardlinks.
                      bit2= at restore-to-disk time:
                            Do not consolidate suitable nodes to hardlinks.
                      bit3= with update:
                            Do not try to detect hardlink splits and joinings.
                      bit4= with extract:
                            Do not create or use hln arrays if sort_lba_on
                      bit5= with command -lsl
                            Do not create hln array for hard link count
                    */

 int iso_level;
 int iso_level_is_default;
 int do_joliet;
 int do_hfsplus;
 int do_fat;
 int do_rockridge;
 int do_iso1999;

 int do_aaip; /* bit0= ACL in
                 bit1= ACL out
                 bit2= EA in
                 bit3= EA out
                 bit4= record dev,inode per node, isofs_st_out in root
                 bit5= check dev,inode,isofs_st_in
                 bit6= omit content check if bit5 check is conclusive
                 bit7= omit dev check with bit5
                 bit8= store output charset in xattr "isofs.cs"
                 bit9= allow to set input charset from xattr "isofs.cs"
              */

 int do_md5;  /* bit0= read MD5 array
                 bit1= write session MD5
                 bit2= write MD5 for each data file
                 bit3= make file content stability check by double reading
                 bit4= use recorded MD5 as proxy of ISO file
                 bit5= with bit0: do not check tags of superblock,tree,session
               */

 int no_emul_toc; /* bit0= On overwriteables: 
                           write first session to LBA 0 rather than 32.
                  */

 int do_old_empty; /* See -compliance old_empty
                      own data content: range [0,31]. The new way is to have
                      a dedicated block to which all such files will point.
                    */

 char scdbackup_tag_name[81];
 char scdbackup_tag_time[19];
 char scdbackup_tag_written[512];
 char scdbackup_tag_listname[SfileadrL];

 int relax_compliance;        /* opaque bitfield to be set by xorrisoburn */
 int allow_dir_id_ext_dflt; /* -compliance allow_dir_id_ext still on default */
 char rr_reloc_dir[256];
 int rr_reloc_flags;
 int untranslated_name_len;
 int do_follow_pattern;
 int do_follow_param;
 int do_follow_links;
 int follow_link_limit;
 int do_follow_mount;
 int do_global_uid;
 uid_t global_uid;
 int do_global_gid;
 gid_t global_gid;
 int do_global_mode;
 mode_t global_dir_mode;
 mode_t global_file_mode;

 int do_tao; /* 1= Use TAO resp. Incremental
               -1= Use SAO resp. DAO
                0= let libburn choose */

 struct Xorriso_lsT *filters;
 int filter_list_closed;

 int zlib_level;
 int zlib_level_default;
 int zisofs_block_size;
 int zisofs_block_size_default;
 int zisofs_by_magic;

 int do_overwrite; /* 0=off, 1=on, 2=nondir */
 int do_reassure;  /* 0=off, 1=on, 2=tree */

 char volid[33];
 int volid_default;
 char loaded_volid[33];
 char assert_volid[SfileadrL];
 char assert_volid_sev[80];

 char preparer_id[129];

 char publisher[129];
 char application_id[129];
 char system_id[33];
 char volset_id[129];

 char copyright_file[38];
 char biblio_file[38];
 char abstract_file[38];

 char session_logfile[SfileadrL];
 int session_lba;
 int session_blocks;

 /* >>> put libburn/isoburn aspects here */

 struct Xorriso_lsT *drive_blacklist;
 struct Xorriso_lsT *drive_greylist;
 struct Xorriso_lsT *drive_whitelist;

 int toc_emulation_flag; /* bit0= bit3 for isoburn_drive_aquire()
                                  scan -ROM profiles for ISO sessions
                            bit1= bit4 for isoburn_drive_aquire()
                                  do not emulate TOC on overwriteable media
                            bit2= bit7 for isoburn_drive_aquire()
                                  pretend any media to be -ROM
                            bit3= bit9 for isoburn_drive_aquire()
                                  Ignore enclosing session at LBA 0
                         */

 int image_start_mode;  /* From what address to load the ISO image
                           bit0-15= addressing mode
                            0= automatic lba as deduced from media
                            1= value is session number
                            2= value is track number
                            3= value is lba
                           bit16= with mode 3 : value is possibly 16 too high.
                                  Let isoburn_set_msc1() adjust it.
                           bit30= interference with normal msc1 processing
                                  is enabled. Without this bit,
                                  isoburn_set_msc1() will not be called.
                           bit31= image loading has happened,
                                  setting is kept for rollback only.
                                  Always apply as 0=auto.
                        */
 char image_start_value[81]; /* value according image_start_mode */

 uint32_t displacement;
 int displacement_sign;

 int drives_exclusive;   /* burn_preset_device_open() param exclusive */

 int early_stdio_test;   /* For burn_allow_drive_role_4():
                            bit1= Test whether a stdio drive can be opened for
                                  read-write resp. read-only resp. write only.
                            bit2= Classify files which cannot be opened at all
                                  as role 0 : useless dummy.
                            bit3= Classify non-empty role 5 drives as
                                  BURN_DISC_APPENDABLE with NWA after the
                                  end of the file. It is nevertheless
                                  possible to change this address by call
                                  burn_write_opts_set_start_byte().
                          */

 int cache_num_tiles;     /* -data_cache_size */
 int cache_tile_blocks;
 int cache_default;       /* bit0= cache_num_tiles, bit1= cache_tile_blocks */

 int do_calm_drive;      /* bit0= calm down drive after aquiring it */

 char indev[SfileadrL];
 void *in_drive_handle;  /* interpreted only by libburnia oriented modules */
 void *in_volset_handle; /* interpreted only by libburnia oriented modules */
 char *in_charset;       /* The charset to interpret the filename bytes */
 int indev_is_exclusive;
 char indev_off_adr[SfileadrL]; /* Result of burn_drive_convert_fs_adr(indev)
                                   when indev gets aquired. */

 time_t isofs_st_out;    /* A time point at least 1 second before image
                            composition began. To be stored with image as
                            xattr "isofs.st". */
 time_t isofs_st_in;     /* That time point as read from "isofs.st" of the
                            loaded image. */

 int volset_change_pending; /* whether -commit would make sense
                               0= no change pending , 1= change pending
                               2= change pending, but -as misofs -print-size
                                  was performed on the changed image model
                             */
 int no_volset_present;     /* set to 1 on first failure */

 struct CheckmediajoB *check_media_default;
 int check_media_bad_limit;      /* values defined as Xorriso_read_quality_* */
 struct SectorbitmaP *in_sector_map; /* eventual sector validity bitmap */


 char outdev[SfileadrL];
 void *out_drive_handle; /* interpreted only by xorrisoburn.c */
 char *out_charset;      /* The charset to produce the filename bytes for */
 int dev_fd_1; /* The fd which substitutes for /dev/fd/1 and is
                     connected to externaly perveived stdout.
                  */
 int outdev_is_exclusive;
 char outdev_off_adr[SfileadrL]; /* Result of burn_drive_convert_fs_adr(outdev)
		        	    when outdev gets aquired. */

 int grow_blindly_msc2; /* if >= 0 this causes growing from drive to drive.
                           The value is used as block address offset for
                           image generation. Like in: mkisofs -C msc1,msc2
                         */

 int ban_stdio_write;
 int do_dummy;
 int do_close;
 int speed;     /* in libburn units : 1000 bytes/second , 0 = Max, -1 = Min */
 int fs;        /* fifo size in 2048 byte chunks : at most 1 GB */
 int padding;   /* number of bytes to add after ISO 9660 image */
 int do_padding_by_libisofs; /* 0= by libburn , 1= by libisofs */

 int alignment; /* if > 0 : output size alignment in 2048 byte blocks.
                            This is always done by libburn, i.e. attached
                            outside the image. Eventual inner alignment of
                            the image end happens first.
                */

 int do_stream_recording; /* 0=no, 1=yes, 2=for data, not for dir
                             >=16 means yes with number as start LBA  */

 int dvd_obs;                 /* DVD write chunk size: 0, 32k or 64k */
 int stdio_sync;              /* stdio fsync interval: -1, 0, >=32 */

 int keep_boot_image;
 char boot_image_cat_path[SfileadrL];
 int boot_image_cat_hidden;   /* bit0= hidden in ISO/RR , bit1= in Joliet ,
                                 bit2= in HFS+ */
 int boot_count; /* number of already attached boot images */

 char boot_image_bin_path[SfileadrL];
 char boot_image_bin_form[16];
 int boot_platform_id; 
 int patch_isolinux_image; /* bit0= boot-info-table , bit1= not with EFI
                              bit2-7= Mentioning in isohybrid GPT
                                      1=EFI, 2=HFS+
                              bit8= Mention in isohybrid Apple Partition Map
                              bit9= GRUB2 boot provisions (patch at byte 1012)
                            */
 int boot_image_emul; /* 0=no emulation
                         1=emulation as hard disk
                         2=emulation as floppy
                      */
 int boot_emul_default;  /* 1= boot_image_emul is still default */
 off_t boot_image_load_size;
 unsigned char boot_id_string[29];
 unsigned char boot_selection_crit[21];

 int boot_image_isohybrid; /* 0=off , deprecated: 1=auto , 2=on , 3=force */

 int boot_efi_default; /* 0= no effect ,
                          1= appy --efi-boot parameters when attaching to img */

 char system_area_disk_path[SfileadrL];
 int system_area_options;             /* bit0= "GRUB protective msdos label"
                                               (a simple partition table)
                                         bit1= isohybrid boot image pointer
                                               and partition table
                                         bit2-7= System area type
                                               0= with bit0 or bit1: MBR
                                                  else: unspecified type 
                                               1= MIPS Big Endian Volume Header
                                               2= MIPS Little Endian Boot Block
                                               3= SUN Disk Label for SUN SPARC
                                         bit8-9= Only with System area type 0
                                               Cylinder alignment mode
                                               0 = auto (align if bit1)
                                               1 = always align
                                               2 = never align
                                         bit10-13= System area sub type
                                               With type 0 = MBR:
                                               Gets overridden by bit0 and bit1.
                                               0 = no particular sub type
                                               1 = CHRP: A single MBR partition
                                                   of type 0x96 covers the ISO
                                                   image. Not compatible with 
                                                   any other feature which
                                                   needs to have own MBR
                                                   partition entries.
                                         bit14= Only with System area type 0
                                               GRUB2 boot provisions:
                                               Patch system area at byte 92 to
                                               99 with 512-block address + 1
                                               of the first boot image file.
                                               Little-endian 8-byte.
                                               Should be combined with
                                               options bit0.
                                       */
 int patch_system_area;               /* Bits as of system_area_options.
                                         to be applied to the loaded system
                                         area of the image, if no 
                                         system_area_disk_path is set.
                                       */

 /* The number of unclaimed 2K blocks before start of partition 1 as of
    the MBR in system area.
    If not 0 this will cause double volume descriptor sets and double tree.
 */
 uint32_t partition_offset;
 /* Partition table parameter: 1 to 63, 0= disabled/default */
 int partition_secs_per_head;
 /* 1 to 255, 0= disabled/default */
 int partition_heads_per_cyl;

 /* Disk file paths of content of PreP partition and EFI system partition */
 char prep_partition[SfileadrL];
 char efi_boot_partition[SfileadrL];

 /* Path and type of image files to be appended as MBR partitions */
 char *appended_partitions[Xorriso_max_appended_partitionS];
 uint8_t appended_part_types[Xorriso_max_appended_partitionS];

 /* Eventual name of the non-ISO aspect of the image. E.g. SUN ASCII label.
 */
 char ascii_disc_label[Xorriso_disc_label_sizE];

 /* A data file of which the position and size shall be written after
    a SUN Disk Label.
 */
 char grub2_sparc_core[SfileadrL];

 /* HFS+ image serial number.
    00...00 means that it shall be generated by libisofs.
 */
 uint8_t hfsp_serial_number[8];

 /* Allocation block size of HFS+  and APM : 0= auto , 512, or 2048
 */
 int hfsp_block_size;
 int apm_block_size;

 /* User settable PVD time stamps */
 time_t vol_creation_time;
 time_t vol_modification_time;
 time_t vol_expiration_time;
 time_t vol_effective_time;
 /* To eventually override vol_modification_time by unconverted string
    and timezone 0 */
 char vol_uuid[17];

#ifdef Xorriso_with_libjtE
 /* Parameters and state of Jigdo Template Export environment */
 struct libjte_env *libjte_handle;
#endif

 /* List of -jigdo parameters since the most recent -jigdo clear */
 struct Xorriso_lsT *jigdo_params;
 struct Xorriso_lsT *jigdo_values;
 int libjte_params_given; /* bits: 0= outfile , 1= verbosity , 2= template_path
                                   3= jigdo_path , 4= md5_path , 5= min_size
                                   6= checksum_iso , 7= checksum_template
                                   8= compression , 9= exclude , 10= demand_md5
                                  11= mapping
                          */

 /* LBA of boot image after image loading */
 int loaded_boot_bin_lba;
 /* Path of the catalog node after image loading */
 char loaded_boot_cat_path[SfileadrL];

 /* XORRISO options */
 int allow_graft_points;

 int allow_restore; /* -2=disallowed until special mode "unblock"
                       -1=permanently disallowed
                        0=disallowed, 1=allowed, 2=device files allowed */
 int do_concat_split;  /* 1= restore complete split file directories as
                             regular files
                        */
 int do_auto_chmod;    /* 1= eventually temporarily open access permissions
                             of self-owned directories during restore
                        */
 int do_restore_sort_lba; /* 1= restore via node_array rather than via
                                tree traversal. Better read performance,
                                no directory mtime restore, needs do_auto_chmod
                           */
 int do_strict_acl;       /* bit0= do not tolerate inappropriate presence or
                                   absence of directory "default" ACL
                          */

 int mount_opts_flag; /* bit0= "shared" = not "exclusive"
                               Try to emit non-exclusive mount command.
                               Do not give up drives.
                               Linux: use loop device even on block devices
                               in order to circumvent the ban to mount a
                               device twice (with different sbsector=)
                               FreeBSD: ?
                       */

 int dialog;  /* 0=off , 1=single-line , 2=multi-line */

 int bsl_interpretation;
                     /* whether to run input through Sfile_bsl_interpreter():
                        bit0-1= dialog and quoted file reading
                          0= no interpretation, leave unchanged
                          1= only inside double quotes
                          2= outside single quotes
                          3= everywhere
                        bit2-3= reserved as future expansion of bit0-1
                        bit4= interpretation within program start arguments
                        bit5= perform backslash encoding with results
                        bit6= perform backslash encoding with info texts
                     */

 /* Pattern matching facility. It still carries legacy from scdbackup/askme.c
    but is fully functional for xorriso.
 */
 int search_mode;
 /* 0= start text
    1= fgrep , 
    2= regular expression
    3= (eventually structured) shell parser expression
    4= shell parser expression for leaf name
 */

 int structured_search;
 /* 0= flat text search
    1= '/' is a significant separator that cannot be matched by wildcards
    ( 2= like 1 : but report only occurence in tree, no payload, no location )
    ( 3= like 2 : but report first content level of matching directories )
    4= actually not structured but unique find mode (with search_mode 4)
 */

 int do_iso_rr_pattern; /* 0=off, 1=on, 2=ls */
 int do_disk_pattern;   /* 0=off, 1=on, 2=ls */

 int temp_mem_limit;

 off_t file_size_limit;

 struct ExclusionS *disk_exclusions;
 int disk_excl_mode; /* bit0= on (else off)
                        bit1= parameter too (else rekursion only)
                        bit2= whole subtree banned (else only exact path)
                        bit3= when comparing ignore excluded files rather
                              than to treat them as truely missing on disk
                     */

 struct ExclusionS *iso_rr_hidings;
 struct ExclusionS *joliet_hidings;
 struct ExclusionS *hfsplus_hidings;

 int use_stdin; /* use raw stdin even if readline support is compiled */
 int result_page_length;
 int result_page_width;
 char mark_text[SfileadrL]; /* ( stdout+stderr, M: ) */
 int packet_output;
 char logfile[4][SfileadrL];
 FILE *logfile_fp[4];
 FILE *pktlog_fp;
 FILE *stderr_fp;
 struct Xorriso_lsT *result_msglists[Xorriso_max_outlist_stacK];
 struct Xorriso_lsT *info_msglists[Xorriso_max_outlist_stacK];
 int msglist_flags[Xorriso_max_outlist_stacK]; /* bit0= result is redirected
                                                  bit1= info is redirected
                                               */
 int msglist_stackfill;

 int lib_msg_queue_lock_ini;
 int result_msglists_lock_ini;
 pthread_mutex_t lib_msg_queue_lock;
 pthread_mutex_t result_msglists_lock;

 int write_to_channel_lock_ini;
 pthread_mutex_t write_to_channel_lock;

 int msg_watcher_lock_ini;
 pthread_mutex_t msg_watcher_lock;
 int msg_watcher_state; /* 0= inactive
                           1= registered
                           2= started
                           3= request to end
                        */
 int (*msgw_result_handler)(void *handle, char *text);
 void *msgw_result_handle;
 int (*msgw_info_handler)(void *handle, char *text);
 void *msgw_info_handle;
 int msgw_stack_handle;
 int msgw_msg_pending; /* 0=no, 1=fetching(i.e. maybe) , 2=yes */
 int msgw_fetch_lock_ini;
 pthread_mutex_t msgw_fetch_lock;

 struct Xorriso_msg_sievE *msg_sieve;
 int msg_sieve_disabled;

 int status_history_max; /* for -status long_history */
 
 /* 0= no logging of SCSI commands, 1= to stderr */
 int scsi_log;

 char report_about_text[20];
 int report_about_severity;
 int library_msg_direct_print;
 char abort_on_text[20];
 int abort_on_severity; /* A severity rank number as threshold */
 int abort_on_is_default; /* will be set to 0 by first -abort_on */
 int problem_status; /* Severity rank number. 0= no abort condition present */
 char problem_status_text[20];
 int problem_status_lock_ini;
 pthread_mutex_t problem_status_lock;

 char errfile_log[SfileadrL]; /* for -errfile_log */
 int errfile_mode; /* bit0= marked */
 FILE *errfile_fp;

 int img_read_error_mode; /* 0=best_effort , 1=failure , 2=fatal */
 int extract_error_mode; /* 0=best_effort , 1=keep , 2=delete */

 char return_with_text[20];
 int return_with_severity;
 int return_with_value;
 int eternal_problem_status;
 char eternal_problem_status_text[20];

 /* temporary search facilities */
 regex_t *re;
 regmatch_t match[1]; 
 char **re_constants;
 int re_count;
 int re_fill;
 char reg_expr[2*SfileadrL];

 /* run state */
 int run_state; /* 0=preparing , 1=writing image */
 int is_dialog;
 int bar_is_fresh;
 char pending_option[SfileadrL]; /* eventual option entered at page prompt */
 int request_to_abort; /* abort a single operation like -ls, not the program */
 int request_not_to_ask; /* suppress reassure and pager */
 double idle_time;
 int re_failed_at; /* mismatch position with structured_search */
 int prepended_wd;
 double insert_count;
 double insert_bytes;
 double error_count; /* double will not roll over */
 int launch_frontend_banned;

 /* pacifiers */
 int pacifier_style; /* 0= xorriso, 1=mkisofs 2=cdrecord */
 double pacifier_interval;
 double start_time;
 double last_update_time;
 /* optional global counters for brain reduced callback functions */
 off_t pacifier_count;
 off_t pacifier_total;
 off_t pacifier_byte_count; /* auxiliary counter for data bytes */
 off_t pacifier_prev_count; /* internal counter for speed measurement */

 void *pacifier_fifo;

 int find_compare_result; /* 1=everything matches , 0=mismatch , -1=error */ 
 int find_check_md5_result; /* bit0= seen mismatch
                               bit1= seen error
                               bit2= seen data file without MD5
                               bit3= seen match
                             */

 double last_abort_file_time; /* most recent check for aborting -check_md5 */

 /* Tree node collection and LBA sorting facility */
 int node_counter;
 int node_array_size;
 void **node_array;
 struct Xorriso_lsT *node_disk_prefixes;
 struct Xorriso_lsT *node_img_prefixes;

 /* Hardlink matching at restore time memorizes hardlink target paths.
    Array of nodes sorted by LBA. */
 int hln_count;
 void **hln_array;
 void **hln_targets;
 int hln_change_pending; /* whether a change was made since hln creation */

 /* >>> this should count all temp_mem and thus change its name */
 off_t node_targets_availmem;

 /* Hardlink matching at update time:
    Array of all nodes in the tree, sorted by disk dev,ino.
    Bitmap of nodes which possibly got new hardlink siblings.
    List of involved disk-iso path pairs. */
 int di_count;
 void **di_array;
 char *di_do_widen;
 struct Xorriso_lsT *di_disk_paths;
 struct Xorriso_lsT *di_iso_paths;

 struct PermiteM *perm_stack; /* Temporarily altered dir access permissions */

 /* bit0= update_merge active: mark all newly added nodes as visited+found
 */
 int update_flags;

 /* result (stdout, R: ) */
 char result_line[10*SfileadrL];
 int result_line_counter;
 int result_page_counter;
 int result_open_line_len;


 /* info (stderr, I:) */
 char info_text[10*SfileadrL];

};


#include "base_obj.h"
#include "aux_objects.h"
#include "findjob.h"
#include "check_media.h"
#include "misc_funct.h"
#include "text_io.h"
#include "match.h"
#include "emulators.h"
#include "disk_ops.h"
#include "cmp_update.h"
#include "parse_exec.h"


#endif /* Xorriso_private_includeD */

