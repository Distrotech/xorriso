
/* xorriso - Command line oriented batch and dialog tool which creates, loads,
   manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2013 Thomas Schmitt, <scdbackup@gmx.net>

   Initial code of this program was derived from program src/askme.c out
   of scdbackup-0.8.8, Copyright 2007 Thomas Schmitt, BSD-License.

   Provided under GPL version 2 or later, with the announcement that this
   might get changed in future. I would prefer BSD or LGPL as soon as the
   license situation of the library code allows that.
   (This announcement affects only future releases of xorriso and it will
    always be possible to derive a GPLv2+ from the future license.)

   There is a derived package "GNU xorriso" under GPLv3+ which combines the
   libburnia libraries and program xorriso to a statically linked binary.


   Overview of xorriso architecture:

   libburn provides the ability to read and write data. 

   libisofs interprets and manipulates ISO 9660 directory trees. It generates
   the output stream which is handed over to libburn.

   libisoburn by its lower level API encapsulates the connectivity issues
   between libburn and libisofs. This API also enables multi-session emulation
   on overwritable media and random access file objects.

   xorriso is the higher level API of libisoburn which allows to operate all
   three libraries by a unified set of commands. 
   <libisoburn/xorriso.h> exposes the public functions.
   Among these functions are direct equivalents of the xorriso interpreter
   commands. There are also functions for fundamental management and for
   handling event messages.

   This file  xorriso_main.c  runs the xorriso API as batch and dialog program.

   One should not mix the use of the xorriso API with the use of the lower
   level APIs of libburn, libisofs, libisoburn.
   
   --------------------------------------------------------------------------
   The following overview is relevant for development but not for usage of
   xorriso. An application programmer should read xorriso.h and man xorriso
   resp. info xorriso, rather than diving into its source code.
   For examples see the functions main() and check_compatibility() below.
   --------------------------------------------------------------------------

   The xorriso source is divided in two groups:

   A set of source modules interacts with the lower level library APIs:

   base_obj.[ch]    fundamental operations of the XorrisO object
   lib_mgt.[ch]     manages the relation between xorriso and the libraries
   drive_mgt.[ch]   operates on drives and media
   iso_img.[ch]     operates on ISO images and their global properties
   iso_tree.[ch]    access nodes of the libisofs tree model
   iso_manip.[ch]   manipulates the libisofs tree model
   sort_cmp.[ch]    sorts and compare tree nodes
   write_run.[ch]   functions to write sessions
   read_run.[ch]    functions to read data from ISO image
   filters.[ch]     operates on data filter objects
   xorrisoburn.h    declarations needed by the non-library modules

   Another set is independent of the lower level APIs:

   parse_exec.c     deals with parsing and interpretation of command input
   sfile.c          functions around files and strings
   aux_objects.c    various helper classes
   misc_funct.c     miscellaneous helper functions
   findjob.c        performs tree searches in libisofs or in POSIX filesystem
   check_media.c    perform verifying runs on media resp. images
   text_io.c        text i/o functions
   match.c          functions for pattern matching
   emulators.c      emulators for mkisofs and cdrecord
   disk_ops.c       actions on onjects of disk filesystems
   cmp_update.c     compare or update files between disk filesystem and
                    ISO filesystem
   opts_a_c.c       options -a* to -c*
   opts_d_h.c       options -d* to -h*
   opts_i_o.c       options -i* to -o*
   opts_p_z.c       options -p* to -z*

   xorriso_private.h contains the definition of struct Xorriso and for
   convenience includes the .h files of the non-library group.

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

#include <locale.h>


/* xorriso_main.c includes the internal copy of the API definition */
/* The official xorriso options API is defined in <libisoburn/xorriso.h> */
#include "xorriso.h"


/* The minimum version of libisoburn xorriso API to be used with this
   version of xorriso.
*/
#define Xorriso_req_majoR  1
#define Xorriso_req_minoR  3
#define Xorriso_req_micrO  0


static void yell_xorriso()
{
 fprintf(stderr,
         "%sxorriso %d.%d.%d%s : RockRidge filesystem manipulator, libburnia project.\n\n",
#ifdef Xorriso_GNU_xorrisO
        "GNU ",
#else
        "",
#endif
        Xorriso_header_version_majoR, Xorriso_header_version_minoR,
        Xorriso_header_version_micrO, Xorriso_program_patch_leveL);
}


