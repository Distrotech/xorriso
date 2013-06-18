
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2011 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains functions which manage the relation between xorriso
   and the libraries: libburn, libisofs, libisoburn.
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif

/* for -charset */
#include <iconv.h>
#include <langinfo.h>

#ifdef Xorriso_standalonE

#ifdef Xorriso_with_libjtE
#include "../libjte/libjte.h"
#endif

#else

#ifdef Xorriso_with_libjtE
#include <libjte/libjte.h>
#endif

#endif /* ! Xorriso_standalonE */

#include "xorriso.h" 
#include "xorriso_private.h"
#include "xorrisoburn.h"

#include "lib_mgt.h"
#include "iso_manip.h"


int Xorriso_abort(struct XorrisO *xorriso, int flag)
{
 int ret;

 ret= burn_abort(4440, burn_abort_pacifier, "xorriso : ");
 if(ret<=0) {
   fprintf(stderr,
       "\nxorriso : ABORT : Cannot cancel burn session and release drive.\n");
   return(0);
 } 
 fprintf(stderr,
   "xorriso : ABORT : Drive is released and library is shut down now.\n");
 fprintf(stderr,
   "xorriso : ABORT : Program done. Even if you do not see a shell prompt.\n");
 fprintf(stderr, "\n");
 exit(1);
}


/* @param flag bit0= asynchronous handling (else catch thread, wait, and exit)
               bit1= dealing with MMC drive in critical state
                     behavior 2 -> behavior 1
*/
int Xorriso_set_signal_handling(struct XorrisO *xorriso, int flag)
{
 char *handler_prefix= NULL;
 int behavior, mode;

 behavior= Xorriso__get_signal_behavior(0);
 if(behavior == 0)
   return(2);
 if(behavior == 2 && !(flag & 2))
   mode= 1;
 else if(behavior == 3)
   mode= 2;
 else
   mode= (flag & 1) * 0x30;
 handler_prefix= calloc(strlen(xorriso->progname)+3+1, 1);
 if(handler_prefix==NULL) {
   sprintf(xorriso->info_text,
           "Cannot allocate memory for setting signal handler");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   return(-1);
 }

 /* <<< */
 sprintf(xorriso->info_text, "burn_set_signal_handling(%d)", mode);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);

 sprintf(handler_prefix, "%s : ", xorriso->progname);
 burn_set_signal_handling(handler_prefix, NULL, mode);
 free(handler_prefix);
 return(1);
}


