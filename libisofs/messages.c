/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2009 - 2011 Thomas Schmitt
 *
 * This file is part of the libisofs project; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef Xorriso_standalonE

#ifdef Xorriso_with_libjtE
#include "../libjte/libjte.h"
#endif

#else

#ifdef Libisofs_with_libjtE
#include <libjte/libjte.h>
#endif

#endif /* ! Xorriso_standalonE */

#include "libiso_msgs.h"
#include "libisofs.h"
#include "messages.h"

#include "util.h"
#include "node.h"


/*
 * error codes are 32 bit numbers, that follow the following conventions:
 *
 * bit  31 (MSB) -> 1 (to make the value always negative)
 * bits 30-24 -> Encoded severity (Use ISO_ERR_SEV to translate an error code
 *               to a LIBISO_MSGS_SEV_* constant)
 *        = 0x10 -> DEBUG
 *        = 0x20 -> UPDATE
 *        = 0x30 -> NOTE
 *        = 0x40 -> HINT
 *        = 0x50 -> WARNING
 *        = 0x60 -> SORRY
 *        = 0x64 -> MISHAP
 *        = 0x68 -> FAILURE
 *        = 0x70 -> FATAL
 *        = 0x71 -> ABORT
 * bits 23-20 -> Encoded priority (Use ISO_ERR_PRIO to translate an error code
 *               to a LIBISO_MSGS_PRIO_* constant)
 *        = 0x0 -> ZERO
 *        = 0x1 -> LOW
 *        = 0x2 -> MEDIUM
 *        = 0x3 -> HIGH
 * bits 19-16 -> Reserved for future usage (maybe message ranges)
 * bits 15-0  -> Error code
 */
#define ISO_ERR_SEV(e)      (e & 0x7F000000)
#define ISO_ERR_PRIO(e)     ((e & 0x00700000) << 8)
#define ISO_ERR_CODE(e)     ((e & 0x0000FFFF) | 0x00030000)

int iso_message_id = LIBISO_MSGS_ORIGIN_IMAGE_BASE;

/**
 * Threshold for aborting.
 */
int abort_threshold = LIBISO_MSGS_SEV_FAILURE;

#define MAX_MSG_LEN     4096

struct libiso_msgs *libiso_msgr = NULL;


/* ------------- List of xinfo clone functions ----------- */

struct iso_xinfo_cloner_assoc {
    iso_node_xinfo_func proc;
    iso_node_xinfo_cloner cloner;
    struct iso_xinfo_cloner_assoc *next;
};

struct iso_xinfo_cloner_assoc *iso_xinfo_cloner_list = NULL;

/* API */
int iso_node_xinfo_make_clonable(iso_node_xinfo_func proc,
                                 iso_node_xinfo_cloner cloner, int flag)
{
    struct iso_xinfo_cloner_assoc *assoc;

    /* Look for existing assoc of proc */
    for (assoc = iso_xinfo_cloner_list; assoc != NULL; assoc = assoc->next) 
        if (assoc->proc == proc)
    break;

    if (assoc == NULL) {
        assoc = calloc(1, sizeof(struct iso_xinfo_cloner_assoc));
        if (assoc == NULL)
            return ISO_OUT_OF_MEM;
        assoc->proc = proc;
        assoc->next = iso_xinfo_cloner_list;
        iso_xinfo_cloner_list = assoc;
    }
    assoc->cloner = cloner;
    return ISO_SUCCESS;
}

/* API */
int iso_node_xinfo_get_cloner(iso_node_xinfo_func proc,
                              iso_node_xinfo_cloner *cloner, int flag)
{
    struct iso_xinfo_cloner_assoc *assoc;

    *cloner = NULL;
    for (assoc = iso_xinfo_cloner_list; assoc != NULL; assoc = assoc->next) {
        if (assoc->proc != proc)
    continue;
        *cloner = assoc->cloner;
        return 1;
    }
    return 0;
}

static
int iso_node_xinfo_dispose_cloners(int flag)
{
    struct iso_xinfo_cloner_assoc *assoc, *next;

    for (assoc = iso_xinfo_cloner_list; assoc != NULL; assoc = next) {
        next = assoc->next;
        free((char *) assoc);
    }
    return(1);
}