/* Check whether build configuration and runtime linking are consistent.
*/
static void check_compatibility()
{
 int lib_major, lib_minor, lib_micro;

/* First an ugly compile time check for header version compatibility.
   If everthing matches, then no C code is produced. In case of mismatch,
   intentionally faulty C code will be inserted.
*/
/* The minimum requirement of xorriso towards the libisoburn header
   at compile time is defined above 
     Xorriso_req_majoR
     Xorriso_req_minoR
     Xorriso_req_micrO
   It gets compared against the version macros in xorriso.h :
     Xorriso_header_version_majoR
     Xorriso_header_version_minoR
     Xorriso_header_version_micrO
   If the header is too old then the following code shall cause failure of
   cdrskin compilation rather than to allow production of a program with
   unpredictable bugs or memory corruption.
   The compiler messages supposed to appear in this case are:
      error: 'XORRISO_MISCONFIGURATION' undeclared (first use in this function)
      error: 'INTENTIONAL_ABORT_OF_COMPILATION__HEADERFILE_xorriso_dot_h_TOO_OLD__SEE_xorriso_main_dot_c' undeclared (first use in this function)
      error: 'XORRISO_MISCONFIGURATION_' undeclared (first use in this function)
*/
/* The indendation is an advise of man gcc to help old compilers ignoring */
 #if Xorriso_req_majoR > Xorriso_header_version_majoR
 #define Xorriso_dot_h_too_olD 1
 #endif
 #if Xorriso_req_majoR == Xorriso_header_version_majoR && Xorriso_req_minoR > Xorriso_header_version_minoR
 #define Xorriso_dot_h_too_olD 1
 #endif
 #if Xorriso_req_minoR == Xorriso_header_version_minoR && Xorriso_req_micrO > Xorriso_header_version_micrO
 #define Xorriso_dot_h_too_olD 1
 #endif

#ifdef Xorriso_dot_h_too_olD
XORRISO_MISCONFIGURATION = 0;
INTENTIONAL_ABORT_OF_COMPILATION__HEADERFILE_xorriso_dot_h_TOO_OLD__SEE_xorriso_main_dot_c = 0;
XORRISO_MISCONFIGURATION_ = 0;
#endif

/* End of ugly compile time test (scroll up for explanation) */


 /* Needed are at least 44 bits in signed type off_t .
    This is a popular mistake in configuration or compilation.
 */
 if(sizeof(off_t) < 6) {
   yell_xorriso();
   fprintf(stderr,
    "xorriso : FATAL : Compile time misconfiguration. sizeof(off_t) too small.\n\n");
   exit(4);
 }

 /* Check whether the linked xorriso code is young enough.
 */
 if(! Xorriso__is_compatible(Xorriso_header_version_majoR,
                             Xorriso_header_version_minoR,
                             Xorriso_header_version_micrO, 0)) {
   yell_xorriso();
   Xorriso__version(&lib_major, &lib_minor, &lib_micro);
   fprintf(stderr,
           "xorriso : FATAL : libisoburn/xorriso runtime version mismatch. Found %d.%d.%d, need %d.%d.%d\n\n",
           lib_major, lib_minor, lib_micro,
           Xorriso_header_version_majoR, Xorriso_header_version_minoR,
           Xorriso_header_version_micrO);
   exit(4);
 }

}


int main(int argc, char **argv)
{
 int ret, i;
 struct XorrisO *xorriso= NULL;
 char **orig_argv= NULL;

 check_compatibility(); /* might exit() */

 if(argc < 2) {
   yell_xorriso();
   fprintf(stderr,"usage : %s [options]\n", argv[0]);
   fprintf(stderr, "        More is told by option -help\n");
   exit(2);
 }
 setlocale(LC_CTYPE, "");
 ret= Xorriso_new(&xorriso, argv[0], 0);
 if(ret <= 0) {
   fprintf(stderr,"Creation of XorrisO object failed. (not enough memory ?)\n");
   exit(3);
 }

 /* The prescan of arguments performs actions which have to happen before
    the normal processing of startup files and arguments.
    Among them are -help and -prog_help which end the program without
    yelling its name and version.
 */
 ret= Xorriso_prescan_args(xorriso,argc,argv,0);
 if(ret == 0)
   goto end_successfully;
 /* Put out program name and version to stderr only if not done already now */
 yell_xorriso();
 if(ret < 0)
   exit(5);
 /* After having yelled xorriso, prescan again for unknown arguments */
 ret= Xorriso_prescan_args(xorriso, argc, argv, 2);
 if(ret < 0)
   exit(5);

 /* The following command interpreters are allowed only after this
    initialization.
 */
 ret= Xorriso_startup_libraries(xorriso, 0);
 if(ret <= 0)
   {ret= 4; goto emergency_exit;}
 Xorriso_process_msg_queues(xorriso, 0);

 /* Interpret startup files */
 ret= Xorriso_read_rc(xorriso, 0);
 if(ret == 3)
   goto end_successfully;
 if(ret <= 0)
   {ret= 5; goto emergency_exit;}

 /* Interpret program arguments */
 orig_argv= argv;
 ret= Xorriso_program_arg_bsl(xorriso, argc, &argv, 0); 
 if(ret <= 0)
   {ret= 5; goto emergency_exit;}
 i= 1;
 ret= Xorriso_interpreter(xorriso, argc, argv, &i, 2);
 if(ret == 3)
   goto end_successfully;
 if(ret <= 0)
   {ret= 5; goto emergency_exit;}

 /* Enter dialog mode if it has been activated meanwhile */
 ret= Xorriso_dialog(xorriso, 0);
 if(ret <= 0)
   {ret= 6; goto emergency_exit;}

end_successfully:; /* normal shutdown, including eventual -commit */
 Xorriso_stop_msg_watcher(xorriso, 1);
 Xorriso_process_msg_queues(xorriso, 0);
 if(Xorriso_change_is_pending(xorriso, 1))
   Xorriso_option_end(xorriso, 2);
 Xorriso_process_msg_queues(xorriso, 0);
 ret= Xorriso_make_return_value(xorriso, 0);
 Xorriso_process_errfile(xorriso, 0, "xorriso end", 0, 1);
 Xorriso_destroy(&xorriso, 1);
 if(orig_argv != argv && orig_argv != NULL) {
   for(i= 0; i < argc; i++)
     if(argv[i] != NULL)
       free(argv[i]);
   free(argv);
 }
 exit(ret);

emergency_exit:;
 if(xorriso != NULL) { /* minimal shutdown */
   Xorriso_process_msg_queues(xorriso, 0);
   Xorriso_destroy(&xorriso, 1);
 }
 exit(ret);
}


