/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2009 - 2011 Thomas Schmitt
 *
 * This file is part of the libisofs project; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

/*
 * Filesystem/FileSource implementation to access an ISO image, using an
 * IsoDataSource to read image data.
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "libisofs.h"
#include "ecma119.h"
#include "messages.h"
#include "rockridge.h"
#include "image.h"
#include "tree.h"
#include "eltorito.h"
#include "node.h"
#include "aaip_0_2.h"

#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <langinfo.h>
#include <limits.h>
#include <stdio.h>


/* Enable this and write the correct absolute path into the include statement
   below in order to test the pending contribution to syslinux:
     http://www.syslinux.org/archives/2013-March/019755.html

 # def ine Libisofs_syslinux_tesT 1

*/
#ifdef Libisofs_syslinux_tesT
#define Isolinux_rockridge_in_libisofS 1
#include "/reiser/syslinux/core/fs/iso9660/susp_rr.c"
/*
 # inc lude "/home/thomas/projekte/cdrskin_dir/libisoburn-develop/test/susp_rr.c"
*/
#endif /* Libisofs_syslinux_tesT */


/**
 * Options for image reading.
 * There are four kind of options:
 * - Related to multisession support.
 *   In most cases, an image begins at LBA 0 of the data source. However,
 *   in multisession discs, the later image begins in the last session on
 *   disc. The block option can be used to specify the start of that last
 *   session.
 * - Related to the tree that will be read.
 *   As default, when Rock Ridge extensions are present in the image, that
 *   will be used to get the tree. If RR extensions are not present, libisofs
 *   will use the Joliet extensions if available. Finally, the plain ISO-9660
 *   tree is used if neither RR nor Joliet extensions are available. With
 *   norock, nojoliet, and preferjoliet options, you can change this
 *   default behavior.
 * - Related to default POSIX attributes.
 *   When Rock Ridege extensions are not used, libisofs can't figure out what
 *   are the the permissions, uid or gid for the files. You should supply
 *   default values for that.
 */
struct iso_read_opts
{
    /**
     * Block where the image begins, usually 0, can be different on a
     * multisession disc.
     */
    uint32_t block;

    unsigned int norock : 1; /*< Do not read Rock Ridge extensions */
    unsigned int nojoliet : 1; /*< Do not read Joliet extensions */
    unsigned int noiso1999 : 1; /*< Do not read ISO 9660:1999 enhanced tree */
    unsigned int noaaip : 1; /* Do not read AAIP extension for xattr and ACL */
    unsigned int nomd5 : 2;  /* Do not read MD5 array */

    /**
     * Hand out new inode numbers and overwrite eventually read PX inode
     * numbers. This will split apart any hardlinks.
     */
    unsigned int make_new_ino : 1 ;

    /**
     * When both Joliet and RR extensions are present, the RR tree is used.
     * If you prefer using Joliet, set this to 1.
     */
    unsigned int preferjoliet : 1;

    uid_t uid; /**< Default uid when no RR */
    gid_t gid; /**< Default uid when no RR */
    mode_t dir_mode; /**< Default mode when no RR (only permissions) */
    mode_t file_mode;
    /* TODO #00024 : option to convert names to lower case for iso reading */

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


    /** 
     * Enable or disable loading of the first 32768 bytes of the session and
     * submission by iso_write_opts_set_system_area(data, 0).
     */
    int load_system_area;
};

/**
 * Return information for image.
 * Both size, hasRR and hasJoliet will be filled by libisofs with suitable
 * values.
 */
struct iso_read_image_features
{
    /**
     * Will be filled with the size (in 2048 byte block) of the image, as
     * reported in the PVM.
     */
    uint32_t size;

    /** It will be set to 1 if RR extensions are present, to 0 if not. */
    unsigned int hasRR :1;

    /** It will be set to 1 if Joliet extensions are present, to 0 if not. */
    unsigned int hasJoliet :1;

    /**
     * It will be set to 1 if the image is an ISO 9660:1999, i.e. it has
     * a version 2 Enhanced Volume Descriptor.
     */
    unsigned int hasIso1999 :1;

    /** It will be set to 1 if El-Torito boot record is present, to 0 if not.*/
    unsigned int hasElTorito :1;
};

static int ifs_fs_open(IsoImageFilesystem *fs);
static int ifs_fs_close(IsoImageFilesystem *fs);
static int iso_file_source_new_ifs(IsoImageFilesystem *fs,
           IsoFileSource *parent, struct ecma119_dir_record *record,
           IsoFileSource **src, int flag);

/** unique identifier for each image */
unsigned int fs_dev_id = 0;

/**
 * Should the RR extensions be read?
 */
enum read_rr_ext {
    RR_EXT_NO = 0, /*< Do not use RR extensions */
    RR_EXT_110 = 1, /*< RR extensions conforming version 1.10 */
    RR_EXT_112 = 2 /*< RR extensions conforming version 1.12 */
};


/**
 * Private data for the image IsoFilesystem
 */
typedef struct
{
    /** DataSource from where data will be read */
    IsoDataSource *src;

    /** unique id for the each image (filesystem instance) */
    unsigned int id;

    /**
     * Counter of the times the filesystem has been openned still pending of
     * close. It is used to keep track of when we need to actually open or
     * close the IsoDataSource.
     */
    unsigned int open_count;

    uid_t uid; /**< Default uid when no RR */
    gid_t gid; /**< Default uid when no RR */
    mode_t dir_mode; /**< Default mode when no RR (only permissions) */
    mode_t file_mode;

    int msgid;

    char *input_charset; /**< Input charset for RR names */
    char *local_charset; /**< For RR names, will be set to the locale one */

    /**
     * Enable or disable methods to automatically choose an input charset.
     * This eventually overrides input_charset.
     *
     * bit0= allow to set the input character set automatically from
     *       attribute "isofs.cs" of root directory
     */
    int auto_input_charset;

    /**
     * Will be filled with the block lba of the extend for the root directory
     * of the hierarchy that will be read, either from the PVD (ISO, RR) or
     * from the SVD (Joliet)
     */
    uint32_t iso_root_block;

    /**
     * Will be filled with the block lba of the extend for the root directory,
     * as read from the PVM
     */
    uint32_t pvd_root_block;

    /**
     * Will be filled with the block lba of the extend for the root directory,
     * as read from the SVD
     */
    uint32_t svd_root_block;

    /**
     * Will be filled with the block lba of the extend for the root directory,
     * as read from the enhanced volume descriptor (ISO 9660:1999)
     */
    uint32_t evd_root_block;

    /**
     * If we need to read RR extensions. i.e., if the image contains RR
     * extensions, and the user wants to read them.
     */
    enum read_rr_ext rr;

    /**
     * Bytes skipped within the System Use field of a directory record, before
     * the beginning of the SUSP system user entries. See IEEE 1281, SUSP. 5.3.
     */
    uint8_t len_skp;

    /* Volume attributes */
    char *volset_id;
    char *volume_id; /**< Volume identifier. */
    char *publisher_id; /**< Volume publisher. */
    char *data_preparer_id; /**< Volume data preparer. */
    char *system_id; /**< Volume system identifier. */
    char *application_id; /**< Volume application id */
    char *copyright_file_id;
    char *abstract_file_id;
    char *biblio_file_id;
    char *creation_time;
    char *modification_time;
    char *expiration_time;
    char *effective_time;

    /* extension information */

    /**
     * RR version being used in image.
     * 0 no RR extension, 1 RRIP 1.10, 2 RRIP 1.12
     */
    enum read_rr_ext rr_version;

    /** If Joliet extensions are available on image */
    unsigned int joliet : 1;

    /** If ISO 9660:1999 is available on image */
    unsigned int iso1999 : 1;

    /** Whether AAIP info shall be loaded if it is present.
     *  1 = yes , 0 = no
     */
    int aaip_load;

    /** Whether the MD5 array shall be read if available.
     *  2 = yes, but do not check tags , 1 = yes , 0 = no
     */
    int md5_load;

    /** Whether AAIP is present. Version major.minor = major * 100 + minor
     *  Value -1 means that no AAIP ER was detected yet.
     */
    int aaip_version;

    /**
     * Number of blocks of the volume, as reported in the PVM.
     */
    uint32_t nblocks;

    /* el-torito information */
    unsigned int eltorito : 1; /* is el-torito available */
    int num_bootimgs;
    unsigned char platform_ids[Libisofs_max_boot_imageS];
    unsigned char id_strings[Libisofs_max_boot_imageS][28];
    unsigned char selection_crits[Libisofs_max_boot_imageS][20];
    unsigned char boot_flags[Libisofs_max_boot_imageS]; /* bit0= bootable */
    unsigned char media_types[Libisofs_max_boot_imageS];
    unsigned char partition_types[Libisofs_max_boot_imageS];
    short load_segs[Libisofs_max_boot_imageS];
    short load_sizes[Libisofs_max_boot_imageS];
    /** Block addresses of for El-Torito boot images.
        Needed to recognize them when the get read from the directory tree.
     */
    uint32_t bootblocks[Libisofs_max_boot_imageS];

    uint32_t catblock; /**< Block for El-Torito catalog */
    off_t catsize; /* Size of boot catalog in bytes */
    char *catcontent;

    /* Whether inode numbers from PX entries shall be discarded */
    unsigned int make_new_ino : 1 ;

    /* Inode number generator counter */
    ino_t inode_counter;

    /* PX inode number status
       bit0= there were nodes with PX inode numbers
       bit1= there were nodes with PX but without inode numbers
       bit2= there were nodes without PX
       bit3= there were nodes with faulty PX
     */
    int px_ino_status;

    /* Which Rock Ridge error messages already have occured
       bit0= Invalid PX entry
       bit1= Invalid TF entry
       bit2= New NM entry found without previous CONTINUE flag
       bit3= Invalid NM entry
       bit4= New SL entry found without previous CONTINUE flag
       bit5= Invalid SL entry
       bit6= Invalid SL entry, no child location
       bit7= Invalid PN entry
       bit8= Sparse files not supported
       bit9= SP entry found in a directory entry other than '.' entry of root
      bit10= ER entry found in a directory entry other than '.' entry of root
      bit11= Invalid AA entry
      bit12= Invalid AL entry
      bit13= Invalid ZF entry
      bit14= Rock Ridge PX entry is not present or invalid
      bit15= Incomplete NM
      bit16= Incomplete SL
      bit17= Charset conversion error
      bit18= Link without destination
    */
    int rr_err_reported;
    int rr_err_repeated;

} _ImageFsData;

typedef struct image_fs_data ImageFileSourceData;

/* IMPORTANT: Any change must be reflected by ifs_clone_src */
struct image_fs_data
{
    IsoImageFilesystem *fs; /**< reference to the image it belongs to */
    IsoFileSource *parent; /**< reference to the parent (NULL if root) */

    struct stat info; /**< filled struct stat */
    char *name; /**< name of this file */

    /**
     * Location of file extents.
     */
    struct iso_file_section *sections;
    int nsections;

    unsigned int opened : 2; /**< 0 not opened, 1 opened file, 2 opened dir */

#ifdef Libisofs_with_zliB
    uint8_t header_size_div4;
    uint8_t block_size_log2;
    uint32_t uncompressed_size;
#endif

    /* info for content reading */
    struct
    {
        /**
         * - For regular files, once opened it points to a temporary data
         *   buffer of 2048 bytes.
         * - For dirs, once opened it points to a IsoFileSource* array with
         *   its children
         * - For symlinks, it points to link destination
         */
        void *content;

        /**
         * - For regular files, number of bytes already read.
         */
        off_t offset;
    } data;

    /**
     * malloc() storage for the string of AAIP fields which represent
     * ACLs and XFS-style Extended Attributes. (Not to be confused with
     * ECMA-119 Extended Attributes.)
     */
    unsigned char *aa_string;

};

struct child_list
{
    IsoFileSource *file;
    struct child_list *next;
};

void child_list_free(struct child_list *list)
{
    struct child_list *temp;
    struct child_list *next = list;
    while (next != NULL) {
        temp = next->next;
        iso_file_source_unref(next->file);
        free(next);
        next = temp;
    }
}

static
char* ifs_get_path(IsoFileSource *src)
{
    ImageFileSourceData *data;
    data = src->data;

    if (data->parent == NULL) {
        return strdup("");
    } else {
        char *path, *new_path;
        int pathlen;

        if (data->name == NULL)
            return NULL;
        path = ifs_get_path(data->parent);
        if (path == NULL)
            return NULL;
        pathlen = strlen(path);
        new_path = realloc(path, pathlen + strlen(data->name) + 2);
        if (new_path == NULL) {
            free(path);
            return NULL;
        }
        path= new_path;
        path[pathlen] = '/';
        path[pathlen + 1] = '\0';
        return strcat(path, data->name);
    }
}

static
char* ifs_get_name(IsoFileSource *src)
{
    ImageFileSourceData *data;
    data = src->data;
    return data->name == NULL ? NULL : strdup(data->name);
}

static
int ifs_lstat(IsoFileSource *src, struct stat *info)
{
    ImageFileSourceData *data;

    if (src == NULL || info == NULL) {
        return ISO_NULL_POINTER;
    }

    data = src->data;
    if (data == NULL)
        return ISO_NULL_POINTER;
    *info = data->info;
    return ISO_SUCCESS;
}

static
int ifs_stat(IsoFileSource *src, struct stat *info)
{
    ImageFileSourceData *data;

    if (src == NULL || info == NULL || src->data == NULL) {
        return ISO_NULL_POINTER;
    }

    data = (ImageFileSourceData*)src->data;

    if (S_ISLNK(data->info.st_mode)) {
        /* TODO #00012 : support follow symlinks on image filesystem */
        return ISO_FILE_BAD_PATH;
    }
    *info = data->info;
    return ISO_SUCCESS;
}

static
int ifs_access(IsoFileSource *src)
{
    /* we always have access, it is controlled by DataSource */
    return ISO_SUCCESS;
}

/**
 * Read all directory records in a directory, and creates an IsoFileSource for
 * each of them, storing them in the data field of the IsoFileSource for the
 * given dir.
 */
static
int read_dir(ImageFileSourceData *data)
{
    int ret;
    uint32_t size;
    uint32_t block;
    IsoImageFilesystem *fs;
    _ImageFsData *fsdata;
    struct ecma119_dir_record *record;
    uint8_t *buffer = NULL;
    IsoFileSource *child = NULL;
    uint32_t pos = 0;
    uint32_t tlen = 0;

    if (data == NULL) {
        ret = ISO_NULL_POINTER; goto ex;
    }

    LIBISO_ALLOC_MEM(buffer, uint8_t, BLOCK_SIZE);
    fs = data->fs;
    fsdata = fs->data;

    /* a dir has always a single extent */
    block = data->sections[0].block;
    ret = fsdata->src->read_block(fsdata->src, block, buffer);
    if (ret < 0) {
        goto ex;
    }

    /* "." entry, get size of the dir and skip */
    record = (struct ecma119_dir_record *)(buffer + pos);
    size = iso_read_bb(record->length, 4, NULL);
    tlen += record->len_dr[0];
    pos += record->len_dr[0];

    /* skip ".." */
    record = (struct ecma119_dir_record *)(buffer + pos);
    tlen += record->len_dr[0];
    pos += record->len_dr[0];

    while (tlen < size) {

        record = (struct ecma119_dir_record *)(buffer + pos);
        if (pos == 2048 || record->len_dr[0] == 0) {
            /*
             * The directory entries are splitted in several blocks
             * read next block
             */
            ret = fsdata->src->read_block(fsdata->src, ++block, buffer);
            if (ret < 0) {
                goto ex;
            }
            tlen += 2048 - pos;
            pos = 0;
            continue;
        }

        /* (Vreixo:)
         * What about ignoring files with existence flag?
         * if (record->flags[0] & 0x01)
         *	continue;
         * ts B20306 : >>> One should rather record that flag and write it
         *             >>> to the new image.
         */

#ifdef Libisofs_wrongly_skip_rr_moveD
        /* ts B20306 :
           This skipping by name is wrong resp. redundant:
           If no rr reading is enabled, then it is the only access point for
           the content of relocated directories. So one should not ignore it.
           If rr reading is enabled, then the RE entry of mkisofs' RR_MOVED
           will cause it to be skipped.
	*/

        /* (Vreixo:)
         * For a extrange reason, mkisofs relocates directories under
         * a RR_MOVED dir. It seems that it is only used for that purposes,
         * and thus it should be removed from the iso tree before
         * generating a new image with libisofs, that don't uses it.
         */

        if (data->parent == NULL && record->len_fi[0] == 8
            && !strncmp((char*)record->file_id, "RR_MOVED", 8)) {

            iso_msg_debug(fsdata->msgid, "Skipping RR_MOVE entry.");

            tlen += record->len_dr[0];
            pos += record->len_dr[0];
            continue;
        }

#endif /* Libisofs_wrongly_skip_rr_moveD */

        /*
         * We pass a NULL parent instead of dir, to prevent the circular
         * reference from child to parent.
         */
        ret = iso_file_source_new_ifs(fs, NULL, record, &child, 0);
        if (ret < 0) {
            if (child) {
                /*
                 * This can only happen with multi-extent files.
                 */
                ImageFileSourceData *ifsdata = child->data;
                free(ifsdata->sections);
                free(ifsdata->name);
                free(ifsdata);
                free(child);
            }
            goto ex;
        }

        /* add to the child list */
        if (ret == 1) {
            struct child_list *node;
            node = malloc(sizeof(struct child_list));
            if (node == NULL) {
                iso_file_source_unref(child);
                {ret = ISO_OUT_OF_MEM; goto ex;}
            }
            /*
             * Note that we insert in reverse order. This leads to faster
             * addition here, but also when adding to the tree, as insertion
             * will be done, sorted, in the first position of the list.
             */
            node->next = data->data.content;
            node->file = child;
            data->data.content = node;
            child = NULL;
        }

        tlen += record->len_dr[0];
        pos += record->len_dr[0];
    }

    ret = ISO_SUCCESS;
ex:;
    LIBISO_FREE_MEM(buffer);
    return ret;
}