/* ------------- End of xinfo clone functions list ----------- */


/*
  @param flag  bit0= do not set up locale by LC_* environment variables
*/
int iso_init_with_flag(int flag)
{
    int ret;

#ifdef Libisofs_with_libjtE

 /* Ugly compile time check for header version compatibility.
    If everthing matches, then it produces no C code. In case of mismatch,
    intentionally faulty C code will be inserted.
 */ 
 /* The indendation is an advise of man gcc to help old compilers ignoring */
 #if iso_libjte_req_major > LIBJTE_VERSION_MAJOR
 #define Libisofs_libjte_dot_h_too_olD 1
 #endif
 #if iso_libjte_req_major == LIBJTE_VERSION_MAJOR && iso_libjte_req_minor > LIBJTE_VERSION_MINOR
 #define Libisofs_libjte_dot_h_too_olD 1
 #endif
 #if iso_libjte_req_minor == LIBJTE_VERSION_MINOR && iso_libjte_req_micro > LIBJTE_VERSION_MICRO
 #define Libisofs_libjte_dot_h_too_olD 1
 #endif

#ifdef Libisofs_libjte_dot_h_too_olD
LIBJTE_MISCONFIGURATION = 0;
INTENTIONAL_ABORT_OF_COMPILATION__HEADERFILE_libjte_dot_h_TOO_OLD__SEE_libisofs_dot_h_AND_messages_c = 0;
LIBJTE_MISCONFIGURATION_ = 0;
#endif

    if (! libjte__is_compatible(LIBJTE_VERSION_MAJOR, LIBJTE_VERSION_MINOR,
                                LIBJTE_VERSION_MICRO, 0)) {
        fprintf(stderr,
              "\nlibisofs: libjte TOO OLD ! Need at least libjte-%d.%d.%d\n\n",
              LIBJTE_VERSION_MAJOR, LIBJTE_VERSION_MINOR,
              LIBJTE_VERSION_MICRO);
        return ISO_FATAL_ERROR;
    }

#endif /* Libisofs_with_libjtE */

    if (! (flag & 1)) {
        iso_init_locale(0);
    }
    if (libiso_msgr == NULL) {
        if (libiso_msgs_new(&libiso_msgr, 0) <= 0)
            return ISO_FATAL_ERROR;
    }
    libiso_msgs_set_severities(libiso_msgr, LIBISO_MSGS_SEV_NEVER,
                   LIBISO_MSGS_SEV_FATAL, "libisofs: ", 0);

    ret = iso_node_xinfo_make_clonable(aaip_xinfo_func, aaip_xinfo_cloner, 0);
    if (ret < 0)
        return ret;
    ret = iso_node_xinfo_make_clonable(checksum_cx_xinfo_func,
                                       checksum_cx_xinfo_cloner, 0);
    if (ret < 0)
        return ret;
    ret = iso_node_xinfo_make_clonable(checksum_md5_xinfo_func,
                                       checksum_md5_xinfo_cloner, 0);
    if (ret < 0)
        return ret;
    ret = iso_node_xinfo_make_clonable(zisofs_zf_xinfo_func,
                                       zisofs_zf_xinfo_cloner, 0);
    if (ret < 0)
        return ret;
    ret = iso_node_xinfo_make_clonable(iso_px_ino_xinfo_func,
                                       iso_px_ino_xinfo_cloner, 0);
    if (ret < 0)
        return ret;
    ret = iso_node_xinfo_make_clonable(iso_hfsplus_xinfo_func,
                                       iso_hfsplus_xinfo_cloner, 0);
    if (ret < 0)
        return ret;
    return 1;
}

int iso_init()
{
    return iso_init_with_flag(0);
}

void iso_finish()
{
    libiso_msgs_destroy(&libiso_msgr, 0);
    iso_node_xinfo_dispose_cloners(0);
}

int iso_set_abort_severity(char *severity)
{
    int ret, sevno;

    ret = libiso_msgs__text_to_sev(severity, &sevno, 0);
    if (ret <= 0)
        return ISO_WRONG_ARG_VALUE;
    if (sevno > LIBISO_MSGS_SEV_FAILURE || sevno < LIBISO_MSGS_SEV_NOTE)
        return ISO_WRONG_ARG_VALUE;
    ret = abort_threshold;
    abort_threshold = sevno;
    return ret;
}