int Xorriso_startup_libraries(struct XorrisO *xorriso, int flag)
{
 int ret, major, minor, micro;
 char *queue_sev, *print_sev, reason[1024];
 struct iso_zisofs_ctrl zisofs_ctrl= {0, 6, 15};


/* First an ugly compile time check for header version compatibility.
   If everthing matches, then no C code is produced. In case of mismatch,
   intentionally faulty C code will be inserted.
*/

/* The minimum requirement of xorriso towards the libisoburn header
   at compile time is defined in xorriso/xorrisoburn.h 
     xorriso_libisoburn_req_major
     xorriso_libisoburn_req_minor
     xorriso_libisoburn_req_micro
   It gets compared against the version macros in libburn/libburn.h :
     isoburn_header_version_major
     isoburn_header_version_minor
     isoburn_header_version_micro
   If the header is too old then the following code shall cause failure of
   cdrskin compilation rather than to allow production of a program with
   unpredictable bugs or memory corruption.
   The compiler messages supposed to appear in this case are:
      error: 'LIBISOBURN_MISCONFIGURATION' undeclared (first use in this function)
      error: 'INTENTIONAL_ABORT_OF_COMPILATION__HEADERFILE_libisoburn_dot_h_TOO_OLD__SEE_xorrisoburn_dot_c' undeclared (first use in this function)
      error: 'LIBISOBURN_MISCONFIGURATION_' undeclared (first use in this function)
*/
/* The indendation is an advise of man gcc to help old compilers ignoring */
 #if xorriso_libisoburn_req_major > isoburn_header_version_major
 #define Isoburn_libisoburn_dot_h_too_olD 1
 #endif
 #if xorriso_libisoburn_req_major == isoburn_header_version_major && xorriso_libisoburn_req_minor > isoburn_header_version_minor
 #define Isoburn_libisoburn_dot_h_too_olD 1
 #endif
 #if xorriso_libisoburn_req_minor == isoburn_header_version_minor && xorriso_libisoburn_req_micro > isoburn_header_version_micro
 #define Isoburn_libisoburn_dot_h_too_olD 1
 #endif

#ifdef Isoburn_libisoburn_dot_h_too_olD
LIBISOBURN_MISCONFIGURATION = 0;
INTENTIONAL_ABORT_OF_COMPILATION__HEADERFILE_libisoburn_dot_h_TOO_OLD__SEE_xorrisoburn_dot_c = 0;
LIBISOBURN_MISCONFIGURATION_ = 0;
#endif

/* End of ugly compile time test (scroll up for explanation) */

 reason[0]= 0;
 ret= isoburn_initialize(reason, 0);
 if(ret==0) {
   sprintf(xorriso->info_text, "Cannot initialize libraries");
   if(reason[0])
     sprintf(xorriso->info_text+strlen(xorriso->info_text),
             ". Reason given:\n%s", reason);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   return(0);
 }
 ret= isoburn_is_compatible(isoburn_header_version_major,
                            isoburn_header_version_minor,
                            isoburn_header_version_micro, 0);
 if(ret<=0) {
   isoburn_version(&major, &minor, &micro);
   sprintf(xorriso->info_text,
          "libisoburn version too old: %d.%d.%d . Need at least: %d.%d.%d .\n",
          major, minor, micro,
          isoburn_header_version_major, isoburn_header_version_minor,
          isoburn_header_version_micro);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   return(-1);
 }

 xorriso->libs_are_started= 1;

 queue_sev= "ALL";
 if(xorriso->library_msg_direct_print) {

   /* >>> need option for controlling this in XorrisO.
          See also Xorriso_msgs_submit */;

   print_sev= xorriso->report_about_text;
 } else
   print_sev= "NEVER";

 iso_set_msgs_severities(queue_sev, print_sev, "libsofs : ");
 burn_msgs_set_severities(queue_sev, print_sev, "libburn : ");

 /* ??? >>> do we want united queues ? */
 /* burn_set_messenger(iso_get_messenger()); */

 isoburn_set_msgs_submit(Xorriso_msgs_submit_void, (void *) xorriso,
                         (3<<2) | 128 , 0);

 ret= Xorriso_set_signal_handling(xorriso, 0);
 if(ret <= 0)
   return(ret);

 ret = iso_zisofs_get_params(&zisofs_ctrl, 0);
 if (ret == 1) {
   xorriso->zisofs_block_size= xorriso->zisofs_block_size_default=
       (1 << zisofs_ctrl.block_size_log2);
   xorriso->zlib_level= xorriso->zlib_level_default=
       zisofs_ctrl.compression_level;
 }

 iso_node_xinfo_make_clonable(Xorriso__mark_update_xinfo,
                              Xorriso__mark_update_cloner, 0);

 /* Second initialization. This time with libs. */
 Xorriso_preparer_string(xorriso, xorriso->preparer_id, 0);

 Xorriso_process_msg_queues(xorriso,0);
 if(reason[0]) {
   sprintf(xorriso->info_text, "%s", reason);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
 }
 strcpy(xorriso->info_text, "Using ");
 strncat(xorriso->info_text, burn_scsi_transport_id(0), 1024);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
 return(1);
}


/* @param flag bit0= global shutdown of libraries */
int Xorriso_detach_libraries(struct XorrisO *xorriso, int flag)
{
 Xorriso_give_up_drive(xorriso, 3);
 if(xorriso->in_volset_handle!=NULL) { /* standalone image */
   iso_image_unref((IsoImage *) xorriso->in_volset_handle);
   xorriso->in_volset_handle= NULL;
   Sectorbitmap_destroy(&(xorriso->in_sector_map), 0);
   Xorriso_destroy_di_array(xorriso, 0);
   Xorriso_destroy_hln_array(xorriso, 0);
   xorriso->boot_count= 0;
 }
 if(flag&1) {
   if(xorriso->libs_are_started==0)
     return(0);
   isoburn_finish();
 }
 return(1);
}