static
int ifs_open(IsoFileSource *src)
{
    int ret;
    ImageFileSourceData *data;

    if (src == NULL || src->data == NULL) {
        return ISO_NULL_POINTER;
    }
    data = (ImageFileSourceData*)src->data;

    if (data->opened) {
        return ISO_FILE_ALREADY_OPENED;
    }

    if (S_ISDIR(data->info.st_mode)) {
        /* ensure fs is openned */
        ret = data->fs->open(data->fs);
        if (ret < 0) {
            return ret;
        }

        /*
         * Cache all directory entries.
         * This can waste more memory, but improves as disc is read in much more
         * sequencially way, thus reducing jump between tracks on disc
         */
        ret = read_dir(data);
        data->fs->close(data->fs);

        if (ret < 0) {
            /* free probably allocated children */
            child_list_free((struct child_list*)data->data.content);
        } else {
            data->opened = 2;
        }

        return ret;
    } else if (S_ISREG(data->info.st_mode)) {
        /* ensure fs is openned */
        ret = data->fs->open(data->fs);
        if (ret < 0) {
            return ret;
        }
        data->data.content = malloc(BLOCK_SIZE);
        if (data->data.content == NULL) {
            return ISO_OUT_OF_MEM;
        }
        data->data.offset = 0;
        data->opened = 1;
    } else {
        /* symlinks and special files inside image can't be openned */
        return ISO_FILE_ERROR;
    }
    return ISO_SUCCESS;
}

static
int ifs_close(IsoFileSource *src)
{
    ImageFileSourceData *data;

    if (src == NULL || src->data == NULL) {
        return ISO_NULL_POINTER;
    }
    data = (ImageFileSourceData*)src->data;

    if (!data->opened) {
        return ISO_FILE_NOT_OPENED;
    }

    if (data->opened == 2) {
        /*
         * close a dir, free all pending pre-allocated children.
         * not that we don't need to close the filesystem, it was already
         * closed
         */
        child_list_free((struct child_list*) data->data.content);
        data->data.content = NULL;
        data->opened = 0;
    } else if (data->opened == 1) {
        /* close regular file */
        free(data->data.content);
        data->fs->close(data->fs);
        data->data.content = NULL;
        data->opened = 0;
    } else {
        /* TODO only dirs and files supported for now */
        return ISO_ERROR;
    }

    return ISO_SUCCESS;
}

/**
 * Computes the block where the given offset should start.
 */
static
uint32_t block_from_offset(int nsections, struct iso_file_section *sections,
                           off_t offset)
{
    int section = 0;
    off_t bytes = 0;

    do {
        if ( (offset - bytes) < (off_t) sections[section].size ) {
            return sections[section].block + (offset - bytes) / BLOCK_SIZE;
        } else {
            bytes += (off_t) sections[section].size;
            section++;
        }

    } while(section < nsections);
    return 0; /* should never happen */
}

/**
 * Get the size available for reading on the corresponding block
 */
static
uint32_t size_available(int nsections, struct iso_file_section *sections,
                           off_t offset)
{
    int section = 0;
    off_t bytes = 0;

    do {
        if ( (offset - bytes) < (off_t) sections[section].size ) {
            uint32_t curr_section_offset = (uint32_t)(offset - bytes);
            uint32_t curr_section_left = sections[section].size - curr_section_offset;
            uint32_t available = BLOCK_SIZE - curr_section_offset % BLOCK_SIZE;
            return MIN(curr_section_left, available);
        } else {
            bytes += (off_t) sections[section].size;
            section++;
        }

    } while(section < nsections);
    return 0; /* should never happen */
}

/**
 * Get the block offset for reading the given file offset
 */
static
uint32_t block_offset(int nsections, struct iso_file_section *sections,
                      off_t offset)
{
    int section = 0;
    off_t bytes = 0;


    do {
        if ( (offset - bytes) < (off_t) sections[section].size ) {
            return (uint32_t)(offset - bytes) % BLOCK_SIZE;
        } else {
            bytes += (off_t) sections[section].size;
            section++;
        }

    } while(section < nsections);
    return 0; /* should never happen */
}

/**
 * Attempts to read up to count bytes from the given source into
 * the buffer starting at buf.
 *
 * The file src must be open() before calling this, and close() when no
 * more needed. Not valid for dirs. On symlinks it reads the destination
 * file.
 *
 * @return
 *     number of bytes read, 0 if EOF, < 0 on error
 *      Error codes:
 *         ISO_FILE_ERROR
 *         ISO_NULL_POINTER
 *         ISO_FILE_NOT_OPENED
 *         ISO_FILE_IS_DIR
 *         ISO_OUT_OF_MEM
 *         ISO_INTERRUPTED
 */
static
int ifs_read(IsoFileSource *src, void *buf, size_t count)
{
    int ret;
    ImageFileSourceData *data;
    uint32_t read = 0;

    if (src == NULL || src->data == NULL || buf == NULL) {
        return ISO_NULL_POINTER;
    }
    if (count == 0) {
        return ISO_WRONG_ARG_VALUE;
    }
    data = (ImageFileSourceData*)src->data;

    if (!data->opened) {
        return ISO_FILE_NOT_OPENED;
    } else if (data->opened != 1) {
        return ISO_FILE_IS_DIR;
    }

    while (read < count && data->data.offset < data->info.st_size) {
        size_t bytes;
        uint8_t *orig;

        if (block_offset(data->nsections, data->sections, data->data.offset) == 0) {
            /* we need to buffer next block */
            uint32_t block;
            _ImageFsData *fsdata;

            if (data->data.offset >= data->info.st_size) {
                /* EOF */
                break;
            }
            fsdata = data->fs->data;
            block = block_from_offset(data->nsections, data->sections,
                                      data->data.offset);
            ret = fsdata->src->read_block(fsdata->src, block,
                                          data->data.content);
            if (ret < 0) {
                return ret;
            }
        }

        /* how much can I read */
        bytes = MIN(size_available(data->nsections, data->sections, data->data.offset),
                    count - read);
        if (data->data.offset + (off_t)bytes > data->info.st_size) {
             bytes = data->info.st_size - data->data.offset;
        }
        orig = data->data.content;
        orig += block_offset(data->nsections, data->sections, data->data.offset);
        memcpy((uint8_t*)buf + read, orig, bytes);
        read += bytes;
        data->data.offset += (off_t)bytes;
    }
    return read;
}

static
off_t ifs_lseek(IsoFileSource *src, off_t offset, int flag)
{
    ImageFileSourceData *data;

    if (src == NULL) {
        return (off_t)ISO_NULL_POINTER;
    }
    if (offset < (off_t)0) {
        return (off_t)ISO_WRONG_ARG_VALUE;
    }

    data = src->data;

    if (!data->opened) {
        return (off_t)ISO_FILE_NOT_OPENED;
    } else if (data->opened != 1) {
        return (off_t)ISO_FILE_IS_DIR;
    }

    switch (flag) {
    case 0: /* SEEK_SET */
        data->data.offset = offset;
        break;
    case 1: /* SEEK_CUR */
        data->data.offset += offset;
        break;
    case 2: /* SEEK_END */
        /* do this make sense? */
        data->data.offset = data->info.st_size + offset;
        break;
    default:
        return (off_t)ISO_WRONG_ARG_VALUE;
    }

    /*
     * We check for block_offset != 0 because if it is already 0, the block
     * will be read from image in the read function
     */
    if (block_offset(data->nsections, data->sections, data->data.offset) != 0) {
        /* we need to buffer the block */
        uint32_t block;
        _ImageFsData *fsdata;

        if (data->data.offset < data->info.st_size) {
            int ret;
            fsdata = data->fs->data;
            block = block_from_offset(data->nsections, data->sections,
                                      data->data.offset);
            ret = fsdata->src->read_block(fsdata->src, block,
                                          data->data.content);
            if (ret < 0) {
                return (off_t)ret;
            }
        }
    }
    return data->data.offset;
}

static
int ifs_readdir(IsoFileSource *src, IsoFileSource **child)
{
    ImageFileSourceData *data, *cdata;
    struct child_list *children;

    if (src == NULL || src->data == NULL || child == NULL) {
        return ISO_NULL_POINTER;
    }
    data = (ImageFileSourceData*)src->data;

    if (!data->opened) {
        return ISO_FILE_NOT_OPENED;
    } else if (data->opened != 2) {
        return ISO_FILE_IS_NOT_DIR;
    }

    /* return the first child and free it */
    if (data->data.content == NULL) {
        return 0; /* EOF */
    }

    children = (struct child_list*)data->data.content;
    *child = children->file;
    cdata = (ImageFileSourceData*)(*child)->data;

    /* set the ref to the parent */
    cdata->parent = src;
    iso_file_source_ref(src);

    /* free the first element of the list */
    data->data.content = children->next;
    free(children);

    return ISO_SUCCESS;
}

/**
 * Read the destination of a symlink. You don't need to open the file
 * to call this.
 *
 * @param buf
 *     allocated buffer of at least bufsiz bytes.
 *     The dest. will be copied there, and it will be NULL-terminated
 * @param bufsiz
 *     characters to be copied. Destination link will be truncated if
 *     it is larger than given size. This include the \0 character.
 * @return
 *     1 on success, < 0 on error
 *      Error codes:
 *         ISO_FILE_ERROR
 *         ISO_NULL_POINTER
 *         ISO_WRONG_ARG_VALUE -> if bufsiz <= 0
 *         ISO_FILE_IS_NOT_SYMLINK
 *         ISO_OUT_OF_MEM
 *         ISO_FILE_BAD_PATH
 *         ISO_FILE_DOESNT_EXIST
 *
 */
static
int ifs_readlink(IsoFileSource *src, char *buf, size_t bufsiz)
{
    char *dest;
    size_t len;
    int ret;
    ImageFileSourceData *data;

    if (src == NULL || buf == NULL || src->data == NULL) {
        return ISO_NULL_POINTER;
    }

    if (bufsiz <= 0) {
        return ISO_WRONG_ARG_VALUE;
    }

    data = (ImageFileSourceData*)src->data;

    if (!S_ISLNK(data->info.st_mode)) {
        return ISO_FILE_IS_NOT_SYMLINK;
    }

    dest = (char*)data->data.content;
    len = strlen(dest);

    ret = ISO_SUCCESS;
    if (len >= bufsiz) {
        ret = ISO_RR_PATH_TOO_LONG;
        len = bufsiz - 1;
    }
    strncpy(buf, dest, len);
    buf[len] = '\0';
    return ret;
}

static
IsoFilesystem* ifs_get_filesystem(IsoFileSource *src)
{
    ImageFileSourceData *data;

    if (src == NULL) {
        return NULL;
    }

    data = src->data;
    return data->fs;
}

static
void ifs_free(IsoFileSource *src)
{
    ImageFileSourceData *data;

    data = src->data;

    /* close the file if it is already openned */
    if (data->opened) {
        src->class->close(src);
    }

    /* free destination if it is a link */
    if (S_ISLNK(data->info.st_mode)) {
        free(data->data.content);
    }
    iso_filesystem_unref(data->fs);
    if (data->parent != NULL) {
        iso_file_source_unref(data->parent);
    }

    free(data->sections);
    free(data->name);
    if (data->aa_string != NULL)
        free(data->aa_string);
    free(data);
}


static
int ifs_get_aa_string(IsoFileSource *src, unsigned char **aa_string, int flag)
{
    size_t len;
    ImageFileSourceData *data;

    data = src->data;

    if ((flag & 1) || data->aa_string == NULL) {
        *aa_string = data->aa_string;
        data->aa_string = NULL;
    } else {
        len = aaip_count_bytes(data->aa_string, 0);
        *aa_string = calloc(len, 1);
        if (*aa_string == NULL)
            return ISO_OUT_OF_MEM;
        memcpy(*aa_string, data->aa_string, len);
    }
    return 1;
}

static
int ifs_clone_src(IsoFileSource *old_source,
                  IsoFileSource **new_source, int flag)
{
    IsoFileSource *src = NULL;
    ImageFileSourceData *old_data, *new_data = NULL;
    char *new_name = NULL;
    struct iso_file_section *new_sections = NULL;
    void *new_aa_string = NULL;
    int i, ret;

    if (flag)
        return ISO_STREAM_NO_CLONE; /* unknown option required */

    old_data = (ImageFileSourceData *) old_source->data;
    *new_source = NULL; 
    src = calloc(1, sizeof(IsoFileSource));
    if (src == NULL)
        goto no_mem;
    new_name = strdup(old_data->name);
    if (new_name == NULL)
        goto no_mem;
    new_data = calloc(1, sizeof(ImageFileSourceData));

    if (new_data == NULL)
        goto no_mem;
    if (old_data->nsections > 0) {
        new_sections = calloc(old_data->nsections,
                              sizeof(struct iso_file_section));
        if (new_sections == NULL)
            goto no_mem;
    }
    ret = aaip_xinfo_cloner(old_data->aa_string, &new_aa_string, 0);
    if (ret < 0)
        goto no_mem;

    new_data->fs = old_data->fs;

    new_data->parent = old_data->parent;

    memcpy(&(new_data->info), &(old_data->info), sizeof(struct stat));
    new_data->name = new_name;
    new_data->sections = new_sections;
    new_data->nsections = old_data->nsections;
    for (i = 0; i < new_data->nsections; i++) 
        memcpy(new_data->sections + i, old_data->sections + i,
               sizeof(struct iso_file_section));
    new_data->opened = old_data->opened;
#ifdef Libisofs_with_zliB
    new_data->header_size_div4 = old_data->header_size_div4;
    new_data->block_size_log2 = old_data->block_size_log2;
    new_data->uncompressed_size = old_data->uncompressed_size;
#endif
    new_data->data.content = NULL;
    new_data->aa_string = (unsigned char *) new_aa_string;
    
    src->class = old_source->class;
    src->refcount = 1;
    src->data = new_data;
    *new_source = src;
    iso_file_source_ref(new_data->parent);
    iso_filesystem_ref(new_data->fs);
    return ISO_SUCCESS;
no_mem:;
    if (src != NULL)
        free((char *) src);
    if (new_data != NULL)
        free((char *) new_data);
    if (new_name != NULL)
        free(new_name);
    if (new_sections != NULL)
        free((char *) new_sections);
    if (new_aa_string != NULL)
        aaip_xinfo_func(new_aa_string, 1);
    return ISO_OUT_OF_MEM;
}


IsoFileSourceIface ifs_class = {

    2, /* version */
    ifs_get_path,
    ifs_get_name,
    ifs_lstat,
    ifs_stat,
    ifs_access,
    ifs_open,
    ifs_close,
    ifs_read,
    ifs_readdir,
    ifs_readlink,
    ifs_get_filesystem,
    ifs_free,
    ifs_lseek,
    ifs_get_aa_string,
    ifs_clone_src

};