void iso_msg_debug(int imgid, const char *fmt, ...)
{
    char *msg = NULL;
    va_list ap;

    LIBISO_ALLOC_MEM_VOID(msg, char, MAX_MSG_LEN);
    va_start(ap, fmt);
    vsnprintf(msg, MAX_MSG_LEN, fmt, ap);
    va_end(ap);

    libiso_msgs_submit(libiso_msgr, imgid, 0x00000002, LIBISO_MSGS_SEV_DEBUG,
                       LIBISO_MSGS_PRIO_ZERO, msg, 0, 0);
ex:;
    LIBISO_FREE_MEM(msg);
}

const char *iso_error_to_msg(int errcode)
{
    switch(errcode) {
    case ISO_CANCELED:
        return "Operation canceled";
    case ISO_FATAL_ERROR:
        return "Unknown or unexpected fatal error";
    case ISO_ERROR:
        return "Unknown or unexpected error";
    case ISO_ASSERT_FAILURE:
        return "Internal programming error. Please report this bug";
    case ISO_NULL_POINTER:
        return "NULL pointer as value for an arg. that does not allow NULL";
    case ISO_OUT_OF_MEM:
        return "Memory allocation error";
    case ISO_INTERRUPTED:
        return "Interrupted by a signal";
    case ISO_WRONG_ARG_VALUE:
        return "Invalid parameter value";
    case ISO_THREAD_ERROR:
        return "Cannot create a needed thread";
    case ISO_WRITE_ERROR:
        return "Write error";
    case ISO_BUF_READ_ERROR:
        return "Buffer read error";
    case ISO_NODE_ALREADY_ADDED:
        return "Trying to add to a dir a node already added to a dir";
    case ISO_NODE_NAME_NOT_UNIQUE:
        return "Node with same name already exists";
    case ISO_NODE_NOT_ADDED_TO_DIR:
        return "Trying to remove a node that was not added to dir";
    case ISO_NODE_DOESNT_EXIST:
        return "A requested node does not exist";
    case ISO_IMAGE_ALREADY_BOOTABLE:
        return "Try to set the boot image of an already bootable image";
    case ISO_BOOT_IMAGE_NOT_VALID:
        return "Trying to use an invalid file as boot image";
    case ISO_BOOT_IMAGE_OVERFLOW:
        return "Too many boot images added";
    case ISO_FILE_ERROR:
        return "Error on file operation";
    case ISO_FILE_ALREADY_OPENED:
        return "Trying to open an already opened file";
    case ISO_FILE_ACCESS_DENIED:
        return "Access to file is not allowed";
    case ISO_FILE_BAD_PATH:
        return "Incorrect path to file";
    case ISO_FILE_DOESNT_EXIST:
        return "The file does not exist in the filesystem";
    case ISO_FILE_NOT_OPENED:
        return "Trying to read or close a file not opened";
    case ISO_FILE_IS_DIR:
        return "Directory used where no dir is expected";
    case ISO_FILE_READ_ERROR:
        return "Read error";
    case ISO_FILE_IS_NOT_DIR:
        return "Not dir used where a dir is expected";
    case ISO_FILE_IS_NOT_SYMLINK:
        return "Not symlink used where a symlink is expected";
    case ISO_FILE_SEEK_ERROR:
        return "Cannot seek to specified location";
    case ISO_FILE_IGNORED:
        return "File not supported in ECMA-119 tree and thus ignored";
    case ISO_FILE_TOO_BIG:
        return "A file is bigger than supported by used standard";
    case ISO_FILE_CANT_WRITE:
        return "File read error during image creation";
    case ISO_FILENAME_WRONG_CHARSET:
    case ISO_FILENAME_WRONG_CHARSET_OLD:
        return "Cannot convert filename to requested charset";
    case ISO_FILE_CANT_ADD:
        return "File cannot be added to the tree";
    case ISO_FILE_IMGPATH_WRONG:
        return "File path break specification constraints and will be ignored";
    case ISO_CHARSET_CONV_ERROR:
        return "Charset conversion error";
    case ISO_MANGLE_TOO_MUCH_FILES:
        return "Too much files to mangle, cannot guarantee unique file names";
    case ISO_WRONG_PVD:
        return "Wrong or damaged Primary Volume Descriptor";
    case ISO_WRONG_RR:
        return "Wrong or damaged Rock Ridge entry";
    case ISO_UNSUPPORTED_RR:
        return "Unsupported Rock Ridge feature";
    case ISO_WRONG_ECMA119:
        return "Wrong or damaged ECMA-119";
    case ISO_UNSUPPORTED_ECMA119:
        return "Unsupported ECMA-119 feature";
    case ISO_WRONG_EL_TORITO:
        return "Wrong or damaged El-Torito catalog";
    case ISO_UNSUPPORTED_EL_TORITO:
        return "Unsupported El-Torito feature";
    case ISO_ISOLINUX_CANT_PATCH:
        return "Cannot patch isolinux boot image";
    case ISO_UNSUPPORTED_SUSP:
        return "Unsupported SUSP feature";
    case ISO_WRONG_RR_WARN:
        return "Error on a Rock Ridge entry that can be ignored";
    case ISO_SUSP_UNHANDLED:
        return "Unhandled SUSP entry";
    case ISO_SUSP_MULTIPLE_ER:
        return "Multiple ER SUSP entries found";
    case ISO_UNSUPPORTED_VD:
        return "Unsupported volume descriptor found";
    case ISO_EL_TORITO_WARN:
        return "El-Torito related warning";
    case ISO_IMAGE_WRITE_CANCELED:
        return "Image write cancelled";
    case ISO_EL_TORITO_HIDDEN:
        return "El-Torito image is hidden";
    case ISO_DATA_SOURCE_SORRY:
    case ISO_DATA_SOURCE_MISHAP:
    case ISO_DATA_SOURCE_FAILURE:
    case ISO_DATA_SOURCE_FATAL:
        return "Read error occured with IsoDataSource";
    case ISO_AAIP_IGNORED:
        return "AAIP info with ACL or xattr in ISO image will be ignored";
    case ISO_AAIP_BAD_ACL:
        return "Error with decoding ACL from AAIP info";
    case ISO_AAIP_BAD_ACL_TEXT:
        return "Error with encoding ACL for AAIP";
    case ISO_AAIP_NOT_ENABLED:
        return "AAIP processing for ACL or xattr not enabled at compile time";
    case ISO_AAIP_BAD_AASTRING:
        return "Error with decoding AAIP info for ACL or xattr";
    case ISO_AAIP_NO_GET_LOCAL:
        return "Error with reading ACL or xattr from local file";
    case ISO_AAIP_NO_SET_LOCAL:
        return "Error with attaching ACL or xattr to local file";
    case ISO_AAIP_NON_USER_NAME:
        return "Unallowed attempt to set an xattr with non-userspace name";
    case ISO_EXTF_TOO_OFTEN:
        return "Too many references on a single external filter command";
    case ISO_ZLIB_NOT_ENABLED:
        return "Use of zlib was not enabled at compile time";
    case ISO_ZISOFS_TOO_LARGE:
        return "Cannot apply zisofs filter to file >= 4 GiB";
    case ISO_FILTER_WRONG_INPUT:
        return "Filter input differs from previous run";
    case ISO_ZLIB_COMPR_ERR:
        return "zlib compression/decompression error";
    case ISO_ZISOFS_WRONG_INPUT:
        return "Input stream is not in zisofs format";
    case ISO_ZISOFS_PARAM_LOCK:
        return "Cannot set global zisofs parameters while filters exist";
    case ISO_ZLIB_EARLY_EOF:
        return "Premature EOF of zlib input stream";
    case ISO_MD5_AREA_CORRUPTED:
        return "Checksum area or checksum tag appear corrupted";
    case ISO_MD5_TAG_MISMATCH:
        return "Checksum mismatch between checksum tag and data blocks";
    case ISO_SB_TREE_CORRUPTED:
        return "Checksum mismatch in System Area, Volume Descriptors, or directory tree";
    case ISO_MD5_TAG_UNEXPECTED:
        return "Unexpected checksum tag type encountered";
    case ISO_MD5_TAG_MISPLACED:
        return "Misplaced checksum tag type encountered";
    case ISO_MD5_TAG_OTHER_RANGE:
        return "Checksum tag with unexpected address range encountered";
    case ISO_MD5_STREAM_CHANGE:
        return "Detected file content changes while it was written into the image";
    case ISO_SCDBACKUP_TAG_NOT_0:
        return "Session does not start at LBA 0. scdbackup checksum tag not written.";
    case ISO_BOOT_NO_CATALOG:
        return "No boot catalog created yet";
    case ISO_OVWRT_MS_TOO_SMALL:
        return "Multi-session offset too small for overwrite buffer";
    case ISO_PART_OFFST_TOO_SMALL:
        return "Partition offset too small for first tree root.";
    case ISO_OVWRT_FIFO_TOO_SMALL:
        return "The ring buffer is too small for overwrite buffer";
    case ISO_LIBJTE_NOT_ENABLED:
        return "Use of libjte was not enabled at compile time";
    case ISO_LIBJTE_START_FAILED:
        return "Failed to start up Jigdo Template Extraction";
    case ISO_LIBJTE_END_FAILED:
        return "Failed to finish Jigdo Template Extraction";
    case ISO_LIBJTE_FILE_FAILED:
        return "Failed to process file for Jigdo Template Extraction";
    case ISO_BOOT_TOO_MANY_MIPS:
        return "Too many MIPS Big Endian boot files given (max. 15)";
    case ISO_BOOT_FILE_MISSING:
        return "Boot file missing in image";
    case ISO_BAD_PARTITION_NO:
        return "Partition number out of range";
    case ISO_BAD_PARTITION_FILE:
        return "Cannot open data file for appended partition";
    case ISO_NON_MBR_SYS_AREA:
        return "May not combine MBR partition with non-MBR system area";
    case ISO_DISPLACE_ROLLOVER:
        return "Displacement offset leads outside 32 bit range";
    case ISO_NAME_NEEDS_TRANSL:
        return "File name cannot be written into ECMA-119 untranslated";
    case ISO_STREAM_NO_CLONE:
        return "Data file input stream object offers no cloning method";
    case ISO_XINFO_NO_CLONE:
        return "Extended information class offers no cloning method";
    case ISO_MD5_TAG_COPIED:
        return "Found copied superblock checksum tag";
    case ISO_RR_NAME_TOO_LONG:
        return "Rock Ridge leaf name too long";
    case ISO_RR_NAME_RESERVED:
        return "Reserved Rock Ridge leaf name";
    case ISO_RR_PATH_TOO_LONG:
        return "Rock Ridge path too long";
    case ISO_AAIP_BAD_ATTR_NAME:
        return "Attribute name cannot be represented";
    case ISO_AAIP_ACL_MULT_OBJ:
        return "ACL text contains multiple entries of user::, group::, other::";
    case ISO_SECT_SCATTERED:
        return "File sections do not form consecutive array of blocks";
    case ISO_BOOT_TOO_MANY_APM:
        return "Too many Apple Partition Map entries requested";
    case ISO_BOOT_APM_OVERLAP:
        return "Overlapping Apple Partition Map entries requested";
    case ISO_BOOT_TOO_MANY_GPT:
        return "Too many GPT entries requested";
    case ISO_BOOT_GPT_OVERLAP:
        return "Overlapping GPT entries requested";
    case ISO_BOOT_TOO_MANY_MBR:
        return "Too many MBR partition entries requested";
    case ISO_BOOT_MBR_OVERLAP:
        return "Overlapping MBR partition entries requested";
    case ISO_BOOT_MBR_COLLISION:
        return "Attempt to use an MBR partition entry twice";
    case ISO_BOOT_NO_EFI_ELTO:
        return "No suitable El Torito EFI boot image for exposure as GPT partition";
    case ISO_BOOT_HFSP_BAD_BSIZE:
        return "Not a supported HFS+ or APM block size";
    case ISO_BOOT_APM_GPT_BSIZE:
        return "APM block size prevents coexistence with GPT";
    case ISO_HFSP_NO_MANGLE:
        return "Name collision in HFS+, mangling not possible";
    case ISO_DEAD_SYMLINK:
        return "Symbolic link cannot be resolved";
    case ISO_DEEP_SYMLINK:
        return "Too many chained symbolic links";
    case ISO_BAD_ISO_FILETYPE:
        return "Unrecognized file type in ISO image";
    default:
        return "Unknown error";
    }
}

