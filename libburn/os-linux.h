
/* os-linux.h
   Operating system specific libburn definitions and declarations. Included
   by os.h in case of compilation for
           Linux kernels 2.4 and 2.6, GNU/Linux SCSI Generic (sg)

   Copyright (C) 2006 - 2010 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/


/** List of all signals which shall be caught by signal handlers and trigger
    a graceful abort of libburn. (See man 7 signal.)
*/
/* Once as system defined macros */
#define BURN_OS_SIGNAL_MACRO_LIST \
 SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGABRT, \
 SIGFPE, SIGSEGV, SIGPIPE, SIGALRM, SIGTERM, \
 SIGUSR1, SIGUSR2, SIGXCPU, SIGTSTP, SIGTTIN, \
 SIGTTOU, \
 SIGBUS, SIGPOLL, SIGPROF, SIGSYS, SIGTRAP, \
 SIGVTALRM, SIGXCPU, SIGXFSZ

/* Once as text 1:1 list of strings for messages and interpreters */
#define BURN_OS_SIGNAL_NAME_LIST \
 "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGABRT", \
 "SIGFPE", "SIGSEGV", "SIGPIPE", "SIGALRM", "SIGTERM", \
 "SIGUSR1", "SIGUSR2", "SIGXCPU", "SIGTSTP", "SIGTTIN", \
 "SIGTTOU", \
 "SIGBUS", "SIGPOLL", "SIGPROF", "SIGSYS", "SIGTRAP", \
 "SIGVTALRM", "SIGXCPU", "SIGXFSZ"

/* The number of above list items */
#define BURN_OS_SIGNAL_COUNT 24

/** To list all signals which shall surely not be caught */
#define BURN_OS_NON_SIGNAL_MACRO_LIST \
SIGKILL, SIGCHLD, SIGSTOP, SIGURG, SIGWINCH

/* The number of above list items */
#define BURN_OS_NON_SIGNAL_COUNT 5


/* The maximum size for a (SCSI) i/o transaction */
/* Important : MUST be at least 32768 ! */
/* ts A70523 : >32k seems not good with kernel 2.4 USB drivers and audio
   #define BURN_OS_TRANSPORT_BUFFER_SIZE 32768
*/
/* ts A80414 : curbed in write.c CD media to Libburn_cd_obS = 32 kiB
               re-enlarged transport to 64 kiB for BD-RE experiments
*/
#define BURN_OS_TRANSPORT_BUFFER_SIZE 65536


/* To hold the position of the most recently delivered address from
   device enumeration.
*/
struct burn_drive_enumerator_struct {
	int pos;
	int info_count;
	char **info_list;
};

#define BURN_OS_DEFINE_DRIVE_ENUMERATOR_T \
typedef struct burn_drive_enumerator_struct burn_drive_enumerator_t;


/* Parameters for sibling list. See sibling_fds, sibling_fnames */
#define BURN_OS_SG_MAX_SIBLINGS 5
#define BURN_OS_SG_MAX_NAMELEN 16

/* The list of operating system dependent elements in struct burn_drive.
   Usually they are initialized in  sg-*.c:enumerate_common().
*/
#define BURN_OS_TRANSPORT_DRIVE_ELEMENTS \
int fd; \
 \
/* ts A60926 : trying to lock against growisofs /dev/srN, /dev/scdN */ \
int sibling_count; \
int sibling_fds[BURN_OS_SG_MAX_SIBLINGS]; \
/* ts A70409 : DDLP */ \
char sibling_fnames[BURN_OS_SG_MAX_SIBLINGS][BURN_OS_SG_MAX_NAMELEN];

