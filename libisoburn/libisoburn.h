
/*
  Lower level API definition of libisoburn.

  Copyright 2007-2013 Vreixo Formoso Lopes <metalpain2002@yahoo.es>
                  and Thomas Schmitt <scdbackup@gmx.net>
  Provided under GPL version 2 or later.
*/

/**                           Overview

libisoburn is a frontend for libraries libburn and libisofs which enables
creation and expansion of ISO-9660 filesystems on all CD/DVD/BD media supported
by libburn. This includes media like DVD+RW, which do not support multi-session
management on media level and even plain disk files or block devices.

The price for that is thorough specialization on data files in ISO-9660
filesystem images. So libisoburn is not suitable for audio (CD-DA) or any
other CD layout which does not entirely consist of ISO-9660 sessions.

Note that there is a higher level of API: xorriso.h. One should not mix it
with the API calls of libisoburn.h, libisofs.h, and libburn.h.


                          Connector functions

libisofs and libburn do not depend on each other but share some interfaces
by which they can cooperate.
libisoburn establishes the connection between both modules by creating the
necessary interface objects and attaching them to the right places.


                          Wrapper functions 

The priciple of this frontend is that you may use any call of libisofs or
libburn unless it has a  isoburn_*()  wrapper listed in the following function
documentation.

E.g. call isoburn_initialize() rather than iso_init(); burn_initialize();
and call isoburn_drive_scan_and_grab() rather than burn_drive_scan_and_grab().
But you may call  burn_disc_get_profile()  directly if you want to display
the media type.

The wrappers will transparently provide the necessary emulations which
are appropriate for particular target drives and media states.
To learn about them you have to read both API descriptions: the one of
the wrapper and the one of the underlying libburn or libisofs call.

Macros BURN_* and functions burn_*() are documented in <libburn/libburn.h>
Macros ISO_* and functions iso_*() are documented in <libisofs/libisofs.h>


                             Usage model

There may be an input drive and an output drive. Either of them may be missing
with the consequence that no reading resp. writing is possible.
Both drive roles can be fulfilled by the same drive.

Input can be a random access readable libburn drive:
  optical media, regular files, block devices.
Output can be any writeable libburn drive:
  writeable optical media in burner, writeable file objects (no directories).

libburn demands rw-permissions to drive device file resp. file object.

If the input drive provides a suitable ISO RockRidge image, then its tree
may be loaded into memory and can then be manipulated by libisofs API calls.
The loading is done by isoburn_read_image() under control of
struct isoburn_read_opts which the application obtains from libisoburn
and manipulates by the family of isoburn_ropt_set_*() functions.

Writing of result images is controlled by libisofs related parameters
in a struct isoburn_imgen_opts which the application obtains from libisoburn
and manipulates by the family of isoburn_igopt_set_*() functions.

All multi-session aspects are handled by libisoburn according to these
settings. The application does not have to analyze media state and write
job parameters. It rather states its desires which libisoburn tries to
fulfill, or else will refuse to start the write run.


              Setup for Growing, Modifying or Blind Growing

The connector function family offers alternative API calls for performing
the setup for several alternative image generation strategies.

Growing:
If input and output drive are the same, then isoburn_prepare_disc() is to
be used. It will lead to an add-on session on appendable or overwriteable
media with existing ISO image. With blank media it will produce a first
session.

Modifying:
If the output drive is not the input drive, and if it bears blank media
or overwriteable without a valid ISO image, then one may produce a consolidated
image with old and new data. This will copy file data from an eventual input
drive with valid image, add any newly introduced data from the local
filesystem, and produce a first session on output media.
To prepare for such an image generation run, use isoburn_prepare_new_image().

Blind Growing:
This method reads the old image from one drive and writes the add-on session
to a different drive. That output drive is nevertheless supposed to
finally lead to the same medium from where the session was loaded. Usually it
will be stdio:/dev/fd/1 (i.e. stdout) being piped into some burn program
like with this classic gesture:
  mkisofs -M $dev -C $msc1,$nwa | cdrecord -waiti dev=$dev
Blind growing is prepared by the call isoburn_prepare_blind_grow().
The input drive should be released immediately after this call in order
to allow the consumer of the output stream to access that drive for writing.

After either of these setups, some peripheral libburn drive parameter settings
like  burn_write_opts_set_simulate(),  burn_write_opts_set_multi(),
 burn_drive_set_speed(),  burn_write_opts_set_underrun_proof()  should be made.
Do not set the write mode. It will be chosen by libisoburn so it matches job
and media state.

                           Writing the image

Then one may start image generation and write threads by isoburn_disc_write().
Progress may be watched at the output drive by burn_drive_get_status() and
isoburn_get_fifo_status().

At some time, the output drive will be BURN_DRIVE_IDLE indicating that
writing has ended.
One should inquire isoburn_drive_wrote_well() to learn about overall success.

Finally one must call isoburn_activate_session() which will complete any
eventual multi-session emulation.


                         Application Constraints

Applications shall include libisofs/libisofs.h , libburn/libburn.h and this
file itself: libisoburn/libisoburn.h .
They shall link with -lisofs -lburn -lisoburn or with the .o files emerging
from building those libraries from their sources.

Applications must use 64 bit off_t.
E.g. on 32-bit GNU/Linux by defining
  #define _LARGEFILE_SOURCE
  #define _FILE_OFFSET_BITS 64
The minimum requirement is to interface with the library by 64 bit signed
integers where libisofs.h or libisoburn.h prescribe off_t.
Failure to do so may result in surprising malfunction or memory faults.

Application files which include libisofs/libisofs.h or libisoburn/libisoburn.h
must provide definitions for uint32_t and uint8_t.
This can be achieved either:
- by using autotools which will define HAVE_STDINT_H or HAVE_INTTYPES_H 
  according to its ./configure tests,
- or by defining the macros HAVE_STDINT_H resp. HAVE_INTTYPES_H according
  to the local situation,
- or by appropriately defining uint32_t and uint8_t by other means,
  e.g. by including inttypes.h before including libisofs.h and libisoburn.h

*/
#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif


/* Important: If you add a public API function then add its name to file
                  libisoburn/libisoburn.ver
*/


                          /* API functions */


/** Initialize libisoburn, libisofs and libburn.
    Wrapper for : iso_init() and burn_initialize()
    @since 0.1.0
    @param msg  A character array for eventual messages (e.g. with errors)
    @param flag Bitfield for control purposes (unused yet, submit 0) 
    @return 1 indicates success, 0 is failure
*/
int isoburn_initialize(char msg[1024], int flag);


/** Check whether all features of header file libisoburn.h from the given
    major.minor.micro revision triple can be delivered by the library version
    which is performing this call.
    An application of libisoburn can easily memorize the version of the
    libisoburn.h header in its own code. Immediately after isoburn_initialize()
    it should simply do this check:
        if (! isoburn_is_compatible(isoburn_header_version_major,
                                    isoburn_header_version_minor,
                                    isoburn_header_version_micro, 0))
           ...refuse to start the program with this dynamic library version...
    @since 0.1.0
    @param major obtained at build time
    @param minor obtained at build time
    @param micro obtained at build time
    @param flag Bitfield for control purposes. Unused yet. Submit 0.
    @return 1= library can work for caller
            0= library is not usable in some aspects. Caller must restrict
               itself to an earlier API version or must not use this libray
               at all.
*/
int isoburn_is_compatible(int major, int minor, int micro, int flag);


/** Obtain the three release version numbers of the library. These are the
    numbers encountered by the application when linking with libisoburn,
    i.e. possibly not before run time.
    Better do not base the fundamental compatibility decision of an application
    on these numbers. For a reliable check use isoburn_is_compatible().
    @since 0.1.0
    @param major The maturity version (0 for now, as we are still learning)
    @param minor The development goal version.
    @param micro The development step version. This has an additional meaning:

                 Pare numbers indicate a version with frozen API. I.e. you can
                 rely on the same set of features to be present in all
                 published releases with that major.minor.micro combination.
                 Features of a pare release will stay available and ABI
                 compatible as long as the SONAME of libisoburn stays "1".
                 Currently there are no plans to ever change the SONAME.
                 
                 Odd numbers indicate that API upgrades are in progress.
                 I.e. new features might be already present or they might
                 be still missing. Newly introduced features may be changed
                 incompatibly or even be revoked before release of a pare
                 version.
                 So micro revisions {1,3,5,7,9} should never be used for
                 dynamic linking unless the proper library match can be
                 guaranteed by external circumstances.

    @return 1 success, <=0 might in future become an error indication
*/
void isoburn_version(int *major, int *minor, int *micro);


/** The minimum version of libisofs to be used with this version of libisoburn
    at compile time.
    @since 0.1.0
*/
#define isoburn_libisofs_req_major  1
#define isoburn_libisofs_req_minor  3
#define isoburn_libisofs_req_micro  0 

/** The minimum version of libburn to be used with this version of libisoburn
    at compile time.
    @since 0.1.0
*/
#define isoburn_libburn_req_major  1
#define isoburn_libburn_req_minor  3
#define isoburn_libburn_req_micro  0

/** The minimum compile time requirements of libisoburn towards libjte are
    the same as of a suitable libisofs towards libjte.
    So use these macros from libisofs.h :
      iso_libjte_req_major
      iso_libjte_req_minor
      iso_libjte_req_micro
    @since 0.6.4
*/

/** The minimum version of libisofs to be used with this version of libisoburn
    at runtime. This is checked already in isoburn_initialize() which will
    refuse on outdated version. So this call is for information purposes after
    successful startup only.
    @since 0.1.0
    @param major isoburn_libisofs_req_major as seen at build time
    @param minor as seen at build time
    @param micro as seen at build time
    @return 1 success, <=0 might in future become an error indication
*/
int isoburn_libisofs_req(int *major, int *minor, int *micro);


/** The minimum version of libjte to be used with this version of libisoburn
    at runtime. The use of libjte is optional and depends on configure
    tests. It can be prevented by ./configure option --disable-libjte .
    This is checked already in isoburn_initialize() which will refuse on
    outdated version. So this call is for information purposes after
    successful startup only.
    @since 0.6.4
*/
int isoburn_libjte_req(int *major, int *minor, int *micro);


/** The minimum version of libburn to be used with this version of libisoburn
    at runtime. This is checked already in isoburn_initialize() which will
    refuse on outdated version. So this call is for information purposes after
    successful startup only.
    @since 0.1.0
    @param major isoburn_libburn_req_major as seen at build time
    @param minor as seen at build time
    @param micro as seen at build time
    @return 1 success, <=0 might in future become an error indication
*/
int isoburn_libburn_req(int *major, int *minor, int *micro);