/* Used from libisofs/stream.c : iso_stream_get_src_zf() */
int iso_ifs_source_get_zf(IsoFileSource *src, int *header_size_div4,
                          int *block_size_log2, uint32_t *uncompressed_size,
                          int flag)
{

#ifdef Libisofs_with_zliB

    ImageFileSourceData *data;

    if (src->class != &ifs_class)
        return 0;
    data = src->data;
    *header_size_div4 = data->header_size_div4;
    *block_size_log2 = data->block_size_log2;
    *uncompressed_size = data->uncompressed_size;
    return 1;

#else

    return 0;

#endif /* ! Libisofs_with_zliB */
}     


/**
 * Read a file name from a directory record, doing the needed charset
 * conversion
 */
static
char *get_name(_ImageFsData *fsdata, const char *str, size_t len)
{
    int ret;
    char *name = NULL;
    if (strcmp(fsdata->local_charset, fsdata->input_charset)) {
        /* charset conversion needed */
        ret = strnconv(str, fsdata->input_charset, fsdata->local_charset, len,
                       &name);
        if (ret == 1) {
            return name;
        } else {
            ret = iso_msg_submit(fsdata->msgid, ISO_FILENAME_WRONG_CHARSET, ret,
                "Charset conversion error. Cannot convert from %s to %s",
                fsdata->input_charset, fsdata->local_charset);
            if (ret < 0) {
                return NULL; /* aborted */
            }
            /* fallback */
        }
    }

    /* we reach here when the charset conversion is not needed or has failed */

    name = malloc(len + 1);
    if (name == NULL) {
        return NULL;
    }
    memcpy(name, str, len);
    name[len] = '\0';
    return name;
}


static
int iso_rr_msg_submit(_ImageFsData *fsdata, int rr_err_bit,
                      int errcode, int causedby, const char *msg)
{
    int ret;

    if ((fsdata->rr_err_reported & (1 << rr_err_bit)) &&
        (fsdata->rr_err_repeated & (1 << rr_err_bit))) {
        if (iso_msg_is_abort(errcode))
            return ISO_CANCELED;
        return 0;
    }
    if (fsdata->rr_err_reported & (1 << rr_err_bit)) {
        ret = iso_msg_submit(fsdata->msgid, errcode, causedby,
                             "MORE THAN ONCE : %s", msg);
        fsdata->rr_err_repeated |= (1 << rr_err_bit);
    } else {
        ret = iso_msg_submit(fsdata->msgid, errcode, causedby, "%s", msg);
        fsdata->rr_err_reported |= (1 << rr_err_bit);
    }
    return ret;
}


/**
 *
 * @param src
 *      if not-NULL, it points to a multi-extent file returned by a previous
 *      call to this function.
 * @param flag
 *      bit0= this is the root node attribute load call
 *            (parameter parent is not reliable for this)
 * @return
 *      2 node is still incomplete (multi-extent)
 *      1 success, 0 record ignored (not an error, can be a relocated dir),
 *      < 0 error
 */
static
int iso_file_source_new_ifs(IsoImageFilesystem *fs, IsoFileSource *parent,
                            struct ecma119_dir_record *record,
                            IsoFileSource **src, int flag)
{
    int ret;
    struct stat atts;
    time_t recorded;
    _ImageFsData *fsdata;
    IsoFileSource *ifsrc = NULL;
    ImageFileSourceData *ifsdata = NULL;

    int namecont = 0; /* 1 if found a NM with CONTINUE flag */
    char *name = NULL;

    /* 1 if found a SL with CONTINUE flag,
     * 2 if found a component with continue flag */
    int linkdestcont = 0;
    char *linkdest = NULL;

    uint32_t relocated_dir = 0;

    unsigned char *aa_string = NULL;
    size_t aa_size = 0, aa_len = 0, prev_field = 0;
    int aa_done = 0;
    char *cs_value = NULL;
    size_t cs_value_length = 0;
    char *msg = NULL;
    uint8_t *buffer = NULL;

    int has_px = 0;

#ifdef Libisofs_with_zliB
    uint8_t zisofs_alg[2], zisofs_hs4 = 0, zisofs_bsl2 = 0;
    uint32_t zisofs_usize = 0;
#endif

    if (fs == NULL || fs->data == NULL || record == NULL || src == NULL) {
        ret = ISO_NULL_POINTER; goto ex;
    }

    fsdata = (_ImageFsData*)fs->data;

    memset(&atts, 0, sizeof(struct stat));
    atts.st_nlink = 1;

    /*
     * First of all, check for unsupported ECMA-119 features
     */

    /* check for unsupported interleaved mode */
    if (record->file_unit_size[0] || record->interleave_gap_size[0]) {
        iso_msg_submit(fsdata->msgid, ISO_UNSUPPORTED_ECMA119, 0,
              "Unsupported image. This image has at least one file recorded "
              "in interleaved mode. We do not support this mode, as we think "
              "it is not used. If you are reading this, then we are wrong :) "
              "Please contact libisofs developers, so we can fix this.");
        {ret = ISO_UNSUPPORTED_ECMA119; goto ex;}
    }

    /*
     * Check for extended attributes, that are not supported. Note that even
     * if we don't support them, it is easy to ignore them.
     */
    if (record->len_xa[0]) {
        iso_msg_submit(fsdata->msgid, ISO_UNSUPPORTED_ECMA119, 0,
              "Unsupported image. This image has at least one file with "
              "ECMA-119 Extended Attributes, that are not supported");
        {ret = ISO_UNSUPPORTED_ECMA119; goto ex;}
    }

    /* TODO #00013 : check for unsupported flags when reading a dir record */

    /*
     * If src is not-NULL, it refers to more extents of this file. We ensure
     * name matches, otherwise it means we are dealing with wrong image
     */
    if (*src != NULL) {
        ImageFileSourceData* data = (*src)->data;
        char* new_name = get_name(fsdata, (char*)record->file_id, record->len_fi[0]);
        if (new_name == NULL) {
            iso_msg_submit(fsdata->msgid, ISO_WRONG_ECMA119, 0,
                          "Cannot retrieve file name");
            {ret = ISO_WRONG_ECMA119; goto ex;}
        }
        if (strcmp(new_name, data->name)) {
            iso_msg_submit(fsdata->msgid, ISO_WRONG_ECMA119, 0,
                          "Multi-extent file lacks last entry.");
            free(new_name);
            {ret = ISO_WRONG_ECMA119; goto ex;}
        }
        free(new_name);
    }

    /* check for multi-extent */
    if (record->flags[0] & 0x80) {
        iso_msg_debug(fsdata->msgid, "Found multi-extent file");

        /*
         * Directory entries can only have one section (ECMA-119, 6.8.1)
         */
        if (record->flags[0] & 0x02) {
            iso_msg_submit(fsdata->msgid, ISO_WRONG_ECMA119, 0,
                          "Directories with more than one section are not allowed.");
            {ret = ISO_WRONG_ECMA119; goto ex;}
        }

        if (*src == NULL) {
            ifsdata = calloc(1, sizeof(ImageFileSourceData));
            if (ifsdata == NULL) {
                ret = ISO_OUT_OF_MEM;
                goto ifs_cleanup;
            }
            ifsrc = calloc(1, sizeof(IsoFileSource));
            if (ifsrc == NULL) {
                ret = ISO_OUT_OF_MEM;
                goto ifs_cleanup;
            }
            ifsrc->data = ifsdata;
            ifsdata->name = get_name(fsdata, (char*)record->file_id, record->len_fi[0]);
            if (ifsdata->name == NULL) {
                iso_msg_submit(fsdata->msgid, ISO_WRONG_ECMA119, 0,
                              "Cannot retrieve file name");
                ret = ISO_WRONG_ECMA119;
                goto ifs_cleanup;
            }

            *src = ifsrc;
        } else {
            ifsdata = (*src)->data;
        }

        /* store current extent */
        ifsdata->sections = realloc(ifsdata->sections,
                    (1 + ifsdata->nsections) * sizeof(struct iso_file_section));
        if (ifsdata->sections == NULL) {
            free(ifsdata->name);
            ret = ISO_OUT_OF_MEM;
            goto ifs_cleanup;
        }
        ifsdata->sections[ifsdata->nsections].block = iso_read_bb(record->block, 4, NULL);
        ifsdata->sections[ifsdata->nsections].size = iso_read_bb(record->length, 4, NULL);

        ifsdata->info.st_size += (off_t) ifsdata->sections[ifsdata->nsections].size;
        ifsdata->nsections++;
        {ret = 2; goto ex;}
    }

    /*
     * The idea is to read all the RR entries (if we want to do that and RR
     * extensions exist on image), storing the info we want from that.
     * Then, we need some sanity checks.
     * Finally, we select what kind of node it is, and set values properly.
     */

    if (fsdata->rr) {
        struct susp_sys_user_entry *sue;
        SuspIterator *iter;


        iter = susp_iter_new(fsdata->src, record, fsdata->len_skp,
                             fsdata->msgid);
        if (iter == NULL) {
            {ret = ISO_OUT_OF_MEM; goto ex;}
        }

        while ((ret = susp_iter_next(iter, &sue)) > 0) {

            /* ignore entries from different version */
            if (sue->version[0] != 1)
                continue;

            if (SUSP_SIG(sue, 'P', 'X')) {
                has_px = 1;
                ret = read_rr_PX(sue, &atts);
                if (ret < 0) {
                    /* notify and continue */
                    ret = iso_rr_msg_submit(fsdata, 0, ISO_WRONG_RR_WARN, ret,
                                            "Invalid PX entry");
                    fsdata->px_ino_status |= 8;
                } if (ret == 2) {
                    if (fsdata->inode_counter < atts.st_ino) 
                        fsdata->inode_counter = atts.st_ino;
                    fsdata->px_ino_status |= 1;

                } else {
                    fsdata->px_ino_status |= 2;
                }

            } else if (SUSP_SIG(sue, 'T', 'F')) {
                ret = read_rr_TF(sue, &atts);
                if (ret < 0) {
                    /* notify and continue */
                    ret = iso_rr_msg_submit(fsdata, 1, ISO_WRONG_RR_WARN, ret,
                                            "Invalid TF entry");
                }
            } else if (SUSP_SIG(sue, 'N', 'M')) {
                if (name != NULL && namecont == 0) {
                    /* ups, RR standard violation */
                    ret = iso_rr_msg_submit(fsdata, 2, ISO_WRONG_RR_WARN, 0,
                                 "New NM entry found without previous"
                                 "CONTINUE flag. Ignored");
                    continue;
                }
                ret = read_rr_NM(sue, &name, &namecont);
                if (ret < 0) {
                    /* notify and continue */
                    ret = iso_rr_msg_submit(fsdata, 3, ISO_WRONG_RR_WARN, ret,
                                            "Invalid NM entry");
                }

#ifdef Libisofs_syslinux_tesT

if (name != NULL && !namecont) {
    struct device syslinux_dev;
    struct iso_sb_info syslinux_sbi;
    struct fs_info syslinux_fsi;
    char *syslinux_name = NULL;
    int syslinux_name_len;

    syslinux_dev.src = fsdata->src;
    memset(&(syslinux_sbi.root), 0, 256);
    syslinux_sbi.do_rr = 1;
    syslinux_sbi.susp_skip = 0;
    syslinux_fsi.fs_dev = &syslinux_dev;
    syslinux_fsi.fs_info = &syslinux_sbi;
    ret = susp_rr_get_nm(&syslinux_fsi, (char *) record,
                         &syslinux_name, &syslinux_name_len);
    if (ret == 1) {
        if (name == NULL || syslinux_name == NULL)
          fprintf(stderr, "################ Hoppla. NULL\n");
        else if(strcmp(syslinux_name, name) != 0)
          fprintf(stderr,
                  "################ libisofs '%s' != '%s' susp_rr_get_nm()\n",
                  name, syslinux_name);
    } else if (ret == 0) {
        fprintf(stderr,
                "################ '%s' not found by susp_rr_get_nm()\n", name);
    } else {
        fprintf(stderr, "################ 'susp_rr_get_nm() returned error\n");
    }
    if (syslinux_name != NULL)
        free(syslinux_name);

}

#endif /* Libisofs_syslinux_tesT */


            } else if (SUSP_SIG(sue, 'S', 'L')) {
                if (linkdest != NULL && linkdestcont == 0) {
                    /* ups, RR standard violation */
                    ret = iso_rr_msg_submit(fsdata, 4, ISO_WRONG_RR_WARN, 0,
                                 "New SL entry found without previous"
                                 "CONTINUE flag. Ignored");
                    continue;
                }
                ret = read_rr_SL(sue, &linkdest, &linkdestcont);
                if (ret < 0) {
                    /* notify and continue */
                    ret = iso_rr_msg_submit(fsdata, 5, ISO_WRONG_RR_WARN, ret,
                                            "Invalid SL entry");
                }
            } else if (SUSP_SIG(sue, 'R', 'E')) {
                /*
                 * this directory entry refers to a relocated directory.
                 * We simply ignore it, as it will be correctly handled
                 * when found the CL
                 */
                susp_iter_free(iter);
                free(name);
                {ret = 0; goto ex;} /* it's not an error */
            } else if (SUSP_SIG(sue, 'C', 'L')) {
                /*
                 * This entry is a placeholder for a relocated dir.
                 * We need to ignore other entries, with the exception of NM.
                 * Then we create a directory node that represents the
                 * relocated dir, and iterate over its children.
                 */
                relocated_dir = iso_read_bb(sue->data.CL.child_loc, 4, NULL);
                if (relocated_dir == 0) {
                    ret = iso_rr_msg_submit(fsdata, 6, ISO_WRONG_RR, 0,
                                  "Invalid SL entry, no child location");
                    break;
                }
            } else if (SUSP_SIG(sue, 'P', 'N')) {
                ret = read_rr_PN(sue, &atts);
                if (ret < 0) {
                    /* notify and continue */
                    ret = iso_rr_msg_submit(fsdata, 7, ISO_WRONG_RR_WARN, ret,
                                            "Invalid PN entry");
                }
            } else if (SUSP_SIG(sue, 'S', 'F')) {
                ret = iso_rr_msg_submit(fsdata, 8, ISO_UNSUPPORTED_RR, 0,
                                        "Sparse files not supported.");
                break;
            } else if (SUSP_SIG(sue, 'R', 'R')) {
                /* This was an optional flag byte in RRIP 1.09 which told the
                   reader what other RRIP fields to expect.
                   mkisofs emits it. We don't.
                */
                continue;
            } else if (SUSP_SIG(sue, 'S', 'P')) {
                /*
                 * Ignore this, to prevent the hint message, if we are dealing
                 * with root node (SP is only valid in "." of root node)
                 */
                if (!(flag & 1)) {
                    /* notify and continue */
                    ret = iso_rr_msg_submit(fsdata, 9, ISO_WRONG_RR, 0,
                                  "SP entry found in a directory entry other "
                                  "than '.' entry of root node");
                }
                continue;
            } else if (SUSP_SIG(sue, 'E', 'R')) {
                /*
                 * Ignore this, to prevent the hint message, if we are dealing
                 * with root node (ER is only valid in "." of root node)
                 */
                if (!(flag & 1)) {
                    /* notify and continue */
                    ret = iso_rr_msg_submit(fsdata, 10, ISO_WRONG_RR, 0,
                                  "ER entry found in a directory entry other "
                                  "than '.' entry of root node");
                }
                continue;

            /* Need to read AA resp. AL in any case so it is available for
               S_IRWXG mapping in case that fsdata->aaip_load != 1
             */
            } else if (SUSP_SIG(sue, 'A', 'A')) {

                ret = read_aaip_AA(sue, &aa_string, &aa_size, &aa_len,
                                   &prev_field, &aa_done, 0);
                if (ret < 0) {
                    /* notify and continue */
                    ret = iso_rr_msg_submit(fsdata, 11, ISO_WRONG_RR_WARN, ret,
                                            "Invalid AA entry");
                    continue;
                }

            } else if (SUSP_SIG(sue, 'A', 'L')) {

                ret = read_aaip_AL(sue, &aa_string, &aa_size, &aa_len,
                                   &prev_field, &aa_done, 0);
                if (ret < 0) {
                    /* notify and continue */
                    ret = iso_rr_msg_submit(fsdata, 12, ISO_WRONG_RR_WARN, ret,
                                            "Invalid AL entry");
                    continue;
                }

#ifdef Libisofs_with_zliB

            } else if (SUSP_SIG(sue, 'Z', 'F')) {

                ret = read_zisofs_ZF(sue, zisofs_alg, &zisofs_hs4,
                                     &zisofs_bsl2, &zisofs_usize, 0);
                if (ret < 0 || zisofs_alg[0] != 'p' || zisofs_alg[1] != 'z') {
                    /* notify and continue */
                    ret = iso_rr_msg_submit(fsdata, 13, ISO_WRONG_RR_WARN, ret,
                                 "Invalid ZF entry");
                    zisofs_hs4 = 0;
                    continue;
                }

#endif /* Libisofs_with_zliB */

/* This message is inflationary */
/*
            } else {
                ret = iso_msg_submit(fsdata->msgid, ISO_SUSP_UNHANDLED, 0,
                    "Unhandled SUSP entry %c%c.", sue->sig[0], sue->sig[1]);
*/

            }
        }

        susp_iter_free(iter);

        /* check for RR problems */

        if (ret < 0) {
            /* error was already submitted above */
            iso_msg_debug(fsdata->msgid, "Error parsing RR entries");
        } else if (!relocated_dir && atts.st_mode == (mode_t) 0 ) {
            ret = iso_rr_msg_submit(fsdata, 14, ISO_WRONG_RR, 0, "Mandatory "
                                 "Rock Ridge PX entry is not present or it "
                                 "contains invalid values.");
        } else {
            /* ensure both name and link dest are finished */
            if (namecont != 0) {
                ret = iso_rr_msg_submit(fsdata, 15, ISO_WRONG_RR, 0,
                        "Incomplete Rock Ridge name, last NM entry continues");
            }
            if (linkdestcont != 0) {
                ret = iso_rr_msg_submit(fsdata, 16, ISO_WRONG_RR, 0,
                    "Incomplete link destination, last SL entry continues");
            }
        }

        if (ret < 0) {
            free(name);
            goto ex;
        }

        if ((flag & 1)  && aa_string != NULL) {
            ret = iso_aa_lookup_attr(aa_string, "isofs.cs",
                                     &cs_value_length, &cs_value, 0);
            if (ret == 1) {
                LIBISO_FREE_MEM(msg);
                LIBISO_ALLOC_MEM(msg, char, 160);
                if (fsdata->auto_input_charset & 1) {
                    if (fsdata->input_charset != NULL)
                        free(fsdata->input_charset);
                    fsdata->input_charset = cs_value;
                    sprintf(msg,
                         "Learned from ISO image: input character set '%.80s'",
                         cs_value);
                } else {
                    sprintf(msg,
                           "Character set name recorded in ISO image: '%.80s'",
                           cs_value);
                    free(cs_value);
                }
                iso_msgs_submit(0, msg, 0, "NOTE", 0);
                cs_value = NULL;
            }
        }

        /* convert name to needed charset */
        if (strcmp(fsdata->input_charset, fsdata->local_charset) && name) {
            /* we need to convert name charset */
            char *newname = NULL;
            ret = strconv(name, fsdata->input_charset, fsdata->local_charset,
                          &newname);
            if (ret < 0) {
                /* its just a hint message */
                LIBISO_FREE_MEM(msg);
                LIBISO_ALLOC_MEM(msg, char, 160);
                sprintf(msg,
                "Charset conversion error. Cannot convert from %.40s to %.40s",
                                 fsdata->input_charset, fsdata->local_charset);
                ret = iso_rr_msg_submit(fsdata, 17, ISO_FILENAME_WRONG_CHARSET,
                                        ret, msg);
                free(newname);
                if (ret < 0) {
                    free(name);
                    goto ex;
                }
            } else {
                free(name);
                name = newname;
            }
        }

        /* convert link destination to needed charset */
        if (strcmp(fsdata->input_charset, fsdata->local_charset) && linkdest) {
            /* we need to convert name charset */
            char *newlinkdest = NULL;
            ret = strconv(linkdest, fsdata->input_charset,
                          fsdata->local_charset, &newlinkdest);
            if (ret < 0) {
                LIBISO_FREE_MEM(msg);
                LIBISO_ALLOC_MEM(msg, char, 160);
                sprintf(msg,
                "Charset conversion error. Cannot convert from %.40s to %.40s",
                                 fsdata->input_charset, fsdata->local_charset);
                ret = iso_rr_msg_submit(fsdata, 17, ISO_FILENAME_WRONG_CHARSET,
                                     ret, msg);
                free(newlinkdest);
                if (ret < 0) {
                    free(name);
                    goto ex;
                }
            } else {
                free(linkdest);
                linkdest = newlinkdest;
            }
        }

    } else {
        /* RR extensions are not read / used */
        atts.st_gid = fsdata->gid;
        atts.st_uid = fsdata->uid;
        if (record->flags[0] & 0x02) {
            atts.st_mode = S_IFDIR | fsdata->dir_mode;
        } else {
            atts.st_mode = S_IFREG | fsdata->file_mode;
        }
    }

    if (!has_px) {
        fsdata->px_ino_status |= 4;
    }

    /*
     * if we haven't RR extensions, or no NM entry is present,
     * we use the name in directory record
     */
    if (!name) {
        size_t len;

        if (record->len_fi[0] == 1 && record->file_id[0] == 0) {
            /* "." entry, we can call this for root node, so... */
            if (!(atts.st_mode & S_IFDIR)) {
                ret = iso_msg_submit(fsdata->msgid, ISO_WRONG_ECMA119, 0,
                              "Wrong ISO file name. \".\" not dir");
                goto ex;
            }
        } else {

            name = get_name(fsdata, (char*)record->file_id, record->len_fi[0]);
            if (name == NULL) {
                ret = iso_msg_submit(fsdata->msgid, ISO_WRONG_ECMA119, 0,
                              "Cannot retrieve file name");
                goto ex;
            }

            /* remove trailing version number */
            len = strlen(name);
            if (len > 2 && name[len-2] == ';' && name[len-1] == '1') {
                if (len > 3 && name[len-3] == '.') {
                    /*
                     * the "." is mandatory, so in most cases is included only
                     * for standard compliance
                     */
                    name[len-3] = '\0';
                } else {
                    name[len-2] = '\0';
                }
            }
        }
    }

    if (relocated_dir) {

        /*
         * We are dealing with a placeholder for a relocated dir.
         * Thus, we need to read attributes for this directory from the "."
         * entry of the relocated dir.
         */

        LIBISO_ALLOC_MEM(buffer, uint8_t, BLOCK_SIZE);
        ret = fsdata->src->read_block(fsdata->src, relocated_dir, buffer);
        if (ret < 0) {
            goto ex;
        }

        ret = iso_file_source_new_ifs(fs, parent, (struct ecma119_dir_record*)
                                      buffer, src, 0);
        if (ret <= 0) {
            goto ex;
        }

        /* but the real name is the name of the placeholder */
        ifsdata = (ImageFileSourceData*) (*src)->data;
        ifsdata->name = name;

        {ret = ISO_SUCCESS; goto ex;}
    }

    /* Production of missing inode numbers is delayed until the image is
       complete. Then all nodes which shall get a new inode number will
       be served.
    */

    /*
     * if we haven't RR extensions, or a needed TF time stamp is not present,
     * we use plain iso recording time
     */
    recorded = iso_datetime_read_7(record->recording_time);
    if (atts.st_atime == (time_t) 0) {
        atts.st_atime = recorded;
    }
    if (atts.st_ctime == (time_t) 0) {
        atts.st_ctime = recorded;
    }
    if (atts.st_mtime == (time_t) 0) {
        atts.st_mtime = recorded;
    }

    /* the size is read from iso directory record */
    atts.st_size = iso_read_bb(record->length, 4, NULL);

    /* Fill last entries */
    atts.st_dev = fsdata->id;
    atts.st_blksize = BLOCK_SIZE;
    atts.st_blocks = DIV_UP(atts.st_size, BLOCK_SIZE);

    /* TODO #00014 : more sanity checks to ensure dir record info is valid */
    if (S_ISLNK(atts.st_mode) && (linkdest == NULL)) {
        ret = iso_rr_msg_submit(fsdata, 18, ISO_WRONG_RR, 0,
                                "Link without destination.");
        free(name);
        goto ex;
    }

    /* ok, we can now create the file source */
    if (*src == NULL) {
        ifsdata = calloc(1, sizeof(ImageFileSourceData));
        if (ifsdata == NULL) {
            ret = ISO_OUT_OF_MEM;
            goto ifs_cleanup;
        }
        ifsrc = calloc(1, sizeof(IsoFileSource));
        if (ifsrc == NULL) {
            ret = ISO_OUT_OF_MEM;
            goto ifs_cleanup;
        }
    } else {
        ifsdata = (*src)->data;
        ifsrc = (*src);
        free(ifsdata->name); /* we will assign a new one */
        ifsdata->name = NULL;
        atts.st_size += (off_t)ifsdata->info.st_size;
        if (ifsdata->aa_string != NULL)
            free(ifsdata->aa_string);
        ifsdata->aa_string = NULL;
    }

    /* fill data */
    ifsdata->fs = fs;
    iso_filesystem_ref(fs);
    if (parent != NULL) {
        ifsdata->parent = parent;
        iso_file_source_ref(parent);
    }
    ifsdata->info = atts;
    ifsdata->name = name;
    ifsdata->aa_string = aa_string;

#ifdef Libisofs_with_zliB
    if (zisofs_hs4 > 0) {
        ifsdata->header_size_div4 = zisofs_hs4;
        ifsdata->block_size_log2 = zisofs_bsl2;
        ifsdata->uncompressed_size = zisofs_usize;
    } else {
        ifsdata->header_size_div4 = 0;
    }
#endif

    /* save extents */
    ifsdata->sections = realloc(ifsdata->sections,
                (1 + ifsdata->nsections) * sizeof(struct iso_file_section));
    if (ifsdata->sections == NULL) {
        free(ifsdata->name);
        ret = ISO_OUT_OF_MEM;
        goto ifs_cleanup;
    }
    ifsdata->sections[ifsdata->nsections].block = iso_read_bb(record->block, 4, NULL);
    ifsdata->sections[ifsdata->nsections].size = iso_read_bb(record->length, 4, NULL);
    ifsdata->nsections++;

    if (S_ISLNK(atts.st_mode)) {
        ifsdata->data.content = linkdest;
    }

    ifsrc->class = &ifs_class;
    ifsrc->data = ifsdata;
    ifsrc->refcount = 1;

    *src = ifsrc;
    {ret = ISO_SUCCESS; goto ex;}

ifs_cleanup: ;
    free(name);
    free(linkdest);
    free(ifsdata);
    free(ifsrc);

ex:;
    LIBISO_FREE_MEM(msg);
    LIBISO_FREE_MEM(buffer);
    return ret;
}

