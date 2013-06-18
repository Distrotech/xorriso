
/*
   cc -g -c \
      -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE=1 -D_LARGEFILE64_SOURCE \
      burn_wrap.c
*/
/* libburn wrappers for libisoburn 

   Copyright 2007 - 2011  Thomas Schmitt, <scdbackup@gmx.net> 
   Provided under GPL version 2 or later.
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

/* <<< A70929 : hardcoded CD-RW with fabricated -msinfo 
#define Hardcoded_cd_rW 1
#define Hardcoded_cd_rw_c1     12999
#define Hardcoded_cd_rw_nwA   152660 
*/

#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <regex.h>

#ifndef Xorriso_standalonE

#include <libburn/libburn.h>
#include <libisofs/libisofs.h>
#ifdef Xorriso_with_libjtE
#include <libjte/libjte.h>
#endif

#else /* ! Xorriso_standalonE */

#include "../libisofs/libisofs.h"
#include "../libburn/libburn.h"
#ifdef Xorriso_with_libjtE
#include "../libjte/libjte.h"
#endif

#endif /* Xorriso_standalonE */


#include "libisoburn.h"
#include "isoburn.h"


/* The global list of isoburn objects. Usually there is only one. */
extern struct isoburn *isoburn_list_start; /* in isoburn.c */

/* Default values for application provided msgs_submit methods.
   To be attached to newly aquired drives.
   Storage location is isoburn.c
*/
extern int (*libisoburn_default_msgs_submit)
    (void *handle, int error_code, char msg_text[],
                 int os_errno, char severity[], int flag);
extern void *libisoburn_default_msgs_submit_handle;
extern int libisoburn_default_msgs_submit_flag;


static int isoburn_emulate_toc(struct burn_drive *d, int flag);


int isoburn_initialize(char msg[1024], int flag)
{
 int major, minor, micro, bad_match= 0, no_iso_init= 0;


/* First the ugly compile time checks for header version compatibility.
   If everthing matches, then they produce no C code. In case of mismatch,
   intentionally faulty C code will be inserted.
*/

#ifdef iso_lib_header_version_major
/* The minimum requirement of libisoburn towards the libisofs header
   at compile time is defined in libisoburn/libisoburn.h :
     isoburn_libisofs_req_major
     isoburn_libisofs_req_minor
     isoburn_libisofs_req_micro
   It gets compared against the version macros in libisofs/libisofs.h :
     iso_lib_header_version_major
     iso_lib_header_version_minor
     iso_lib_header_version_micro
   If the header is too old then the following code shall cause failure of
   libisoburn compilation rather than to allow production of a program with
   unpredictable bugs or memory corruption.
   The compiler messages supposed to appear in this case are:
      error: 'LIBISOFS_MISCONFIGURATION' undeclared (first use in this function)
      error: 'INTENTIONAL_ABORT_OF_COMPILATION__HEADERFILE_libisofs_dot_h_TOO_OLD__SEE_libisoburn_dot_h_AND_burn_wrap_dot_h' undeclared (first use in this function)
      error: 'LIBISOFS_MISCONFIGURATION_' undeclared (first use in this function)
*/
/* The indendation is an advise of man gcc to help old compilers ignoring */
 #if isoburn_libisofs_req_major > iso_lib_header_version_major
 #define Isoburn_libisofs_dot_h_too_olD 1
 #endif
 #if isoburn_libisofs_req_major == iso_lib_header_version_major && isoburn_libisofs_req_minor > iso_lib_header_version_minor
 #define Isoburn_libisofs_dot_h_too_olD 1
 #endif
 #if isoburn_libisofs_req_minor == iso_lib_header_version_minor && isoburn_libisofs_req_micro > iso_lib_header_version_micro
 #define Isoburn_libisofs_dot_h_too_olD 1
 #endif

#ifdef Isoburn_libisofs_dot_h_too_olD
LIBISOFS_MISCONFIGURATION = 0;
INTENTIONAL_ABORT_OF_COMPILATION__HEADERFILE_libisofs_dot_h_TOO_OLD__SEE_libisoburn_dot_h_AND_burn_wrap_dot_c = 0;
LIBISOFS_MISCONFIGURATION_ = 0;
#endif

#endif /* iso_lib_header_version_major */

/* The minimum requirement of libisoburn towards the libburn header
   at compile time is defined in libisoburn/libisoburn.h :
     isoburn_libburn_req_major
     isoburn_libburn_req_minor
     isoburn_libburn_req_micro
   It gets compared against the version macros in libburn/libburn.h :
     burn_header_version_major
     burn_header_version_minor
     burn_header_version_micro
   If the header is too old then the following code shall cause failure of
   cdrskin compilation rather than to allow production of a program with
   unpredictable bugs or memory corruption.
   The compiler messages supposed to appear in this case are:
      error: 'LIBBURN_MISCONFIGURATION' undeclared (first use in this function)
      error: 'INTENTIONAL_ABORT_OF_COMPILATION__HEADERFILE_libburn_dot_h_TOO_OLD__SEE_libisoburn_dot_h_and_burn_wrap_dot_h' undeclared (first use in this function)
      error: 'LIBBURN_MISCONFIGURATION_' undeclared (first use in this function)
*/

/* The indendation is an advise of man gcc to help old compilers ignoring */
 #if isoburn_libburn_req_major > burn_header_version_major
 #define Isoburn_libburn_dot_h_too_olD 1
 #endif
 #if isoburn_libburn_req_major == burn_header_version_major && isoburn_libburn_req_minor > burn_header_version_minor
 #define Isoburn_libburn_dot_h_too_olD 1
 #endif
 #if isoburn_libburn_req_minor == burn_header_version_minor && isoburn_libburn_req_micro > burn_header_version_micro
 #define Isoburn_libburn_dot_h_too_olD 1
 #endif

#ifdef Isoburn_libburn_dot_h_too_olD
LIBBURN_MISCONFIGURATION = 0;
INTENTIONAL_ABORT_OF_COMPILATION__HEADERFILE_libburn_dot_h_TOO_OLD__SEE_libisoburn_dot_h_and_burn_wrap_dot_h = 0;
LIBBURN_MISCONFIGURATION_ = 0;
#endif


#ifdef Xorriso_with_libjtE

/* The minimum requirement of libisoburn towards the libjte header
   at compile time is the same as the one of a usable libisofs towards libjte.
   So the requirement is defined in libisofs/libisofs.h :
     iso_libjte_req_major , iso_libjte_req_minor , iso_libjte_req_micro
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
INTENTIONAL_ABORT_OF_COMPILATION__HEADERFILE_libjte_dot_h_TOO_OLD__SEE_libisofs_dot_h_AND_burn_wrap.c_c = 0;
LIBJTE_MISCONFIGURATION_ = 0;
#endif

#endif /* Xorriso_with_libjtE */


/* End of ugly compile time tests (scroll up for explanation) */


 msg[0]= 0;

#ifdef Xorriso_with_libjtE

 libjte__version(&major, &minor, &micro);
 sprintf(msg + strlen(msg), "libjte-%d.%d.%d ", major, minor, micro);
 if (libjte__is_compatible(LIBJTE_VERSION_MAJOR, LIBJTE_VERSION_MINOR,
                                LIBJTE_VERSION_MICRO, 0)) {
   sprintf(msg+strlen(msg), "ok, ");
 } else {
   sprintf(msg + strlen(msg),
           "- TOO OLD -, need at least libjte-%d.%d.%d ,\n",
           LIBJTE_VERSION_MAJOR, LIBJTE_VERSION_MINOR,
           LIBJTE_VERSION_MICRO);
   bad_match= 1;
   no_iso_init= 1; /* iso_init() will fail anyway */
 }

#endif /* Xorriso_with_libjtE */

 if(!no_iso_init) {
   if(iso_init()<0) {
     sprintf(msg+strlen(msg), "Cannot initialize libisofs\n");
     return(0);
   }
 }
 iso_lib_version(&major, &minor, &micro);
 sprintf(msg+strlen(msg), "libisofs-%d.%d.%d ", major, minor, micro);
#ifdef iso_lib_header_version_major
 if(iso_lib_is_compatible(iso_lib_header_version_major,
                          iso_lib_header_version_minor,
                          iso_lib_header_version_micro)) {
   sprintf(msg+strlen(msg), "ok, ");
 } else {
   sprintf(msg+strlen(msg),"- TOO OLD -, need at least libisofs-%d.%d.%d ,\n",
           iso_lib_header_version_major, iso_lib_header_version_minor,
           iso_lib_header_version_micro);
   bad_match= 1;
 }
#else
 if(iso_lib_is_compatible(isoburn_libisofs_req_major,
                          isoburn_libisofs_req_minor,
                          isoburn_libisofs_req_micro)) {
   sprintf(msg+strlen(msg), "suspicious, ");
 } else {
   sprintf(msg+strlen(msg),"- TOO OLD -, need at least libisofs-%d.%d.%d ,\n",
           isoburn_libisofs_req_major, isoburn_libisofs_req_minor,
           isoburn_libisofs_req_micro);
   bad_match= 1;
 }
#endif /* ! iso_lib_header_version_major */

 if(!burn_initialize()) {
   sprintf(msg+strlen(msg), "Cannot initialize libburn\n");
   return(0);
 }

 burn_version(&major, &minor, &micro);
 sprintf(msg+strlen(msg), "libburn-%d.%d.%d ", major, minor, micro);
 if(major > burn_header_version_major
    || (major == burn_header_version_major
        && (minor > burn_header_version_minor
            || (minor == burn_header_version_minor
                && micro >= burn_header_version_micro)))) {
   sprintf(msg+strlen(msg), "ok, ");
 } else {
   sprintf(msg+strlen(msg), "- TOO OLD -, need at least libburn-%d.%d.%d ,\n",
           burn_header_version_major, burn_header_version_minor,
           burn_header_version_micro);
   bad_match= 1;
 }

 isoburn_version(&major, &minor, &micro);
 sprintf(msg+strlen(msg), "for libisoburn-%d.%d.%d", major, minor, micro);
 if(bad_match)
   return(0);

 isoburn_destroy_all(&isoburn_list_start, 0); /* isoburn_list_start= NULL */
 return(1);
}