/** These three release version numbers tell the revision of this header file
    and of the API it describes. They are memorized by applications at build
    time.
    @since 0.1.0
*/
#define isoburn_header_version_major  1
#define isoburn_header_version_minor  3
#define isoburn_header_version_micro  0
/** Note:
    Above version numbers are also recorded in configure.ac because libtool
    wants them as parameters at build time.
    For the library compatibility check, ISOBURN_*_VERSION in configure.ac
    are not decisive. Only the three numbers here do matter.
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

-----------------------------------------------------

For an implementation of the Thomas Schmitt approach,
see libisoburn/burn_wrap.c : isoburn_initialize()
This connects libisoburn as "application" with libisofs
as "library".

The compatible part of Vreixo Formoso's approach is implemented
in configure.ac LIBBURN_REQUIRED, LIBISOFS_REQUIRED.
In isoburn_initialize() it would rather test by
  iso_lib_is_compatible(isoburn_libisofs_req_major,...
than by
  iso_lib_is_compatible(iso_lib_header_version_major,...
and would leave out the ugly compile time traps.

*/


/** Announce to the library an application provided method for immediate
    delivery of messages. It is used when no drive is affected directly or
    if the drive has no own msgs_submit() method attached by
    isoburn_drive_set_msgs_submit.
    If no method is preset or if the method is set to NULL then libisoburn
    delivers its messages through the message queue of libburn.
    @param msgs_submit   The function call which implements the method
    @param submit_handle Handle to be used as first argument of msgs_submit
    @param submit_flag   Flag to be used as last argument of msgs_submit
    @param flag          Unused yet, submit 0
    @since 0.2.0
*/
int isoburn_set_msgs_submit(int (*msgs_submit)(void *handle, int error_code,
                                                  char msg_text[], int os_errno,
                                                  char severity[], int flag),
                               void *submit_handle, int submit_flag, int flag);


/** Acquire a target drive by its filesystem path resp. libburn persistent
    address.
    Wrapper for: burn_drive_scan_and_grab()
    @since 0.1.0
    @param drive_infos On success returns a one element array with the drive
                  (cdrom/burner). Thus use with driveno 0 only. On failure
                  the array has no valid elements at all.
                  The returned array should be freed via burn_drive_info_free()
                  when the drive is no longer needed. But before this is done
                  one has to call isoburn_drive_release(drive_infos[0].drive).
    @param adr    The persistent address of the desired drive.
    @param load   1 attempt to load the disc tray. 0 no attempt,rather failure.
    @return       1 = success , 0 = drive not found , <0 = other error
*/
int isoburn_drive_scan_and_grab(struct burn_drive_info *drive_infos[],
                                char* adr, int load);


/** Acquire a target drive by its filesystem path resp. libburn persistent
    address. This is a modern successor of isoburn_drive_scan_and_grab().  
    Wrapper for: burn_drive_scan_and_grab()
    @since 0.1.2
    @param drive_infos On success returns a one element array with the drive
                  (cdrom/burner). Thus use with driveno 0 only. On failure
                  the array has no valid elements at all.
                  The returned array should be freed via burn_drive_info_free()
                  when the drive is no longer needed. But before this is done
                  one has to call isoburn_drive_release(drive_infos[0].drive).
    @param adr    The persistent address of the desired drive.
    @param flag   bit0= attempt to load the disc tray.
                        Else: failure if not loaded.
                  bit1= regard overwriteable media as blank
                  bit2= if the drive is a regular disk file:
                        truncate it to the write start address when writing
                        begins
                  bit3= if the drive reports a read-only profile try to read
                        table of content by scanning for ISO image headers.
                        (depending on media type and drive this might
                         help or it might make the resulting toc even worse)
                  bit4= do not emulate table of content on overwriteable media
                  bit5= ignore ACL from external filesystems
                  bit6= ignore POSIX Extended Attributes from external
                        filesystems
                  bit7= pretend read-only profile and scan for table of content
                  bit8= re-assess already acquired (*drive_infos)[0] rather
                        than aquiring adr
                        @since 1.1.8
                  bit9= when scanning for ISO 9660 sessions  by bit3:
                        Do not demand a valid superblock at LBA 0, ignore it in
                        favor of one at LBA 32, and scan until end of medium.
                        @since 1.2.6
    @return       1 = success , 0 = drive not found , <0 = other error

    Please excuse the typo "aquire" in the function name.
*/
int isoburn_drive_aquire(struct burn_drive_info *drive_infos[],
                         char* adr, int flag);

/** Acquire a drive from the burn_drive_info[] array which was obtained by
    a previous call of burn_drive_scan(). 
    Wrapper for: burn_drive_grab()
    @since 0.1.0
    @param drive The drive to grab. E.g. drive_infos[1].drive .
                 Call isoburn_drive_release(drive) when it it no longer needed.
    @param load  1 attempt to load the disc tray. 0 no attempt, rather failure.
    @return      1 success, <=0 failure
*/
int isoburn_drive_grab(struct burn_drive *drive, int load);


/** Attach to a drive an application provided method for immediate
    delivery of messages.
    If no method is set or if the method is set to NULL then libisoburn
    delivers messages of the drive through the global msgs_submit() method
    set by isoburn_set_msgs_submiti() or by the message queue of libburn.
    @since 0.2.0
    @param d     The drive to which this function, handle and flag shall apply
    @param msgs_submit   The function call which implements the method
    @param submit_handle Handle to be used as first argument of msgs_submit
    @param submit_flag   Flag to be used as last argument of msgs_submit
    @param flag          Unused yet, submit 0
*/
int isoburn_drive_set_msgs_submit(struct burn_drive *d,
                            int (*msgs_submit)(void *handle, int error_code,
                                               char msg_text[], int os_errno,
                                               char severity[], int flag),
                            void *submit_handle, int submit_flag, int flag);


/** Inquire the medium status. Expect the whole spectrum of libburn BURN_DISC_*
    with multi-session media. Emulated states with random access media are
    BURN_DISC_BLANK and BURN_DISC_APPENDABLE.
    Wrapper for: burn_disc_get_status()
    @since 0.1.0
    @param drive The drive to inquire.
    @return The status of the drive, or what kind of disc is in it.
            Note: BURN_DISC_UNGRABBED indicates wrong API usage
*/
#ifdef __cplusplus
enum burn::burn_disc_status isoburn_disc_get_status(struct burn_drive *drive);
#else
enum burn_disc_status isoburn_disc_get_status(struct burn_drive *drive);
#endif


/** Tells whether the medium can be treated by isoburn_disc_erase().
    Wrapper for: burn_disc_erasable()
    @since 0.1.0
    @param d     The drive to inquire.
    @return      0=not erasable , else erasable
*/
int isoburn_disc_erasable(struct burn_drive *d);


/** Mark the medium as blank. With multi-session media this will call
    burn_disc_erase(). With random access media, an eventual ISO-9660
    filesystem will get invalidated by altering its start blocks on the medium.
    In case of success, the medium is in status BURN_DISC_BLANK afterwards.
    Wrapper for: burn_disc_erase()
    @since 0.1.0
    @param drive The drive with the medium to erase.
    @param fast 1=fast erase, 0=thorough erase
               With DVD-RW, fast erase yields media incapable of multi-session.
*/
void isoburn_disc_erase(struct burn_drive *drive, int fast);


/** Set up isoburn_disc_get_msc1() to return a fabricated value.
    This makes only sense between aquiring the drive and reading the
    image. After isoburn_read_image() it will confuse the coordination
    of libisoburn and libisofs.
    Note: Sessions and tracks are counted beginning with 1, not with 0.
    @since 0.1.6
    @param d        The drive where msc1 is to be set
    @param adr_mode Determines how to interpret adr_value and to set msc1.
                    If adr_value shall represent a number then decimal ASCII
                    digits are expected.
                    0= start lba of last session in TOC, ignore adr_value
                    1= start lba of session number given by adr_value
                    2= start lba of track given number by adr_value
                    3= adr_value itself is the lba to be used
                    4= start lba of last session with volume id
                       given by adr_value
    @param adr_value A string describing the value to be eventually used.
    @param flag Bitfield for control purposes.
                bit0= @since 0.2.2
                      with adr_mode 3: adr_value might be 16 blocks too high
                      (e.g. -C stemming from growisofs). Probe for ISO head
                      at adr_value-16 and eventually adjust setting. 
                bit1= insist in seeing a disc object with at least one session
                bit2= with adr_mode 4: use adr_value as regular expression
*/
int isoburn_set_msc1(struct burn_drive *d, int adr_mode, char *adr_value,
                     int flag);


/* ----------------------------------------------------------------------- */
/* 

  Wrappers for emulation of TOC on overwriteable media

  Media which match the overwriteable usage model lack of a history of sessions
  and tracks. libburn will not even hand out a burn_disc object for them and
  always declare them blank. libisoburn checks for a valid ISO filesystem
  header at LBA 0 and eventually declares them appendable.
  Nevertheless one can only determine an upper limit of the size of the overall
  image (by isoburn_get_min_start_byte()) but not a list of stored sessions
  and their LBAs, as it is possible with true multi-session media.

  The following wrappers add the capability to obtain a session and track TOC
  from emulated multi-session images on overwriteables if the first session
  was written by libisoburn-0.1.6 or later (i.e. with a header copy at LBA 32).

  Be aware that the structs emitted by these isoburn calls are not compatible
  with the libburn structs. I.e. you may use them only with isoburn_toc_*
  calls. 
  isoburn_toc_disc needs to be freed after use. isoburn_toc_session and
  isoburn_toc_track vanish together with their isoburn_toc_disc.
*/

/* Opaque handles to media, session, track */
struct isoburn_toc_disc;
struct isoburn_toc_session;
struct isoburn_toc_track;


/** Obtain a master handle for the table of content.
    This handle governs allocated resources which have to be released by
    isoburn_toc_disc_free() when no longer needed.
    Wrapper for: burn_drive_get_disc()
    @since 0.1.6
    @param d   The drive with the medium to inspect
    @return    NULL in case there is no content info, else it is a valid handle
*/
struct isoburn_toc_disc *isoburn_toc_drive_get_disc(struct burn_drive *d);


/** Tell the number of 2048 byte blocks covered by the table of content.
    This number includes the eventual gaps between sessions and tracks.
    So this call is not really a wrapper for burn_disc_get_sectors().
    @since 0.1.6
    @param disc  The master handle of the medium
    @return      Number of blocks, <=0 indicates unknown or unreadable state
*/
int isoburn_toc_disc_get_sectors(struct isoburn_toc_disc *disc);


/** Get the array of session handles and the number of complete sessions
    from the table of content.
    The result array contains *num + isoburn_toc_disc_get_incmpl_sess()
    elements. All above *num are incomplete sessions.
    Typically there is at most one incomplete session with no track.
    Wrapper for: burn_disc_get_sessions()
    @since 0.1.6
    @param disc The master handle of the medium
    @param num returns the number of sessions in the array
    @return the address of the array of session handles
*/
struct isoburn_toc_session **isoburn_toc_disc_get_sessions(
                                      struct isoburn_toc_disc *disc, int *num);


/** Obtain the number of incomplete sessions which are recorded in the
    result array of isoburn_toc_disc_get_sessions() after the complete
    sessions. See above.
    @since 1.2.8
    @param disc The master handle of the medium
    @return  the number of incomplete sessions
*/
int isoburn_toc_disc_get_incmpl_sess(struct isoburn_toc_disc *disc);


