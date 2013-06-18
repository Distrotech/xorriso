
#ifndef LIBISO_LIBISOFS_H_
#define LIBISO_LIBISOFS_H_

/*
 * Copyright (c) 2007-2008 Vreixo Formoso, Mario Danic
 * Copyright (c) 2009-2013 Thomas Schmitt
 *
 * This file is part of the libisofs project; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

/* Important: If you add a public API function then add its name to file
                 libisofs/libisofs.ver 
*/

/* 
 *
 * Applications must use 64 bit off_t.
 * E.g. on 32-bit GNU/Linux by defining
 *   #define _LARGEFILE_SOURCE
 *   #define _FILE_OFFSET_BITS 64
 * The minimum requirement is to interface with the library by 64 bit signed
 * integers where libisofs.h or libisoburn.h prescribe off_t.
 * Failure to do so may result in surprising malfunction or memory faults.
 * 
 * Application files which include libisofs/libisofs.h must provide
 * definitions for uint32_t and uint8_t.
 * This can be achieved either:
 * - by using autotools which will define HAVE_STDINT_H or HAVE_INTTYPES_H
 *   according to its ./configure tests,
 * - or by defining the macros HAVE_STDINT_H resp. HAVE_INTTYPES_H according
 *   to the local situation,
 * - or by appropriately defining uint32_t and uint8_t by other means,
 *   e.g. by including inttypes.h before including libisofs.h
 */
#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif


/*
 * Normally this API is operated via public functions and opaque object
 * handles. But it also exposes several C structures which may be used to
 * provide custom functionality for the objects of the API. The same
 * structures are used for internal objects of libisofs, too.
 * You are not supposed to manipulate the entrails of such objects if they
 * are not your own custom extensions.
 *
 * See for an example IsoStream = struct iso_stream below.
 */


#include <sys/stat.h>

#include <stdlib.h>


/**
 * The following two functions and three macros are utilities to help ensuring
 * version match of application, compile time header, and runtime library.
 */
/**
 * These three release version numbers tell the revision of this header file
 * and of the API it describes. They are memorized by applications at
 * compile time.
 * They must show the same values as these symbols in ./configure.ac
 *     LIBISOFS_MAJOR_VERSION=...
 *     LIBISOFS_MINOR_VERSION=...
 *     LIBISOFS_MICRO_VERSION=...
 * Note to anybody who does own work inside libisofs:
 * Any change of configure.ac or libisofs.h has to keep up this equality !
 *
 * Before usage of these macros on your code, please read the usage discussion
 * below.
 *
 * @since 0.6.2
 */
#define iso_lib_header_version_major  1
#define iso_lib_header_version_minor  3
#define iso_lib_header_version_micro  0

/**
 * Get version of the libisofs library at runtime.
 * NOTE: This function may be called before iso_init().
 *
 * @since 0.6.2
 */
void iso_lib_version(int *major, int *minor, int *micro);

/**
 * Check at runtime if the library is ABI compatible with the given version.
 * NOTE: This function may be called before iso_init().
 *
 * @return
 *      1 lib is compatible, 0 is not.
 *
 * @since 0.6.2
 */
int iso_lib_is_compatible(int major, int minor, int micro);

/**
 * Usage discussion:
 *
 * Some developers of the libburnia project have differing opinions how to
 * ensure the compatibility of libaries and applications.
 *
 * It is about whether to use at compile time and at runtime the version
 * numbers provided here. Thomas Schmitt advises to use them. Vreixo Formoso
 * advises to use other means.
 *
 * At compile time:
 *
 * Vreixo Formoso advises to leave proper version matching to properly
 * programmed checks in the the application's build system, which will
 * eventually refuse compilation.
 *
 * Thomas Schmitt advises to use the macros defined here for comparison with
 * the application's requirements of library revisions and to eventually
 * break compilation.
 *
 * Both advises are combinable. I.e. be master of your build system and have
 * #if checks in the source code of your application, nevertheless.
 *
 * At runtime (via iso_lib_is_compatible()):
 *
 * Vreixo Formoso advises to compare the application's requirements of
 * library revisions with the runtime library. This is to allow runtime
 * libraries which are young enough for the application but too old for
 * the lib*.h files seen at compile time.
 *
 * Thomas Schmitt advises to compare the header revisions defined here with
 * the runtime library. This is to enforce a strictly monotonous chain of
 * revisions from app to header to library, at the cost of excluding some older
 * libraries.
 *
 * These two advises are mutually exclusive.
 */

struct burn_source;

/**
 * Context for image creation. It holds the files that will be added to image,
 * and several options to control libisofs behavior.
 *
 * @since 0.6.2
 */
typedef struct Iso_Image IsoImage;

/*
 * A node in the iso tree, i.e. a file that will be written to image.
 *
 * It can represent any kind of files. When needed, you can get the type with
 * iso_node_get_type() and cast it to the appropiate subtype. Useful macros
 * are provided, see below.
 *
 * @since 0.6.2
 */
typedef struct Iso_Node IsoNode;

/**
 * A directory in the iso tree. It is an special type of IsoNode and can be
 * casted to it in any case.
 *
 * @since 0.6.2
 */
typedef struct Iso_Dir IsoDir;

/**
 * A symbolic link in the iso tree. It is an special type of IsoNode and can be
 * casted to it in any case.
 *
 * @since 0.6.2
 */
typedef struct Iso_Symlink IsoSymlink;

/**
 * A regular file in the iso tree. It is an special type of IsoNode and can be
 * casted to it in any case.
 *
 * @since 0.6.2
 */
typedef struct Iso_File IsoFile;

/**
 * An special file in the iso tree. This is used to represent any POSIX file
 * other that regular files, directories or symlinks, i.e.: socket, block and
 * character devices, and fifos.
 * It is an special type of IsoNode and can be casted to it in any case.
 *
 * @since 0.6.2
 */
typedef struct Iso_Special IsoSpecial;

/**
 * The type of an IsoNode.
 *
 * When an user gets an IsoNode from an image, (s)he can use
 * iso_node_get_type() to get the current type of the node, and then
 * cast to the appropriate subtype. For example:
 *
 * ...
 * IsoNode *node;
 * res = iso_dir_iter_next(iter, &node);
 * if (res == 1 && iso_node_get_type(node) == LIBISO_DIR) {
 *      IsoDir *dir = (IsoDir *)node;
 *      ...
 * }
 *
 * @since 0.6.2
 */
enum IsoNodeType {
    LIBISO_DIR,
    LIBISO_FILE,
    LIBISO_SYMLINK,
    LIBISO_SPECIAL,
    LIBISO_BOOT
};

/* macros to check node type */
#define ISO_NODE_IS_DIR(n) (iso_node_get_type(n) == LIBISO_DIR)
#define ISO_NODE_IS_FILE(n) (iso_node_get_type(n) == LIBISO_FILE)
#define ISO_NODE_IS_SYMLINK(n) (iso_node_get_type(n) == LIBISO_SYMLINK)
#define ISO_NODE_IS_SPECIAL(n) (iso_node_get_type(n) == LIBISO_SPECIAL)
#define ISO_NODE_IS_BOOTCAT(n) (iso_node_get_type(n) == LIBISO_BOOT)

/* macros for safe downcasting */
#define ISO_DIR(n) ((IsoDir*)(ISO_NODE_IS_DIR(n) ? n : NULL))
#define ISO_FILE(n) ((IsoFile*)(ISO_NODE_IS_FILE(n) ? n : NULL))
#define ISO_SYMLINK(n) ((IsoSymlink*)(ISO_NODE_IS_SYMLINK(n) ? n : NULL))
#define ISO_SPECIAL(n) ((IsoSpecial*)(ISO_NODE_IS_SPECIAL(n) ? n : NULL))

#define ISO_NODE(n) ((IsoNode*)n)

/**
 * File section in an old image.
 *
 * @since 0.6.8
 */
struct iso_file_section
{
    uint32_t block;
    uint32_t size;
};

/* If you get here because of a compilation error like

       /usr/include/libisofs/libisofs.h:166: error:
       expected specifier-qualifier-list before 'uint32_t'

   then see the paragraph above about the definition of uint32_t.
*/


/**
 * Context for iterate on directory children.
 * @see iso_dir_get_children()
 *
 * @since 0.6.2
 */
typedef struct Iso_Dir_Iter IsoDirIter;

/**
 * It represents an El-Torito boot image.
 *
 * @since 0.6.2
 */
typedef struct el_torito_boot_image ElToritoBootImage;

/**
 * An special type of IsoNode that acts as a placeholder for an El-Torito
 * boot catalog. Once written, it will appear as a regular file.
 *
 * @since 0.6.2
 */
typedef struct Iso_Boot IsoBoot;

/**
 * Flag used to hide a file in the RR/ISO or Joliet tree.
 *
 * @see iso_node_set_hidden
 * @since 0.6.2
 */
enum IsoHideNodeFlag {
    /** Hide the node in the ECMA-119 / RR tree */
    LIBISO_HIDE_ON_RR = 1 << 0,
    /** Hide the node in the Joliet tree, if Joliet extension are enabled */
    LIBISO_HIDE_ON_JOLIET = 1 << 1,
    /** Hide the node in the ISO-9660:1999 tree, if that format is enabled */
    LIBISO_HIDE_ON_1999 = 1 << 2,

    /** Hide the node in the HFS+ tree, if that format is enabled.
        @since 1.2.4
    */
    LIBISO_HIDE_ON_HFSPLUS = 1 << 4,

    /** Hide the node in the FAT tree, if that format is enabled.
        @since 1.2.4
    */
    LIBISO_HIDE_ON_FAT = 1 << 5,

    /** With IsoNode and IsoBoot: Write data content even if the node is
     *                            not visible in any tree.
     *  With directory nodes    : Write data content of IsoNode and IsoBoot
     *                            in the directory's tree unless they are
     *                            explicitely marked LIBISO_HIDE_ON_RR
     *                            without LIBISO_HIDE_BUT_WRITE.
     *  @since 0.6.34
     */
    LIBISO_HIDE_BUT_WRITE = 1 << 3
};

/**
 * El-Torito bootable image type.
 *
 * @since 0.6.2
 */
enum eltorito_boot_media_type {
    ELTORITO_FLOPPY_EMUL,
    ELTORITO_HARD_DISC_EMUL,
    ELTORITO_NO_EMUL
};

/**
 * Replace mode used when addding a node to a file.
 * This controls how libisofs will act when you tried to add to a dir a file
 * with the same name that an existing file.
 *
 * @since 0.6.2
 */
enum iso_replace_mode {
    /**
     * Never replace an existing node, and instead fail with
     * ISO_NODE_NAME_NOT_UNIQUE.
     */
    ISO_REPLACE_NEVER,
    /**
     * Always replace the old node with the new.
     */
    ISO_REPLACE_ALWAYS,
    /**
     * Replace with the new node if it is the same file type
     */
    ISO_REPLACE_IF_SAME_TYPE,
    /**
     * Replace with the new node if it is the same file type and its ctime
     * is newer than the old one.
     */
    ISO_REPLACE_IF_SAME_TYPE_AND_NEWER,
    /**
     * Replace with the new node if its ctime is newer than the old one.
     */
    ISO_REPLACE_IF_NEWER
    /*
     * TODO #00006 define more values
     *  -if both are dirs, add contents (and what to do with conflicts?)
     */
};

/**
 * Options for image written.
 * @see iso_write_opts_new()
 * @since 0.6.2
 */
typedef struct iso_write_opts IsoWriteOpts;

/**
 * Options for image reading or import.
 * @see iso_read_opts_new()
 * @since 0.6.2
 */
typedef struct iso_read_opts IsoReadOpts;

/**
 * Source for image reading.
 *
 * @see struct iso_data_source
 * @since 0.6.2
 */
typedef struct iso_data_source IsoDataSource;

/**
 * Data source used by libisofs for reading an existing image.
 *
 * It offers homogeneous read access to arbitrary blocks to different sources
 * for images, such as .iso files, CD/DVD drives, etc...
 *
 * To create a multisession image, libisofs needs a IsoDataSource, that the
 * user must provide. The function iso_data_source_new_from_file() constructs
 * an IsoDataSource that uses POSIX I/O functions to access data. You can use
 * it with regular .iso images, and also with block devices that represent a
 * drive.
 *
 * @since 0.6.2
 */
struct iso_data_source
{

    /* reserved for future usage, set to 0 */
    int version;

    /**
     * Reference count for the data source. Should be 1 when a new source
     * is created. Don't access it directly, but with iso_data_source_ref()
     * and iso_data_source_unref() functions.
     */
    unsigned int refcount;

    /**
     * Opens the given source. You must open() the source before any attempt
     * to read data from it. The open is the right place for grabbing the
     * underlying resources.
     *
     * @return
     *      1 if success, < 0 on error (has to be a valid libisofs error code)
     */
    int (*open)(IsoDataSource *src);

    /**
     * Close a given source, freeing all system resources previously grabbed in
     * open().
     *
     * @return
     *      1 if success, < 0 on error (has to be a valid libisofs error code)
     */
    int (*close)(IsoDataSource *src);

    /**
     * Read an arbitrary block (2048 bytes) of data from the source.
     *
     * @param lba
     *     Block to be read.
     * @param buffer
     *     Buffer where the data will be written. It should have at least
     *     2048 bytes.
     * @return
     *      1 if success,
     *    < 0 if error. This function has to emit a valid libisofs error code.
     *        Predifined (but not mandatory) for this purpose are:
     *          ISO_DATA_SOURCE_SORRY ,   ISO_DATA_SOURCE_MISHAP,
     *          ISO_DATA_SOURCE_FAILURE , ISO_DATA_SOURCE_FATAL
     */
    int (*read_block)(IsoDataSource *src, uint32_t lba, uint8_t *buffer);

    /**
     * Clean up the source specific data. Never call this directly, it is
     * automatically called by iso_data_source_unref() when refcount reach
     * 0.
     */
    void (*free_data)(IsoDataSource *src);

    /** Source specific data */
    void *data;
};

/**
 * Return information for image. This is optionally allocated by libisofs,
 * as a way to inform user about the features of an existing image, such as
 * extensions present, size, ...
 *
 * @see iso_image_import()
 * @since 0.6.2
 */
typedef struct iso_read_image_features IsoReadImageFeatures;

/**
 * POSIX abstraction for source files.
 *
 * @see struct iso_file_source
 * @since 0.6.2
 */
typedef struct iso_file_source IsoFileSource;

/**
 * Abstract for source filesystems.
 *
 * @see struct iso_filesystem
 * @since 0.6.2
 */
typedef struct iso_filesystem IsoFilesystem;

/**
 * Interface that defines the operations (methods) available for an
 * IsoFileSource.
 *
 * @see struct IsoFileSource_Iface
 * @since 0.6.2
 */
typedef struct IsoFileSource_Iface IsoFileSourceIface;

/**
 * IsoFilesystem implementation to deal with ISO images, and to offer a way to
 * access specific information of the image, such as several volume attributes,
 * extensions being used, El-Torito artifacts...
 *
 * @since 0.6.2
 */
typedef IsoFilesystem IsoImageFilesystem;

/**
 * See IsoFilesystem->get_id() for info about this.
 * @since 0.6.2
 */
extern unsigned int iso_fs_global_id;

/**
 * An IsoFilesystem is a handler for a source of files, or a "filesystem".
 * That is defined as a set of files that are organized in a hierarchical
 * structure.
 *
 * A filesystem allows libisofs to access files from several sources in
 * an homogeneous way, thus abstracting the underlying operations needed to
 * access and read file contents. Note that this doesn't need to be tied
 * to the disc filesystem used in the partition being accessed. For example,
 * we have an IsoFilesystem implementation to access any mounted filesystem,
 * using standard POSIX functions. It is also legal, of course, to implement
 * an IsoFilesystem to deal with a specific filesystem over raw partitions.
 * That is what we do, for example, to access an ISO Image.
 *
 * Each file inside an IsoFilesystem is represented as an IsoFileSource object,
 * that defines POSIX-like interface for accessing files.
 *
 * @since 0.6.2
 */
struct iso_filesystem
{
    /**
     * Type of filesystem.
     * "file" -> local filesystem
     * "iso " -> iso image filesystem
     */
    char type[4];

    /* reserved for future usage, set to 0 */
    int version;

    /**
     * Get the root of a filesystem.
     *
     * @return
     *    1 on success, < 0 on error (has to be a valid libisofs error code)
     */
    int (*get_root)(IsoFilesystem *fs, IsoFileSource **root);

    /**
     * Retrieve a file from its absolute path inside the filesystem.
     * @param file
     *     Returns a pointer to a IsoFileSource object representing the
     *     file. It has to be disposed by iso_file_source_unref() when
     *     no longer needed.
     * @return
     *     1 success, < 0 error (has to be a valid libisofs error code)
     *      Error codes:
     *         ISO_FILE_ACCESS_DENIED
     *         ISO_FILE_BAD_PATH
     *         ISO_FILE_DOESNT_EXIST
     *         ISO_OUT_OF_MEM
     *         ISO_FILE_ERROR
     *         ISO_NULL_POINTER
     */
    int (*get_by_path)(IsoFilesystem *fs, const char *path,
                       IsoFileSource **file);

    /**
     * Get filesystem identifier.
     *
     * If the filesystem is able to generate correct values of the st_dev
     * and st_ino fields for the struct stat of each file, this should
     * return an unique number, greater than 0.
     *
     * To get a identifier for your filesystem implementation you should
     * use iso_fs_global_id, incrementing it by one each time.
     *
     * Otherwise, if you can't ensure values in the struct stat are valid,
     * this should return 0.
     */
    unsigned int (*get_id)(IsoFilesystem *fs);

    /**
     * Opens the filesystem for several read operations. Calling this funcion
     * is not needed at all, each time that the underlying system resource
     * needs to be accessed, it is openned propertly.
     * However, if you plan to execute several operations on the filesystem,
     * it is a good idea to open it previously, to prevent several open/close
     * operations to occur.
     *
     * @return 1 on success, < 0 on error (has to be a valid libisofs error code)
     */
    int (*open)(IsoFilesystem *fs);

    /**
     * Close the filesystem, thus freeing all system resources. You should
     * call this function if you have previously open() it.
     * Note that you can open()/close() a filesystem several times.
     *
     * @return 1 on success, < 0 on error (has to be a valid libisofs error code)
     */
    int (*close)(IsoFilesystem *fs);

    /**
     * Free implementation specific data. Should never be called by user.
     * Use iso_filesystem_unref() instead.
     */
    void (*free)(IsoFilesystem *fs);

    /* internal usage, do never access them directly */
    unsigned int refcount;
    void *data;
};

/**
 * Interface definition for an IsoFileSource. Defines the POSIX-like function
 * to access files and abstract underlying source.
 *
 * @since 0.6.2
 */
struct IsoFileSource_Iface
{
    /**
     * Tells the version of the interface:
     * Version 0 provides functions up to (*lseek)().
     * @since 0.6.2
     * Version 1 additionally provides function *(get_aa_string)().
     * @since 0.6.14
     * Version 2 additionally provides function *(clone_src)().
     * @since 1.0.2
     */
    int version;

    /**
     * Get the absolute path in the filesystem this file source belongs to.
     *
     * @return
     *     the path of the FileSource inside the filesystem, it should be
     *     freed when no more needed.
     */
    char* (*get_path)(IsoFileSource *src);

    /**
     * Get the name of the file, with the dir component of the path.
     *
     * @return
     *     the name of the file, it should be freed when no more needed.
     */
    char* (*get_name)(IsoFileSource *src);

    /**
     * Get information about the file. It is equivalent to lstat(2).
     *
     * @return
     *    1 success, < 0 error (has to be a valid libisofs error code)
     *      Error codes:
     *         ISO_FILE_ACCESS_DENIED
     *         ISO_FILE_BAD_PATH
     *         ISO_FILE_DOESNT_EXIST
     *         ISO_OUT_OF_MEM
     *         ISO_FILE_ERROR
     *         ISO_NULL_POINTER
     */
    int (*lstat)(IsoFileSource *src, struct stat *info);

    /**
     * Get information about the file. If the file is a symlink, the info
     * returned refers to the destination. It is equivalent to stat(2).
     *
     * @return
     *    1 success, < 0 error
     *      Error codes:
     *         ISO_FILE_ACCESS_DENIED
     *         ISO_FILE_BAD_PATH
     *         ISO_FILE_DOESNT_EXIST
     *         ISO_OUT_OF_MEM
     *         ISO_FILE_ERROR
     *         ISO_NULL_POINTER
     */
    int (*stat)(IsoFileSource *src, struct stat *info);

    /**
     * Check if the process has access to read file contents. Note that this
     * is not necessarily related with (l)stat functions. For example, in a
     * filesystem implementation to deal with an ISO image, if the user has
     * read access to the image it will be able to read all files inside it,
     * despite of the particular permission of each file in the RR tree, that
     * are what the above functions return.
     *
     * @return
     *     1 if process has read access, < 0 on error (has to be a valid
     *     libisofs error code)
     *      Error codes:
     *         ISO_FILE_ACCESS_DENIED
     *         ISO_FILE_BAD_PATH
     *         ISO_FILE_DOESNT_EXIST
     *         ISO_OUT_OF_MEM
     *         ISO_FILE_ERROR
     *         ISO_NULL_POINTER
     */
    int (*access)(IsoFileSource *src);

    /**
     * Opens the source.
     * @return 1 on success, < 0 on error (has to be a valid libisofs error code)
     *      Error codes:
     *         ISO_FILE_ALREADY_OPENED
     *         ISO_FILE_ACCESS_DENIED
     *         ISO_FILE_BAD_PATH
     *         ISO_FILE_DOESNT_EXIST
     *         ISO_OUT_OF_MEM
     *         ISO_FILE_ERROR
     *         ISO_NULL_POINTER
     */
    int (*open)(IsoFileSource *src);

    /**
     * Close a previuously openned file
     * @return 1 on success, < 0 on error
     *      Error codes:
     *         ISO_FILE_ERROR
     *         ISO_NULL_POINTER
     *         ISO_FILE_NOT_OPENED
     */
    int (*close)(IsoFileSource *src);

    /**
     * Attempts to read up to count bytes from the given source into
     * the buffer starting at buf.
     *
     * The file src must be open() before calling this, and close() when no
     * more needed. Not valid for dirs. On symlinks it reads the destination
     * file.
     *
     * @return
     *     number of bytes read, 0 if EOF, < 0 on error (has to be a valid
     *     libisofs error code)
     *      Error codes:
     *         ISO_FILE_ERROR
     *         ISO_NULL_POINTER
     *         ISO_FILE_NOT_OPENED
     *         ISO_WRONG_ARG_VALUE -> if count == 0
     *         ISO_FILE_IS_DIR
     *         ISO_OUT_OF_MEM
     *         ISO_INTERRUPTED
     */
    int (*read)(IsoFileSource *src, void *buf, size_t count);

    /**
     * Read a directory.
     *
     * Each call to this function will return a new children, until we reach
     * the end of file (i.e, no more children), in that case it returns 0.
     *
     * The dir must be open() before calling this, and close() when no more
     * needed. Only valid for dirs.
     *
     * Note that "." and ".." children MUST NOT BE returned.
     *
     * @param child
     *     pointer to be filled with the given child. Undefined on error or OEF
     * @return
     *     1 on success, 0 if EOF (no more children), < 0 on error (has to be
     *     a valid libisofs error code)
     *      Error codes:
     *         ISO_FILE_ERROR
     *         ISO_NULL_POINTER
     *         ISO_FILE_NOT_OPENED
     *         ISO_FILE_IS_NOT_DIR
     *         ISO_OUT_OF_MEM
     */
    int (*readdir)(IsoFileSource *src, IsoFileSource **child);

    /**
     * Read the destination of a symlink. You don't need to open the file
     * to call this.
     *
     * @param buf
     *     allocated buffer of at least bufsiz bytes.
     *     The dest. will be copied there, and it will be NULL-terminated
     * @param bufsiz
     *     characters to be copied. Destination link will be truncated if
     *     it is larger than given size. This include the 0x0 character.
     * @return
     *     1 on success, < 0 on error (has to be a valid libisofs error code)
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
    int (*readlink)(IsoFileSource *src, char *buf, size_t bufsiz);

    /**
     * Get the filesystem for this source. No extra ref is added, so you
     * musn't unref the IsoFilesystem.
     *
     * @return
     *     The filesystem, NULL on error
     */
    IsoFilesystem* (*get_filesystem)(IsoFileSource *src);

    /**
     * Free implementation specific data. Should never be called by user.
     * Use iso_file_source_unref() instead.
     */
    void (*free)(IsoFileSource *src);

    /**
     * Repositions the offset of the IsoFileSource (must be opened) to the
     * given offset according to the value of flag.
     *
     * @param offset
     *      in bytes
     * @param flag
     *      0 The offset is set to offset bytes (SEEK_SET)
     *      1 The offset is set to its current location plus offset bytes
     *        (SEEK_CUR)
     *      2 The offset is set to the size of the file plus offset bytes
     *        (SEEK_END).
     * @return
     *      Absolute offset position of the file, or < 0 on error. Cast the
     *      returning value to int to get a valid libisofs error.
     *
     * @since 0.6.4
     */
    off_t (*lseek)(IsoFileSource *src, off_t offset, int flag);

    /* Add-ons of .version 1 begin here */

    /**
     * Valid only if .version is > 0. See above.
     * Get the AAIP string with encoded ACL and xattr.
     * (Not to be confused with ECMA-119 Extended Attributes).
     *
     * bit1 and bit2 of flag should be implemented so that freshly fetched
     * info does not include the undesired ACL or xattr. Nevertheless if the
     * aa_string is cached, then it is permissible that ACL and xattr are still
     * delivered.
     *
     * @param flag       Bitfield for control purposes
     *                   bit0= Transfer ownership of AAIP string data.
     *                         src will free the eventual cached data and might
     *                         not be able to produce it again.
     *                   bit1= No need to get ACL (no guarantee of exclusion)
     *                   bit2= No need to get xattr (no guarantee of exclusion)
     * @param aa_string  Returns a pointer to the AAIP string data. If no AAIP
     *                   string is available, *aa_string becomes NULL.
     *                   (See doc/susp_aaip_*_*.txt for the meaning of AAIP and
     *                    libisofs/aaip_0_2.h for encoding and decoding.)
     *                   The caller is responsible for finally calling free()
     *                   on non-NULL results.
     * @return           1 means success (*aa_string == NULL is possible)
     *                  <0 means failure and must b a valid libisofs error code
     *                     (e.g. ISO_FILE_ERROR if no better one can be found).
     * @since 0.6.14
     */
    int (*get_aa_string)(IsoFileSource *src,
                                     unsigned char **aa_string, int flag);

    /**
     * Produce a copy of a source. It must be possible to operate both source
     * objects concurrently.
     * 
     * @param old_src
     *     The existing source object to be copied
     * @param new_stream
     *     Will return a pointer to the copy
     * @param flag
     *     Bitfield for control purposes. Submit 0 for now.
     *     The function shall return ISO_STREAM_NO_CLONE on unknown flag bits.
     *
     * @since 1.0.2
     * Present if .version is 2 or higher.
     */
    int (*clone_src)(IsoFileSource *old_src, IsoFileSource **new_src, 
                     int flag);

    /*
     * TODO #00004 Add a get_mime_type() function.
     * This can be useful for GUI apps, to choose the icon of the file
     */
};

#ifndef __cplusplus
#ifndef Libisofs_h_as_cpluspluS

/**
 * An IsoFile Source is a POSIX abstraction of a file.
 *
 * @since 0.6.2
 */
struct iso_file_source
{
    const IsoFileSourceIface *class;
    int refcount;
    void *data;
};

#endif /* ! Libisofs_h_as_cpluspluS */
#endif /* ! __cplusplus */


/* A class of IsoStream is implemented by a class description
 *    IsoStreamIface = struct IsoStream_Iface
 * and a structure of data storage for each instance of IsoStream.
 * This structure shall be known to the functions of the IsoStreamIface.
 * To create a custom IsoStream class:
 * - Define the structure of the custom instance data.
 * - Implement the methods which are described by the definition of
 *   struct IsoStream_Iface (see below),
 * - Create a static instance of IsoStreamIface which lists the methods as
 *   C function pointers. (Example in libisofs/stream.c : fsrc_stream_class)
 * To create an instance of that class:
 * - Allocate sizeof(IsoStream) bytes of memory and initialize it as
 *   struct iso_stream :
 *   - Point to the custom IsoStreamIface by member .class .
 *   - Set member .refcount to 1.
 *   - Let member .data point to the custom instance data.
 *
 * Regrettably the choice of the structure member name "class" makes it
 * impossible to implement this generic interface in C++ language directly.
 * If C++ is absolutely necessary then you will have to make own copies
 * of the public API structures. Use other names but take care to maintain
 * the same memory layout.
 */

/**
 * Representation of file contents. It is an stream of bytes, functionally
 * like a pipe.
 *
 * @since 0.6.4
 */
typedef struct iso_stream IsoStream;

/**
 * Interface that defines the operations (methods) available for an
 * IsoStream.
 *
 * @see struct IsoStream_Iface
 * @since 0.6.4
 */
typedef struct IsoStream_Iface IsoStreamIface;

/**
 * Serial number to be used when you can't get a valid id for a Stream by other
 * means. If you use this, both fs_id and dev_id should be set to 0.
 * This must be incremented each time you get a reference to it.
 *
 * @see IsoStreamIface->get_id()
 * @since 0.6.4
 */
extern ino_t serial_id;

/**
 * Interface definition for IsoStream methods. It is public to allow
 * implementation of own stream types.
 * The methods defined here typically make use of stream.data which points
 * to the individual state data of stream instances.
 * 
 * @since 0.6.4
 */

struct IsoStream_Iface
{
    /*
     * Current version of the interface.
     * Version 0 (since 0.6.4)
     *    deprecated but still valid.
     * Version 1 (since 0.6.8) 
     *    update_size() added.
     * Version 2 (since 0.6.18)
     *    get_input_stream() added.
     *    A filter stream must have version 2 at least.
     * Version 3 (since 0.6.20)
     *    compare() added.
     *    A filter stream should have version 3 at least.
     * Version 4 (since 1.0.2)
     *    clone_stream() added.
     */
    int version;

    /**
     * Type of Stream.
     * "fsrc" -> Read from file source
     * "cout" -> Cut out interval from disk file
     * "mem " -> Read from memory
     * "boot" -> Boot catalog
     * "extf" -> External filter program
     * "ziso" -> zisofs compression
     * "osiz" -> zisofs uncompression
     * "gzip" -> gzip compression
     * "pizg" -> gzip uncompression (gunzip)
     * "user" -> User supplied stream
     */
    char type[4];

    /**
     * Opens the stream.
     *
     * @return
     *     1 on success, 2 file greater than expected, 3 file smaller than
     *     expected, < 0 on error (has to be a valid libisofs error code)
     */
    int (*open)(IsoStream *stream);

    /**
     * Close the Stream.
     * @return
     *     1 on success, < 0 on error (has to be a valid libisofs error code)
     */
    int (*close)(IsoStream *stream);

    /**
     * Get the size (in bytes) of the stream. This function should always
     * return the same size, even if the underlying source size changes,
     * unless you call update_size() method.
     */
    off_t (*get_size)(IsoStream *stream);

    /**
     * Attempt to read up to count bytes from the given stream into
     * the buffer starting at buf. The implementation has to make sure that
     * either the full desired count of bytes is delivered or that the
     * next call to this function will return EOF or error.
     * I.e. only the last read block may be shorter than parameter count.
     *
     * The stream must be open() before calling this, and close() when no
     * more needed.
     *
     * @return
     *     number of bytes read, 0 if EOF, < 0 on error (has to be a valid
     *     libisofs error code)
     */
    int (*read)(IsoStream *stream, void *buf, size_t count);

