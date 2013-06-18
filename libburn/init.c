/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2012 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif


#include <unistd.h>

/* ts A61007 */
/* #include <a ssert.h> */

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

/* ts A70928 : init.h is for others, not for init .c
#include "init.h"
*/


#include "sg.h"
#include "error.h"
#include "libburn.h"
#include "drive.h"
#include "transport.h"

/* ts A60825 : The storage location for back_hacks.h variables. */
#define BURN_BACK_HACKS_INIT 1
#include "back_hacks.h"

/* ts A60924 : a new message handling facility */
#include "libdax_msgs.h"
struct libdax_msgs *libdax_messenger= NULL;


int burn_running = 0;

/* ts A60813 : GNU/Linux: whether to use O_EXCL on open() of device files
   ts B00212 : FreeBSD:   whether to use flock(LOCK_EX) after open()
*/
int burn_sg_open_o_excl = 1;

/* ts A70403 : GNU/Linux: wether to use fcntl(,F_SETLK,)
                      after open() of device files  */
int burn_sg_fcntl_f_setlk = 1;

/* ts A70314 : GNU/Linux: what device family to use :
    0= default family
    1= sr
    2= scd
   (3= st)
    4= sg
*/
int burn_sg_use_family = 0;

/* O_NONBLOCK was hardcoded in enumerate_ata() which i hardly use.
   For enumerate_sg() it seems ok.
   So it should stay default mode until enumerate_ata() without O_NONBLOCK
   has been thoroughly tested. */
int burn_sg_open_o_nonblock = 1;

/* wether to take a busy drive as an error */
/* Caution: this is implemented by a rough hack and eventually leads
	    to unconditional abort of the process  */
int burn_sg_open_abort_busy = 0;


/* The message returned from sg_id_string() and/or sg_initialize()
*/
static char sg_initialize_msg[1024] = {""};


/* ts A61002 */

#include "cleanup.h"

/* Parameters for builtin abort handler */
static char abort_message_prefix[81] = {"libburn : "};
static pid_t abort_control_pid= 0;
static pthread_t abort_control_thread;
volatile int burn_global_abort_level= 0;
int burn_global_abort_signum= 0;
void *burn_global_signal_handle = NULL;
burn_abort_handler_t burn_global_signal_handler = NULL;
int burn_builtin_signal_action = 0;            /* burn_set_signal_handling() */
volatile int burn_builtin_triggered_action = 0;       /*  burn_is_aborting() */


/* ts A70223 : wether implemented untested profiles are supported */
int burn_support_untested_profiles = 0;

/* ts A91111 :
   whether to log SCSI commands (to be implemented in sg-*.c)
   bit0= log in /tmp/libburn_sg_command_log
   bit1= log to stderr
   bit2= flush every line
*/
int burn_sg_log_scsi = 0;


/* ts B10312 :
   Whether to map random-access readonly files to drive role 4.
   Else it is role 2 overwriteable drive
*/
int burn_drive_role_4_allowed = 0;


/* ts A60925 : ticket 74 */
/** Create the messenger object for libburn. */
int burn_msgs_initialize(void)
{
	int ret;

	if(libdax_messenger == NULL) {
		ret = libdax_msgs_new(&libdax_messenger,0);
		if (ret <= 0)
			return 0;
	}
	libdax_msgs_set_severities(libdax_messenger, LIBDAX_MSGS_SEV_NEVER,
				   LIBDAX_MSGS_SEV_FATAL, "libburn: ", 0);
	return 1;
}

/* ts A60924 : ticket 74 : Added use of global libdax_messenger */
int burn_initialize(void)
{
	int ret;

	if (burn_running)
		return 1;
	burn_support_untested_profiles = 0;
	ret = burn_msgs_initialize();
	if (ret <= 0)
		return 0;
	ret = sg_initialize(sg_initialize_msg, 0);
	if (ret <= 0) {
                libdax_msgs_submit(libdax_messenger, -1,
                        0x00020175,
                        LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
                        sg_initialize_msg, 0, 0);
		return 0;
	}
	burn_running = 1;
	return 1;
}