/** Tell the number of 2048 byte blocks covered by a particular session.
    Wrapper for: burn_session_get_sectors()
    @since 0.1.6
    @param s The session handle
    @return number of blocks, <=0 indicates unknown or unreadable state
*/
int isoburn_toc_session_get_sectors(struct isoburn_toc_session *s);


/** Obtain a copy of the entry which describes the end of a particular session.
    Wrapper for: burn_session_get_leadout_entry()
    @since 0.1.6
    @param s The session handle
    @param entry A pointer to memory provided by the caller. It will be filled
                 with info according to struct burn_toc_entry as defined
                 in libburn.h
*/
void isoburn_toc_session_get_leadout_entry(struct isoburn_toc_session *s,
                                       struct burn_toc_entry *entry);


/** Get the array of track handles from a particular session.
    Wrapper for: burn_session_get_tracks()
    @since 0.1.6
    @param s The session handle
    @param num returns the number of tracks in the array
    @return the address of the array of track handles,
            NULL if no tracks are registered with session s
*/
struct isoburn_toc_track **isoburn_toc_session_get_tracks(
                                      struct isoburn_toc_session *s, int *num);


/** Obtain a copy of the entry which describes a particular track.
    Wrapper for: burn_track_get_entry()
    @since 0.1.6
    @param t The track handle
    @param entry A pointer to memory provided by the caller. It will be filled
                 with info according to struct burn_toc_entry as defined
                 in libburn.h
*/
void isoburn_toc_track_get_entry(struct isoburn_toc_track *t,
                                 struct burn_toc_entry *entry);


/** Obtain eventual ISO image parameters of an emulated track. This info was
    gained with much effort and thus gets cached in the track object.
    If this call returns 1 then one can save a call of isoburn_read_iso_head()
    with return mode 1 which could cause an expensive read operation.
    @since 0.4.0
    @param t            The track handle
    @param start_lba    Returns the start address of the ISO session
    @param image_blocks Returns the number of 2048 bytes blocks
    @param volid        Caller provided memory for the volume id
    @param flag         unused yet, submit 0
    @return             0= not an emulated ISO session , 1= reply is valid
*/
int isoburn_toc_track_get_emul(struct isoburn_toc_track *t, int *start_lba,
                               int *image_blocks, char volid[33], int flag);



/** Release the memory associated with a master handle of a medium.
    The handle is invalid afterwards and may not be used any more.
    Wrapper for: burn_disc_free()
    @since 0.1.6
    @param disc The master handle of the medium
*/
void isoburn_toc_disc_free(struct isoburn_toc_disc *disc);


/** Try whether the data at the given address look like a ISO 9660
    image header and obtain its alleged size. Depending on the info mode
    one other string of text information can be retrieved too.
    @since 0.1.6
    @param d     The drive with the medium to inspect
    @param lba   The block number from where to read
    @param image_blocks  Returns the number of 2048 bytes blocks in the session
    @param info  Caller provided memory, enough to take eventual info reply
    @param flag bit0-7: info return mode
                 0= do not return anything in info (do not even touch it)
                 1= copy volume id to info (info needs 33 bytes)
                 2= @since 0.2.2 :
                    copy 64 kB header to info (needs 65536 bytes) 
                bit13= @since 0.2.2:
                       Do not read head from medium but use first 64 kB from
                       info.
                       In this case it is permissible to submit d == NULL.
                bit14= check both half buffers (not only second)
                       return 2 if found in first block
                bit15= return -1 on read error
    @return >0 seems to be a valid ISO image, 0 format not recognized, <0 error
*/
int isoburn_read_iso_head(struct burn_drive *d, int lba,
                          int *image_blocks, char *info, int flag);


/** Try to convert the given entity address into various entity addresses
    which would describe it.
    Note: Sessions and tracks are counted beginning with 1, not with 0.
    @since 0.3.2
    @param d        The drive where msc1 is to be set
    @param adr_mode Determines how to interpret the input adr_value.
                    If adr_value shall represent a number then decimal ASCII
                    digits are expected.
                    0= start lba of last session in TOC, ignore adr_value
                    1= start lba of session number given by adr_value
                    2= start lba of track given number by adr_value
                    3= adr_value itself is the lba to be used
                    4= start lba of last session with volume id
                       given by adr_value
    @param adr_value A string describing the value to be eventually used.
    @param lba       returns the block address of the entity, -1 means invalid
    @param track     returns the track number of the entity, -1 means invalid
    @param session   returns the session number of the entity, -1 means invalid
    @param volid     returns the volume id of the entity if it is a ISO session
    @param flag      Bitfield for control purposes.
                     bit2= with adr_mode 4: use adr_value as regular expression
    @return          <=0 error , 1 ok, ISO session, 2 ok, not an ISO session
*/
int isoburn_get_mount_params(struct burn_drive *d,
                             int adr_mode, char *adr_value,
                             int *lba, int *track, int *session,
                             char volid[33], int flag);


/* ----------------------------------------------------------------------- */
/*

  Options for image reading.

  An application shall create an option set object by isoburn_ropt_new(),
  program it by isoburn_ropt_set_*(), use it with isoburn_read_image(),
  and finally delete it by isoburn_ropt_destroy().

*/
/* ----------------------------------------------------------------------- */

struct isoburn_read_opts;

/** Produces a set of image read options, initialized with default values.
    @since 0.1.0
    @param o the newly created option set object
    @param flag  Bitfield for control purposes. Submit 0 for now.
    @return 1=ok , <0 = failure
*/
int isoburn_ropt_new(struct isoburn_read_opts **o, int flag);


/** Deletes an option set which was created by isoburn_ropt_new().
    @since 0.1.0
    @param o The option set to work on
    @param flag  Bitfield for control purposes. Submit 0 for now.
    @return 1= **o destroyed , 0= *o was already NULL (harmless)
*/
int isoburn_ropt_destroy(struct isoburn_read_opts **o, int flag);

/** Sets the size and granularity of the cache which libisoburn provides to
    libisofs for reading of ISO image data. This cache consists of several
    tiles which are buffers of a given size. The ISO image is divided into
    virtual tiles of that size. A cache tile may hold an in-memory copy
    of such a virtual image tile.
    When libisofs requests to read a block, then first the cache is inquired
    whether it holds that block. If not, then the block is read via libburn
    together with its neighbors in their virtual image tile into a free
    cache tile. If no cache tile is free, then the one will be re-used which
    has the longest time of not being hit by a read attempt.

    A larger cache might speed up image loading by reducing the number of
    libburn read calls on the directory tree. It might also help with
    reading the content of many small files, if for some reason it is not an
    option to sort access by LBA.
    Caching will not provide much benefit with libburn "stdio:" drives,
    because the operating system is supposed to provide the same speed-up
    in a more flexible way.

    @since 1.2.2
    @param o            The option set to work on.
                        It is permissible to submit NULL in order to just
                        have the parameters tested.
    @param cache_tiles  Number of tiles in the cache. Not less than 1.
                        Default is 32.
    @param tile_blocks  Number of blocks per tile. Must be a power of 2.
                        Default is 32.
                        cache_tiles * tile_blocks * 2048 must not exceed
                        1073741824 (= 1 GiB).
    @param flag         Bitfield for control purposes. Unused yet. Submit 0.
    @return             <=0 error , >0 ok
*/ 
int isoburn_ropt_set_data_cache(struct isoburn_read_opts *o,
                                int cache_tiles, int tile_blocks, int flag);

/** Inquire the current settings of isoburn_set_data_cache().
    @since 1.2.2
    @param o            The option set to work on.
                        NULL has the same effect as flag bit0.
    @param cache_tiles  Will return the number of tiles in the cache.
    @param tile_blocks  Will return the number of blocks per tile.
    @param set_flag     Will return control bits. None are defined yet.
    @param flag         Bitfield for control purposes
                        bit0= return default values rather than current ones
    @return             <=0 error , >0 reply is valid
*/
int isoburn_ropt_get_data_cache(struct isoburn_read_opts *o,
                                int *cache_tiles, int *tile_blocks,
                                int *set_flag, int flag);


/** Which existing ISO 9660 extensions in the image to read or not to read.
    Whether to read the content of an existing image at all.
    The bits can be combined by | resp. inquired by &.
    @since 0.1.0
    @param ext Bitfield:
               bit0= norock
                     Do not read Rock Ridge extensions
               bit1= nojoliet
                     Do not read Joliet extensions
               bit2= noiso1999
                     Do not read ISO 9660:1999 enhanced tree
               bit3= preferjoliet
                     When both Joliet and RR extensions are present, the RR
                     tree is used. If you prefer using Joliet, set this to 1.
               bit4= pretend_blank
                     Always create empty image.Ignore any image on input drive.
               bit5= noaaip
                     @since 0.3.4
                     Do not load AAIP information from image. This information
                     eventually contains ACL or XFS-style Extended Attributes.
               bit6= noacl
                     @since 0.3.4
                     Do not obtain ACL from external filesystem objects (e.g.
                     local filesystem files).
               bit7= noea
                     @since 0.3.4
                     Do not obtain XFS-style Extended Attributes from external
                     filesystem objects (e.g.  local filesystem files).
               bit8= noino
                     @since 0.4.0
                     Do not load eventual inode numbers from RRIP entry PX,
                     but generate a new unique inode number for each imported
                     IsoNode object.
                     PX inode numbers allow to mark families of hardlinks by
                     giving all family members the same inode number. libisofs
                     keeps the PX inode numbers unaltered when IsoNode objects
                     get written into an ISO image.
               bit9= nomd5
                     @since 0.4.2
                     Do not load the eventual MD5 checksum array.
                     Do not check eventual session_md5 tags.
              bit10= nomd5tag
                     @since 1.0.4
                     Do not check eventual session_md5 tags although bit9
                     is not set.
    @return    1 success, <=0 failure
*/
#define isoburn_ropt_norock         1
#define isoburn_ropt_nojoliet       2
#define isoburn_ropt_noiso1999      4
#define isoburn_ropt_preferjoliet   8
#define isoburn_ropt_pretend_blank 16
#define isoburn_ropt_noaaip        32
#define isoburn_ropt_noacl         64
#define isoburn_ropt_noea         128
#define isoburn_ropt_noino        256
#define isoburn_ropt_nomd5        512
#define isoburn_ropt_nomd5tag    1024

int isoburn_ropt_set_extensions(struct isoburn_read_opts *o, int ext);
int isoburn_ropt_get_extensions(struct isoburn_read_opts *o, int *ext);


/** Default attributes to use if no RockRidge extension gets loaded.
    @since 0.1.0
    @param o    The option set to work on
    @param uid  user id number (see /etc/passwd)
    @param gid  group id number (see /etc/group)
    @param mode  permissions (not file type) as of man 2 stat.
                 With directories, r-permissions will automatically imply
                 x-permissions. See isoburn_ropt_set_default_dirperms() below.
    @return      1 success, <=0 failure
*/
int isoburn_ropt_set_default_perms(struct isoburn_read_opts *o,
                                   uid_t uid, gid_t gid, mode_t mode);
int isoburn_ropt_get_default_perms(struct isoburn_read_opts *o,
                                   uid_t *uid, gid_t *gid, mode_t *mode);

