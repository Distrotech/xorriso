/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2009 - 2013 Thomas Schmitt
 *
 * This file is part of the libisofs project; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

#ifndef LIBISO_ECMA119_H_
#define LIBISO_ECMA119_H_

#include "libisofs.h"
#include "util.h"
#include "buffer.h"

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif

#include <pthread.h>

#define BLOCK_SIZE      2048

/*
 * Maximum file section size. Set to 4GB - 1 =  0xffffffff
 */
#define MAX_ISO_FILE_SECTION_SIZE   0xffffffff

/*
 * When a file need to be splitted in several sections, the maximum size
 * of such sections, but the last one. Set to a multiple of BLOCK_SIZE.
 * Default to 4GB - 2048 = 0xFFFFF800
 */
#define ISO_EXTENT_SIZE             0xFFFFF800

/*
 * The maximum number of partition images that can be registered. Depending
 * on the system area type, the effectively usable number may be smaller or
 * even 0.
 */
#define ISO_MAX_PARTITIONS 8

/*
 * The cylindersize with SUN Disk Label
 * (512 bytes/sector, 640 sectors/head, 1 head/cyl = 320 KiB).
 * Expressed in ECMA-119 blocks of 2048 bytes/block.
 */
#define ISO_SUN_CYL_SIZE 160

/*
 * Maximum length of a disc label text plus 1.
 */
#define ISO_DISC_LABEL_SIZE 129


/* The maximum lenght of an specs violating ECMA-119 file identifier.
   The theoretical limit is  254 - 34 - 28 (len of SUSP CE entry) = 192
   Currently the practical limit is 254 - 34 - 96 (non-CE RR entries) - 28 (CE)
*/
#ifdef Libisofs_with_rrip_rR
#define ISO_UNTRANSLATED_NAMES_MAX 92
#else
#define ISO_UNTRANSLATED_NAMES_MAX 96
#endif


/* The theoretical maximum number of Apple Partition Map entries in the
   System Area of an ISO image:
   Block0 plus 63 entries with block size 512
*/
#define ISO_APM_ENTRIES_MAX 63

/* The maximum number of MBR partition table entries.
*/
#define ISO_MBR_ENTRIES_MAX  4

/* The theoretical maximum number of GPT entries in the System Area of an
   ISO image:
   MBR plus GPT header block plus 248 GPT entries of 128 bytes each. 
*/
#define ISO_GPT_ENTRIES_MAX 248


/**
 * Holds the options for the image generation.
 */
struct iso_write_opts {

    int will_cancel;

    int level; /**< ISO level to write at. (ECMA-119, 10) */

    /** Which extensions to support. */
    unsigned int rockridge :1;
    unsigned int joliet :1;
    unsigned int iso1999 :1;
    unsigned int hfsplus :1;
    unsigned int fat :1;

    unsigned int aaip :1; /* whether to write eventual ACL and EAs */

    /* allways write timestamps in GMT */
    unsigned int always_gmt :1;

    /*
     * Relaxed constraints. Setting any of these to 1 break the specifications,
     * but it is supposed to work on most moderns systems. Use with caution.
     */

    /**
     * Convert directory names for ECMA-119 the same way as other file names
     * but do not force dots or add version numbers.
     * This violates ECMA-119 by allowing one "." and especially ISO level 1
     * by allowing DOS style 8.3 names rather than only 8 characters.
     */
    unsigned int allow_dir_id_ext :1;

    /**
     * Omit the version number (";1") at the end of the ISO-9660 identifiers.
     * Version numbers are usually not used.
     * bit0= ECMA-119 and Joliet (for historical reasons)
     * bit1= Joliet
     */
    unsigned int omit_version_numbers :2;

    /**
     * Allow ISO-9660 directory hierarchy to be deeper than 8 levels.
     */
    unsigned int allow_deep_paths :1;

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
     * bit0= ECMA-119
     * bit1= Joliet
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
     * If not allow_full_ascii is set: allow all 7 bit characters that would
     * be allowed by allow_full_ascii. But still map lowercase to uppercase if
     * not allow_lowercase is set to 1.
     */
    unsigned int allow_7bit_ascii :1;

    /**
     * Allow all characters to be part of Volume and Volset identifiers on
     * the Primary Volume Descriptor. This breaks ISO-9660 contraints, but
     * should work on modern systems.
     */
    unsigned int relaxed_vol_atts :1;

    /**
     * Allow paths in the Joliet tree to have more than 240 characters.
     */
    unsigned int joliet_longer_paths :1;