/* @param flag bit0= suppress messages below UPDATE
               bit1= suppress messages below FAILURE
*/
int Xorriso_set_image_severities(struct XorrisO *xorriso, int flag)
{
 char *queue_sev, *print_sev;

 if(flag&2)
   queue_sev= "FAILURE";
 else if(flag&1)
   queue_sev= "UPDATE";
 else
   queue_sev= "ALL";
 if(xorriso->library_msg_direct_print)
   print_sev= xorriso->report_about_text;
 else
   print_sev= "NEVER";
 iso_set_msgs_severities(queue_sev, print_sev, "libisofs : ");
 return(1);
}


/* @param flag bit0=prepare for a burn run */
int Xorriso_set_abort_severity(struct XorrisO *xorriso, int flag)
{
 int ret, abort_on_number;
 char *sev_text;
 static int note_number= -1, failure_number= -1;

 if(note_number==-1)
   Xorriso__text_to_sev("NOTE", &note_number, 0);
 if(failure_number==-1)
   Xorriso__text_to_sev("FAILURE", &failure_number, 0);
 sev_text= xorriso->abort_on_text;
 ret= Xorriso__text_to_sev(xorriso->abort_on_text, &abort_on_number, 0);
 if(ret<=0)
   return(ret);
 if(abort_on_number<note_number)
   sev_text= "NOTE";
 else if(abort_on_number>failure_number)
   sev_text= "FAILURE";
 ret= iso_set_abort_severity(sev_text);
 return(ret>=0);
}


int Xorriso_report_lib_versions(struct XorrisO *xorriso, int flag)
{
 int major, minor, micro;
 int req_major, req_minor, req_micro;

 iso_lib_version(&major, &minor, &micro);
 isoburn_libisofs_req(&req_major, &req_minor, &req_micro);
 sprintf(xorriso->result_line,
         "libisofs   in use :  %d.%d.%d  (min. %d.%d.%d)\n",
         major, minor, micro, req_major, req_minor, req_micro);
 Xorriso_result(xorriso, 0);

#ifdef Xorriso_with_libjtE
 libjte__version(&major, &minor, &micro);
 isoburn_libjte_req(&req_major, &req_minor, &req_micro);
 sprintf(xorriso->result_line,
         "libjte     in use :  %d.%d.%d  (min. %d.%d.%d)\n",
         major, minor, micro, req_major, req_minor, req_micro);
 Xorriso_result(xorriso, 0);
#endif

 burn_version(&major, &minor, &micro);
 isoburn_libburn_req(&req_major, &req_minor, &req_micro);
 sprintf(xorriso->result_line,
         "libburn    in use :  %d.%d.%d  (min. %d.%d.%d)\n",
         major, minor, micro, req_major, req_minor, req_micro);
 Xorriso_result(xorriso, 0);
 strcpy(xorriso->result_line, "libburn OS adapter:  ");
 strncat(xorriso->result_line, burn_scsi_transport_id(0), 1024);
 strcat(xorriso->result_line, "\n");
 Xorriso_result(xorriso, 0);
 isoburn_version(&major, &minor, &micro);
 sprintf(xorriso->result_line,
         "libisoburn in use :  %d.%d.%d  (min. %d.%d.%d)\n",
         major, minor, micro,
         isoburn_header_version_major, isoburn_header_version_minor,
         isoburn_header_version_micro);
 Xorriso_result(xorriso, 0);
 return(1);
}


int Xorriso__sev_to_text(int severity, char **severity_name,
                         int flag)
{
 int ret;

 ret= iso_sev_to_text(severity, severity_name);
 if(ret>0)
   return(ret);
 ret= burn_sev_to_text(severity, severity_name, 0);
 if(ret>0)
   return(ret);
 *severity_name= "";
 return(0);
}


int Xorriso__text_to_sev(char *severity_name, int *severity_number, int flag)
{
 int ret= 1;
 char severity[20];

 Xorriso__to_upper(severity_name, severity, (int) sizeof(severity), 0);
 ret= iso_text_to_sev(severity, severity_number);
 if(ret>0)
   return(ret);
 ret= burn_text_to_sev(severity, severity_number, 0);
 return(ret);
}