/** Default attributes to use on directories if no RockRidge extension
    gets loaded.
    Above call isoburn_ropt_set_default_perms() automatically adds
    x-permissions to r-permissions for directories. This call here may
    be done afterwards to set independend permissions for directories,
    especially to override the automatically added x-permissions.
    @since 0.1.0
    @param o    The option set to work on
    @param mode permissions (not file type) as of man 2 stat.
    @return     1 success, <=0 failure
*/
int isoburn_ropt_set_default_dirperms(struct isoburn_read_opts *o,
                                      mode_t mode);
int isoburn_ropt_get_default_dirperms(struct isoburn_read_opts *o,
                                      mode_t *mode);


/** Set the character set for reading RR file names from ISO images.
    @since 0.1.0
    @param o             The option set to work on
    @param input_charset Set this to NULL to use the default locale charset
                         For selecting a particular character set, submit its
                         name, e.g. as listed by program iconv -l.
                         Example: "UTF-8". 
    @return              1 success, <=0 failure
*/
int isoburn_ropt_set_input_charset(struct isoburn_read_opts *o,
                                   char *input_charset);
int isoburn_ropt_get_input_charset(struct isoburn_read_opts *o,
                                   char **input_charset);


/**
    Enable or disable methods to automatically choose an input charset.
    This eventually overrides the name set via isoburn_ropt_set_input_charset()
    @since 0.3.8
    @param o      The option set to work on
    @param mode   Bitfield for control purposes:
                  bit0= allow to set the input character set automatically from
                        attribute "isofs.cs" of root directory.
                  Submit any other bits with value 0.
    @return       1 success, <=0 failure
 */
int isoburn_ropt_set_auto_incharset(struct isoburn_read_opts *o, int mode);
int isoburn_ropt_get_auto_incharset(struct isoburn_read_opts *o, int *mode);


/** Control an offset to be applied to all block address pointers in the ISO
    image in order to compensate for an eventual displacement of the image
    relative to the start block address for which it was produced.
    E.g. if track number 2 from CD gets copied into a disk file and shall then
    be loaded as ISO filesystem, then the directory tree and all data file
    content of the track copy will become readable by setting the track start
    address as displacement and -1 as displacement_sign.
    Data file content outside the track will of course not be accessible and
    eventually produce read errors.
    @since 0.6.6
    @param o                  The option set to work on
    @param displacement       0 or a positive number
    @param displacement_sign  Determines wether to add or subtract displacement
                              to block addresses before applying them to the
                              storage object for reading:
                              +1 = add , -1= subtract , else keep unaltered
*/
int isoburn_ropt_set_displacement(struct isoburn_read_opts *o,
                               uint32_t displacement, int displacement_sign);
int isoburn_ropt_get_displacement(struct isoburn_read_opts *o,
                               uint32_t *displacement, int *displacement_sign);

/* If you get here because of a compilation error like

       /usr/include/libisoburn/libisoburn.h:895: error:
       expected declaration specifiers or '...' before 'uint32_t'

   then see above paragraph "Application Constraints" about the definition
   of uint32_t.
*/


/** After calling function isoburn_read_image() there are informations
    available in the option set.
    This info can be obtained as bits in parameter has_what. Like:
      joliet_available = (has_what & isoburn_ropt_has_joliet);
    @since 0.1.0
    @param o     The option set to work on
    @param size  Number of image data blocks, 2048 bytes each.
    @param has_what Bitfield:
           bit0= has_rockridge
                 RockRidge extension info is available (POSIX filesystem)
           bit1= has_joliet
                 Joliet extension info is available (suitable for MS-Windows)
           bit2= has_iso1999
                 ISO version 2 Enhanced Volume Descriptor is available.
                 This is rather exotic.
           bit3= has_el_torito
                 El-Torito boot record is present
    @return  1 success, <=0 failure
*/
#define isoburn_ropt_has_rockridge 1
#define isoburn_ropt_has_joliet    2
#define isoburn_ropt_has_iso1999   4
#define isoburn_ropt_has_el_torito 8

int isoburn_ropt_get_size_what(struct isoburn_read_opts *o,
                               uint32_t *size, int *has_what);

/* ts A90122 */
/* >>> to be implemented:
#define isoburn_ropt_has_acl          64
#define isoburn_ropt_has_ea          128
*/



/* ----------------------------------------------------------------------- */
/*                      End of Options for image reading                   */
/* ----------------------------------------------------------------------- */

/* ----------------------------------------------------------------------- */
/*

  Options for image generation by libisofs and image transport to libburn.

  An application shall create an option set by isoburn_igopt_new(),
  program it by isoburn_igopt_set_*(), use it with either
  isoburn_prepare_new_image() or isoburn_prepare_disc(), and finally delete
  it by isoburn_igopt_destroy().

*/
/* ----------------------------------------------------------------------- */

struct isoburn_imgen_opts;

/** Produces a set of generation and transfer options, initialized with default
    values.
    @since 0.1.0
    @param o the newly created option set object
    @param flag  Bitfield for control purposes. Submit 0 for now.
    @return 1=ok , <0 = failure
*/
int isoburn_igopt_new(struct isoburn_imgen_opts **o, int flag);


/** Deletes an option set which was created by isoburn_igopt_new().
    @since 0.1.0
    @param o     The option set to give up
    @param flag  Bitfield for control purposes. Submit 0 for now.
    @return 1= **o destroyed , 0= *o was already NULL (harmless)
*/
int isoburn_igopt_destroy(struct isoburn_imgen_opts **o, int flag);


/** ISO level to write at.
    @since 0.1.0
    @param o     The option set to work on
    @param level is a term of the ISO 9660 standard. It should be one of:
                 1= filenames restricted to form 8.3
                 2= filenames allowed up to 31 characters
                 3= file content may be larger than 4 GB - 1.
    @return      1 success, <=0 failure
*/
int isoburn_igopt_set_level(struct isoburn_imgen_opts *o, int level);
int isoburn_igopt_get_level(struct isoburn_imgen_opts *o, int *level);


/** Which extensions to support.
    @since 0.1.0
    @param o     The option set to work on
    @param ext Bitfield:
           bit0= rockridge
                 Rock Ridge extensions add POSIX file attributes like
                 owner, group, access permissions, long filenames. Very
                 advisable if the designed audience has Unix style systems.
           bit1= joliet
                 Longer filenames for Windows systems.
                 Weaker than RockRidge, but also readable with GNU/Linux.
           bit2= iso1999
                 This is rather exotic. Better do not surprise the readers.
           bit3= hardlinks
                 Enable hardlink consolidation. IsoNodes which refer to the
                 same source object and have the same properties will get
                 the same ISO image inode numbers.
                 If combined with isoburn_igopt_rrip_version_1_10 below,
                 then the PX entry layout of RRIP-1.12 will be used within
                 RRIP-1.10 (mkisofs does this without causing visible trouble).
           bit5= aaip
                 The libisofs specific SUSP based extension of ECMA-119 which
                 can encode ACL and XFS-style Extended Attributes.
           bit6= session_md5
                 @since 0.4.2
                 Produce and write MD5 checksum tags of superblock, directory
                 tree, and the whole session stream.
           bit7= file_md5
                 @since 0.4.2
                 Produce and write MD5 checksums for each single IsoFile.
           bit8= file_stability (only together with file_md5)
                 @since 0.4.2
                 Compute MD5 of each file before copying it into the image and
                 compare this with the MD5 of the actual copying. If they do
                 not match then issue MISHAP event.
                 See also libisofs.h  iso_write_opts_set_record_md5()
           bit9= no_emul_toc
                 @since 0.5.8
                 On overwriteable media or random access files do not write
                 the first session to LBA 32 and do not copy the first 64kB
                 of the first session to LBA 0, but rather write the first
                 session to LBA 0 directly.
          bit10= will_cancel
                 @since 0.6.6
                 Announce to libisofs that only the image size is desired
                 and that the write thread will be cancelled by
                 isoburn_cancel_prepared_write() before actual image writing
                 occurs. Without this, cancellation can cause a MISHAP event.
          bit11= old_empty
                 @since 1.0.2
                 Let symbolic links and device files point to block 0, and let
                 empty data files point to the address of the Volume Descriptor
                 Set Terminator. This was done by libisofs in the past.
                 By default there is now a single dedicated block of zero bytes
                 after the end of the directory trees, of which the address
                 is used for all files without own content.
          bit12= hfsplus
                 @since 1.2.4
                 Produce a HFS+ partition inside the ISO image and announce it
                 by an Apple Partition Map in the System Area.
                 >>> GPT production ?
                 Caution: Interferes with isoburn_igopt_set_system_area() by
                          overwriting the first 8 bytes of the data, and
                          several blocks of 2 KiB after the first one.
          bit13= fat
                 @since 1.2.4
                 >>> Not yet implemented. Planned to co-exist with hfsplus.
                 Produce a FAT32 partition inside the ISO image and announce it
                 by an MBR partition entry in the System Area.
                 Caution: Interferes with isoburn_igopt_set_system_area() by
                          >>> what impact ?

    @return 1 success, <=0 failure
*/
#define isoburn_igopt_rockridge         1
#define isoburn_igopt_joliet            2
#define isoburn_igopt_iso1999           4
#define isoburn_igopt_hardlinks         8
#define isoburn_igopt_aaip             32
#define isoburn_igopt_session_md5      64
#define isoburn_igopt_file_md5        128
#define isoburn_igopt_file_stability  256
#define isoburn_igopt_no_emul_toc     512
#define isoburn_igopt_will_cancel    1024
#define isoburn_igopt_old_empty      2048
#define isoburn_igopt_hfsplus        4096
#define isoburn_igopt_fat            8192
int isoburn_igopt_set_extensions(struct isoburn_imgen_opts *o, int ext);
int isoburn_igopt_get_extensions(struct isoburn_imgen_opts *o, int *ext);