    /**
     * Allow Joliet names up to 103 characters rather than 64.
     */
    unsigned int joliet_long_names :1;

    /**
     * Write Rock Ridge info as of specification RRIP-1.10 rather than
     * RRIP-1.12: signature "RRIP_1991A" rather than "IEEE_1282",
     *            field PX without file serial number
     */
    unsigned int rrip_version_1_10 :1;

    /**
     * Write field PX with file serial number even with RRIP-1.10
     */
    unsigned int rrip_1_10_px_ino :1;

    /**
     * See iso_write_opts_set_hardlinks()
     */
    unsigned int hardlinks:1;

    /**
     * Write AAIP as extension according to SUSP 1.10 rather than SUSP 1.12.
     * I.e. without announcing it by an ER field and thus without the need
     * to preceed the RRIP fields by an ES and to preceed the AA field by ES.
     * This saves bytes and might avoid problems with readers which dislike
     * ER fields other than the ones for RRIP.
     * On the other hand, SUSP 1.12 frowns on such unannounced extensions
     * and prescribes ER and ES. It does this since year 1994.
     *
     * In effect only if above flag .aaip is set to 1.
     */
    unsigned int aaip_susp_1_10 :1;

    /**
     * Store as ECMA-119 Directory Record timestamp the mtime of the source
     * rather than the image creation time. (The ECMA-119 prescription seems
     * to expect that we do have a creation timestamp with the source.
     * mkisofs writes mtimes and the result seems more suitable if mounted
     * without Rock Ridge support.)
     * bit0= ECMA-119, bit1= Joliet, bit2= ISO 9660:1999
     */
    unsigned int dir_rec_mtime :3;

    /**
     * This describes the directory where to store Rock Ridge relocated
     * directories.
     * If not relaxation "allow_deep_paths" is in effect, it is necessary to
     * relocate directories so that no ECMA-119 file path has more than
     * 8 components. For Rock Ridge the relocated directories are linked forth
     * and back to a placeholder at their original position in path level 8
     * (entries CL and PL). Directories marked by entry RE are to be considered
     * artefacts of relocation and shall not be read into a Rock Ridge tree.
     * For plain ECMA-119, the relocation directory is just a normal directory
     * which contains normal files and directories.
     */
    char *rr_reloc_dir;          /* IsoNode name in root directory */
    int rr_reloc_flags;          /* bit0= mark auto-created rr_reloc_dir by RE
                                    bit1= directory was auto-created
                                          (cannot be set via API)
                                 */

    /**
     * Compute MD5 checksum for the whole session and record it as index 0 of
     * the checksum blocks after the data area of the session. The layout and
     * position of these blocks will be recorded in xattr "isofs.ca" of the
     * root node. See see also API call iso_image_get_session_md5().
     */
    unsigned int md5_session_checksum :1;

    /**
     * Compute MD5 checksums for IsoFile objects and write them to blocks
     * after the data area of the session. The layout and position of these
     * blocks will be recorded in xattr "isofs.ca" of the root node.
     * The indice of the MD5 sums will be recorded with the IsoFile directory
     * entries as xattr "isofs.cx". See also API call iso_file_get_md5().
     * bit0= compute individual checksums
     * bit1= pre-compute checksum and compare it with actual one.
     *       Raise MISHAP if mismatch.
     */
    unsigned int md5_file_checksums :2;

    /** If files should be sorted based on their weight. */
    unsigned int sort_files :1;

    /**
     * The following options set the default values for files and directory
     * permissions, gid and uid. All these take one of three values: 0, 1 or 2.
     * If 0, the corresponding attribute will be kept as setted in the IsoNode.
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

    /**
     * See API call iso_write_opts_set_old_empty().
     */ 
    unsigned int old_empty :1;

    /**
     * Extra Caution: This option breaks any assumptions about names that
     *                are supported by ECMA-119 specifications. 
     * Omit any translation which would make a file name compliant to the
     * ECMA-119 rules. This includes and exceeds omit_version_numbers,
     * max_37_char_filenames, no_force_dots bit0, allow_lowercase.
     * The maximum name length is given by this variable.
     * There is a length limit of ISO_UNTRANSLATED_NAMES_MAX characters,
     * because ECMA-119 allows 254 byte in a directory record, some
     * of them are occupied by ECMA-119, some more are needed for SUSP CE,
     * and some are fixely occupied by libisofs Rock Ridge code.
     * The default value 0 disables this feature.
     */
    unsigned int untranslated_name_len;