    /**
     * Tell whether this IsoStream can be read several times, with the same
     * results. For example, a regular file is repeatable, you can read it
     * as many times as you want. However, a pipe is not.
     *
     * @return
     *     1 if stream is repeatable, 0 if not,
     *     < 0 on error (has to be a valid libisofs error code)
     */
    int (*is_repeatable)(IsoStream *stream);

    /**
     * Get an unique identifier for the IsoStream.
     */
    void (*get_id)(IsoStream *stream, unsigned int *fs_id, dev_t *dev_id,
                  ino_t *ino_id);

    /**
     * Free implementation specific data. Should never be called by user.
     * Use iso_stream_unref() instead.
     */
    void (*free)(IsoStream *stream);

    /**
     * Update the size of the IsoStream with the current size of the underlying
     * source, if the source is prone to size changes. After calling this,
     * get_size() shall eventually return the new size.
     * This will never be called after iso_image_create_burn_source() was
     * called and before the image was completely written.
     * (The API call to update the size of all files in the image is
     *  iso_image_update_sizes()).
     *
     * @return
     *     1 if ok, < 0 on error (has to be a valid libisofs error code)
     *
     * @since 0.6.8
     * Present if .version is 1 or higher.
     */
    int (*update_size)(IsoStream *stream);

    /**
     * Retrieve the eventual input stream of a filter stream.
     *
     * @param stream
     *     The eventual filter stream to be inquired.
     * @param flag
     *     Bitfield for control purposes. 0 means normal behavior.
     * @return
     *     The input stream, if one exists. Elsewise NULL.
     *     No extra reference to the stream shall be taken by this call.
     *
     * @since 0.6.18
     * Present if .version is 2 or higher.
     */
    IsoStream *(*get_input_stream)(IsoStream *stream, int flag);

    /**
     * Compare two streams whether they are based on the same input and will
     * produce the same output. If in any doubt, then this comparison should
     * indicate no match. A match might allow hardlinking of IsoFile objects.
     *
     * If this function cannot accept one of the given stream types, then
     * the decision must be delegated to
     *    iso_stream_cmp_ino(s1, s2, 1);
     * This is also appropriate if one has reason to implement stream.cmp_ino()
     * without having an own special comparison algorithm.
     *
     * With filter streams, the decision whether the underlying chains of
     * streams match, should be delegated to
     *    iso_stream_cmp_ino(iso_stream_get_input_stream(s1, 0),
     *                       iso_stream_get_input_stream(s2, 0), 0);
     *
     * The stream.cmp_ino() function has to establish an equivalence and order
     * relation: 
     *   cmp_ino(A,A) == 0
     *   cmp_ino(A,B) == -cmp_ino(B,A) 
     *   if cmp_ino(A,B) == 0 && cmp_ino(B,C) == 0 then cmp_ino(A,C) == 0
     *   if cmp_ino(A,B) < 0 && cmp_ino(B,C) < 0 then cmp_ino(A,C) < 0
     *
     * A big hazard to the last constraint are tests which do not apply to some 
     * types of streams.Thus it is mandatory to let iso_stream_cmp_ino(s1,s2,1)
     * decide in this case.
     *
     * A function s1.(*cmp_ino)() must only accept stream s2 if function
     * s2.(*cmp_ino)() would accept s1. Best is to accept only the own stream
     * type or to have the same function for a family of similar stream types.
     *
     * @param s1
     *     The first stream to compare. Expect foreign stream types.
     * @param s2
     *     The second stream to compare. Expect foreign stream types.
     * @return
     *     -1 if s1 is smaller s2 , 0 if s1 matches s2 , 1 if s1 is larger s2
     *
     * @since 0.6.20
     * Present if .version is 3 or higher.
     */
    int (*cmp_ino)(IsoStream *s1, IsoStream *s2);

    /**
     * Produce a copy of a stream. It must be possible to operate both stream
     * objects concurrently.
     * 
     * @param old_stream
     *     The existing stream object to be copied
     * @param new_stream
     *     Will return a pointer to the copy
     * @param flag
     *     Bitfield for control purposes. 0 means normal behavior.
     *     The function shall return ISO_STREAM_NO_CLONE on unknown flag bits.
     * @return
     *     1 in case of success, or an error code < 0
     *
     * @since 1.0.2
     * Present if .version is 4 or higher.
     */
    int (*clone_stream)(IsoStream *old_stream, IsoStream **new_stream,
                        int flag);

};

#ifndef __cplusplus
#ifndef Libisofs_h_as_cpluspluS

/**
 * Representation of file contents as a stream of bytes.
 *
 * @since 0.6.4
 */
struct iso_stream
{
    IsoStreamIface *class;
    int refcount;
    void *data;
};

#endif /* ! Libisofs_h_as_cpluspluS */
#endif /* ! __cplusplus */


/**
 * Initialize libisofs. Before any usage of the library you must either call
 * this function or iso_init_with_flag().
 * Only exception from this rule: iso_lib_version(), iso_lib_is_compatible().
 * @return 1 on success, < 0 on error
 *
 * @since 0.6.2
 */
int iso_init();

/**
 * Initialize libisofs. Before any usage of the library you must either call
 * this function or iso_init() which is equivalent to iso_init_with_flag(0).
 * Only exception from this rule: iso_lib_version(), iso_lib_is_compatible().
 * @param flag
 *      Bitfield for control purposes
 *      bit0= do not set up locale by LC_* environment variables
 * @return 1 on success, < 0 on error
 *
 * @since 0.6.18
 */
int iso_init_with_flag(int flag);

/**
 * Finalize libisofs.
 *
 * @since 0.6.2
 */
void iso_finish();

/**
 * Override the reply of libc function nl_langinfo(CODESET) which may or may
 * not give the name of the character set which is in effect for your
 * environment. So this call can compensate for inconsistent terminal setups.
 * Another use case is to choose UTF-8 as intermediate character set for a
 * conversion from an exotic input character set to an exotic output set.
 *
 * @param name
 *     Name of the character set to be assumed as "local" one.
 * @param flag
 *     Unused yet. Submit 0.
 * @return
 *     1 indicates success, <=0 failure
 *
 * @since 0.6.12
 */
int iso_set_local_charset(char *name, int flag);

/**
 * Obtain the local charset as currently assumed by libisofs.
 * The result points to internal memory. It is volatile and must not be
 * altered.
 *
 * @param flag
 *     Unused yet. Submit 0.
 *
 * @since 0.6.12
 */
char *iso_get_local_charset(int flag);

/**
 * Create a new image, empty.
 *
 * The image will be owned by you and should be unref() when no more needed.
 *
 * @param name
 *     Name of the image. This will be used as volset_id and volume_id.
 * @param image
 *     Location where the image pointer will be stored.
 * @return
 *     1 sucess, < 0 error
 *
 * @since 0.6.2
 */
int iso_image_new(const char *name, IsoImage **image);


/**
 * Control whether ACL and xattr will be imported from external filesystems
 * (typically the local POSIX filesystem) when new nodes get inserted. If
 * enabled by iso_write_opts_set_aaip() they will later be written into the
 * image as AAIP extension fields.
 *
 * A change of this setting does neither affect existing IsoNode objects
 * nor the way how ACL and xattr are handled when loading an ISO image.
 * The latter is controlled by iso_read_opts_set_no_aaip().
 *
 * @param image
 *     The image of which the behavior is to be controlled
 * @param what
 *     A bit field which sets the behavior:
 *     bit0= ignore ACLs if the external file object bears some
 *     bit1= ignore xattr if the external file object bears some
 *     all other bits are reserved
 *
 * @since 0.6.14
 */
void iso_image_set_ignore_aclea(IsoImage *image, int what);


/**
 * Creates an IsoWriteOpts for writing an image. You should set the options
 * desired with the correspondent setters.
 *
 * Options by default are determined by the selected profile. Fifo size is set
 * by default to 2 MB.
 *
 * @param opts
 *     Pointer to the location where the newly created IsoWriteOpts will be
 *     stored. You should free it with iso_write_opts_free() when no more
 *     needed.
 * @param profile
 *     Default profile for image creation. For now the following values are
 *     defined:
 *     ---> 0 [BASIC]
 *        No extensions are enabled, and ISO level is set to 1. Only suitable
 *        for usage for very old and limited systems (like MS-DOS), or by a
 *        start point from which to set your custom options.
 *     ---> 1 [BACKUP]
 *        POSIX compatibility for backup. Simple settings, ISO level is set to
 *        3 and RR extensions are enabled. Useful for backup purposes.
 *        Note that ACL and xattr are not enabled by default.
 *        If you enable them, expect them not to show up in the mounted image.
 *        They will have to be retrieved by libisofs applications like xorriso.
 *     ---> 2 [DISTRIBUTION]
 *        Setting for information distribution. Both RR and Joliet are enabled
 *        to maximize compatibility with most systems. Permissions are set to
 *        default values, and timestamps to the time of recording.
 * @return
 *      1 success, < 0 error
 *
 * @since 0.6.2
 */
int iso_write_opts_new(IsoWriteOpts **opts, int profile);

/**
 * Free an IsoWriteOpts previously allocated with iso_write_opts_new().
 *
 * @since 0.6.2
 */
void iso_write_opts_free(IsoWriteOpts *opts);

/**
 * Announce that only the image size is desired, that the struct burn_source
 * which is set to consume the image output stream will stay inactive,
 * and that the write thread will be cancelled anyway by the .cancel() method
 * of the struct burn_source.
 * This avoids to create a write thread which would begin production of the
 * image stream and would generate a MISHAP event when burn_source.cancel()
 * gets into effect.
 * 
 * @param opts
 *      The option set to be manipulated.
 * @param will_cancel
 *      0= normal image generation
 *      1= prepare for being canceled before image stream output is completed
 * @return
 *      1 success, < 0 error
 *
 * @since 0.6.40
 */
int iso_write_opts_set_will_cancel(IsoWriteOpts *opts, int will_cancel);

/**
 * Set the ISO-9960 level to write at.
 *
 * @param opts
 *      The option set to be manipulated.
 * @param level
 *      -> 1 for higher compatibility with old systems. With this level
 *      filenames are restricted to 8.3 characters.
 *      -> 2 to allow up to 31 filename characters.
 *      -> 3 to allow files greater than 4GB
 * @return
 *      1 success, < 0 error
 *
 * @since 0.6.2
 */
int iso_write_opts_set_iso_level(IsoWriteOpts *opts, int level);

/**
 * Whether to use or not Rock Ridge extensions.
 *
 * This are standard extensions to ECMA-119, intended to add POSIX filesystem
 * features to ECMA-119 images. Thus, usage of this flag is highly recommended
 * for images used on GNU/Linux systems. With the usage of RR extension, the
 * resulting image will have long filenames (up to 255 characters), deeper
 * directory structure, POSIX permissions and owner info on files and
 * directories, support for symbolic links or special files... All that
 * attributes can be modified/setted with the appropiate function.
 *
 * @param opts
 *      The option set to be manipulated.
 * @param enable
 *      1 to enable RR extension, 0 to not add them
 * @return
 *      1 success, < 0 error
 *
 * @since 0.6.2
 */
int iso_write_opts_set_rockridge(IsoWriteOpts *opts, int enable);

/**
 * Whether to add the non-standard Joliet extension to the image.
 *
 * This extensions are heavily used in Microsoft Windows systems, so if you
 * plan to use your disc on such a system you should add this extension.
 * Usage of Joliet supplies longer filesystem length (up to 64 unicode
 * characters), and deeper directory structure.
 *
 * @param opts
 *      The option set to be manipulated.
 * @param enable
 *      1 to enable Joliet extension, 0 to not add them
 * @return
 *      1 success, < 0 error
 *
 * @since 0.6.2
 */
int iso_write_opts_set_joliet(IsoWriteOpts *opts, int enable);

/**
 * Whether to add a HFS+ filesystem to the image which points to the same
 * file content as the other directory trees.
 * It will get marked by an Apple Partition Map in the System Area of the ISO
 * image. This may collide with data submitted by
 *   iso_write_opts_set_system_area()
 * and with settings made by 
 *   el_torito_set_isolinux_options()
 * The first 8 bytes of the System Area get overwritten by
 *   {0x45, 0x52, 0x08 0x00, 0xeb, 0x02, 0xff, 0xff}
 * which can be executed as x86 machine code without negative effects.
 * So if an MBR gets combined with this feature, then its first 8 bytes
 * should contain no essential commands.
 * The next blocks of 2 KiB in the System Area will be occupied by APM entries.
 * The first one covers the part of the ISO image before the HFS+ filesystem
 * metadata. The second one marks the range from HFS+ metadata to the end
 * of file content data. If more ISO image data follow, then a third partition
 * entry gets produced. Other features of libisofs might cause the need for
 * more APM entries.
 *
 * @param opts
 *      The option set to be manipulated.
 * @param enable
 *      1 to enable HFS+ extension, 0 to not add HFS+ metadata and APM
 * @return
 *      1 success, < 0 error
 *
 * @since 1.2.4
 */
int iso_write_opts_set_hfsplus(IsoWriteOpts *opts, int enable);

/**
 * >>> Production of FAT32 is not implemented yet.
 * >>> This call exists only as preparation for implementation.
 *
 * Whether to add a FAT32 filesystem to the image which points to the same
 * file content as the other directory trees.
 *
 * >>> FAT32 is planned to get implemented in co-existence with HFS+
 * >>> Describe impact on MBR
 *
 * @param opts
 *      The option set to be manipulated.
 * @param enable
 *      1 to enable FAT32 extension, 0 to not add FAT metadata
 * @return
 *      1 success, < 0 error
 *
 * @since 1.2.4
 */
int iso_write_opts_set_fat(IsoWriteOpts *opts, int enable);

/**
 * Supply a serial number for the HFS+ extension of the emerging image.
 *
 * @param opts
 *      The option set to be manipulated.
 * @param serial_number
 *      8 bytes which should be unique to the image.
 *      If all bytes are 0, then the serial number will be generated as
 *      random number by libisofs. This is the default setting.
 * @return
 *      1 success, < 0 error
 *
 * @since 1.2.4
 */
int iso_write_opts_set_hfsp_serial_number(IsoWriteOpts *opts,
                                          uint8_t serial_number[8]);

/**
 * Set the block size for Apple Partition Map and for HFS+.
 *
 * @param opts
 *      The option set to be manipulated.
 * @param hfsp_block_size
 *      The allocation block size to be used by the HFS+ fileystem.
 *      0, 512, or 2048
 * @param hfsp_block_size
 *      The block size to be used for and within the Apple Partition Map.
 *      0, 512, or 2048.
 *      Size 512 is not compatible with options which produce GPT.
 * @return
 *      1 success, < 0 error
 *
 * @since 1.2.4
 */
int iso_write_opts_set_hfsp_block_size(IsoWriteOpts *opts,
                                     int hfsp_block_size, int apm_block_size);


/**
 * Whether to use newer ISO-9660:1999 version.
 *
 * This is the second version of ISO-9660. It allows longer filenames and has
 * less restrictions than old ISO-9660. However, nobody is using it so there
 * are no much reasons to enable this.
 *
 * @since 0.6.2
 */
int iso_write_opts_set_iso1999(IsoWriteOpts *opts, int enable);

/**
 * Control generation of non-unique inode numbers for the emerging image.
 * Inode numbers get written as "file serial number" with PX entries as of
 * RRIP-1.12. They may mark families of hardlinks.
 * RRIP-1.10 prescribes a PX entry without file serial number. If not overriden
 * by iso_write_opts_set_rrip_1_10_px_ino() there will be no file serial number
 * written into RRIP-1.10 images.
 *
 * Inode number generation does not affect IsoNode objects which imported their
 * inode numbers from the old ISO image (see iso_read_opts_set_new_inos())
 * and which have not been altered since import. It rather applies to IsoNode
 * objects which were newly added to the image, or to IsoNode which brought no
 * inode number from the old image, or to IsoNode where certain properties 
 * have been altered since image import.
 *
 * If two IsoNode are found with same imported inode number but differing
 * properties, then one of them will get assigned a new unique inode number.
 * I.e. the hardlink relation between both IsoNode objects ends.
 *
 * @param opts
 *      The option set to be manipulated.
 * @param enable 
 *      1 = Collect IsoNode objects which have identical data sources and
 *          properties.
 *      0 = Generate unique inode numbers for all IsoNode objects which do not
 *          have a valid inode number from an imported ISO image.
 *      All other values are reserved.
 *
 * @since 0.6.20
 */
int iso_write_opts_set_hardlinks(IsoWriteOpts *opts, int enable);

/**
 * Control writing of AAIP informations for ACL and xattr.
 * For importing ACL and xattr when inserting nodes from external filesystems
 * (e.g. the local POSIX filesystem) see iso_image_set_ignore_aclea().
 * For loading of this information from images see iso_read_opts_set_no_aaip().
 *
 * @param opts
 *      The option set to be manipulated.
 * @param enable
 *      1 = write AAIP information from nodes into the image
 *      0 = do not write AAIP information into the image
 *      All other values are reserved.
 *
 * @since 0.6.14
 */
int iso_write_opts_set_aaip(IsoWriteOpts *opts, int enable);

/**
 * Use this only if you need to reproduce a suboptimal behavior of older
 * versions of libisofs. They used address 0 for links and device files,
 * and the address of the Volume Descriptor Set Terminator for empty data
 * files.
 * New versions let symbolic links, device files, and empty data files point
 * to a dedicated block of zero-bytes after the end of the directory trees.
 * (Single-pass reader libarchive needs to see all directory info before
 *  processing any data files.)
 *
 * @param opts
 *      The option set to be manipulated.
 * @param enable
 *      1 = use the suboptimal block addresses in the range of 0 to 115.
 *      0 = use the address of a block after the directory tree. (Default)
 *
 * @since 1.0.2
 */
int iso_write_opts_set_old_empty(IsoWriteOpts *opts, int enable);

/**
 * Caution: This option breaks any assumptions about names that
 *          are supported by ECMA-119 specifications. 
 * Try to omit any translation which would make a file name compliant to the
 * ECMA-119 rules. This includes and exceeds omit_version_numbers,
 * max_37_char_filenames, no_force_dots bit0, allow_full_ascii. Further it
 * prevents the conversion from local character set to ASCII.
 * The maximum name length is given by this call. If a filename exceeds
 * this length or cannot be recorded untranslated for other reasons, then
 * image production is aborted with ISO_NAME_NEEDS_TRANSL.
 * Currently the length limit is 96 characters, because an ECMA-119 directory
 * record may at most have 254 bytes and up to 158 other bytes must fit into
 * the record. Probably 96 more bytes can be made free for the name in future.
 * @param opts
 *      The option set to be manipulated.
 * @param len
 *      0 = disable this feature and perform name translation according to
 *          other settings.
 *     >0 = Omit any translation. Eventually abort image production
 *          if a name is longer than the given value.
 *     -1 = Like >0. Allow maximum possible length (currently 96)
 * @return >=0 success, <0 failure
 *         In case of >=0 the return value tells the effectively set len.
 *         E.g. 96 after using len == -1.
 * @since 1.0.0
 */
int iso_write_opts_set_untranslated_name_len(IsoWriteOpts *opts, int len);

/**
 * Convert directory names for ECMA-119 similar to other file names, but do
 * not force a dot or add a version number.
 * This violates ECMA-119 by allowing one "." and especially ISO level 1 
 * by allowing DOS style 8.3 names rather than only 8 characters.
 * (mkisofs and its clones seem to do this violation.)
 * @param opts
 *      The option set to be manipulated.
 * @param allow
 *      1= allow dots , 0= disallow dots and convert them
 * @return
 *      1 success, < 0 error
 * @since 1.0.0
 */
int iso_write_opts_set_allow_dir_id_ext(IsoWriteOpts *opts, int allow);

/**
 * Omit the version number (";1") at the end of the ISO-9660 identifiers.
 * This breaks ECMA-119 specification, but version numbers are usually not
 * used, so it should work on most systems. Use with caution.
 * @param opts
 *      The option set to be manipulated.
 * @param omit
 *      bit0= omit version number with ECMA-119 and Joliet
 *      bit1= omit version number with Joliet alone (@since 0.6.30)
 * @since 0.6.2
 */
int iso_write_opts_set_omit_version_numbers(IsoWriteOpts *opts, int omit);

/**
 * Allow ISO-9660 directory hierarchy to be deeper than 8 levels.
 * This breaks ECMA-119 specification. Use with caution.
 *
 * @since 0.6.2
 */
int iso_write_opts_set_allow_deep_paths(IsoWriteOpts *opts, int allow);

/**
 * This call describes the directory where to store Rock Ridge relocated
 * directories.
 * If not iso_write_opts_set_allow_deep_paths(,1) is in effect, then it may
 * become necessary to relocate directories so that no ECMA-119 file path
 * has more than 8 components. These directories are grafted into either
 * the root directory of the ISO image or into a dedicated relocation
 * directory.
 * For Rock Ridge, the relocated directories are linked forth and back to
 * placeholders at their original positions in path level 8. Directories
 * marked by Rock Ridge entry RE are to be considered artefacts of relocation
 * and shall not be read into a Rock Ridge tree. Instead they are to be read
 * via their placeholders and their links.
 * For plain ECMA-119, the relocation directory and the relocated directories
 * are just normal directories which contain normal files and directories.
 * @param opts
 *      The option set to be manipulated.
 * @param name
 *      The name of the relocation directory in the root directory. Do not
 *      prepend "/". An empty name or NULL will direct relocated directories
 *      into the root directory. This is the default.
 *      If the given name does not exist in the root directory when
 *      iso_image_create_burn_source() is called, and if there are directories
 *      at path level 8, then directory /name will be created automatically.
 *      The name given by this call will be compared with iso_node_get_name()
 *      of the directories in the root directory, not with the final ECMA-119
 *      names of those directories.
 * @parm flags
 *      Bitfield for control purposes.
 *      bit0= Mark the relocation directory by a Rock Ridge RE entry, if it
 *            gets created during iso_image_create_burn_source(). This will
 *            make it invisible for most Rock Ridge readers.
 *      bit1= not settable via API (used internally)
 * @return
 *      1 success, < 0 error
 * @since 1.2.2
*/
int iso_write_opts_set_rr_reloc(IsoWriteOpts *opts, char *name, int flags);

/**
 * Allow path in the ISO-9660 tree to have more than 255 characters.
 * This breaks ECMA-119 specification. Use with caution.
 *
 * @since 0.6.2
 */
int iso_write_opts_set_allow_longer_paths(IsoWriteOpts *opts, int allow);

/**
 * Allow a single file or directory identifier to have up to 37 characters.
 * This is larger than the 31 characters allowed by ISO level 2, and the
 * extra space is taken from the version number, so this also forces
 * omit_version_numbers.
 * This breaks ECMA-119 specification and could lead to buffer overflow
 * problems on old systems. Use with caution.
 *
 * @since 0.6.2
 */
int iso_write_opts_set_max_37_char_filenames(IsoWriteOpts *opts, int allow);

/**
 * ISO-9660 forces filenames to have a ".", that separates file name from
 * extension. libisofs adds it if original filename doesn't has one. Set
 * this to 1 to prevent this behavior.
 * This breaks ECMA-119 specification. Use with caution.
 *
 * @param opts
 *      The option set to be manipulated.
 * @param no
 *      bit0= no forced dot with ECMA-119
 *      bit1= no forced dot with Joliet (@since 0.6.30)
 *
 * @since 0.6.2
 */
int iso_write_opts_set_no_force_dots(IsoWriteOpts *opts, int no);

/**
 * Allow lowercase characters in ISO-9660 filenames. By default, only
 * uppercase characters, numbers and a few other characters are allowed.
 * This breaks ECMA-119 specification. Use with caution.
 * If lowercase is not allowed then those letters get mapped to uppercase
 * letters.
 *
 * @since 0.6.2
 */
int iso_write_opts_set_allow_lowercase(IsoWriteOpts *opts, int allow);

/**
 * Allow all 8-bit characters to appear on an ISO-9660 filename. Note
 * that "/" and 0x0 characters are never allowed, even in RR names.
 * This breaks ECMA-119 specification. Use with caution.
 *
 * @since 0.6.2
 */
int iso_write_opts_set_allow_full_ascii(IsoWriteOpts *opts, int allow);

/**
 * If not iso_write_opts_set_allow_full_ascii() is set to 1:
 * Allow all 7-bit characters that would be allowed by allow_full_ascii, but
 * map lowercase to uppercase if iso_write_opts_set_allow_lowercase()
 * is not set to 1.
 * @param opts    
 *      The option set to be manipulated.
 * @param allow
 *      If not zero, then allow what is described above.
 *
 * @since 1.2.2
 */
int iso_write_opts_set_allow_7bit_ascii(IsoWriteOpts *opts, int allow);

/**
 * Allow all characters to be part of Volume and Volset identifiers on
 * the Primary Volume Descriptor. This breaks ISO-9660 contraints, but
 * should work on modern systems.
 *
 * @since 0.6.2
 */
int iso_write_opts_set_relaxed_vol_atts(IsoWriteOpts *opts, int allow);

/**
 * Allow paths in the Joliet tree to have more than 240 characters.
 * This breaks Joliet specification. Use with caution.
 *
 * @since 0.6.2
 */
int iso_write_opts_set_joliet_longer_paths(IsoWriteOpts *opts, int allow);

/**
 * Allow leaf names in the Joliet tree to have up to 103 characters.
 * Normal limit is 64. 
 * This breaks Joliet specification. Use with caution.
 *
 * @since 1.0.6
 */
int iso_write_opts_set_joliet_long_names(IsoWriteOpts *opts, int allow);

/**
 * Write Rock Ridge info as of specification RRIP-1.10 rather than RRIP-1.12:
 * signature "RRIP_1991A" rather than "IEEE_1282", field PX without file
 * serial number.
 *
 * @since 0.6.12
 */
int iso_write_opts_set_rrip_version_1_10(IsoWriteOpts *opts, int oldvers);

/**
 * Write field PX with file serial number (i.e. inode number) even if
 * iso_write_opts_set_rrip_version_1_10(,1) is in effect.
 * This clearly violates the RRIP-1.10 specs. But it is done by mkisofs since
 * a while and no widespread protest is visible in the web.
 * If this option is not enabled, then iso_write_opts_set_hardlinks() will
 * only have an effect with iso_write_opts_set_rrip_version_1_10(,0).
 * 
 * @since 0.6.20
 */
int iso_write_opts_set_rrip_1_10_px_ino(IsoWriteOpts *opts, int enable);

/**
 * Write AAIP as extension according to SUSP 1.10 rather than SUSP 1.12.
 * I.e. without announcing it by an ER field and thus without the need
 * to preceed the RRIP fields and the AAIP field by ES fields.
 * This saves 5 to 10 bytes per file and might avoid problems with readers
 * which dislike ER fields other than the ones for RRIP.
 * On the other hand, SUSP 1.12 frowns on such unannounced extensions 
 * and prescribes ER and ES. It does this since the year 1994.
 *
 * In effect only if above iso_write_opts_set_aaip() enables writing of AAIP.
 *
 * @since 0.6.14
 */
int iso_write_opts_set_aaip_susp_1_10(IsoWriteOpts *opts, int oldvers);

/**
 * Store as ECMA-119 Directory Record timestamp the mtime of the source node
 * rather than the image creation time.
 * If storing of mtime is enabled, then the settings of
 * iso_write_opts_set_replace_timestamps() apply. (replace==1 will revoke,
 * replace==2 will override mtime by iso_write_opts_set_default_timestamp().
 *
 * Since version 1.2.0 this may apply also to Joliet and ISO 9660:1999. To
 * reduce the probability of unwanted behavior changes between pre-1.2.0 and
 * post-1.2.0, the bits for Joliet and ISO 9660:1999 also enable ECMA-119.
 * The hopefully unlikely bit14 may then be used to disable mtime for ECMA-119.
 *
 * To enable mtime for all three directory trees, submit 7.
 * To disable this feature completely, submit 0.
 *
 * @param opts    
 *      The option set to be manipulated.
 * @param allow
 *      If this parameter is negative, then mtime is enabled only for ECMA-119.
 *      With positive numbers, the parameter is interpreted as bit field :
 *          bit0= enable mtime for ECMA-119 
 *          bit1= enable mtime for Joliet and ECMA-119
 *          bit2= enable mtime for ISO 9660:1999 and ECMA-119
 *          bit14= disable mtime for ECMA-119 although some of the other bits
 *                 would enable it
 *          @since 1.2.0
 *      Before version 1.2.0 this applied only to ECMA-119 :
 *          0 stored image creation time in ECMA-119 tree.
 *          Any other value caused storing of mtime.
 *          Joliet and ISO 9660:1999 always stored the image creation time.
 * @since 0.6.12
 */
int iso_write_opts_set_dir_rec_mtime(IsoWriteOpts *opts, int allow);

/**
 * Whether to sort files based on their weight.
 *
 * @see iso_node_set_sort_weight
 * @since 0.6.2
 */
int iso_write_opts_set_sort_files(IsoWriteOpts *opts, int sort);

/**
 * Whether to compute and record MD5 checksums for the whole session and/or
 * for each single IsoFile object. The checksums represent the data as they
 * were written into the image output stream, not necessarily as they were
 * on hard disk at any point of time.
 * See also calls iso_image_get_session_md5() and iso_file_get_md5().
 * @param opts
 *      The option set to be manipulated.
 * @param session
 *      If bit0 set: Compute session checksum
 * @param files
 *      If bit0 set: Compute a checksum for each single IsoFile object which
 *                   gets its data content written into the session. Copy
 *                   checksums from files which keep their data in older
 *                   sessions.
 *      If bit1 set: Check content stability (only with bit0). I.e.  before
 *                   writing the file content into to image stream, read it
 *                   once and compute a MD5. Do a second reading for writing
 *                   into the image stream. Afterwards compare both MD5 and
 *                   issue a MISHAP event ISO_MD5_STREAM_CHANGE if they do not
 *                   match.
 *                   Such a mismatch indicates content changes between the
 *                   time point when the first MD5 reading started and the
 *                   time point when the last block was read for writing.
 *                   So there is high risk that the image stream was fed from
 *                   changing and possibly inconsistent file content.
 *                   
 * @since 0.6.22
 */
int iso_write_opts_set_record_md5(IsoWriteOpts *opts, int session, int files);

/**
 * Set the parameters "name" and "timestamp" for a scdbackup checksum tag.
 * It will be appended to the libisofs session tag if the image starts at
 * LBA 0 (see iso_write_opts_set_ms_block()). The scdbackup tag can be used
 * to verify the image by command scdbackup_verify device -auto_end.
 * See scdbackup/README appendix VERIFY for its inner details.
 *
 * @param opts
 *      The option set to be manipulated.
 * @param name
 *      A word of up to 80 characters. Typically volno_totalno telling
 *      that this is volume volno of a total of totalno volumes.
 * @param timestamp
 *      A string of 13 characters YYMMDD.hhmmss (e.g. A90831.190324).
 *      A9 = 2009, B0 = 2010, B1 = 2011, ... C0 = 2020, ...
 * @param tag_written
 *      Either NULL or the address of an array with at least 512 characters.
 *      In the latter case the eventually produced scdbackup tag will be
 *      copied to this array when the image gets written. This call sets
 *      scdbackup_tag_written[0] = 0 to mark its preliminary invalidity.
 * @return
 *      1 indicates success, <0 is error
 *
 * @since 0.6.24
 */
int iso_write_opts_set_scdbackup_tag(IsoWriteOpts *opts,
                                     char *name, char *timestamp,
                                     char *tag_written);