/** Relaxed constraints. Setting any of the bits to 1 break the specifications,
    but it is supposed to work on most moderns systems. Use with caution.
    @since 0.1.0
    @param o     The option set to work on
    @param relax Bitfield:
           bit0= omit_version_numbers
                 Omit the version number (";1") at the end of the
                 ISO-9660 and Joliet identifiers.
                 Version numbers are usually not used by readers.
           bit1= allow_deep_paths
                 Allow ISO-9660 directory hierarchy to be deeper
                 than 8 levels.
           bit2= allow_longer_paths
                 Allow path in the ISO-9660 tree to have more than
                 255 characters.
           bit3= max_37_char_filenames
                 Allow a single file or directory hierarchy to have
                 up to 37 characters. This is larger than the 31
                 characters allowed by ISO level 2, and the extra space
                 is taken from the version number, so this also forces
                 omit_version_numbers.
           bit4= no_force_dots
                 ISO-9660 forces filenames to have a ".", that separates
                 file name from extension. libisofs adds it if original
                 filename has none. Set this to 1 to prevent this
                 behavior.
           bit5= allow_lowercase 
                 Allow lowercase characters in ISO-9660 filenames.
                 By default, only uppercase characters, numbers and
                 a few other characters are allowed.
           bit6= allow_full_ascii
                 Allow all ASCII characters to be appear on an ISO-9660
                 filename. Note that "/" and "\0" characters are never
                 allowed, even in RR names.
           bit7= joliet_longer_paths
                 Allow paths in the Joliet tree to have more than
                 240 characters.
           bit8= always_gmt
                 Write timestamps as GMT although the specs prescribe local
                 time with eventual non-zero timezone offset. Negative
                 timezones (west of GMT) can trigger bugs in some operating
                 systems which typically appear in mounted ISO images as if
                 the timezone shift from GMT was applied twice
                 (e.g. in New York 22:36 becomes 17:36).
           bit9= rrip_version_1_10
                 Write Rock Ridge info as of specification RRIP-1.10 rather
                 than RRIP-1.12: signature "RRIP_1991A" rather than
                 "IEEE_1282", field PX without file serial number.
          bit10= dir_rec_mtime
                 Store as ECMA-119 Directory Record timestamp the mtime
                 of the source rather than the image creation time.
          bit11= aaip_susp_1_10
                 Write AAIP fields without announcing AAIP by an ER field and
                 without distinguishing RRIP fields from the AAIP field by
                 prefixed ES fields. This saves 5 to 10 bytes per file and
                 might avoid problems with readers which only accept RRIP.
                 SUSP-1.10 allows it, SUSP-1.12 frowns on it.
          bit12= only_iso_numbers
                 Same as bit1 omit_version_number but restricted to the names
                 in the eventual Joliet tree.
                 @since 0.5.4
                 For reasons of backward compatibility it is not possible yet
                 to disable version numbers for ISO 9660 while enabling them
                 for Joliet. 
          bit13= no_j_force_dots
                 Same as no_force_dots but affecting the names in the eventual
                 Joliet tree rather than the ISO 9660 / ECMA-119 names.
                 @since 0.5.4
                 Previous versions added dots to Joliet names unconditionally.
          bit14= allow_dir_id_ext
                 Convert directory names for ECMA-119 similar to other file
                 names, but do not force a dot or add a version number.
                 This violates ECMA-119 by allowing one "." and especially
                 ISO level 1 by allowing DOS style 8.3 names rather than
                 only 8 characters.
                 (mkisofs and its clones obviously do this violation.)
                 @since 1.0.0
          bit15= joliet_long_names
                 Allow for Joliet leaf names up to 103 characters rather than
                 up to 64.
                 @since 1.0.6
          bit16= joliet_rec_mtime
                 Like dir_rec_mtime, but for the Joliet tree.
                 @since 1.2.0
          bit17= iso1999_rec_mtime
                 Like dir_rec_mtime, but for the ISO 9660:1999 tree.
                 @since 1.2.0
          bit18= allow_7bit_ascii
                 Like allow_full_ascii, but only allowing 7-bit characters.
                 Lowercase letters get mapped to uppercase if not
                 allow_lowercase is set.
                 Gets overridden if allow_full_ascii is enabled.
    @return 1 success, <=0 failure
*/
#define isoburn_igopt_omit_version_numbers       1
#define isoburn_igopt_allow_deep_paths           2
#define isoburn_igopt_allow_longer_paths         4
#define isoburn_igopt_max_37_char_filenames      8
#define isoburn_igopt_no_force_dots             16
#define isoburn_igopt_allow_lowercase           32
#define isoburn_igopt_allow_full_ascii          64
#define isoburn_igopt_joliet_longer_paths      128
#define isoburn_igopt_always_gmt               256
#define isoburn_igopt_rrip_version_1_10        512
#define isoburn_igopt_dir_rec_mtime           1024
#define isoburn_igopt_aaip_susp_1_10          2048
#define isoburn_igopt_only_iso_versions       4096
#define isoburn_igopt_no_j_force_dots         8192
#define isoburn_igopt_allow_dir_id_ext       16384
#define isoburn_igopt_joliet_long_names      32768
#define isoburn_igopt_joliet_rec_mtime     0x10000
#define isoburn_igopt_iso1999_rec_mtime    0x20000
#define isoburn_igopt_allow_7bit_ascii     0x40000
int isoburn_igopt_set_relaxed(struct isoburn_imgen_opts *o, int relax);
int isoburn_igopt_get_relaxed(struct isoburn_imgen_opts *o, int *relax);


/** If not isoburn_igopt_allow_deep_paths is in effect, then it may become
    necessary to relocate directories so that no ECMA-119 file path
    has more than 8 components. These directories are grafted into either
    the root directory of the ISO image or into a dedicated relocation
    directory. For details see libisofs.h.
    Wrapper for: iso_write_opts_set_rr_reloc()
    @since 1.2.2
    @param o     The option set to work on
    @param name  The name of the relocation directory in the root directory.
                 Do not prepend "/". An empty name or NULL will direct
                 relocated directories into the root directory. This is the
                 default.
                 If the given name does not exist in the root directory when
                 isoburn_disc_write() is called, and if there are directories
                 at path level 8, then directory /name will be created
                 automatically.
    @param flags Bitfield for control purposes.
                 bit0= Mark the relocation directory by a Rock Ridge RE entry,
                       if it gets created during isoburn_disc_write(). This
                       will make it invisible for most Rock Ridge readers.
                 bit1= not settable via API (used internally)
    @return      > 0 success, <= 0 failure
*/
int isoburn_igopt_set_rr_reloc(struct isoburn_imgen_opts *o, char *name,
                               int flags);

/** Obtain the settings of isoburn_igopt_set_rr_reloc().
    @since 1.2.2
    @param o     The option set to work on
    @param name  Will return NULL or a pointer to the name of the relocation
                 directory in the root directory. Do not alter or dispose the
                 memory which holds this name.
    @param flags Will return the flags bitfield.
    @return      > 0 success, <= 0 failure
*/
int isoburn_igopt_get_rr_reloc(struct isoburn_imgen_opts *o, char **name,
                               int *flags);


/** Caution: This option breaks any assumptions about names that
             are supported by ECMA-119 specifications.
    Try to omit any translation which would make a file name compliant to the
    ECMA-119 rules. This includes and exceeds omit_version_numbers,
    max_37_char_filenames, no_force_dots bit0, allow_full_ascii. Further it
    prevents the conversion from local character set to ASCII.
    The maximum name length is given by this call. If a filename exceeds
    this length or cannot be recorded untranslated for other reasons, then
    image production gets aborted.
    Currently the length limit is 96 characters, because an ECMA-119 directory
    record may at most have 254 bytes and up to 158 other bytes must fit into
    the record. Probably 96 more bytes can be made free for the name in future.
    @since 1.0.0
    @param o    The option set to work on
    @param len  0 = disable this feature and perform name translation
                    according to other settings.
               >0 = Omit any translation. Eventually abort image production
                    if a name is longer than the given value.
               -1 = Like >0. Allow maximum possible length.
                    isoburn_igopt_get_untranslated_name_len() will tell the
                    effectively resulting value.
    @return >0 success, <=0 failure
*/
int isoburn_igopt_set_untranslated_name_len(struct isoburn_imgen_opts *o,
                                            int len);
int isoburn_igopt_get_untranslated_name_len(struct isoburn_imgen_opts *o,
                                            int *len);


/** Whether and how files should be sorted.
    @since 0.1.0
    @param o     The option set to work on
    @param value Bitfield: bit0= sort_files_by_weight
                                 files should be sorted based on their weight.
                                 Weight is attributed to files in the image
                                 by libisofs call iso_node_set_sort_weight().
    @return 1 success, <=0 failure
*/
#define isoburn_igopt_sort_files_by_weight        1
int isoburn_igopt_set_sort_files(struct isoburn_imgen_opts *o, int value);
int isoburn_igopt_get_sort_files(struct isoburn_imgen_opts *o, int *value);


/** Set the override values for files and directory permissions.
    The parameters replace_* these take one of three values: 0, 1 or 2.
    If 0, the corresponding attribute will be kept as set in the IsoNode
    at the time of image generation.
    If set to 1, the corresponding attrib. will be changed by a default
    suitable value.
    With value 2, the attrib. will be changed with the value specified
    in the corresponding *_mode options. Note that only the permissions
    are set, the file type remains unchanged.
    @since 0.1.0
    @param o                 The option set to work on
    @param replace_dir_mode  whether and how to override directories
    @param replace_file_mode whether and how to override files of other type
    @param dir_mode          Mode to use on dirs with replace_dir_mode == 2.
    @param file_mode;        Mode to use on files with replace_file_mode == 2.
    @return 1 success, <=0 failure
*/
int isoburn_igopt_set_over_mode(struct isoburn_imgen_opts *o,
                               int replace_dir_mode, int replace_file_mode,
                               mode_t dir_mode, mode_t file_mode);
int isoburn_igopt_get_over_mode(struct isoburn_imgen_opts *o,
                               int *replace_dir_mode, int *replace_file_mode,
                               mode_t *dir_mode, mode_t *file_mode);

/** Set the override values values for group id and user id.
    The rules are like with above overriding of mode values. replace_* controls
    whether and how. The other two parameters provide values for eventual use.
    @since 0.1.0
    @param o                 The option set to work on
    @param replace_uid       whether and how to override user ids
    @param replace_gid       whether and how to override group ids
    @param uid               User id to use with replace_uid == 2.
    @param gid               Group id to use on files with replace_gid == 2.
    @return 1 success, <=0 failure
*/
int isoburn_igopt_set_over_ugid(struct isoburn_imgen_opts *o,
                               int replace_uid, int replace_gid,
                               uid_t uid, gid_t gid);
int isoburn_igopt_get_over_ugid(struct isoburn_imgen_opts *o,
                               int *replace_uid, int *replace_gid,
                               uid_t *uid, gid_t *gid);

/** Set the charcter set to use for representing RR filenames in the image.
    @since 0.1.0
    @param o              The option set to work on
    @param output_charset Set this to NULL to use the default output charset.
                          For selecting a particular character set, submit its
                          name, e.g. as listed by program iconv -l.
                          Example: "UTF-8". 
    @return 1 success, <=0 failure
*/
int isoburn_igopt_set_out_charset(struct isoburn_imgen_opts *o,
                                 char *output_charset);
int isoburn_igopt_get_out_charset(struct isoburn_imgen_opts *o,
                                 char **output_charset);


/** The number of bytes to be used for the fifo which decouples libisofs
    and libburn for better throughput and for reducing the risk of
    interrupting signals hitting the libburn thread which operates the
    MMC drive.
    The size will be rounded up to the next full 2048.
    Minimum is 64kiB, maximum is 1 GiB (but that is too much anyway).
    @since 0.1.0
    @param o          The option set to work on
    @param fifo_size  Number of bytes to use
    @return 1 success, <=0 failure
*/
int isoburn_igopt_set_fifo_size(struct isoburn_imgen_opts *o, int fifo_size);
int isoburn_igopt_get_fifo_size(struct isoburn_imgen_opts *o, int *fifo_size);