/* API @since 0.1.0 */
int isoburn_libisofs_req(int *major, int *minor, int *micro)
{
 *major= iso_lib_header_version_major;
 *minor= iso_lib_header_version_minor;
 *micro= iso_lib_header_version_micro;
 return(1);
}


/* API @since 0.1.0 */
int isoburn_libburn_req(int *major, int *minor, int *micro)
{
 *major= burn_header_version_major;
 *minor= burn_header_version_minor;
 *micro= burn_header_version_micro;
 return(1);
}


/* API @since 0.6.4 */
int isoburn_libjte_req(int *major, int *minor, int *micro)
{
#ifdef Xorriso_with_libjtE
 *major= LIBJTE_VERSION_MAJOR;
 *minor= LIBJTE_VERSION_MINOR;
 *micro= LIBJTE_VERSION_MICRO;
#else
 *major= *minor= *micro= 0;
#endif /* ! Xorriso_with_libjtE */
 return(1);
}


int isoburn_set_msgs_submit(int (*msgs_submit)(void *handle, int error_code,
                                               char msg_text[], int os_errno,
                                               char severity[], int flag),
                               void *submit_handle, int submit_flag, int flag)
{
 libisoburn_default_msgs_submit= msgs_submit;
 libisoburn_default_msgs_submit_handle= submit_handle;
 libisoburn_default_msgs_submit_flag= submit_flag;
 return(1);
}


int isoburn_is_intermediate_dvd_rw(struct burn_drive *d, int flag)
{
 int profile, ret= 0, format_status, num_formats;
 char profile_name[80];
 enum burn_disc_status s;
 off_t format_size= -1;
 unsigned bl_sas;

 s= isoburn_disc_get_status(d);
 ret= burn_disc_get_profile(d, &profile, profile_name);
 if(ret>0 && profile==0x13)
   ret= burn_disc_get_formats(d, &format_status, &format_size,
                              &bl_sas, &num_formats);
 if(ret>0 && profile==0x13 && s==BURN_DISC_BLANK &&
    format_status==BURN_FORMAT_IS_UNKNOWN)
   return(1);
 return(0);
}


/** Examines the medium and sets appropriate emulation if needed.
    @param flag bit0= pretent blank on overwriteable media
                bit3= if the drive reports a -ROM profile then try to read 
                      table of content by scanning for ISO image headers.
                bit4= do not emulate TOC on overwriteable media
                bit5= ignore ACL from external filesystems
                bit6= ignore POSIX Extended Attributes from external filesystems
                bit7= pretend -ROM and scan for table of content
                bit9= when scanning for ISO 9660 sessions on overwritable
                      media: Do not demand a valid superblock at LBA 0
                      and scan until end of medium.
*/ 
static int isoburn_welcome_media(struct isoburn **o, struct burn_drive *d,
                                 int flag)
{
 int ret, profile, readonly= 0, role, random_access;
 int emulation_started= 0;
 struct burn_multi_caps *caps= NULL;
 struct isoburn_toc_entry *t;
 char profile_name[80];
 struct isoburn_toc_disc *disc= NULL;
 struct isoburn_toc_session **sessions;
 struct isoburn_toc_track **tracks;
 int num_sessions= 0, num_tracks= 0, track_count= 0, session_no= 0;
 char msg[80];
 enum burn_disc_status s;

#ifndef Hardcoded_cd_rW
 int lba, nwa;
#endif

 s= burn_disc_get_status(d);
 profile_name[0]= 0;
 ret= burn_disc_get_profile(d, &profile, profile_name);
 if(ret<=0)
   profile= 0x00;
 ret= burn_disc_get_multi_caps(d, BURN_WRITE_NONE, &caps, 0);
 if(ret<0) /*== 0 is read-only medium, but it is too early to reject it here */
   goto ex;
 if(ret==0 || (flag & 128))
   readonly= 1;
 if(flag & 128)
   flag = (flag & ~ 16) | 8;
   
 ret= isoburn_new(o, 0);
 if(ret<=0)
   goto ex;
 (*o)->drive= d;
 (*o)->msgs_submit= libisoburn_default_msgs_submit;
 (*o)->msgs_submit_handle= libisoburn_default_msgs_submit_handle;
 (*o)->msgs_submit_flag= libisoburn_default_msgs_submit_flag;
 iso_image_set_ignore_aclea((*o)->image, (flag >> 5 ) & 3);

#ifdef Hardcoded_cd_rW
 /* <<< A70929 : hardcoded CD-RW with fabricated -msinfo */
 caps->start_adr= 0;
 (*o)->fabricated_disc_status= BURN_DISC_APPENDABLE;
#endif

 role= burn_drive_get_drive_role(d);
 random_access= caps->start_adr || role == 4;
 if(random_access)
   (*o)->emulation_mode= 1;
 if(random_access && !readonly) {       /* set emulation to overwriteable */
   ret= isoburn_is_intermediate_dvd_rw(d, 0);
   if(ret>0) {
     (*o)->min_start_byte= 0;
     (*o)->nwa= 0;
     (*o)->zero_nwa= 0;
   }
   if((flag & 1) && role != 4 && role != 5) {
     (*o)->nwa= (*o)->zero_nwa;
     (*o)->fabricated_disc_status= BURN_DISC_BLANK;
   } else {
     ret= isoburn_start_emulation(*o, 0);
     if(ret<=0) {
       (*o)->emulation_mode= -1;
       goto ex;
     }
     emulation_started= 1;
     /* try to read emulated toc */
     ret= isoburn_emulate_toc(d, (flag & 16) | ((!!(flag & 512)) << 1));
     if(ret<0) {
       (*o)->emulation_mode= -1;
       goto ex;
     }
   }
 } else {

    /* >>> recognize unsuitable media (but allow read-only media) */;

   if(readonly && s != BURN_DISC_EMPTY) {

     /* >>> ts B10712: This maps BURN_DISC_UNSUITABLE to BURN_DISC_FULL
                       which can hardly be correct in general.
        ??? What reason does this have ?
     */
     (*o)->fabricated_disc_status= BURN_DISC_FULL;

     /* This might be an overwriteable medium in a -ROM drive.
        Pitfall:
        Multi-session media which bear a xorriso image for overwriteables
        in their first session would get a TOC of that first image rather
        than of the medium.
        It is not possible to distinguish a BD-RE from a single session
        BD-R with an image for overwriteables. But as soon as the medium
        bears 2 logical tracks it cannot be overwriteable.
        So count the number of tracks first.
     */
     disc= isoburn_toc_drive_get_disc(d);
     if(disc != NULL) {
       sessions= isoburn_toc_disc_get_sessions(disc, &num_sessions);
       for(session_no= 0; session_no < num_sessions; session_no++) {
         tracks= isoburn_toc_session_get_tracks(sessions[session_no],
                                                &num_tracks);
         if(tracks != NULL)
           track_count+= num_tracks;
       }
       isoburn_toc_disc_free(disc);
     }

     sprintf(msg, "ROM medium has libburn track count = %d", track_count);
     isoburn_msgs_submit(*o, 0x00060000, msg, 0, "DEBUG", 0);

     if((flag & 16) || track_count >= 2) {
       ret= 0; /* toc emulation off, or not overwriteable */
     } else {
       ret= isoburn_start_emulation(*o, 1);
       if(ret<=0) {
         (*o)->emulation_mode= -1;
         goto ex;
       }
       emulation_started= 1;
       ret= isoburn_emulate_toc(d, 1 | ((!!(flag & 512)) << 1));
       if(ret<0)
         goto ex;
       else if(ret > 0)
         (*o)->emulation_mode= 1;
     }
     if(ret == 0 && (profile != 0x08 || (flag & 128)) && (flag & 8)) {
       /* This might also be multi-session media which do not
          get shown with a decent TOC.
          CD-R TOC (profile 0x08) can be trusted. Others not.
          Do a scan search of ISO headers.
       */
       if(!emulation_started) {
         ret= isoburn_start_emulation(*o, 1);
         if(ret<=0) {
           (*o)->emulation_mode= -1;
           goto ex;
         }
       }
       ret= isoburn_emulate_toc(d, 1 | 2);
       if(ret<0)
         goto ex;
       if(ret>0) { /* point msc1 to last session */
         if((*o)->toc!=NULL) {
           for(t= (*o)->toc; t->next!=NULL; t= t->next);
            (*o)->fabricated_msc1= t->start_lba;
         }
       }
     }
   }
#ifdef Hardcoded_cd_rW
   (*o)->nwa= Hardcoded_cd_rw_nwA;
#else
   ret= burn_disc_track_lba_nwa(d, NULL, 0, &lba, &nwa);
   if(ret>0)
     (*o)->nwa= nwa;
   if((*o)->nwa < (*o)->zero_nwa)
     (*o)->zero_nwa= 0;
#endif

 }

 ret= 1;
ex:
 if(caps!=NULL)
   burn_disc_free_multi_caps(&caps);
 return(ret);
}