int Xorriso__severity_cmp(char *sev1, char *sev2)
{
 int s1= 0x7fffffff, s2= 0x7fffffff, ret;
 char *default_sev= "FATAL";

 ret= Xorriso__text_to_sev(sev1, &s1, 0);
 if(ret <= 0)
   Xorriso__text_to_sev(default_sev, &s1, 0);
 ret= Xorriso__text_to_sev(sev2, &s2, 0);
 if(ret <= 0)
   Xorriso__text_to_sev(default_sev, &s2, 0);
 if(s1 < s2)
   return -1;
 if(s1 > s2)
   return(1);
 return(0);
}


char *Xorriso__severity_list(int flag)
{
 return(burn_list_sev_texts(0));
}


/* @param flag bit0= report libisofs error text
               bit1= victim is disk_path
               bit2= do not inquire libisofs, report msg_text and min_severity
*/
int Xorriso_report_iso_error(struct XorrisO *xorriso, char *victim,
                        int iso_error_code, char msg_text[], int os_errno,
                        char min_severity[], int flag)
{
 int error_code, iso_sev, min_sev, ret;
 char *sev_text_pt, *msg_text_pt= NULL;
 char *sfe= NULL;
 static int sorry_sev= -1;

 Xorriso_alloc_meM(sfe, char, 6 * SfileadrL);

 if(sorry_sev<0)
   Xorriso__text_to_sev("SORRY", &sorry_sev, 0);

 if(flag&4) {
   error_code= 0x00050000;
   Xorriso__text_to_sev(min_severity, &iso_sev, 0);
 } else {
   error_code= iso_error_get_code(iso_error_code);
   if(error_code < 0x00030000 || error_code >= 0x00040000)
     error_code= (error_code & 0xffff) | 0x00050000;
   if(flag&1)
     msg_text_pt= (char *) iso_error_to_msg(iso_error_code);
   iso_sev= iso_error_get_severity(iso_error_code);
 }
 if(msg_text_pt==NULL)
   msg_text_pt= msg_text;

 if(iso_sev >= sorry_sev && (flag & 2) && victim[0])
   Xorriso_msgs_submit(xorriso, 0, victim, 0, "ERRFILE", 0);
 sev_text_pt= min_severity;
 Xorriso__text_to_sev(min_severity, &min_sev, 0);
 if(min_sev < iso_sev && !(flag&4))
   Xorriso__sev_to_text(iso_sev, &sev_text_pt, 0);
 strcpy(sfe, msg_text_pt);
 if(victim[0]) {
   strcat(sfe, ": ");
   Text_shellsafe(victim, sfe+strlen(sfe), 0);
 }
 ret= Xorriso_msgs_submit(xorriso, error_code, sfe, os_errno, sev_text_pt, 4);
ex:;
 Xorriso_free_meM(sfe);
 return(ret);
}


int Xorriso_get_local_charset(struct XorrisO *xorriso, char **name, int flag)
{
 (*name)= iso_get_local_charset(0);
 return(1);
}