    /**
     * 0 to use IsoNode timestamps, 1 to use recording time, 2 to use
     * values from timestamp field. This has only meaning if RR extensions
     * are enabled.
     */
    unsigned int replace_timestamps :2;
    time_t timestamp;

    /**
     * Charset for the RR filenames that will be created.
     * NULL to use default charset, the locale one.
     */
    char *output_charset;

    /**
     * This flags control the type of the image to create. Libisofs support
     * two kind of images: stand-alone and appendable.
     *
     * A stand-alone image is an image that is valid alone, and that can be
     * mounted by its own. This is the kind of image you will want to create
     * in most cases. A stand-alone image can be burned in an empty CD or DVD,
     * or write to an .iso file for future burning or distribution.
     *
     * On the other side, an appendable image is not self contained, it refers
     * to serveral files that are stored outside the image. Its usage is for
     * multisession discs, where you add data in a new session, while the
     * previous session data can still be accessed. In those cases, the old
     * data is not written again. Instead, the new image refers to it, and thus
     * it's only valid when appended to the original. Note that in those cases
     * the image will be written after the original, and thus you will want
     * to use a ms_block greater than 0.
     *
     * Note that if you haven't import a previous image (by means of
     * iso_image_import()), the image will always be a stand-alone image, as
     * there is no previous data to refer to.
     */
    unsigned int appendable : 1;

    /**
     * Start block of the image. It is supposed to be the lba where the first
     * block of the image will be written on disc. All references inside the
     * ISO image will take this into account, thus providing a mountable image.
     *
     * For appendable images, that are written to a new session, you should
     * pass here the lba of the next writable address on disc.
     *
     * In stand alone images this is usually 0. However, you may want to
     * provide a different ms_block if you don't plan to burn the image in the
     * first session on disc, such as in some CD-Extra disc whether the data
     * image is written in a new session after some audio tracks.
     */
    uint32_t ms_block;

    /**
     * When not NULL, it should point to a buffer of at least 64KiB, where
     * libisofs will write the contents that should be written at the beginning
     * of a overwriteable media, to grow the image. The growing of an image is
     * a way, used by first time in growisofs by Andy Polyakov, to allow the
     * appending of new data to non-multisession media, such as DVD+RW, in the
     * same way you append a new session to a multisession disc, i.e., without
     * need to write again the contents of the previous image.
     *
     * Note that if you want this kind of image growing, you will also need to
     * set appendable to "1" and provide a valid ms_block after the previous
     * image.
     *
     * You should initialize the buffer either with 0s, or with the contents of
     * the first blocks of the image you're growing. In most cases, 0 is good
     * enought.
     */
    uint8_t *overwrite;

    /**
     * Size, in number of blocks, of the FIFO buffer used between the writer
     * thread and the burn_source. You have to provide at least a 32 blocks
     * buffer.
     */
    size_t fifo_size;

    /**
     * This is not an option setting but a value returned after the options
     * were used to compute the layout of the image.
     * It tells the LBA of the first plain file data block in the image.
     */
    uint32_t data_start_lba;

    /**
     * If not empty: A text holding parameters "name" and "timestamp" for
     * a scdbackup stream checksum tag. See scdbackup/README appendix VERIFY.
     * It makes sense only for single session images which start at LBA 0.
     * Such a tag may be part of a libisofs checksum tag block after the
     * session tag line. It then covers the whole session up to its own start
     * position.
     */
    char scdbackup_tag_parm[100];

    /* If not NULL: A pointer to an application provided array with
       at least 512 characters. The effectively written scdbackup tag
       will be copied to this memory location.
     */
    char *scdbackup_tag_written;

    /*
     * See ecma119_image : System Area related information
     */
    char *system_area_data;
    int system_area_options;

    /* User settable PVD time stamps */
    time_t vol_creation_time;
    time_t vol_modification_time;
    time_t vol_expiration_time;
    time_t vol_effective_time;
    /* To eventually override vol_creation_time and vol_modification_time
     * by unconverted string with timezone 0
     */
    char vol_uuid[17];

    /* The number of unclaimed 2K blocks before start of partition 1 as of
       the MBR in system area.
       Must be 0 or >= 16. (Actually >= number of voldescr + checksum tag)
    */
    uint32_t partition_offset;
    /* Partition table parameter: 1 to 63, 0= disabled/default */
    int partition_secs_per_head;
    /* 1 to 255, 0= disabled/default */
    int partition_heads_per_cyl;

#ifdef Libisofs_with_libjtE
    /* Parameters and state of Jigdo Template Export environment.
    */
    struct libjte_env *libjte_handle;
#endif /* Libisofs_with_libjtE */