/**
    @param flag bit0= load
                bit1= regard overwriteable media as blank
                bit2= if the drive is a regular disk file: truncate it to
                      the write start address
                bit3= if the drive reports a -ROM profile then try to read 
                      table of content by scanning for ISO image headers.
                      (depending on media type and drive state this might 
                       help or it might make the resulting toc even worse)
                bit4= do not emulate TOC on overwriteable media
                bit5= ignore ACL from external filesystems
                bit6= ignore POSIX Extended Attributes from external filesystems
                bit7= pretend -ROM profile and scan for table of content
                bit8= re-assess (*drive_infos)[0] rather than aquiring adr
                bit9= when scanning for ISO 9660 sessions on overwritable
                      media: Do not demand a valid superblock at LBA 0
                      and scan until end of medium.
*/
int isoburn_drive_aquire(struct burn_drive_info *drive_infos[],
                         char *adr, int flag)
{
 int ret, drive_grabbed= 0;
 struct isoburn *o= NULL;
 int conv_ret;
 char *libburn_drive_adr= NULL;

 /* Should be obsolete by new drive addressing of libburn-0.5.2 */
 /* but helps with kernel 2.4 to use /dev/sr */
 libburn_drive_adr= calloc(1, BURN_DRIVE_ADR_LEN);
 if(libburn_drive_adr == NULL)
   {ret= -1; goto ex;}
 conv_ret= burn_drive_convert_fs_adr(adr, libburn_drive_adr);
 if(conv_ret<=0)
   strcpy(libburn_drive_adr, adr);

 if(flag & 256)
   ret= burn_drive_re_assess((*drive_infos)[0].drive, 0);
 else
   ret= burn_drive_scan_and_grab(drive_infos, libburn_drive_adr, flag&1);
 if(ret<=0)
   goto ex;
 drive_grabbed= 1;
 ret= isoburn_welcome_media(&o, (*drive_infos)[0].drive,
                         (flag & (8 | 16 | 32 | 64 | 128 | 512)) | !!(flag&2));
 if(ret<=0)
   goto ex;

 if(flag&4) {
   ret= isoburn_find_emulator(&o, (*drive_infos)[0].drive, 0);
   if(ret>0 && o!=NULL)
     o->truncate= 1;
 }

 ret= 1;
ex:
 if(ret<=0) {
   if(drive_grabbed)
     burn_drive_release((*drive_infos)[0].drive, 0);
   isoburn_destroy(&o, 0);
 }
 if(libburn_drive_adr != NULL)
   free(libburn_drive_adr);
 return(ret);
}


int isoburn_drive_scan_and_grab(struct burn_drive_info *drive_infos[],
                                char *adr, int load)
{
 int ret;

 ret= isoburn_drive_aquire(drive_infos, adr, !!load);
 return(ret);
}


int isoburn_drive_grab(struct burn_drive *drive, int load)
{
 int ret;
 struct isoburn *o= NULL;

 ret= burn_drive_grab(drive, load);
 if(ret<=0)
   goto ex;
 ret= isoburn_welcome_media(&o, drive, 0);
 if(ret<=0)
   goto ex;
 
 ret= 1;
ex:
 if(ret<=0)
   isoburn_destroy(&o,0);
 return(ret);
}


/** Retrieve medium emulation and eventual isoburn emulator of drive.
    @return -1 unsuitable medium, 0 generic medium, 1 emulated medium.
*/
int isoburn_find_emulator(struct isoburn **pt,
                                 struct burn_drive *drive, int flag)
{
 int ret;

 ret= isoburn_find_by_drive(pt, drive, 0);
 if(ret<=0)
   return(0);
 if((*pt)->emulation_mode==-1) {
   isoburn_msgs_submit(*pt, 0x00060000,
                    "Unsuitable drive and medium state", 0, "FAILURE", 0);
   return(-1);
 }
 if((*pt)->emulation_mode==0)
   return(0);
 return(1);
} 


enum burn_disc_status isoburn_disc_get_status(struct burn_drive *drive)
{
 int ret;
 struct isoburn *o;

 ret= isoburn_find_emulator(&o, drive, 0);
 if(ret<0)
   return(BURN_DISC_UNSUITABLE);
 if(o!=NULL)
   if(o->fabricated_disc_status!=BURN_DISC_UNREADY)
     return(o->fabricated_disc_status);
 if(ret==0)
   return(burn_disc_get_status(drive));

 /* emulated status */
 if(o->emulation_mode==-1)
   return(BURN_DISC_UNSUITABLE);
 if(o->nwa>o->zero_nwa)
   return(BURN_DISC_APPENDABLE);
 return(BURN_DISC_BLANK);
}


int isoburn_disc_erasable(struct burn_drive *d)
{
 int ret;
 struct isoburn *o;

 ret= isoburn_find_emulator(&o, d, 0);
 if(ret>0)
   if(o->emulation_mode==1)
     return(1);
 return burn_disc_erasable(d);
}


void isoburn_disc_erase(struct burn_drive *drive, int fast)
{
 int ret, do_pseudo_blank= 0, role;
 struct isoburn *o;
 enum burn_disc_status s;
 char *zero_buffer= NULL;
 struct burn_multi_caps *caps= NULL;

 zero_buffer= calloc(1, Libisoburn_target_head_sizE);
 if(zero_buffer == NULL) {
   /* To cause a negative reply with burn_drive_wrote_well() */
   burn_drive_cancel(drive);
   goto ex;
 }

 ret= isoburn_find_emulator(&o, drive, 0);
 if(ret>0) {
   if(o->emulation_mode==-1) {
     /* To cause a negative reply with burn_drive_wrote_well() */
     burn_drive_cancel(drive);
     goto ex;
   }
   role = burn_drive_get_drive_role(drive);
   if (role == 5) {
     /* libburn will truncate the random-access write-only file
        to zero size and change its state */
     burn_disc_erase(drive, fast);
     o->fabricated_disc_status= burn_disc_get_status(drive);
     o->nwa= o->zero_nwa= 0;
     goto ex;
   }
   if(o->emulation_mode > 0) { /* might be readonly with emulated sessions */
     ret= burn_disc_get_multi_caps(drive, BURN_WRITE_NONE, &caps, 0);
     if(ret > 0 && caps->start_adr)
       do_pseudo_blank= 1;
   }
   if(do_pseudo_blank) {
     s= isoburn_disc_get_status(drive);
     if(s==BURN_DISC_FULL) { /* unknown data format in first 64 kB */
       memset(zero_buffer, 0, Libisoburn_target_head_sizE);
       ret= burn_random_access_write(drive, (off_t) 0, zero_buffer,
                                     (off_t) Libisoburn_target_head_sizE, 1);
     } else {
       ret= isoburn_invalidate_iso(o, 0);
     }
     if(ret<=0)
       burn_drive_cancel(drive); /* mark run as failure */
     goto ex;
   }
 }
 burn_disc_erase(drive, fast);
ex:;
 if(caps!=NULL)
   burn_disc_free_multi_caps(&caps);
 if(zero_buffer != NULL)
   free(zero_buffer);
}


off_t isoburn_disc_available_space(struct burn_drive *d,
                                   struct burn_write_opts *opts)
{
 int ret;
 struct isoburn *o;
 struct burn_write_opts *eff_opts= NULL, *local_opts= NULL;
 enum burn_disc_status s;
 off_t avail;

 eff_opts= opts;
 ret= isoburn_find_emulator(&o, d, 0);
 if(ret>0 && o!=NULL)
   if(o->emulation_mode!=0) {
     s= isoburn_disc_get_status(d);
     if(s==BURN_DISC_FULL) /* unknown data format in first 64 kB */
       return((off_t) 0);
     local_opts= burn_write_opts_new(d);
     eff_opts= local_opts;
     burn_write_opts_set_start_byte(eff_opts, ((off_t) o->nwa) * (off_t) 2048);
   }
 avail= burn_disc_available_space(d, eff_opts);
 if(local_opts!=NULL)
   burn_write_opts_free(local_opts);
 local_opts= NULL;
 return(avail);
}


int isoburn_disc_get_msc1(struct burn_drive *d, int *start_lba)
{
 int ret;
 struct isoburn *o;

#ifdef Hardcoded_cd_rW
 /* <<< A70929 : hardcoded CD-RW with fabricated -msinfo */
 *start_lba= Hardcoded_cd_rw_c1;
 return(1);
#endif

 if(isoburn_disc_get_status(d)!=BURN_DISC_APPENDABLE &&
    isoburn_disc_get_status(d)!=BURN_DISC_FULL) {
   isoburn_msgs_submit(NULL, 0x00060000,
                       "Medium contains no recognizable data", 0, "SORRY", 0);
   return(0);
 }
 ret= isoburn_find_emulator(&o, d, 0);
 if(ret<0)
   return(0);
 if(o->fabricated_msc1>=0) {
   *start_lba= o->fabricated_msc1;
   return(1);
 }
 if(ret>0) if(o->emulation_mode>0) {
   *start_lba= 0;
   return(1);
 }
 return(burn_disc_get_msc1(d, start_lba));
}


