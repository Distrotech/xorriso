
/* os-solaris.h
   Operating system specific libburn definitions and declarations. Included
   by os.h in case of compilation for
           Solaris based systems, e.g. SunOS 5.11
           with Solaris uscsi MMC transport adapter sg-solaris.c

   Copyright (C) 2010 Thomas Schmitt <scdbackup@gmx.net>, provided under GPLv2+
*/


/** List of all signals which shall be caught by signal handlers and trigger
    a graceful abort of libburn. (See man signal.h)
*/
/* Once as system defined macros */
#define BURN_OS_SIGNAL_MACRO_LIST \
 SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGABRT, \
 SIGFPE, SIGSEGV, SIGPIPE, SIGALRM, SIGTERM, \
 SIGUSR1, SIGUSR2, SIGXCPU, SIGTSTP, SIGTTIN, \
 SIGTTOU

/* Once as text 1:1 list of strings for messages and interpreters */
#define BURN_OS_SIGNAL_NAME_LIST \
 "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGABRT", \
 "SIGFPE", "SIGSEGV", "SIGPIPE", "SIGALRM", "SIGTERM", \
 "SIGUSR1", "SIGUSR2", "SIGXCPU", "SIGTSTP", "SIGTTIN", \
 "SIGTTOU"

/* The number of above list items */
#define BURN_OS_SIGNAL_COUNT 16

/** To list all signals which shall surely not be caught */
#define BURN_OS_NON_SIGNAL_MACRO_LIST \
SIGKILL, SIGCHLD, SIGSTOP, SIGURG, SIGWINCH

/* The number of above list items */
#define BURN_OS_NON_SIGNAL_COUNT 5


/* The maximum size for a (SCSI) i/o transaction */
/* Important : MUST be at least 32768 ! */
/* My Blu-ray burner LG GGW-H20 writes junk if stream recording is combined
   with buffer size 32 kB. So stream recording is allowed only with size 64k.
*/
#define BURN_OS_TRANSPORT_BUFFER_SIZE 65536


/* >>> */

/* To hold the position of the most recently delivered address from
   device enumeration.
*/
struct burn_drive_enumerator_struct {
	void *dir;
};

#define BURN_OS_DEFINE_DRIVE_ENUMERATOR_T \
typedef struct burn_drive_enumerator_struct burn_drive_enumerator_t;


/* The list of operating system dependent elements in struct burn_drive.
   Usually they are initialized in  sg-*.c:enumerate_common().
*/
#define BURN_OS_TRANSPORT_DRIVE_ELEMENTS \
	int fd;

