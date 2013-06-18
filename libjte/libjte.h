/*
 * libjte.h
 *
 * Copyright (c) 2010 Thomas Schmitt <scdbackup@gmx.net>
 *
 * API definition of libjte, to be included as <libjte/libjte.h>
 *
 * GNU LGPL v2.1 (including option for GPL v2 or later)
 *
 */

#ifndef LIBJTE_H_INCLUDED 
#define LIBJTE_H_INCLUDED 1

#include <sys/types.h>

/* The common environment handle */

struct libjte_env;


/* --------------------------- Version Inquiry --------------------------- */

/** These three release version numbers tell the revision of this header file
    and of the API which it describes. They shall be memorized by applications
    at build time.
    @since 0.1.0
*/
#define LIBJTE_VERSION_MAJOR   1
#define LIBJTE_VERSION_MINOR   0
#define LIBJTE_VERSION_MICRO   0

/** Obtain the three release version numbers of the library. These are the
    numbers encountered by the application when linking with libjte
    i.e. possibly not before run time.
    Better do not base the fundamental compatibility decision of an application
    on these numbers. For a reliable check use libjte__is_compatible().
    @since 0.1.0
    @param major The maturity version (0 for now, as we are still learning)
    @param minor The development goal version.
    @param micro The development step version.
*/
void libjte__version(int *major, int *minor, int *micro);

/** Check whether all features of header file libjte.h from the given
    major.minor.micro revision triple can be delivered by the library version
    which is performing this call.
        if (! libjte__is_compatible(LIBJTE_VERSION_MAJOR, LIBJTE_VERSION_MINOR,
                                    LIBJTE_VERSION_MICRO, 0))
           ...refuse to start the program with this dynamic library version...
    @since 0.1.0
    @param major obtained at build time
    @param minor obtained at build time
    @param micro obtained at build time
    @param flag Bitfield for control purposes. Unused yet. Submit 0.
    @return 1= library can work for caller
            0= library is not usable in some aspects. Caller must restrict
               itself to an earlier API version or must not use this library
               at all.
*/
int libjte__is_compatible(int major, int minor, int micro, int flag);


/* ------------------------------- Life Cycle ----------------------------- */

/** Create a libjte environment object. It is to be used with most of the
    calls of this API for storing parameters and for memorizing the state
    of its libjte operations.
    @since 0.1.0
    @param jte_handle  Returns an opaque handle to the allocated environment
    @param flag        Bitfield for control purposes. Unused yet. Submit 0.
    @return  >0 means success, <=0 indicates failure 
*/
int libjte_new(struct libjte_env **jte_handle, int flag);


/** Dispose a libjte environment and free the system facilities which it uses.
    @since 0.1.0
    @param jte_handle  The environment to be disposed.
                       *jte_handle will be set to NULL
    @return  >0 means success, <=0 indicates failure
*/
int libjte_destroy(struct libjte_env **jte_handle);


/* ----------------------------- Parameter Setup ------------------------- */

/** Tell libjte the name that will become value of "Filename=" in the "[Image]"
    section of the jigdo file.
    @since 0.1.0
    @param jte_handle  The environment to be manipulated.
    @param outfile     The value
    @return  >0 means success, <=0 indicates failure
*/
int libjte_set_outfile(struct libjte_env *jte_handle, char *outfile);

/** Enable or disable debugging verbosity.
    @since 0.1.0
    @param jte_handle  The environment to be manipulated.
    @param verbose     If not 0 : cause verbosity
    @return  >0 means success, <=0 indicates failure
*/
int libjte_set_verbose(struct libjte_env *jte_handle, int verbose);

/** Define the file address on hard disk for the template file.
    @since 0.1.0
    @param jte_handle  The environment to be manipulated.
    @param path        Will be used with fopen(path, "w")
    @return  >0 means success, <=0 indicates failure
*/
int libjte_set_template_path(struct libjte_env *jte_handle, char *path);

/** Define the file address on hard disk for the jigdo file.
    @since 0.1.0
    @param jte_handle  The environment to be manipulated.
    @param path        Will be used with fopen(path, "w")
    @return  >0 means success, <=0 indicates failure
*/
int libjte_set_jigdo_path(struct libjte_env *jte_handle, char *path);