/** Obtain after image preparation the block address where the session will
    start on the medium.
    This value cannot be set by the application but only be inquired.
    @since 0.1.4
    @param o          The option set to work on
    @param lba        The block number of the session start on the medium.
                      <0 means that no address has been determined yet.
    @return 1 success, <=0 failure
*/
int isoburn_igopt_get_effective_lba(struct isoburn_imgen_opts *o, int *lba);


/** Obtain after image preparation the lowest block address of file content
    data. Failure can occur if libisofs is too old to provide this information,
    if the result exceeds 31 bit, or if the call is made before image
    preparation.
    This value cannot be set by the application but only be inquired.
    @since 0.3.6
    @param o          The option set to work on
    @param lba        The block number of the session start on the medium.
                      <0 means that no address has been determined yet.
    @return 1 success, <=0 failure
*/
int isoburn_igopt_get_data_start(struct isoburn_imgen_opts *o, int *lba);


/** Set resp. get parameters "name" and "timestamp" for a scdbackup checksum
    tag. It will be appended to the libisofs session tag if the image starts at
    LBA 0. See isoburn_disc_track_lba_nwa. The scdbackup tag can be used
    to verify the image by command scdbackup_verify $device -auto_end.
    See scdbackup/README appendix VERIFY for its inner details.
    @since 0.4.4
    @param o          The option set to work on
    @param name       The tag name. 80 characters max.
    @param timestamp  A string of up to 13 characters YYMMDD.hhmmss
                      A9 = 2009, B0 = 2010, B1 = 2011, ... C0 = 2020, ...
    @param tag_written Either NULL or the address of an array with at least 512
                      characters. In the latter case the eventually produced
                      scdbackup tag will be copied to this array when the image
                      gets written. This call sets scdbackup_tag_written[0] = 0
                      to mark its preliminary invalidity.
    @return 1 success, <=0 failure
 */
int isoburn_igopt_set_scdbackup_tag(struct isoburn_imgen_opts *o, char *name,
                                    char *timestamp, char *tag_written);
int isoburn_igopt_get_scdbackup_tag(struct isoburn_imgen_opts *o,
                                    char name[81], char timestamp[19],
                                    char **tag_written);


/** Attach 32 kB of binary data which shall get written to the first 32 kB
    of the ISO image, the System Area.
    options can cause manipulations of these data before writing happens.
    If system area data are giveni or options bit0 is set, then bit1 of
    el_torito_set_isolinux_options() is automatically disabled.
    @since 0.5.4
    @param o        The option set to work on
    @param data     Either NULL or 32 kB of data. Do not submit less bytes !
    @param options  Can cause manipulations of submitted data before they
                    get written:
                    bit0= apply a --protective-msdos-label as of grub-mkisofs.
                          This means to patch bytes 446 to 512 of the system
                          area so that one partition is defined which begins
                          at the second 512-byte block of the image and ends
                          where the image ends.
                          This works with and without system_area_data.
                    bit1= apply isohybrid MBR patching to the system area.
                          This works only with system area data from
                          SYSLINUX plus an ISOLINUX boot image (see
                          iso_image_set_boot_image()) and only if not bit0
                          is set.
                    bit2-7= System area type
                          0= with bit0 or bit1: MBR
                             else: unspecified type 
                          @since 0.6.4
                          1= MIPS Big Endian Volume Header
                             Submit up to 15 MIPS Big Endian boot files by
                             iso_image_add_mips_boot_file() of libisofs.
                             This will overwrite the first 512 bytes of
                             the submitted data.
                          2= DEC Boot Block for MIPS Little Endian
                             The first boot file submitted by
                             iso_image_add_mips_boot_file() will be activated.
                             This will overwrite the first 512 bytes of
                             the submitted data.
                          @since 0.6.6
                          3= SUN Disk Label for SUN SPARC
                             Submit up to 7 SPARC boot images by
                             isoburn_igopt_set_partition_img() for partition
                             numbers 2 to 8.
                             This will overwrite the first 512 bytes of
                             the submitted data.
                    bit8-9= Only with System area type 0 = MBR
                          @since 1.0.4
                          Cylinder alignment mode eventually pads the image
                          to make it end at a cylinder boundary.
                          0 = auto (align if bit1)
                          1 = always align to cylinder boundary
                          2 = never align to cylinder boundary
                    bit10-13= System area sub type
                          @since 1.2.4 
                          With type 0 = MBR:
                            Gets overridden by bit0 and bit1. 
                            0 = no particular sub type
                            1 = CHRP: A single MBR partition of type 0x96
                                covers the ISO image. Not compatible with
                                any other feature which needs to have own
                                MBR partition entries.
                    bit14= Only with System area type 0 = MBR
                          GRUB2 boot provisions:
                          @since 1.3.0
                          Patch system area at byte 92 to 99 with 512-block
                          address + 1 of the first boot image file.
                          Little-endian 8-byte.
                          Should be combined with options bit0.
                          Will not be in effect if options bit1 is set.
    @return 1 success, 0 no data to get, <0 failure
*/
int isoburn_igopt_set_system_area(struct isoburn_imgen_opts *o,
                                  char data[32768], int options);
int isoburn_igopt_get_system_area(struct isoburn_imgen_opts *o,
                                  char data[32768], int *options);

/** Control production of a second set of volume descriptors (superblock)
    and directory trees, together with a partition table in the MBR where the
    first partition has non-zero start address and the others are zeroed.
    The first partition stretches to the end of the whole ISO image.
    The additional volume descriptor set and trees will allow to mount the
    ISO image at the start of the first partition, while it is still possible
    to mount it via the normal first volume descriptor set and tree at the
    start of the image resp. storage device.
    This makes few sense on optical media. But on USB sticks it creates a
    conventional partition table which makes it mountable on e.g. Linux via
    /dev/sdb and /dev/sdb1 alike.
    @since 0.6.2
    @param opts
           The option set to be manipulated.
    @param block_offset_2k
           The offset of the partition start relative to device start.
           This is counted in 2 kB blocks. The partition table will show the
           according number of 512 byte sectors.
           Default is 0 which causes no second set and trees.
           If it is not 0 then it must not be smaller than 16.
    @param secs_512_per_head
           Number of 512 byte sectors per head. 1 to 63. 0=automatic.
    @param heads_per_cyl
           Number of heads per cylinder. 1 to 255. 0=automatic.
    @return 1 success, <=0 failure
 */
int isoburn_igopt_set_part_offset(struct isoburn_imgen_opts  *opts,
                                  uint32_t block_offset_2k,
                                  int secs_512_per_head, int heads_per_cyl);
int isoburn_igopt_get_part_offset(struct isoburn_imgen_opts *opts,
                                  uint32_t *block_offset_2k,
                                  int *secs_512_per_head, int *heads_per_cyl);


/** Explicitely set the four timestamps of the emerging ISO image.
    Default with all parameters is 0.
    @since 0.5.4
    @param opts
           The option set to work on
    @param creation_time
           ECMA-119 Volume Creation Date and Time
           When "the information in the volume was created."
           A value of 0 means that the timepoint of write start is to be used.
    @param modification_time
           ECMA-119 Volume Modification Date and Time
           When "the informationin the volume was last modified."
           A value of 0 means that the timepoint of write start is to be used.
    @param expiration_time
           ECMA-119 Volume Expiration Date and Time
           When "the information in the volume may be regarded as obsolete."
           A value of 0 means that the information never shall expire.
    @param effective_time
           ECMA-119 Volume Effective Date and Time
           When "the information in the volume may be used."
           A value of 0 means that not such retention is intended.
    @param uuid
           If this text is not empty, then it overrides vol_modification_time
           by copying the first 16 decimal digits from uuid, eventually
           padding up with decimal '1', and writing a NUL-byte as timezone GMT.
           It should express a reasonable time in form  YYYYMMDDhhmmsscc
           E.g.:  2010040711405800 = 7 Apr 2010 11:40:58 (+0 centiseconds)
    @return 1 success, <=0 failure
 */
int isoburn_igopt_set_pvd_times(struct isoburn_imgen_opts *opts,
                        time_t creation_time, time_t modification_time,
                        time_t expiration_time, time_t effective_time,
                        char *uuid);
int isoburn_igopt_get_pvd_times(struct isoburn_imgen_opts *opts,
                      time_t *creation_time, time_t *modification_time,
                      time_t *expiration_time, time_t *effective_time,
                      char uuid[17]);


/** Associate a libjte environment object to the upcomming write run.
    libjte implements Jigdo Template Extraction as of Steve McIntyre and
    Richard Atterer.
    A non-NULL libjte_handle will cause failure to write if libjte was not
    enabled in libisofs at compile time.
    @since 0.6.4
    @param opts
           The option set to work on
    @param libjte_handle
           Pointer to a struct libjte_env e.g. created by libjte_new().
           It must stay existent from the start of image writing by
           isoburn_prepare_*() until the write thread has ended.
           E.g. until libburn indicates the end of its write run.
    @return 1 success, <=0 failure
*/
int isoburn_igopt_attach_jte(struct isoburn_imgen_opts *opts,
                             void *libjte_handle);

/** Remove eventual association to a libjte environment handle.
    @since 0.6.4
    @param opts
           The option set to work on
    @param libjte_handle
           If not submitted as NULL, this will return the previously set
           libjte handle. 
    @return 1 success, <=0 failure
*/
int isoburn_igopt_detach_jte(struct isoburn_imgen_opts *opts,
                             void **libjte_handle);


/** Set resp. get the number of trailing zero byte blocks to be written by
    libisofs. The image size counter of the emerging ISO image will include
    them. Eventual checksums will take them into respect.
    They will be written immediately before the eventual image checksum area
    which is at the very end of the image.
    For a motivation see iso_write_opts_set_tail_blocks() in libisofs.h .
    @since 0.6.4
    @param opts
           The option set to work on
    @aram num_blocks
           Number of extra 2 kB blocks to be written by libisofs.
    @return 1 success, <=0 failure
*/
int isoburn_igopt_set_tail_blocks(struct isoburn_imgen_opts *opts,
                                  uint32_t num_blocks);
int isoburn_igopt_get_tail_blocks(struct isoburn_imgen_opts *opts,
                                  uint32_t *num_blocks);


/** Copy a data file from the local filesystem into the emerging ISO image.
    Mark it by an MBR partition entry as PreP partition and also cause
    protective MBR partition entries before and after this partition.
    See libisofs.h iso_write_opts_set_prep_img().
    @since 1.2.4
    @param opts
           The option set to be manipulated.
    @param path 
           File address in the local file system.
    @param flag
           Reserved for future usage, set to 0.
    @return 1 success, <=0 failure
*/
int isoburn_igopt_set_prep_partition(struct isoburn_imgen_opts *opts,
                                     char *path, int flag);
int isoburn_igopt_get_prep_partition(struct isoburn_imgen_opts *opts,
                                     char **path, int flag);

/** Copy a data file from the local filesystem into the emerging ISO image.
    @since 1.2.4
    @param opts
           The option set to be manipulated.
    @param path 
           File address in the local file system.
    @param flag
           Reserved for future usage, set to 0.
    @return 1 success, <=0 failure
*/
int isoburn_igopt_set_efi_bootp(struct isoburn_imgen_opts *opts,
                                char *path, int flag);