static
int ifs_get_root(IsoFilesystem *fs, IsoFileSource **root)
{
    int ret;
    _ImageFsData *data;
    uint8_t *buffer = NULL;

    if (fs == NULL || fs->data == NULL || root == NULL) {
        ret = ISO_NULL_POINTER; goto ex;
    }

    LIBISO_ALLOC_MEM(buffer, uint8_t, BLOCK_SIZE);
    data = (_ImageFsData*)fs->data;

    /* open the filesystem */
    ret = ifs_fs_open((IsoImageFilesystem*)fs);
    if (ret < 0) {
        goto ex;
    }

    /* read extend for root record */
    ret = data->src->read_block(data->src, data->iso_root_block, buffer);
    if (ret < 0) {
        ifs_fs_close((IsoImageFilesystem*)fs);
        goto ex;
    }

    /* get root attributes from "." entry */
    *root = NULL;
    ret = iso_file_source_new_ifs((IsoImageFilesystem*)fs, NULL,
                                 (struct ecma119_dir_record*) buffer, root, 1);

    ifs_fs_close((IsoImageFilesystem*)fs);
ex:;
    LIBISO_FREE_MEM(buffer);
    return ret;
}

/**
 * Find a file inside a node.
 *
 * @param file
 *     it is not modified if requested file is not found
 * @return
 *     1 success, 0 not found, < 0 error
 */
static
int ifs_get_file(IsoFileSource *dir, const char *name, IsoFileSource **file)
{
    int ret;
    IsoFileSource *src;

    ret = iso_file_source_open(dir);
    if (ret < 0) {
        return ret;
    }
    while ((ret = iso_file_source_readdir(dir, &src)) == 1) {
        char *fname = iso_file_source_get_name(src);
        if (!strcmp(name, fname)) {
            free(fname);
            *file = src;
            ret = ISO_SUCCESS;
            break;
        }
        free(fname);
        iso_file_source_unref(src);
    }
    iso_file_source_close(dir);
    return ret;
}

static
int ifs_get_by_path(IsoFilesystem *fs, const char *path, IsoFileSource **file)
{
    int ret;
    IsoFileSource *src;
    char *ptr, *brk_info, *component;

    if (fs == NULL || fs->data == NULL || path == NULL || file == NULL) {
        return ISO_NULL_POINTER;
    }

    if (path[0] != '/') {
        /* only absolute paths supported */
        return ISO_FILE_BAD_PATH;
    }

    /* open the filesystem */
    ret = ifs_fs_open((IsoImageFilesystem*)fs);
    if (ret < 0) {
        return ret;
    }

    ret = ifs_get_root(fs, &src);
    if (ret < 0) {
        return ret;
    }
    if (!strcmp(path, "/")) {
        /* we are looking for root */
        *file = src;
        ret = ISO_SUCCESS;
        goto get_path_exit;
    }

    ptr = strdup(path);
    if (ptr == NULL) {
        iso_file_source_unref(src);
        ret = ISO_OUT_OF_MEM;
        goto get_path_exit;
    }

    component = strtok_r(ptr, "/", &brk_info);
    while (component) {
        IsoFileSource *child = NULL;

        ImageFileSourceData *fdata;
        fdata = src->data;
        if (!S_ISDIR(fdata->info.st_mode)) {
            ret = ISO_FILE_BAD_PATH;
            break;
        }

        ret = ifs_get_file(src, component, &child);
        iso_file_source_unref(src);
        if (ret <= 0) {
            break;
        }

        src = child;
        component = strtok_r(NULL, "/", &brk_info);
    }

    free(ptr);
    if (ret < 0) {
        iso_file_source_unref(src);
    } else if (ret == 0) {
        ret = ISO_FILE_DOESNT_EXIST;
    } else {
        *file = src;
    }

    get_path_exit:;
    ifs_fs_close((IsoImageFilesystem*)fs);
    return ret;
}

unsigned int ifs_get_id(IsoFilesystem *fs)
{
    return ISO_IMAGE_FS_ID;
}

static
int ifs_fs_open(IsoImageFilesystem *fs)
{
    _ImageFsData *data;

    if (fs == NULL || fs->data == NULL) {
        return ISO_NULL_POINTER;
    }

    data = (_ImageFsData*)fs->data;

    if (data->open_count == 0) {
        /* we need to actually open the data source */
        int res = data->src->open(data->src);
        if (res < 0) {
            return res;
        }
    }
    ++data->open_count;
    return ISO_SUCCESS;
}

static
int ifs_fs_close(IsoImageFilesystem *fs)
{
    _ImageFsData *data;

    if (fs == NULL || fs->data == NULL) {
        return ISO_NULL_POINTER;
    }

    data = (_ImageFsData*)fs->data;

    if (--data->open_count == 0) {
        /* we need to actually close the data source */
        return data->src->close(data->src);
    }
    return ISO_SUCCESS;
}

static
void ifs_fs_free(IsoFilesystem *fs)
{
    _ImageFsData *data;

    data = (_ImageFsData*) fs->data;

    /* close data source if already openned */
    if (data->open_count > 0) {
        data->src->close(data->src);
    }

    /* free our ref to datasource */
    iso_data_source_unref(data->src);

    /* free volume atts */
    free(data->volset_id);
    free(data->volume_id);
    free(data->publisher_id);
    free(data->data_preparer_id);
    free(data->system_id);
    free(data->application_id);
    free(data->copyright_file_id);
    free(data->abstract_file_id);
    free(data->biblio_file_id);
    free(data->creation_time);
    free(data->modification_time);
    free(data->expiration_time);
    free(data->effective_time);
    free(data->input_charset);
    free(data->local_charset);

    if(data->catcontent != NULL)
        free(data->catcontent);

    free(data);
}