void burn_finish(void)
{
	/* ts A61007 : assume no messageing system */
	/* a ssert(burn_running); */
	if (!burn_running)
		return;

	/* ts A61007 */
	/* burn_wait_all(); */
	if (!burn_drives_are_clear(0)) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00020107,
			LIBDAX_MSGS_SEV_WARNING, LIBDAX_MSGS_PRIO_HIGH,
			"A drive is still busy on shutdown of library", 0, 0);
		usleep(1000001);
		burn_abort(4440, burn_abort_pacifier, abort_message_prefix);
	}

	/* ts A60904 : ticket 62, contribution by elmom : name addon "_all" */
	burn_drive_free_all();

	/* ts A60924 : ticket 74 */
	libdax_msgs_destroy(&libdax_messenger,0);

	sg_shutdown(0);

	burn_drive_clear_whitelist();

	burn_running = 0;
}


/* ts A91226 */
/** API function. See libburn.h */
char *burn_scsi_transport_id(int flag)
{
	if (!burn_running)
		sg_id_string(sg_initialize_msg, 0);
	return sg_initialize_msg;
}


/* ts A60813 */
/** API function. See libburn.h */
void burn_preset_device_open(int exclusive, int blocking, int abort_on_busy)
{
	/* ts A61007 */
	/* a ssert(burn_running); */
	if (!burn_running)
		return;	
	burn_sg_open_o_excl = exclusive & 3;
	burn_sg_fcntl_f_setlk = !!(exclusive & 32);
	burn_sg_use_family = (exclusive >> 2) & 7;
	burn_sg_open_o_nonblock = !blocking;
	burn_sg_open_abort_busy = !!abort_on_busy;
}


/* ts A60924 : ticket 74 */
/** Control queueing and stderr printing of messages from libburn.
    Severity may be one of "NEVER", "FATAL", "SORRY", "WARNING", "HINT",
    "NOTE", "UPDATE", "DEBUG", "ALL".
    @param queue_severity Gives the minimum limit for messages to be queued.
                          Default: "NEVER". If you queue messages then you
                          must consume them by burn_msgs_obtain().
    @param print_severity Does the same for messages to be printed directly
                          to stderr.
    @param print_id       A text prefix to be printed before the message.
    @return               >0 for success, <=0 for error

*/
int burn_msgs_set_severities(char *queue_severity,
                             char *print_severity, char *print_id)
{
	int ret, queue_sevno, print_sevno;

	ret = libdax_msgs__text_to_sev(queue_severity, &queue_sevno, 0);
	if (ret <= 0)
		return 0;
	ret = libdax_msgs__text_to_sev(print_severity, &print_sevno, 0);
	if (ret <= 0)
		return 0;
	ret = libdax_msgs_set_severities(libdax_messenger, queue_sevno,
					 print_sevno, print_id, 0);
	if (ret <= 0)
		return 0;
	return 1;
}


/* ts A60924 : ticket 74 */
#define BURM_MSGS_MESSAGE_LEN 4096

/** Obtain the oldest pending libburn message from the queue which has at
    least the given minimum_severity. This message and any older message of
    lower severity will get discarded from the queue and is then lost forever.
    Severity may be one of "NEVER", "FATAL", "SORRY", "WARNING", "HINT",
    "NOTE", "UPDATE", "DEBUG", "ALL". To call with minimum_severity "NEVER"
    will discard the whole queue.
    @param error_code Will become a unique error code as liste in 
                      libburn/libdax_msgs.h
    @param msg_text   Must provide at least BURM_MSGS_MESSAGE_LEN bytes.
    @param os_errno   Will become the eventual errno related to the message
    @param severity   Will become the severity related to the message and
                      should provide at least 80 bytes.
    @return 1 if a matching item was found, 0 if not, <0 for severe errors
*/
int burn_msgs_obtain(char *minimum_severity,
                     int *error_code, char msg_text[], int *os_errno,
                     char severity[])
{
	int ret, minimum_sevno, sevno, priority;
	char *textpt, *sev_name;
	struct libdax_msgs_item *item = NULL;

	ret = libdax_msgs__text_to_sev(minimum_severity, &minimum_sevno, 0);
	if (ret <= 0)
		return 0;
	if (libdax_messenger == NULL)
		return 0;
	ret = libdax_msgs_obtain(libdax_messenger, &item, minimum_sevno,
				LIBDAX_MSGS_PRIO_ZERO, 0);
	if (ret <= 0)
		goto ex;
	ret = libdax_msgs_item_get_msg(item, error_code, &textpt, os_errno, 0);
	if (ret <= 0)
		goto ex;
	strncpy(msg_text, textpt, BURM_MSGS_MESSAGE_LEN-1);
	if(strlen(textpt) >= BURM_MSGS_MESSAGE_LEN)
		msg_text[BURM_MSGS_MESSAGE_LEN-1] = 0;

	severity[0]= 0;
	ret = libdax_msgs_item_get_rank(item, &sevno, &priority, 0);
	if(ret <= 0)
		goto ex;
	ret = libdax_msgs__sev_to_text(sevno, &sev_name, 0);
	if(ret <= 0)
		goto ex;
	strcpy(severity,sev_name);

	ret = 1;
ex:
	libdax_msgs_destroy_item(libdax_messenger, &item, 0);
	return ret;
}


