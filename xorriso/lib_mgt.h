
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of functions which manage the relation
   between xorriso and the libraries: libburn, libisofs, and libisoburn.
*/


#ifndef Xorriso_pvt_x_includeD
#define Xorriso_pvt_x_includeD yes


#ifndef Xorriso_standalonE

/* The library which does the ISO 9660 / RockRidge manipulations */
#include <libisofs/libisofs.h>

/* The library which does MMC optical drive operations */
#include <libburn/libburn.h>

/* The library which enhances overwriteable media with ISO 9660 multi-session
   capabilities via the method invented by Andy Polyakov for growisofs */
#include <libisoburn/libisoburn.h>

/* The official xorriso options API. "No shortcuts" */
#include "xorriso.h"

/* The inner description of XorrisO */
#include "xorriso_private.h"

/* The inner isofs- and burn-library interface */
#include "xorrisoburn.h"

#else /* ! Xorriso_standalonE */

#include "../libisofs/libisofs.h"
#include "../libburn/libburn.h"
#include "../libisoburn/libisoburn.h"
#include "xorriso.h"
#include "xorriso_private.h"
#include "xorrisoburn.h"

#endif /* Xorriso_standalonE */


int Xorriso_abort(struct XorrisO *xorriso, int flag);


/* @param flag bit0= suppress messages below UPDATE
               bit1= suppress messages below FAILURE
*/
int Xorriso_set_image_severities(struct XorrisO *xorriso, int flag);

int Xorriso__sev_to_text(int severity, char **severity_name,
                         int flag);

/* @param flag bit0= report libisofs error text
               bit1= victim is disk_path
               bit2= do not inquire libisofs, report msg_text and min_severity
*/
int Xorriso_report_iso_error(struct XorrisO *xorriso, char *victim,
                        int iso_error_code, char msg_text[], int os_errno,
                        char min_severity[], int flag);

int Xorriso_process_msg_queues(struct XorrisO *xorriso, int flag);


#endif /* ! Xorriso_pvt_x_includeD */