/**
 * Read the SUSP system user entries of the "." entry of the root directory,
 * indentifying when Rock Ridge extensions are being used.
 *
 * @return
 *      1 success, 0 ignored, < 0 error
 */
static
int read_root_susp_entries(_ImageFsData *data, uint32_t block)
{
    int ret;
    unsigned char *buffer = NULL;
    struct ecma119_dir_record *record;
    struct susp_sys_user_entry *sue;
    SuspIterator *iter;

    LIBISO_ALLOC_MEM(buffer, unsigned char, 2048);
    ret = data->src->read_block(data->src, block, buffer);
    if (ret < 0) {
        goto ex;
    }

    /* record will be the "." directory entry for the root record */
    record = (struct ecma119_dir_record *)buffer;

#ifdef Libisofs_syslinux_tesT

{
    struct device syslinux_dev;
    struct iso_sb_info syslinux_sbi;
    struct fs_info syslinux_fsi;

    syslinux_dev.src = data->src;
    memcpy(&(syslinux_sbi.root), (char *) record, 256);
    syslinux_sbi.do_rr = 1;
    syslinux_sbi.susp_skip = 0;
    syslinux_fsi.fs_dev = &syslinux_dev;
    syslinux_fsi.fs_info = &syslinux_sbi;
    
    ret = susp_rr_check_signatures(&syslinux_fsi, 1);
    fprintf(stderr, "--------- susp_rr_check_signatures == %d , syslinux_sbi.do_rr == %d\n", ret, syslinux_sbi.do_rr);
}
    
#endif /* Libisofs_syslinux_tesT */
    

    /*
     * TODO #00015 : take care of CD-ROM XA discs when reading SP entry
     * SUSP specification claims that for CD-ROM XA the SP entry
     * is not at position BP 1, but at BP 15. Is that used?
     * In that case, we need to set info->len_skp to 15!!
     */

    iter = susp_iter_new(data->src, record, data->len_skp, data->msgid);
    if (iter == NULL) {
        ret = ISO_OUT_OF_MEM; goto ex;
    }

    /* first entry must be an SP system use entry */
    ret = susp_iter_next(iter, &sue);
    if (ret < 0) {
        /* error */
        susp_iter_free(iter);
        goto ex;
    } else if (ret == 0 || !SUSP_SIG(sue, 'S', 'P') ) {
        iso_msg_debug(data->msgid, "SUSP/RR is not being used.");
        susp_iter_free(iter);
        {ret = ISO_SUCCESS; goto ex;}
    }

    /* it is a SP system use entry */
    if (sue->version[0] != 1 || sue->data.SP.be[0] != 0xBE
        || sue->data.SP.ef[0] != 0xEF) {

        susp_iter_free(iter);
        ret = iso_msg_submit(data->msgid, ISO_UNSUPPORTED_SUSP, 0,
                              "SUSP SP system use entry seems to be wrong. "
                              "Ignoring Rock Ridge Extensions.");
        goto ex;
    }

    iso_msg_debug(data->msgid, "SUSP/RR is being used.");

    /*
     * The LEN_SKP field, defined in IEEE 1281, SUSP. 5.3, specifies the
     * number of bytes to be skipped within each System Use field.
     * I think this will be always 0, but given that support this standard
     * feature is easy...
     */
    data->len_skp = sue->data.SP.len_skp[0];

    /*
     * Ok, now search for ER entry.
     * Just notice that the attributes for root dir are read elsewhere.
     *
     * TODO #00016 : handle non RR ER entries
     *
     * if several ER are present, we need to identify the position of
     * what refers to RR, and then look for corresponding ES entry in
     * each directory record. I have not implemented this (it's not used,
     * no?), but if we finally need it, it can be easily implemented in
     * the iterator, transparently for the rest of the code.
     */
    while ((ret = susp_iter_next(iter, &sue)) > 0) {

        /* ignore entries from different version */
        if (sue->version[0] != 1)
            continue;

        if (SUSP_SIG(sue, 'E', 'R')) {
            /*
             * it seems that Rock Ridge can be identified with any
             * of the following
             */
            if ( sue->data.ER.len_id[0] == 10 &&
                 !strncmp((char*)sue->data.ER.ext_id, "RRIP_1991A", 10) ) {

                iso_msg_debug(data->msgid,
                              "Suitable Rock Ridge ER found. Version 1.10.");
                data->rr_version = RR_EXT_110;

            } else if ( (sue->data.ER.len_id[0] == 10 &&
                    !strncmp((char*)sue->data.ER.ext_id, "IEEE_P1282", 10))
                 || (sue->data.ER.len_id[0] == 9 &&
                    !strncmp((char*)sue->data.ER.ext_id, "IEEE_1282", 9)) ) {

                iso_msg_debug(data->msgid,
                              "Suitable Rock Ridge ER found. Version 1.12.");
                data->rr_version = RR_EXT_112;

            } else if (sue->data.ER.len_id[0] == 9 &&
                  (strncmp((char*)sue->data.ER.ext_id, "AAIP_0002", 9) == 0 ||
                   strncmp((char*)sue->data.ER.ext_id, "AAIP_0100", 9) == 0 ||
                   strncmp((char*)sue->data.ER.ext_id, "AAIP_0200", 9) == 0)) {

                /* Tolerate AAIP ER even if not supported */
                iso_msg_debug(data->msgid, "Suitable AAIP ER found.");

                if (strncmp((char*)sue->data.ER.ext_id, "AAIP_0200", 9) == 0)
                    data->aaip_version = 200;
                else if (((char*)sue->data.ER.ext_id)[6] == '1')
                    data->aaip_version = 100;
                else
                    data->aaip_version = 2;
                if (!data->aaip_load)
                    iso_msg_submit(data->msgid, ISO_AAIP_IGNORED, 0,
           "Identifier for extension AAIP found, but loading is not enabled.");

            } else {
                ret = iso_msg_submit(data->msgid, ISO_SUSP_MULTIPLE_ER, 0,
                    "Unknown Extension Signature found in ER.\n"
                    "It will be ignored, but can cause problems in "
                    "image reading. Please notify us about this.");
                if (ret < 0) {
                    break;
                }
            }
        }
    }

    susp_iter_free(iter);

    if (ret < 0) {
        goto ex;
    }

    ret = ISO_SUCCESS;
ex:
    LIBISO_FREE_MEM(buffer);
    return ret;
}

static
int read_pvd_block(IsoDataSource *src, uint32_t block, uint8_t *buffer,
                   uint32_t *image_size)
{
    int ret;
    struct ecma119_pri_vol_desc *pvm;

    ret = src->read_block(src, block, buffer);
    if (ret < 0)
        return ret;
    pvm = (struct ecma119_pri_vol_desc *)buffer;

    /* sanity checks */
    if (pvm->vol_desc_type[0] != 1 || pvm->vol_desc_version[0] != 1
            || strncmp((char*)pvm->std_identifier, "CD001", 5)
            || pvm->file_structure_version[0] != 1) {

        return ISO_WRONG_PVD;
    }
    if (image_size != NULL)
        *image_size = iso_read_bb(pvm->vol_space_size, 4, NULL);
    return ISO_SUCCESS;
}

static
int read_pvm(_ImageFsData *data, uint32_t block)
{
    int ret;
    struct ecma119_pri_vol_desc *pvm;
    struct ecma119_dir_record *rootdr;
    uint8_t *buffer = NULL;

    LIBISO_ALLOC_MEM(buffer, uint8_t, BLOCK_SIZE);
    ret = read_pvd_block(data->src, block, buffer, NULL);
    if (ret < 0)
        goto ex;
    /* ok, it is a valid PVD */
    pvm = (struct ecma119_pri_vol_desc *)buffer;

    /* fill volume attributes  */
    /* TODO take care of input charset */
    data->volset_id = iso_util_strcopy_untail((char*)pvm->vol_set_id, 128);
    data->volume_id = iso_util_strcopy_untail((char*)pvm->volume_id, 32);
    data->publisher_id =
               iso_util_strcopy_untail((char*)pvm->publisher_id, 128);
    data->data_preparer_id =
               iso_util_strcopy_untail((char*)pvm->data_prep_id, 128);
    data->system_id = iso_util_strcopy_untail((char*)pvm->system_id, 32);
    data->application_id =
               iso_util_strcopy_untail((char*)pvm->application_id, 128);
    data->copyright_file_id =
               iso_util_strcopy_untail((char*) pvm->copyright_file_id, 37);
    data->abstract_file_id =
               iso_util_strcopy_untail((char*) pvm->abstract_file_id, 37);
    data->biblio_file_id =
               iso_util_strcopy_untail((char*) pvm->bibliographic_file_id, 37);
    if (data->copyright_file_id[0] == '_' && data->copyright_file_id[1] == 0 &&
        data->abstract_file_id[0] == '_' && data->abstract_file_id[1] == 0 &&
        data->biblio_file_id[0] == '_' && data->biblio_file_id[1] == 0) {
        /* This is bug output from libisofs <= 0.6.23 . The texts mean file
           names and should have been empty to indicate that there are no such
           files. It is obvious that not all three roles can be fulfilled by
           one file "_" so that one cannot spoil anything by assuming them
           empty now.
        */
        data->copyright_file_id[0] = 0;
        data->abstract_file_id[0] = 0;
        data->biblio_file_id[0] = 0;
    }
    data->creation_time =
            iso_util_strcopy_untail((char*) pvm->vol_creation_time, 17);
    data->modification_time =
            iso_util_strcopy_untail((char*) pvm->vol_modification_time, 17);
    data->expiration_time =
            iso_util_strcopy_untail((char*) pvm->vol_expiration_time, 17);
    data->effective_time =
            iso_util_strcopy_untail((char*) pvm->vol_effective_time, 17);

    data->nblocks = iso_read_bb(pvm->vol_space_size, 4, NULL);

    rootdr = (struct ecma119_dir_record*) pvm->root_dir_record;
    data->pvd_root_block = iso_read_bb(rootdr->block, 4, NULL);

    /*
     * TODO #00017 : take advantage of other atts of PVD
     * PVD has other things that could be interesting, but that don't have a
     * member in IsoImage, such as creation date. In a multisession disc, we
     * could keep the creation date and update the modification date, for
     * example.
     */

    ret = ISO_SUCCESS;
ex:;
    LIBISO_FREE_MEM(buffer);
    return ret;
}

/**
 * @return
 *      1 success, 0 ignored, < 0 error
 */
static
int read_el_torito_boot_catalog(_ImageFsData *data, uint32_t block)
{
    int ret, i, rx, last_done, idx, bufsize;
    struct el_torito_validation_entry *ve;
    struct el_torito_section_header *sh;
    struct el_torito_section_entry *entry; /* also usable as default_entry */
    unsigned char *buffer = NULL, *rpt;

    LIBISO_ALLOC_MEM(buffer, unsigned char, BLOCK_SIZE);
    data->num_bootimgs = 0;
    data->catsize = 0;
    ret = data->src->read_block(data->src, block, buffer);
    if (ret < 0) {
        goto ex;
    }

    ve = (struct el_torito_validation_entry*)buffer;

    /* check if it is a valid catalog (TODO: check also the checksum)*/
    if ( (ve->header_id[0] != 1) || (ve->key_byte1[0] != 0x55)
         || (ve->key_byte2[0] != 0xAA) ) {
        iso_msg_submit(data->msgid, ISO_WRONG_EL_TORITO, 0,
                      "Wrong or damaged El-Torito Catalog. El-Torito info "
                      "will be ignored.");
        {ret = ISO_WRONG_EL_TORITO; goto ex;}
    }

    /* check for a valid platform */
    if (ve->platform_id[0] != 0 && ve->platform_id[0] != 0xef) {
        iso_msg_submit(data->msgid, ISO_UNSUPPORTED_EL_TORITO, 0,
                     "Unsupported El-Torito platform. Only 80x86 and EFI are "
                     "supported. El-Torito info will be ignored.");
        {ret = ISO_UNSUPPORTED_EL_TORITO; goto ex;}
    }

    /* ok, once we are here we assume it is a valid catalog */

    /* parse the default entry */
    entry = (struct el_torito_section_entry *)(buffer + 32);

    data->eltorito = 1;
    /* The Default Entry is declared mandatory */
    data->catsize = 64;
    data->num_bootimgs = 1;
    data->platform_ids[0] = ve->platform_id[0];
    memcpy(data->id_strings[0], ve->id_string, 24);
    memset(data->id_strings[0] + 24, 0, 4);
    data->boot_flags[0] = entry->boot_indicator[0] ? 1 : 0;
    data->media_types[0] = entry->boot_media_type[0];
    data->partition_types[0] = entry->system_type[0];
    data->load_segs[0] = iso_read_lsb(entry->load_seg, 2);
    data->load_sizes[0] = iso_read_lsb(entry->sec_count, 2);
    data->bootblocks[0] = iso_read_lsb(entry->block, 4);
    /* The Default Entry has no selection criterion */
    memset(data->selection_crits[0], 0, 20);

    /* Read eventual more entries from the boot catalog */
    last_done = 0;
    for (rx = 64; (buffer[rx] & 0xfe) == 0x90 && !last_done; rx += 32) {
        last_done = buffer[rx] & 1;
        /* Read Section Header */

        /* >>> ts B10703 : load a new buffer if needed */;

        sh = (struct el_torito_section_header *) (buffer + rx);
        data->catsize += 32;
        for (i = 0; i < sh->num_entries[0]; i++) {
            rx += 32;
            data->catsize += 32;

            /* >>> ts B10703 : load a new buffer if needed */;

            if (data->num_bootimgs >= Libisofs_max_boot_imageS) {

                /* >>> ts B10703 : need to continue rather than abort */;

                ret = iso_msg_submit(data->msgid, ISO_EL_TORITO_WARN, 0,
                                "Too many boot images found. List truncated.");
                goto after_bootblocks;
            }
            /* Read bootblock from section entry */
            entry = (struct el_torito_section_entry *)(buffer + rx);
            idx = data->num_bootimgs;
            data->platform_ids[idx] = sh->platform_id[0];
            memcpy(data->id_strings[idx], sh->id_string, 28);
            data->boot_flags[idx] = entry->boot_indicator[0] ? 1 : 0;
            data->media_types[idx] = entry->boot_media_type[0];
            data->partition_types[idx] = entry->system_type[0];
            data->load_segs[idx] = iso_read_lsb(entry->load_seg, 2);
            data->load_sizes[idx] = iso_read_lsb(entry->sec_count, 2);
            data->bootblocks[idx] = iso_read_lsb(entry->block, 4);
            data->selection_crits[idx][0] = entry->selec_criteria[0];
            memcpy(data->selection_crits[idx] + 1, entry->vendor_sc, 19);
            data->num_bootimgs++;
        }
    }
after_bootblocks:;
    if(data->catsize > 0) {
      if(data->catcontent != NULL)
          free(data->catcontent);
      if(data->catsize > 10 * BLOCK_SIZE)
          data->catsize = 10 * BLOCK_SIZE;
      bufsize = data->catsize;
      if (bufsize % BLOCK_SIZE)
          bufsize += BLOCK_SIZE - (bufsize % BLOCK_SIZE);
      data->catcontent = calloc(bufsize , 1);
      if(data->catcontent == NULL) {
         data->catsize = 0;
         ret = ISO_OUT_OF_MEM;
         goto ex; 
      }
      for(rx = 0; rx < bufsize; rx += BLOCK_SIZE) {
        rpt = (unsigned char *) (data->catcontent + rx);
        ret = data->src->read_block(data->src, block + rx / BLOCK_SIZE, rpt);
        if (ret < 0)
           goto ex;
      }
    }
    ret = ISO_SUCCESS;
ex:;
    LIBISO_FREE_MEM(buffer);
    return ret;
}