/* ts A70922 : API */
int burn_msgs_submit(int error_code, char msg_text[], int os_errno,
			char severity[], struct burn_drive *d)
{
	int ret, sevno, global_index = -1;

	ret = libdax_msgs__text_to_sev(severity, &sevno, 0);
	if (ret <= 0)
		sevno = LIBDAX_MSGS_SEV_ALL;
	if (error_code <= 0) {
		switch(sevno) {
		       case LIBDAX_MSGS_SEV_ABORT:   error_code = 0x00040000;
		break; case LIBDAX_MSGS_SEV_FATAL:   error_code = 0x00040001;
		break; case LIBDAX_MSGS_SEV_SORRY:   error_code = 0x00040002;
		break; case LIBDAX_MSGS_SEV_WARNING: error_code = 0x00040003;
		break; case LIBDAX_MSGS_SEV_HINT:    error_code = 0x00040004;
		break; case LIBDAX_MSGS_SEV_NOTE:    error_code = 0x00040005;
		break; case LIBDAX_MSGS_SEV_UPDATE:  error_code = 0x00040006;
		break; case LIBDAX_MSGS_SEV_DEBUG:   error_code = 0x00040007;
		break; default:                      error_code = 0x00040008;
		}
	}
	if (d != NULL)
		global_index = d->global_index;
	ret = libdax_msgs_submit(libdax_messenger, global_index, error_code,
			sevno, LIBDAX_MSGS_PRIO_HIGH, msg_text, os_errno, 0);
	return ret;
}


/* ts A71016 API */
int burn_text_to_sev(char *severity_name, int *sevno, int flag)
{
	int ret;

	ret = libdax_msgs__text_to_sev(severity_name, sevno, 0);
	return ret;
}


/* ts A80202 API */
int burn_sev_to_text(int severity_number, char **severity_name, int flag)
{
	int ret;

	ret = libdax_msgs__sev_to_text(severity_number, severity_name, 0);
	return ret;
}


/* ts B21214 API */
char *burn_list_sev_texts(int flag)
{
 char *sev_list;

 libdax_msgs__sev_to_text(0, &sev_list, 1);
 return sev_list;
}


/* ts B00224 */
char *burn_util_thread_id(pid_t pid, pthread_t tid, char text[80])
{
	int i, l;

	sprintf(text, "[%lu,", (unsigned long int) getpid());
	l= strlen(text);
	for(i= 0; i < ((int) sizeof(pthread_t)) && 2 * i < 80 - l - 3; i++)
		sprintf(text + l + 2 * i,
			 "%2.2X", ((unsigned char *) &tid)[i]);

	sprintf(text + l + 2 * i, "]");
	return text;
}


/* ts B20122 */
/* @param value 0=return rather than exit(value)
*/
int burn_abort_exit(int value)
{
	burn_abort(4440, burn_abort_pacifier, abort_message_prefix);
	fprintf(stderr,
	"\n%sABORT : Program done. Even if you do not see a shell prompt.\n\n",
		abort_message_prefix);
	if (value)
		exit(value);
	burn_global_abort_level = -2;
	return(1);
}