/** Tell libjte the hard disk address of the .md5 file, which lists the
    data files which might get extracted and referred in the jigdo file.
    @since 0.1.0
    @param jte_handle  The environment to be manipulated.
    @param path        Will be used with fopen(path, "r")
    @return  >0 means success, <=0 indicates failure
*/
int libjte_set_md5_path(struct libjte_env *jte_handle, char *path);

/** Define a minimum size for data files to get extracted and referred in
    the jigdo file.
    @since 0.1.0
    @param jte_handle  The environment to be manipulated.
    @param min_size    Lower size limit in bytes
    @return  >0 means success, <=0 indicates failure
*/
int libjte_set_min_size(struct libjte_env *jte_handle, int min_size);

/** Choose one or more checksum algorithms to be applied to the emerging
    payload image. The resulting sums will be written into the jigdo file
    as lines "# Image Hex ...".
    Supported algorithms are "md5", "sha1", "sha256", "sha512" which may be
    combined in a comma separated list like "md5,sha1,sha512".
    Checksum_code "all" chooses all available algorithms.
    @since 0.1.0
    @param jte_handle     The environment to be manipulated.
    @param  checksum_code Comma separated list string or "all".
    @return  >0 means success, <=0 indicates failure
*/
int libjte_set_checksum_iso(struct libjte_env *jte_handle,
                            char *checksum_code);

/** Choose one or more checksum algorithms to be applied to the emerging
    template file. The resulting sums will be written into the jigdo file
    as lines "# Template Hex ...".
    Supported algorithms are "md5", "sha1", "sha256", "sha512" which may be
    combined in a comma separated list like "md5,sha1,sha512".
    Checksum_code "all" chooses all available algorithms.
    @since 0.1.0
    @param jte_handle     The environment to be manipulated.
    @param  checksum_code Comma separated list string or "all".
    @return  >0 means success, <=0 indicates failure
*/
int libjte_set_checksum_template(struct libjte_env *jte_handle,
                                 char *checksum_code);

/** Choose a compression algorithm for the template file.
    @since 0.1.0
    @param jte_handle        The environment to be manipulated.
    @param compression_code  Either "gzip" or "bzip2".
    @return  >0 means success, <=0 indicates failure
*/
int libjte_set_compression(struct libjte_env *jte_handle,
                           char *compression_code);

/** Add a regular expression pattern to the list of excluded filenames.
    The pattern will be tested against the filenames that are handed to
    libjte_decide_file_jigdo() or libjte_begin_data_file().
    @since 0.1.0
    @param jte_handle  The environment to be manipulated.
    @param             String for regcomp(3)
    @return  >0 means success, <=0 indicates failure
*/
int libjte_add_exclude(struct libjte_env *jte_handle, char *pattern);

/** Add a regular expression pattern to the list of files which must be
    found matching in the .md5 file.
    The pattern will be tested against the filenames that are handed to
    libjte_decide_file_jigdo() or libjte_begin_data_file() and do not find
    a matching entry in the .md5 file. If it matches, then said two functions
    will return error resp. perform exit() if this is enabled by 
    libjte_set_error_behavior().
    @since 0.1.0
    @param jte_handle  The environment to be manipulated.
    @param             String for regcomp(3)
    @return  >0 means success, <=0 indicates failure
*/
int libjte_add_md5_demand(struct libjte_env *jte_handle, char *pattern);

/** Add a To=From pair to the list of mirror name mapping.
    The pair will be split at the '=' character. The mirror_name submitted to
    libjte_write_match_record() will be tested whether it begins by the From
    string. If so, then this string will be replaced by the To string and           a ':' character.
    The resulting string will be used as data file name in the jigdo file.

    libjte_decide_file_jigdo() gets the mirror_name from the matching line
    in the .md5 file and hands it to the application.
    libjte_begin_data_file() obtains the mirror name and processes it without
    bothering the application.

    @since 0.1.0
    @param jte_handle  The environment to be manipulated.
    @param arg         String of the form To=From
    @return  >0 means success, <=0 indicates failure
*/
int libjte_add_mapping(struct libjte_env *jte_handle, char *arg);