int iso_msg_is_abort(int errcode)
{
    if (ISO_ERR_SEV(errcode) >= abort_threshold)
        return 1;
    return 0;
}
       

int iso_msg_submit(int imgid, int errcode, int causedby, const char *fmt, ...)
{
    char msg[MAX_MSG_LEN];
    va_list ap;

    /* when called with ISO_CANCELED, we don't need to submit any message */
    if (errcode == (int) ISO_CANCELED && fmt == NULL) {
        return ISO_CANCELED;
    }

    if (fmt) {
        va_start(ap, fmt);
        vsnprintf(msg, MAX_MSG_LEN, fmt, ap);
        va_end(ap);
    } else {
        strncpy(msg, iso_error_to_msg(errcode), MAX_MSG_LEN);
    }

    libiso_msgs_submit(libiso_msgr, imgid, ISO_ERR_CODE(errcode),
                       ISO_ERR_SEV(errcode), ISO_ERR_PRIO(errcode), msg, 0, 0);
    if (causedby != 0) {
        snprintf(msg, MAX_MSG_LEN, " > Caused by: %s",
                 iso_error_to_msg(causedby));
        libiso_msgs_submit(libiso_msgr, imgid, ISO_ERR_CODE(causedby),
                 LIBISO_MSGS_SEV_NOTE, LIBISO_MSGS_PRIO_LOW, msg, 0, 0);
        if (ISO_ERR_SEV(causedby) == LIBISO_MSGS_SEV_FATAL) {
            return ISO_CANCELED;
        }
    }

    if (iso_msg_is_abort(errcode)) {
        return ISO_CANCELED;
    } else {
        return 0;
    }
}

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
 */