    /* A trailing padding of zero bytes which belongs to the image
    */
    uint32_t tail_blocks;

    /* Eventual disk file path of a PreP partition which shall be prepended
       to HFS+/FAT and IsoFileSrc areas and marked by an MBR partition entry.
    */
    char *prep_partition;

    /* Eventual disk file path of an EFI system partition image which shall
       be prepended to HFS+/FAT and IsoFileSrc areas and marked by a GPT entry.
    */
    char *efi_boot_partition;

    /* Eventual disk file paths of prepared images which shall be appended
       after the ISO image and described by partiton table entries in a MBR
    */
    char *appended_partitions[ISO_MAX_PARTITIONS]; 
    uint8_t appended_part_types[ISO_MAX_PARTITIONS];

    /* Eventual name of the non-ISO aspect of the image. E.g. SUN ASCII label.
     */
    char ascii_disc_label[ISO_DISC_LABEL_SIZE];

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

};

typedef struct ecma119_image Ecma119Image;
typedef struct ecma119_node Ecma119Node;
typedef struct joliet_node JolietNode;
typedef struct iso1999_node Iso1999Node;
typedef struct hfsplus_node HFSPlusNode;
typedef struct Iso_File_Src IsoFileSrc;
typedef struct Iso_Image_Writer IsoImageWriter;

struct ecma119_image
{
    int refcount;

    IsoImage *image;
    Ecma119Node *root;

    int will_cancel :1;

    unsigned int iso_level :2;

    /* extensions */
    unsigned int rockridge :1;
    unsigned int joliet :1;
    unsigned int eltorito :1;
    unsigned int iso1999 :1;
    unsigned int hfsplus :1;
    unsigned int fat :1;

    unsigned int hardlinks:1; /* see iso_write_opts_set_hardlinks() */

    unsigned int aaip :1;     /* see iso_write_opts_set_aaip() */

    /* allways write timestamps in GMT */
    unsigned int always_gmt :1;

    /* relaxed constraints */
    unsigned int allow_dir_id_ext :1;
    unsigned int omit_version_numbers :2;
    unsigned int allow_deep_paths :1;
    unsigned int allow_longer_paths :1;
    unsigned int max_37_char_filenames :1;
    unsigned int no_force_dots :2;
    unsigned int allow_lowercase :1;
    unsigned int allow_full_ascii :1;
    unsigned int allow_7bit_ascii :1;

    unsigned int relaxed_vol_atts : 1;

    /** Allow paths on Joliet tree to be larger than 240 bytes */
    unsigned int joliet_longer_paths :1;

    /** Allow Joliet names up to 103 characters rather than 64  */
    unsigned int joliet_long_names :1;

    /** Write old fashioned RRIP-1.10 rather than RRIP-1.12 */
    unsigned int rrip_version_1_10 :1;

    /** Write field PX with file serial number even with RRIP-1.10 */
    unsigned int rrip_1_10_px_ino :1;

    /* Write AAIP as extension according to SUSP 1.10 rather than SUSP 1.12. */
    unsigned int aaip_susp_1_10 :1;

    /* Store in ECMA-119, Joliet, ISO 9660:1999 timestamp the mtime of source
       bit0= ECMA-119, bit1= Joliet, bit2= ISO 9660:1999.
    */
    unsigned int dir_rec_mtime :3;

    /* The ECMA-119 directory where to store Rock Ridge relocated directories.
    */
    char *rr_reloc_dir;          /* IsoNode name in root directory */
    int rr_reloc_flags; 
    Ecma119Node *rr_reloc_node;  /* Directory node in ecma119_image */

    unsigned int md5_session_checksum :1;
    unsigned int md5_file_checksums :2;

    /*
     * Mode replace. If one of these flags is set, the correspodent values are
     * replaced with values below.
     */
    unsigned int replace_uid :1;
    unsigned int replace_gid :1;
    unsigned int replace_file_mode :1;
    unsigned int replace_dir_mode :1;
    unsigned int replace_timestamps :1;

    uid_t uid;
    gid_t gid;
    mode_t file_mode;
    mode_t dir_mode;
    time_t timestamp;

    unsigned int old_empty :1;
    unsigned int untranslated_name_len;

    /**
     *  if sort files or not. Sorting is based of the weight of each file
     */
    int sort_files;

    char *input_charset;
    char *output_charset;