/*
 @return 1= ok, checked, go on with loading
         2= no checksum tags found, go on with loading
        <0= libisofs error
            especially ISO_SB_TREE_CORRUPTED
*/
static
int iso_src_check_sb_tree(IsoDataSource *src, uint32_t start_lba, int flag)
{
    int tag_type, ret;
    char *block = NULL, md5[16];
    int desired = (1 << 2);
    void *ctx = NULL;
    uint32_t next_tag = 0, i;

    LIBISO_ALLOC_MEM(block, char, 2048);    
    ret = iso_md5_start(&ctx);
    if (ret < 0)
        goto ex;
    if (start_lba == 0)
         desired |= (1 << 4);
    for (i = 0; i < 32; i++) {
        ret = src->read_block(src, start_lba + i, (uint8_t *) block);
        if (ret < 0)
            goto ex;
        ret = 0;
        if (i >= 16)
            ret = iso_util_eval_md5_tag(block, desired, start_lba + i,
                                      ctx, start_lba, &tag_type, &next_tag, 0);
        iso_md5_compute(ctx, block, 2048);
        if (ret == (int) ISO_MD5_TAG_COPIED) {/* growing without emulated TOC */
            ret = 2;
            goto ex;
        }
        if (ret == (int) ISO_MD5_AREA_CORRUPTED ||
            ret == (int) ISO_MD5_TAG_MISMATCH)
            ret = ISO_SB_TREE_CORRUPTED;
        if (ret < 0)
            goto ex;
        if (ret == 1)
    break;
    }
    if (i >= 32) {
        ret = 2;
        goto ex;
    }
    if (tag_type == 4) {
        /* Relocated Superblock: restart checking at real session start */
        if (next_tag < 32) {
            /* Non plausible session_start address */
            ret = ISO_SB_TREE_CORRUPTED;
            iso_msg_submit(-1, ret, 0, NULL);
            goto ex;
        }
        /* Check real session */
        ret = iso_src_check_sb_tree(src, next_tag, 0);
        goto ex;
    }

    /* Go on with tree */
    for (i++; start_lba + i <= next_tag; i++) {
        ret = src->read_block(src, start_lba + i, (uint8_t *) block);
        if (ret < 0)
            goto ex;
        if (start_lba + i < next_tag)
            iso_md5_compute(ctx, block, 2048);
    }
    ret = iso_util_eval_md5_tag(block, (1 << 3), start_lba + i - 1,
                                ctx, start_lba, &tag_type, &next_tag, 0);
    if (ret == (int) ISO_MD5_AREA_CORRUPTED ||
        ret == (int) ISO_MD5_TAG_MISMATCH)
        ret = ISO_SB_TREE_CORRUPTED;
    if (ret < 0)
        goto ex;

    ret = 1;
ex:
    if (ctx != NULL)
        iso_md5_end(&ctx, md5);
    LIBISO_FREE_MEM(block);
    return ret;
}


int iso_image_filesystem_new(IsoDataSource *src, struct iso_read_opts *opts,
                             int msgid, IsoImageFilesystem **fs)
{
    int ret, i;
    uint32_t block;
    IsoImageFilesystem *ifs;
    _ImageFsData *data;
    uint8_t *buffer = NULL;

    if (src == NULL || opts == NULL || fs == NULL) {
        ret = ISO_NULL_POINTER; goto ex;
    }

    LIBISO_ALLOC_MEM(buffer, uint8_t, BLOCK_SIZE);
    data = calloc(1, sizeof(_ImageFsData));
    if (data == NULL) {
        ret = ISO_OUT_OF_MEM; goto ex;
    }

    ifs = calloc(1, sizeof(IsoImageFilesystem));
    if (ifs == NULL) {
        free(data);
        {ret = ISO_OUT_OF_MEM; goto ex;}
    }

    /* get our ref to IsoDataSource */
    data->src = src;
    iso_data_source_ref(src);
    data->open_count = 0;

    data->catcontent = NULL;

    /* get an id for the filesystem */
    data->id = ++fs_dev_id;

    /* fill data from opts */
    data->gid = opts->gid;
    data->uid = opts->uid;
    data->file_mode = opts->file_mode & ~S_IFMT;
    data->dir_mode = opts->dir_mode & ~S_IFMT;
    data->msgid = msgid;
    data->aaip_load = !opts->noaaip;
    if (opts->nomd5 == 0)
        data->md5_load = 1;
    else if (opts->nomd5 == 2)
        data->md5_load = 2;
    else
        data->md5_load = 0;
    data->aaip_version = -1;
    data->make_new_ino = opts->make_new_ino;
    data->num_bootimgs = 0;
    for (i = 0; i < Libisofs_max_boot_imageS; i++)
        data->bootblocks[i] = 0;
    data->inode_counter = 0;
    data->px_ino_status = 0;
    data->rr_err_reported = 0;
    data->rr_err_repeated = 0;


    data->local_charset = strdup(iso_get_local_charset(0));
    if (data->local_charset == NULL) {
        ret = ISO_OUT_OF_MEM;
        goto fs_cleanup;
    }

    strncpy(ifs->type, "iso ", 4);
    ifs->data = data;
    ifs->refcount = 1;
    ifs->version = 0;
    ifs->get_root = ifs_get_root;
    ifs->get_by_path = ifs_get_by_path;
    ifs->get_id = ifs_get_id;
    ifs->open = ifs_fs_open;
    ifs->close = ifs_fs_close;
    ifs->free = ifs_fs_free;

    /* read Volume Descriptors and ensure it is a valid image */
    if (data->md5_load == 1) {
        /* From opts->block on : check for superblock and tree tags */;
        ret = iso_src_check_sb_tree(src, opts->block, 0);
        if (ret < 0) {
            iso_msgs_submit(0,
                "Image loading aborted due to MD5 mismatch of image tree data",
                            0, "FAILURE", 0);
            iso_msgs_submit(0,
                     "You may override this refusal by disabling MD5 checking",
                            0, "HINT", 0);
            goto fs_cleanup;
        }
    }

    /* 1. first, open the filesystem */
    ifs_fs_open(ifs);

    /* 2. read primary volume description */
    ret = read_pvm(data, opts->block + 16);
    if (ret < 0) {
        goto fs_cleanup;
    }

    /* 3. read next volume descriptors */
    block = opts->block + 17;
    do {
        ret = src->read_block(src, block, buffer);
        if (ret < 0) {
            /* cleanup and exit */
            goto fs_cleanup;
        }
        switch (buffer[0]) {
        case 0:
            /* boot record */
            {
                struct ecma119_boot_rec_vol_desc *vol;
                vol = (struct ecma119_boot_rec_vol_desc*)buffer;

                /* some sanity checks */
                if (strncmp((char*)vol->std_identifier, "CD001", 5)
                    || vol->vol_desc_version[0] != 1
                    || strncmp((char*)vol->boot_sys_id,
                               "EL TORITO SPECIFICATION", 23)) {
                    iso_msg_submit(data->msgid,
                          ISO_UNSUPPORTED_EL_TORITO, 0,
                          "Unsupported Boot Vol. Desc. Only El-Torito "
                          "Specification, Version 1.0 Volume "
                          "Descriptors are supported. Ignoring boot info");
                } else {
                    data->catblock = iso_read_lsb(vol->boot_catalog, 4);
                    ret = read_el_torito_boot_catalog(data, data->catblock);
                    if (ret < 0 && ret != (int) ISO_UNSUPPORTED_EL_TORITO &&
                        ret != (int) ISO_WRONG_EL_TORITO) {
                        goto fs_cleanup;
                    }
                }
            }
            break;
        case 2:
            /* suplementary volume descritor */
            {
                struct ecma119_sup_vol_desc *sup;
                struct ecma119_dir_record *root;

                sup = (struct ecma119_sup_vol_desc*)buffer;
                if (sup->esc_sequences[0] == 0x25 &&
                    sup->esc_sequences[1] == 0x2F &&
                    (sup->esc_sequences[2] == 0x40 ||
                     sup->esc_sequences[2] == 0x43 ||
                     sup->esc_sequences[2] == 0x45) ) {

                    /* it's a Joliet Sup. Vol. Desc. */
                    iso_msg_debug(data->msgid, "Found Joliet extensions");
                    data->joliet = 1;
                    root = (struct ecma119_dir_record*)sup->root_dir_record;
                    data->svd_root_block = iso_read_bb(root->block, 4, NULL);
                    /* TODO #00019 : set IsoImage attribs from Joliet SVD? */
                    /* TODO #00020 : handle RR info in Joliet tree */
                } else if (sup->vol_desc_version[0] == 2) {
                    /*
                     * It is an Enhanced Volume Descriptor, image is an
                     * ISO 9660:1999
                     */
                    iso_msg_debug(data->msgid, "Found ISO 9660:1999");
                    data->iso1999 = 1;
                    root = (struct ecma119_dir_record*)sup->root_dir_record;
                    data->evd_root_block = iso_read_bb(root->block, 4, NULL);
                    /* TODO #00021 : handle RR info in ISO 9660:1999 tree */
                } else {
                    ret = iso_msg_submit(data->msgid, ISO_UNSUPPORTED_VD, 0,
                        "Unsupported Sup. Vol. Desc found.");
                    if (ret < 0) {
                        goto fs_cleanup;
                    }
                }
            }
            break;
        case 255:
            /*
             * volume set terminator
             * ignore, as it's checked in loop end condition
             */
            break;
        default:
            iso_msg_submit(data->msgid, ISO_UNSUPPORTED_VD, 0,
                           "Ignoring Volume descriptor %x.", buffer[0]);
            break;
        }
        block++;
    } while (buffer[0] != 255);

    /* 4. check if RR extensions are being used */
    ret = read_root_susp_entries(data, data->pvd_root_block);
    if (ret < 0) {
        goto fs_cleanup;
    }

    /* user doesn't want to read RR extensions */
    if (opts->norock) {
        data->rr = RR_EXT_NO;
    } else {
        data->rr = data->rr_version;
    }

    /* select what tree to read */
    if (data->rr) {
        /* RR extensions are available */
        if (!opts->nojoliet && opts->preferjoliet && data->joliet) {
            /* if user prefers joliet, that is used */
            iso_msg_debug(data->msgid, "Reading Joliet extensions.");
            data->input_charset = strdup("UCS-2BE");
            data->rr = RR_EXT_NO;
            data->iso_root_block = data->svd_root_block;
        } else {
            /* RR will be used */
            iso_msg_debug(data->msgid, "Reading Rock Ridge extensions.");
            data->iso_root_block = data->pvd_root_block;
        }
    } else {
        /* RR extensions are not available */
        if (!opts->nojoliet && data->joliet) {
            /* joliet will be used */
            iso_msg_debug(data->msgid, "Reading Joliet extensions.");
            data->input_charset = strdup("UCS-2BE");
            data->iso_root_block = data->svd_root_block;
        } else if (!opts->noiso1999 && data->iso1999) {
            /* we will read ISO 9660:1999 */
            iso_msg_debug(data->msgid, "Reading ISO-9660:1999 tree.");
            data->iso_root_block = data->evd_root_block;
        } else {
            /* default to plain iso */
            iso_msg_debug(data->msgid, "Reading plain ISO-9660 tree.");
            data->iso_root_block = data->pvd_root_block;
            data->input_charset = strdup("ASCII");
        }
    }

    if (data->input_charset == NULL) {
        if (opts->input_charset != NULL) {
            data->input_charset = strdup(opts->input_charset);
        } else {
            data->input_charset = strdup(data->local_charset);
        }
    }
    if (data->input_charset == NULL) {
        ret = ISO_OUT_OF_MEM;
        goto fs_cleanup;
    }
    data->auto_input_charset = opts->auto_input_charset;

    /* and finally return. Note that we keep the DataSource opened */

    *fs = ifs;
    {ret = ISO_SUCCESS; goto ex;}

fs_cleanup: ;
    ifs_fs_free(ifs);
    free(ifs);

ex:;
    LIBISO_FREE_MEM(buffer);
    return ret;
}


/* Take over aa_string from file source to node or discard it after making
   the necessary change in node->mode group permissions.
   node->mode must already be set.
*/
static
int src_aa_to_node(IsoFileSource *src, IsoNode *node, int flag)
{
    int ret;
    unsigned char *aa_string;
    ImageFileSourceData *data;
    _ImageFsData *fsdata;
    char *a_text = NULL, *d_text = NULL;

    data = (ImageFileSourceData*)src->data;
    fsdata = data->fs->data;

    /* Obtain ownership of eventual AAIP string */
    ret = iso_file_source_get_aa_string(src, &aa_string, 1);
    if (ret != 1 || aa_string == NULL)
        return 1;
    if (fsdata->aaip_load == 1) {
        /* Attach aa_string to node */
        ret = iso_node_add_xinfo(node, aaip_xinfo_func, aa_string);
        if (ret < 0)
            return ret;
    } else {
        /* Look for ACL and perform S_IRWXG mapping */
        iso_aa_get_acl_text(aa_string, node->mode, &a_text, &d_text, 16);
        if (a_text != NULL)
            aaip_cleanout_st_mode(a_text, &(node->mode), 4 | 16);
        /* Dispose ACL a_text and d_text */
        iso_aa_get_acl_text(aa_string, node->mode, &a_text, &d_text, 1 << 15);
        /* Dispose aa_string */
        aaip_xinfo_func(aa_string, 1);
    }
    return 1;
}