int burn_builtin_abort_handler(void *handle, int signum, int flag)
{

#define Libburn_new_thread_signal_handleR 1
/*
#define Libburn_signal_handler_verbouS 1
*/

	int ret;
	struct burn_drive *d;

#ifdef Libburn_signal_handler_verbouS
	char text[80];

	fprintf(stderr, "libburn_ABORT: in = %s\n",
		burn_util_thread_id(getpid(), pthread_self(), text));
	fprintf(stderr, "libburn_ABORT: ctrl = %s\n",
		burn_util_thread_id(abort_control_pid, abort_control_thread,
					text));
	if (burn_global_signal_handler == burn_builtin_abort_handler)
		fprintf(stderr, "libburn_ABORT: signal action = %d\n",
				burn_builtin_signal_action);

	/* >>> find writing drives and report their tid
	fprintf(stderr, "libburn_ABORT: wrt = %s\n",
		burn_util_thread_id(0, burn_write_thread_id, text));
	fprintf(stderr, "libburn_ABORT: sig= %d\n", signum);
	*/
#endif

	burn_builtin_triggered_action = burn_builtin_signal_action;
	burn_global_abort_level = -1;

	if (burn_builtin_signal_action > 1) {
		Cleanup_set_handlers(NULL, NULL, 2);
		if (burn_builtin_signal_action == 4)
			return -2;
		fprintf(stderr,"%sABORT : Trying to shut down busy drives\n",
			abort_message_prefix);
		fprintf(stderr,
		 "%sABORT : Wait the normal burning time before any kill -9\n",
		 	abort_message_prefix);
		burn_abort_5(0, burn_abort_pacifier, abort_message_prefix,
				0, 1);
		libdax_msgs_submit(libdax_messenger, -1, 0x00020177,
			LIBDAX_MSGS_SEV_ABORT, LIBDAX_MSGS_PRIO_HIGH,
			"Urged drive worker threads to do emergency halt",
			0, 0);
		return -2;
	}


	/* ---- old deprecated stuck-in-abort-handler loop ---- */

	/* ts A70928:
	Must be quick. Allowed to coincide with other thread and to share
	the increment with that one. It must not decrease, though, and
	yield at least 1 if any thread calls this function.
	*/
	burn_global_abort_level++;
	burn_global_abort_signum= signum;

	if(getpid() != abort_control_pid) {

#ifdef Libburn_new_thread_signal_handleR

		ret = burn_drive_find_by_thread_pid(&d, getpid(),
							pthread_self());
		if (ret > 0 && d->busy == BURN_DRIVE_WRITING) {
					/* This is an active writer thread */

#ifdef Libburn_signal_handler_verbouS
			fprintf(stderr, "libburn_ABORT: pid %d found drive busy with writing, (level= %d)\n", (int) getpid(), burn_global_abort_level);
#endif

			d->sync_cache(d);

			/* >>> perform a more qualified end of burn process */;

			d->busy = BURN_DRIVE_IDLE;

			if (burn_global_abort_level > 0) {
				/* control process did not show up yet */
#ifdef Libburn_signal_handler_verbouS
					fprintf(stderr, "libburn_ABORT: pid %d sending signum %d to pid %d\n", (int) getpid(), (int) signum, (int) abort_control_pid);
#endif
					kill(abort_control_pid, signum);
			}

#ifdef Libburn_signal_handler_verbouS
					fprintf(stderr, "libburn_ABORT: pid %d signum %d returning -2\n", (int) getpid(), (int) signum);
#endif

			return -2;
		} else {
			usleep(1000000); /* calm down */
			return -2;
		}

#else
		usleep(1000000); /* calm down */
		return -2;
#endif /* ! Libburn_new_thread_signal_handleR */

	}
	burn_global_abort_level = -1;
	Cleanup_set_handlers(NULL, NULL, 2);

	fprintf(stderr,"%sABORT : Trying to shut down drive and library\n",
		abort_message_prefix);
	fprintf(stderr,
		"%sABORT : Wait the normal burning time before any kill -9\n",
		abort_message_prefix);
	close(0); /* somehow stdin as input blocks abort until EOF */

	burn_abort_exit(0);
	return (1);
}