int isoburn_disc_track_lba_nwa(struct burn_drive *d,
                               struct burn_write_opts *opts,
                               int trackno, int *lba, int *nwa)
{
 int ret;
 struct isoburn *o;
 enum burn_disc_status s;

#ifdef Hardcoded_cd_rW
 /* <<< A70929 : hardcoded CD-RW with fabricated -msinfo */
 *lba= Hardcoded_cd_rw_c1;
 *nwa= Hardcoded_cd_rw_nwA;
 return(1);
#endif

 *nwa= *lba= 0;
 ret= isoburn_find_emulator(&o, d, 0);
 if(ret<0)
   return(0);
 if(ret>0) if(o->emulation_mode>0) {
   *lba= 0;
   *nwa= o->nwa;
   return(1);
 }
 if(burn_drive_get_drive_role(d) != 1)
   return(1);

 s= isoburn_disc_get_status(d);
 if(s == BURN_DISC_BLANK) /* We do not believe in anything but nwa = lba = 0 */
   return(1);
 return(burn_disc_track_lba_nwa(d, opts, trackno, lba, nwa));
}


int isoburn_get_msc2(struct isoburn *o,
                     struct burn_write_opts *opts, int *msc2, int flag)
{
 int ret, lba, nwa;

 if(o->fabricated_msc2>=0)
   *msc2= o->fabricated_msc2;
 else {
   ret= isoburn_disc_track_lba_nwa(o->drive, opts, 0, &lba, &nwa);
   if(ret<=0)
     return(ret);
   *msc2= nwa;
 }
 return(1);
}

/* @param flag bit0= truncate (else do not truncate)
               bit1= do not warn if call is inappropriate to drive 
               bit2= only set if truncation is currently enabled
*/
int isoburn_set_truncate(struct burn_drive *drive, int flag)
{
 int ret;
 struct isoburn *o;

 ret= isoburn_find_emulator(&o, drive, 0);
 if(ret < 0)
   return ret;
 if(o == NULL) {
   if(!(flag & (2 | 4)))
     isoburn_msgs_submit(o, 0x00060000,
        "Drive type or role is inappropriate for truncation", 0, "WARNING", 0);
   return(0);
 }
 if(o->truncate || !(flag & 4))
   o->truncate= flag & 1;
 return(1);
}


void isoburn_disc_write(struct burn_write_opts *opts, struct burn_disc *disc)
{
 int ret;
 off_t nwa= 0;
 struct isoburn *o;
 struct burn_drive *drive;
 char *reasons= NULL, *msg= NULL, *adr= NULL;
 struct stat stbuf;
 enum burn_write_types write_type;

 drive= burn_write_opts_get_drive(opts);

 reasons= calloc(1, BURN_REASONS_LEN);
 msg= calloc(1, 160+BURN_REASONS_LEN);
 adr= calloc(1, BURN_DRIVE_ADR_LEN);
 if(reasons == NULL || msg == NULL || adr == NULL) {
   /* To cause a negative reply with burn_drive_wrote_well() */
   burn_drive_cancel(drive);
   goto ex;
 }

 ret= isoburn_find_emulator(&o, drive, 0);
 if(ret<0)
   goto ex;
 if(o!=NULL) {
   o->wrote_well= -1;
   if(o->emulation_mode!=0) {
     burn_write_opts_set_multi(opts, 0);
     if(o->emulation_mode>0 && o->nwa >= 0) {
       nwa= o->nwa;

       /* This caters for unwritten formatted DVD-RW. They need to be written
          sequentially on the first use. Only written areas are random access.
          If the first session is not written to LBA 0, then re-opening of
          formatting and padding is needed. 
          This can be done. But when the track gets closed after padding,
          this lasts a long time. There is a high risk that an app will not
          poll the message queue while waiting for  isoburn_disc_write()  to
          return. The pacifier loop usually happens only afterwards.
          So automatic formatting might cause a nervous clueless user.
       */
       ret= isoburn_is_intermediate_dvd_rw(drive, 0);
       if(ret>0 && nwa>0 && nwa <= o->zero_nwa) {
         /* actually this should not happen since such media get recognized
            by isoburn_welcome_media and o->zero_nwa gets set to 0
         */
         sprintf(msg,
        "DVD-RW insufficiently formatted. (Intermediate State, size unknown)");
         isoburn_msgs_submit(o, 0x00060000, msg, 0, "FAILURE", 0);
         sprintf(msg,
                "It might help to first deformat it and then format it again");
         isoburn_msgs_submit(o, 0x00060000, msg, 0, "HINT", 0);
         burn_drive_cancel(drive); /* mark run as failure */
         goto ex;
       }
       /* end of DVD-RW oriented check */
       burn_write_opts_set_start_byte(opts, nwa * (off_t) 2048);
     }
   }
 }

 if(o->do_tao) {
   if (o->do_tao > 0)
     burn_write_opts_set_write_type(opts, BURN_WRITE_TAO, BURN_BLOCK_MODE1);
   else
     burn_write_opts_set_write_type(opts, BURN_WRITE_SAO, BURN_BLOCK_SAO);

   ret = burn_precheck_write(opts, disc, reasons, 0);
   if(ret <= 0) {
     sprintf(msg, "Cannot set write type %s for this medium.",
             o->do_tao > 0 ? "TAO" : "SAO");
     sprintf(msg + strlen(msg), "Reasons given:\n   %s", reasons);
     goto no_write_type;
   }
   sprintf(msg, "Explicitly chosen write type: %s",
           o->do_tao > 0 ? "TAO" : "SAO");
   isoburn_msgs_submit(o, 0x00060000, msg, 0, "NOTE", 0);
 } else {
   write_type= burn_write_opts_auto_write_type(opts, disc, reasons, 0);
   if (write_type == BURN_WRITE_NONE) {
      sprintf(msg, "Failed to find a suitable write type:\n%s", reasons);
no_write_type:;
      isoburn_msgs_submit(o, 0x00060000, msg, 0, "FAILURE", 0);
      if(o!=NULL)
        o->wrote_well= 0;
      /* To cause a negative reply with burn_drive_wrote_well() */
      burn_drive_cancel(drive);
      goto ex;
   }

   sprintf(reasons, "%d", (int) write_type);
   sprintf(msg, "Write_type = %s\n",
                (write_type == BURN_WRITE_SAO ? "SAO" :
                (write_type == BURN_WRITE_TAO ? "TAO" : reasons)));
   isoburn_msgs_submit(o, 0x00060000, msg, 0, "DEBUG", 0);
 }

#ifdef Hardcoded_cd_rW
 /* <<< A70929 : hardcoded CD-RW with fabricated -msinfo */
 fprintf(stderr, "Setting write address to LBA %d\n", Hardcoded_cd_rw_nwA);
 burn_write_opts_set_start_byte(opts,
				 ((off_t) Hardcoded_cd_rw_nwA) * (off_t) 2048);
#endif

 if(o->truncate) {
   ret= burn_drive_get_drive_role(drive);
   if(ret == 2 || ret == 5) {
     ret= burn_drive_d_get_adr(drive, adr);
     if(ret>0) {
       ret= lstat(adr, &stbuf);
       if(ret!=-1)
         if(S_ISREG(stbuf.st_mode))
           ret= truncate(adr, nwa * (off_t) 2048);
           /* (result of truncate intentionally ignored) */
     }
   }
 }

 burn_disc_write(opts, disc);
ex:;
 if(reasons != NULL)
   free(reasons);
 if(msg != NULL)
   free(msg);
 if(adr != NULL)
   free(adr);
}


void isoburn_drive_release(struct burn_drive *drive, int eject)
{
 int ret;
 struct isoburn *o;

 ret= isoburn_find_emulator(&o, drive, 0);
 if(ret<0)
   return;
 if(o!=NULL) {
   isoburn_destroy(&o, 0);
 }
 burn_drive_release(drive, eject);
}


void isoburn_finish(void)
{
 isoburn_destroy_all(&isoburn_list_start, 0);
 burn_finish();
 iso_finish();
}


int isoburn_needs_emulation(struct burn_drive *drive)
{
 int ret;
 struct isoburn *o;
 enum burn_disc_status s;

 s= isoburn_disc_get_status(drive);
 if(s!=BURN_DISC_BLANK && s!=BURN_DISC_APPENDABLE)
   return(-1);
 ret= isoburn_find_emulator(&o, drive, 0);
 if(ret<0)
   return(-1);
 if(ret>0)
   if(o->emulation_mode>0)
     return(1);
 return(0);  
}