static
int image_builder_create_node(IsoNodeBuilder *builder, IsoImage *image,
                              IsoFileSource *src, IsoNode **node)
{
    int ret, idx, to_copy;
    struct stat info;
    IsoNode *new = NULL;
    IsoBoot *bootcat;
    char *name = NULL;
    char *dest = NULL;
    ImageFileSourceData *data;
    _ImageFsData *fsdata;

#ifdef Libisofs_with_zliB
    /* Intimate friendship with this function in filters/zisofs.c */
    int ziso_add_osiz_filter(IsoFile *file, uint8_t header_size_div4,
                             uint8_t block_size_log2,
                             uint32_t uncompressed_size, int flag);
#endif /* Libisofs_with_zliB */


    if (builder == NULL || src == NULL || node == NULL || src->data == NULL) {
        ret = ISO_NULL_POINTER; goto ex;
    }

    data = (ImageFileSourceData*)src->data;
    fsdata = data->fs->data;

    name = iso_file_source_get_name(src);

    /* get info about source */
    ret = iso_file_source_lstat(src, &info);
    if (ret < 0) {
        goto ex;
    }

    switch (info.st_mode & S_IFMT) {
    case S_IFREG:
        {
            /* source is a regular file */

            /* El-Torito images have only one section */
            if (fsdata->eltorito && data->sections[0].block == fsdata->catblock) {

                if (image->bootcat->node != NULL) {
                    ret = iso_msg_submit(image->id, ISO_EL_TORITO_WARN, 0,
                                 "More than one catalog node has been found. "
                                 "We can continue, but that could lead to "
                                 "problems");
                    if (ret < 0)
                        goto ex;
                    iso_node_unref((IsoNode*)image->bootcat->node);
                }

                /* we create a placeholder for the catalog instead of
                 * a regular file */
                new = calloc(1, sizeof(IsoBoot));
                if (new == NULL) {
                    ret = ISO_OUT_OF_MEM; goto ex;
                }
                bootcat = (IsoBoot *) new;
                bootcat->lba = data->sections[0].block;
                bootcat->size = info.st_size;
                if (bootcat->size > 10 * BLOCK_SIZE)
                    bootcat->size = 10 * BLOCK_SIZE;
                bootcat->content = NULL;
                if (bootcat->size > 0) {
                    bootcat->content = calloc(1, bootcat->size);
                    if (bootcat->content == NULL) {
                        ret = ISO_OUT_OF_MEM; goto ex;
                    }
                    to_copy = bootcat->size;
                    if (bootcat->size > fsdata->catsize)
                        to_copy = fsdata->catsize;
                    memcpy(bootcat->content, fsdata->catcontent, to_copy);
                }

                /* and set the image node */
                image->bootcat->node = bootcat;
                new->type = LIBISO_BOOT;
                new->refcount = 1;
            } else {
                IsoStream *stream;
                IsoFile *file;

                ret = iso_file_source_stream_new(src, &stream);
                if (ret < 0)
                    goto ex;

                /* take a ref to the src, as stream has taken our ref */
                iso_file_source_ref(src);

                file = calloc(1, sizeof(IsoFile));
                if (file == NULL) {
                    iso_stream_unref(stream);
                    {ret = ISO_OUT_OF_MEM; goto ex;}
                }

                /* mark file as from old session */
                file->from_old_session = 1;

                /*
                 * and we set the sort weight based on the block on image, to
                 * improve performance on image modifying.
                 */
                file->sort_weight = INT_MAX - data->sections[0].block;

                file->stream = stream;
                file->node.type = LIBISO_FILE;

#ifdef Libisofs_with_zliB

                if (data->header_size_div4 > 0) {
                    ret = ziso_add_osiz_filter(file, data->header_size_div4,
                                               data->block_size_log2,
                                               data->uncompressed_size, 0);
                    if (ret < 0) {
                        iso_stream_unref(stream);
                        goto ex;
                    }
                }

#endif /* Libisofs_with_zliB */

                new = (IsoNode*) file;
                new->refcount = 0;

                for (idx = 0; idx < fsdata->num_bootimgs; idx++)
                    if (fsdata->eltorito && data->sections[0].block ==
                        fsdata->bootblocks[idx])
                break;
                if (idx < fsdata->num_bootimgs) {
                    /* it is boot image node */
                    if (image->bootcat->bootimages[idx]->image != NULL) {
                        /* idx is already occupied, try to find unoccupied one
                           which has the same block address.
                        */
                        for (; idx < fsdata->num_bootimgs; idx++)
                            if (fsdata->eltorito && data->sections[0].block ==
                                fsdata->bootblocks[idx] &&
                                image->bootcat->bootimages[idx]->image == NULL)
                        break;
                    }
                    if (idx >= fsdata->num_bootimgs) {
                        ret = iso_msg_submit(image->id, ISO_EL_TORITO_WARN, 0,
             "More than one ISO node has been found for the same boot image.");
                        if (ret < 0) {
                            iso_stream_unref(stream);
                            goto ex;
                        }
                    } else {
                        /* and set the image node */
                        image->bootcat->bootimages[idx]->image = file;
                        new->refcount++;
                    }
                }
            }
        }
        break;
    case S_IFDIR:
        {
            /* source is a directory */
            new = calloc(1, sizeof(IsoDir));
            if (new == NULL) {
                {ret = ISO_OUT_OF_MEM; goto ex;}
            }
            new->type = LIBISO_DIR;
            new->refcount = 0;
        }
        break;
    case S_IFLNK:
        {
            /* source is a symbolic link */
            IsoSymlink *link;

            LIBISO_ALLOC_MEM(dest, char, LIBISOFS_NODE_PATH_MAX);

            ret = iso_file_source_readlink(src, dest, LIBISOFS_NODE_PATH_MAX);
            if (ret < 0) {
                goto ex;
            }
            link = calloc(1, sizeof(IsoSymlink));
            if (link == NULL) {
                {ret = ISO_OUT_OF_MEM; goto ex;}
            }
            link->dest = strdup(dest);
            link->node.type = LIBISO_SYMLINK;
            link->fs_id = ISO_IMAGE_FS_ID;
            link->st_dev = info.st_dev;
            link->st_ino = info.st_ino;
            new = (IsoNode*) link;
            new->refcount = 0;
        }
        break;
    case S_IFSOCK:
    case S_IFBLK:
    case S_IFCHR:
    case S_IFIFO:
        {
            /* source is an special file */
            IsoSpecial *special;
            special = calloc(1, sizeof(IsoSpecial));
            if (special == NULL) {
                ret = ISO_OUT_OF_MEM; goto ex;
            }
            special->dev = info.st_rdev;
            special->node.type = LIBISO_SPECIAL;
            special->fs_id = ISO_IMAGE_FS_ID;
            special->st_dev = info.st_dev;
            special->st_ino = info.st_ino;
            new = (IsoNode*) special;
            new->refcount = 0;
        }
        break;
    default:
        ret = ISO_BAD_ISO_FILETYPE; goto ex;
    }
    /* fill fields */
    new->refcount++;
    new->name = name; name = NULL;
    new->mode = info.st_mode;
    new->uid = info.st_uid;
    new->gid = info.st_gid;
    new->atime = info.st_atime;
    new->mtime = info.st_mtime;
    new->ctime = info.st_ctime;

    new->hidden = 0;

    new->parent = NULL;
    new->next = NULL;

    ret = src_aa_to_node(src, new, 0);
    if (ret < 0) {
        goto ex;
    }

    /* Attach ino as xinfo if valid and no IsoStream is involved */
    if (info.st_ino != 0 && (info.st_mode & S_IFMT) != S_IFREG &&
        !fsdata->make_new_ino) {
        ret = iso_node_set_ino(new, info.st_ino, 0);
        if (ret < 0)
            goto ex;
    }

    *node = new; new = NULL;
    {ret = ISO_SUCCESS; goto ex;}

ex:;
    if (name != NULL)
        free(name);
    if (new != NULL)
        iso_node_unref(new);
    LIBISO_FREE_MEM(dest);
    return ret;
}

/**
 * Create a new builder, that is exactly a copy of an old builder, but where
 * create_node() function has been replaced by image_builder_create_node.
 */
static
int iso_image_builder_new(IsoNodeBuilder *old, IsoNodeBuilder **builder)
{
    IsoNodeBuilder *b;

    if (builder == NULL) {
        return ISO_NULL_POINTER;
    }

    b = malloc(sizeof(IsoNodeBuilder));
    if (b == NULL) {
        return ISO_OUT_OF_MEM;
    }

    b->refcount = 1;
    b->create_file_data = old->create_file_data;
    b->create_node_data = old->create_node_data;
    b->create_file = old->create_file;
    b->create_node = image_builder_create_node;
    b->free = old->free;

    *builder = b;
    return ISO_SUCCESS;
}

/**
 * Create a file source to access the El-Torito boot image, when it is not
 * accessible from the ISO filesystem.
 */
static
int create_boot_img_filesrc(IsoImageFilesystem *fs, IsoImage *image, int idx,
                            IsoFileSource **src)
{
    int ret;
    struct stat atts;
    _ImageFsData *fsdata;
    IsoFileSource *ifsrc = NULL;
    ImageFileSourceData *ifsdata = NULL;

    if (fs == NULL || fs->data == NULL || src == NULL) {
        return ISO_NULL_POINTER;
    }

    fsdata = (_ImageFsData*)fs->data;

    memset(&atts, 0, sizeof(struct stat));
    atts.st_mode = S_IFREG;
    atts.st_ino = img_give_ino_number(image, 0);
    atts.st_nlink = 1;

    /*
     * this is the greater problem. We don't know the size. For now, we
     * just use a single block of data. In a future, maybe we could figure out
     * a better idea. Another alternative is to use several blocks, that way
     * is less probable that we throw out valid data.
     */
    atts.st_size = (off_t)BLOCK_SIZE;

    /* Fill last entries */
    atts.st_dev = fsdata->id;
    atts.st_blksize = BLOCK_SIZE;
    atts.st_blocks = DIV_UP(atts.st_size, BLOCK_SIZE);

    /* ok, we can now create the file source */
    ifsdata = calloc(1, sizeof(ImageFileSourceData));
    if (ifsdata == NULL) {
        ret = ISO_OUT_OF_MEM;
        goto boot_fs_cleanup;
    }
    ifsrc = calloc(1, sizeof(IsoFileSource));
    if (ifsrc == NULL) {
        ret = ISO_OUT_OF_MEM;
        goto boot_fs_cleanup;
    }

    ifsdata->sections = malloc(sizeof(struct iso_file_section));
    if (ifsdata->sections == NULL) {
        ret = ISO_OUT_OF_MEM;
        goto boot_fs_cleanup;
    }

    /* fill data */
    ifsdata->fs = fs;
    iso_filesystem_ref(fs);
    ifsdata->parent = NULL;
    ifsdata->info = atts;
    ifsdata->name = NULL;
    ifsdata->sections[0].block = fsdata->bootblocks[idx];
    ifsdata->sections[0].size = BLOCK_SIZE;
    ifsdata->nsections = 1;

    ifsrc->class = &ifs_class;
    ifsrc->data = ifsdata;
    ifsrc->refcount = 1;

    *src = ifsrc;
    return ISO_SUCCESS;

boot_fs_cleanup: ;
    free(ifsdata);
    free(ifsrc);
    return ret;
}

/** ??? >>> ts B00428 : should the max size become public ? */
#define Libisofs_boot_image_max_sizE (4096*1024)

/** Guess which of the loaded boot images contain boot information tables.
    Set boot->seems_boot_info_table accordingly.
*/
static
int iso_image_eval_boot_info_table(IsoImage *image, struct iso_read_opts *opts,
                         IsoDataSource *src, uint32_t iso_image_size, int flag)
{
    int i, j, ret, section_count, todo, chunk;
    uint32_t img_lba, img_size, boot_pvd_found, image_pvd, alleged_size;
    struct iso_file_section *sections = NULL;
    struct el_torito_boot_image *boot;
    uint8_t *boot_image_buf = NULL, boot_info_found[16], *buf = NULL;
    IsoStream *stream = NULL;
    IsoFile *boot_file;
    uint64_t blk;

    if (image->bootcat == NULL)
        {ret = ISO_SUCCESS; goto ex;}
    LIBISO_ALLOC_MEM(buf, uint8_t, BLOCK_SIZE);
    for (i = 0; i < image->bootcat->num_bootimages; i++) {
        boot = image->bootcat->bootimages[i];
        boot_file = boot->image;
        boot->seems_boot_info_table = 0;
        boot->seems_grub2_boot_info = 0;
        img_size = iso_file_get_size(boot_file);
        if (img_size > Libisofs_boot_image_max_sizE || img_size < 64)
    continue;
        img_lba = 0;
        sections = NULL;
        ret = iso_file_get_old_image_sections(boot_file,
                                              &section_count, &sections, 0);
        if (ret == 1 && section_count > 0)
            img_lba = sections[0].block;
        if (sections != NULL) {
            free(sections);
            sections = NULL;
        }
        if(img_lba == 0)
    continue;

        boot_image_buf = calloc(1, img_size);
        if (boot_image_buf == NULL) {
            ret = ISO_OUT_OF_MEM;
            goto ex;
        }
        stream = iso_file_get_stream(boot_file);
        ret = iso_stream_open(stream);
        if (ret < 0) {
            stream = NULL;
            goto ex;
        }
        for (todo = img_size; todo > 0; ) {
          if (todo > BLOCK_SIZE)
              chunk = BLOCK_SIZE;
          else
              chunk = todo;
          ret = iso_stream_read(stream, boot_image_buf + (img_size - todo),
                                chunk);
          if (ret != chunk) {
            ret = (ret < 0) ? ret : (int) ISO_FILE_READ_ERROR;
            goto ex;
          }
          todo -= chunk;
        }
        iso_stream_close(stream);
        stream = NULL;
        
        memcpy(boot_info_found, boot_image_buf + 8, 16);
        boot_pvd_found = iso_read_lsb(boot_info_found, 4);
        image_pvd = (uint32_t) (opts->block + 16);

        /* Accomodate to eventually relocated superblock */
        if (image_pvd != boot_pvd_found &&
            image_pvd == 16 && boot_pvd_found < iso_image_size) {
            /* Check whether there is a PVD at boot_pvd_found
               and whether it bears the same image size 
             */
            ret = read_pvd_block(src, boot_pvd_found, buf, &alleged_size);
            if (ret == 1 &&
                alleged_size + boot_pvd_found == iso_image_size + image_pvd)
              image_pvd = boot_pvd_found;
        }

        ret = make_boot_info_table(boot_image_buf, image_pvd,
                                   img_lba, img_size);
        if (ret < 0)
            goto ex;
        if (memcmp(boot_image_buf + 8, boot_info_found, 16) == 0)
            boot->seems_boot_info_table = 1;

        if (img_size >= Libisofs_grub2_elto_patch_poS + 8) {
            blk = 0;
            for (j = Libisofs_grub2_elto_patch_poS + 7;
                 j >= Libisofs_grub2_elto_patch_poS; j--)
                blk = (blk << 8) | boot_image_buf[j];
            if (blk == img_lba * 4 + Libisofs_grub2_elto_patch_offsT)
                boot->seems_grub2_boot_info = 1;
        }

        free(boot_image_buf);
        boot_image_buf = NULL;
    }
    ret = 1;
ex:;
    if (boot_image_buf != NULL)
        free(boot_image_buf);
    if (stream != NULL)
        iso_stream_close(stream);
    LIBISO_FREE_MEM(buf);
    return ret;
}