int iso_set_msgs_severities(char *queue_severity, char *print_severity,
                            char *print_id)
{
    int ret, queue_sevno, print_sevno;

    ret = libiso_msgs__text_to_sev(queue_severity, &queue_sevno, 0);
    if (ret <= 0)
        return 0;
    ret = libiso_msgs__text_to_sev(print_severity, &print_sevno, 0);
    if (ret <= 0)
        return 0;
    ret = libiso_msgs_set_severities(libiso_msgr, queue_sevno, print_sevno,
                                     print_id, 0);
    if (ret <= 0)
        return 0;
    return 1;
}

/**
 * Obtain the oldest pending libisofs message from the queue which has at
 * least the given minimum_severity. This message and any older message of
 * lower severity will get discarded from the queue and is then lost forever.
 *
 * Severity may be one of "NEVER", "FATAL", "SORRY", "WARNING", "HINT",
 * "NOTE", "UPDATE", "DEBUG", "ALL". To call with minimum_severity "NEVER"
 * will discard the whole queue.
 *
 * @param error_code Will become a unique error code as listed in messages.h
 * @param imgid      Id of the image that was issued the message.
 * @param msg_text   Must provide at least ISO_MSGS_MESSAGE_LEN bytes.
 * @param severity   Will become the severity related to the message and
 *                   should provide at least 80 bytes.
 * @return 1 if a matching item was found, 0 if not, <0 for severe errors
 */