/* ----------------------------- Operation --------------------------------- */

/** Start the production of jigdo and template file.
    This is to be done after all parameters are set and before any payload
    data are produced by the application.
    @since 0.1.0
    @param jte_handle  The environment to be started.
    @return >0 = ok
             0 = error
            -1 = would have called exit(1) if enabled
*/
int libjte_write_header(struct libjte_env *jte_handle);


/** Finish the production of jigdo and template file.
    This is to be done after all payload has been produced by the application
    and processed by libjte, and before the libjte environment gets disposed.
    @since 0.1.0
    @param jte_handle  The environment to be finished.
    @return >0 = ok
             0 = error
            -1 = would have called exit(1) if enabled
*/
int libjte_write_footer(struct libjte_env *jte_handle);


/* ---------------------------  Data File APIs ---------------------------- */

/* There are two alternative ways how to process a single data file and its
   content: Traditional and Simplified.
   Choose either one.

   CAUTION: Do not mix them !

*/

/* ---------------------- Traditional Data File API ----------------------- */

/* This implements the way how genisoimage deals with single data files when
   showing them to its built-in JTE.

   When processing of a data file begins :

     if (libjte_decide_file_jigdo(..., &mirror_name, md5) == 1) {
         libjte_write_match_record(..., mirror_name, ..., md5);
         write_unmatched_data = 0;
     } else
         write_unmatched_data = 1;

   When a chunk of data content is written to the payload image :

     if (write_unmatched_data)
        libjte_write_unmatched(...);

   Before calling libjte_write_footer() :

     libjte_set_image_size(...);

*/

/** Decide whether a data file shall be extracted, i.e. referred in the
    template file and listed in the jigdo file, or whether its content
    shall be written compressed into the template file.
    @since 0.1.0
    @param jte_handle  The environment to be used.
    @param filename    The address to be used with fopen(filename, "r")
    @param size        Number of bytes in the data file
    @param mirror_name Returns the name which shall be listed in the jigdo
                       file. This is NULL or allocated memory that has to
                       be disposed by free(3).
    @param md5         May get filled by MD5 for libjte_write_match_record().
    @return  0= use libjte_write_unmatched(),
             1= use libjte_write_match_record()
            -1= would have called exit(1) if enabled
*/
int libjte_decide_file_jigdo(struct libjte_env *jte_handle,
                             char *filename, off_t size, char **mirror_name,
                             unsigned char md5[16]);

/** Register a list entry in the jigdo file and write a reference tag into
    the template file. This is to be called if libjte_decide_file_jigdo()
    returned 1.
    @since 0.1.0
    @param jte_handle  The environment to be used.
    @param filename    The address to be used with fopen(filename, "r").
    @param mirror_name The mirror_name returned by libjte_decide_file_jigdo().
    @param sector_size An eventual byte address alignment which is achieved
                       in the payload image by padding file ends with zeros.
                       Submit 1 if no alignment is done. For ISO images: 2048.
    @param size        The number of bytes in the data file.
    @param md5         The md5 buffer submitted to libjte_decide_file_jigdo().
    @return >0 = ok
             0 = error
            -1 = would have called exit(1) if enabled
*/
int libjte_write_match_record(struct libjte_env *jte_handle,
                            char *filename, char *mirror_name, int sector_size,
                            off_t size, unsigned char md5[16]);

/** Write a payload data chunk into the template file. This is to be called
    with any data file bytes if libjte_decide_file_jigdo() returned 0.
    It is also to be called with any payload image bytes which are not content
    of a data file.
    @since 0.1.0
    @param jte_handle  The environment to be used.
    @param buffer      The memory buffer containing the payload bytes
    @param size        The block size within buffer. (1 is well ok.)
    @param count       The number of blocks of the given size within buffer.
    @return  >0 means success, <=0 indicates failure
*/
/* @return >0 = ok
            0 = error
           -1 = would have called exit(1) if enabled
*/
int libjte_write_unmatched(struct libjte_env *jte_handle, void *buffer,
                            int size, int count);