int isoburn_igopt_get_efi_bootp(struct isoburn_imgen_opts *opts,
                                char **path, int flag);


/** Cause an arbitrary data file to be appended to the ISO image and to be
    described by a partition table entry in an MBR or SUN Disk Label at the
    start of the ISO image.
    The partition entry will bear the size of the image file rounded up to
    the next multiple of 2048 bytes.
    MBR or SUN Disk Label are selected by isoburn_igopt_set_system_area()
    system area type: 0 selects MBR partition table. 3 selects a SUN partition
    table with 320 kB start alignment.
    @since 0.6.4
    @param opts
           The option set to be manipulated.
    @param partition_number
           Depicts the partition table entry which shall describe the
           appended image.
           Range with MBR: 1 to 4. 1 will cause the whole ISO image to be
                           unclaimable space before partition 1.
           @since 0.6.6
           Range with SUN Disk Label: 2 to 8.
    @param image_path 
           File address in the local file system.
           With SUN Disk Label: an empty name causes the partition to become
           a copy of the next lower partition.
    @param image_type
           The MBR partition type. E.g. FAT12 = 0x01 , FAT16 = 0x06,
           Linux Native Partition = 0x83. See fdisk command L.
           This parameter is ignored with SUN Disk Label.
    @return
           <=0 = error, 1 = success
*/
int isoburn_igopt_set_partition_img(struct isoburn_imgen_opts *opts,
                                  int partition_number, uint8_t partition_type,
                                  char *image_path);

/** Inquire the current settings made by isoburn_igopt_set_partition_img().
    @since 0.6.4
    @param opts
           The option set to be inquired.
    @param num_entries
           Number of array elements in partition_types[] and image_paths[].
    @param partition_types
           The partition type associated with the partition. Valid only if
           image_paths[] of the same index is not NULL.
    @param image_paths
           Its elements get filled with either NULL or a pointer to a string
           with a file address resp. an empty text.
    @return
           <0 = error
            0 = no partition image set
           >0 highest used partition number
*/
int isoburn_igopt_get_partition_img(struct isoburn_imgen_opts *opts,
                                    int num_entries,
                                    uint8_t partition_types[],
                                    char *image_paths[]);


/** Set a name for the system area. This setting is ignored unless system area
    type 3 "SUN Disk Label" is in effect by iso_write_opts_set_system_area().
    In this case it will replace the default text at the start of the image:
      "CD-ROM Disc with Sun sparc boot created by libisofs"
    @since 0.6.6
    @param opts
           The option set to be manipulated.
    @param label
           A text of up to 128 characters.
    @return
           <=0 = error, 1 = success
*/ 
int isoburn_igopt_set_disc_label(struct isoburn_imgen_opts *opts, char *label);

/** Inquire the current setting made by isoburn_igopt_set_disc_label().
    @since 0.6.6
    @param opts
           The option set to be inquired.
    @param label
           Returns a pointer to the currently set label string.
           Do not alter this string.
           Use only as long as the opts object exists.
    @return
           <=0 = error, 1 = success
*/
int isoburn_igopt_get_disc_label(struct isoburn_imgen_opts *opts,
                                 char **label);

/** Set a serial number for the HFS+ extension of the emerging ISO image.
    @since 1.2.4
    @param opts
           The option set to be manipulated.
    @param serial_number
           8 bytes which should be unique to the image.
           If all bytes are 0, then the serial number will be generated as
           random number by libisofs. This is the default setting.
    @return
           <=0 = error, 1 = success
*/  
int isoburn_igopt_set_hfsp_serial_number(struct isoburn_imgen_opts *opts,
                                         uint8_t serial_number[8]);


/** Inquire the current setting made by isoburn_igopt_set_disc_label()
    @since 1.2.4
    @param opts
           The option set to be inquired.
    @param serial_number
           Will get filled with the current setting.
    @return
           <=0 = error, 1 = success
*/
int isoburn_igopt_get_hfsp_serial_number(struct isoburn_imgen_opts *opts,
                                         uint8_t serial_number[8]);

/** Set the allocation block size for HFS+ production and the block size
    for layout and address unit of Apple Partition map.
    @since 1.2.4
    @param opts
           The option set to be manipulated.
    @param hfsp_block_size
           -1 means that this setting shall be left unchanged
           0 allows the automatic default setting
           512 and 2048 enforce a size.
    @param apm_block_size
           -1 means that this setting shall be left unchanged
           0 allows the automatic default setting
           512 and 2048 enforce a size.
           Size 512 cannot be combined with GPT production.
           Size 2048 cannot be mounted -t hfsplus by Linux kernels at least up
           to 2.6.32.
    @return
           <=0 = error, 1 = success
*/
int isoburn_igopt_set_hfsp_block_size(struct isoburn_imgen_opts *opts,
                                      int hfsp_block_size, int apm_block_size);

/** Inquire the current setting made by isoburn_igopt_set_hfsp_block_size
    @since 1.2.4
    @param opts
           The option set to be inquired.
    @param hfsp_block_size
           Will be set to a value as described above. Except -1.
    @param apm_block_size
           Will be set to a value as described above. Except -1.
    @return
           <=0 = error, 1 = success
*/
int isoburn_igopt_get_hfsp_block_size(struct isoburn_imgen_opts *opts,
                                    int *hfsp_block_size, int *apm_block_size);


/** Set or inquire the write type for the next write run on optical media.
    @since 1.2.4
    @param opts
           The option set to be manipulated or inquired.
    @param do_tao
           The value to be set or the variable where to return the current
           setting:
            0 = Let libburn choose according to other write parameters.
                This is advisable unless there are particular reasons not to
                use one of the two write types. Be aware that 1 and -1 can
                lead to failure if the write type is not appropriate for
                the given media situation.
            1 = Use BURN_WRITE_TAO which does
                TAO on CD, Incremental on DVD-R,
                no track reservation on DVD+R and BD-R
           -1 = Use BURN_WRITE_SAO which does
                SAO on CD, DAO on DVD-R,
                track reservation on DVD+R and BD-R
    @return
           <=0 = error, 1 = success
*/
int isoburn_igopt_set_write_type(struct isoburn_imgen_opts *opts, int do_tao);
int isoburn_igopt_get_write_type(struct isoburn_imgen_opts *opts, int *do_tao);


/* ----------------------------------------------------------------------- */
/*                      End of Options for image generation                */
/* ----------------------------------------------------------------------- */


/** Get the image attached to a drive, if any.
    @since 0.1.0
    @param d The drive to inquire
    @return A reference to attached image, or NULL if the drive has no image
            attached. This reference needs to be released via iso_image_unref()
            when it is not longer needed.
*/
IsoImage *isoburn_get_attached_image(struct burn_drive *d);

/** Get the start address of the image that is attached to the drive, if any.
    @since 1.2.2
    @param d The drive to inquire
    @return  The logical block address where the System Area of the image
             starts. <0 means that the address is invalid.
*/
int isoburn_get_attached_start_lba(struct burn_drive *d);


/** Load the ISO filesystem directory tree from the medium in the given drive.
    This will give libisoburn the base on which it can let libisofs perform
    image growing or image modification. The loaded volset gets attached
    to the drive object and handed out to the application.
    Not a wrapper, but peculiar to libisoburn.
    @since 0.1.0
    @param d The drive which holds an existing ISO filesystem or blank media.
             d is allowed to be NULL which produces an empty ISO image. In
             this case one has to call before writing isoburn_attach_volset()
             with the volset from this call and with the intended output
             drive.
    @param read_opts The read options which can be chosen by the application
    @param image the image read, if the disc is blank it will have no files.
             This reference needs to be released via iso_image_unref() when
             it is not longer needed. The drive, if not NULL, will hold an
             own reference which it will release when it gets a new volset
             or when it gets released via isoburn_drive_release().
             You can pass NULL if you already have a reference or you plan to
             obtain it later with isoburn_get_attached_image(). Of course, if
             you haven't specified a valid drive (i.e., if d == NULL), this
             parameter can't be NULL.
    @return <=0 error , 1 = success
*/
int isoburn_read_image(struct burn_drive *d,
                       struct isoburn_read_opts *read_opts,
                       IsoImage **image);

/** Set a callback function for producing pacifier messages during the lengthy
    process of image reading. The callback function and the application handle
    are stored until they are needed for the underlying call to libisofs.
    Other than with libisofs the handle is managed entirely by the application.
    An idle .free() function is exposed to libisofs. The handle has to stay
    valid until isoburn_read_image() is done. It has to be detached by
      isoburn_set_read_pacifier(drive, NULL, NULL);
    before it may be removed from memory.
    @since 0.1.0
    @param drive  The drive which will be used with isoburn_read_image()
                  It has to be acquired by an isoburn_* wrapper call.
    @param read_pacifier  The callback function
    @param app_handle  The app handle which the callback function can obtain
                       via iso_image_get_attached_data() from its IsoImage*
    @return 1 success, <=0 failure
*/
int isoburn_set_read_pacifier(struct burn_drive *drive,
                              int (*read_pacifier)(IsoImage*, IsoFileSource*),
                              void *app_handle);

/** Inquire the partition offset of the loaded image. The first 512 bytes of
    the image get examined whether they bear an MBR signature and a first
    partition table entry which matches the size of the image. In this case
    the start address is recorded as partition offset and internal buffers
    get adjusted.
    See also isoburn_igopt_set_part_offset().
    @since 0.6.2
    @param drive           The drive with the loaded image
    @param block_offset_2k returns the recognized partition offset
    @return <0 = error
             0 = no partition offset recognized
             1 = acceptable non-zero offset, buffers are adjusted
             2 = offset is credible but not acceptable for buffer size
*/ 
int isoburn_get_img_partition_offset(struct burn_drive *drive,
                                     uint32_t *block_offset_2k);


/** Set the IsoImage to be used with a drive. This eventually releases
    the reference to the old IsoImage attached to the drive.
    Caution: Use with care. It hardly makes sense to replace an image that
             reflects a valid ISO image on the medium.
    This call is rather intended for writing a newly created and populated
    image to blank media. The use case in xorriso is to let an image survive
    the change or demise of the outdev target drive. 
    @since 0.1.0
    @param d The drive which shall be write target of the volset.
    @param image The image that represents the image to be written.
             This image pointer MUST already be a valid reference suitable
             for iso_image_unref().
             It may have been obtained by appropriate libisofs calls or by
             isoburn_read_image() with d==NULL.
    @return <=0 error , 1 = success
*/ 
int isoburn_attach_image(struct burn_drive *d, IsoImage *image);


/** Set the start address of the image that is attached to the drive, if any.
    @since 1.2.2
    @param d    The drive to inquire
    @param lba  The logical block address where the System Area of the image
                starts. <0 means that the address is invalid.
    @param flag Bitfield, submit 0 for now.
    @return     <=0 error (e.g. because no image is attached), 1 = success
*/
int isoburn_attach_start_lba(struct burn_drive *d, int lba, int flag);