/* ts A61002 : API */
void burn_set_signal_handling(void *handle, burn_abort_handler_t handler,
				int mode)
{

/*
	fprintf(stderr, "libburn_experimental: burn_set_signal_handling, handler==%lx  mode=%d\n", (unsigned long) handler, mode);
*/

	if(handler == NULL) {
		handler = burn_builtin_abort_handler;
/*
		if ((mode & ~4) == 0)
			fprintf(stderr, "libburn_experimental: activated burn_builtin_abort_handler() with handle '%s'\n",(handle==NULL ? "libburn : " : (char *) handle));
*/

	}
	strcpy(abort_message_prefix, "libburn : ");
	abort_message_prefix[0] = 0;
	if(handle != NULL && handler == burn_builtin_abort_handler)
		strncpy(abort_message_prefix, (char *) handle,
			sizeof(abort_message_prefix)-1);
	abort_message_prefix[sizeof(abort_message_prefix)-1] = 0;
	abort_control_pid = getpid();
	abort_control_thread = pthread_self();
	burn_builtin_signal_action = (mode >> 4) & 15;
	if((mode & 11) != 0)
		burn_builtin_signal_action = 0;
	if(burn_builtin_signal_action > 1)
		burn_builtin_triggered_action = 0;
	if(burn_builtin_signal_action == 0)
		burn_builtin_signal_action = 1;
	Cleanup_set_handlers(handle, (Cleanup_app_handler_T) handler,
				 (mode & 15) | 4);
	burn_global_signal_handle = handle;
	burn_global_signal_handler = handler;
}


/* ts B00304 : API */
int burn_is_aborting(int flag)
{
	return burn_builtin_triggered_action;
}


/* ts B00225 */
/* @return 0= no abort action 2 pending , 1= not control thread
*/
int burn_init_catch_on_abort(int flag)
{
	if (burn_builtin_triggered_action != 2)
		return 0;
	if (abort_control_pid != getpid() ||
		abort_control_thread != pthread_self())
		return 1;
	burn_abort(4440, burn_abort_pacifier, abort_message_prefix);
	fprintf(stderr,
	"\n%sABORT : Program done. Even if you do not see a shell prompt.\n\n",
		abort_message_prefix);
	exit(1);
}


/* B20122 */
/* Temporarily disable builtin actions 0,1,2 to avoid that burn_abort()
   waits for its own thread to end grabbing.
*/
int burn_grab_prepare_sig_action(int *signal_action_mem, int flag)
{
	*signal_action_mem = -1;
	if (burn_global_signal_handler == burn_builtin_abort_handler &&
	    burn_builtin_signal_action >= 0 &&
	    burn_builtin_signal_action <= 2) {
		*signal_action_mem = burn_builtin_signal_action;
		burn_builtin_signal_action = 3;
	}
	return 1;
}


/* B20122 */
/* Re-enable builtin actions 0,1,2 and perform delayed signal reactions
*/
int burn_grab_restore_sig_action(int signal_action_mem, int flag)
{
	if (signal_action_mem >= 0)
		burn_builtin_signal_action = signal_action_mem;
	if (burn_is_aborting(0) && signal_action_mem >= 0) {
		if (signal_action_mem == 0 || signal_action_mem == 1) {
			burn_abort_exit(1); /* Never comes back */
		} else if (signal_action_mem == 2) {
			burn_builtin_triggered_action = signal_action_mem;
		}
	}
	return 1;
}


/* ts A70223 : API */
void burn_allow_untested_profiles(int yes)
{
	burn_support_untested_profiles = !!yes;
}


/* ts A70915 : API */
int burn_set_messenger(void *messenger)
{
	struct libdax_msgs *pt;

	if (libdax_msgs_refer(&pt, messenger, 0) <= 0)
		return 0;
	libdax_msgs_destroy(&libdax_messenger, 0);
	libdax_messenger = (struct libdax_msgs *) pt;
	return 1;
}


/* ts A91111 API */
void burn_set_scsi_logging(int flag)
{
	burn_sg_log_scsi = flag & 7;
}


/* ts B10312 API */
void burn_allow_drive_role_4(int allowed)
{
	burn_drive_role_4_allowed = (allowed & 0xf);
}


/* ts B10606 */
void *burn_alloc_mem(size_t size, size_t count, int flag)
{
	void *pt;

	pt = calloc(count, size);
	if(pt == NULL)
		libdax_msgs_submit(libdax_messenger, -1, 0x00000003,
				LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				"Out of virtual memory", 0, 0);
	return pt;
}