int iso_image_import(IsoImage *image, IsoDataSource *src,
                     struct iso_read_opts *opts,
                     IsoReadImageFeatures **features)
{
    int ret, hflag, i, idx;
    IsoImageFilesystem *fs;
    IsoFilesystem *fsback;
    IsoNodeBuilder *blback;
    IsoDir *oldroot;
    IsoFileSource *newroot;
    _ImageFsData *data;
    struct el_torito_boot_catalog *oldbootcat;
    uint8_t *rpt;
    IsoFileSource *boot_src;
    IsoNode *node;
    char *old_checksum_array = NULL;
    char checksum_type[81];
    uint32_t checksum_size;
    size_t size;
    void *ctx = NULL;
    char md5[16];
    struct el_torito_boot_catalog *catalog = NULL;
    ElToritoBootImage *boot_image = NULL;

    if (image == NULL || src == NULL || opts == NULL) {
        return ISO_NULL_POINTER;
    }


    ret = iso_image_filesystem_new(src, opts, image->id, &fs);
    if (ret < 0) {
        return ret;
    }
    data = fs->data;


    if (opts->load_system_area) {
        if (image->system_area_data != NULL)
            free(image->system_area_data);
        image->system_area_data = calloc(32768, 1);
        if (image->system_area_data == NULL)
            return ISO_OUT_OF_MEM;
        image->system_area_options = 0;
        /* Read 32768 bytes */
        for (i = 0; i < 16; i++) {
            rpt = (uint8_t *) (image->system_area_data + i * 2048);
            ret = src->read_block(src, opts->block + i, rpt);
            if (ret < 0)
                return ret;
        }
    }

    /* get root from filesystem */
    ret = fs->get_root(fs, &newroot);
    if (ret < 0) {
        return ret;
    }

    /* backup image filesystem, builder and root */
    fsback = image->fs;
    blback = image->builder;
    oldroot = image->root;
    oldbootcat = image->bootcat; /* could be NULL */
    image->bootcat = NULL;
    old_checksum_array = image->checksum_array;
    image->checksum_array = NULL;

    /* create new builder */
    ret = iso_image_builder_new(blback, &image->builder);
    if (ret < 0) {
        goto import_revert;
    }

    image->fs = fs;

    /* create new root, and set root attributes from source */
    ret = iso_node_new_root(&image->root);
    if (ret < 0) {
        goto import_revert;
    }
    {
        struct stat info;

        /* I know this will not fail */
        iso_file_source_lstat(newroot, &info);
        image->root->node.mode = info.st_mode;
        image->root->node.uid = info.st_uid;
        image->root->node.gid = info.st_gid;
        image->root->node.atime = info.st_atime;
        image->root->node.mtime = info.st_mtime;
        image->root->node.ctime = info.st_ctime;

        /* This might fail in iso_node_add_xinfo() */
        ret = src_aa_to_node(newroot, &(image->root->node), 0);
        if (ret < 0)
            goto import_revert;

        /* Attach ino as xinfo if valid */
        if (info.st_ino != 0 && !data->make_new_ino) {
            ret = iso_node_set_ino(&(image->root->node), info.st_ino, 0);
            if (ret < 0)
                goto import_revert;
        }
    }

    /* if old image has el-torito, add a new catalog */
    if (data->eltorito) {

        catalog = calloc(1, sizeof(struct el_torito_boot_catalog));
        if (catalog == NULL) {
            ret = ISO_OUT_OF_MEM;
            goto import_revert;
        }

        catalog->num_bootimages = 0;
        for (idx = 0; idx < data->num_bootimgs; idx++) {
            boot_image = calloc(1, sizeof(ElToritoBootImage));
            if (boot_image == NULL) {
                ret = ISO_OUT_OF_MEM;
                goto import_revert;
            }
            boot_image->image = NULL;
            boot_image->bootable = data->boot_flags[idx] & 1;
            boot_image->type = data->media_types[idx];
            boot_image->partition_type = data->partition_types[idx];
            boot_image->load_seg = data->load_segs[idx];
            boot_image->load_size = data->load_sizes[idx];
            boot_image->platform_id = data->platform_ids[idx];
            memcpy(boot_image->id_string, data->id_strings[idx], 28);
            memcpy(boot_image->selection_crit, data->selection_crits, 20);

            catalog->bootimages[catalog->num_bootimages] = boot_image;
            boot_image = NULL;
            catalog->num_bootimages++;
        }
        for ( ; idx < Libisofs_max_boot_imageS; idx++)
            catalog->bootimages[idx] = NULL;
        image->bootcat = catalog;
        catalog = NULL; /* So it does not get freed */
    }

    /* recursively add image */
    ret = iso_add_dir_src_rec(image, image->root, newroot);

    /* error during recursive image addition? */
    if (ret < 0) {
        iso_node_builder_unref(image->builder);
        goto import_revert;
    }

    /* Take over inode management from IsoImageFilesystem.
       data->inode_counter is supposed to hold the maximum PX inode number.
     */
    image->inode_counter = data->inode_counter;

    if ((data->px_ino_status & (2 | 4 | 8)) || opts->make_new_ino) {
        /* Attach new inode numbers to any node which does not have one,
           resp. to all nodes in case of opts->make_new_ino 
        */
        if (opts->make_new_ino)
            hflag = 1; /* Equip all data files with new unique inos */
        else
            hflag = 2 | 4 | 8; /* Equip any file type if it has ino == 0 */
        ret = img_make_inos(image, image->root, hflag);
        if (ret < 0) {
            iso_node_builder_unref(image->builder);
            goto import_revert;
        }
    }

    if (data->eltorito) {
        /* if catalog and boot image nodes were not filled,
           we create them here */
        for (idx = 0; idx < image->bootcat->num_bootimages; idx++) {
            if (image->bootcat->bootimages[idx]->image != NULL)
        continue;
            ret = create_boot_img_filesrc(fs, image, idx, &boot_src);
            if (ret < 0) {
                iso_node_builder_unref(image->builder);
                goto import_revert;
            }
            ret = image_builder_create_node(image->builder, image, boot_src,
                                            &node);
            if (ret < 0) {
                iso_node_builder_unref(image->builder);
                goto import_revert;
            }
            image->bootcat->bootimages[idx]->image = (IsoFile*)node;

            /* warn about hidden images */
            iso_msg_submit(image->id, ISO_EL_TORITO_HIDDEN, 0,
                           "Found hidden El-Torito image. Its size could not "
                           "be figured out, so image modify or boot image "
                           "patching may lead to bad results.");
        }
        if (image->bootcat->node == NULL) {
            IsoNode *node;
            IsoBoot *bootcat;
            node = calloc(1, sizeof(IsoBoot));
            if (node == NULL) {
                ret = ISO_OUT_OF_MEM;
                goto import_revert;
            }
            bootcat = (IsoBoot *) node;
            bootcat->lba = data->catblock;
            bootcat->size = data->catsize;
            bootcat->content = NULL; 
            if (bootcat->size > 0) {
                bootcat->content = calloc(1, bootcat->size);
                if (bootcat->content == NULL) {
                    ret = ISO_OUT_OF_MEM;
                    goto import_revert;
                }
                memcpy(bootcat->content, data->catcontent, bootcat->size);
            }
            node->type = LIBISO_BOOT;
            node->mode = S_IFREG;
            node->refcount = 1;
            image->bootcat->node = (IsoBoot*)node;
        }
    }

    iso_node_builder_unref(image->builder);

    /* set volume attributes */
    iso_image_set_volset_id(image, data->volset_id);
    iso_image_set_volume_id(image, data->volume_id);
    iso_image_set_publisher_id(image, data->publisher_id);
    iso_image_set_data_preparer_id(image, data->data_preparer_id);
    iso_image_set_system_id(image, data->system_id);
    iso_image_set_application_id(image, data->application_id);
    iso_image_set_copyright_file_id(image, data->copyright_file_id);
    iso_image_set_abstract_file_id(image, data->abstract_file_id);
    iso_image_set_biblio_file_id(image, data->biblio_file_id);
    iso_image_set_pvd_times(image, data->creation_time,
         data->modification_time, data->expiration_time, data->effective_time);

    if (features != NULL) {
        *features = malloc(sizeof(IsoReadImageFeatures));
        if (*features == NULL) {
            ret = ISO_OUT_OF_MEM;
            goto import_revert;
        }
        (*features)->hasJoliet = data->joliet;
        (*features)->hasRR = data->rr_version != 0;
        (*features)->hasIso1999 = data->iso1999;
        (*features)->hasElTorito = data->eltorito;
        (*features)->size = data->nblocks;
    }

    if (data->md5_load) {
        /* Read checksum array */
        ret = iso_root_get_isofsca((IsoNode *) image->root,
                                   &(image->checksum_start_lba),
                                   &(image->checksum_end_lba),
                                   &(image->checksum_idx_count),
                                   &checksum_size, checksum_type, 0); 
        if (ret > 0)
            if (checksum_size != 16 || strcmp(checksum_type, "MD5") != 0)
                ret = 0;
        if (ret > 0 && image->checksum_idx_count > 1) {
            size = image->checksum_idx_count / 128;
            if (size * 128 < image->checksum_idx_count)
                size++;
            image->checksum_array = calloc(size, 2048);
            if (image->checksum_array == NULL) {
                ret = ISO_OUT_OF_MEM;
                goto import_revert;
            }

            /* Load from image->checksum_end_lba */;
            for (i = 0; i < (int) size; i++) {
                rpt = (uint8_t *) (image->checksum_array + i * 2048);
                ret = src->read_block(src, image->checksum_end_lba + i, rpt);
                if (ret <= 0)
                    goto import_cleanup;
            }

            /* Compute MD5 and compare with recorded MD5 */
            ret = iso_md5_start(&ctx);
            if (ret < 0) {
                ret = ISO_OUT_OF_MEM;
                goto import_revert;
            }
            for (i = 0; i < (int) image->checksum_idx_count - 1; i++)
                iso_md5_compute(ctx, image->checksum_array + i * 16, 16);
            iso_md5_end(&ctx, md5);
            for (i = 0; i < 16; i++)
                if (md5[i] != image->checksum_array[
                                      (image->checksum_idx_count - 1) * 16 + i]
                   )
            break;
            if (i < 16) {
                iso_msg_submit(image->id, ISO_MD5_AREA_CORRUPTED, 0,
  "MD5 checksum array appears damaged and not trustworthy for verifications.");
                free(image->checksum_array);
                image->checksum_array = NULL;
                image->checksum_idx_count = 0;
            }
        }
    }

    ret = iso_image_eval_boot_info_table(image, opts, src, data->nblocks, 0);
    if (ret < 0)
        goto import_revert;

    ret = ISO_SUCCESS;
    goto import_cleanup;

    import_revert:;

    iso_node_unref((IsoNode*)image->root);
    el_torito_boot_catalog_free(image->bootcat);
    image->root = oldroot;
    oldroot = NULL;
    image->bootcat = oldbootcat;
    oldbootcat = NULL;
    image->checksum_array = old_checksum_array;
    old_checksum_array = NULL;

    import_cleanup:;

    /* recover backed fs and builder */
    image->fs = fsback;
    image->builder = blback;

    /* free old root */
    if (oldroot != NULL)
        iso_node_unref((IsoNode*)oldroot);

    /* free old boot catalog */
    if (oldbootcat != NULL)
        el_torito_boot_catalog_free(oldbootcat);

    if (catalog != NULL)
        el_torito_boot_catalog_free(catalog);
    if (boot_image != NULL)
        free((char *) boot_image);
    iso_file_source_unref(newroot);
    fs->close(fs);
    iso_filesystem_unref(fs);
    if (old_checksum_array != NULL)
        free(old_checksum_array);
    if (ctx != NULL)
        iso_md5_end(&ctx, md5);
    return ret;
}

const char *iso_image_fs_get_volset_id(IsoImageFilesystem *fs)
{
    _ImageFsData *data = (_ImageFsData*) fs->data;
    return data->volset_id;
}

const char *iso_image_fs_get_volume_id(IsoImageFilesystem *fs)
{
    _ImageFsData *data = (_ImageFsData*) fs->data;
    return data->volume_id;
}

const char *iso_image_fs_get_publisher_id(IsoImageFilesystem *fs)
{
    _ImageFsData *data = (_ImageFsData*) fs->data;
    return data->publisher_id;
}

const char *iso_image_fs_get_data_preparer_id(IsoImageFilesystem *fs)
{
    _ImageFsData *data = (_ImageFsData*) fs->data;
    return data->data_preparer_id;
}

const char *iso_image_fs_get_system_id(IsoImageFilesystem *fs)
{
    _ImageFsData *data = (_ImageFsData*) fs->data;
    return data->system_id;
}

const char *iso_image_fs_get_application_id(IsoImageFilesystem *fs)
{
    _ImageFsData *data = (_ImageFsData*) fs->data;
    return data->application_id;
}

const char *iso_image_fs_get_copyright_file_id(IsoImageFilesystem *fs)
{
    _ImageFsData *data = (_ImageFsData*) fs->data;
    return data->copyright_file_id;
}

const char *iso_image_fs_get_abstract_file_id(IsoImageFilesystem *fs)
{
    _ImageFsData *data;
    data = (_ImageFsData*) fs->data;
    return data->abstract_file_id;
}

const char *iso_image_fs_get_biblio_file_id(IsoImageFilesystem *fs)
{
    _ImageFsData *data = (_ImageFsData*) fs->data;
    return data->biblio_file_id;
}

int iso_read_opts_new(IsoReadOpts **opts, int profile)
{
    IsoReadOpts *ropts;

    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    if (profile != 0) {
        return ISO_WRONG_ARG_VALUE;
    }

    ropts = calloc(1, sizeof(IsoReadOpts));
    if (ropts == NULL) {
        return ISO_OUT_OF_MEM;
    }

    ropts->file_mode = 0444;
    ropts->dir_mode = 0555;
    ropts->noaaip = 1;
    ropts->nomd5 = 1;
    ropts->load_system_area = 0;

    *opts = ropts;
    return ISO_SUCCESS;
}

void iso_read_opts_free(IsoReadOpts *opts)
{
    if (opts == NULL) {
        return;
    }

    free(opts->input_charset);
    free(opts);
}

int iso_read_opts_set_start_block(IsoReadOpts *opts, uint32_t block)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->block = block;
    return ISO_SUCCESS;
}

int iso_read_opts_set_no_rockridge(IsoReadOpts *opts, int norr)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->norock = norr ? 1 :0;
    return ISO_SUCCESS;
}

int iso_read_opts_set_no_joliet(IsoReadOpts *opts, int nojoliet)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->nojoliet = nojoliet ? 1 :0;
    return ISO_SUCCESS;
}

int iso_read_opts_set_no_iso1999(IsoReadOpts *opts, int noiso1999)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->noiso1999 = noiso1999 ? 1 :0;
    return ISO_SUCCESS;
}

int iso_read_opts_set_no_aaip(IsoReadOpts *opts, int noaaip)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->noaaip = noaaip ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_read_opts_set_no_md5(IsoReadOpts *opts, int no_md5)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->nomd5 = no_md5 == 2 ? 2 : no_md5 == 1 ? 1 : 0;
    return ISO_SUCCESS;
}


int iso_read_opts_set_new_inos(IsoReadOpts *opts, int new_inos)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->make_new_ino = new_inos ? 1 : 0;
    return ISO_SUCCESS;
}

int iso_read_opts_set_preferjoliet(IsoReadOpts *opts, int preferjoliet)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->preferjoliet = preferjoliet ? 1 :0;
    return ISO_SUCCESS;
}

int iso_read_opts_set_default_uid(IsoReadOpts *opts, uid_t uid)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->uid = uid;
    return ISO_SUCCESS;
}

int iso_read_opts_set_default_gid(IsoReadOpts *opts, gid_t gid)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->gid = gid;
    return ISO_SUCCESS;
}

int iso_read_opts_set_default_permissions(IsoReadOpts *opts, mode_t file_perm,
                                          mode_t dir_perm)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->file_mode = file_perm;
    opts->dir_mode = dir_perm;
    return ISO_SUCCESS;
}

int iso_read_opts_set_input_charset(IsoReadOpts *opts, const char *charset)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->input_charset = charset ? strdup(charset) : NULL;
    return ISO_SUCCESS;
}

int iso_read_opts_auto_input_charset(IsoReadOpts *opts, int mode)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->auto_input_charset = mode;
    return ISO_SUCCESS;
}

int iso_read_opts_load_system_area(IsoReadOpts *opts, int mode)
{
    if (opts == NULL) {
        return ISO_NULL_POINTER;
    }
    opts->load_system_area = mode & 1;
    return ISO_SUCCESS;
}

/**
 * Destroy an IsoReadImageFeatures object obtained with iso_image_import.
 */
void iso_read_image_features_destroy(IsoReadImageFeatures *f)
{
    if (f) {
        free(f);
    }
}

/**
 * Get the size (in 2048 byte block) of the image, as reported in the PVM.
 */
uint32_t iso_read_image_features_get_size(IsoReadImageFeatures *f)
{
    return f->size;
}

/**
 * Whether RockRidge extensions are present in the image imported.
 */
int iso_read_image_features_has_rockridge(IsoReadImageFeatures *f)
{
    return f->hasRR;
}

/**
 * Whether Joliet extensions are present in the image imported.
 */
int iso_read_image_features_has_joliet(IsoReadImageFeatures *f)
{
    return f->hasJoliet;
}

/**
 * Whether the image is recorded according to ISO 9660:1999, i.e. it has
 * a version 2 Enhanced Volume Descriptor.
 */
int iso_read_image_features_has_iso1999(IsoReadImageFeatures *f)
{
    return f->hasIso1999;
}

/**
 * Whether El-Torito boot record is present present in the image imported.
 */
int iso_read_image_features_has_eltorito(IsoReadImageFeatures *f)
{
    return f->hasElTorito;
}


/**
 * Get the start addresses and the sizes of the data extents of a file node
 * if it was imported from an old image.
 *
 * @param file
 *      The file
 * @param section_count
 *      Returns the number of extent entries in sections arrays
 * @param sections
 *      Returns the array of file sections. Apply free() to dispose it.
 * @param flag
 *      Reserved for future usage, submit 0
 * @return
 *      1 if there are valid extents (file comes from old image),
 *      0 if file was newly added, i.e. it does not come from an old image,
 *      < 0 error
 */
int iso_file_get_old_image_sections(IsoFile *file, int *section_count,
                                   struct iso_file_section **sections,
                                   int flag)
{
    if (file == NULL || section_count == NULL || sections == NULL) {
        return ISO_NULL_POINTER;
    }
    if (flag != 0) {
        return ISO_WRONG_ARG_VALUE;
    }
    if (file->from_old_session != 0) {

        /*
         * When file is from old session, we retrieve the original IsoFileSource
         * to get the sections. This break encapsultation, but safes memory as
         * we don't need to store the sections in the IsoFile node.
         */
        IsoStream *stream = file->stream, *input_stream;
        FSrcStreamData *data;
        ImageFileSourceData *ifsdata;

        /* Get the most original stream */
        while (1) {
            input_stream = iso_stream_get_input_stream(stream, 0);
            if (input_stream == NULL || input_stream == stream)
        break;
            stream = input_stream;
        }

        /* From here on it must be a stream with FSrcStreamData. */
        /* ??? Shall one rather check :
                stream->class == extern IsoStreamIface fsrc_stream_class
               (its storage location is global in stream.c)
         */
        if (stream->class->type[0] != 'f' || stream->class->type[1] != 's' ||
            stream->class->type[2] != 'r' || stream->class->type[3] != 'c')
            return 0;

        data = stream->data;
        ifsdata = data->src->data;

        *section_count = ifsdata->nsections;
        *sections = malloc(ifsdata->nsections *
                                sizeof(struct iso_file_section));
        if (*sections == NULL) {
            return ISO_OUT_OF_MEM;
        }
        memcpy(*sections, ifsdata->sections,
               ifsdata->nsections * sizeof(struct iso_file_section));
        return 1;
    }
    return 0;
}

/* Rank two IsoFileSource by their eventual old image LBAs.
   Other IsoFileSource classes will be ranked only roughly.
*/
int iso_ifs_sections_cmp(IsoFileSource *s1, IsoFileSource *s2, int flag)
{
    int i;
    ImageFileSourceData *d1, *d2;

    if (s1->class != s2->class)
        return (s1->class < s2->class ? -1 : 1);
    if (s1->class != &ifs_class)
        return(0);

    d1= s1->data;
    d2= s2->data;
    for (i = 0; i < d1->nsections; i++) {
        if (i >= d2->nsections)
            return 1;
        if (d1->sections[i].block != d2->sections[i].block)
            return (d1->sections[i].block < d2->sections[i].block ? -1 : 1);
        if (d1->sections[i].size != d2->sections[i].size)
            return (d1->sections[i].size < d2->sections[i].size ? -1 : 1);
    }
    return(0);
}