/** Before calling libjte_footer() submit the number of written payload bytes.
    The Traditional Data File API does not keep track of this count. 
    @since 0.1.0
    @param jte_handle  The environment to be used.
    @param image_size  Number of bytes in the image.
    @return  >0 means success, <=0 indicates failure
*/
int libjte_set_image_size(struct libjte_env *o, off_t image_size);
            

/* ------------------------- Simplified Data File API ---------------------- */

/* This implements the way how libisofs deals with single data files when
   showing them to libjte. It does not demand from the application to memorize
   the state of libjte decisions and parameters.
   It rather shows any payload data to libjte and only marks the begin and
   end of data file content.

   When a chunk of bytes is written to the payload image :

     libjte_show_data_chunk();

   When processing of a data file begins :

     libjte_begin_data_file();

   When the content of a data file has been shown completely :

     libjte_end_data_file();
*/

/** Show a chunk of payload data to libjte which will decide whether to write
    it into the template file or whether to ignore it, because it belongs to
    a Jigdo extracted file.
    @since 0.1.0
    @param jte_handle  The environment to be used.
    @param buffer      The memory buffer containing the payload bytes
    @param size        The block size within buffer. (1 is well ok.)
    @param count       The number of blocks of the given size within buffer.
    @return  >0 means success, <=0 indicates failure
*/
int libjte_show_data_chunk(struct libjte_env *jte_handle,
                           void *buffer, int size, int count);

/** Tell libjte that the content of a data file is to be processed next.
    libjte will decide whether to extract the file and list it in the jigdo
    file, or whether to direct subsequent calls of libjte_show_data_chunk()
    into the template file.
    @param jte_handle  The environment to be used.
    @param filename    The address to be used with fopen(filename, "r").
    @param sector_size An eventual byte address alignment which is achieved
                       in the payload image by padding file ends with zeros.
                       Submit 1 if no alignment is done. For ISO images: 2048.
    @param size        The number of bytes in the data file.
    @return >0 = ok
             0 = error
            -1 = would have called exit(1) if enabled
                 E.g. because a libjte_add_md5_demand() pattern matched
                 a file that shall not get extracted.
*/
int libjte_begin_data_file(struct libjte_env *jte_handle, char *filename,
                           int sector_size, off_t size);

/** Tell libjte that all content of the previously announced data file has
    been submitted to libjte_show_data_chunk().
    libjte will direct the following calls of libjte_show_data_chunk() into
    the template file.
    @since 0.1.0
    @param jte_handle  The environment to be used.
    @return  >0 means success, <=0 indicates failure
*/
int libjte_end_data_file(struct libjte_env *jte_handle);


/* ----------------- Message Reporting and Error Behavior ----------------- */

/** Define how libjte shall deliver its messages and whether it is allowed
    to call exit() in hopeless situations. 
    @since 0.1.0
    @param jte_handle  The environment to be manipulated.
    @param to_stderr   If 0, append messages to the message list of the libjte
                       environment.
                       If 1 print messages directly to stderr. This is the
                       default.
    @param with_exit   If 1 perform exit(1); when encountering severe errors.
                       If 0 return -1 in such situations. This is the default.
    @return  >0 means success, <=0 indicates failure
*/
int libjte_set_error_behavior(struct libjte_env *o, 
                              int to_stderr, int with_exit);

/** Get the oldest pending message from the message list. 
    @since 0.1.0
    @param jte_handle  The environment to be manipulated.
    @return Pointer to alloceted memory. Apply free() when no longer needed.
            NULL means that no more message is available 
*/
char *libjte_get_next_message(struct libjte_env *o);

/** Dispose all pending messages after eventually printing them to stderr.
    @since 0.1.0
    @param jte_handle  The environment to be manipulated.
    @param flag bit0= print pending messages to stderr
                bit1= eventually complain before printing messages
    @return  >0 means success, <=0 indicates failure
*/
int libjte_clear_msg_list(struct libjte_env *o, int flag);

#endif /* LIBJTE_H_INCLUDED */

