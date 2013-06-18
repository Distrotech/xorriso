
/* os.h
   Operating system specific libburn definitions and declarations.
   The macros defined here are used by libburn modules in order to
   avoid own system dependent case distinctions.
   Copyright (C) 2009 Thomas Schmitt <scdbackup@gmx.net>, provided under GPLv2+
*/

#ifndef BURN_OS_H_INCLUDED
#define BURN_OS_H_INCLUDED 1

/*
   Operating system case distinction
*/


#ifdef Libburn_use_sg_dummY


/* --------- Any other system. With dummy MMC transport sg-dummy.c --------- */
#include "os-dummy.h"


#else
#ifdef Libburn_use_libcdiO


/* -------------------------- X/Open with GNU libcdio ---------------------- */
#include "os-libcdio.h"


#else
#ifdef __FreeBSD__


/* ----------------------------- FreeBSD with CAM -------------------------- */
#include "os-freebsd.h"


#else
#ifdef __FreeBSD_kernel__


/* ----------------------- FreeBSD with CAM under Debian ------------------- */
#include "os-freebsd.h"


#else
#ifdef __linux


/* ------- Linux kernels 2.4 and 2.6 with GNU/Linux SCSI Generic (sg) ------ */
#include "os-linux.h"


#else
#ifdef __sun


/* ------- Solaris (e.g. SunOS 5.11) with uscsi ------ */
#include "os-solaris.h"


#else


/* --------- Any other system. With dummy MMC transport sg-dummy.c --------- */
#include "os-dummy.h"


#endif /* ! __sun*/
#endif /* ! __linux */
#endif /* ! __FreeBSD__kernel__ */
#endif /* ! __FreeBSD__ */
#endif /* ! Libburn_use_libcdiO */
#endif /* ! Libburn_use_sg_dummY */


#endif /* ! BURN_OS_H_INCLUDED */