/**
 * Whether to set default values for files and directory permissions, gid and
 * uid. All these take one of three values: 0, 1 or 2.
 *
 * If 0, the corresponding attribute will be kept as set in the IsoNode.
 * Unless you have changed it, it corresponds to the value on disc, so it
 * is suitable for backup purposes. If set to 1, the corresponding attrib.
 * will be changed by a default suitable value. Finally, if you set it to
 * 2, the attrib. will be changed with the value specified by the functioins
 * below. Note that for mode attributes, only the permissions are set, the
 * file type remains unchanged.
 *
 * @see iso_write_opts_set_default_dir_mode
 * @see iso_write_opts_set_default_file_mode
 * @see iso_write_opts_set_default_uid
 * @see iso_write_opts_set_default_gid
 * @since 0.6.2
 */
int iso_write_opts_set_replace_mode(IsoWriteOpts *opts, int dir_mode,
                                    int file_mode, int uid, int gid);

/**
 * Set the mode to use on dirs when you set the replace_mode of dirs to 2.
 *
 * @see iso_write_opts_set_replace_mode
 * @since 0.6.2
 */
int iso_write_opts_set_default_dir_mode(IsoWriteOpts *opts, mode_t dir_mode);

/**
 * Set the mode to use on files when you set the replace_mode of files to 2.
 *
 * @see iso_write_opts_set_replace_mode
 * @since 0.6.2
 */
int iso_write_opts_set_default_file_mode(IsoWriteOpts *opts, mode_t file_mode);

/**
 * Set the uid to use when you set the replace_uid to 2.
 *
 * @see iso_write_opts_set_replace_mode
 * @since 0.6.2
 */
int iso_write_opts_set_default_uid(IsoWriteOpts *opts, uid_t uid);

/**
 * Set the gid to use when you set the replace_gid to 2.
 *
 * @see iso_write_opts_set_replace_mode
 * @since 0.6.2
 */
int iso_write_opts_set_default_gid(IsoWriteOpts *opts, gid_t gid);

/**
 * 0 to use IsoNode timestamps, 1 to use recording time, 2 to use
 * values from timestamp field. This applies to the timestamps of Rock Ridge
 * and if the use of mtime is enabled by iso_write_opts_set_dir_rec_mtime().
 * In the latter case, value 1 will revoke the recording of mtime, value
 * 2 will override mtime by iso_write_opts_set_default_timestamp().
 *
 * @see iso_write_opts_set_default_timestamp
 * @since 0.6.2
 */
int iso_write_opts_set_replace_timestamps(IsoWriteOpts *opts, int replace);

/**
 * Set the timestamp to use when you set the replace_timestamps to 2.
 *
 * @see iso_write_opts_set_replace_timestamps
 * @since 0.6.2
 */
int iso_write_opts_set_default_timestamp(IsoWriteOpts *opts, time_t timestamp);

/**
 * Whether to always record timestamps in GMT.
 *
 * By default, libisofs stores local time information on image. You can set
 * this to always store timestamps converted to GMT. This prevents any
 * discrimination of the timezone of the image preparer by the image reader.
 *
 * It is useful if you want to hide your timezone, or you live in a timezone
 * that can't be represented in ECMA-119. These are timezones with an offset
 * from GMT greater than +13 hours, lower than -12 hours, or not a multiple
 * of 15 minutes.
 * Negative timezones (west of GMT) can trigger bugs in some operating systems
 * which typically appear in mounted ISO images as if the timezone shift from
 * GMT was applied twice (e.g. in New York 22:36 becomes 17:36).
 *
 * @since 0.6.2
 */
int iso_write_opts_set_always_gmt(IsoWriteOpts *opts, int gmt);

/**
 * Set the charset to use for the RR names of the files that will be created
 * on the image.
 * NULL to use default charset, that is the locale charset.
 * You can obtain the list of charsets supported on your system executing
 * "iconv -l" in a shell.
 *
 * @since 0.6.2
 */
int iso_write_opts_set_output_charset(IsoWriteOpts *opts, const char *charset);

/**
 * Set the type of image creation in case there was already an existing
 * image imported. Libisofs supports two types of creation:
 * stand-alone and appended.
 *
 * A stand-alone image is an image that does not need the old image any more
 * for being mounted by the operating system or imported by libisofs. It may
 * be written beginning with byte 0 of optical media or disk file objects. 
 * There will be no distinction between files from the old image and those
 * which have been added by the new image generation.
 *
 * On the other side, an appended image is not self contained. It may refer
 * to files that stay stored in the imported existing image.
 * This usage model is inspired by CD multi-session. It demands that the
 * appended image is finally written to the same media resp. disk file
 * as the imported image at an address behind the end of that imported image.
 * The exact address may depend on media peculiarities and thus has to be
 * announced by the application via iso_write_opts_set_ms_block().
 * The real address where the data will be written is under control of the
 * consumer of the struct burn_source which takes the output of libisofs
 * image generation. It may be the one announced to libisofs or an intermediate
 * one. Nevertheless, the image will be readable only at the announced address.
 *
 * If you have not imported a previous image by iso_image_import(), then the
 * image will always be a stand-alone image, as there is no previous data to
 * refer to. 
 *
 * @param opts
 *      The option set to be manipulated.
 * @param append
 *      1 to create an appended image, 0 for an stand-alone one.
 *
 * @since 0.6.2
 */
int iso_write_opts_set_appendable(IsoWriteOpts *opts, int append);

/**
 * Set the start block of the image. It is supposed to be the lba where the
 * first block of the image will be written on disc. All references inside the
 * ISO image will take this into account, thus providing a mountable image.
 *
 * For appendable images, that are written to a new session, you should
 * pass here the lba of the next writable address on disc.
 *
 * In stand alone images this is usually 0. However, you may want to
 * provide a different ms_block if you don't plan to burn the image in the
 * first session on disc, such as in some CD-Extra disc whether the data
 * image is written in a new session after some audio tracks.
 *
 * @since 0.6.2
 */
int iso_write_opts_set_ms_block(IsoWriteOpts *opts, uint32_t ms_block);

/**
 * Sets the buffer where to store the descriptors which shall be written
 * at the beginning of an overwriteable media to point to the newly written
 * image.
 * This is needed if the write start address of the image is not 0.
 * In this case the first 64 KiB of the media have to be overwritten
 * by the buffer content after the session was written and the buffer
 * was updated by libisofs. Otherwise the new session would not be
 * found by operating system function mount() or by libisoburn.
 * (One could still mount that session if its start address is known.)
 *
 * If you do not need this information, for example because you are creating a
 * new image for LBA 0 or because you will create an image for a true
 * multisession media, just do not use this call or set buffer to NULL.
 *
 * Use cases:
 *
 * - Together with iso_write_opts_set_appendable(opts, 1) the buffer serves
 *   for the growing of an image as done in growisofs by Andy Polyakov.
 *   This allows appending of a new session to non-multisession media, such
 *   as DVD+RW. The new session will refer to the data of previous sessions
 *   on the same media.
 *   libisoburn emulates multisession appendability on overwriteable media
 *   and disk files by performing this use case.
 *
 * - Together with iso_write_opts_set_appendable(opts, 0) the buffer allows
 *   to write the first session on overwriteable media to start addresses
 *   other than 0.
 *   This address must not be smaller than 32 blocks plus the eventual
 *   partition offset as defined by iso_write_opts_set_part_offset().
 *   libisoburn in most cases writes the first session on overwriteable media
 *   and disk files to LBA (32 + partition_offset) in order to preserve its
 *   descriptors from the subsequent overwriting by the descriptor buffer of
 *   later sessions.
 *
 * @param opts
 *      The option set to be manipulated.
 * @param overwrite
 *      When not NULL, it should point to at least 64KiB of memory, where
 *      libisofs will install the contents that shall be written at the
 *      beginning of overwriteable media.
 *      You should initialize the buffer either with 0s, or with the contents
 *      of the first 32 blocks of the image you are growing. In most cases,
 *      0 is good enought.
 *      IMPORTANT: If you use iso_write_opts_set_part_offset() then the
 *                 overwrite buffer must be larger by the offset defined there.
 *
 * @since 0.6.2
 */
int iso_write_opts_set_overwrite_buf(IsoWriteOpts *opts, uint8_t *overwrite);

/**
 * Set the size, in number of blocks, of the ring buffer used between the
 * writer thread and the burn_source. You have to provide at least a 32
 * blocks buffer. Default value is set to 2MB, if that is ok for you, you
 * don't need to call this function.
 *
 * @since 0.6.2
 */
int iso_write_opts_set_fifo_size(IsoWriteOpts *opts, size_t fifo_size);

/*
 * Attach 32 kB of binary data which shall get written to the first 32 kB 
 * of the ISO image, the ECMA-119 System Area. This space is intended for
 * system dependent boot software, e.g. a Master Boot Record which allows to
 * boot from USB sticks or hard disks. ECMA-119 makes no own assumptions or
 * prescriptions about the byte content.
 *
 * If system area data are given or options bit0 is set, then bit1 of
 * el_torito_set_isolinux_options() is automatically disabled.
 *
 * @param opts
 *      The option set to be manipulated.
 * @param data
 *        Either NULL or 32 kB of data. Do not submit less bytes !
 * @param options
 *        Can cause manipulations of submitted data before they get written:
 *        bit0= Only with System area type 0 = MBR
 *              Apply a --protective-msdos-label as of grub-mkisofs.
 *              This means to patch bytes 446 to 512 of the system area so
 *              that one partition is defined which begins at the second
 *              512-byte block of the image and ends where the image ends.
 *              This works with and without system_area_data.
 *        bit1= Only with System area type 0 = MBR
 *              Apply isohybrid MBR patching to the system area.
 *              This works only with system area data from SYSLINUX plus an
 *              ISOLINUX boot image (see iso_image_set_boot_image()) and
 *              only if not bit0 is set.
 *        bit2-7= System area type
 *              0= with bit0 or bit1: MBR
 *                 else: unspecified type which will be used unaltered.
 *              1= MIPS Big Endian Volume Header
 *                 @since 0.6.38
 *                 Submit up to 15 MIPS Big Endian boot files by
 *                 iso_image_add_mips_boot_file().
 *                 This will overwrite the first 512 bytes of the submitted
 *                 data.
 *              2= DEC Boot Block for MIPS Little Endian
 *                 @since 0.6.38
 *                 The first boot file submitted by
 *                 iso_image_add_mips_boot_file() will be activated.
 *                 This will overwrite the first 512 bytes of the submitted
 *                 data.
 *              3= SUN Disk Label for SUN SPARC
 *                 @since 0.6.40
 *                 Submit up to 7 SPARC boot images by
 *                 iso_write_opts_set_partition_img() for partition numbers 2
 *                 to 8.
 *                 This will overwrite the first 512 bytes of the submitted
 *                 data.
 *        bit8-9= Only with System area type 0 = MBR
 *              @since 1.0.4
 *              Cylinder alignment mode eventually pads the image to make it
 *              end at a cylinder boundary.
 *                0 = auto (align if bit1)
 *                1 = always align to cylinder boundary
 *                2 = never align to cylinder boundary
 *                3 = always align, additionally pad up and align partitions
 *                    which were appended by iso_write_opts_set_partition_img()
 *                    @since 1.2.6
 *        bit10-13= System area sub type
 *              @since 1.2.4
 *              With type 0 = MBR:
 *                Gets overridden by bit0 and bit1.
 *                0 = no particular sub type
 *                1 = CHRP: A single MBR partition of type 0x96 covers the
 *                          ISO image. Not compatible with any other feature
 *                          which needs to have own MBR partition entries.
 *        bit14= Only with System area type 0 = MBR
 *              GRUB2 boot provisions:
 *              @since 1.3.0
 *              Patch system area at byte 92 to 99 with 512-block address + 1
 *              of the first boot image file. Little-endian 8-byte.
 *              Should be combined with options bit0.
 *              Will not be in effect if options bit1 is set.
 * @param flag
 *        bit0 = invalidate any attached system area data. Same as data == NULL
 *               (This re-activates eventually loaded image System Area data.
 *                To erase those, submit 32 kB of zeros without flag bit0.)
 *        bit1 = keep data unaltered
 *        bit2 = keep options unaltered
 * @return
 *      ISO_SUCCESS or error
 * @since 0.6.30
 */
int iso_write_opts_set_system_area(IsoWriteOpts *opts, char data[32768],
                                   int options, int flag);

/**
 * Set a name for the system area. This setting is ignored unless system area
 * type 3 "SUN Disk Label" is in effect by iso_write_opts_set_system_area().
 * In this case it will replace the default text at the start of the image:
 *   "CD-ROM Disc with Sun sparc boot created by libisofs"
 *
 * @param opts
 *      The option set to be manipulated.
 * @param label
 *      A text of up to 128 characters.
 * @return
 *      ISO_SUCCESS or error
 * @since 0.6.40
*/
int iso_write_opts_set_disc_label(IsoWriteOpts *opts, char *label);

/**
 * Explicitely set the four timestamps of the emerging Primary Volume
 * Descriptor and in the volume descriptors of Joliet and ISO 9660:1999,
 * if those are to be generated.
 * Default with all parameters is 0.
 *
 * ECMA-119 defines them as:
 * @param opts
 *        The option set to be manipulated.
 * @param vol_creation_time
 *        When "the information in the volume was created."
 *        A value of 0 means that the timepoint of write start is to be used.
 * @param vol_modification_time
 *        When "the information in the volume was last modified."
 *        A value of 0 means that the timepoint of write start is to be used.
 * @param vol_expiration_time
 *        When "the information in the volume may be regarded as obsolete."
 *        A value of 0 means that the information never shall expire.
 * @param vol_effective_time
 *        When "the information in the volume may be used."
 *        A value of 0 means that not such retention is intended.
 * @param vol_uuid
 *        If this text is not empty, then it overrides vol_creation_time and
 *        vol_modification_time by copying the first 16 decimal digits from
 *        uuid, eventually padding up with decimal '1', and writing a NUL-byte
 *        as timezone.
 *        Other than with vol_*_time the resulting string in the ISO image
 *        is fully predictable and free of timezone pitfalls.
 *        It should express a reasonable time in form  YYYYMMDDhhmmsscc
 *        E.g.:  "2010040711405800" = 7 Apr 2010 11:40:58 (+0 centiseconds)
 * @return
 *        ISO_SUCCESS or error
 *
 * @since 0.6.30
 */
int iso_write_opts_set_pvd_times(IsoWriteOpts *opts,
                        time_t vol_creation_time, time_t vol_modification_time,
                        time_t vol_expiration_time, time_t vol_effective_time,
                        char *vol_uuid);


/*
 * Control production of a second set of volume descriptors (superblock)
 * and directory trees, together with a partition table in the MBR where the
 * first partition has non-zero start address and the others are zeroed.
 * The first partition stretches to the end of the whole ISO image.
 * The additional volume descriptor set and trees will allow to mount the
 * ISO image at the start of the first partition, while it is still possible
 * to mount it via the normal first volume descriptor set and tree at the
 * start of the image resp. storage device.
 * This makes few sense on optical media. But on USB sticks it creates a
 * conventional partition table which makes it mountable on e.g. Linux via
 * /dev/sdb and /dev/sdb1 alike.
 * IMPORTANT: When submitting memory by iso_write_opts_set_overwrite_buf()
 *            then its size must be at least 64 KiB + partition offset. 
 *
 * @param opts
 *        The option set to be manipulated.
 * @param block_offset_2k
 *        The offset of the partition start relative to device start.
 *        This is counted in 2 kB blocks. The partition table will show the
 *        according number of 512 byte sectors.
 *        Default is 0 which causes no special partition table preparations.
 *        If it is not 0 then it must not be smaller than 16.
 * @param secs_512_per_head
 *        Number of 512 byte sectors per head. 1 to 63. 0=automatic.
 * @param heads_per_cyl
 *        Number of heads per cylinder. 1 to 255. 0=automatic.
 * @return
 *        ISO_SUCCESS or error
 *
 * @since 0.6.36
 */
int iso_write_opts_set_part_offset(IsoWriteOpts *opts,
                                   uint32_t block_offset_2k,
                                   int secs_512_per_head, int heads_per_cyl);


/** The minimum version of libjte to be used with this version of libisofs
    at compile time. The use of libjte is optional and depends on configure
    tests. It can be prevented by ./configure option --disable-libjte .
    @since 0.6.38
*/
#define iso_libjte_req_major 1
#define iso_libjte_req_minor 0
#define iso_libjte_req_micro 0

/** 
 * Associate a libjte environment object to the upcomming write run.
 * libjte implements Jigdo Template Extraction as of Steve McIntyre and
 * Richard Atterer.
 * The call will fail if no libjte support was enabled at compile time.
 * @param opts
 *        The option set to be manipulated.
 * @param libjte_handle
 *        Pointer to a struct libjte_env e.g. created by libjte_new().
 *        It must stay existent from the start of image generation by
 *        iso_image_create_burn_source() until the write thread has ended.
 *        This can be inquired by iso_image_generator_is_running().
 *        In order to keep the libisofs API identical with and without
 *        libjte support the parameter type is (void *).
 * @return
 *        ISO_SUCCESS or error
 *
 * @since 0.6.38
*/
int iso_write_opts_attach_jte(IsoWriteOpts *opts, void *libjte_handle);

/**
 * Remove eventual association to a libjte environment handle.
 * The call will fail if no libjte support was enabled at compile time.
 * @param opts
 *        The option set to be manipulated.
 * @param libjte_handle
 *        If not submitted as NULL, this will return the previously set
 *        libjte handle. 
 * @return
 *        ISO_SUCCESS or error
 *
 * @since 0.6.38
*/
int iso_write_opts_detach_jte(IsoWriteOpts *opts, void **libjte_handle);


/**
 * Cause a number of blocks with zero bytes to be written after the payload
 * data, but before the eventual checksum data. Unlike libburn tail padding,
 * these blocks are counted as part of the image and covered by eventual
 * image checksums.
 * A reason for such padding can be the wish to prevent the Linux read-ahead
 * bug by sacrificial data which still belong to image and Jigdo template.
 * Normally such padding would be the job of the burn program which should know
 * that it is needed with CD write type TAO if Linux read(2) shall be able
 * to read all payload blocks.
 * 150 blocks = 300 kB is the traditional sacrifice to the Linux kernel.
 * @param opts
 *        The option set to be manipulated.
 * @param num_blocks
 *        Number of extra 2 kB blocks to be written.
 * @return
 *        ISO_SUCCESS or error
 *
 * @since 0.6.38
 */
int iso_write_opts_set_tail_blocks(IsoWriteOpts *opts, uint32_t num_blocks);

/**
 * Copy a data file from the local filesystem into the emerging ISO image.
 * Mark it by an MBR partition entry as PreP partition and also cause
 * protective MBR partition entries before and after this partition.
 * Vladimir Serbinenko stated aboy PreP = PowerPC Reference Platform :
 *   "PreP [...] refers mainly to IBM hardware. PreP boot is a partition
 *    containing only raw ELF and having type 0x41."
 *
 * This feature is only combinable with system area type 0
 * and currently not combinable with ISOLINUX isohybrid production.
 * It overrides --protective-msdos-label. See iso_write_opts_set_system_area().
 * Only partition 4 stays available for iso_write_opts_set_partition_img().
 * It is compatible with HFS+/FAT production by storing the PreP partition
 * before the start of the HFS+/FAT partition.
 *
 * @param opts
 *        The option set to be manipulated.
 * @param image_path
 *        File address in the local file system.
 *        NULL revokes production of the PreP partition.
 * @param flag
 *        Reserved for future usage, set to 0.
 * @return
 *        ISO_SUCCESS or error
 *
 * @since 1.2.4
 */
int iso_write_opts_set_prep_img(IsoWriteOpts *opts, char *image_path,
                                int flag);

/**
 * Copy a data file from the local filesystem into the emerging ISO image.
 * Mark it by an GPT partition entry as EFI System partition, and also cause
 * protective GPT partition entries before and after the partition.
 * GPT = Globally Unique Identifier Partition Table
 *
 * This feature may collide with data submitted by
 *   iso_write_opts_set_system_area()
 * and with settings made by
 *   el_torito_set_isolinux_options()
 * It is compatible with HFS+/FAT production by storing the EFI partition
 * before the start of the HFS+/FAT partition.
 * The GPT overwrites byte 0x0200 to 0x03ff of the system area and all
 * further bytes above 0x0800 which are not used by an Apple Partition Map.
 *
 * @param opts
 *        The option set to be manipulated.
 * @param image_path
 *        File address in the local file system.
 *        NULL revokes production of the EFI boot partition.
 * @param flag
 *        Reserved for future usage, set to 0.
 * @return
 *        ISO_SUCCESS or error
 *
 * @since 1.2.4
 */
int iso_write_opts_set_efi_bootp(IsoWriteOpts *opts, char *image_path, 
                                 int flag);

/**
 * Cause an arbitrary data file to be appended to the ISO image and to be
 * described by a partition table entry in an MBR or SUN Disk Label at the
 * start of the ISO image.
 * The partition entry will bear the size of the image file rounded up to
 * the next multiple of 2048 bytes.
 * MBR or SUN Disk Label are selected by iso_write_opts_set_system_area()
 * system area type: 0 selects MBR partition table. 3 selects a SUN partition
 * table with 320 kB start alignment.
 *
 * @param opts
 *        The option set to be manipulated.
 * @param partition_number
 *        Depicts the partition table entry which shall describe the
 *        appended image.
 *        Range with MBR: 1 to 4. 1 will cause the whole ISO image to be
 *                        unclaimable space before partition 1.
 *        Range with SUN Disk Label: 2 to 8.
 * @param image_path
 *        File address in the local file system.
 *        With SUN Disk Label: an empty name causes the partition to become
 *        a copy of the next lower partition.
 * @param image_type
 *        The MBR partition type. E.g. FAT12 = 0x01 , FAT16 = 0x06, 
 *        Linux Native Partition = 0x83. See fdisk command L.
 *        This parameter is ignored with SUN Disk Label.
 * @param flag
 *        Reserved for future usage, set to 0.
 * @return
 *        ISO_SUCCESS or error
 *
 * @since 0.6.38
 */
int iso_write_opts_set_partition_img(IsoWriteOpts *opts, int partition_number,
                           uint8_t partition_type, char *image_path, int flag);


/**
 * Inquire the start address of the file data blocks after having used
 * IsoWriteOpts with iso_image_create_burn_source().
 * @param opts
 *        The option set that was used when starting image creation
 * @param data_start
 *        Returns the logical block address if it is already valid
 * @param flag
 *        Reserved for future usage, set to 0.
 * @return
 *        1 indicates valid data_start, <0 indicates invalid data_start
 *
 * @since 0.6.16
 */
int iso_write_opts_get_data_start(IsoWriteOpts *opts, uint32_t *data_start,
                                  int flag);

/**
 * Update the sizes of all files added to image.
 *
 * This may be called just before iso_image_create_burn_source() to force
 * libisofs to check the file sizes again (they're already checked when added
 * to IsoImage). It is useful if you have changed some files after adding then
 * to the image.
 *
 * @return
 *    1 on success, < 0 on error
 * @since 0.6.8
 */
int iso_image_update_sizes(IsoImage *image);

/**
 * Create a burn_source and a thread which immediately begins to generate
 * the image. That burn_source can be used with libburn as a data source
 * for a track. A copy of its public declaration in libburn.h can be found
 * further below in this text.
 *
 * If image generation shall be aborted by the application program, then
 * the .cancel() method of the burn_source must be called to end the
 * generation thread:  burn_src->cancel(burn_src);
 *
 * @param image
 *     The image to write.
 * @param opts
 *     The options for image generation. All needed data will be copied, so
 *     you can free the given struct once this function returns.
 * @param burn_src
 *     Location where the pointer to the burn_source will be stored
 * @return
 *     1 on success, < 0 on error
 *
 * @since 0.6.2
 */
int iso_image_create_burn_source(IsoImage *image, IsoWriteOpts *opts,
                                 struct burn_source **burn_src);

/**
 * Inquire whether the image generator thread is still at work. As soon as the
 * reply is 0, the caller of iso_image_create_burn_source() may assume that
 * the image generation has ended.
 * Nevertheless there may still be readily formatted output data pending in
 * the burn_source or its consumers. So the final delivery of the image has
 * also to be checked at the data consumer side,e.g. by burn_drive_get_status()
 * in case of libburn as consumer.
 * @param image
 *     The image to inquire.
 * @return
 *     1 generating of image stream is still in progress
 *     0 generating of image stream has ended meanwhile
 *
 * @since 0.6.38
 */
int iso_image_generator_is_running(IsoImage *image);

/**
 * Creates an IsoReadOpts for reading an existent image. You should set the
 * options desired with the correspondent setters. Note that you may want to
 * set the start block value.
 *
 * Options by default are determined by the selected profile.
 *
 * @param opts
 *     Pointer to the location where the newly created IsoReadOpts will be
 *     stored. You should free it with iso_read_opts_free() when no more
 *     needed.
 * @param profile
 *     Default profile for image reading. For now the following values are
 *     defined:
 *     ---> 0 [STANDARD]
 *         Suitable for most situations. Most extension are read. When both
 *         Joliet and RR extension are present, RR is used.
 *         AAIP for ACL and xattr is not enabled by default.
 * @return
 *      1 success, < 0 error
 *
 * @since 0.6.2
 */
int iso_read_opts_new(IsoReadOpts **opts, int profile);

/**
 * Free an IsoReadOpts previously allocated with iso_read_opts_new().
 *
 * @since 0.6.2
 */
void iso_read_opts_free(IsoReadOpts *opts);

/**
 * Set the block where the image begins. It is usually 0, but may be different
 * on a multisession disc.
 *
 * @since 0.6.2
 */
int iso_read_opts_set_start_block(IsoReadOpts *opts, uint32_t block);

/**
 * Do not read Rock Ridge extensions.
 * In most cases you don't want to use this. It could be useful if RR info
 * is damaged, or if you want to use the Joliet tree.
 *
 * @since 0.6.2
 */
int iso_read_opts_set_no_rockridge(IsoReadOpts *opts, int norr);

/**
 * Do not read Joliet extensions.
 *
 * @since 0.6.2
 */
int iso_read_opts_set_no_joliet(IsoReadOpts *opts, int nojoliet);

/**
 * Do not read ISO 9660:1999 enhanced tree
 *
 * @since 0.6.2
 */
int iso_read_opts_set_no_iso1999(IsoReadOpts *opts, int noiso1999);

/**
 * Control reading of AAIP informations about ACL and xattr when loading
 * existing images.
 * For importing ACL and xattr when inserting nodes from external filesystems
 * (e.g. the local POSIX filesystem) see iso_image_set_ignore_aclea().
 * For eventual writing of this information see iso_write_opts_set_aaip().
 *
 * @param opts
 *       The option set to be manipulated
 * @param noaaip
 *       1 = Do not read AAIP information
 *       0 = Read AAIP information if available
 *       All other values are reserved.
 * @since 0.6.14
 */
int iso_read_opts_set_no_aaip(IsoReadOpts *opts, int noaaip);

/**
 * Control reading of an array of MD5 checksums which is eventually stored
 * at the end of a session. See also iso_write_opts_set_record_md5().
 * Important: Loading of the MD5 array will only work if AAIP is enabled
 *            because its position and layout is recorded in xattr "isofs.ca".
 *
 * @param opts
 *       The option set to be manipulated
 * @param no_md5
 *       0 = Read MD5 array if available, refuse on non-matching MD5 tags
 *       1 = Do not read MD5 checksum array
 *       2 = Read MD5 array, but do not check MD5 tags
 *           @since 1.0.4
 *       All other values are reserved.
 *
 * @since 0.6.22
 */
int iso_read_opts_set_no_md5(IsoReadOpts *opts, int no_md5);


/**
 * Control discarding of eventual inode numbers from existing images.
 * Such numbers may come from RRIP 1.12 entries PX. If not discarded they
 * get written unchanged when the file object gets written into an ISO image. 
 * If this inode number is missing with a file in the imported image,
 * or if it has been discarded during image reading, then a unique inode number
 * will be generated at some time before the file gets written into an ISO
 * image.
 * Two image nodes which have the same inode number represent two hardlinks
 * of the same file object. So discarding the numbers splits hardlinks.
 *
 * @param opts
 *       The option set to be manipulated
 * @param new_inos
 *     1 = Discard imported inode numbers and finally hand out a unique new
 *         one to each single file before it gets written into an ISO image.
 *     0 = Keep eventual inode numbers from PX entries.
 *         All other values are reserved.
 * @since 0.6.20
 */
int iso_read_opts_set_new_inos(IsoReadOpts *opts, int new_inos);

/**
 * Whether to prefer Joliet over RR. libisofs usually prefers RR over
 * Joliet, as it give us much more info about files. So, if both extensions
 * are present, RR is used. You can set this if you prefer Joliet, but
 * note that this is not very recommended. This doesn't mean than RR
 * extensions are not read: if no Joliet is present, libisofs will read
 * RR tree.
 *
 * @since 0.6.2
 */
int iso_read_opts_set_preferjoliet(IsoReadOpts *opts, int preferjoliet);

/**
 * Set default uid for files when RR extensions are not present.
 *
 * @since 0.6.2
 */
int iso_read_opts_set_default_uid(IsoReadOpts *opts, uid_t uid);

/**
 * Set default gid for files when RR extensions are not present.
 *
 * @since 0.6.2
 */
int iso_read_opts_set_default_gid(IsoReadOpts *opts, gid_t gid);

/**
 * Set default permissions for files when RR extensions are not present.
 *
 * @param opts
 *       The option set to be manipulated
 * @param file_perm
 *      Permissions for files.
 * @param dir_perm
 *      Permissions for directories.
 *
 * @since 0.6.2
 */
int iso_read_opts_set_default_permissions(IsoReadOpts *opts, mode_t file_perm,
                                          mode_t dir_perm);

/**
 * Set the input charset of the file names on the image. NULL to use locale
 * charset. You have to specify a charset if the image filenames are encoded
 * in a charset different that the local one. This could happen, for example,
 * if the image was created on a system with different charset.
 *
 * @param opts
 *       The option set to be manipulated
 * @param charset
 *      The charset to use as input charset.  You can obtain the list of
 *      charsets supported on your system executing "iconv -l" in a shell.
 *
 * @since 0.6.2
 */
int iso_read_opts_set_input_charset(IsoReadOpts *opts, const char *charset);

/**
 * Enable or disable methods to automatically choose an input charset.
 * This eventually overrides the name set via iso_read_opts_set_input_charset()
 *
 * @param opts
 *       The option set to be manipulated
 * @param mode
 *       Bitfield for control purposes:
 *       bit0= Allow to use the input character set name which is eventually
 *             stored in attribute "isofs.cs" of the root directory.
 *             Applications may attach this xattr by iso_node_set_attrs() to
 *             the root node, call iso_write_opts_set_output_charset() with the
 *             same name and enable iso_write_opts_set_aaip() when writing
 *             an image.
 *       Submit any other bits with value 0.
 *
 * @since 0.6.18
 *
 */
int iso_read_opts_auto_input_charset(IsoReadOpts *opts, int mode);

/**
 * Enable or disable loading of the first 32768 bytes of the session.
 *
 * @param opts
 *       The option set to be manipulated
 * @param mode
 *       Bitfield for control purposes:
 *       bit0= Load System Area data and attach them to the image so that they
 *             get written by the next session, if not overridden by
 *             iso_write_opts_set_system_area().
 *       Submit any other bits with value 0.
 *
 * @since 0.6.30
 *
 */
int iso_read_opts_load_system_area(IsoReadOpts *opts, int mode);

