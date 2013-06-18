
/* libdax_sigmgr
   Signal management facility of libdax and libburn.
   Copyright (C) 2006 Thomas Schmitt <scdbackup@gmx.net>, provided under GPLv2+
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>

#include <signal.h>
typedef void (*sighandler_t)(int);


/* Only this single source module is entitled to do this */ 
#define LIBDAX_SIGMGR_H_INTERNAL 1

/* All participants in the abort system must do this */
#include "libdax_sigmgr.h"


/* Signals to be caught */
static int signal_list[]= {
 SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGABRT,
 SIGFPE, SIGSEGV, SIGPIPE, SIGALRM, SIGTERM,
 SIGUSR1, SIGUSR2, SIGXCPU, SIGTSTP, SIGTTIN,
 SIGTTOU,
 SIGBUS, SIGPOLL, SIGPROF, SIGSYS, SIGTRAP,
 SIGVTALRM, SIGXCPU, SIGXFSZ, -1
};
static char *signal_name_list[]= {
 "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGABRT",
 "SIGFPE", "SIGSEGV", "SIGPIPE", "SIGALRM", "SIGTERM",
 "SIGUSR1", "SIGUSR2", "SIGXCPU", "SIGTSTP", "SIGTTIN",
 "SIGTTOU",
 "SIGBUS", "SIGPOLL", "SIGPROF", "SIGSYS", "SIGTRAP",
 "SIGVTALRM", "SIGXCPU", "SIGXFSZ", "@"
};
static int signal_list_count= 24;

/* Signals not to be caught */
static int non_signal_list[]= {
  SIGKILL, SIGCHLD, SIGSTOP, SIGURG, SIGWINCH, -1
};
static int non_signal_list_count= 5;


/* Points to the globally activated signal handling manager */
struct libdax_sigmgr *libdax_sigmgr_activated= NULL;


/* ------------------------------ libdax_sigmgr ---------------------------- */


int libdax_sigmgr_new(struct libdax_sigmgr **m, pid_t control_pid, int flag)
{
 struct libdax_sigmgr *o;

 (*m)= o= (struct libdax_sigmgr *) malloc(sizeof(struct libdax_sigmgr));
 if(o==NULL)
   return(-1);
 o->control_pid= control_pid;
 o->api_chain= NULL;
 o->msg[0]= 0;
 o->exiting= 0;
 return(1);
}


int libdax_sigmgr_destroy(struct libdax_sigmgr **m, int flag)
{
 struct libdax_sigmgr *o;

 o= *m;
 if(o==NULL)
   return(0);
 free((char *) o);
 *m= NULL;
 return(1);
}


static void libdax_sigmgr_central_handler(int signum)
{
 int i, ret;
 struct libdax_sigmgr *o;

 o= libdax_sigmgr_activated;
 if(o==NULL)
   return;
 if(o->control_pid != getpid())
   return;
 if(o->exiting) {
/*
   fprintf(stderr,"libdax_sigmgr: ABORT : repeat by pid=%d, signum=%d\n",
           getpid(),signum);
*/
   return;
 }
 o->exiting= 1;
 sprintf(o->msg,"UNIX-SIGNAL caught:  %d  errno= %d",signum,errno);
 for(i= 0; i<signal_list_count; i++)
   if(signum==signal_list[i]) {
     sprintf(o->msg,"UNIX-SIGNAL:  %s  errno= %d",
             signal_name_list[i],errno);
 break;
   }
/*
 fprintf(stderr,"libdax_sigmgr: ABORT : %s\n", o->msg);
*/
 alarm(0);
 ret= libdax_api_handle_abort(o->api_chain, o->msg, 0);
 if(ret == -2)
   return;
 if(ret<0)
   ret= 1;
 exit(ret);
}


/* Set global signal handling.
   @param o The signal manager to respond to signals in mode 0
   @param api_chain One of the libdax_api objects in the chain to handle
   @param flag Bitfield for control purposes (unused yet, submit 0)
               bit0-2= activation mode
                       0 set to use of item handlers
                       1 set to default handlers
                       2 set to ignore
               bit3= set SIGABRT to handler (makes sense with bits 0 or 1)
*/
int libdax_sigmgr_activate(struct libdax_sigmgr *o, 
                           struct libdax_api *api_chain,
                           int flag)
{
 int mode, i, j, max_sig= -1, min_sig= 0x7fffffff;
 sighandler_t sig_handler;

 mode= flag&7;
 o->api_chain= api_chain;
 libdax_sigmgr_activated= o;
 if(mode==0)
   sig_handler= libdax_sigmgr_central_handler;
 else if(mode==1)
   sig_handler= SIG_DFL;
 else if(mode==2)
   sig_handler= SIG_IGN;
 else
   return(-1);
 /* set all signal numbers between the lowest and highest in the list
    except those in the non-signal list */
 for(i= 0; i<signal_list_count; i++) {
   if(signal_list[i]>max_sig)
     max_sig= signal_list[i];
   if(signal_list[i]<min_sig)
     min_sig= signal_list[i];
 }
 for(i= min_sig; i<=max_sig; i++) {
   for(j= 0; j<non_signal_list_count; j++)
     if(i==non_signal_list[j])
   break;
   if(j>=non_signal_list_count) {
     if(i==SIGABRT && (flag&8))
       signal(i,libdax_sigmgr_central_handler);
     else
       signal(i,sig_handler);
   }
 }
 return(1);
}


