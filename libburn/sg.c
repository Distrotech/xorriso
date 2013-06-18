
/* sg.c
   Switcher for operating system dependent transport level modules of libburn.
   Copyright (C) 2009 - 2010 Thomas Schmitt <scdbackup@gmx.net>, 
   provided under GPLv2+
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#undef HAVE_CONFIG_H
#endif


#ifdef Libburn_use_sg_dummY

#include "sg-dummy.c"

#else
#ifdef Libburn_use_libcdiO

#include "sg-libcdio.c"

#else
#ifdef __FreeBSD__

#ifdef Libburn_use_sg_freebsd_porT
#include "sg-freebsd-port.c"
#else
#include "sg-freebsd.c"
#endif

#else
#ifdef __FreeBSD_kernel__

#ifdef Libburn_use_sg_freebsd_porT
#include "sg-freebsd-port.c"
#else
#include "sg-freebsd.c"
#endif

#else
#ifdef __linux

#include "sg-linux.c"

#else
#ifdef __sun

#include "sg-solaris.c"

#else

/* The dummy adapter formally fulfills the expectations of libburn towards
   its SCSI command transport. It will show no drives and perform no SCSI
   commands.
   libburn will then be restricted to using its stdio pseudo drives.
*/
static int intentional_compiler_warning(void)
{
 int INTENTIONAL_COMPILER_WARNING_;
 int Cannot_recognize_GNU_Linux_nor_FreeBSD_nor_Solaris_;
 int Have_to_use_dummy_MMC_transport_adapter_;
 int This_libburn_will_not_be_able_to_operate_on_real_CD_drives;
 int Have_to_use_dummy_MMC_transport_adapter;
 int Cannot_recognize_GNU_Linux_nor_FreeBSD_nor_Solaris;
 int INTENTIONAL_COMPILER_WARNING;

 return(0);
}

#include "sg-dummy.c"

#endif /* ! __sun */
#endif /* ! __linux */
#endif /* ! __FreeBSD_kernel__ */
#endif /* ! __FreeBSD__ */
#endif /* ! Libburn_use_libcdiO */
#endif /* ! Libburn_use_sg_dummY */