/**
 * Import a previous session or image, for growing or modify.
 *
 * @param image
 *     The image context to which old image will be imported. Note that all
 *     files added to image, and image attributes, will be replaced with the
 *     contents of the old image.
 *     TODO #00025 support for merging old image files
 * @param src
 *     Data Source from which old image will be read. A extra reference is
 *     added, so you still need to iso_data_source_unref() yours.
 * @param opts
 *     Options for image import. All needed data will be copied, so you
 *     can free the given struct once this function returns.
 * @param features
 *     If not NULL, a new IsoReadImageFeatures will be allocated and filled
 *     with the features of the old image. It should be freed with
 *     iso_read_image_features_destroy() when no more needed. You can pass
 *     NULL if you're not interested on them.
 * @return
 *     1 on success, < 0 on error
 *
 * @since 0.6.2
 */
int iso_image_import(IsoImage *image, IsoDataSource *src, IsoReadOpts *opts,
                     IsoReadImageFeatures **features);

/**
 * Destroy an IsoReadImageFeatures object obtained with iso_image_import.
 *
 * @since 0.6.2
 */
void iso_read_image_features_destroy(IsoReadImageFeatures *f);

/**
 * Get the size (in 2048 byte block) of the image, as reported in the PVM.
 *
 * @since 0.6.2
 */
uint32_t iso_read_image_features_get_size(IsoReadImageFeatures *f);

/**
 * Whether RockRidge extensions are present in the image imported.
 *
 * @since 0.6.2
 */
int iso_read_image_features_has_rockridge(IsoReadImageFeatures *f);

/**
 * Whether Joliet extensions are present in the image imported.
 *
 * @since 0.6.2
 */
int iso_read_image_features_has_joliet(IsoReadImageFeatures *f);

/**
 * Whether the image is recorded according to ISO 9660:1999, i.e. it has
 * a version 2 Enhanced Volume Descriptor.
 *
 * @since 0.6.2
 */
int iso_read_image_features_has_iso1999(IsoReadImageFeatures *f);

/**
 * Whether El-Torito boot record is present present in the image imported.
 *
 * @since 0.6.2
 */
int iso_read_image_features_has_eltorito(IsoReadImageFeatures *f);

/**
 * Increments the reference counting of the given image.
 *
 * @since 0.6.2
 */
void iso_image_ref(IsoImage *image);

/**
 * Decrements the reference couting of the given image.
 * If it reaches 0, the image is free, together with its tree nodes (whether
 * their refcount reach 0 too, of course).
 *
 * @since 0.6.2
 */
void iso_image_unref(IsoImage *image);

/**
 * Attach user defined data to the image. Use this if your application needs
 * to store addition info together with the IsoImage. If the image already
 * has data attached, the old data will be freed.
 *
 * @param image
 *      The image to which data shall be attached.
 * @param data
 *      Pointer to application defined data that will be attached to the
 *      image. You can pass NULL to remove any already attached data.
 * @param give_up
 *      Function that will be called when the image does not need the data
 *      any more. It receives the data pointer as an argumente, and eventually
 *      causes data to be freed. It can be NULL if you don't need it.
 * @return
 *      1 on succes, < 0 on error
 *
 * @since 0.6.2
 */
int iso_image_attach_data(IsoImage *image, void *data, void (*give_up)(void*));

/**
 * The the data previously attached with iso_image_attach_data()
 *
 * @since 0.6.2
 */
void *iso_image_get_attached_data(IsoImage *image);

/**
 * Get the root directory of the image.
 * No extra ref is added to it, so you musn't unref it. Use iso_node_ref()
 * if you want to get your own reference.
 *
 * @since 0.6.2
 */
IsoDir *iso_image_get_root(const IsoImage *image);

/**
 * Fill in the volset identifier for a image.
 *
 * @since 0.6.2
 */
void iso_image_set_volset_id(IsoImage *image, const char *volset_id);

/**
 * Get the volset identifier.
 * The returned string is owned by the image and should not be freed nor
 * changed.
 *
 * @since 0.6.2
 */
const char *iso_image_get_volset_id(const IsoImage *image);

/**
 * Fill in the volume identifier for a image.
 *
 * @since 0.6.2
 */
void iso_image_set_volume_id(IsoImage *image, const char *volume_id);

/**
 * Get the volume identifier.
 * The returned string is owned by the image and should not be freed nor
 * changed.
 *
 * @since 0.6.2
 */
const char *iso_image_get_volume_id(const IsoImage *image);

/**
 * Fill in the publisher for a image.
 *
 * @since 0.6.2
 */
void iso_image_set_publisher_id(IsoImage *image, const char *publisher_id);

/**
 * Get the publisher of a image.
 * The returned string is owned by the image and should not be freed nor
 * changed.
 *
 * @since 0.6.2
 */
const char *iso_image_get_publisher_id(const IsoImage *image);

/**
 * Fill in the data preparer for a image.
 *
 * @since 0.6.2
 */
void iso_image_set_data_preparer_id(IsoImage *image,
                                    const char *data_preparer_id);

/**
 * Get the data preparer of a image.
 * The returned string is owned by the image and should not be freed nor
 * changed.
 *
 * @since 0.6.2
 */
const char *iso_image_get_data_preparer_id(const IsoImage *image);

/**
 * Fill in the system id for a image. Up to 32 characters.
 *
 * @since 0.6.2
 */
void iso_image_set_system_id(IsoImage *image, const char *system_id);

/**
 * Get the system id of a image.
 * The returned string is owned by the image and should not be freed nor
 * changed.
 *
 * @since 0.6.2
 */
const char *iso_image_get_system_id(const IsoImage *image);

/**
 * Fill in the application id for a image. Up to 128 chars.
 *
 * @since 0.6.2
 */
void iso_image_set_application_id(IsoImage *image, const char *application_id);

/**
 * Get the application id of a image.
 * The returned string is owned by the image and should not be freed nor
 * changed.
 *
 * @since 0.6.2
 */
const char *iso_image_get_application_id(const IsoImage *image);

/**
 * Fill copyright information for the image. Usually this refers
 * to a file on disc. Up to 37 characters.
 *
 * @since 0.6.2
 */
void iso_image_set_copyright_file_id(IsoImage *image,
                                     const char *copyright_file_id);

/**
 * Get the copyright information of a image.
 * The returned string is owned by the image and should not be freed nor
 * changed.
 *
 * @since 0.6.2
 */
const char *iso_image_get_copyright_file_id(const IsoImage *image);

/**
 * Fill abstract information for the image. Usually this refers
 * to a file on disc. Up to 37 characters.
 *
 * @since 0.6.2
 */
void iso_image_set_abstract_file_id(IsoImage *image,
                                    const char *abstract_file_id);

/**
 * Get the abstract information of a image.
 * The returned string is owned by the image and should not be freed nor
 * changed.
 *
 * @since 0.6.2
 */
const char *iso_image_get_abstract_file_id(const IsoImage *image);

/**
 * Fill biblio information for the image. Usually this refers
 * to a file on disc. Up to 37 characters.
 *
 * @since 0.6.2
 */
void iso_image_set_biblio_file_id(IsoImage *image, const char *biblio_file_id);

/**
 * Get the biblio information of a image.
 * The returned string is owned by the image and should not be freed nor
 * changed.
 *
 * @since 0.6.2
 */
const char *iso_image_get_biblio_file_id(const IsoImage *image);

/**
 * Get the four timestamps from the Primary Volume Descriptor of the imported
 * ISO image. The timestamps are strings which are either empty or consist
 * of 17 digits of the form YYYYMMDDhhmmsscc.
 * None of the returned string pointers shall be used for altering or freeing
 * data. They are just for reading. 
 *
 * @param image
 *        The image to be inquired.
 * @param vol_creation_time
 *        Returns a pointer to the Volume Creation time:
 *        When "the information in the volume was created."
 * @param vol_modification_time
 *        Returns a pointer to Volume Modification time:
 *        When "the information in the volume was last modified."
 * @param vol_expiration_time
 *        Returns a pointer to Volume Expiration time:
 *        When "the information in the volume may be regarded as obsolete."
 * @param vol_effective_time
 *        Returns a pointer to Volume Expiration time:
 *        When "the information in the volume may be used."
 * @return
 *        ISO_SUCCESS or error
 *
 * @since 1.2.8
 */
int iso_image_get_pvd_times(IsoImage *image,
                            char **creation_time, char **modification_time,
                            char **expiration_time, char **effective_time);

/**
 * Create a new set of El-Torito bootable images by adding a boot catalog
 * and the default boot image.
 * Further boot images may then be added by iso_image_add_boot_image().
 *
 * @param image
 *      The image to make bootable. If it was already bootable this function
 *      returns an error and the image remains unmodified.
 * @param image_path
 *      The absolute path of a IsoFile to be used as default boot image.
 * @param type
 *      The boot media type. This can be one of 3 types:
 *             - Floppy emulation: Boot image file must be exactly
 *               1200 kB, 1440 kB or 2880 kB.
 *             - Hard disc emulation: The image must begin with a master
 *               boot record with a single image.
 *             - No emulation. You should specify load segment and load size
 *               of image.
 * @param catalog_path
 *      The absolute path in the image tree where the catalog will be stored.
 *      The directory component of this path must be a directory existent on
 *      the image tree, and the filename component must be unique among all
 *      children of that directory on image. Otherwise a correspodent error
 *      code will be returned. This function will add an IsoBoot node that acts
 *      as a placeholder for the real catalog, that will be generated at image
 *      creation time.
 * @param boot
 *      Location where a pointer to the added boot image will be stored. That
 *      object is owned by the IsoImage and should not be freed by the user,
 *      nor dereferenced once the last reference to the IsoImage was disposed
 *      via iso_image_unref(). A NULL value is allowed if you don't need a
 *      reference to the boot image.
 * @return
 *      1 on success, < 0 on error
 *
 * @since 0.6.2
 */
int iso_image_set_boot_image(IsoImage *image, const char *image_path,
                             enum eltorito_boot_media_type type,
                             const char *catalog_path,
                             ElToritoBootImage **boot);

/**
 * Add a further boot image to the set of El-Torito bootable images.
 * This set has already to be created by iso_image_set_boot_image().
 * Up to 31 further boot images may be added.
 *
 * @param image
 *      The image to which the boot image shall be added.
 *      returns an error and the image remains unmodified.
 * @param image_path
 *      The absolute path of a IsoFile to be used as default boot image.
 * @param type
 *      The boot media type. See iso_image_set_boot_image
 * @param flag
 *      Bitfield for control purposes. Unused yet. Submit 0.
 * @param boot
 *      Location where a pointer to the added boot image will be stored.
 *      See iso_image_set_boot_image
 * @return
 *      1 on success, < 0 on error
 *                    ISO_BOOT_NO_CATALOG means iso_image_set_boot_image()
 *                    was not called first.
 *
 * @since 0.6.32
 */
int iso_image_add_boot_image(IsoImage *image, const char *image_path,
                             enum eltorito_boot_media_type type, int flag,
                             ElToritoBootImage **boot);

/**
 * Get the El-Torito boot catalog and the default boot image of an ISO image.
 *
 * This can be useful, for example, to check if a volume read from a previous
 * session or an existing image is bootable. It can also be useful to get
 * the image and catalog tree nodes. An application would want those, for
 * example, to prevent the user removing it.
 *
 * Both nodes are owned by libisofs and should not be freed. You can get your
 * own ref with iso_node_ref(). You can also check if the node is already
 * on the tree by getting its parent (note that when reading El-Torito info
 * from a previous image, the nodes might not be on the tree even if you haven't
 * removed them). Remember that you'll need to get a new ref
 * (with iso_node_ref()) before inserting them again to the tree, and probably
 * you will also need to set the name or permissions.
 *
 * @param image
 *      The image from which to get the boot image.
 * @param boot
 *      If not NULL, it will be filled with a pointer to the boot image, if
 *      any. That  object is owned by the IsoImage and should not be freed by
 *      the user, nor dereferenced once the last reference to the IsoImage was
 *      disposed via iso_image_unref().
 * @param imgnode
 *      When not NULL, it will be filled with the image tree node. No extra ref
 *      is added, you can use iso_node_ref() to get one if you need it.
 * @param catnode
 *      When not NULL, it will be filled with the catnode tree node. No extra
 *      ref is added, you can use iso_node_ref() to get one if you need it.
 * @return
 *      1 on success, 0 is the image is not bootable (i.e., it has no El-Torito
 *      image), < 0 error.
 *
 * @since 0.6.2
 */
int iso_image_get_boot_image(IsoImage *image, ElToritoBootImage **boot,
                             IsoFile **imgnode, IsoBoot **catnode);

/**
 * Get detailed information about the boot catalog that was loaded from
 * an ISO image.
 * The boot catalog links the El Torito boot record at LBA 17 with the 
 * boot images which are IsoFile objects in the image. The boot catalog
 * itself is not a regular file and thus will not deliver an IsoStream.
 * Its content is usually quite short and can be obtained by this call.
 *
 * @param image
 *      The image to inquire.
 * @param catnode
 *      Will return the boot catalog tree node. No extra ref is taken.
 * @param lba
 *      Will return the block address of the boot catalog in the image.
 * @param content
 *      Will return either NULL or an allocated memory buffer with the
 *      content bytes of the boot catalog.
 *      Dispose it by free() when no longer needed.
 * @param size
 *      Will return the number of bytes in content.
 * @return
 *      1 if reply is valid, 0 if not boot catalog was loaded, < 0 on error.
 *
 * @since 1.1.2
 */
int iso_image_get_bootcat(IsoImage *image, IsoBoot **catnode, uint32_t *lba,
                          char **content, off_t *size);


/**
 * Get all El-Torito boot images of an ISO image.
 *
 * The first of these boot images is the same as returned by
 * iso_image_get_boot_image(). The others are alternative boot images. 
 *
 * @param image
 *      The image from which to get the boot images.
 * @param num_boots
 *      The number of available array elements in boots and bootnodes.
 * @param boots
 *      Returns NULL or an allocated array of pointers to boot images.
 *      Apply system call free(boots) to dispose it.
 * @param bootnodes
 *      Returns NULL or an allocated array of pointers to the IsoFile nodes
 *      which bear the content of the boot images in boots.
 * @param flag
 *      Bitfield for control purposes. Unused yet. Submit 0.
 * @return
 *      1 on success, 0 no El-Torito catalog and boot image attached,
 *      < 0 error.
 *
 * @since 0.6.32
 */
int iso_image_get_all_boot_imgs(IsoImage *image, int *num_boots,
                   ElToritoBootImage ***boots, IsoFile ***bootnodes, int flag);


/**
 * Removes all El-Torito boot images from the ISO image.
 *
 * The IsoBoot node that acts as placeholder for the catalog is also removed
 * for the image tree, if there.
 * If the image is not bootable (don't have el-torito boot image) this function
 * just returns.
 *
 * @since 0.6.2
 */
void iso_image_remove_boot_image(IsoImage *image);

/**
 * Sets the sort weight of the boot catalog that is attached to an IsoImage.
 * 
 * For the meaning of sort weights see iso_node_set_sort_weight().
 * That function cannot be applied to the emerging boot catalog because
 * it is not represented by an IsoFile.
 *
 * @param image
 *      The image to manipulate.
 * @param sort_weight
 *      The larger this value, the lower will be the block address of the
 *      boot catalog record.
 * @return
 *      0= no boot catalog attached , 1= ok , <0 = error
 *
 * @since 0.6.32
 */
int iso_image_set_boot_catalog_weight(IsoImage *image, int sort_weight);

/**
 * Hides the boot catalog file from directory trees.
 * 
 * For the meaning of hiding files see iso_node_set_hidden().
 *
 * 
 * @param image
 *      The image to manipulate.
 * @param hide_attrs
 *      Or-combination of values from enum IsoHideNodeFlag to set the trees
 *      in which the record.
 * @return
 *      0= no boot catalog attached , 1= ok , <0 = error
 *
 * @since 0.6.34
 */
int iso_image_set_boot_catalog_hidden(IsoImage *image, int hide_attrs);


/**
 * Get the boot media type as of parameter "type" of iso_image_set_boot_image()
 * resp. iso_image_add_boot_image().
 *
 * @param bootimg
 *      The image to inquire
 * @param media_type
 *      Returns the media type
 * @return
 *      1 = ok , < 0 = error
 *
 * @since 0.6.32
 */
int el_torito_get_boot_media_type(ElToritoBootImage *bootimg, 
                                  enum eltorito_boot_media_type *media_type);

/**
 * Sets the platform ID of the boot image.
 * 
 * The Platform ID gets written into the boot catalog at byte 1 of the
 * Validation Entry, or at byte 1 of a Section Header Entry.
 * If Platform ID and ID String of two consequtive bootimages are the same
 *
 * @param bootimg
 *      The image to manipulate.
 * @param id
 *      A Platform ID as of
 *      El Torito 1.0  : 0x00= 80x86,  0x01= PowerPC,  0x02= Mac
 *      Others         : 0xef= EFI
 * @return
 *      1 ok , <=0 error
 *
 * @since 0.6.32
 */
int el_torito_set_boot_platform_id(ElToritoBootImage *bootimg, uint8_t id);

/**
 * Get the platform ID value. See el_torito_set_boot_platform_id().
 *
 * @param bootimg
 *      The image to inquire
 * @return
 *      0 - 255 : The platform ID 
 *      < 0     : error
 *
 * @since 0.6.32
 */
int el_torito_get_boot_platform_id(ElToritoBootImage *bootimg);

/**
 * Sets the load segment for the initial boot image. This is only for
 * no emulation boot images, and is a NOP for other image types.
 *
 * @since 0.6.2
 */
void el_torito_set_load_seg(ElToritoBootImage *bootimg, short segment);

/**
 * Get the load segment value. See el_torito_set_load_seg().
 *
 * @param bootimg
 *      The image to inquire
 * @return
 *      0 - 65535 : The load segment value 
 *      < 0       : error
 *
 * @since 0.6.32
 */
int el_torito_get_load_seg(ElToritoBootImage *bootimg);

/**
 * Sets the number of sectors (512b) to be load at load segment during
 * the initial boot procedure. This is only for
 * no emulation boot images, and is a NOP for other image types.
 *
 * @since 0.6.2
 */
void el_torito_set_load_size(ElToritoBootImage *bootimg, short sectors);

/**
 * Get the load size. See el_torito_set_load_size().
 *
 * @param bootimg
 *      The image to inquire
 * @return
 *      0 - 65535 : The load size value
 *      < 0       : error
 *
 * @since 0.6.32
 */
int el_torito_get_load_size(ElToritoBootImage *bootimg);

/**
 * Marks the specified boot image as not bootable
 *
 * @since 0.6.2
 */
void el_torito_set_no_bootable(ElToritoBootImage *bootimg);

/**
 * Get the bootability flag. See el_torito_set_no_bootable().
 *
 * @param bootimg
 *      The image to inquire
 * @return
 *      0 = not bootable, 1 = bootable , <0 = error
 *
 * @since 0.6.32
 */
int el_torito_get_bootable(ElToritoBootImage *bootimg);

/**
 * Set the id_string of the Validation Entry resp. Sector Header Entry which
 * will govern the boot image Section Entry in the El Torito Catalog.
 *
 * @param bootimg
 *      The image to manipulate.
 * @param id_string
 *      The first boot image puts 24 bytes of ID string into the Validation
 *      Entry, where they shall "identify the manufacturer/developer of
 *      the CD-ROM".
 *      Further boot images put 28 bytes into their Section Header.
 *      El Torito 1.0 states that "If the BIOS understands the ID string, it
 *      may choose to boot the system using one of these entries in place
 *      of the INITIAL/DEFAULT entry." (The INITIAL/DEFAULT entry points to the
 *      first boot image.)
 * @return
 *      1 = ok , <0 = error
 *
 * @since 0.6.32
 */
int el_torito_set_id_string(ElToritoBootImage *bootimg, uint8_t id_string[28]);

/** 
 * Get the id_string as of el_torito_set_id_string().
 *
 * @param bootimg
 *      The image to inquire
 * @param id_string
 *      Returns 28 bytes of id string
 * @return
 *      1 = ok , <0 = error
 *
 * @since 0.6.32
 */
int el_torito_get_id_string(ElToritoBootImage *bootimg, uint8_t id_string[28]);

/**
 * Set the Selection Criteria of a boot image.
 *
 * @param bootimg
 *      The image to manipulate.
 * @param crit
 *      The first boot image has no selection criteria. They will be ignored.
 *      Further boot images put 1 byte of Selection Criteria Type and 19
 *      bytes of data into their Section Entry.
 *      El Torito 1.0 states that "The format of the selection criteria is
 *      a function of the BIOS vendor. In the case of a foreign language
 *      BIOS three bytes would be used to identify the language".
 *      Type byte == 0 means "no criteria",
 *      type byte == 1 means "Language and Version Information (IBM)".
 * @return
 *      1 = ok , <0 = error
 *
 * @since 0.6.32
 */
int el_torito_set_selection_crit(ElToritoBootImage *bootimg, uint8_t crit[20]);

/** 
 * Get the Selection Criteria bytes as of el_torito_set_selection_crit().
 *
 * @param bootimg
 *      The image to inquire
 * @param id_string
 *      Returns 20 bytes of type and data
 * @return
 *      1 = ok , <0 = error
 *
 * @since 0.6.32
 */
int el_torito_get_selection_crit(ElToritoBootImage *bootimg, uint8_t crit[20]);


/**
 * Makes a guess whether the boot image was patched by a boot information
 * table. It is advisable to patch such boot images if their content gets
 * copied to a new location. See el_torito_set_isolinux_options().
 * Note: The reply can be positive only if the boot image was imported
 *       from an existing ISO image.
 *
 * @param bootimg
 *      The image to inquire
 * @param flag
 *       Bitfield for control purposes:
 *       bit0 - bit3= mode
 *       0 = inquire for classic boot info table as described in man mkisofs
 *           @since 0.6.32
 *       1 = inquire for GRUB2 boot info as of bit9 of options of
 *           el_torito_set_isolinux_options()
 *           @since 1.3.0
 * @return
 *      1 = seems to contain the inquired boot info, 0 = quite surely not
 * @since 0.6.32
 */
int el_torito_seems_boot_info_table(ElToritoBootImage *bootimg, int flag);

/**
 * Specifies options for ISOLINUX or GRUB boot images. This should only be used
 * if the type of boot image is known.
 *
 * @param bootimg
 *      The image to set options on 
 * @param options
 *        bitmask style flag. The following values are defined:
 *
 *        bit0= Patch the boot info table of the boot image.
 *              This does the same as mkisofs option -boot-info-table.
 *              Needed for ISOLINUX or GRUB boot images with platform ID 0.
 *              The table is located at byte 8 of the boot image file.
 *              Its size is 56 bytes. 
 *              The original boot image file on disk will not be modified.
 *
 *              One may use el_torito_seems_boot_info_table() for a
 *              qualified guess whether a boot info table is present in
 *              the boot image. If the result is 1 then it should get bit0
 *              set if its content gets copied to a new LBA.
 *
 *        bit1= Generate a ISOLINUX isohybrid image with MBR.
 *              ----------------------------------------------------------
 *              @deprecated since 31 Mar 2010:
 *              The author of syslinux, H. Peter Anvin requested that this
 *              feature shall not be used any more. He intends to cease
 *              support for the MBR template that is included in libisofs.
 *              ----------------------------------------------------------
 *              A hybrid image is a boot image that boots from either
 *              CD/DVD media or from disk-like media, e.g. USB stick.
 *              For that you need isolinux.bin from SYSLINUX 3.72 or later.
 *              IMPORTANT: The application has to take care that the image
 *                         on media gets padded up to the next full MB.
 *                         Under seiveral circumstances it might get aligned
 *                         automatically. But there is no warranty.
 *        bit2-7= Mentioning in isohybrid GPT
 *                0= Do not mention in GPT
 *                1= Mention as Basic Data partition.
 *                   This cannot be combined with GPT partitions as of
 *                   iso_write_opts_set_efi_bootp()
 *                   @since 1.2.4
 *                2= Mention as HFS+ partition.
 *                   This cannot be combined with HFS+ production by
 *                   iso_write_opts_set_hfsplus().
 *                   @since 1.2.4
 *                Primary GPT and backup GPT get written if at least one 
 *                ElToritoBootImage shall be mentioned.
 *                The first three mentioned GPT partitions get mirrored in the
 *                the partition table of the isohybrid MBR. They get type 0xfe.
 *                The MBR partition entry for PC-BIOS gets type 0x00 rather
 *                than 0x17.
 *                Often it is one of the further MBR partitions which actually
 *                gets used by EFI.
 *                @since 1.2.4
 *        bit8= Mention in isohybrid Apple partition map
 *              APM get written if at least one ElToritoBootImage shall be
 *              mentioned. The ISOLINUX MBR must look suitable or else an error
 *              event will happen at image generation time.
 *              @since 1.2.4
 *        bit9= GRUB2 boot info
 *              Patch the boot image file at byte 1012 with the 512-block
 *              address + 2. Two little endian 32-bit words. Low word first.
 *              This is combinable with bit0.
 *              @since 1.3.0
 * @param flag
 *        Reserved for future usage, set to 0.
 * @return
 *      1 success, < 0 on error
 * @since 0.6.12
 */
int el_torito_set_isolinux_options(ElToritoBootImage *bootimg,
                                   int options, int flag);

/** 
 * Get the options as of el_torito_set_isolinux_options().
 *
 * @param bootimg
 *      The image to inquire
 * @param flag
 *        Reserved for future usage, set to 0.
 * @return
 *      >= 0 returned option bits , <0 = error
 *
 * @since 0.6.32
 */
int el_torito_get_isolinux_options(ElToritoBootImage *bootimg, int flag);

/** Deprecated:
 * Specifies that this image needs to be patched. This involves the writing
 * of a 16 bytes boot information table at offset 8 of the boot image file.
 * The original boot image file won't be modified.
 * This is needed for isolinux boot images.
 *
 * @since 0.6.2
 * @deprecated Use el_torito_set_isolinux_options() instead
 */
void el_torito_patch_isolinux_image(ElToritoBootImage *bootimg);

/**
 * Obtain a copy of the eventually loaded first 32768 bytes of the imported
 * session, the System Area.
 * It will be written to the start of the next session unless it gets
 * overwritten by iso_write_opts_set_system_area().
 *
 * @param img
 *        The image to be inquired.
 * @param data
 *        A byte array of at least 32768 bytes to take the loaded bytes.
 * @param options
 *        The option bits which will be applied if not overridden by
 *        iso_write_opts_set_system_area(). See there.
 * @param flag
 *        Bitfield for control purposes, unused yet, submit 0
 * @return
 *        1 on success, 0 if no System Area was loaded, < 0 error.
 * @since 0.6.30
 */
int iso_image_get_system_area(IsoImage *img, char data[32768],
                              int *options, int flag);

/**
 * Add a MIPS boot file path to the image.
 * Up to 15 such files can be written into a MIPS Big Endian Volume Header
 * if this is enabled by value 1 in iso_write_opts_set_system_area() option
 * bits 2 to 7. 
 * A single file can be written into a DEC Boot Block if this is enabled by
 * value 2 in iso_write_opts_set_system_area() option bits 2 to 7. So only
 * the first added file gets into effect with this system area type.
 * The data files which shall serve as MIPS boot files have to be brought into
 * the image by the normal means.
 * @param img
 *        The image to be manipulated.
 * @param path
 *        Absolute path of the boot file in the ISO 9660 Rock Ridge tree.
 * @param flag
 *        Bitfield for control purposes, unused yet, submit 0
 * @return
 *        1 on success, < 0 error
 * @since 0.6.38
 */
int iso_image_add_mips_boot_file(IsoImage *image, char *path, int flag);

/**
 * Obtain the number of added MIPS Big Endian boot files and pointers to
 * their paths in the ISO 9660 Rock Ridge tree.
 * @param img
 *        The image to be inquired.
 * @param paths
 *        An array of pointers to be set to the registered boot file paths.
 *        This are just pointers to data inside IsoImage. Do not free() them.
 *        Eventually make own copies of the data before manipulating the image.
 * @param flag
 *        Bitfield for control purposes, unused yet, submit 0
 * @return
 *        >= 0 is the number of valid path pointers , <0 means error
 * @since 0.6.38
 */
int iso_image_get_mips_boot_files(IsoImage *image, char *paths[15], int flag);

/**
 * Clear the list of MIPS Big Endian boot file paths.
 * @param img
 *        The image to be manipulated.
 * @param flag
 *        Bitfield for control purposes, unused yet, submit 0
 * @return
 *        1 is success , <0 means error
 * @since 0.6.38
 */
int iso_image_give_up_mips_boot(IsoImage *image, int flag);

/**
 * Designate a data file in the ISO image of which the position and size
 * shall be written after the SUN Disk Label. The position is written as
 * 64-bit big-endian number to byte position 0x228. The size is written
 * as 32-bit big-endian to 0x230.
 * This setting has an effect only if system area type is set to 3
 * with iso_write_opts_set_system_area(). 
 *
 * @param img
 *        The image to be manipulated.
 * @param sparc_core
 *        The IsoFile which shall be mentioned after the SUN Disk label.
 *        NULL is a permissible value. It disables this feature.
 * @param flag
 *        Bitfield for control purposes, unused yet, submit 0
 * @return
 *        1 is success , <0 means error
 * @since 1.3.0
 */
int iso_image_set_sparc_core(IsoImage *img, IsoFile *sparc_core, int flag);

/**
 * Obtain the current setting of iso_image_set_sparc_core().
 *
 * @param img
 *        The image to be inquired.
 * @param sparc_core
 *        Will return a pointer to the IsoFile (or NULL, which is not an error)
 * @param flag
 *        Bitfield for control purposes, unused yet, submit 0
 * @return
 *        1 is success , <0 means error
 * @since 1.3.0
 */
int iso_image_get_sparc_core(IsoImage *img, IsoFile **sparc_core, int flag);

/**
 * Increments the reference counting of the given node.
 *
 * @since 0.6.2
 */
void iso_node_ref(IsoNode *node);

/**
 * Decrements the reference couting of the given node.
 * If it reach 0, the node is free, and, if the node is a directory,
 * its children will be unref() too.
 *
 * @since 0.6.2
 */
void iso_node_unref(IsoNode *node);

/**
 * Get the type of an IsoNode.
 *
 * @since 0.6.2
 */
enum IsoNodeType iso_node_get_type(IsoNode *node);

/**
 * Class of functions to handle particular extended information. A function
 * instance acts as an identifier for the type of the information. Structs
 * with same information type must use a pointer to the same function.
 *
 * @param data
 *     Attached data
 * @param flag
 *     What to do with the data. At this time the following values are
 *     defined:
 *      -> 1 the data must be freed
 * @return
 *     1 in any case.
 *
 * @since 0.6.4
 */
typedef int (*iso_node_xinfo_func)(void *data, int flag);

/**
 * Add extended information to the given node. Extended info allows
 * applications (and libisofs itself) to add more information to an IsoNode.
 * You can use this facilities to associate temporary information with a given
 * node. This information is not written into the ISO 9660 image on media
 * and thus does not persist longer than the node memory object.
 *
 * Each node keeps a list of added extended info, meaning you can add several
 * extended info data to each node. Each extended info you add is identified
 * by the proc parameter, a pointer to a function that knows how to manage
 * the external info data. Thus, in order to add several types of extended
 * info, you need to define a "proc" function for each type.
 *
 * @param node
 *      The node where to add the extended info
 * @param proc
 *      A function pointer used to identify the type of the data, and that
 *      knows how to manage it
 * @param data
 *      Extended info to add.
 * @return
 *      1 if success, 0 if the given node already has extended info of the
 *      type defined by the "proc" function, < 0 on error
 *
 * @since 0.6.4
 */
int iso_node_add_xinfo(IsoNode *node, iso_node_xinfo_func proc, void *data);

/**
 * Remove the given extended info (defined by the proc function) from the
 * given node.
 *
 * @return
 *      1 on success, 0 if node does not have extended info of the requested
 *      type, < 0 on error
 *
 * @since 0.6.4
 */
int iso_node_remove_xinfo(IsoNode *node, iso_node_xinfo_func proc);