int iso_obtain_msgs(char *minimum_severity, int *error_code, int *imgid,
                    char msg_text[], char severity[])
{
    int ret, minimum_sevno, sevno, priority, os_errno;
    double timestamp;
    pid_t pid;
    char *textpt, *sev_name;
    struct libiso_msgs_item *item= NULL;

    ret = libiso_msgs__text_to_sev(minimum_severity, &minimum_sevno, 0);
    if (ret <= 0)
        return 0;
    ret = libiso_msgs_obtain(libiso_msgr, &item, minimum_sevno,
                             LIBISO_MSGS_PRIO_ZERO, 0);
    if (ret <= 0)
        goto ex;
    ret = libiso_msgs_item_get_msg(item, error_code, &textpt, &os_errno, 0);
    if (ret <= 0)
        goto ex;
    strncpy(msg_text, textpt, ISO_MSGS_MESSAGE_LEN-1);
    if (strlen(textpt) >= ISO_MSGS_MESSAGE_LEN)
        msg_text[ISO_MSGS_MESSAGE_LEN-1] = 0;

    ret = libiso_msgs_item_get_origin(item, &timestamp, &pid, imgid, 0);
    if (ret <= 0)
        goto ex;

    severity[0]= 0;
    ret = libiso_msgs_item_get_rank(item, &sevno, &priority, 0);
    if (ret <= 0)
        goto ex;
    ret = libiso_msgs__sev_to_text(sevno, &sev_name, 0);
    if (ret <= 0)
        goto ex;
    strcpy(severity, sev_name);

    ret = 1;
    ex: ;
    libiso_msgs_destroy_item(libiso_msgr, &item, 0);
    return ret;
}