    /* See iso_write_opts and iso_write_opts_set_hfsp_serial_number().
     * 00...00 means that it shall be generated by libisofs.
     */
    uint8_t hfsp_serial_number[8];

    unsigned int appendable : 1;
    uint32_t ms_block; /**< start block for a ms image */
    time_t now; /**< Time at which writing began. */

    /** Total size of the output. This only includes the current volume. */
    off_t total_size;
    uint32_t vol_space_size;

    /* Bytes already written to image output */
    off_t bytes_written;
    /* just for progress notification */
    int percent_written;

    /*
     * Block being processed, either during image writing or structure
     * size calculation.
     */
    uint32_t curblock;

    /*
     * The address to be used for the content pointer of empty data files.
     */
    uint32_t empty_file_block;

    /*
     * The calculated block address after ECMA-119 tree and eventual
     * tree checksum tag.
     */
    uint32_t tree_end_block;

    /*
     * number of dirs in ECMA-119 tree, computed together with dir position,
     * and needed for path table computation in a efficient way
     */
    size_t ndirs;
    uint32_t path_table_size;
    uint32_t l_path_table_pos;
    uint32_t m_path_table_pos;

    /*
     * Joliet related information
     */
    JolietNode *joliet_root;
    size_t joliet_ndirs;
    uint32_t joliet_path_table_size;
    uint32_t joliet_l_path_table_pos;
    uint32_t joliet_m_path_table_pos;

    /*
     * HFS+ related information
     * (by Vladimir Serbinenko, see libisofs/hfsplus.c)
     */
    HFSPlusNode *hfsp_leafs; 
    struct hfsplus_btree_level *hfsp_levels;
    uint32_t hfsp_nlevels; 
    uint32_t hfsp_part_start;
    uint32_t hfsp_nfiles;
    uint32_t hfsp_ndirs;
    uint32_t hfsp_cat_id;
    uint32_t hfsp_allocation_blocks;
    uint32_t hfsp_allocation_file_start;
    uint32_t hfsp_extent_file_start;
    uint32_t hfsp_catalog_file_start;
    uint32_t hfsp_total_blocks;
    uint32_t hfsp_allocation_size;
    uint32_t hfsp_nleafs;
    uint32_t hfsp_curleaf;
    uint32_t hfsp_nnodes;
    uint32_t hfsp_bless_id[ISO_HFSPLUS_BLESS_MAX];
    uint32_t hfsp_collision_count;

    /*
     * ISO 9660:1999 related information
     */
    Iso1999Node *iso1999_root;
    size_t iso1999_ndirs;
    uint32_t iso1999_path_table_size;
    uint32_t iso1999_l_path_table_pos;
    uint32_t iso1999_m_path_table_pos;

    /*
     * El-Torito related information
     */
    struct el_torito_boot_catalog *catalog;
    IsoFileSrc *cat; /**< location of the boot catalog in the new image */

    int num_bootsrc;
    IsoFileSrc **bootsrc; /* location of the boot images in the new image */

    /*
     * System Area related information
     */
    /* Content of an embedded boot image. Valid if not NULL.
     * In that case it must point to a memory buffer at least 32 kB.
     */
    char *system_area_data;
    /*
     * bit0= Only with DOS MBR
     *       Make bytes 446 - 512 of the system area a partition
     *       table which reserves partition 1 from byte 63*512 to the
     *       end of the ISO image. Assumed are 63 secs/hed, 255 head/cyl.
     *       (GRUB protective msdos label.)
     *       This works with and without system_area_data.
     * bit1= Only with DOS MBR
     *       Apply isohybrid MBR patching to the system area.
     *       This works only with system_area_data plus ISOLINUX boot image
     *       and only if not bit0 is set.
     * bit2-7= System area type
     *       0= DOS MBR
     *       1= MIPS Big Endian Volume Header
     *       2= DEC Boot Block for MIPS Little Endian
     *       3= SUN Disk Label for SUN SPARC
     * bit8-9= Only with DOS MBR
     *       Cylinder alignment mode eventually pads the image to make it
     *       end at a cylinder boundary.
     *       0 = auto (align if bit1)
     *       1 = always align to cylinder boundary
     *       2 = never align to cylinder boundary
     *       3 = always align, additionally pad up and align partitions
     *           which were appended by iso_write_opts_set_partition_img()
     * bit10-13= System area sub type
     *       With type 0 = MBR:
     *         Gets overridden by bit0 and bit1.
     *         0 = no particular sub type
     *         1 = CHRP: A single MBR partition of type 0x96 covers the
     *                   ISO image. Not compatible with any other feature
     *                   which needs to have own MBR partition entries.
     */
    int system_area_options;