/**
 * Remove all extended information  from the given node.
 *
 * @param node
 *      The node where to remove all extended info
 * @param flag
 *      Bitfield for control purposes, unused yet, submit 0
 * @return
 *      1 on success, < 0 on error
 *      
 * @since 1.0.2
 */
int iso_node_remove_all_xinfo(IsoNode *node, int flag);

/**
 * Get the given extended info (defined by the proc function) from the
 * given node.
 *
 * @param node
 *      The node to inquire
 * @param proc
 *      The function pointer which serves as key
 * @param data
 *      Will after successful call point to the xinfo data corresponding
 *      to the given proc. This is a pointer, not a feeable data copy.
 * @return
 *      1 on success, 0 if node does not have extended info of the requested
 *      type, < 0 on error
 *
 * @since 0.6.4
 */
int iso_node_get_xinfo(IsoNode *node, iso_node_xinfo_func proc, void **data);


/**
 * Get the next pair of function pointer and data of an iteration of the
 * list of extended informations. Like:
 *     iso_node_xinfo_func proc;
 *     void *handle = NULL, *data; 
 *     while (iso_node_get_next_xinfo(node, &handle, &proc, &data) == 1) {
 *         ... make use of proc and data ...
 *     }
 * The iteration allocates no memory. So you may end it without any disposal
 * action.
 * IMPORTANT: Do not continue iterations after manipulating the extended
 *            information of a node. Memory corruption hazard !
 * @param node
 *      The node to inquire
 * @param  handle
 *      The opaque iteration handle. Initialize iteration by submitting
 *      a pointer to a void pointer with value NULL.
 *      Do not alter its content until iteration has ended.
 * @param proc
 *      The function pointer which serves as key
 * @param data
 *      Will be filled with the extended info corresponding to the given proc
 *      function
 * @return
 *      1 on success
 *      0 if iteration has ended (proc and data are invalid then)
 *      < 0 on error
 *
 * @since 1.0.2
 */
int iso_node_get_next_xinfo(IsoNode *node, void **handle,
                            iso_node_xinfo_func *proc, void **data);


/**
 * Class of functions to clone extended information. A function instance gets
 * associated to a particular iso_node_xinfo_func instance by function
 * iso_node_xinfo_make_clonable(). This is a precondition to have IsoNode
 * objects clonable which carry data for a particular iso_node_xinfo_func.
 *
 * @param old_data
 *     Data item to be cloned
 * @param new_data
 *     Shall return the cloned data item
 * @param flag
 *     Unused yet, submit 0
 *     The function shall return ISO_XINFO_NO_CLONE on unknown flag bits.
 * @return
 *     > 0 number of allocated bytes
 *       0 no size info is available
 *     < 0 error
 * 
 * @since 1.0.2
 */
typedef int (*iso_node_xinfo_cloner)(void *old_data, void **new_data,int flag);

/**
 * Associate a iso_node_xinfo_cloner to a particular class of extended
 * information in order to make it clonable.
 *
 * @param proc
 *     The key and disposal function which identifies the particular
 *     extended information class.
 * @param cloner
 *     The cloner function which shall be associated with proc.
 * @param flag
 *     Unused yet, submit 0
 * @return
 *     1 success, < 0 error
 * 
 * @since 1.0.2
 */
int iso_node_xinfo_make_clonable(iso_node_xinfo_func proc,
                                 iso_node_xinfo_cloner cloner, int flag);

/**
 * Inquire the registered cloner function for a particular class of
 * extended information.
 *
 * @param proc
 *     The key and disposal function which identifies the particular
 *     extended information class.
 * @param cloner
 *     Will return the cloner function which is associated with proc, or NULL.
 * @param flag
 *     Unused yet, submit 0
 * @return
 *     1 success, 0 no cloner registered for proc, < 0 error
 * 
 * @since 1.0.2
 */
int iso_node_xinfo_get_cloner(iso_node_xinfo_func proc,
                              iso_node_xinfo_cloner *cloner, int flag);


/**
 * Set the name of a node. Note that if the node is already added to a dir
 * this can fail if dir already contains a node with the new name.
 *
 * @param node
 *      The node whose name you want to change. Note that you can't change
 *      the name of the root.
 * @param name
 *      The name for the node. If you supply an empty string or a
 *      name greater than 255 characters this returns with failure, and
 *      node name is not modified.
 * @return
 *      1 on success, < 0 on error
 *
 * @since 0.6.2
 */
int iso_node_set_name(IsoNode *node, const char *name);

/**
 * Get the name of a node.
 * The returned string belongs to the node and should not be modified nor
 * freed. Use strdup if you really need your own copy.
 *
 * @since 0.6.2
 */
const char *iso_node_get_name(const IsoNode *node);

/**
 * Set the permissions for the node. This attribute is only useful when
 * Rock Ridge extensions are enabled.
 *
 * @param node
 *      The node to change
 * @param mode
 *     bitmask with the permissions of the node, as specified in 'man 2 stat'.
 *     The file type bitfields will be ignored, only file permissions will be
 *     modified.
 *
 * @since 0.6.2
 */
void iso_node_set_permissions(IsoNode *node, mode_t mode);

/**
 * Get the permissions for the node
 *
 * @since 0.6.2
 */
mode_t iso_node_get_permissions(const IsoNode *node);

/**
 * Get the mode of the node, both permissions and file type, as specified in
 * 'man 2 stat'.
 *
 * @since 0.6.2
 */
mode_t iso_node_get_mode(const IsoNode *node);

/**
 * Set the user id for the node. This attribute is only useful when
 * Rock Ridge extensions are enabled.
 *
 * @since 0.6.2
 */
void iso_node_set_uid(IsoNode *node, uid_t uid);

/**
 * Get the user id of the node.
 *
 * @since 0.6.2
 */
uid_t iso_node_get_uid(const IsoNode *node);

/**
 * Set the group id for the node. This attribute is only useful when
 * Rock Ridge extensions are enabled.
 *
 * @since 0.6.2
 */
void iso_node_set_gid(IsoNode *node, gid_t gid);

/**
 * Get the group id of the node.
 *
 * @since 0.6.2
 */
gid_t iso_node_get_gid(const IsoNode *node);

/**
 * Set the time of last modification of the file
 *
 * @since 0.6.2
 */
void iso_node_set_mtime(IsoNode *node, time_t time);

/**
 * Get the time of last modification of the file
 *
 * @since 0.6.2
 */
time_t iso_node_get_mtime(const IsoNode *node);

/**
 * Set the time of last access to the file
 *
 * @since 0.6.2
 */
void iso_node_set_atime(IsoNode *node, time_t time);

/**
 * Get the time of last access to the file
 *
 * @since 0.6.2
 */
time_t iso_node_get_atime(const IsoNode *node);

/**
 * Set the time of last status change of the file
 *
 * @since 0.6.2
 */
void iso_node_set_ctime(IsoNode *node, time_t time);

/**
 * Get the time of last status change of the file
 *
 * @since 0.6.2
 */
time_t iso_node_get_ctime(const IsoNode *node);

/**
 * Set whether the node will be hidden in the directory trees of RR/ISO 9660,
 * or of Joliet (if enabled at all), or of ISO-9660:1999 (if enabled at all).
 *
 * A hidden file does not show up by name in the affected directory tree.
 * For example, if a file is hidden only in Joliet, it will normally
 * not be visible on Windows systems, while being shown on GNU/Linux.
 *
 * If a file is not shown in any of the enabled trees, then its content will
 * not be written to the image, unless LIBISO_HIDE_BUT_WRITE is given (which
 * is available only since release 0.6.34).
 *
 * @param node
 *      The node that is to be hidden.
 * @param hide_attrs
 *      Or-combination of values from enum IsoHideNodeFlag to set the trees
 *      in which the node's name shall be hidden.
 *
 * @since 0.6.2
 */
void iso_node_set_hidden(IsoNode *node, int hide_attrs);

/**
 * Get the hide_attrs as eventually set by iso_node_set_hidden().
 *
 * @param node
 *      The node to inquire.
 * @return
 *      Or-combination of values from enum IsoHideNodeFlag which are
 *      currently set for the node.
 *
 * @since 0.6.34
 */
int iso_node_get_hidden(IsoNode *node);

/**
 * Compare two nodes whether they are based on the same input and
 * can be considered as hardlinks to the same file objects.
 *
 * @param n1
 *     The first node to compare.
 * @param n2
 *     The second node to compare.
 * @return
 *     -1 if s1 is smaller s2 , 0 if s1 matches s2 , 1 if s1 is larger s2
 * @param flag
 *     Bitfield for control purposes, unused yet, submit 0
 * @since 0.6.20
 */
int iso_node_cmp_ino(IsoNode *n1, IsoNode *n2, int flag);

/**
 * Add a new node to a dir. Note that this function don't add a new ref to
 * the node, so you don't need to free it, it will be automatically freed
 * when the dir is deleted. Of course, if you want to keep using the node
 * after the dir life, you need to iso_node_ref() it.
 *
 * @param dir
 *     the dir where to add the node
 * @param child
 *     the node to add. You must ensure that the node hasn't previously added
 *     to other dir, and that the node name is unique inside the child.
 *     Otherwise this function will return a failure, and the child won't be
 *     inserted.
 * @param replace
 *     if the dir already contains a node with the same name, whether to
 *     replace or not the old node with this.
 * @return
 *     number of nodes in dir if succes, < 0 otherwise
 *     Possible errors:
 *         ISO_NULL_POINTER, if dir or child are NULL
 *         ISO_NODE_ALREADY_ADDED, if child is already added to other dir
 *         ISO_NODE_NAME_NOT_UNIQUE, a node with same name already exists
 *         ISO_WRONG_ARG_VALUE, if child == dir, or replace != (0,1)
 *
 * @since 0.6.2
 */
int iso_dir_add_node(IsoDir *dir, IsoNode *child,
                     enum iso_replace_mode replace);

/**
 * Locate a node inside a given dir.
 *
 * @param dir
 *     The dir where to look for the node.
 * @param name
 *     The name of the node
 * @param node
 *     Location for a pointer to the node, it will filled with NULL if the dir
 *     doesn't have a child with the given name.
 *     The node will be owned by the dir and shouldn't be unref(). Just call
 *     iso_node_ref() to get your own reference to the node.
 *     Note that you can pass NULL is the only thing you want to do is check
 *     if a node with such name already exists on dir.
 * @return
 *     1 node found, 0 child has no such node, < 0 error
 *     Possible errors:
 *         ISO_NULL_POINTER, if dir or name are NULL
 *
 * @since 0.6.2
 */
int iso_dir_get_node(IsoDir *dir, const char *name, IsoNode **node);

/**
 * Get the number of children of a directory.
 *
 * @return
 *     >= 0 number of items, < 0 error
 *     Possible errors:
 *         ISO_NULL_POINTER, if dir is NULL
 *
 * @since 0.6.2
 */
int iso_dir_get_children_count(IsoDir *dir);

/**
 * Removes a child from a directory.
 * The child is not freed, so you will become the owner of the node. Later
 * you can add the node to another dir (calling iso_dir_add_node), or free
 * it if you don't need it (with iso_node_unref).
 *
 * @return
 *     1 on success, < 0 error
 *     Possible errors:
 *         ISO_NULL_POINTER, if node is NULL
 *         ISO_NODE_NOT_ADDED_TO_DIR, if node doesn't belong to a dir
 *
 * @since 0.6.2
 */
int iso_node_take(IsoNode *node);

/**
 * Removes a child from a directory and free (unref) it.
 * If you want to keep the child alive, you need to iso_node_ref() it
 * before this call, but in that case iso_node_take() is a better
 * alternative.
 *
 * @return
 *     1 on success, < 0 error
 *
 * @since 0.6.2
 */
int iso_node_remove(IsoNode *node);

/*
 * Get the parent of the given iso tree node. No extra ref is added to the
 * returned directory, you must take your ref. with iso_node_ref() if you
 * need it.
 *
 * If node is the root node, the same node will be returned as its parent.
 *
 * This returns NULL if the node doesn't pertain to any tree
 * (it was removed/taken).
 *
 * @since 0.6.2
 */
IsoDir *iso_node_get_parent(IsoNode *node);

/**
 * Get an iterator for the children of the given dir.
 *
 * You can iterate over the children with iso_dir_iter_next. When finished,
 * you should free the iterator with iso_dir_iter_free.
 * You musn't delete a child of the same dir, using iso_node_take() or
 * iso_node_remove(), while you're using the iterator. You can use
 * iso_dir_iter_take() or iso_dir_iter_remove() instead.
 *
 * You can use the iterator in the way like this
 *
 * IsoDirIter *iter;
 * IsoNode *node;
 * if ( iso_dir_get_children(dir, &iter) != 1 ) {
 *     // handle error
 * }
 * while ( iso_dir_iter_next(iter, &node) == 1 ) {
 *     // do something with the child
 * }
 * iso_dir_iter_free(iter);
 *
 * An iterator is intended to be used in a single iteration over the
 * children of a dir. Thus, it should be treated as a temporary object,
 * and free as soon as possible.
 *
 * @return
 *     1 success, < 0 error
 *     Possible errors:
 *         ISO_NULL_POINTER, if dir or iter are NULL
 *         ISO_OUT_OF_MEM
 *
 * @since 0.6.2
 */
int iso_dir_get_children(const IsoDir *dir, IsoDirIter **iter);

/**
 * Get the next child.
 * Take care that the node is owned by its parent, and will be unref() when
 * the parent is freed. If you want your own ref to it, call iso_node_ref()
 * on it.
 *
 * @return
 *     1 success, 0 if dir has no more elements, < 0 error
 *     Possible errors:
 *         ISO_NULL_POINTER, if node or iter are NULL
 *         ISO_ERROR, on wrong iter usage, usual caused by modiying the
 *         dir during iteration
 *
 * @since 0.6.2
 */
int iso_dir_iter_next(IsoDirIter *iter, IsoNode **node);

/**
 * Check if there're more children.
 *
 * @return
 *     1 dir has more elements, 0 no, < 0 error
 *     Possible errors:
 *         ISO_NULL_POINTER, if iter is NULL
 *
 * @since 0.6.2
 */
int iso_dir_iter_has_next(IsoDirIter *iter);

/**
 * Free a dir iterator.
 *
 * @since 0.6.2
 */
void iso_dir_iter_free(IsoDirIter *iter);

/**
 * Removes a child from a directory during an iteration, without freeing it.
 * It's like iso_node_take(), but to be used during a directory iteration.
 * The node removed will be the last returned by the iteration.
 *
 * If you call this function twice without calling iso_dir_iter_next between
 * them is not allowed and you will get an ISO_ERROR in second call.
 *
 * @return
 *     1 on succes, < 0 error
 *     Possible errors:
 *         ISO_NULL_POINTER, if iter is NULL
 *         ISO_ERROR, on wrong iter usage, for example by call this before
 *         iso_dir_iter_next.
 *
 * @since 0.6.2
 */
int iso_dir_iter_take(IsoDirIter *iter);

/**
 * Removes a child from a directory during an iteration and unref() it.
 * Like iso_node_remove(), but to be used during a directory iteration.
 * The node removed will be the one returned by the previous iteration.
 *
 * It is not allowed to call this function twice without calling
 * iso_dir_iter_next inbetween.
 *
 * @return
 *     1 on succes, < 0 error
 *     Possible errors:
 *         ISO_NULL_POINTER, if iter is NULL
 *         ISO_ERROR, on wrong iter usage, for example by calling this before
 *         iso_dir_iter_next.
 *
 * @since 0.6.2
 */
int iso_dir_iter_remove(IsoDirIter *iter);

/**
 * Removes a node by iso_node_remove() or iso_dir_iter_remove(). If the node
 * is a directory then the whole tree of nodes underneath is removed too.
 *
 * @param node
 *      The node to be removed.
 * @param iter
 *      If not NULL, then the node will be removed by iso_dir_iter_remove(iter)
 *      else it will be removed by iso_node_remove(node).
 * @return
 *      1 is success, <0 indicates error
 *
 * @since 1.0.2
 */
int iso_node_remove_tree(IsoNode *node, IsoDirIter *boss_iter);


/**
 * @since 0.6.4
 */
typedef struct iso_find_condition IsoFindCondition;

/**
 * Create a new condition that checks if the node name matches the given
 * wildcard.
 *
 * @param wildcard
 * @result
 *      The created IsoFindCondition, NULL on error.
 *
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_name(const char *wildcard);

/**
 * Create a new condition that checks the node mode against a mode mask. It
 * can be used to check both file type and permissions.
 *
 * For example:
 *
 * iso_new_find_conditions_mode(S_IFREG) : search for regular files
 * iso_new_find_conditions_mode(S_IFCHR | S_IWUSR) : search for character
 *     devices where owner has write permissions.
 *
 * @param mask
 *      Mode mask to AND against node mode.
 * @result
 *      The created IsoFindCondition, NULL on error.
 *
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_mode(mode_t mask);

/**
 * Create a new condition that checks the node gid.
 *
 * @param gid
 *      Desired Group Id.
 * @result
 *      The created IsoFindCondition, NULL on error.
 *
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_gid(gid_t gid);

/**
 * Create a new condition that checks the node uid.
 *
 * @param uid
 *      Desired User Id.
 * @result
 *      The created IsoFindCondition, NULL on error.
 *
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_uid(uid_t uid);

/**
 * Possible comparison between IsoNode and given conditions.
 *
 * @since 0.6.4
 */
enum iso_find_comparisons {
    ISO_FIND_COND_GREATER,
    ISO_FIND_COND_GREATER_OR_EQUAL,
    ISO_FIND_COND_EQUAL,
    ISO_FIND_COND_LESS,
    ISO_FIND_COND_LESS_OR_EQUAL
};

/**
 * Create a new condition that checks the time of last access.
 *
 * @param time
 *      Time to compare against IsoNode atime.
 * @param comparison
 *      Comparison to be done between IsoNode atime and submitted time.
 *      Note that ISO_FIND_COND_GREATER, for example, is true if the node
 *      time is greater than the submitted time.
 * @result
 *      The created IsoFindCondition, NULL on error.
 *
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_atime(time_t time,
                      enum iso_find_comparisons comparison);

/**
 * Create a new condition that checks the time of last modification.
 *
 * @param time
 *      Time to compare against IsoNode mtime.
 * @param comparison
 *      Comparison to be done between IsoNode mtime and submitted time.
 *      Note that ISO_FIND_COND_GREATER, for example, is true if the node
 *      time is greater than the submitted time.
 * @result
 *      The created IsoFindCondition, NULL on error.
 *
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_mtime(time_t time,
                      enum iso_find_comparisons comparison);

/**
 * Create a new condition that checks the time of last status change.
 *
 * @param time
 *      Time to compare against IsoNode ctime.
 * @param comparison
 *      Comparison to be done between IsoNode ctime and submitted time.
 *      Note that ISO_FIND_COND_GREATER, for example, is true if the node
 *      time is greater than the submitted time.
 * @result
 *      The created IsoFindCondition, NULL on error.
 *
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_ctime(time_t time,
                      enum iso_find_comparisons comparison);

/**
 * Create a new condition that check if the two given conditions are
 * valid.
 *
 * @param a
 * @param b
 *      IsoFindCondition to compare
 * @result
 *      The created IsoFindCondition, NULL on error.
 *
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_and(IsoFindCondition *a,
                                              IsoFindCondition *b);

/**
 * Create a new condition that check if at least one the two given conditions
 * is valid.
 *
 * @param a
 * @param b
 *      IsoFindCondition to compare
 * @result
 *      The created IsoFindCondition, NULL on error.
 *
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_or(IsoFindCondition *a,
                                              IsoFindCondition *b);

/**
 * Create a new condition that check if the given conditions is false.
 *
 * @param negate
 * @result
 *      The created IsoFindCondition, NULL on error.
 *
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_not(IsoFindCondition *negate);

/**
 * Find all directory children that match the given condition.
 *
 * @param dir
 *      Directory where we will search children.
 * @param cond
 *      Condition that the children must match in order to be returned.
 *      It will be free together with the iterator. Remember to delete it
 *      if this function return error.
 * @param iter
 *      Iterator that returns only the children that match condition.
 * @return
 *      1 on success, < 0 on error
 *
 * @since 0.6.4
 */
int iso_dir_find_children(IsoDir* dir, IsoFindCondition *cond,
                          IsoDirIter **iter);

/**
 * Get the destination of a node.
 * The returned string belongs to the node and should not be modified nor
 * freed. Use strdup if you really need your own copy.
 *
 * @since 0.6.2
 */
const char *iso_symlink_get_dest(const IsoSymlink *link);

/**
 * Set the destination of a link.
 *
 * @param opts
 *     The option set to be manipulated
 * @param dest
 *     New destination for the link. It must be a non-empty string, otherwise
 *     this function doesn't modify previous destination.
 * @return
 *     1 on success, < 0 on error
 *
 * @since 0.6.2
 */
int iso_symlink_set_dest(IsoSymlink *link, const char *dest);

/**
 * Sets the order in which a node will be written on image. The data content
 * of files with high weight will be written to low block addresses.
 *
 * @param node
 *      The node which weight will be changed. If it's a dir, this function
 *      will change the weight of all its children. For nodes other that dirs
 *      or regular files, this function has no effect.
 * @param w
 *      The weight as a integer number, the greater this value is, the
 *      closer from the begining of image the file will be written.
 *      Default value at IsoNode creation is 0.
 *
 * @since 0.6.2
 */
void iso_node_set_sort_weight(IsoNode *node, int w);

/**
 * Get the sort weight of a file.
 *
 * @since 0.6.2
 */
int iso_file_get_sort_weight(IsoFile *file);

/**
 * Get the size of the file, in bytes
 *
 * @since 0.6.2
 */
off_t iso_file_get_size(IsoFile *file);

/**
 * Get the device id (major/minor numbers) of the given block or
 * character device file. The result is undefined for other kind
 * of special files, of first be sure iso_node_get_mode() returns either
 * S_IFBLK or S_IFCHR.
 *
 * @since 0.6.6
 */
dev_t iso_special_get_dev(IsoSpecial *special);

/**
 * Get the IsoStream that represents the contents of the given IsoFile.
 * The stream may be a filter stream which itself get its input from a
 * further stream. This may be inquired by iso_stream_get_input_stream().
 *
 * If you iso_stream_open() the stream, iso_stream_close() it before
 * image generation begins.
 *
 * @return
 *      The IsoStream. No extra ref is added, so the IsoStream belongs to the
 *      IsoFile, and it may be freed together with it. Add your own ref with
 *      iso_stream_ref() if you need it.
 *
 * @since 0.6.4
 */
IsoStream *iso_file_get_stream(IsoFile *file);

/**
 * Get the block lba of a file node, if it was imported from an old image.
 *
 * @param file
 *      The file
 * @param lba
 *      Will be filled with the kba
 * @param flag
 *      Reserved for future usage, submit 0
 * @return
 *      1 if lba is valid (file comes from old image), 0 if file was newly
 *      added, i.e. it does not come from an old image, < 0 error
 *
 * @since 0.6.4
 *
 * @deprecated Use iso_file_get_old_image_sections(), as this function does
 *             not work with multi-extend files.
 */
int iso_file_get_old_image_lba(IsoFile *file, uint32_t *lba, int flag);

/**
 * Get the start addresses and the sizes of the data extents of a file node
 * if it was imported from an old image.
 *
 * @param file
 *      The file
 * @param section_count
 *      Returns the number of extent entries in sections array.
 * @param sections
 *      Returns the array of file sections. Apply free() to dispose it.
 * @param flag
 *      Reserved for future usage, submit 0
 * @return
 *      1 if there are valid extents (file comes from old image),
 *      0 if file was newly added, i.e. it does not come from an old image,
 *      < 0 error
 *
 * @since 0.6.8
 */
int iso_file_get_old_image_sections(IsoFile *file, int *section_count,
                                   struct iso_file_section **sections,
                                   int flag);

/*
 * Like iso_file_get_old_image_lba(), but take an IsoNode.
 *
 * @return
 *      1 if lba is valid (file comes from old image), 0 if file was newly
 *      added, i.e. it does not come from an old image, 2 node type has no
 *      LBA (no regular file), < 0 error
 *
 * @since 0.6.4
 */
int iso_node_get_old_image_lba(IsoNode *node, uint32_t *lba, int flag);

/**
 * Add a new directory to the iso tree. Permissions, owner and hidden atts
 * are taken from parent, you can modify them later.
 *
 * @param parent
 *      the dir where the new directory will be created
 * @param name
 *      name for the new dir. If a node with same name already exists on
 *      parent, this functions fails with ISO_NODE_NAME_NOT_UNIQUE.
 * @param dir
 *      place where to store a pointer to the newly created dir. No extra
 *      ref is addded, so you will need to call iso_node_ref() if you really
 *      need it. You can pass NULL in this parameter if you don't need the
 *      pointer.
 * @return
 *     number of nodes in parent if success, < 0 otherwise
 *     Possible errors:
 *         ISO_NULL_POINTER, if parent or name are NULL
 *         ISO_NODE_NAME_NOT_UNIQUE, a node with same name already exists
 *         ISO_OUT_OF_MEM
 *
 * @since 0.6.2
 */
int iso_tree_add_new_dir(IsoDir *parent, const char *name, IsoDir **dir);

/**
 * Add a new regular file to the iso tree. Permissions are set to 0444,
 * owner and hidden atts are taken from parent. You can modify any of them
 * later.
 *
 * @param parent
 *      the dir where the new file will be created
 * @param name
 *      name for the new file. If a node with same name already exists on
 *      parent, this functions fails with ISO_NODE_NAME_NOT_UNIQUE.
 * @param stream
 *      IsoStream for the contents of the file. The reference will be taken
 *      by the newly created file, you will need to take an extra ref to it
 *      if you need it.
 * @param file
 *      place where to store a pointer to the newly created file. No extra
 *      ref is addded, so you will need to call iso_node_ref() if you really
 *      need it. You can pass NULL in this parameter if you don't need the
 *      pointer
 * @return
 *     number of nodes in parent if success, < 0 otherwise
 *     Possible errors:
 *         ISO_NULL_POINTER, if parent, name or dest are NULL
 *         ISO_NODE_NAME_NOT_UNIQUE, a node with same name already exists
 *         ISO_OUT_OF_MEM
 *
 * @since 0.6.4
 */
int iso_tree_add_new_file(IsoDir *parent, const char *name, IsoStream *stream,
                          IsoFile **file);

/**
 * Create an IsoStream object from content which is stored in a dynamically
 * allocated memory buffer. The new stream will become owner of the buffer
 * and apply free() to it when the stream finally gets destroyed itself.
 *
 * @param buf
 *     The dynamically allocated memory buffer with the stream content.
 * @parm size
 *     The number of bytes which may be read from buf.
 * @param stream
 *     Will return a reference to the newly created stream.
 * @return
 *     ISO_SUCCESS or <0 for error. E.g. ISO_NULL_POINTER, ISO_OUT_OF_MEM.
 *
 * @since 1.0.0
 */
int iso_memory_stream_new(unsigned char *buf, size_t size, IsoStream **stream);

/**
 * Add a new symlink to the directory tree. Permissions are set to 0777,
 * owner and hidden atts are taken from parent. You can modify any of them
 * later.
 *
 * @param parent
 *      the dir where the new symlink will be created
 * @param name
 *      name for the new symlink. If a node with same name already exists on
 *      parent, this functions fails with ISO_NODE_NAME_NOT_UNIQUE.
 * @param dest
 *      destination of the link
 * @param link
 *      place where to store a pointer to the newly created link. No extra
 *      ref is addded, so you will need to call iso_node_ref() if you really
 *      need it. You can pass NULL in this parameter if you don't need the
 *      pointer
 * @return
 *     number of nodes in parent if success, < 0 otherwise
 *     Possible errors:
 *         ISO_NULL_POINTER, if parent, name or dest are NULL
 *         ISO_NODE_NAME_NOT_UNIQUE, a node with same name already exists
 *         ISO_OUT_OF_MEM
 *
 * @since 0.6.2
 */
int iso_tree_add_new_symlink(IsoDir *parent, const char *name,
                             const char *dest, IsoSymlink **link);

/**
 * Add a new special file to the directory tree. As far as libisofs concerns,
 * an special file is a block device, a character device, a FIFO (named pipe)
 * or a socket. You can choose the specific kind of file you want to add
 * by setting mode propertly (see man 2 stat).
 *
 * Note that special files are only written to image when Rock Ridge
 * extensions are enabled. Moreover, a special file is just a directory entry
 * in the image tree, no data is written beyond that.
 *
 * Owner and hidden atts are taken from parent. You can modify any of them
 * later.
 *
 * @param parent
 *      the dir where the new special file will be created
 * @param name
 *      name for the new special file. If a node with same name already exists
 *      on parent, this functions fails with ISO_NODE_NAME_NOT_UNIQUE.
 * @param mode
 *      file type and permissions for the new node. Note that you can't
 *      specify any kind of file here, only special types are allowed. i.e,
 *      S_IFSOCK, S_IFBLK, S_IFCHR and S_IFIFO are valid types; S_IFLNK,
 *      S_IFREG and S_IFDIR aren't.
 * @param dev
 *      device ID, equivalent to the st_rdev field in man 2 stat.
 * @param special
 *      place where to store a pointer to the newly created special file. No
 *      extra ref is addded, so you will need to call iso_node_ref() if you
 *      really need it. You can pass NULL in this parameter if you don't need
 *      the pointer.
 * @return
 *     number of nodes in parent if success, < 0 otherwise
 *     Possible errors:
 *         ISO_NULL_POINTER, if parent, name or dest are NULL
 *         ISO_NODE_NAME_NOT_UNIQUE, a node with same name already exists
 *         ISO_WRONG_ARG_VALUE if you select a incorrect mode
 *         ISO_OUT_OF_MEM
 *
 * @since 0.6.2
 */
int iso_tree_add_new_special(IsoDir *parent, const char *name, mode_t mode,
                             dev_t dev, IsoSpecial **special);

/**
 * Set whether to follow or not symbolic links when added a file from a source
 * to IsoImage. Default behavior is to not follow symlinks.
 *
 * @since 0.6.2
 */
void iso_tree_set_follow_symlinks(IsoImage *image, int follow);

/**
 * Get current setting for follow_symlinks.
 *
 * @see iso_tree_set_follow_symlinks
 * @since 0.6.2
 */
int iso_tree_get_follow_symlinks(IsoImage *image);

/**
 * Set whether to skip or not disk files with names beginning by '.'
 * when adding a directory recursively.
 * Default behavior is to not ignore them.
 *
 * Clarification: This is not related to the IsoNode property to be hidden
 *                in one or more of the resulting image trees as of
 *                IsoHideNodeFlag and iso_node_set_hidden().
 *
 * @since 0.6.2
 */
void iso_tree_set_ignore_hidden(IsoImage *image, int skip);

/**
 * Get current setting for ignore_hidden.
 *
 * @see iso_tree_set_ignore_hidden
 * @since 0.6.2
 */
int iso_tree_get_ignore_hidden(IsoImage *image);

/**
 * Set the replace mode, that defines the behavior of libisofs when adding
 * a node whit the same name that an existent one, during a recursive
 * directory addition.
 *
 * @since 0.6.2
 */
void iso_tree_set_replace_mode(IsoImage *image, enum iso_replace_mode mode);

/**
 * Get current setting for replace_mode.
 *
 * @see iso_tree_set_replace_mode
 * @since 0.6.2
 */
enum iso_replace_mode iso_tree_get_replace_mode(IsoImage *image);

/**
 * Set whether to skip or not special files. Default behavior is to not skip
 * them. Note that, despite of this setting, special files will never be added
 * to an image unless RR extensions were enabled.
 *
 * @param image
 *      The image to manipulate.
 * @param skip
 *      Bitmask to determine what kind of special files will be skipped:
 *          bit0: ignore FIFOs
 *          bit1: ignore Sockets
 *          bit2: ignore char devices
 *          bit3: ignore block devices
 *
 * @since 0.6.2
 */