/** Return the best possible estimation of the currently available capacity of
    the medium. This might depend on particular write option settings and on
    drive state.
    An eventual start address for emulated multi-session will be subtracted
    from the capacity estimation given by burn_disc_available_space().
    Negative results get defaulted to 0.
    Wrapper for: burn_disc_available_space()
    @since 0.1.0
    @param d The drive to query.
    @param o If not NULL: write parameters to be set on drive before query
    @return number of most probably available free bytes
*/
off_t isoburn_disc_available_space(struct burn_drive *d,
                                   struct burn_write_opts *o);


/** Obtain the start block number of the most recent session on the medium. In
    case of random access media this will normally be 0. Successfull return is
    not a guarantee that there is a ISO-9660 image at all. The call will fail,
    nevertheless,if isoburn_disc_get_status() returns not BURN_DISC_APPENDABLE
    or BURN_DISC_FULL.
    Note: The result of this call may be fabricated by a previous call of
    isoburn_set_msc1() which can override the rule to load the most recent
    session.
    Wrapper for: burn_disc_get_msc1()
    @since 0.1.0
    @param d         The drive to inquire
    @param start_lba Contains on success the start address in 2048 byte blocks
    @return <=0 error , 1 = success
*/
int isoburn_disc_get_msc1(struct burn_drive *d, int *start_lba);


/** Use this with trackno==0 to obtain the predicted start block number of the
    new session. The interesting number is returned in parameter nwa.
    Wrapper for: burn_disc_track_lba_nwa()
    @since 0.1.0
    @param d         The drive to inquire
    @param o If not NULL: write parameters to be set on drive before query
    @param trackno Submit 0.
    @param lba return value: start lba
    @param nwa return value: Next Writeable Address
    @return 1=nwa is valid , 0=nwa is not valid , -1=error
*/
int isoburn_disc_track_lba_nwa(struct burn_drive *d, struct burn_write_opts *o,
                               int trackno, int *lba, int *nwa);


/** Obtain the size which was attributed to an emulated appendable on actually
    overwriteable media. This value is supposed to be <= 2048 * nwa as of
    isoburn_disc_track_lba_nwa().
    @since 0.1.0
    @param d     The drive holding the medium.
    @param start_byte The reply value counted in bytes, not in sectors.
    @param flag  Unused yet. Submit 0.
    @return 1=stat_byte is valid, 0=not an emulated appendable, -1=error 
*/
int isoburn_get_min_start_byte(struct burn_drive *d, off_t *start_byte,
                               int flag);


/** To choose the expansion method of Growing:
    Create a disc object for writing the new session from the created or loaded
    iso_volset which has been manipulated via libisofs, to the same medium from
    where the image was eventually loaded. This struct burn_disc is ready for
    use by a subsequent call to isoburn_disc_write().
    After this asynchronous writing has ended and the drive is BURN_DRIVE_IDLE
    again, the burn_disc object has to be disposed by burn_disc_free().
    @since 0.1.0
    @param drive The combined source and target drive, grabbed with
                 isoburn_drive_scan_and_grab(). .
    @param disc Returns the newly created burn_disc object.
    @param opts Image generation options, see isoburn_igopt_*()
    @return <=0 error , 1 = success
*/
int isoburn_prepare_disc(struct burn_drive *drive, struct burn_disc **disc,
                         struct isoburn_imgen_opts *opts);


/** To choose the expansion method of Modifying:
    Create a disc object for producing a new image from a previous image
    plus the changes made by user. The generated burn_disc is suitable
    to be written to a grabbed drive with blank writeable medium.
    But you must not use the same drive for input and output, because data
    will be read from the source drive while at the same time the target
    drive is already writing.
    The resulting burn_disc object has to be disposed when all its writing
    is done and the drive is BURN_DRIVE_IDLE again after asynchronous
    burn_disc_write().
    @since 0.1.0
    @param in_drive The input drive, grabbed with isoburn_drive_aquire() or
                    one of its alternatives.
    @param disc     Returns the newly created burn_disc object.
    @param opts     Options for image generation and data transport to the
                    medium.
    @param out_drive The output drive, from isoburn_drive_aquire() et.al..
    @return <=0 error , 1 = success
*/
int isoburn_prepare_new_image(struct burn_drive *in_drive,
                              struct burn_disc **disc,
                              struct isoburn_imgen_opts *opts,
                              struct burn_drive *out_drive);


/** To choose the expansion method of Blind Growing:
    Create a disc object for writing an add-on session from the created or
    loaded IsoImage which has been manipulated via libisofs, to a different
    drive than the one from where it was loaded.
    Usually output will be stdio:/dev/fd/1 (i.e. stdout) being piped
    into some burn program like with this classic gesture:
      mkisofs -M $dev -C $msc1,$nwa | cdrecord -waiti dev=$dev
    Parameter translation into libisoburn:
      $dev  is the address by which parameter in_drive of this call was
            acquired $msc1 was set by isoburn_set_msc1() before image reading
            or was detected from the in_drive medium
      $nwa  is a parameter of this call
            or can be used as detected from the in_drive medium

    This call waits for libisofs output to become available and then detaches
    the input drive object from the data source object by which libisofs was
    reading from the input drive.
    So, as far as libisofs is concerned, that drive may be released immediately
    after this call in order to allow the consumer to access the drive for
    writing.
    The consumer should wait for input to become available and only then open
    its burn drive. With cdrecord this is caused by option -waiti.
  
    The resulting burn_disc object has to be disposed when all its writing
    is done and the drive is BURN_DRIVE_IDLE again after asynchronous
    burn_disc_write().
    @since 0.2.2
    @param in_drive The input drive,grabbed with isoburn_drive_scan_and_grab().
    @param disc     Returns the newly created burn_disc object.
    @param opts     Options for image generation and data transport to media.
    @param out_drive The output drive, from isoburn_drive_aquire() et.al..
                    typically stdio:/dev/fd/1 .
    @param nwa      The address (2048 byte block count) where the add-on
                    session will be finally stored on a mountable medium
                    or in a mountable file.
                    If nwa is -1 then the address is used as determined from
                    the in_drive medium.
    @return <=0 error , 1 = success
*/
int isoburn_prepare_blind_grow(struct burn_drive *in_drive,
                               struct burn_disc **disc,
                               struct isoburn_imgen_opts *opts,
                               struct burn_drive *out_drive, int nwa);


/**
    Revoke isoburn_prepare_*() instead of running isoburn_disc_write().
    libisofs reserves resources and maybe already starts generating the
    image stream when one of above three calls is performed. It is mandatory to
    either run isoburn_disc_write() or to revoke the preparations by the
    call described here.
    If this call returns 0 or 1 then the write thread of libisofs has ended.
    @since 0.1.0
    @param input_drive   The drive resp. in_drive which was used with the
                         preparation call.
    @param output_drive  The out_drive used with isoburn_prepare_new_image(),
                         NULL if none.
    @param flag Bitfield, submit 0 for now.
                bit0= -reserved for internal use-
    @return     <0 error, 0= no pending preparations detectable, 1 = canceled
*/
int isoburn_cancel_prepared_write(struct burn_drive *input_drive,
                                  struct burn_drive *output_drive, int flag);


/**
    Override the truncation setting that was made with flag bit2 during the
    call of isoburn_drive_aquire. This applies only to stdio pseudo drives.
    @since 0.1.6
    @param drive The drive which was acquired and shall be used for writing.
    @param flag Bitfield controlling the setting:
                bit0= truncate (else do not truncate)
                bit1= do not warn if call is inappropriate to drive
                bit2= only set if truncation is currently enabled
                      do not warn if call is inappropriate to drive
    @return     1 success, 0 inappropriate drive, <0 severe error
*/
int isoburn_set_truncate(struct burn_drive *drive, int flag);


/** Start writing of the new session.
    This call is asynchrounous. I.e. it returns quite soon and the progress has
    to be watched by a loop with call burn_drive_get_status() until
    BURN_DRIVE_IDLE is returned.
    Wrapper for: burn_disc_write()
    @since 0.1.0
    @param o    Options which control the burn process. See burnwrite_opts_*()
                in libburn.h.
    @param disc Disc object created either by isoburn_prepare_disc() or by
                isoburn_prepare_new_image().
*/
void isoburn_disc_write(struct burn_write_opts *o, struct burn_disc *disc);


/** Inquire state and fill parameters of the fifo which is attached to
    the emerging track. This should be done in the pacifier loop while
    isoburn_disc_write() or burn_disc_write() are active.
    This works only with drives obtained by isoburn_drive_scan_and_grab()
    or isoburn_drive_grab(). If isoburn_prepare_new_image() was used, then
    parameter out_drive must have announced the track output drive.
    Hint: If only burn_write_opts and not burn_drive is known, then the drive
          can be obtained by burn_write_opts_get_drive().
    @since 0.1.0
    @param d     The drive to which the track with the fifo gets burned.
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
*/
int isoburn_get_fifo_status(struct burn_drive *d, int *size, int *free_bytes,
                            char **status_text);


/** Inquire whether the most recent write run was successful.
    Wrapper for: burn_drive_wrote_well()
    @since 0.1.0
    @param d  The drive to inquire
    @return   1=burn seems to have went well, 0=burn failed
*/
int isoburn_drive_wrote_well(struct burn_drive *d);


/** Call this after isoburn_disc_write has finished and burn_drive_wrote_well()
    indicates success. It will eventually complete the emulation of
    multi-session functionality, if needed at all. Let libisoburn decide.
    Not a wrapper, but peculiar to libisoburn.
    @since 0.1.0
    @param d  The output drive to which the session was written
    @return   1 success , <=0 failure
*/
int isoburn_activate_session(struct burn_drive *d);


/** Wait after normal end of operations until libisofs ended all write
    threads and freed resource reservations.
    This call is not mandatory. But without it, messages from the ending
    threads might appear after the application ended its write procedure.
    @since 0.1.0
    @param input_drive   The drive resp. in_drive which was used with the
                         preparation call.
    @param output_drive  The out_drive used with isoburn_prepare_new_image(),
                         NULL if none.
    @param flag Bitfield, submit 0 for now.
    @return     <=0 error , 1 = success
*/
int isoburn_sync_after_write(struct burn_drive *input_drive,
                             struct burn_drive *output_drive, int flag);


/** Release an acquired drive.
    Wrapper for: burn_drive_release()
    @since 0.1.0
    @param drive The drive to be released
    @param eject 1= eject medium from drive , 0= do not eject
*/
void isoburn_drive_release(struct burn_drive *drive, int eject);


/** Shutdown all three libraries.
    Wrapper for : iso_finish() and burn_finish().
    @since 0.1.0
*/
void isoburn_finish(void);


/*
    The following calls are for expert applications only.
    An application should have a special reason to use them.
*/


/** Inquire wether the medium needs emulation or would be suitable for
    generic multi-session via libburn.
    @since 0.1.0
    @param d  The drive to inquire
    @return 0 is generic multi-session 
            1 is emulated multi-session
           -1 is not suitable for isoburn
*/
int isoburn_needs_emulation(struct burn_drive *d);
 

/* ---------------------------- Test area ----------------------------- */

/* no tests active, currently */