int Xorriso_set_local_charset(struct XorrisO *xorriso, char *name, int flag)
{
 int ret;
 char *nl_charset;
 iconv_t iconv_ret= (iconv_t) -1;

 nl_charset= nl_langinfo(CODESET);
 if(name == NULL)
   name= nl_charset;

 if(name != NULL) {
   iconv_ret= iconv_open(nl_charset, name);
   if(iconv_ret == (iconv_t) -1)
     goto cannot;
   else
     iconv_close(iconv_ret);
 }
 ret= iso_set_local_charset(name, 0);
 if(ret <= 0) {
cannot:;
   sprintf(xorriso->info_text,
           "-local_charset: Cannot assume as local character set: ");
   Text_shellsafe(name, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   return(0);
 }
 sprintf(xorriso->info_text, "Local character set is now assumed as: ");
 Text_shellsafe(name, xorriso->info_text, 1);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
 return(1);
}


int Xorriso_process_msg_queues(struct XorrisO *xorriso, int flag)
{
 int ret, error_code= 0, os_errno= 0, count= 0, pass, imgid, tunneled;
 int name_prefix_code;
 char severity[80], *text= NULL;

#ifdef Xorriso_fetch_with_msg_queueS
 int locked= 0, uret;
#endif

#ifdef Xorriso_with_libjtE
 char *msg;
#endif

 if(!xorriso->libs_are_started) {
   ret= 1; goto ex;
 }

#ifdef Xorriso_fetch_with_msg_queueS

 Xorriso_alloc_meM(text, char, sizeof(xorriso->info_text));

 ret= pthread_mutex_lock(&(xorriso->lib_msg_queue_lock));
 if(ret != 0) {
   Xorriso_msgs_submit(xorriso, 0,
              "Cannot aquire mutex lock for processing library message queues",
               ret, "FATAL", 0);
 } else
   locked= 1;

#else /* Xorriso_fetch_with_msg_queueS */

 text= xorriso->info_text;

#endif /* ! Xorriso_fetch_with_msg_queueS */


 for(pass= 0; pass< 3; pass++) {
   while(1) {
     tunneled= 0;
     if(pass==0) {
       ret= 0;
#ifdef Xorriso_with_libjtE
       if(xorriso->libjte_handle != NULL) {
         msg= libjte_get_next_message(xorriso->libjte_handle);
         if(msg != NULL) {
           sprintf(text, "%1.4095s", msg);
           free(msg);
           strcpy(severity, "NOTE");
           error_code= 0;
           os_errno= 0;
           ret= 1;
         }
       }
#endif /* Xorriso_with_libjtE */

     } else if(pass==1)
       ret= iso_obtain_msgs("ALL", &error_code, &imgid, text, severity);
     else {
       ret= burn_msgs_obtain("ALL", &error_code, text, &os_errno, severity);
       if((error_code>=0x00030000 && error_code<0x00040000) ||
          (error_code>=0x00050000 && error_code<0x00060000))
         tunneled= -1; /* "libisofs:" */
       else if(error_code>=0x00060000 && error_code<0x00070000)
         tunneled= 1;  /* "libisoburn:" */
     }
     if(ret<=0)
   break;

     /* <<< tunneled MISHAP from libisoburn through libburn
            or well known error codes of MISHAP events
            With libburn-0.4.4 this is not necessary */
     if(error_code==0x5ff73 || error_code==0x3ff73 ||
        error_code==0x3feb9 || error_code==0x3feb2)
       strcpy(severity, "MISHAP");
     else if(error_code==0x51001)
       strcpy(severity, "ERRFILE");

     if(pass == 0)
       name_prefix_code= 0;
     else
       name_prefix_code= pass + tunneled;
     Xorriso_msgs_submit(xorriso, error_code, text, os_errno, 
                         severity, name_prefix_code << 2);
     count++;
   }
 }
 if(xorriso->library_msg_direct_print && count>0) {
   sprintf(text,"   (%d library messages repeated by xorriso)\n", count);

#ifdef Xorriso_fetch_with_msg_queueS

   Xorriso_msgs_submit(xorriso, 0, text, 0, "NOTE", 256);

#else /* Xorriso_fetch_with_msg_queueS */

   Xorriso_info(xorriso, 0);

#endif /* Xorriso_fetch_with_msg_queueS */

 }
 ret= 1;
ex:;

#ifdef Xorriso_fetch_with_msg_queueS

 if(locked) {
   uret= pthread_mutex_unlock(&(xorriso->lib_msg_queue_lock));
   if(uret != 0) {
     Xorriso_msgs_submit(xorriso, 0,
             "Cannot release mutex lock for processing library message queues",
              uret, "FATAL", 0);
     ret= -1;
   }
 }
 Xorriso_free_meM(text);

#endif /* Xorriso_fetch_with_msg_queueS */

 return(ret);
}


int Xorriso_md5_start(struct XorrisO *xorriso, void **ctx, int flag)
{
 int ret;

 ret= iso_md5_start(ctx);
 if(ret == 1)
   return(1);
 Xorriso_no_malloc_memory(xorriso, NULL, 0);
 return(-1);
}


int Xorriso_md5_compute(struct XorrisO *xorriso, void *ctx,
                        char *data, int datalen, int flag)
{
 iso_md5_compute(ctx, data, datalen);
 return(1);
}


int Xorriso_md5_end(struct XorrisO *xorriso, void **ctx, char md5[16],
                    int flag)
{
 int ret;

 ret= iso_md5_end(ctx, md5);
 Xorriso_process_msg_queues(xorriso,0);
 if(ret <= 0)
   return(0);
 return(1);
}


/* @param flag  bit0= avoid library calls
 */
int Xorriso_preparer_string(struct XorrisO *xorriso, char xorriso_id[129],
                            int flag)
{
 int major, minor, micro;

 xorriso_id[0]= 0;
 sprintf(xorriso_id, "XORRISO-%d.%d.%d ",
         Xorriso_header_version_majoR, Xorriso_header_version_minoR,
         Xorriso_header_version_micrO);
 if(strlen(xorriso_id) + strlen(Xorriso_timestamP) < 128)
   strcat(xorriso_id, Xorriso_timestamP);
 if(flag & 1)
   return(1);
 isoburn_version(&major, &minor, &micro);
 if(strlen(xorriso_id) < 100)
   sprintf(xorriso_id + strlen(xorriso_id),
           ", LIBISOBURN-%d.%d.%d", major, minor, micro);
 iso_lib_version(&major, &minor, &micro);
 if(strlen(xorriso_id) < 100)
   sprintf(xorriso_id + strlen(xorriso_id),
           ", LIBISOFS-%d.%d.%d", major, minor, micro);
 burn_version(&major, &minor, &micro);
 if(strlen(xorriso_id) < 100)
   sprintf(xorriso_id + strlen(xorriso_id),
           ", LIBBURN-%d.%d.%d", major, minor, micro);
 return(1);
}


#ifdef Xorriso_with_libjtE

int Xorriso_assert_jte_handle(struct XorrisO *xorriso, int flag)
{
 int ret;
 
 if(xorriso->libjte_handle == NULL) {
   ret= libjte_new(&(xorriso->libjte_handle), 0);
   if(ret <= 0 || xorriso->libjte_handle == NULL) {
     sprintf(xorriso->info_text,
             "-jigdo: Failed to create libjte environment object"); 
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
     return(-1);
   }
   /* no stderr, no exit() */
   libjte_set_error_behavior(xorriso->libjte_handle, 0, 0);
 }
 return(1);
}

#endif /* Xorriso_with_libjtE */


int Xorriso_jigdo_interpreter(struct XorrisO *xorriso, char *aspect, char *arg,
                         int flag)
{

#ifdef Xorriso_with_libjtE

 int ret, num;
 struct libjte_env *jte;
 char *msg = NULL;

 if(strcmp(aspect, "clear") == 0) {
   if(xorriso->libjte_handle != NULL)
   libjte_destroy(&(xorriso->libjte_handle));
   Xorriso_lst_destroy_all(&(xorriso->jigdo_params), 0);
   Xorriso_lst_destroy_all(&(xorriso->jigdo_values), 0);
   xorriso->libjte_params_given= 0;
   return(1);
 }
 ret= Xorriso_assert_jte_handle(xorriso, 0);
 if(ret <= 0)
   return(ret);
 jte= xorriso->libjte_handle;

 if(strcmp(aspect, "verbose") == 0) {
   if(strcmp(arg, "on") == 0) {
     libjte_set_verbose(jte, 1);
     /* Direct libjte messages to stderr, rather than message list */
     libjte_set_error_behavior(xorriso->libjte_handle, 1, 0);
     xorriso->libjte_params_given|= 2;
   } else if(strcmp(arg, "off") == 0) {
     libjte_set_verbose(jte, 0);
     libjte_set_error_behavior(xorriso->libjte_handle, 0, 0);
     xorriso->libjte_params_given&= ~2;
   } else
     goto bad_arg;
 } else if(strcmp(aspect, "template_path") == 0 ||
           strcmp(aspect, "-jigdo-template") == 0) {
   ret= libjte_set_template_path(jte, arg);
   if(ret <= 0)
     goto jte_failed;
   xorriso->libjte_params_given|= 4;
 } else if(strcmp(aspect, "jigdo_path") == 0 ||
           strcmp(aspect, "-jigdo-jigdo") == 0) {
   ret= libjte_set_jigdo_path(jte, arg);
   if(ret <= 0)
     goto jte_failed;
   xorriso->libjte_params_given|= 8;
 } else if(strcmp(aspect, "md5_path") == 0 ||
           strcmp(aspect, "-md5-list") == 0) {
   ret= libjte_set_md5_path(jte, arg);
   if(ret <= 0)
     goto jte_failed;
   xorriso->libjte_params_given|= 16;
 } else if(strcmp(aspect, "min_size") == 0 ||
           strcmp(aspect, "-jigdo-min-file-size") == 0) {
   num= Scanf_io_size(arg, 0);
   ret= libjte_set_min_size(jte, num);
   if(ret <= 0)
     goto jte_failed;
   xorriso->libjte_params_given|= 32;
 } else if(strcmp(aspect, "checksum_iso") == 0 ||
           strcmp(aspect, "-checksum_algorithm_iso") == 0) {
   ret= libjte_set_checksum_iso(jte, arg);
   if(ret <= 0)
     goto jte_failed;
   xorriso->libjte_params_given|= 64;
 } else if(strcmp(aspect, "checksum_template") == 0 ||
           strcmp(aspect, "-checksum_algorithm_template") == 0) {
   ret= libjte_set_checksum_template(jte, arg);
   if(ret <= 0)
     goto jte_failed;
   xorriso->libjte_params_given|= 128;
 } else if(strcmp(aspect, "compression") == 0 ||
           strcmp(aspect, "-jigdo-template-compress") == 0) {
   ret= libjte_set_compression(jte, arg);
   if(ret <= 0)
     goto jte_failed;
   xorriso->libjte_params_given|= 256;
 } else if(strcmp(aspect, "exclude") == 0 ||
           strcmp(aspect, "-jigdo-exclude") == 0) {
   ret= libjte_add_exclude(jte, arg);
   if(ret <= 0)
     goto jte_failed;
   xorriso->libjte_params_given|= 512;
 } else if(strcmp(aspect, "demand_md5") == 0 ||
           strcmp(aspect, "-jigdo-force-md5") == 0) {
   ret= libjte_add_md5_demand(jte, arg);
   if(ret <= 0)
     goto jte_failed;
   xorriso->libjte_params_given|= 1024;
 } else if(strcmp(aspect, "mapping") == 0 ||
           strcmp(aspect, "-jigdo-map") == 0) {
   ret= libjte_add_mapping(jte, arg);
   if(ret <= 0)
     goto jte_failed;
   xorriso->libjte_params_given|= 2048;
 } else {
   sprintf(xorriso->info_text, "-jigdo: unknown aspect '%s'", aspect); 
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }

 ret= Xorriso_lst_new(&(xorriso->jigdo_params), aspect, xorriso->jigdo_params,
                      1);
 if(ret > 0)
   ret= Xorriso_lst_new(&(xorriso->jigdo_values), arg, xorriso->jigdo_values,
                        1);
 if(ret <= 0) {
   Xorriso_no_malloc_memory(xorriso, NULL, 0);
   return(-1);
 }
 Xorriso_process_msg_queues(xorriso, 0);
 return(1);

bad_arg:
 sprintf(xorriso->info_text, "-jigdo %s : unknown argument '%s'", aspect, arg);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
 return(0);

jte_failed:
 while(1) {
   msg= libjte_get_next_message(xorriso->libjte_handle);
   if(msg == NULL)
 break;
   sprintf(xorriso->info_text, "%1.4095s", msg);
   free(msg);
   msg= NULL;
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
 }
 sprintf(xorriso->info_text, "Experienced libjte failure with: -jigdo %s %s",
         aspect, arg);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
 return(0);

#else /* Xorriso_with_libjtE */

 sprintf(xorriso->info_text,
         "Jigdo Template Extraction was not enabled at compile time");
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
 return(0);

#endif /* ! Xorriso_with_libjtE */

}


int Xorriso_list_extras_result(struct XorrisO *xorriso, char *mode,
                               char *what, int flag)
{
 if(mode[0] != 0 && strcmp(mode, "all") != 0) {
   if(strcmp(mode, what) != 0 &&
      (mode[0] != '-' || strcmp(mode + 1, what) != 0)) 
     return(2);
 }
 Xorriso_result(xorriso, 0);
 return(1);
}


int Xorriso_list_extras(struct XorrisO *xorriso, char *mode, int flag)
{
 int ret;

 if(strcmp(mode, "codes") == 0) {
   sprintf(xorriso->result_line,
        "List of xorriso extra feature codes. Usable with or without dash.\n");
   Xorriso_result(xorriso, 0);
   sprintf(xorriso->result_line, "Local ACL    : -acl\n");
   Xorriso_result(xorriso, 0);
   sprintf(xorriso->result_line, "Local xattr  : -xattr\n");
   Xorriso_result(xorriso, 0);
   sprintf(xorriso->result_line, "Jigdo files  : -jigdo\n");
   Xorriso_result(xorriso, 0);
   sprintf(xorriso->result_line, "zisofs       : -zisofs\n");
   Xorriso_result(xorriso, 0);
   sprintf(xorriso->result_line, "Ext. filters : -external_filter\n");
   Xorriso_result(xorriso, 0);
   sprintf(xorriso->result_line, "DVD obs 64 kB: -dvd_obs\n");
   Xorriso_result(xorriso, 0);
   sprintf(xorriso->result_line, "Readline     : -use_readline\n");
   Xorriso_result(xorriso, 0);
   return(1);
 }
 sprintf(xorriso->result_line,
         "List of xorriso extra features. yes = enabled , no = disabled\n");
 Xorriso_list_extras_result(xorriso, mode, "list_extras", 0);

 ret= iso_local_attr_support(3);
 sprintf(xorriso->result_line, "Local ACL    : %s\n", ret & 1 ? "yes" : "no");
 Xorriso_list_extras_result(xorriso, mode, "acl", 0);
 sprintf(xorriso->result_line, "Local xattr  : %s\n", ret & 2 ? "yes" : "no");
 Xorriso_list_extras_result(xorriso, mode, "xattr", 0);
 
 sprintf(xorriso->result_line, "Jigdo files  : %s\n",
#ifdef Xorriso_with_libjtE
         "yes");
#else
         "no");
#endif
 Xorriso_list_extras_result(xorriso, mode, "jigdo", 0);

 ret= iso_file_add_zisofs_filter(NULL, 4);
 sprintf(xorriso->result_line, "zisofs       : %s\n", ret == 2 ? "yes" : "no");
 Xorriso_list_extras_result(xorriso, mode, "zisofs", 0);

 sprintf(xorriso->result_line, "Ext. filters : %s\n",
#ifdef Xorriso_allow_external_filterS
#ifdef Xorriso_allow_extf_suiD
         "yes , setuid allowed");
#else
         "yes , setuid banned");