int iso_msgs_submit(int error_code, char msg_text[], int os_errno,
			char severity[], int origin)
{
    int ret, sevno;

    ret = libiso_msgs__text_to_sev(severity, &sevno, 0);
    if (ret <= 0)
    	sevno = LIBISO_MSGS_SEV_ALL;
    if (error_code <= 0) {
    	switch(sevno) {
    	       case LIBISO_MSGS_SEV_ABORT:   error_code = 0x00040000;
    	break; case LIBISO_MSGS_SEV_FATAL:   error_code = 0x00040001;
    	break; case LIBISO_MSGS_SEV_SORRY:   error_code = 0x00040002;
    	break; case LIBISO_MSGS_SEV_WARNING: error_code = 0x00040003;
    	break; case LIBISO_MSGS_SEV_HINT:    error_code = 0x00040004;
    	break; case LIBISO_MSGS_SEV_NOTE:    error_code = 0x00040005;
    	break; case LIBISO_MSGS_SEV_UPDATE:  error_code = 0x00040006;
    	break; case LIBISO_MSGS_SEV_DEBUG:   error_code = 0x00040007;
    	break; default:                      error_code = 0x00040008;
    	}
    }
    ret = libiso_msgs_submit(libiso_msgr, origin, error_code,
        	          sevno, LIBISO_MSGS_PRIO_HIGH, msg_text, os_errno, 0);
    return ret;
}


int iso_text_to_sev(char *severity_name, int *sevno)
{
    int ret;

    ret = libiso_msgs__text_to_sev(severity_name, sevno, 0);
    if (ret <= 0)
    	*sevno = LIBISO_MSGS_SEV_FATAL;
    return ret;
}


int iso_sev_to_text(int severity_number, char **severity_name)
{
    int ret;

    ret = libiso_msgs__sev_to_text(severity_number, severity_name, 0);
    return ret;
}


/**
 * Return the messenger object handle used by libisofs. This handle
 * may be used by related libraries to  their own compatible
 * messenger objects and thus to direct their messages to the libisofs
 * message queue. See also: libburn, API function burn_set_messenger().
 *
 * @return the handle. Do only use with compatible
 */
void *iso_get_messenger()
{
    return libiso_msgr;
}

int iso_error_get_severity(int e)
{
    return ISO_ERR_SEV(e);
}

int iso_error_get_priority(int e)
{
    return ISO_ERR_PRIO(e);
}

int iso_error_get_code(int e)
{
    return ISO_ERR_CODE(e);
}


int iso_report_errfile(char *path, int error_code, int os_errno, int flag)
{
    libiso_msgs_submit(libiso_msgr, 0, error_code,
                       LIBISO_MSGS_SEV_ERRFILE, LIBISO_MSGS_PRIO_HIGH,
                       path, os_errno, 0);
    return(1);
}


int iso_libjte_forward_msgs(void *libjte_handle,
                            int imgid, int errcode, int flag)
{

#ifdef Libisofs_with_libjtE

    char *msg = NULL;
    int res;
    struct libjte_env *handle = (struct libjte_env *) libjte_handle;

    res = ISO_SUCCESS;
    while(1) {
       msg= libjte_get_next_message(handle);
       if(msg == NULL)
    break;
       res = iso_msg_submit(imgid, errcode, 0, msg);
       free(msg);
    }
    return res;

#else /* Libisofs_with_libjtE */

    return ISO_SUCCESS;

#endif /* ! Libisofs_with_libjtE */

}