    /*
     * Number of pad blocks that we need to write. Padding blocks are blocks
     * filled by 0s that we put between the directory structures and the file
     * data. These padding blocks are added by libisofs to improve the handling
     * of image growing. The idea is that the first blocks in the image are
     * overwritten with the volume descriptors of the new image. These first
     * blocks usually correspond to the volume descriptors and directory
     * structure of the old image, and can be safety overwritten. However,
     * with very small images they might correspond to valid data. To ensure
     * this never happens, what we do is to add padding bytes, to ensure no
     * file data is written in the first 64 KiB, that are the bytes we usually
     * overwrite.
     */
    uint32_t mspad_blocks;

    size_t nwriters;
    IsoImageWriter **writers;

    /* tree of files sources */
    IsoRBTree *files;

    unsigned int checksum_idx_counter;
    void *checksum_ctx;
    off_t checksum_counter;
    uint32_t checksum_rlsb_tag_pos;
    uint32_t checksum_sb_tag_pos;
    uint32_t checksum_tree_tag_pos;
    uint32_t checksum_tag_pos;
    char image_md5[16];
    char *checksum_buffer;
    uint32_t checksum_array_pos;
    uint32_t checksum_range_start;
    uint32_t checksum_range_size;

    char *opts_overwrite; /* Points to IsoWriteOpts->overwrite.
                             Use only underneath ecma119_image_new()
                             and if not NULL*/

    /* ??? Is there a reason why we copy lots of items from IsoWriteOpts
           rather than taking ownership of the IsoWriteOpts object which
           is submitted with ecma119_image_new() ?
     */

    char scdbackup_tag_parm[100];
    char *scdbackup_tag_written;

    /* Buffer for communication between burn_source and writer thread */
    IsoRingBuffer *buffer;

    /* writer thread descriptor */
    pthread_t wthread;
    int wthread_is_running;
    pthread_attr_t th_attr;

    /* User settable PVD time stamps */
    time_t vol_creation_time;
    time_t vol_modification_time;
    time_t vol_expiration_time;
    time_t vol_effective_time;
    /* To eventually override vol_creation_time and vol_modification_time
     * by unconverted string with timezone 0
     */
    char vol_uuid[17];

    /* The number of unclaimed 2K blocks before
       start of partition 1 as of the MBR in system area. */
    uint32_t partition_offset;
    /* Partition table parameter: 1 to 63, 0= disabled/default */
    int partition_secs_per_head;
    /* 1 to 255, 0= disabled/default */
    int partition_heads_per_cyl;

    /* The currently applicable LBA offset. To be subtracted from any LBA
     *  that is mentioned in volume descriptors, trees, path tables,
     *  Either 0 or .partition_offset
     */
    uint32_t eff_partition_offset;

    /* The second ECMA-119 directory tree and path tables */
    Ecma119Node *partition_root;
    uint32_t partition_l_table_pos;
    uint32_t partition_m_table_pos;

    /* The second Joliet directory tree and path tables */
    JolietNode *j_part_root;
    uint32_t j_part_l_path_table_pos;
    uint32_t j_part_m_path_table_pos;

#ifdef Libisofs_with_libjtE
    struct libjte_env *libjte_handle;
#endif /* Libisofs_with_libjtE */

    uint32_t tail_blocks;

    /* Memorized ELF parameters from MIPS Little Endian boot file */
    uint32_t mipsel_e_entry;
    uint32_t mipsel_p_offset;
    uint32_t mipsel_p_vaddr;
    uint32_t mipsel_p_filesz;

    /* A data file of which the position and size shall be written after
       a SUN Disk Label.
    */
    IsoFileSrc *sparc_core_src;

    char *appended_partitions[ISO_MAX_PARTITIONS]; 
    uint8_t appended_part_types[ISO_MAX_PARTITIONS];
    /* Counted in blocks of 2048 */
    uint32_t appended_part_prepad[ISO_MAX_PARTITIONS];
    uint32_t appended_part_start[ISO_MAX_PARTITIONS];
    uint32_t appended_part_size[ISO_MAX_PARTITIONS];

    char ascii_disc_label[ISO_DISC_LABEL_SIZE];

    /* See IsoImage and libisofs.h */
    IsoNode *hfsplus_blessed[ISO_HFSPLUS_BLESS_MAX];