int isoburn_set_start_byte(struct isoburn *o, off_t value, int flag)
{
 int ret;
 struct burn_drive *drive = o->drive;
 struct burn_multi_caps *caps= NULL;
 
 ret= burn_disc_get_multi_caps(drive, BURN_WRITE_NONE, &caps, 0);
 if(ret<=0)
   goto ex;
 if(!caps->start_adr) {
   isoburn_msgs_submit(o, 0x00060000,
                       "Cannot set start byte address with this type of media",
                       0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 o->min_start_byte= value;
 if(value % caps->start_alignment)
   value+= caps->start_alignment - (value % caps->start_alignment);
 o->nwa= value/2048;
 if(o->nwa < o->zero_nwa)
   o->zero_nwa= 0;
 /* If suitable for media alignment, round up to Libisoburn_nwa_alignemenT */
 if((o->nwa % Libisoburn_nwa_alignemenT) &&
     ((Libisoburn_nwa_alignemenT*2048) % caps->start_alignment)==0 )
   o->nwa+= Libisoburn_nwa_alignemenT - (o->nwa % Libisoburn_nwa_alignemenT);
 ret= 1;
ex:
 if(caps!=NULL)
   burn_disc_free_multi_caps(&caps);
 return(ret);
}


int isoburn_get_min_start_byte(struct burn_drive *d, off_t *start_byte,
                               int flag)
{
 int ret;
 struct isoburn *o;

 ret= isoburn_find_emulator(&o, d, 0);
 if(ret<0)
   return(-1);
 if(ret==0) 
   return(0);
 *start_byte= o->min_start_byte;
 if(o->min_start_byte<=0)
   return(0);
 return(1);
}


int isoburn_drive_wrote_well(struct burn_drive *d)
{
 int ret;
 struct isoburn *o;
 
 ret= isoburn_find_emulator(&o, d, 0);
 if(ret<0)
   return(-1);
 if(o!=NULL)
   if(o->wrote_well>=0)
     return(o->wrote_well);
 ret= burn_drive_wrote_well(d);
 return ret;
}


int isoburn_get_fifo_status(struct burn_drive *d, int *size, int *free_bytes,
			    char **status_text)
{
 int ret;
 struct isoburn *o;
 size_t hsize= 0, hfree_bytes= 0;

 ret= isoburn_find_emulator(&o, d, 0);
 if(ret<0)
   return(-1);

 if(o==NULL)
   return(-1);
 if(o->iso_source==NULL)
   return(-1);
 ret= iso_ring_buffer_get_status(o->iso_source, &hsize, &hfree_bytes);
 if(hsize > 1024*1024*1024)
   *size= 1024*1024*1024;
 else
   *size= hsize;
 if(hfree_bytes > 1024*1024*1024)
   *free_bytes= 1024*1024*1024;
 else
   *free_bytes= hfree_bytes;
 *status_text= "";
 if(ret==0)
   *status_text= "standby";
 else if(ret==1)
   *status_text= "active";
 else if(ret==2)
   *status_text= "ending";
 else if(ret==3)
   *status_text= "failing";
 else if(ret==4)
   *status_text= "unused";
 else if(ret==5)
   *status_text= "abandoned";
 else if(ret==6)
   *status_text= "ended";
 else if(ret==7)
   *status_text= "aborted";
 return(ret);
}


/* @param flag bit0= -reserved-
               bit1= this is a libburn severity
*/
int isoburn__sev_to_text(int severity, char **severity_name,
                         int flag)
{
 int ret;
   
 ret= iso_sev_to_text(severity, severity_name);
 if(ret>0)
   return(ret);
 ret= burn_sev_to_text(severity, severity_name, 0);
 return(ret);
}


int isoburn__text_to_sev(char *severity_name, int *severity_number, int flag)
{
 int ret= 1;

 ret= iso_text_to_sev(severity_name, severity_number);
 if(ret>0)
   return(ret);
 ret= burn_text_to_sev(severity_name, severity_number, 0);
 return(ret);
}


int isoburn_report_iso_error(int iso_error_code, char msg_text[], int os_errno,
                             char min_severity[], int flag)
{
 int error_code, iso_sev, min_sev, ret;
 char *sev_text_pt, *msg_text_pt= NULL;

 error_code= iso_error_get_code(iso_error_code);
 if(error_code < 0x00030000 || error_code >= 0x00040000)
   error_code= (error_code & 0xffff) | 0x00050000;

 if(iso_error_code<0)
   msg_text_pt= (char *) iso_error_to_msg(iso_error_code);
 if(msg_text_pt==NULL)
   msg_text_pt= msg_text;
 iso_sev= iso_error_get_severity(iso_error_code);
 sev_text_pt= min_severity;
 isoburn__text_to_sev(min_severity, &min_sev, 0);
 if(min_sev < iso_sev) 
   isoburn__sev_to_text(iso_sev, &sev_text_pt, 0);
 ret= iso_msgs_submit(error_code, msg_text_pt, os_errno, sev_text_pt, 0);
 return(ret);
}


/* @param flag bit0-7: info return mode
                 0= do not return anything in info (do not even touch it)
                 1= copy volume id to info (info needs 33 bytes)
                 2= do not touch info (caller will copy 64 kB header to it)
               bit14= -reserved -
               bit15= -reserved-
   @return 1 seems to be a valid ISO image , 0 format not recognized, <0 error
*/
int isoburn_read_iso_head_parse(unsigned char *data,
                                int *image_blocks, char *info, int flag)
{
 int i, info_mode;

 /* is this an ISO image ? */
 if(data[0]!=1)
   return(0);
 if(strncmp((char *) (data+1),"CD001",5)!=0)
   return(0);
 /* believe so */

 *image_blocks= data[80] | (data[81]<<8) | (data[82]<<16) | (data[83]<<24);
 info_mode= flag&255;
 if(info_mode==0) {
   ;
 } else if(info_mode==1) {
   strncpy(info, (char *) (data+40), 32);
   info[32]= 0;
   for(i= strlen(info)-1; i>=0; i--)
     if(info[i]!=' ')
   break;
     else
       info[i]= 0;
 } else if(info_mode==2) {
   ;
 } else {
   isoburn_msgs_submit(NULL, 0x00060000,
               "Program error: Unknown info mode with isoburn_read_iso_head()",
               0, "FATAL", 0);
   return(-1);
 }
 return(1);
}
                         

/* API
   @param flag bit0-7: info return mode
                 0= do not return anything in info (do not even touch it)
                 1= copy volume id to info (info needs 33 bytes)
                 2= copy 64 kB header to info (needs 65536 bytes)
               bit13= do not read head from media but use first 64 kB from info
               bit14= check both half buffers (not only second)
                      return 2 if found in first block
               bit15= return-1 on read error
   @return 1 seems to be a valid ISO image , 2 found in first half buffer,
           0 format not recognized, <0 error
*/
int isoburn_read_iso_head(struct burn_drive *d, int lba,
                          int *image_blocks, char *info, int flag)
{
 unsigned char *buffer= NULL;
 int ret, info_mode, capacity, role;
 off_t data_count, to_read;
 struct isoburn *o;

 buffer= calloc(1, 64 * 1024);
 if(buffer == NULL)
   {ret= -1; goto ex;}

 info_mode= flag&255;
 *image_blocks= 0;
 if(flag&(1<<13)) {
   memcpy(buffer, info, 64*1024);
 } else {
   memset(buffer, 0, 64 * 1024);
   role = burn_drive_get_drive_role(d);
   if (role == 3 || role == 5)

     /* >>> ??? return always 0 ? */
     {ret= (-1*!!(flag&(1<<15))); goto ex;}

   ret = burn_get_read_capacity(d, &capacity, 0);
   if (ret <= 0 && (role == 2 || role == 4)) {
     /* Might be a block device on a system where libburn cannot determine its
        size.  Try to read anyway. */
     capacity = 0x7ffffff0;
     ret = 1;
   }
   to_read= (off_t) capacity * ((off_t) 2048);
   if(ret > 0 && to_read >= (off_t) (36 * 1024)) {
     ret= isoburn_find_emulator(&o, d, 0);
     if(ret > 0)
       if(o->media_read_error)
         {ret= (-1 * !!(flag & (1 << 15))); goto ex;}
     if(to_read >= (off_t) (64 * 1024))
       to_read= 64 * 1024;
     ret = burn_read_data(d, ((off_t) lba) * (off_t) 2048, (char *) buffer,
                      to_read, &data_count, 2); /* no error messages */
   } else
     ret= 0;
   if(ret<=0)
     {ret= (-1*!!(flag&(1<<15))); goto ex;}
   if(info_mode==2)
     memcpy(info, buffer, 64*1024);
 }

 if(flag&(1<<14)) {
   ret= isoburn_read_iso_head_parse(buffer, image_blocks, info, info_mode);
   if(ret<0)
     goto ex;
   if(ret>0)
     {ret= 2; goto ex;}
 }
 ret= isoburn_read_iso_head_parse(buffer+32*1024, image_blocks, info,
                                  info_mode);
 if(ret<=0)
   goto ex;
 ret= 1;
ex:;
 if(buffer != NULL)
   free(buffer);
 return(ret);
}


int isoburn_make_toc_entry(struct isoburn *o, int *session_count, int lba,
                           int track_blocks, char *volid, int flag)
{
 int ret;
 struct isoburn_toc_entry *item;

 ret= isoburn_toc_entry_new(&item, o->toc, 0);
 if(ret<=0) {
no_memory:;
   isoburn_msgs_submit(o, 0x00060000,
                       "Not enough memory for emulated TOC entry object",
                       0, "FATAL", 0);
   return(-1);
 }
 if(o->toc==NULL)
   o->toc= item;
 (*session_count)++;
 item->session= *session_count;
 item->track_no= *session_count;
 item->start_lba= lba;
 item->track_blocks= track_blocks;
 if(volid != NULL) {
   item->volid= strdup(volid);
   if(item->volid == NULL)
     goto no_memory;
 }
 return(1);
}


/* @param flag bit0= allow unemulated media
               bit1= free scanning without enclosing LBA-0-header 
               bit4= represent emulated media as one single session
                     (not with bit1)
   @return -1 severe error, 0= no neat header chain, 1= credible chain read
*/
int isoburn_emulate_toc(struct burn_drive *d, int flag)
{
 int ret, image_size= 0, lba, track_blocks, session_count= 0, read_flag= 0;
 int scan_start= 0, scan_count= 0, probe_minus_16= 0, growisofs_nwa, role;
 int with_enclosure= 0, readable_blocks= -1;
 struct isoburn *o;
 char *msg= NULL, *size_text= NULL, *sev, volid[33], *volid_pt= NULL;
 time_t start_time, last_pacifier, now;

 msg= calloc(1, 160);
 size_text= calloc(1, 80);
 if(msg == NULL || size_text == NULL)
   {ret= -1; goto ex;}
 
 /* is the medium emulated multi-session ? */
 ret= isoburn_find_emulator(&o, d, 0);
 if(ret<0)
   {ret= -1; goto ex;}
 if(o==NULL)
   {ret= -1; goto ex;}
 if(o->emulation_mode<=0 && !(flag&1))
   {ret= 0; goto ex;}

 ret= burn_get_read_capacity(d, &readable_blocks, 0);
 if(ret <= 0) {
   role = burn_drive_get_drive_role(d);
   if (role == 2 || role == 4)
     /* Might be a block device on a system where libburn cannot determine its
        size.  Try to read anyway. */
     readable_blocks= 0x7ffffff0; /* try to read anyway */
   else
     readable_blocks= -1;
 }
 if(o->fabricated_disc_status == BURN_DISC_BLANK)
   {ret= 0; goto failure;}

 start_time= last_pacifier= time(NULL);
 lba= 0;
 if(flag & 2) {
   /* If there is a PVD at LBA 32 then this is an image with emulated TOC */
   ret= isoburn_read_iso_head(d, 32, &image_size, NULL, 0);
   if(ret > 0)
     lba= 32;
 } else {
   ret= isoburn_read_iso_head(d, lba, &image_size, NULL, 0);
   if(ret<=0)
     {ret= 0; goto failure;}
   lba= o->target_iso_head_size / 2048;
   with_enclosure= 1;
   if((flag & 16) && o->emulation_mode == 1) {
     ret= 1;
     goto failure; /* This will represent the medium as single session */
   }
 }
 while(lba<image_size || (flag&2)) {
   now= time(NULL);
   if(now - last_pacifier >= 5) {
     last_pacifier= now;
     if(scan_count>=10*512)
       sprintf(size_text, "%.f MB", ((double) scan_count) / 512.0);
     else
       sprintf(size_text, "%.f kB", 2 * (double) scan_count);
     sprintf(msg, "Found %d ISO sessions by scanning %s in %.f seconds",
             session_count, size_text, (double) (now - start_time));
     isoburn_msgs_submit(o, 0x00060000, msg, 0, "UPDATE", 0);
   }
   read_flag= 1;
   if(flag&2)
     read_flag|= (1<<15)|((session_count>0)<<14);
   else {

     /* growisofs aligns to 16 rather than 32. Overwriteable TOC emulation
        relies on not accidentially seeing inter-session trash data.
        But one can safely access 16 blocks earlier because a xorriso header
        would have been overwritten with the unused 16 blocks at its start.
        If libisoburn alignment would increase, then this would not be
        possible any more.
     */

     if(probe_minus_16)
       read_flag|= (1<<14);
     probe_minus_16= 0;
   }

   ret= isoburn_read_iso_head(d, lba, &track_blocks, volid, read_flag);
   if(ret > 0) {
     volid_pt= volid;
   } else {
     volid_pt= NULL;
     if(session_count>0) {
       if(flag&2) {
         if(ret==0) {
           /* try at next 64 k block (check both 32 k halves) */
           lba+= 32;
           scan_count+= 32;
           if(lba-scan_start <= Libisoburn_toc_scan_max_gaP)
 continue;
         }
 break;
       }
       sprintf(msg,
               "Chain of ISO session headers broken at #%d, LBA %ds",
               session_count+1, lba);
       isoburn_msgs_submit(o, 0x00060000, msg, 0, "WARNING", 0);

       if(with_enclosure) {
         ret= isoburn_make_toc_entry(o, &session_count, 0, image_size, NULL,0);
         if(ret<=0)
           goto failure;
       }
 break; /* do not return failure */

     }
     {ret= 0; goto failure;}
   }
   if(ret==2) /* ISO header was found in first half block */
     lba-= 16;

   if(readable_blocks >= 0 && lba + track_blocks > readable_blocks) {
     sprintf(msg, "ISO image size %ds larger than readable size %ds",
                  lba + track_blocks, readable_blocks);
     isoburn_msgs_submit(o, 0x00060000, msg, 0, "WARNING", 0);
     track_blocks= readable_blocks - lba;
   }
   ret= isoburn_make_toc_entry(o, &session_count, lba, track_blocks, volid_pt,
                               0);
   if(ret<=0)
     goto failure;
   lba+= track_blocks;
   scan_count+= 32;

   /* growisofs aligns to 16 rather than 32 */
   growisofs_nwa= lba;
   if(growisofs_nwa % 16)
     growisofs_nwa+= 16 - (growisofs_nwa % 16);
   if(lba % Libisoburn_nwa_alignemenT)
     lba+= Libisoburn_nwa_alignemenT - (lba % Libisoburn_nwa_alignemenT);
   scan_start= lba;
   if(lba - growisofs_nwa == 16)
     probe_minus_16= 1;
 }
 if(last_pacifier != start_time)
   sev= "UPDATE";
 else
   sev= "DEBUG";
 now= time(NULL);
 if(scan_count>=10*512)
   sprintf(size_text, "%.f MB", ((double) scan_count) / 512.0);
 else
   sprintf(size_text, "%.f kB", 2 * (double) scan_count);
 sprintf(msg, "Found %d ISO sessions by scanning %s in %.f seconds",
         session_count, size_text, (double) (now - start_time));
 isoburn_msgs_submit(o, 0x00060000, msg, 0, sev, 0);
 {ret= 1; goto ex;}
failure:;
 isoburn_toc_entry_destroy(&(o->toc), 1);
 if(with_enclosure && o->emulation_mode == 1) {
   if(readable_blocks >= 0 && image_size > readable_blocks) {
     sprintf(msg, "ISO image size %ds larger than readable size %ds",
                  image_size, readable_blocks);
     isoburn_msgs_submit(o, 0x00060000, msg, 0, "WARNING", 0);
     image_size= readable_blocks;
   }
   session_count= 0;
   ret= isoburn_make_toc_entry(o, &session_count, 0, image_size, NULL, 0);
 }
ex:;
 if(msg != NULL)
   free(msg);
 if(size_text != NULL)
   free(size_text);
 return(ret);
}


int isoburn_toc_new_arrays(struct isoburn_toc_disc *o,
                           int session_count, int track_count, int flag)
{
 int i;
 int isoburn_toc_destroy_arrays(struct isoburn_toc_disc *o, int flag);

 o->sessions= calloc(session_count, sizeof(struct isoburn_toc_session));
 o->session_pointers=
                   calloc(session_count, sizeof(struct isoburn_toc_session *));
 o->tracks= calloc(track_count, sizeof(struct isoburn_toc_track));
 o->track_pointers= calloc(track_count, sizeof(struct isoburn_toc_track *));
 if(o->sessions!=NULL && o->session_pointers!=NULL &&
    o->tracks!=NULL && o->track_pointers!=NULL) {
   for(i= 0; i<session_count; i++) {
     o->sessions[i].session= NULL;
     o->sessions[i].track_pointers= NULL;
     o->sessions[i].track_count= 0;
     o->sessions[i].toc_entry= NULL;
     o->session_pointers[i]= NULL;
   }
   for(i= 0; i<track_count; i++) {
     o->tracks[i].track= NULL;
     o->tracks[i].toc_entry= NULL;
     o->track_pointers[i]= NULL;
   }
   return(1);
 }
 /* failed */
 isoburn_toc_destroy_arrays(o, 0);
 return(-1);
}


int isoburn_toc_destroy_arrays(struct isoburn_toc_disc *o, int flag)
{
 if(o->sessions!=NULL)
   free((char *) o->sessions);
 o->sessions= NULL;
 if(o->session_pointers!=NULL)
   free((char *) o->session_pointers);
 o->session_pointers= NULL;
 if(o->tracks!=NULL)
   free((char *) o->tracks);
 o->tracks= NULL;
 if(o->track_pointers!=NULL)
   free((char *) o->track_pointers);
 o->track_pointers= NULL;
 return(1);
}


struct isoburn_toc_disc *isoburn_toc_drive_get_disc(struct burn_drive *d)
{
 int ret, session_count= 0, track_count= 0, num_tracks= 0, i, j;
 int open_sessions= 0;
 struct isoburn *o;
 struct isoburn_toc_entry *t;
 struct isoburn_toc_disc *toc_disc= NULL;
 struct burn_session **s;
 struct burn_track **tracks;

 toc_disc= calloc(1, sizeof(struct isoburn_toc_disc));
 if(toc_disc==NULL)
   return(NULL);
 toc_disc->disc= NULL;
 toc_disc->sessions= NULL;
 toc_disc->session_pointers= NULL;
 toc_disc->tracks= NULL;
 toc_disc->track_pointers= NULL;
 toc_disc->session_count= 0;
 toc_disc->incomplete_session_count= 0;
 toc_disc->track_count= 0;
 toc_disc->toc= NULL; 

 /* is the medium emulated multi-session ? */
 ret= isoburn_find_emulator(&o, d, 0);
 if(ret<0)
   goto libburn;
 if(o->toc==NULL)
   goto libburn;

 /* This is an emulated TOC */
 toc_disc->toc= o->toc;
 for(t= toc_disc->toc; t!=NULL; t= t->next)
   session_count++;
 ret= isoburn_toc_new_arrays(toc_disc, session_count, session_count, 0);
 if(ret<=0)
   goto failure;
 t= toc_disc->toc;
 for(i= 0; i<session_count; i++) {
   toc_disc->sessions[i].track_pointers= toc_disc->track_pointers+i;
   toc_disc->sessions[i].track_count= 1;
   toc_disc->sessions[i].toc_entry= t;
   toc_disc->session_pointers[i]= toc_disc->sessions+i;
   toc_disc->tracks[i].toc_entry= t;
   toc_disc->track_pointers[i]= toc_disc->tracks+i;
   t= t->next;
 }
 toc_disc->session_count= session_count;
 toc_disc->track_count= session_count;
 return(toc_disc);

libburn:;
 /* This is a libburn provided TOC */
 toc_disc->disc= burn_drive_get_disc(d);
 if(toc_disc->disc == NULL) {
failure:;
   free((char *) toc_disc);
   return(NULL);
 }
 s= burn_disc_get_sessions(toc_disc->disc, &session_count);
 open_sessions= burn_disc_get_incomplete_sessions(toc_disc->disc);
 for(i= 0; i < session_count + open_sessions; i++) {
   tracks = burn_session_get_tracks(s[i], &num_tracks);
   if(i == session_count + open_sessions - 1 && open_sessions > 0) {
     /* Do not count the invisible track of the last incomplete session */
     num_tracks--;
   }
   track_count+= num_tracks;
 }
 if(session_count + open_sessions <= 0 || track_count <= 0)
   goto failure;
 ret= isoburn_toc_new_arrays(toc_disc, session_count + open_sessions,
                             track_count, 0);
 if(ret<=0)
   goto failure;
 track_count= 0;
 for(i= 0; i < session_count + open_sessions; i++) {
   tracks = burn_session_get_tracks(s[i], &num_tracks);
   if(i == session_count + open_sessions - 1 && open_sessions > 0)
     num_tracks--;
   toc_disc->sessions[i].session= s[i];
   toc_disc->sessions[i].track_pointers= toc_disc->track_pointers+track_count;
   toc_disc->sessions[i].track_count= num_tracks;
   toc_disc->session_pointers[i]= toc_disc->sessions+i;
   for(j= 0; j<num_tracks; j++) {
     toc_disc->tracks[track_count+j].track= tracks[j];
     toc_disc->track_pointers[track_count+j]= toc_disc->tracks+(track_count+j);
   }
   track_count+= num_tracks;
 }
 toc_disc->session_count= session_count;
 toc_disc->incomplete_session_count= open_sessions;
 toc_disc->track_count= track_count;
 return(toc_disc);
}


int isoburn_toc_disc_get_sectors(struct isoburn_toc_disc *disc)
{
 struct isoburn_toc_entry *t;
 int ret= 0, num_sessions, num_tracks, open_sessions= 0, session_idx= -1;
 struct burn_session **sessions;
 struct burn_track **tracks;
 struct burn_toc_entry entry;

 if(disc==NULL)
   return(0);
 if(disc->toc!=NULL) {
   for(t= disc->toc; t!=NULL; t= t->next)
     ret= t->start_lba + t->track_blocks;
 } else if(disc->disc!=NULL) {
   sessions= burn_disc_get_sessions(disc->disc, &num_sessions);
   open_sessions= burn_disc_get_incomplete_sessions(disc->disc);
   if(num_sessions + open_sessions > 0) {
     session_idx= num_sessions + open_sessions - 1;
     tracks = burn_session_get_tracks(sessions[session_idx], &num_tracks);
     if(open_sessions > 0) {
       /* Do not count the invisible track of the last incomplete session */
       num_tracks--;
     }
     if(num_tracks <= 0)
       session_idx--;
   }
   if(session_idx >= 0) {
     tracks = burn_session_get_tracks(sessions[session_idx], &num_tracks);
     if(session_idx == num_sessions + open_sessions - 1 && open_sessions > 0) {
       /* Do not count the invisible track of the last incomplete session */
       num_tracks--;
     }
     if(num_tracks > 0) {
       burn_track_get_entry(tracks[num_tracks - 1], &entry);
       if(entry.extensions_valid & 1)
         ret= entry.start_lba + entry.track_blocks;
     }
   }
/*
   ret= burn_disc_get_sectors(disc->disc);
*/
 }
 return(ret);
}


struct isoburn_toc_session **isoburn_toc_disc_get_sessions(
                                      struct isoburn_toc_disc *disc, int *num)
{
 *num= disc->session_count;
 return(disc->session_pointers);
}


int isoburn_toc_disc_get_incmpl_sess(struct isoburn_toc_disc *disc)
{
 return(disc->incomplete_session_count);
}


int isoburn_toc_session_get_sectors(struct isoburn_toc_session *s)
{
 struct isoburn_toc_entry *t;
 int count= 0, i;

 if(s==NULL)
   return(0);
 if(s->toc_entry!=NULL) {
   t= s->toc_entry;
   for(i= 0; i<s->track_count; i++) {
     count+= t->track_blocks;
     t= t->next;
   }
 } else if(s->session!=NULL)
   count= burn_session_get_sectors(s->session);
 return(count);
}


int isoburn_toc_entry_finish(struct burn_toc_entry *entry,
                             int session_no, int track_no, int flag)
{
 int pmin, psec, pframe;

 entry->extensions_valid= 1;
 entry->adr= 1;
 entry->control= 4;
 entry->session= session_no & 255;
 entry->session_msb= (session_no >> 8) & 255;
 entry->point= track_no & 255;
 entry->point_msb= (track_no >> 8) & 255;

 burn_lba_to_msf(entry->start_lba, &pmin, &psec, &pframe);
 if(pmin<=255)
   entry->pmin= pmin;
 else
   entry->pmin= 255;
 entry->psec= psec;
 entry->pframe= pframe; 
 return(1);
}


void isoburn_toc_session_get_leadout_entry(struct isoburn_toc_session *s,
                                       struct burn_toc_entry *entry)
{
 struct isoburn_toc_track *t;

 if(s==NULL)
   return;
 if(s->session!=NULL && s->toc_entry==NULL) {
   burn_session_get_leadout_entry(s->session, entry);
   return;
 }
 if(s->track_count<=0 || s->track_pointers==NULL || s->toc_entry==NULL)
   return;
 t= s->track_pointers[s->track_count-1];
 entry->start_lba= t->toc_entry->start_lba + t->toc_entry->track_blocks;
 entry->track_blocks= 0;
 isoburn_toc_entry_finish(entry, s->toc_entry->session, t->toc_entry->track_no,
                          0);
}


struct isoburn_toc_track **isoburn_toc_session_get_tracks(
                                      struct isoburn_toc_session *s, int *num)
{
 *num= s->track_count;
 return(s->track_pointers);
}


void isoburn_toc_track_get_entry(struct isoburn_toc_track *t,
                                 struct burn_toc_entry *entry)
{
 if(t==0)
   return;
 if(t->track!=NULL && t->toc_entry==NULL) {
   burn_track_get_entry(t->track, entry);
   return;
 }
 if(t->toc_entry==NULL)
   return;
 entry->start_lba= t->toc_entry->start_lba;
 entry->track_blocks= t->toc_entry->track_blocks;
 isoburn_toc_entry_finish(entry, t->toc_entry->session, t->toc_entry->track_no,
                          0);
}


int isoburn_toc_track_get_emul(struct isoburn_toc_track *t, int *start_lba,
                               int *image_blocks, char volid[33], int flag)
{
 if(t->toc_entry == NULL)
   return(0);
 if(t->toc_entry->volid == NULL)
   return(0);
 *start_lba= t->toc_entry->start_lba;
 *image_blocks= t->toc_entry->track_blocks;
 strncpy(volid, t->toc_entry->volid, 32);
 volid[32]= 0;
 return(1);
}


void isoburn_toc_disc_free(struct isoburn_toc_disc *d)
{
 if(d->disc!=NULL)
   burn_disc_free(d->disc);
 isoburn_toc_destroy_arrays(d, 0);
 free((char *) d);
}


int isoburn_get_track_lba(struct isoburn_toc_track *track, int *lba, int flag)
{
 struct burn_toc_entry entry;

 isoburn_toc_track_get_entry(track, &entry);
 if (entry.extensions_valid & 1)
   *lba= entry.start_lba;
 else
   *lba= burn_msf_to_lba(entry.pmin, entry.psec, entry.pframe);
 return(1);
}


int isoburn_drive_set_msgs_submit(struct burn_drive *d,
                            int (*msgs_submit)(void *handle, int error_code, 
                                               char msg_text[], int os_errno, 
                                               char severity[], int flag),
                            void *submit_handle, int submit_flag, int flag)
{
 struct isoburn *o;
 int ret;

 ret= isoburn_find_emulator(&o, d, 0);
 if(ret<0 || o==NULL)
   return(-1);
 o->msgs_submit= msgs_submit;
 o->msgs_submit_handle= submit_handle;
 o->msgs_submit_flag= submit_flag;
 return(1);
}

 
/* @param flag bit0= with adr_mode 3: adr_value might be 16 blocks too high
               bit1= insist in seeing a disc object with at least one session
               bit2= with adr_mode 4: use adr_value as regular expression
*/
int isoburn_set_msc1(struct burn_drive *d, int adr_mode, char *adr_value,
                     int flag)
{
 int ret, num_sessions= 0, num_tracks, adr_num, i, j, total_tracks;
 int lba, best_lba, size, re_valid= 0, track_count= 0;
 time_t start_time= 0, last_pacifier= 0, now;
 char volid[33], *msg= NULL;
 struct isoburn *o;
 struct isoburn_toc_disc *disc= NULL;
 struct isoburn_toc_session **sessions= NULL;
 struct isoburn_toc_track **tracks= NULL;
 static char mode_names[][20]= {"auto", "session", "track", "lba", "volid"};
 static int max_mode_names= 4;
 regex_t re;
 regmatch_t match[1];
 enum burn_disc_status s;

 ret= isoburn_find_emulator(&o, d, 0);
 if(ret<0)
   return(-1);
 if(o==NULL)
   return(-1);

 msg= calloc(1, 160);
 if(msg == NULL)
   {ret= -1; goto ex;}

 start_time= last_pacifier= time(NULL);
 adr_num= atoi(adr_value);
 if(adr_mode!=3 || (flag & 2)) {
   disc= isoburn_toc_drive_get_disc(d);
   if(disc==NULL) {
not_found:;
     if(adr_mode<0 || adr_mode>max_mode_names)
       goto unknown_mode;
     sprintf(msg, "Failed to find %s %s", mode_names[adr_mode],
                  strlen(adr_value)<=80 ?  adr_value : "-oversized-string-");
     isoburn_msgs_submit(o, 0x00060000, msg, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }
   sessions= isoburn_toc_disc_get_sessions(disc, &num_sessions);
   if(sessions==NULL || num_sessions<=0)
     goto not_found;
 }
 if(adr_mode==0) {
   /* Set fabricated_msc1 to last session in TOC */
   tracks= isoburn_toc_session_get_tracks(sessions[num_sessions-1],
                                          &num_tracks);
   if(tracks==NULL || num_tracks<=0)
     goto not_found;
   isoburn_get_track_lba(tracks[0], &(o->fabricated_msc1), 0);

 } else if(adr_mode==1) {
   /* Use adr_num as session index (first session is 1, not 0) */
   if(adr_num<1 || adr_num>num_sessions)
     goto not_found;
   tracks= isoburn_toc_session_get_tracks(sessions[adr_num-1], &num_tracks);
   if(tracks==NULL || num_tracks<=0)
     goto not_found;
   isoburn_get_track_lba(tracks[0], &(o->fabricated_msc1), 0);

 } else if(adr_mode==2) {
   /* use adr_num as track index */
   total_tracks= 0;
   for(i=0; i<num_sessions; i++) {
     tracks= isoburn_toc_session_get_tracks(sessions[i], &num_tracks);
     if(tracks==NULL)
   continue;
     for(j= 0; j<num_tracks; j++) {
       total_tracks++;
       if(total_tracks==adr_num) {
         isoburn_get_track_lba(tracks[j], &(o->fabricated_msc1), 0);
         ret= 1; goto ex;
       }
     }
   }
   goto not_found;

 } else if(adr_mode==3) {
   o->fabricated_msc1= adr_num;
   s= isoburn_disc_get_status(d);
   if(o->fabricated_msc1 > 0 && s != BURN_DISC_FULL
      && s != BURN_DISC_APPENDABLE) {
     isoburn_msgs_submit(o, 0x00060000,
                         "Non-zero load offset given with blank input media",
                         0, "FAILURE", 0);
     ret= 0; goto ex;
   }
   if((flag & 1) && o->fabricated_msc1 >= 16) {
     /* adr_num is possibly 16 blocks too high */
     ret= isoburn_read_iso_head(d, o->fabricated_msc1, &size,volid, 1|(1<<14));
     if(ret==2)
       o->fabricated_msc1-= 16;
   }
 } else if(adr_mode==4) {
   /* search for volume id that is equal to adr_value */
   if(flag & 4) {
     ret= regcomp(&re, adr_value, 0);
     if(ret != 0)
       flag&= ~4;
     else
       re_valid= 1;
   }
   best_lba= -1;
   for(i=0; i<num_sessions; i++) {
     tracks= isoburn_toc_session_get_tracks(sessions[i], &num_tracks);
     if(tracks==NULL)
   continue;
     for(j= 0; j<num_tracks; j++) {
       now= time(NULL);
       if(now - last_pacifier >= 5 && track_count > 0) {
         last_pacifier= now;
         sprintf(msg,
                 "Scanned %d tracks for matching volid in %.f seconds",
                 track_count, (double) (now - start_time));
         isoburn_msgs_submit(o, 0x00060000, msg, 0, "UPDATE", 0);
       }
       track_count++;
       ret= isoburn_toc_track_get_emul(tracks[0], &lba, &size, volid, 0);
       if(ret < 0)
     continue;
       if(ret == 0) {
         isoburn_get_track_lba(tracks[0], &lba, 0);
         ret= isoburn_read_iso_head(d, lba, &size, volid, 1);
         if(ret<=0)
     continue;
       }
       if(flag & 4) {
         ret= regexec(&re, volid, 1, match, 0);
         if(ret != 0)
     continue;
       } else {
         if(strcmp(volid, adr_value)!=0)
     continue;
       }
       best_lba= lba;
     }
   }
   if(best_lba<0)
     goto not_found;
   o->fabricated_msc1= best_lba;

 } else {
unknown_mode:;
   sprintf(msg, "Program error: Unknown msc1 address mode %d", adr_mode);
   isoburn_msgs_submit(o, 0x00060000, msg, 0, "FATAL", 0);
   ret= 0; goto ex;
 }
 ret= 1;
ex:;
 if(start_time != last_pacifier && track_count > 0) {
   now= time(NULL);
   sprintf(msg,
           "Scanned %d tracks for matching volid in %.f seconds",
           track_count, (double) (now - start_time));
   isoburn_msgs_submit(o, 0x00060000, msg, 0, "UPDATE", 0);
 }
 if(disc!=NULL)
   isoburn_toc_disc_free(disc);
 if((flag & 4) && re_valid)
   regfree(&re);
 if(msg != NULL)
   free(msg);
 return(ret);
}


int isoburn_get_mount_params(struct burn_drive *d,
                             int adr_mode, char *adr_value,
                             int *lba, int *track, int *session,
                             char volid[33], int flag)
{
 int msc1_mem, ret, total_tracks, num_sessions, num_tracks, i, j, track_lba;
 int size, is_iso= 0;
 struct isoburn *o;
 struct isoburn_toc_disc *disc= NULL;
 struct isoburn_toc_session **sessions= NULL;
 struct isoburn_toc_track **tracks= NULL;

 *lba= *track= *session= -1;
 volid[0]= 0;
 ret= isoburn_find_emulator(&o, d, 0);
 if(ret < 0 || o == NULL)
   return(-1);
 msc1_mem= o->fabricated_msc1;
 ret= isoburn_set_msc1(d, adr_mode, adr_value, 2 | (flag & 4));
 if(ret <= 0)
   return(ret);
 *lba= o->fabricated_msc1;

 disc= isoburn_toc_drive_get_disc(d);
 if(disc==NULL) 
   {ret= -1; goto ex;} /* cannot happen because checked by isoburn_set_msc1 */
 sessions= isoburn_toc_disc_get_sessions(disc, &num_sessions);
 if(sessions==NULL || num_sessions<=0)
   {ret= -1; goto ex;} /* cannot happen because checked by isoburn_set_msc1 */
 total_tracks= 0;
 for(i=0; i<num_sessions && *session < 0; i++) {
   tracks= isoburn_toc_session_get_tracks(sessions[i], &num_tracks);
   if(tracks==NULL)
 continue;
   for(j= 0; j<num_tracks && *track < 0; j++) {
     total_tracks++;
     isoburn_get_track_lba(tracks[j], &track_lba, 0);
     if(track_lba == *lba) {
       *track= total_tracks;
       *session= i + 1;
     }
   }
 }
 ret= isoburn_read_iso_head(d, *lba, &size, volid, 1);
 if(ret <= 0)
   volid[0]= 0;
 else
   is_iso= 1;

ex:;
 o->fabricated_msc1= msc1_mem;
 if(disc != NULL)
   isoburn_toc_disc_free(disc);
 return(2 - is_iso); 
}