void iso_tree_set_ignore_special(IsoImage *image, int skip);

/**
 * Get current setting for ignore_special.
 *
 * @see iso_tree_set_ignore_special
 * @since 0.6.2
 */
int iso_tree_get_ignore_special(IsoImage *image);

/**
 * Add a excluded path. These are paths that won't never added to image, and
 * will be excluded even when adding recursively its parent directory.
 *
 * For example, in
 *
 *   iso_tree_add_exclude(image, "/home/user/data/private");
 *   iso_tree_add_dir_rec(image, root, "/home/user/data");
 *
 * the directory /home/user/data/private won't be added to image.
 *
 * However, if you explicity add a deeper dir, it won't be excluded. i.e.,
 * in the following example.
 *
 *   iso_tree_add_exclude(image, "/home/user/data");
 *   iso_tree_add_dir_rec(image, root, "/home/user/data/private");
 *
 * the directory /home/user/data/private is added. On the other, side, and
 * foollowing the the example above,
 *
 *   iso_tree_add_dir_rec(image, root, "/home/user");
 *
 * will exclude the directory "/home/user/data".
 *
 * Absolute paths are not mandatory, you can, for example, add a relative
 * path such as:
 *
 *   iso_tree_add_exclude(image, "private");
 *   iso_tree_add_exclude(image, "user/data");
 *
 * to excluve, respectively, all files or dirs named private, and also all
 * files or dirs named data that belong to a folder named "user". Not that the
 * above rule about deeper dirs is still valid. i.e., if you call
 *
 *   iso_tree_add_dir_rec(image, root, "/home/user/data/music");
 *
 * it is included even containing "user/data" string. However, a possible
 * "/home/user/data/music/user/data" is not added.
 *
 * Usual wildcards, such as * or ? are also supported, with the usual meaning
 * as stated in "man 7 glob". For example
 *
 * // to exclude backup text files
 * iso_tree_add_exclude(image, "*.~");
 *
 * @return
 *      1 on success, < 0 on error
 *
 * @since 0.6.2
 */
int iso_tree_add_exclude(IsoImage *image, const char *path);

/**
 * Remove a previously added exclude.
 *
 * @see iso_tree_add_exclude
 * @return
 *      1 on success, 0 exclude do not exists, < 0 on error
 *
 * @since 0.6.2
 */
int iso_tree_remove_exclude(IsoImage *image, const char *path);

/**
 * Set a callback function that libisofs will call for each file that is
 * added to the given image by a recursive addition function. This includes
 * image import.
 *
 * @param image
 *      The image to manipulate.
 * @param report
 *      pointer to a function that will be called just before a file will be
 *      added to the image. You can control whether the file will be in fact
 *      added or ignored.
 *      This function should return 1 to add the file, 0 to ignore it and
 *      continue, < 0 to abort the process
 *      NULL is allowed if you don't want any callback.
 *
 * @since 0.6.2
 */
void iso_tree_set_report_callback(IsoImage *image,
                                  int (*report)(IsoImage*, IsoFileSource*));

/**
 * Add a new node to the image tree, from an existing file.
 *
 * TODO comment Builder and Filesystem related issues when exposing both
 *
 * All attributes will be taken from the source file. The appropriate file
 * type will be created.
 *
 * @param image
 *      The image
 * @param parent
 *      The directory in the image tree where the node will be added.
 * @param path
 *      The absolute path of the file in the local filesystem.
 *      The node will have the same leaf name as the file on disk.
 *      Its directory path depends on the parent node.
 * @param node
 *      place where to store a pointer to the newly added file. No
 *      extra ref is addded, so you will need to call iso_node_ref() if you
 *      really need it. You can pass NULL in this parameter if you don't need
 *      the pointer.
 * @return
 *     number of nodes in parent if success, < 0 otherwise
 *     Possible errors:
 *         ISO_NULL_POINTER, if image, parent or path are NULL
 *         ISO_NODE_NAME_NOT_UNIQUE, a node with same name already exists
 *         ISO_OUT_OF_MEM
 *
 * @since 0.6.2
 */
int iso_tree_add_node(IsoImage *image, IsoDir *parent, const char *path,
                      IsoNode **node);

/**
 * This is a more versatile form of iso_tree_add_node which allows to set
 * the node name in ISO image already when it gets added. 
 *
 * Add a new node to the image tree, from an existing file, and with the
 * given name, that must not exist on dir.
 *
 * @param image
 *      The image
 * @param parent
 *      The directory in the image tree where the node will be added.
 * @param name
 *      The leaf name that the node will have on image.
 *      Its directory path depends on the parent node.
 * @param path
 *      The absolute path of the file in the local filesystem.
 * @param node
 *      place where to store a pointer to the newly added file. No
 *      extra ref is addded, so you will need to call iso_node_ref() if you
 *      really need it. You can pass NULL in this parameter if you don't need
 *      the pointer.
 * @return
 *     number of nodes in parent if success, < 0 otherwise
 *     Possible errors:
 *         ISO_NULL_POINTER, if image, parent or path are NULL
 *         ISO_NODE_NAME_NOT_UNIQUE, a node with same name already exists
 *         ISO_OUT_OF_MEM
 *
 * @since 0.6.4
 */
int iso_tree_add_new_node(IsoImage *image, IsoDir *parent, const char *name,
                          const char *path, IsoNode **node);

/**
 * Add a new node to the image tree with the given name that must not exist
 * on dir. The node data content will be a byte interval out of the data
 * content of a file in the local filesystem.
 *
 * @param image
 *      The image
 * @param parent
 *      The directory in the image tree where the node will be added.
 * @param name
 *      The leaf name that the node will have on image.
 *      Its directory path depends on the parent node.
 * @param path
 *      The absolute path of the file in the local filesystem. For now
 *      only regular files and symlinks to regular files are supported.
 * @param offset
 *      Byte number in the given file from where to start reading data.
 * @param size
 *      Max size of the file. This may be more than actually available from
 *      byte offset to the end of the file in the local filesystem.
 * @param node
 *      place where to store a pointer to the newly added file. No
 *      extra ref is addded, so you will need to call iso_node_ref() if you
 *      really need it. You can pass NULL in this parameter if you don't need
 *      the pointer.
 * @return
 *     number of nodes in parent if success, < 0 otherwise
 *     Possible errors:
 *         ISO_NULL_POINTER, if image, parent or path are NULL
 *         ISO_NODE_NAME_NOT_UNIQUE, a node with same name already exists
 *         ISO_OUT_OF_MEM
 *
 * @since 0.6.4
 */
int iso_tree_add_new_cut_out_node(IsoImage *image, IsoDir *parent,
                                  const char *name, const char *path,
                                  off_t offset, off_t size,
                                  IsoNode **node);

/**
 * Create a copy of the given node under a different path. If the node is
 * actually a directory then clone its whole subtree.
 * This call may fail because an IsoFile is encountered which gets fed by an
 * IsoStream which cannot be cloned. See also IsoStream_Iface method
 * clone_stream().
 * Surely clonable node types are:
 *   IsoDir,
 *   IsoSymlink,
 *   IsoSpecial,
 *   IsoFile from a loaded ISO image,
 *   IsoFile referring to local filesystem files,
 *   IsoFile created by iso_tree_add_new_file
 *           from a stream created by iso_memory_stream_new(),
 *   IsoFile created by iso_tree_add_new_cut_out_node()
 * Silently ignored are nodes of type IsoBoot.
 * An IsoFile node with IsoStream filters can be cloned if all those filters
 * are clonable and the node would be clonable without filter.
 * Clonable IsoStream filters are created by:
 *   iso_file_add_zisofs_filter()
 *   iso_file_add_gzip_filter()
 *   iso_file_add_external_filter()
 * An IsoNode with extended information as of iso_node_add_xinfo() can only be
 * cloned if each of the iso_node_xinfo_func instances is associated to a
 * clone function. See iso_node_xinfo_make_clonable().
 * All internally used classes of extended information are clonable.
 * 
 * @param node
 *      The node to be cloned.
 * @param new_parent
 *      The existing directory node where to insert the cloned node.
 * @param new_name
 *      The name for the cloned node. It must not yet exist in new_parent,
 *      unless it is a directory and node is a directory and flag bit0 is set.
 * @param new_node
 *      Will return a pointer (without reference) to the newly created clone.
 * @param flag
 *      Bitfield for control purposes. Submit any undefined bits as 0.
 *      bit0= Merge directories rather than returning ISO_NODE_NAME_NOT_UNIQUE.
 *            This will not allow to overwrite any existing node.
 *            Attributes of existing directories will not be overwritten.
 * @return
 *      <0 means error, 1 = new node created,
 *      2 = if flag bit0 is set: new_node is a directory which already existed.
 *
 * @since 1.0.2
 */
int iso_tree_clone(IsoNode *node,
                   IsoDir *new_parent, char *new_name, IsoNode **new_node,
                   int flag);

/**
 * Add the contents of a dir to a given directory of the iso tree.
 *
 * There are several options to control what files are added or how they are
 * managed. Take a look at iso_tree_set_* functions to see diferent options
 * for recursive directory addition.
 *
 * TODO comment Builder and Filesystem related issues when exposing both
 *
 * @param image
 *      The image to which the directory belongs.
 * @param parent
 *      Directory on the image tree where to add the contents of the dir
 * @param dir
 *      Path to a dir in the filesystem
 * @return
 *     number of nodes in parent if success, < 0 otherwise
 *
 * @since 0.6.2
 */
int iso_tree_add_dir_rec(IsoImage *image, IsoDir *parent, const char *dir);

/**
 * Locate a node by its absolute path on image.
 *
 * @param image
 *     The image to which the node belongs.
 * @param node
 *     Location for a pointer to the node, it will filled with NULL if the
 *     given path does not exists on image.
 *     The node will be owned by the image and shouldn't be unref(). Just call
 *     iso_node_ref() to get your own reference to the node.
 *     Note that you can pass NULL is the only thing you want to do is check
 *     if a node with such path really exists.
 * @return
 *      1 found, 0 not found, < 0 error
 *
 * @since 0.6.2
 */
int iso_tree_path_to_node(IsoImage *image, const char *path, IsoNode **node);

/**
 * Get the absolute path on image of the given node.
 *
 * @return
 *      The path on the image, that must be freed when no more needed. If the
 *      given node is not added to any image, this returns NULL.
 * @since 0.6.4
 */
char *iso_tree_get_node_path(IsoNode *node);

/**
 * Get the destination node of a symbolic link within the IsoImage.
 *
 * @param img
 *      The image wherein to try resolving the link.
 * @param sym
 *      The symbolic link node which to resolve.
 * @param res
 *      Will return the found destination node, in case of success.
 *      Call iso_node_ref() / iso_node_unref() if you intend to use the node
 *      over API calls which might in any event delete it.
 * @param depth
 *      Prevents endless loops. Submit as 0.
 * @param flag
 *      Bitfield for control purposes. Submit 0 for now.
 * @return
 *      1 on success,
 *      < 0 on failure, especially ISO_DEEP_SYMLINK and ISO_DEAD_SYMLINK
 *
 * @since 1.2.4
 */
int iso_tree_resolve_symlink(IsoImage *img, IsoSymlink *sym, IsoNode **res,
                             int *depth, int flag);

/* Maximum number link resolution steps before ISO_DEEP_SYMLINK gets
 * returned by iso_tree_resolve_symlink().
 *
 * @since 1.2.4
*/
#define LIBISO_MAX_LINK_DEPTH 100

/**
 * Increments the reference counting of the given IsoDataSource.
 *
 * @since 0.6.2
 */
void iso_data_source_ref(IsoDataSource *src);

/**
 * Decrements the reference counting of the given IsoDataSource, freeing it
 * if refcount reach 0.
 *
 * @since 0.6.2
 */
void iso_data_source_unref(IsoDataSource *src);

/**
 * Create a new IsoDataSource from a local file. This is suitable for
 * accessing regular files or block devices with ISO images.
 *
 * @param path
 *     The absolute path of the file
 * @param src
 *     Will be filled with the pointer to the newly created data source.
 * @return
 *    1 on success, < 0 on error.
 *
 * @since 0.6.2
 */
int iso_data_source_new_from_file(const char *path, IsoDataSource **src);

/**
 * Get the status of the buffer used by a burn_source.
 *
 * @param b
 *      A burn_source previously obtained with
 *      iso_image_create_burn_source().
 * @param size
 *      Will be filled with the total size of the buffer, in bytes
 * @param free_bytes
 *      Will be filled with the bytes currently available in buffer
 * @return
 *      < 0 error, > 0 state:
 *           1="active"    : input and consumption are active
 *           2="ending"    : input has ended without error
 *           3="failing"   : input had error and ended,
 *           5="abandoned" : consumption has ended prematurely
 *           6="ended"     : consumption has ended without input error
 *           7="aborted"   : consumption has ended after input error
 *
 * @since 0.6.2
 */
int iso_ring_buffer_get_status(struct burn_source *b, size_t *size,
                               size_t *free_bytes);

#define ISO_MSGS_MESSAGE_LEN 4096

/**
 * Control queueing and stderr printing of messages from libisofs.
 * Severity may be one of "NEVER", "FATAL", "SORRY", "WARNING", "HINT",
 * "NOTE", "UPDATE", "DEBUG", "ALL".
 *
 * @param queue_severity Gives the minimum limit for messages to be queued.
 *                       Default: "NEVER". If you queue messages then you
 *                       must consume them by iso_msgs_obtain().
 * @param print_severity Does the same for messages to be printed directly
 *                       to stderr.
 * @param print_id       A text prefix to be printed before the message.
 * @return               >0 for success, <=0 for error
 *
 * @since 0.6.2
 */
int iso_set_msgs_severities(char *queue_severity, char *print_severity,
                            char *print_id);

/**
 * Obtain the oldest pending libisofs message from the queue which has at
 * least the given minimum_severity. This message and any older message of
 * lower severity will get discarded from the queue and is then lost forever.
 *
 * Severity may be one of "NEVER", "FATAL", "SORRY", "WARNING", "HINT",
 * "NOTE", "UPDATE", "DEBUG", "ALL". To call with minimum_severity "NEVER"
 * will discard the whole queue.
 *
 * @param minimum_severity
 *     Threshhold
 * @param error_code
 *     Will become a unique error code as listed at the end of this header
 * @param imgid
 *     Id of the image that was issued the message.
 * @param msg_text
 *     Must provide at least ISO_MSGS_MESSAGE_LEN bytes.
 * @param severity
 *     Will become the severity related to the message and should provide at
 *     least 80 bytes.
 * @return
 *     1 if a matching item was found, 0 if not, <0 for severe errors
 *
 * @since 0.6.2
 */
int iso_obtain_msgs(char *minimum_severity, int *error_code, int *imgid,
                    char msg_text[], char severity[]);


/**
 * Submit a message to the libisofs queueing system. It will be queued or
 * printed as if it was generated by libisofs itself.
 *
 * @param error_code
 *      The unique error code of your message.
 *      Submit 0 if you do not have reserved error codes within the libburnia
 *      project.
 * @param msg_text
 *      Not more than ISO_MSGS_MESSAGE_LEN characters of message text.
 * @param os_errno
 *      Eventual errno related to the message. Submit 0 if the message is not
 *      related to a operating system error.
 * @param severity
 *      One of "ABORT", "FATAL", "FAILURE", "SORRY", "WARNING", "HINT", "NOTE",
 *      "UPDATE", "DEBUG". Defaults to "FATAL".
 * @param origin
 *      Submit 0 for now.
 * @return
 *      1 if message was delivered, <=0 if failure
 *
 * @since 0.6.4
 */
int iso_msgs_submit(int error_code, char msg_text[], int os_errno,
                    char severity[], int origin);


/**
 * Convert a severity name into a severity number, which gives the severity
 * rank of the name.
 *
 * @param severity_name
 *      A name as with iso_msgs_submit(), e.g. "SORRY".
 * @param severity_number
 *      The rank number: the higher, the more severe.
 * @return
 *      >0 success, <=0 failure
 *
 * @since 0.6.4
 */
int iso_text_to_sev(char *severity_name, int *severity_number);


/**
 * Convert a severity number into a severity name
 *
 * @param severity_number
 *      The rank number: the higher, the more severe.
 * @param severity_name
 *      A name as with iso_msgs_submit(), e.g. "SORRY".
 *
 * @since 0.6.4
 */
int iso_sev_to_text(int severity_number, char **severity_name);


/**
 * Get the id of an IsoImage, used for message reporting. This message id,
 * retrieved with iso_obtain_msgs(), can be used to distinguish what
 * IsoImage has isssued a given message.
 *
 * @since 0.6.2
 */
int iso_image_get_msg_id(IsoImage *image);

/**
 * Get a textual description of a libisofs error.
 *
 * @since 0.6.2
 */
const char *iso_error_to_msg(int errcode);

/**
 * Get the severity of a given error code
 * @return
 *       0x10000000 -> DEBUG
 *       0x20000000 -> UPDATE
 *       0x30000000 -> NOTE
 *       0x40000000 -> HINT
 *       0x50000000 -> WARNING
 *       0x60000000 -> SORRY
 *       0x64000000 -> MISHAP
 *       0x68000000 -> FAILURE
 *       0x70000000 -> FATAL
 *       0x71000000 -> ABORT
 *
 * @since 0.6.2
 */
int iso_error_get_severity(int e);

/**
 * Get the priority of a given error.
 * @return
 *      0x00000000 -> ZERO
 *      0x10000000 -> LOW
 *      0x20000000 -> MEDIUM
 *      0x30000000 -> HIGH
 *
 * @since 0.6.2
 */
int iso_error_get_priority(int e);

/**
 * Get the message queue code of a libisofs error.
 */
int iso_error_get_code(int e);

/**
 * Set the minimum error severity that causes a libisofs operation to
 * be aborted as soon as possible.
 *
 * @param severity
 *      one of "FAILURE", "MISHAP", "SORRY", "WARNING", "HINT", "NOTE".
 *      Severities greater or equal than FAILURE always cause program to abort.
 *      Severities under NOTE won't never cause function abort.
 * @return
 *      Previous abort priority on success, < 0 on error.
 *
 * @since 0.6.2
 */
int iso_set_abort_severity(char *severity);

/**
 * Return the messenger object handle used by libisofs. This handle
 * may be used by related libraries to  their own compatible
 * messenger objects and thus to direct their messages to the libisofs
 * message queue. See also: libburn, API function burn_set_messenger().
 *
 * @return the handle. Do only use with compatible
 *
 * @since 0.6.2
 */
void *iso_get_messenger();

/**
 * Take a ref to the given IsoFileSource.
 *
 * @since 0.6.2
 */
void iso_file_source_ref(IsoFileSource *src);

/**
 * Drop your ref to the given IsoFileSource, eventually freeing the associated
 * system resources.
 *
 * @since 0.6.2
 */
void iso_file_source_unref(IsoFileSource *src);

/*
 * this are just helpers to invoque methods in class
 */

/**
 * Get the absolute path in the filesystem this file source belongs to.
 *
 * @return
 *     the path of the FileSource inside the filesystem, it should be
 *     freed when no more needed.
 *
 * @since 0.6.2
 */
char* iso_file_source_get_path(IsoFileSource *src);

/**
 * Get the name of the file, with the dir component of the path.
 *
 * @return
 *     the name of the file, it should be freed when no more needed.
 *
 * @since 0.6.2
 */
char* iso_file_source_get_name(IsoFileSource *src);

/**
 * Get information about the file.
 * @return
 *    1 success, < 0 error
 *      Error codes:
 *         ISO_FILE_ACCESS_DENIED
 *         ISO_FILE_BAD_PATH
 *         ISO_FILE_DOESNT_EXIST
 *         ISO_OUT_OF_MEM
 *         ISO_FILE_ERROR
 *         ISO_NULL_POINTER
 *
 * @since 0.6.2
 */
int iso_file_source_lstat(IsoFileSource *src, struct stat *info);

/**
 * Check if the process has access to read file contents. Note that this
 * is not necessarily related with (l)stat functions. For example, in a
 * filesystem implementation to deal with an ISO image, if the user has
 * read access to the image it will be able to read all files inside it,
 * despite of the particular permission of each file in the RR tree, that
 * are what the above functions return.
 *
 * @return
 *     1 if process has read access, < 0 on error
 *      Error codes:
 *         ISO_FILE_ACCESS_DENIED
 *         ISO_FILE_BAD_PATH
 *         ISO_FILE_DOESNT_EXIST
 *         ISO_OUT_OF_MEM
 *         ISO_FILE_ERROR
 *         ISO_NULL_POINTER
 *
 * @since 0.6.2
 */
int iso_file_source_access(IsoFileSource *src);

/**
 * Get information about the file. If the file is a symlink, the info
 * returned refers to the destination.
 *
 * @return
 *    1 success, < 0 error
 *      Error codes:
 *         ISO_FILE_ACCESS_DENIED
 *         ISO_FILE_BAD_PATH
 *         ISO_FILE_DOESNT_EXIST
 *         ISO_OUT_OF_MEM
 *         ISO_FILE_ERROR
 *         ISO_NULL_POINTER
 *
 * @since 0.6.2
 */
int iso_file_source_stat(IsoFileSource *src, struct stat *info);

/**
 * Opens the source.
 * @return 1 on success, < 0 on error
 *      Error codes:
 *         ISO_FILE_ALREADY_OPENED
 *         ISO_FILE_ACCESS_DENIED
 *         ISO_FILE_BAD_PATH
 *         ISO_FILE_DOESNT_EXIST
 *         ISO_OUT_OF_MEM
 *         ISO_FILE_ERROR
 *         ISO_NULL_POINTER
 *
 * @since 0.6.2
 */
int iso_file_source_open(IsoFileSource *src);

/**
 * Close a previuously openned file
 * @return 1 on success, < 0 on error
 *      Error codes:
 *         ISO_FILE_ERROR
 *         ISO_NULL_POINTER
 *         ISO_FILE_NOT_OPENED
 *
 * @since 0.6.2
 */
int iso_file_source_close(IsoFileSource *src);

/**
 * Attempts to read up to count bytes from the given source into
 * the buffer starting at buf.
 *
 * The file src must be open() before calling this, and close() when no
 * more needed. Not valid for dirs. On symlinks it reads the destination
 * file.
 *
 * @param src
 *     The given source
 * @param buf
 *     Pointer to a buffer of at least count bytes where the read data will be
 *     stored
 * @param count
 *     Bytes to read
 * @return
 *     number of bytes read, 0 if EOF, < 0 on error
 *      Error codes:
 *         ISO_FILE_ERROR
 *         ISO_NULL_POINTER
 *         ISO_FILE_NOT_OPENED
 *         ISO_WRONG_ARG_VALUE -> if count == 0
 *         ISO_FILE_IS_DIR
 *         ISO_OUT_OF_MEM
 *         ISO_INTERRUPTED
 *
 * @since 0.6.2
 */
int iso_file_source_read(IsoFileSource *src, void *buf, size_t count);

/**
 * Repositions the offset of the given IsoFileSource (must be opened) to the
 * given offset according to the value of flag.
 *
 * @param src
 *     The given source
 * @param offset
 *      in bytes
 * @param flag
 *      0 The offset is set to offset bytes (SEEK_SET)
 *      1 The offset is set to its current location plus offset bytes
 *        (SEEK_CUR)
 *      2 The offset is set to the size of the file plus offset bytes
 *        (SEEK_END).
 * @return
 *      Absolute offset posistion on the file, or < 0 on error. Cast the
 *      returning value to int to get a valid libisofs error.
 * @since 0.6.4
 */
off_t iso_file_source_lseek(IsoFileSource *src, off_t offset, int flag);

/**
 * Read a directory.
 *
 * Each call to this function will return a new child, until we reach
 * the end of file (i.e, no more children), in that case it returns 0.
 *
 * The dir must be open() before calling this, and close() when no more
 * needed. Only valid for dirs.
 *
 * Note that "." and ".." children MUST NOT BE returned.
 *
 * @param src
 *     The given source
 * @param child
 *     pointer to be filled with the given child. Undefined on error or OEF
 * @return
 *     1 on success, 0 if EOF (no more children), < 0 on error
 *      Error codes:
 *         ISO_FILE_ERROR
 *         ISO_NULL_POINTER
 *         ISO_FILE_NOT_OPENED
 *         ISO_FILE_IS_NOT_DIR
 *         ISO_OUT_OF_MEM
 *
 * @since 0.6.2
 */
int iso_file_source_readdir(IsoFileSource *src, IsoFileSource **child);

/**
 * Read the destination of a symlink. You don't need to open the file
 * to call this.
 *
 * @param src
 *     An IsoFileSource corresponding to a symbolic link.
 * @param buf
 *     Allocated buffer of at least bufsiz bytes.
 *     The destination string will be copied there, and it will be 0-terminated
 *     if the return value indicates success or ISO_RR_PATH_TOO_LONG.
 * @param bufsiz
 *     Maximum number of buf characters + 1. The string will be truncated if
 *     it is larger than bufsiz - 1 and ISO_RR_PATH_TOO_LONG. will be returned.
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
 *         ISO_RR_PATH_TOO_LONG (@since 1.0.6)
 *
 * @since 0.6.2
 */
int iso_file_source_readlink(IsoFileSource *src, char *buf, size_t bufsiz);


/**
 * Get the AAIP string with encoded ACL and xattr.
 * (Not to be confused with ECMA-119 Extended Attributes).
 * @param src        The file source object to be inquired.
 * @param aa_string  Returns a pointer to the AAIP string data. If no AAIP
 *                   string is available, *aa_string becomes NULL.
 *                   (See doc/susp_aaip_2_0.txt for the meaning of AAIP.) 
 *                   The caller is responsible for finally calling free()
 *                   on non-NULL results.
 * @param flag       Bitfield for control purposes
 *                   bit0= Transfer ownership of AAIP string data.
 *                         src will free the eventual cached data and might
 *                         not be able to produce it again.
 *                   bit1= No need to get ACL (but no guarantee of exclusion)
 *                   bit2= No need to get xattr (but no guarantee of exclusion)
 * @return           1 means success (*aa_string == NULL is possible)
 *                  <0 means failure and must b a valid libisofs error code
 *                     (e.g. ISO_FILE_ERROR if no better one can be found).
 * @since 0.6.14
 */
int iso_file_source_get_aa_string(IsoFileSource *src,
                                  unsigned char **aa_string, int flag);

/**
 * Get the filesystem for this source. No extra ref is added, so you
 * musn't unref the IsoFilesystem.
 *
 * @return
 *     The filesystem, NULL on error
 *
 * @since 0.6.2
 */
IsoFilesystem* iso_file_source_get_filesystem(IsoFileSource *src);

/**
 * Take a ref to the given IsoFilesystem
 *
 * @since 0.6.2
 */
void iso_filesystem_ref(IsoFilesystem *fs);

/**
 * Drop your ref to the given IsoFilesystem, evetually freeing associated
 * resources.
 *
 * @since 0.6.2
 */
void iso_filesystem_unref(IsoFilesystem *fs);

/**
 * Create a new IsoFilesystem to access a existent ISO image.
 *
 * @param src
 *      Data source to access data.
 * @param opts
 *      Image read options
 * @param msgid
 *      An image identifer, obtained with iso_image_get_msg_id(), used to
 *      associated messages issued by the filesystem implementation with an
 *      existent image. If you are not using this filesystem in relation with
 *      any image context, just use 0x1fffff as the value for this parameter.
 * @param fs
 *      Will be filled with a pointer to the filesystem that can be used
 *      to access image contents.
 * @param
 *      1 on success, < 0 on error
 *
 * @since 0.6.2
 */
int iso_image_filesystem_new(IsoDataSource *src, IsoReadOpts *opts, int msgid,
                             IsoImageFilesystem **fs);

/**
 * Get the volset identifier for an existent image. The returned string belong
 * to the IsoImageFilesystem and shouldn't be free() nor modified.
 *
 * @since 0.6.2
 */
const char *iso_image_fs_get_volset_id(IsoImageFilesystem *fs);

/**
 * Get the volume identifier for an existent image. The returned string belong
 * to the IsoImageFilesystem and shouldn't be free() nor modified.
 *
 * @since 0.6.2
 */
const char *iso_image_fs_get_volume_id(IsoImageFilesystem *fs);

/**
 * Get the publisher identifier for an existent image. The returned string
 * belong to the IsoImageFilesystem and shouldn't be free() nor modified.
 *
 * @since 0.6.2
 */
const char *iso_image_fs_get_publisher_id(IsoImageFilesystem *fs);

/**
 * Get the data preparer identifier for an existent image. The returned string
 * belong to the IsoImageFilesystem and shouldn't be free() nor modified.
 *
 * @since 0.6.2
 */
const char *iso_image_fs_get_data_preparer_id(IsoImageFilesystem *fs);

/**
 * Get the system identifier for an existent image. The returned string belong
 * to the IsoImageFilesystem and shouldn't be free() nor modified.
 *
 * @since 0.6.2
 */
const char *iso_image_fs_get_system_id(IsoImageFilesystem *fs);

/**
 * Get the application identifier for an existent image. The returned string
 * belong to the IsoImageFilesystem and shouldn't be free() nor modified.
 *
 * @since 0.6.2
 */
const char *iso_image_fs_get_application_id(IsoImageFilesystem *fs);

/**
 * Get the copyright file identifier for an existent image. The returned string
 * belong to the IsoImageFilesystem and shouldn't be free() nor modified.
 *
 * @since 0.6.2
 */
const char *iso_image_fs_get_copyright_file_id(IsoImageFilesystem *fs);

/**
 * Get the abstract file identifier for an existent image. The returned string
 * belong to the IsoImageFilesystem and shouldn't be free() nor modified.
 *
 * @since 0.6.2
 */
const char *iso_image_fs_get_abstract_file_id(IsoImageFilesystem *fs);

/**
 * Get the biblio file identifier for an existent image. The returned string
 * belong to the IsoImageFilesystem and shouldn't be free() nor modified.
 *
 * @since 0.6.2
 */
const char *iso_image_fs_get_biblio_file_id(IsoImageFilesystem *fs);

/**
 * Increment reference count of an IsoStream.
 *
 * @since 0.6.4
 */
void iso_stream_ref(IsoStream *stream);

/**
 * Decrement reference count of an IsoStream, and eventually free it if
 * refcount reach 0.
 *
 * @since 0.6.4
 */
void iso_stream_unref(IsoStream *stream);

/**
 * Opens the given stream. Remember to close the Stream before writing the
 * image.
 *
 * @return
 *     1 on success, 2 file greater than expected, 3 file smaller than
 *     expected, < 0 on error
 *
 * @since 0.6.4
 */
int iso_stream_open(IsoStream *stream);

/**
 * Close a previously openned IsoStream.
 *
 * @return
 *      1 on success, < 0 on error
 *
 * @since 0.6.4
 */
int iso_stream_close(IsoStream *stream);

/**
 * Get the size of a given stream. This function should always return the same
 * size, even if the underlying source size changes, unless you call
 * iso_stream_update_size().
 *
 * @return
 *      IsoStream size in bytes
 *
 * @since 0.6.4
 */
off_t iso_stream_get_size(IsoStream *stream);

/**
 * Attempts to read up to count bytes from the given stream into
 * the buffer starting at buf.
 *
 * The stream must be open() before calling this, and close() when no
 * more needed.
 *
 * @return
 *     number of bytes read, 0 if EOF, < 0 on error
 *
 * @since 0.6.4
 */
int iso_stream_read(IsoStream *stream, void *buf, size_t count);