    /* Block sizes come from write options.
       Only change a block size if it is 0. Set only to 512 or 2048.
       If it stays 0 then it will become 512 or 2048 in time.
    */
    /* Blocksize of Apple Partition Map
       May be defined to 512 or 2048 before writer thread starts.
    */
    int apm_block_size;

    /* Allocation block size of HFS+
       May be defined to 512 or 2048 before hfsplus_writer_create().
    */
    int hfsp_block_size;
    int hfsp_cat_node_size; /* 2 * apm_block_size */
    int hfsp_iso_block_fac; /* 2048 / apm_block_size */

    /* Apple Partition Map description. To be composed during IsoImageWriter
       method ->compute_data_blocks() by calling iso_register_apm_entry().
       Make sure that the composing writers get registered before the
       gpt_tail_writer.
    */
    struct iso_apm_partition_request *apm_req[ISO_APM_ENTRIES_MAX];
    int apm_req_count;
    /* bit1= Do not fill gaps in Apple Partition Map 
       bit2= apm_req entries use apm_block_size in start_block and block_count.
             Normally these two parameters are counted in 2 KiB blocks.
    */
    int apm_req_flags;

    /* MBR partition table description. To be composed during IsoImageWriter
       method ->compute_data_blocks() by calling iso_register_mbr_entry().
    */
    struct iso_mbr_partition_request *mbr_req[ISO_MBR_ENTRIES_MAX];
    int mbr_req_count;

    char *prep_partition; 
    uint32_t prep_part_size;

    /* GPT description. To be composed during IsoImageWriter
       method ->compute_data_blocks() by calling iso_register_gpt_entry().
       Make sure that the composing writers get registered before the
       gpt_tail_writer.
    */
    struct iso_gpt_partition_request *gpt_req[ISO_GPT_ENTRIES_MAX];
    int gpt_req_count;
    /* bit0= GPT partitions may overlap */
    int gpt_req_flags;

    char *efi_boot_partition;
    uint32_t efi_boot_part_size;
    IsoFileSrc *efi_boot_part_filesrc; /* Just a pointer. Do not free. */

    /* Messages from gpt_tail_writer_compute_data_blocks() to
       iso_write_system_area().
    */
    /* Start of GPT entries in System Area, block size 512 */
    uint32_t gpt_part_start;
    /* The ISO block number after the backup GPT header , block size 2048 */
    uint32_t gpt_backup_end; 
    uint32_t gpt_backup_size;
    uint32_t gpt_max_entries;
    int gpt_is_computed;

    /* Message from write_head_part1()/iso_write_system_area() to the
       write_data() methods of the writers.
    */
    uint8_t sys_area_as_written[16 * BLOCK_SIZE];

    /* Size of the filesrc_writer area (data file content).
       This is available before any IsoImageWriter.compute_data_blocks()
       is called.
    */
    uint32_t filesrc_start;
    uint32_t filesrc_blocks;

};

#define BP(a,b) [(b) - (a) + 1]

/* ECMA-119, 8.4 */
struct ecma119_pri_vol_desc
{
    uint8_t vol_desc_type            BP(1, 1);
    uint8_t std_identifier           BP(2, 6);
    uint8_t vol_desc_version         BP(7, 7);
    uint8_t unused1                  BP(8, 8);
    uint8_t system_id                BP(9, 40);
    uint8_t volume_id                BP(41, 72);
    uint8_t unused2                  BP(73, 80);
    uint8_t vol_space_size           BP(81, 88);
    uint8_t unused3                  BP(89, 120);
    uint8_t vol_set_size             BP(121, 124);
    uint8_t vol_seq_number           BP(125, 128);
    uint8_t block_size               BP(129, 132);
    uint8_t path_table_size          BP(133, 140);
    uint8_t l_path_table_pos         BP(141, 144);
    uint8_t opt_l_path_table_pos     BP(145, 148);
    uint8_t m_path_table_pos         BP(149, 152);
    uint8_t opt_m_path_table_pos     BP(153, 156);
    uint8_t root_dir_record          BP(157, 190);
    uint8_t vol_set_id               BP(191, 318);
    uint8_t publisher_id             BP(319, 446);
    uint8_t data_prep_id             BP(447, 574);
    uint8_t application_id           BP(575, 702);
    uint8_t copyright_file_id        BP(703, 739);
    uint8_t abstract_file_id         BP(740, 776);
    uint8_t bibliographic_file_id    BP(777, 813);
    uint8_t vol_creation_time        BP(814, 830);
    uint8_t vol_modification_time    BP(831, 847);
    uint8_t vol_expiration_time      BP(848, 864);
    uint8_t vol_effective_time       BP(865, 881);
    uint8_t file_structure_version   BP(882, 882);
    uint8_t reserved1                BP(883, 883);
    uint8_t app_use                  BP(884, 1395);
    uint8_t reserved2                BP(1396, 2048);
};