#endif
#else
         "no");
#endif
 Xorriso_list_extras_result(xorriso, mode, "external_filter", 0);

 sprintf(xorriso->result_line, "DVD obs 64 kB: %s\n",
#ifdef Xorriso_dvd_obs_default_64K
         "yes");
#else
         "no");
#endif
 Xorriso_list_extras_result(xorriso, mode, "dvd_obs", 0);

 sprintf(xorriso->result_line, "Readline     : %s\n",
#ifdef Xorriso_with_readlinE
         "yes");
#else
         "no");
#endif
 Xorriso_list_extras_result(xorriso, mode, "use_readline", 0);

 return(1);
}


/* @param flag bit0= set num_tiles to default value
               bit1= set tile_blocks to default value
*/
int Xorriso_set_data_cache(struct XorrisO *xorriso, void *o,
                           int num_tiles, int tile_blocks, int flag)
{
 int ret, tiles, blocks, set_flag;
 struct isoburn_read_opts *ropts;

 ropts= (struct isoburn_read_opts *) o;
 if(flag & (1 | 2)) {
   isoburn_ropt_get_data_cache(ropts, &tiles, &blocks, &set_flag, 1);
   if(flag & 1)
     num_tiles= tiles;
   if(flag & 2)
     tile_blocks= blocks;
 }
 ret= isoburn_ropt_set_data_cache(ropts, num_tiles, tile_blocks, 0);
 return(ret);
}