/**
 * Whether the given IsoStream can be read several times, with the same
 * results.
 * For example, a regular file is repeatable, you can read it as many
 * times as you want. However, a pipe isn't.
 *
 * This function doesn't take into account if the file has been modified
 * between the two reads.
 *
 * @return
 *     1 if stream is repeatable, 0 if not, < 0 on error
 *
 * @since 0.6.4
 */
int iso_stream_is_repeatable(IsoStream *stream);

/**
 * Updates the size of the IsoStream with the current size of the
 * underlying source.
 *
 * @return
 *     1 if ok, < 0 on error (has to be a valid libisofs error code),
 *     0 if the IsoStream does not support this function.
 * @since 0.6.8
 */
int iso_stream_update_size(IsoStream *stream);

/**
 * Get an unique identifier for a given IsoStream.
 *
 * @since 0.6.4
 */
void iso_stream_get_id(IsoStream *stream, unsigned int *fs_id, dev_t *dev_id,
                      ino_t *ino_id);

/**
 * Try to get eventual source path string of a stream. Meaning and availability
 * of this string depends on the stream.class . Expect valid results with
 * types "fsrc" and "cout". Result formats are
 * fsrc: result of file_source_get_path()
 * cout: result of file_source_get_path() " " offset " " size 
 * @param stream
 *     The stream to be inquired.
 * @param flag
 *     Bitfield for control purposes, unused yet, submit 0
 * @return
 *     A copy of the path string. Apply free() when no longer needed.
 *     NULL if no path string is available.
 *
 * @since 0.6.18
 */
char *iso_stream_get_source_path(IsoStream *stream, int flag);

/**
 * Compare two streams whether they are based on the same input and will
 * produce the same output. If in any doubt, then this comparison will
 * indicate no match.
 *
 * @param s1
 *     The first stream to compare.
 * @param s2
 *     The second stream to compare.
 * @return
 *     -1 if s1 is smaller s2 , 0 if s1 matches s2 , 1 if s1 is larger s2
 * @param flag
 *     bit0= do not use s1->class->compare() even if available
 *           (e.g. because iso_stream_cmp_ino(0 is called as fallback
 *            from said stream->class->compare())
 *
 * @since 0.6.20
 */
int iso_stream_cmp_ino(IsoStream *s1, IsoStream *s2, int flag);


/**
 * Produce a copy of a stream. It must be possible to operate both stream
 * objects concurrently. The success of this function depends on the
 * existence of a IsoStream_Iface.clone_stream() method with the stream
 * and with its eventual subordinate streams. 
 * See iso_tree_clone() for a list of surely clonable built-in streams.
 * 
 * @param old_stream
 *     The existing stream object to be copied
 * @param new_stream
 *     Will return a pointer to the copy
 * @param flag
 *     Bitfield for control purposes. Submit 0 for now.
 * @return
 *     >0 means success
 *     ISO_STREAM_NO_CLONE is issued if no .clone_stream() exists
 *     other error return values < 0 may occur depending on kind of stream
 *
 * @since 1.0.2
 */
int iso_stream_clone(IsoStream *old_stream, IsoStream **new_stream, int flag);


/* --------------------------------- AAIP --------------------------------- */

/**
 * Function to identify and manage AAIP strings as xinfo of IsoNode.
 *
 * An AAIP string contains the Attribute List with the xattr and ACL of a node
 * in the image tree. It is formatted according to libisofs specification
 * AAIP-2.0 and ready to be written into the System Use Area resp. Continuation
 * Area of a directory entry in an ISO image.
 *
 * Applications are not supposed to manipulate AAIP strings directly.
 * They should rather make use of the appropriate iso_node_get_* and
 * iso_node_set_* calls.
 *
 * AAIP represents ACLs as xattr with empty name and AAIP-specific binary
 * content. Local filesystems may represent ACLs as xattr with names like
 * "system.posix_acl_access". libisofs does not interpret those local
 * xattr representations of ACL directly but rather uses the ACL interface of
 * the local system. By default the local xattr representations of ACL will
 * not become part of the AAIP Attribute List via iso_local_get_attrs() and
 * not be attached to local files via iso_local_set_attrs().
 *
 * @since 0.6.14
 */
int aaip_xinfo_func(void *data, int flag);

/**
 * The iso_node_xinfo_cloner function which gets associated to aaip_xinfo_func
 * by iso_init() resp. iso_init_with_flag() via iso_node_xinfo_make_clonable().
 * @since 1.0.2
 */
int aaip_xinfo_cloner(void *old_data, void **new_data, int flag);

/**
 * Get the eventual ACLs which are associated with the node.
 * The result will be in "long" text form as of man acl resp. acl_to_text().
 * Call this function with flag bit15 to finally release the memory
 * occupied by an ACL inquiry.
 *
 * @param node
 *      The node that is to be inquired.
 * @param access_text
 *      Will return a pointer to the eventual "access" ACL text or NULL if it
 *      is not available and flag bit 4 is set.
 * @param default_text
 *      Will return a pointer to the eventual "default" ACL  or NULL if it
 *      is not available.
 *      (GNU/Linux directories can have a "default" ACL which influences
 *       the permissions of newly created files.)
 * @param flag
 *      Bitfield for control purposes
 *      bit4=  if no "access" ACL is available: return *access_text == NULL
 *             else:                       produce ACL from stat(2) permissions
 *      bit15= free memory and return 1 (node may be NULL)
 * @return
 *      2 *access_text was produced from stat(2) permissions
 *      1 *access_text was produced from ACL of node
 *      0 if flag bit4 is set and no ACL is available
 *      < 0 on error
 *
 * @since 0.6.14
 */
int iso_node_get_acl_text(IsoNode *node,
                          char **access_text, char **default_text, int flag);


/**
 * Set the ACLs of the given node to the lists in parameters access_text and
 * default_text or delete them.
 *
 * The stat(2) permission bits get updated according to the new "access" ACL if
 * neither bit1 of parameter flag is set nor parameter access_text is NULL.
 * Note that S_IRWXG permission bits correspond to ACL mask permissions
 * if a "mask::" entry exists in the ACL. Only if there is no "mask::" then
 * the "group::" entry corresponds to to S_IRWXG.
 * 
 * @param node
 *      The node that is to be manipulated.
 * @param access_text
 *      The text to be set into effect as "access" ACL. NULL will delete an
 *      eventually existing "access" ACL of the node.
 * @param default_text
 *      The text to be set into effect as "default" ACL. NULL will delete an
 *      eventually existing "default" ACL of the node.
 *      (GNU/Linux directories can have a "default" ACL which influences
 *       the permissions of newly created files.)
 * @param flag
 *      Bitfield for control purposes
 *      bit1=  ignore text parameters but rather update eventual "access" ACL
 *             to the stat(2) permissions of node. If no "access" ACL exists,
 *             then do nothing and return success.
 * @return
 *      > 0 success
 *      < 0 failure
 *
 * @since 0.6.14
 */
int iso_node_set_acl_text(IsoNode *node,
                          char *access_text, char *default_text, int flag);

/**
 * Like iso_node_get_permissions but reflecting ACL entry "group::" in S_IRWXG
 * rather than ACL entry "mask::". This is necessary if the permissions of a
 * node with ACL shall be restored to a filesystem without restoring the ACL.
 * The same mapping happens internally when the ACL of a node is deleted.
 * If the node has no ACL then the result is iso_node_get_permissions(node).
 * @param node
 *      The node that is to be inquired.
 * @return
 *      Permission bits as of stat(2)
 *
 * @since 0.6.14
 */
mode_t iso_node_get_perms_wo_acl(const IsoNode *node);


/**
 * Get the list of xattr which is associated with the node.
 * The resulting data may finally be disposed by a call to this function
 * with flag bit15 set, or its components may be freed one-by-one.
 * The following values are either NULL or malloc() memory:
 *   *names, *value_lengths, *values, (*names)[i], (*values)[i] 
 * with 0 <= i < *num_attrs.
 * It is allowed to replace or reallocate those memory items in order to
 * to manipulate the attribute list before submitting it to other calls.
 *
 * If enabled by flag bit0, this list possibly includes the ACLs of the node.
 * They are eventually encoded in a pair with empty name. It is not advisable
 * to alter the value or name of that pair. One may decide to erase both ACLs
 * by deleting this pair or to copy both ACLs by copying the content of this
 * pair to an empty named pair of another node.
 * For all other ACL purposes use iso_node_get_acl_text().
 *
 * @param node
 *      The node that is to be inquired.
 * @param num_attrs
 *      Will return the number of name-value pairs
 * @param names
 *      Will return an array of pointers to 0-terminated names
 * @param value_lengths
 *      Will return an arry with the lenghts of values
 * @param values
 *      Will return an array of pointers to strings of 8-bit bytes
 * @param flag
 *      Bitfield for control purposes
 *      bit0=  obtain eventual ACLs as attribute with empty name
 *      bit2=  with bit0: do not obtain attributes other than ACLs
 *      bit15= free memory (node may be NULL)
 * @return
 *      1 = ok (but *num_attrs may be 0)
 *    < 0 = error
 *
 * @since 0.6.14
 */
int iso_node_get_attrs(IsoNode *node, size_t *num_attrs,
              char ***names, size_t **value_lengths, char ***values, int flag);


/**
 * Obtain the value of a particular xattr name. Eventually make a copy of
 * that value and add a trailing 0 byte for caller convenience.
 * @param node
 *      The node that is to be inquired.
 * @param name
 *      The xattr name that shall be looked up.
 * @param value_length
 *      Will return the lenght of value
 * @param value
 *      Will return a string of 8-bit bytes. free() it when no longer needed.
 * @param flag
 *      Bitfield for control purposes, unused yet, submit 0
 * @return
 *      1= name found , 0= name not found , <0 indicates error
 *
 * @since 0.6.18
 */
int iso_node_lookup_attr(IsoNode *node, char *name,
                         size_t *value_length, char **value, int flag);

/**
 * Set the list of xattr which is associated with the node.
 * The data get copied so that you may dispose your input data afterwards.
 *
 * If enabled by flag bit0 then the submitted list of attributes will not only
 * overwrite xattr but also both eventual ACLs of the node. Eventual ACL in
 * the submitted list have to reside in an attribute with empty name.
 *
 * @param node
 *      The node that is to be manipulated.
 * @param num_attrs
 *      Number of attributes
 * @param names
 *      Array of pointers to 0 terminated name strings
 * @param value_lengths
 *      Array of byte lengths for each value
 * @param values
 *      Array of pointers to the value bytes
 * @param flag
 *      Bitfield for control purposes
 *      bit0= Do not maintain eventual existing ACL of the node.
 *            Set eventual new ACL from value of empty name.
 *      bit1= Do not clear the existing attribute list but merge it with
 *            the list given by this call.
 *            The given values override the values of their eventually existing
 *            names. If no xattr with a given name exists, then it will be
 *            added as new xattr. So this bit can be used to set a single
 *            xattr without inquiring any other xattr of the node.
 *      bit2= Delete the attributes with the given names
 *      bit3= Allow to affect non-user attributes.
 *            I.e. those with a non-empty name which does not begin by "user."
 *            (The empty name is always allowed and governed by bit0.) This
 *            deletes all previously existing attributes if not bit1 is set.
 *      bit4= Do not affect attributes from namespace "isofs".
 *            To be combined with bit3 for copying attributes from local
 *            filesystem to ISO image.
 *            @since 1.2.4
 * @return
 *      1 = ok
 *    < 0 = error
 *
 * @since 0.6.14
 */
int iso_node_set_attrs(IsoNode *node, size_t num_attrs, char **names,
                       size_t *value_lengths, char **values, int flag);


/* ----- This is an interface to ACL and xattr of the local filesystem ----- */

/**
 * libisofs has an internal system dependent adapter to ACL and xattr
 * operations. For the sake of completeness and simplicity it exposes this
 * functionality to its applications which might want to get and set ACLs
 * from local files.
 */

/**
 * Inquire whether local filesystem operations with ACL or xattr are enabled
 * inside libisofs. They may be disabled because of compile time decisions.
 * E.g. because the operating system does not support these features or
 * because libisofs has not yet an adapter to use them.
 * 
 * @param flag
 *      Bitfield for control purposes
 *           bit0= inquire availability of ACL
 *           bit1= inquire availability of xattr
 *           bit2 - bit7= Reserved for future types.
 *                        It is permissibile to set them to 1 already now.
 *           bit8 and higher: reserved, submit 0
 * @return
 *      Bitfield corresponding to flag. If bits are set, th
 *           bit0= ACL adapter is enabled
 *           bit1= xattr adapter is enabled
 *           bit2 - bit7= Reserved for future types.
 *           bit8 and higher: reserved, do not interpret these
 *
 * @since 1.1.6
 */
int iso_local_attr_support(int flag);

/**
 * Get an ACL of the given file in the local filesystem in long text form.
 *
 * @param disk_path
 *      Absolute path to the file
 * @param text
 *      Will return a pointer to the ACL text. If not NULL the text will be
 *      0 terminated and finally has to be disposed by a call to this function
 *      with bit15 set.
 * @param flag
 *      Bitfield for control purposes
 *           bit0=  get "default" ACL rather than "access" ACL
 *           bit4=  set *text = NULL and return 2
 *                  if the ACL matches st_mode permissions.
 *           bit5=  in case of symbolic link: inquire link target
 *           bit15= free text and return 1
 * @return
 *        1 ok 
 *        2 ok, trivial ACL found while bit4 is set, *text is NULL 
 *        0 no ACL manipulation adapter available / ACL not supported on fs
 *       -1 failure of system ACL service (see errno)
 *       -2 attempt to inquire ACL of a symbolic link without bit4 or bit5
 *          resp. with no suitable link target
 *
 * @since 0.6.14
 */
int iso_local_get_acl_text(char *disk_path, char **text, int flag);


/**
 * Set the ACL of the given file in the local filesystem to a given list
 * in long text form.
 *
 * @param disk_path
 *      Absolute path to the file
 * @param text
 *      The input text (0 terminated, ACL long text form)
 * @param flag
 *      Bitfield for control purposes
 *           bit0=  set "default" ACL rather than "access" ACL
 *           bit5=  in case of symbolic link: manipulate link target
 * @return
 *      > 0 ok
 *        0 no ACL manipulation adapter available for desired ACL type
 *       -1 failure of system ACL service (see errno)
 *       -2 attempt to manipulate ACL of a symbolic link without bit5
 *          resp. with no suitable link target
 *
 * @since 0.6.14
 */
int iso_local_set_acl_text(char *disk_path, char *text, int flag);


/**
 * Obtain permissions of a file in the local filesystem which shall reflect
 * ACL entry "group::" in S_IRWXG rather than ACL entry "mask::". This is
 * necessary if the permissions of a disk file with ACL shall be copied to
 * an object which has no ACL.
 * @param disk_path
 *      Absolute path to the local file which may have an "access" ACL or not.
 * @param flag
 *      Bitfield for control purposes
 *           bit5=  in case of symbolic link: inquire link target
 * @param st_mode
 *      Returns permission bits as of stat(2)
 * @return
 *      1 success
 *     -1 failure of lstat() resp. stat() (see errno)
 *
 * @since 0.6.14
 */
int iso_local_get_perms_wo_acl(char *disk_path, mode_t *st_mode, int flag);


/**
 * Get xattr and non-trivial ACLs of the given file in the local filesystem.
 * The resulting data has finally to be disposed by a call to this function
 * with flag bit15 set.
 *
 * Eventual ACLs will get encoded as attribute pair with empty name if this is
 * enabled by flag bit0. An ACL which simply replects stat(2) permissions
 * will not be put into the result.
 *
 * @param disk_path
 *      Absolute path to the file
 * @param num_attrs
 *      Will return the number of name-value pairs
 * @param names
 *      Will return an array of pointers to 0-terminated names
 * @param value_lengths
 *      Will return an arry with the lenghts of values
 * @param values
 *      Will return an array of pointers to 8-bit values
 * @param flag
 *      Bitfield for control purposes
 *      bit0=  obtain eventual ACLs as attribute with empty name
 *      bit2=  do not obtain attributes other than ACLs
 *      bit3=  do not ignore eventual non-user attributes.
 *             I.e. those with a name which does not begin by "user."
 *      bit5=  in case of symbolic link: inquire link target
 *      bit15= free memory
 * @return
 *        1 ok
 *      < 0 failure
 *
 * @since 0.6.14
 */
int iso_local_get_attrs(char *disk_path, size_t *num_attrs, char ***names,
                        size_t **value_lengths, char ***values, int flag);


/**
 * Attach a list of xattr and ACLs to the given file in the local filesystem.
 *
 * Eventual ACLs have to be encoded as attribute pair with empty name.
 *
 * @param disk_path
 *      Absolute path to the file
 * @param num_attrs
 *      Number of attributes
 * @param names
 *      Array of pointers to 0 terminated name strings
 * @param value_lengths
 *      Array of byte lengths for each attribute payload
 * @param values
 *      Array of pointers to the attribute payload bytes
 * @param flag
 *      Bitfield for control purposes
 *      bit0=  do not attach ACLs from an eventual attribute with empty name
 *      bit3=  do not ignore eventual non-user attributes.
 *             I.e. those with a name which does not begin by "user."
 *      bit5=  in case of symbolic link: manipulate link target
 *      bit6=  @since 1.1.6
               tolerate inappropriate presence or absence of
 *             directory "default" ACL
 * @return
 *      1 = ok 
 *    < 0 = error
 *
 * @since 0.6.14
 */
int iso_local_set_attrs(char *disk_path, size_t num_attrs, char **names,
                        size_t *value_lengths, char **values, int flag);


/* Default in case that the compile environment has no macro PATH_MAX.
*/
#define Libisofs_default_path_maX 4096


/* --------------------------- Filters in General -------------------------- */

/*
 * A filter is an IsoStream which uses another IsoStream as input. It gets
 * attached to an IsoFile by specialized calls iso_file_add_*_filter() which
 * replace its current IsoStream by the filter stream which takes over the
 * current IsoStream as input.
 * The consequences are:
 *   iso_file_get_stream() will return the filter stream.
 *   iso_stream_get_size() will return the (cached) size of the filtered data,
 *   iso_stream_open()     will start eventual child processes,
 *   iso_stream_close()    will kill eventual child processes,
 *   iso_stream_read()     will return filtered data. E.g. as data file content
 *                         during ISO image generation.
 *
 * There are external filters which run child processes
 *   iso_file_add_external_filter()
 * and internal filters
 *   iso_file_add_zisofs_filter()
 *   iso_file_add_gzip_filter()
 * which may or may not be available depending on compile time settings and
 * installed software packages like libz.
 *
 * During image generation filters get not in effect if the original IsoStream
 * is an "fsrc" stream based on a file in the loaded ISO image and if the
 * image generation type is set to 1 by iso_write_opts_set_appendable().
 */

/**
 * Delete the top filter stream from a data file. This is the most recent one
 * which was added by iso_file_add_*_filter().
 * Caution: One should not do this while the IsoStream of the file is opened.
 *          For now there is no general way to determine this state.
 *          Filter stream implementations are urged to eventually call .close()
 *          inside method .free() . This will close the input stream too.
 * @param file
 *      The data file node which shall get rid of one layer of content
 *      filtering.
 * @param flag
 *      Bitfield for control purposes, unused yet, submit 0.
 * @return
 *      1 on success, 0 if no filter was present
 *      <0 on error
 *
 * @since 0.6.18
 */
int iso_file_remove_filter(IsoFile *file, int flag);

/**
 * Obtain the eventual input stream of a filter stream.
 * @param stream
 *      The eventual filter stream to be inquired.
 * @param flag
 *      Bitfield for control purposes. Submit 0 for now.
 * @return
 *      The input stream, if one exists. Elsewise NULL.
 *      No extra reference to the stream is taken by this call.
 * 
 * @since 0.6.18
 */    
IsoStream *iso_stream_get_input_stream(IsoStream *stream, int flag);


/* ---------------------------- External Filters --------------------------- */

/**
 * Representation of an external program that shall serve as filter for
 * an IsoStream. This object may be shared among many IsoStream objects.
 * It is to be created and disposed by the application.
 *
 * The filter will act as proxy between the original IsoStream of an IsoFile.
 * Up to completed image generation it will be run at least twice: 
 * for IsoStream.class.get_size() and for .open() with subsequent .read().
 * So the original IsoStream has to return 1 by its .class.is_repeatable().
 * The filter program has to be repeateable too. I.e. it must produce the same
 * output on the same input.
 *
 * @since 0.6.18
 */
struct iso_external_filter_command
{
    /* Will indicate future extensions. It has to be 0 for now. */
    int version;

    /* Tells how many IsoStream objects depend on this command object.
     * One may only dispose an IsoExternalFilterCommand when this count is 0.
     * Initially this value has to be 0.
     */
    int refcount;

    /* An optional instance id.
     * Set to empty text if no individual name for this object is intended.
     */
    char *name;

    /* Absolute local filesystem path to the executable program. */
    char *path;

    /* Tells the number of arguments. */
    int argc;

    /* NULL terminated list suitable for system call execv(3).
     * I.e. argv[0] points to the alleged program name,
     *      argv[1] to argv[argc] point to program arguments (if argc > 0)
     *      argv[argc+1] is NULL
     */
    char **argv;

    /* A bit field which controls behavior variations:
     * bit0= Do not install filter if the input has size 0.
     * bit1= Do not install filter if the output is not smaller than the input.
     * bit2= Do not install filter if the number of output blocks is
     *       not smaller than the number of input blocks. Block size is 2048.
     *       Assume that non-empty input yields non-empty output and thus do
     *       not attempt to attach a filter to files smaller than 2049 bytes.
     * bit3= suffix removed rather than added.
     *       (Removal and adding suffixes is the task of the application.
     *        This behavior bit serves only as reminder for the application.)
     */
    int behavior;

    /* The eventual suffix which is supposed to be added to the IsoFile name
     * resp. to be removed from the name.
     * (This is to be done by the application, not by calls
     *  iso_file_add_external_filter() or iso_file_remove_filter().
     *  The value recorded here serves only as reminder for the application.)
     */
    char *suffix;
};

typedef struct iso_external_filter_command IsoExternalFilterCommand;

/**
 * Install an external filter command on top of the content stream of a data
 * file. The filter process must be repeatable. It will be run once by this
 * call in order to cache the output size.
 * @param file
 *      The data file node which shall show filtered content.
 * @param cmd
 *      The external program and its arguments which shall do the filtering.
 * @param flag
 *      Bitfield for control purposes, unused yet, submit 0.
 * @return
 *      1 on success, 2 if filter installation revoked (e.g. cmd.behavior bit1)
 *      <0 on error
 *
 * @since 0.6.18
 */
int iso_file_add_external_filter(IsoFile *file, IsoExternalFilterCommand *cmd,
                                 int flag);

/**
 * Obtain the IsoExternalFilterCommand which is eventually associated with the
 * given stream. (Typically obtained from an IsoFile by iso_file_get_stream()
 * or from an IsoStream by iso_stream_get_input_stream()).
 * @param stream
 *      The stream to be inquired.
 * @param cmd
 *      Will return the external IsoExternalFilterCommand. Valid only if
 *      the call returns 1. This does not increment cmd->refcount.
 * @param flag
 *      Bitfield for control purposes, unused yet, submit 0.
 * @return
 *      1 on success, 0 if the stream is not an external filter
 *      <0 on error
 *
 * @since 0.6.18
 */
int iso_stream_get_external_filter(IsoStream *stream,
                                   IsoExternalFilterCommand **cmd, int flag);


/* ---------------------------- Internal Filters --------------------------- */


/**
 * Install a zisofs filter on top of the content stream of a data file.
 * zisofs is a compression format which is decompressed by some Linux kernels.
 * See also doc/zisofs_format.txt .
 * The filter will not be installed if its output size is not smaller than
 * the size of the input stream.
 * This is only enabled if the use of libz was enabled at compile time.
 * @param file
 *      The data file node which shall show filtered content.
 * @param flag
 *      Bitfield for control purposes
 *      bit0= Do not install filter if the number of output blocks is
 *            not smaller than the number of input blocks. Block size is 2048.
 *      bit1= Install a decompression filter rather than one for compression.
 *      bit2= Only inquire availability of zisofs filtering. file may be NULL.
 *            If available return 2, else return error.
 *      bit3= is reserved for internal use and will be forced to 0
 * @return
 *      1 on success, 2 if filter available but installation revoked
 *      <0 on error, e.g. ISO_ZLIB_NOT_ENABLED
 *
 * @since 0.6.18
 */
int iso_file_add_zisofs_filter(IsoFile *file, int flag);

/**
 * Inquire the number of zisofs compression and uncompression filters which
 * are in use.
 * @param ziso_count
 *      Will return the number of currently installed compression filters.
 * @param osiz_count
 *      Will return the number of currently installed uncompression filters.
 * @param flag
 *      Bitfield for control purposes, unused yet, submit 0
 * @return
 *      1 on success, <0 on error
 *
 * @since 0.6.18
 */
int iso_zisofs_get_refcounts(off_t *ziso_count, off_t *osiz_count, int flag);


/**
 * Parameter set for iso_zisofs_set_params().
 *
 * @since 0.6.18
 */
struct iso_zisofs_ctrl {

    /* Set to 0 for this version of the structure */
    int version;

    /* Compression level for zlib function compress2(). From <zlib.h>:
     *  "between 0 and 9:
     *   1 gives best speed, 9 gives best compression, 0 gives no compression"
     * Default is 6.
     */
    int compression_level;

    /* Log2 of the block size for compression filters. Allowed values are:
     *   15 = 32 kiB ,  16 = 64 kiB ,  17 = 128 kiB
     */
    uint8_t block_size_log2;

};

/**
 * Set the global parameters for zisofs filtering.
 * This is only allowed while no zisofs compression filters are installed.
 * i.e. ziso_count returned by iso_zisofs_get_refcounts() has to be 0.
 * @param params
 *      Pointer to a structure with the intended settings.
 * @param flag
 *      Bitfield for control purposes, unused yet, submit 0
 * @return
 *      1 on success, <0 on error
 *
 * @since 0.6.18
 */
int iso_zisofs_set_params(struct iso_zisofs_ctrl *params, int flag);

/**
 * Get the current global parameters for zisofs filtering.
 * @param params
 *      Pointer to a caller provided structure which shall take the settings.
 * @param flag
 *      Bitfield for control purposes, unused yet, submit 0
 * @return
 *      1 on success, <0 on error
 *
 * @since 0.6.18
 */
int iso_zisofs_get_params(struct iso_zisofs_ctrl *params, int flag);


/**
 * Check for the given node or for its subtree whether the data file content
 * effectively bears zisofs file headers and eventually mark the outcome
 * by an xinfo data record if not already marked by a zisofs compressor filter.
 * This does not install any filter but only a hint for image generation
 * that the already compressed files shall get written with zisofs ZF entries.
 * Use this if you insert the compressed reults of program mkzftree from disk
 * into the image.
 * @param node
 *      The node which shall be checked and eventually marked.
 * @param flag
 *      Bitfield for control purposes, unused yet, submit 0
 *      bit0= prepare for a run with iso_write_opts_set_appendable(,1).
 *            Take into account that files from the imported image
 *            do not get their content filtered.
 *      bit1= permission to overwrite existing zisofs_zf_info
 *      bit2= if no zisofs header is found:
 *            create xinfo with parameters which indicate no zisofs
 *      bit3= no tree recursion if node is a directory
 *      bit4= skip files which stem from the imported image
 * @return
 *      0= no zisofs data found
 *      1= zf xinfo added
 *      2= found existing zf xinfo and flag bit1 was not set
 *      3= both encountered: 1 and 2
 *      <0 means error
 *
 * @since 0.6.18
 */
int iso_node_zf_by_magic(IsoNode *node, int flag);


/**
 * Install a gzip or gunzip filter on top of the content stream of a data file.
 * gzip is a compression format which is used by programs gzip and gunzip.
 * The filter will not be installed if its output size is not smaller than
 * the size of the input stream.
 * This is only enabled if the use of libz was enabled at compile time.
 * @param file
 *      The data file node which shall show filtered content.
 * @param flag
 *      Bitfield for control purposes
 *      bit0= Do not install filter if the number of output blocks is
 *            not smaller than the number of input blocks. Block size is 2048.
 *      bit1= Install a decompression filter rather than one for compression.
 *      bit2= Only inquire availability of gzip filtering. file may be NULL.
 *            If available return 2, else return error.
 *      bit3= is reserved for internal use and will be forced to 0
 * @return
 *      1 on success, 2 if filter available but installation revoked
 *      <0 on error, e.g. ISO_ZLIB_NOT_ENABLED
 *
 * @since 0.6.18
 */
int iso_file_add_gzip_filter(IsoFile *file, int flag);


/**
 * Inquire the number of gzip compression and uncompression filters which
 * are in use.
 * @param gzip_count
 *      Will return the number of currently installed compression filters.
 * @param gunzip_count
 *      Will return the number of currently installed uncompression filters.
 * @param flag
 *      Bitfield for control purposes, unused yet, submit 0
 * @return
 *      1 on success, <0 on error
 *
 * @since 0.6.18
 */
int iso_gzip_get_refcounts(off_t *gzip_count, off_t *gunzip_count, int flag);


/* ---------------------------- MD5 Checksums --------------------------- */

/* Production and loading of MD5 checksums is controlled by calls
   iso_write_opts_set_record_md5() and iso_read_opts_set_no_md5().
   For data representation details see doc/checksums.txt .
*/

/**
 * Eventually obtain the recorded MD5 checksum of the session which was
 * loaded as ISO image. Such a checksum may be stored together with others
 * in a contiguous array at the end of the session. The session checksum
 * covers the data blocks from address start_lba to address end_lba - 1.
 * It does not cover the recorded array of md5 checksums.
 * Layout, size, and position of the checksum array is recorded in the xattr
 * "isofs.ca" of the session root node.
 * @param image
 *      The image to inquire
 * @param start_lba
 *      Eventually returns the first block address covered by md5
 * @param end_lba
 *      Eventually returns the first block address not covered by md5 any more
 * @param md5
 *      Eventually returns 16 byte of MD5 checksum 
 * @param flag
 *      Bitfield for control purposes, unused yet, submit 0
 * @return
 *      1= md5 found , 0= no md5 available , <0 indicates error
 *
 * @since 0.6.22
 */
int iso_image_get_session_md5(IsoImage *image, uint32_t *start_lba,
                              uint32_t *end_lba, char md5[16], int flag);

/**
 * Eventually obtain the recorded MD5 checksum of a data file from the loaded
 * ISO image. Such a checksum may be stored with others in a contiguous
 * array at the end of the loaded session. The data file eventually has an
 * xattr "isofs.cx" which gives the index in that array.
 * @param image
 *      The image from which file stems.
 * @param file
 *      The file object to inquire
 * @param md5
 *      Eventually returns 16 byte of MD5 checksum 
 * @param flag
 *      Bitfield for control purposes
 *      bit0= only determine return value, do not touch parameter md5
 * @return
 *      1= md5 found , 0= no md5 available , <0 indicates error
 *
 * @since 0.6.22
 */
int iso_file_get_md5(IsoImage *image, IsoFile *file, char md5[16], int flag);

/**
 * Read the content of an IsoFile object, compute its MD5 and attach it to
 * the IsoFile. It can then be inquired by iso_file_get_md5() and will get
 * written into the next session if this is enabled at write time and if the
 * image write process does not compute an MD5 from content which it copies.
 * So this call can be used to equip nodes from the old image with checksums
 * or to make available checksums of newly added files before the session gets
 * written.
 * @param file
 *      The file object to read data from and to which to attach the checksum.
 *      If the file is from the imported image, then its most original stream
 *      will be checksummed. Else the eventual filter streams will get into
 *      effect.
 * @param flag
 *      Bitfield for control purposes. Unused yet. Submit 0.
 * @return
 *      1= ok, MD5 is computed and attached , <0 indicates error
 *
 * @since 0.6.22
 */