/* ECMA-119, 8.5 */
struct ecma119_sup_vol_desc
{
    uint8_t vol_desc_type            BP(1, 1);
    uint8_t std_identifier           BP(2, 6);
    uint8_t vol_desc_version         BP(7, 7);
    uint8_t vol_flags                BP(8, 8);
    uint8_t system_id                BP(9, 40);
    uint8_t volume_id                BP(41, 72);
    uint8_t unused2                  BP(73, 80);
    uint8_t vol_space_size           BP(81, 88);
    uint8_t esc_sequences            BP(89, 120);
    uint8_t vol_set_size             BP(121, 124);
    uint8_t vol_seq_number           BP(125, 128);
    uint8_t block_size               BP(129, 132);
    uint8_t path_table_size          BP(133, 140);
    uint8_t l_path_table_pos         BP(141, 144);
    uint8_t opt_l_path_table_pos     BP(145, 148);
    uint8_t m_path_table_pos         BP(149, 152);
    uint8_t opt_m_path_table_pos     BP(153, 156);
    uint8_t root_dir_record          BP(157, 190);
    uint8_t vol_set_id               BP(191, 318);
    uint8_t publisher_id             BP(319, 446);
    uint8_t data_prep_id             BP(447, 574);
    uint8_t application_id           BP(575, 702);
    uint8_t copyright_file_id        BP(703, 739);
    uint8_t abstract_file_id         BP(740, 776);
    uint8_t bibliographic_file_id    BP(777, 813);
    uint8_t vol_creation_time        BP(814, 830);
    uint8_t vol_modification_time    BP(831, 847);
    uint8_t vol_expiration_time      BP(848, 864);
    uint8_t vol_effective_time       BP(865, 881);
    uint8_t file_structure_version   BP(882, 882);
    uint8_t reserved1                BP(883, 883);
    uint8_t app_use                  BP(884, 1395);
    uint8_t reserved2                BP(1396, 2048);
};

/* ECMA-119, 8.2 */
struct ecma119_boot_rec_vol_desc
{
    uint8_t vol_desc_type            BP(1, 1);
    uint8_t std_identifier           BP(2, 6);
    uint8_t vol_desc_version         BP(7, 7);
    uint8_t boot_sys_id              BP(8, 39);
    uint8_t boot_id                  BP(40, 71);
    uint8_t boot_catalog             BP(72, 75);
    uint8_t unused                   BP(76, 2048);
};

/* ECMA-119, 9.1 */
struct ecma119_dir_record
{
    uint8_t len_dr                   BP(1, 1);
    uint8_t len_xa                   BP(2, 2);
    uint8_t block                    BP(3, 10);
    uint8_t length                   BP(11, 18);
    uint8_t recording_time           BP(19, 25);
    uint8_t flags                    BP(26, 26);
    uint8_t file_unit_size           BP(27, 27);
    uint8_t interleave_gap_size      BP(28, 28);
    uint8_t vol_seq_number           BP(29, 32);
    uint8_t len_fi                   BP(33, 33);
    uint8_t file_id                  BP(34, 34); /* 34 to 33+len_fi */
    /* padding field (if len_fi is even) */
    /* system use (len_dr - len_su + 1 to len_dr) */
};

/* ECMA-119, 9.4 */
struct ecma119_path_table_record
{
    uint8_t len_di                   BP(1, 1);
    uint8_t len_xa                   BP(2, 2);
    uint8_t block                    BP(3, 6);
    uint8_t parent                   BP(7, 8);
    uint8_t dir_id                   BP(9, 9); /* 9 to 8+len_di */
    /* padding field (if len_di is odd) */
};

/* ECMA-119, 8.3 */
struct ecma119_vol_desc_terminator
{
    uint8_t vol_desc_type            BP(1, 1);
    uint8_t std_identifier           BP(2, 6);
    uint8_t vol_desc_version         BP(7, 7);
    uint8_t reserved                 BP(8, 2048);
};

void ecma119_set_voldescr_times(IsoImageWriter *writer,
                                struct ecma119_pri_vol_desc *vol);

/* Copies a data file into the ISO image output stream */
int iso_write_partition_file(Ecma119Image *target, char *path,
                             uint32_t prepad, uint32_t blocks, int flag);

#endif /*LIBISO_ECMA119_H_*/