int iso_file_make_md5(IsoFile *file, int flag);

/**
 * Check a data block whether it is a libisofs session checksum tag and
 * eventually obtain its recorded parameters. These tags get written after
 * volume descriptors, directory tree and checksum array and can be detected
 * without loading the image tree.
 * One may start reading and computing MD5 at the suspected image session
 * start and look out for a session tag on the fly. See doc/checksum.txt .
 * @param data
 *      A complete and aligned data block read from an ISO image session.
 * @param tag_type
 *      0= no tag
 *      1= session tag
 *      2= superblock tag
 *      3= tree tag
 *      4= relocated 64 kB superblock tag (at LBA 0 of overwriteable media)
 * @param pos
 *      Returns the LBA where the tag supposes itself to be stored.
 *      If this does not match the data block LBA then the tag might be
 *      image data payload and should be ignored for image checksumming.
 * @param range_start
 *      Returns the block address where the session is supposed to start.
 *      If this does not match the session start on media then the image
 *      volume descriptors have been been relocated.
 *      A proper checksum will only emerge if computing started at range_start.
 * @param range_size
 *      Returns the number of blocks beginning at range_start which are
 *      covered by parameter md5.
 * @param next_tag
 *      Returns the predicted block address of the next tag.
 *      next_tag is valid only if not 0 and only with return values 2, 3, 4.
 *      With tag types 2 and 3, reading shall go on sequentially and the MD5
 *      computation shall continue up to that address.
 *      With tag type 4, reading shall resume either at LBA 32 for the first
 *      session or at the given address for the session which is to be loaded
 *      by default. In both cases the MD5 computation shall be re-started from
 *      scratch.
 * @param md5
 *      Returns 16 byte of MD5 checksum.
 * @param flag
 *      Bitfield for control purposes:
 *      bit0-bit7= tag type being looked for
 *                 0= any checksum tag
 *                 1= session tag
 *                 2= superblock tag
 *                 3= tree tag
 *                 4= relocated superblock tag
 * @return
 *      0= not a checksum tag, return parameters are invalid
 *      1= checksum tag found, return parameters are valid
 *     <0= error 
 *         (return parameters are valid with error ISO_MD5_AREA_CORRUPTED
 *          but not trustworthy because the tag seems corrupted)
 *
 * @since 0.6.22
 */
int iso_util_decode_md5_tag(char data[2048], int *tag_type, uint32_t *pos,
                            uint32_t *range_start, uint32_t *range_size,
                            uint32_t *next_tag, char md5[16], int flag);


/* The following functions allow to do own MD5 computations. E.g for
   comparing the result with a recorded checksum.
*/
/**
 * Create a MD5 computation context and hand out an opaque handle.
 *
 * @param md5_context
 *      Returns the opaque handle. Submitted *md5_context must be NULL or
 *      point to freeable memory.
 * @return
 *      1= success , <0 indicates error
 *
 * @since 0.6.22
 */
int iso_md5_start(void **md5_context);

/**
 * Advance the computation of a MD5 checksum by a chunk of data bytes.
 *
 * @param md5_context
 *      An opaque handle once returned by iso_md5_start() or iso_md5_clone().
 * @param data
 *      The bytes which shall be processed into to the checksum.
 * @param datalen
 *      The number of bytes to be processed.
 * @return
 *      1= success , <0 indicates error
 *
 * @since 0.6.22
 */
int iso_md5_compute(void *md5_context, char *data, int datalen);

/**     
 * Create a MD5 computation context as clone of an existing one. One may call
 * iso_md5_clone(old, &new, 0) and then iso_md5_end(&new, result, 0) in order
 * to obtain an intermediate MD5 sum before the computation goes on.
 * 
 * @param old_md5_context
 *      An opaque handle once returned by iso_md5_start() or iso_md5_clone().
 * @param new_md5_context
 *      Returns the opaque handle to the new MD5 context. Submitted
 *      *md5_context must be NULL or point to freeable memory.
 * @return
 *      1= success , <0 indicates error
 *
 * @since 0.6.22
 */
int iso_md5_clone(void *old_md5_context, void **new_md5_context);

/**
 * Obtain the MD5 checksum from a MD5 computation context and dispose this
 * context. (If you want to keep the context then call iso_md5_clone() and
 * apply iso_md5_end() to the clone.)
 *
 * @param md5_context
 *      A pointer to an opaque handle once returned by iso_md5_start() or
 *      iso_md5_clone(). *md5_context will be set to NULL in this call.
 * @param result
 *      Gets filled with the 16 bytes of MD5 checksum.
 * @return
 *      1= success , <0 indicates error
 *
 * @since 0.6.22
 */
int iso_md5_end(void **md5_context, char result[16]);

/**
 * Inquire whether two MD5 checksums match. (This is trivial but such a call
 * is convenient and completes the interface.)
 * @param first_md5
 *      A MD5 byte string as returned by iso_md5_end()
 * @param second_md5
 *      A MD5 byte string as returned by iso_md5_end()
 * @return
 *      1= match , 0= mismatch
 *
 * @since 0.6.22
 */
int iso_md5_match(char first_md5[16], char second_md5[16]);


/* -------------------------------- For HFS+ ------------------------------- */


/** 
 * HFS+ attributes which may be attached to IsoNode objects as data parameter
 * of iso_node_add_xinfo(). As parameter proc use iso_hfsplus_xinfo_func().
 * Create instances of this struct by iso_hfsplus_xinfo_new().
 *
 * @since 1.2.4
 */
struct iso_hfsplus_xinfo_data {

  /* Currently set to 0 by iso_hfsplus_xinfo_new() */
  int version;

  /* Attributes available with version 0.
   * See: http://en.wikipedia.org/wiki/Creator_code , .../Type_code
   * @since 1.2.4
  */
  uint8_t creator_code[4];
  uint8_t type_code[4];
};

/** 
 * The function that is used to mark struct iso_hfsplus_xinfo_data at IsoNodes
 * and finally disposes such structs when their IsoNodes get disposed.
 * Usually an application does not call this function, but only uses it as
 * parameter of xinfo calls like iso_node_add_xinfo() or iso_node_get_xinfo().
 *
 * @since 1.2.4
 */
int iso_hfsplus_xinfo_func(void *data, int flag);

/** 
 * Create an instance of struct iso_hfsplus_xinfo_new().
 *
 * @param flag
 *      Bitfield for control purposes. Unused yet. Submit 0.
 * @return
 *      A pointer to the new object
 *      NULL indicates failure to allocate memory
 *
 * @since 1.2.4
 */
struct iso_hfsplus_xinfo_data *iso_hfsplus_xinfo_new(int flag);


/**
 * HFS+ blessings are relationships between HFS+ enhanced ISO images and
 * particular files in such images. Except for ISO_HFSPLUS_BLESS_INTEL_BOOTFILE
 * and ISO_HFSPLUS_BLESS_MAX, these files have to be directories.
 * No file may have more than one blessing. Each blessing can only be issued
 * to one file.
 *
 * @since 1.2.4
 */
enum IsoHfsplusBlessings {
    /* The blessing that is issued by mkisofs option -hfs-bless. */
    ISO_HFSPLUS_BLESS_PPC_BOOTDIR,

    /* To be applied to a data file */
    ISO_HFSPLUS_BLESS_INTEL_BOOTFILE,

    /* Further blessings for directories */
    ISO_HFSPLUS_BLESS_SHOWFOLDER,
    ISO_HFSPLUS_BLESS_OS9_FOLDER,
    ISO_HFSPLUS_BLESS_OSX_FOLDER,

    /* Not a blessing, but telling the number of blessings in this list */
    ISO_HFSPLUS_BLESS_MAX
};

/**
 * Issue a blessing to a particular IsoNode. If the blessing is already issued
 * to some file, then it gets revoked from that one.
 * 
 * @param image
 *     The image to manipulate.
 * @param blessing
 *      The kind of blessing to be issued.
 * @param node
 *      The file that shall be blessed. It must actually be an IsoDir or
 *      IsoFile as is appropriate for the kind of blessing. (See above enum.)
 *      The node may not yet bear a blessing other than the desired one.
 *      If node is NULL, then the blessing will be revoked from any node
 *      which bears it.
 * @param flag
 *      Bitfield for control purposes.
 *        bit0= Revoke blessing if node != NULL bears it.
 *        bit1= Revoke any blessing of the node, regardless of parameter
 *              blessing. If node is NULL, then revoke all blessings in
 *              the image.
 * @return
 *      1 means successful blessing or revokation of an existing blessing.
 *      0 means the node already bears another blessing, or is of wrong type,
 *        or that the node was not blessed and revokation was desired.
 *      <0 is one of the listed error codes.
 *
 * @since 1.2.4
 */
int iso_image_hfsplus_bless(IsoImage *img, enum IsoHfsplusBlessings blessing,
                            IsoNode *node, int flag);

/**
 * Get the array of nodes which are currently blessed.
 * Array indice correspond to enum IsoHfsplusBlessings.
 * Array element value NULL means that no node bears that blessing.
 *
 * Several usage restrictions apply. See parameter blessed_nodes.
 *
 * @param image
 *     The image to inquire.
 * @param blessed_nodes
 *     Will return a pointer to an internal node array of image.
 *     This pointer is valid only as long as image exists and only until
 *     iso_image_hfsplus_bless() gets used to manipulate the blessings.
 *     Do not free() this array. Do not alter the content of the array
 *     directly, but rather use iso_image_hfsplus_bless() and re-inquire
 *     by iso_image_hfsplus_get_blessed().
 *     This call does not impose an extra reference on the nodes in the
 *     array. So do not iso_node_unref() them.
 *     Nodes listed here are not necessarily grafted into the tree of
 *     the IsoImage.
 * @param bless_max
 *     Will return the number of elements in the array.
 *     It is unlikely but not outruled that it will be larger than
 *     ISO_HFSPLUS_BLESS_MAX in this libisofs.h file.
 * @param flag
 *      Bitfield for control purposes. Submit 0.
 * @return
 *      1 means success, <0 means error
 *
 * @since 1.2.4
 */
int iso_image_hfsplus_get_blessed(IsoImage *img, IsoNode ***blessed_nodes,
                                  int *bless_max, int flag);


/************ Error codes and return values for libisofs ********************/

/** successfully execution */
#define ISO_SUCCESS                     1

/**
 * special return value, it could be or not an error depending on the
 * context.
 */
#define ISO_NONE                        0

/** Operation canceled (FAILURE,HIGH, -1) */
#define ISO_CANCELED                    0xE830FFFF

/** Unknown or unexpected fatal error (FATAL,HIGH, -2) */
#define ISO_FATAL_ERROR                 0xF030FFFE

/** Unknown or unexpected error (FAILURE,HIGH, -3) */
#define ISO_ERROR                       0xE830FFFD

/** Internal programming error. Please report this bug (FATAL,HIGH, -4) */
#define ISO_ASSERT_FAILURE              0xF030FFFC

/**
 * NULL pointer as value for an arg. that doesn't allow NULL (FAILURE,HIGH, -5)
 */
#define ISO_NULL_POINTER                0xE830FFFB

/** Memory allocation error (FATAL,HIGH, -6) */
#define ISO_OUT_OF_MEM                  0xF030FFFA

/** Interrupted by a signal (FATAL,HIGH, -7) */
#define ISO_INTERRUPTED                 0xF030FFF9

/** Invalid parameter value (FAILURE,HIGH, -8) */
#define ISO_WRONG_ARG_VALUE             0xE830FFF8

/** Can't create a needed thread (FATAL,HIGH, -9) */
#define ISO_THREAD_ERROR                0xF030FFF7

/** Write error (FAILURE,HIGH, -10) */
#define ISO_WRITE_ERROR                 0xE830FFF6

/** Buffer read error (FAILURE,HIGH, -11) */
#define ISO_BUF_READ_ERROR              0xE830FFF5

/** Trying to add to a dir a node already added to a dir (FAILURE,HIGH, -64) */
#define ISO_NODE_ALREADY_ADDED          0xE830FFC0

/** Node with same name already exists (FAILURE,HIGH, -65) */
#define ISO_NODE_NAME_NOT_UNIQUE        0xE830FFBF

/** Trying to remove a node that was not added to dir (FAILURE,HIGH, -65) */
#define ISO_NODE_NOT_ADDED_TO_DIR       0xE830FFBE

/** A requested node does not exist  (FAILURE,HIGH, -66) */
#define ISO_NODE_DOESNT_EXIST           0xE830FFBD

/**
 * Try to set the boot image of an already bootable image (FAILURE,HIGH, -67)
 */
#define ISO_IMAGE_ALREADY_BOOTABLE      0xE830FFBC

/** Trying to use an invalid file as boot image (FAILURE,HIGH, -68) */
#define ISO_BOOT_IMAGE_NOT_VALID        0xE830FFBB

/** Too many boot images (FAILURE,HIGH, -69) */
#define ISO_BOOT_IMAGE_OVERFLOW         0xE830FFBA

/** No boot catalog created yet ((FAILURE,HIGH, -70) */ /* @since 0.6.34 */
#define ISO_BOOT_NO_CATALOG             0xE830FFB9


/**
 * Error on file operation (FAILURE,HIGH, -128)
 * (take a look at more specified error codes below)
 */
#define ISO_FILE_ERROR                  0xE830FF80

/** Trying to open an already opened file (FAILURE,HIGH, -129) */
#define ISO_FILE_ALREADY_OPENED         0xE830FF7F

/* @deprecated use ISO_FILE_ALREADY_OPENED instead */
#define ISO_FILE_ALREADY_OPENNED        0xE830FF7F

/** Access to file is not allowed (FAILURE,HIGH, -130) */
#define ISO_FILE_ACCESS_DENIED          0xE830FF7E

/** Incorrect path to file (FAILURE,HIGH, -131) */
#define ISO_FILE_BAD_PATH               0xE830FF7D

/** The file does not exist in the filesystem (FAILURE,HIGH, -132) */
#define ISO_FILE_DOESNT_EXIST           0xE830FF7C

/** Trying to read or close a file not openned (FAILURE,HIGH, -133) */
#define ISO_FILE_NOT_OPENED             0xE830FF7B

/* @deprecated use ISO_FILE_NOT_OPENED instead */
#define ISO_FILE_NOT_OPENNED            ISO_FILE_NOT_OPENED

/** Directory used where no dir is expected (FAILURE,HIGH, -134) */
#define ISO_FILE_IS_DIR                 0xE830FF7A

/** Read error (FAILURE,HIGH, -135) */
#define ISO_FILE_READ_ERROR             0xE830FF79

/** Not dir used where a dir is expected (FAILURE,HIGH, -136) */
#define ISO_FILE_IS_NOT_DIR             0xE830FF78

/** Not symlink used where a symlink is expected (FAILURE,HIGH, -137) */
#define ISO_FILE_IS_NOT_SYMLINK         0xE830FF77

/** Can't seek to specified location (FAILURE,HIGH, -138) */
#define ISO_FILE_SEEK_ERROR             0xE830FF76

/** File not supported in ECMA-119 tree and thus ignored (WARNING,MEDIUM, -139) */
#define ISO_FILE_IGNORED                0xD020FF75

/* A file is bigger than supported by used standard  (WARNING,MEDIUM, -140) */
#define ISO_FILE_TOO_BIG                0xD020FF74

/* File read error during image creation (MISHAP,HIGH, -141) */
#define ISO_FILE_CANT_WRITE             0xE430FF73

/* Can't convert filename to requested charset (WARNING,MEDIUM, -142) */
#define ISO_FILENAME_WRONG_CHARSET      0xD020FF72
/* This was once a HINT. Deprecated now. */
#define ISO_FILENAME_WRONG_CHARSET_OLD  0xC020FF72

/* File can't be added to the tree (SORRY,HIGH, -143) */
#define ISO_FILE_CANT_ADD               0xE030FF71

/**
 * File path break specification constraints and will be ignored
 * (WARNING,MEDIUM, -144)
 */
#define ISO_FILE_IMGPATH_WRONG          0xD020FF70

/**
 * Offset greater than file size (FAILURE,HIGH, -150)
 * @since 0.6.4
 */
#define ISO_FILE_OFFSET_TOO_BIG         0xE830FF6A


/** Charset conversion error (FAILURE,HIGH, -256) */
#define ISO_CHARSET_CONV_ERROR          0xE830FF00

/**
 * Too many files to mangle, i.e. we cannot guarantee unique file names
 * (FAILURE,HIGH, -257)
 */
#define ISO_MANGLE_TOO_MUCH_FILES       0xE830FEFF

/* image related errors */

/**
 * Wrong or damaged Primary Volume Descriptor (FAILURE,HIGH, -320)
 * This could mean that the file is not a valid ISO image.
 */
#define ISO_WRONG_PVD                   0xE830FEC0

/** Wrong or damaged RR entry (SORRY,HIGH, -321) */
#define ISO_WRONG_RR                    0xE030FEBF

/** Unsupported RR feature (SORRY,HIGH, -322) */
#define ISO_UNSUPPORTED_RR              0xE030FEBE

/** Wrong or damaged ECMA-119 (FAILURE,HIGH, -323) */
#define ISO_WRONG_ECMA119               0xE830FEBD

/** Unsupported ECMA-119 feature (FAILURE,HIGH, -324) */
#define ISO_UNSUPPORTED_ECMA119         0xE830FEBC

/** Wrong or damaged El-Torito catalog (WARN,HIGH, -325) */
#define ISO_WRONG_EL_TORITO             0xD030FEBB

/** Unsupported El-Torito feature (WARN,HIGH, -326) */
#define ISO_UNSUPPORTED_EL_TORITO       0xD030FEBA

/** Can't patch an isolinux boot image (SORRY,HIGH, -327) */
#define ISO_ISOLINUX_CANT_PATCH         0xE030FEB9

/** Unsupported SUSP feature (SORRY,HIGH, -328) */
#define ISO_UNSUPPORTED_SUSP            0xE030FEB8

/** Error on a RR entry that can be ignored (WARNING,HIGH, -329) */
#define ISO_WRONG_RR_WARN               0xD030FEB7

/** Error on a RR entry that can be ignored (HINT,MEDIUM, -330) */
#define ISO_SUSP_UNHANDLED              0xC020FEB6

/** Multiple ER SUSP entries found (WARNING,HIGH, -331) */
#define ISO_SUSP_MULTIPLE_ER            0xD030FEB5

/** Unsupported volume descriptor found (HINT,MEDIUM, -332) */
#define ISO_UNSUPPORTED_VD              0xC020FEB4

/** El-Torito related warning (WARNING,HIGH, -333) */
#define ISO_EL_TORITO_WARN              0xD030FEB3

/** Image write cancelled (MISHAP,HIGH, -334) */
#define ISO_IMAGE_WRITE_CANCELED        0xE430FEB2

/** El-Torito image is hidden (WARNING,HIGH, -335) */
#define ISO_EL_TORITO_HIDDEN            0xD030FEB1


/** AAIP info with ACL or xattr in ISO image will be ignored
                                                          (NOTE, HIGH, -336) */
#define ISO_AAIP_IGNORED          0xB030FEB0

/** Error with decoding ACL from AAIP info (FAILURE, HIGH, -337) */
#define ISO_AAIP_BAD_ACL          0xE830FEAF

/** Error with encoding ACL for AAIP (FAILURE, HIGH, -338) */
#define ISO_AAIP_BAD_ACL_TEXT     0xE830FEAE

/** AAIP processing for ACL or xattr not enabled at compile time
                                                       (FAILURE, HIGH, -339) */
#define ISO_AAIP_NOT_ENABLED      0xE830FEAD

/** Error with decoding AAIP info for ACL or xattr (FAILURE, HIGH, -340) */
#define ISO_AAIP_BAD_AASTRING     0xE830FEAC

/** Error with reading ACL or xattr from local file (FAILURE, HIGH, -341) */
#define ISO_AAIP_NO_GET_LOCAL     0xE830FEAB

/** Error with attaching ACL or xattr to local file (FAILURE, HIGH, -342) */
#define ISO_AAIP_NO_SET_LOCAL     0xE830FEAA

/** Unallowed attempt to set an xattr with non-userspace name
                                                    (FAILURE, HIGH, -343) */
#define ISO_AAIP_NON_USER_NAME    0xE830FEA9

/** Too many references on a single IsoExternalFilterCommand
                                                    (FAILURE, HIGH, -344) */
#define ISO_EXTF_TOO_OFTEN        0xE830FEA8

/** Use of zlib was not enabled at compile time (FAILURE, HIGH, -345) */
#define ISO_ZLIB_NOT_ENABLED      0xE830FEA7

/** Cannot apply zisofs filter to file >= 4 GiB  (FAILURE, HIGH, -346) */
#define ISO_ZISOFS_TOO_LARGE      0xE830FEA6

/** Filter input differs from previous run  (FAILURE, HIGH, -347) */
#define ISO_FILTER_WRONG_INPUT    0xE830FEA5

/** zlib compression/decompression error  (FAILURE, HIGH, -348) */
#define ISO_ZLIB_COMPR_ERR        0xE830FEA4

/** Input stream is not in zisofs format  (FAILURE, HIGH, -349) */
#define ISO_ZISOFS_WRONG_INPUT    0xE830FEA3

/** Cannot set global zisofs parameters while filters exist
                                                       (FAILURE, HIGH, -350) */
#define ISO_ZISOFS_PARAM_LOCK     0xE830FEA2

/** Premature EOF of zlib input stream  (FAILURE, HIGH, -351) */
#define ISO_ZLIB_EARLY_EOF        0xE830FEA1

/**
 * Checksum area or checksum tag appear corrupted  (WARNING,HIGH, -352)
 * @since 0.6.22
*/
#define ISO_MD5_AREA_CORRUPTED    0xD030FEA0

/**
 * Checksum mismatch between checksum tag and data blocks
 * (FAILURE, HIGH, -353)
 * @since 0.6.22
*/
#define ISO_MD5_TAG_MISMATCH      0xE830FE9F

/**
 * Checksum mismatch in System Area, Volume Descriptors, or directory tree.
 * (FAILURE, HIGH, -354)
 * @since 0.6.22
*/
#define ISO_SB_TREE_CORRUPTED     0xE830FE9E

/**
 * Unexpected checksum tag type encountered.   (WARNING, HIGH, -355)
 * @since 0.6.22
*/
#define ISO_MD5_TAG_UNEXPECTED    0xD030FE9D

/**
 * Misplaced checksum tag encountered. (WARNING, HIGH, -356)
 * @since 0.6.22
*/
#define ISO_MD5_TAG_MISPLACED     0xD030FE9C

/**
 * Checksum tag with unexpected address range encountered.
 * (WARNING, HIGH, -357)
 * @since 0.6.22
*/
#define ISO_MD5_TAG_OTHER_RANGE   0xD030FE9B

/**
 * Detected file content changes while it was written into the image.
 * (MISHAP, HIGH, -358)
 * @since 0.6.22
*/
#define ISO_MD5_STREAM_CHANGE     0xE430FE9A

/**
 * Session does not start at LBA 0. scdbackup checksum tag not written.
 * (WARNING, HIGH, -359)
 * @since 0.6.24
*/
#define ISO_SCDBACKUP_TAG_NOT_0   0xD030FE99

/**
 * The setting of iso_write_opts_set_ms_block() leaves not enough room
 * for the prescibed size of iso_write_opts_set_overwrite_buf().
 * (FAILURE, HIGH, -360)
 * @since 0.6.36
 */
#define ISO_OVWRT_MS_TOO_SMALL    0xE830FE98

/**
 * The partition offset is not 0 and leaves not not enough room for
 * system area, volume descriptors, and checksum tags of the first tree.
 * (FAILURE, HIGH, -361)
 */
#define ISO_PART_OFFST_TOO_SMALL   0xE830FE97

/**
 * The ring buffer is smaller than 64 kB + partition offset.
 * (FAILURE, HIGH, -362)
 */
#define ISO_OVWRT_FIFO_TOO_SMALL   0xE830FE96

/** Use of libjte was not enabled at compile time (FAILURE, HIGH, -363) */
#define ISO_LIBJTE_NOT_ENABLED     0xE830FE95

/** Failed to start up Jigdo Template Extraction (FAILURE, HIGH, -364) */
#define ISO_LIBJTE_START_FAILED    0xE830FE94

/** Failed to finish Jigdo Template Extraction (FAILURE, HIGH, -365) */
#define ISO_LIBJTE_END_FAILED      0xE830FE93

/** Failed to process file for Jigdo Template Extraction
   (MISHAP, HIGH, -366) */
#define ISO_LIBJTE_FILE_FAILED     0xE430FE92

/** Too many MIPS Big Endian boot files given (max. 15) (FAILURE, HIGH, -367)*/
#define ISO_BOOT_TOO_MANY_MIPS     0xE830FE91

/** Boot file missing in image (MISHAP, HIGH, -368) */
#define ISO_BOOT_FILE_MISSING      0xE430FE90

/** Partition number out of range (FAILURE, HIGH, -369) */
#define ISO_BAD_PARTITION_NO       0xE830FE8F

/** Cannot open data file for appended partition (FAILURE, HIGH, -370) */
#define ISO_BAD_PARTITION_FILE     0xE830FE8E

/** May not combine MBR partition with non-MBR system area
                                                       (FAILURE, HIGH, -371) */
#define ISO_NON_MBR_SYS_AREA       0xE830FE8D

/** Displacement offset leads outside 32 bit range (FAILURE, HIGH, -372) */
#define ISO_DISPLACE_ROLLOVER      0xE830FE8C

/** File name cannot be written into ECMA-119 untranslated
                                                       (FAILURE, HIGH, -373) */
#define ISO_NAME_NEEDS_TRANSL      0xE830FE8B

/** Data file input stream object offers no cloning method
                                                       (FAILURE, HIGH, -374) */
#define ISO_STREAM_NO_CLONE        0xE830FE8A

/** Extended information class offers no cloning method
                                                       (FAILURE, HIGH, -375) */
#define ISO_XINFO_NO_CLONE         0xE830FE89

/** Found copied superblock checksum tag  (WARNING, HIGH, -376) */
#define ISO_MD5_TAG_COPIED         0xD030FE88

/** Rock Ridge leaf name too long (FAILURE, HIGH, -377) */
#define ISO_RR_NAME_TOO_LONG       0xE830FE87

/** Reserved Rock Ridge leaf name  (FAILURE, HIGH, -378) */
#define ISO_RR_NAME_RESERVED       0xE830FE86

/** Rock Ridge path too long  (FAILURE, HIGH, -379) */
#define ISO_RR_PATH_TOO_LONG       0xE830FE85

/** Attribute name cannot be represented  (FAILURE, HIGH, -380) */
#define ISO_AAIP_BAD_ATTR_NAME      0xE830FE84

/** ACL text contains multiple entries of user::, group::, other::
                                                     (FAILURE, HIGH, -381)  */
#define ISO_AAIP_ACL_MULT_OBJ       0xE830FE83

/** File sections do not form consecutive array of blocks
                                                     (FAILURE, HIGH, -382) */
#define ISO_SECT_SCATTERED          0xE830FE82

/** Too many Apple Partition Map entries requested (FAILURE, HIGH, -383) */
#define ISO_BOOT_TOO_MANY_APM       0xE830FE81

/** Overlapping Apple Partition Map entries requested (FAILURE, HIGH, -384) */
#define ISO_BOOT_APM_OVERLAP        0xE830FE80

/** Too many GPT entries requested (FAILURE, HIGH, -385) */
#define ISO_BOOT_TOO_MANY_GPT       0xE830FE7F

/** Overlapping GPT entries requested (FAILURE, HIGH, -386) */
#define ISO_BOOT_GPT_OVERLAP        0xE830FE7E

/** Too many MBR partition entries requested (FAILURE, HIGH, -387) */
#define ISO_BOOT_TOO_MANY_MBR       0xE830FE7D

/** Overlapping MBR partition entries requested (FAILURE, HIGH, -388) */
#define ISO_BOOT_MBR_OVERLAP        0xE830FE7C

/** Attempt to use an MBR partition entry twice (FAILURE, HIGH, -389) */
#define ISO_BOOT_MBR_COLLISION      0xE830FE7B

/** No suitable El Torito EFI boot image for exposure as GPT partition
                                                       (FAILURE, HIGH, -390) */
#define ISO_BOOT_NO_EFI_ELTO        0xE830FE7A

/** Not a supported HFS+ or APM block size  (FAILURE, HIGH, -391) */
#define ISO_BOOT_HFSP_BAD_BSIZE     0xE830FE79

/** APM block size prevents coexistence with GPT  (FAILURE, HIGH, -392) */
#define ISO_BOOT_APM_GPT_BSIZE      0xE830FE78

/** Name collision in HFS+, mangling not possible  (FAILURE, HIGH, -393) */
#define ISO_HFSP_NO_MANGLE          0xE830FE77

/** Symbolic link cannot be resolved               (FAILURE, HIGH, -394) */
#define ISO_DEAD_SYMLINK            0xE830FE76

/** Too many chained symbolic links                (FAILURE, HIGH, -395) */
#define ISO_DEEP_SYMLINK            0xE830FE75

/** Unrecognized file type in ISO image            (FAILURE, HIGH, -396) */
#define ISO_BAD_ISO_FILETYPE        0xE830FE74


/* Internal developer note: 
   Place new error codes directly above this comment. 
   Newly introduced errors must get a message entry in
   libisofs/messages.c, function iso_error_to_msg()
*/

/* ! PLACE NEW ERROR CODES ABOVE. NOT AFTER THIS LINE ! */


/** Read error occured with IsoDataSource (SORRY,HIGH, -513) */
#define ISO_DATA_SOURCE_SORRY     0xE030FCFF

/** Read error occured with IsoDataSource (MISHAP,HIGH, -513) */
#define ISO_DATA_SOURCE_MISHAP    0xE430FCFF

/** Read error occured with IsoDataSource (FAILURE,HIGH, -513) */
#define ISO_DATA_SOURCE_FAILURE   0xE830FCFF

/** Read error occured with IsoDataSource (FATAL,HIGH, -513) */
#define ISO_DATA_SOURCE_FATAL     0xF030FCFF


/* ! PLACE NEW ERROR CODES SEVERAL LINES ABOVE. NOT HERE ! */


/* ------------------------------------------------------------------------- */

#ifdef LIBISOFS_WITHOUT_LIBBURN

/**
    This is a copy from the API of libburn-0.6.0 (under GPL).
    It is supposed to be as stable as any overall include of libburn.h.
    I.e. if this definition is out of sync then you cannot rely on any
    contract that was made with libburn.h.

    Libisofs does not need to be linked with libburn at all. But if it is
    linked with libburn then it must be libburn-0.4.2 or later.

    An application that provides own struct burn_source objects and does not
    include libburn/libburn.h has to define LIBISOFS_WITHOUT_LIBBURN before
    including libisofs/libisofs.h in order to make this copy available.
*/ 


/** Data source interface for tracks.
    This allows to use arbitrary program code as provider of track input data.

    Objects compliant to this interface are either provided by the application
    or by API calls of libburn: burn_fd_source_new(), burn_file_source_new(),
    and burn_fifo_source_new().

    libisofs acts as "application" and implements an own class of burn_source.
    Instances of that class are handed out by iso_image_create_burn_source().

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

#endif /* LIBISOFS_WITHOUT_LIBBURN */

/* ----------------------------- Bug Fixes ----------------------------- */

/* currently none being tested */


/* ---------------------------- Improvements --------------------------- */

/* currently none being tested */


/* ---------------------------- Experiments ---------------------------- */


/* Experiment: Write obsolete RR entries with Rock Ridge.
               I suspect Solaris wants to see them.
               DID NOT HELP: Solaris knows only RRIP_1991A.

 #define Libisofs_with_rrip_rR yes
*/


#endif /*LIBISO_LIBISOFS_H_*/
