/*
 cleanup.c , Copyright 2006 - 2011 Thomas Schmitt <scdbackup@gmx.net>

 A signal handler which cleans up an application and exits.

 Provided under GPLv2+ license within GPL projects, BSD license elsewise.
*/

/*
 cc -g -o cleanup -DCleanup_standalonE cleanup.c
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <signal.h>
typedef void (*sighandler_t)(int);


#include "cleanup.h"


#ifndef Cleanup_has_no_libburn_os_H


#include "../libburn/os.h"

/* see os.h for name of particular os-*.h where this is defined */
static int signal_list[]=        { BURN_OS_SIGNAL_MACRO_LIST , -1};
static char *signal_name_list[]= { BURN_OS_SIGNAL_NAME_LIST , "@"};
static int signal_list_count=      BURN_OS_SIGNAL_COUNT;
static int non_signal_list[]=    { BURN_OS_NON_SIGNAL_MACRO_LIST, -1};
static int non_signal_list_count=  BURN_OS_NON_SIGNAL_COUNT;


#else /* ! Cleanup_has_no_libburn_os_H */


/* Outdated. GNU/Linux only.
   For backward compatibility with pre-libburn-0.2.3 */

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


#endif /* Cleanup_has_no_libburn_os_H */



/* run time dynamic part */
static char cleanup_msg[4096]= {""};
static int cleanup_exiting= 0;
static int cleanup_has_reported= -1234567890;

static void *cleanup_app_handle= NULL;
static Cleanup_app_handler_T cleanup_app_handler= NULL;
static int cleanup_perform_app_handler_first= 0;


static int Cleanup_handler_exit(int exit_value, int signum, int flag)
{
 int ret;

 if(cleanup_msg[0]!=0 && cleanup_has_reported!=signum) {
   fprintf(stderr,"\n%s\n",cleanup_msg);
   cleanup_has_reported= signum;
 }
 if(cleanup_perform_app_handler_first)
   if(cleanup_app_handler!=NULL) {
     ret= (*cleanup_app_handler)(cleanup_app_handle,signum,0);
     if(ret==2 || ret==-2)
       return(2);
   }
 if(cleanup_exiting) {
   fprintf(stderr,"cleanup: ABORT : repeat by pid=%.f, signum=%d\n",
           (double) getpid(), signum);
   return(0);
 }
 cleanup_exiting= 1;
 alarm(0);
 if(!cleanup_perform_app_handler_first)
   if(cleanup_app_handler!=NULL) {
     ret= (*cleanup_app_handler)(cleanup_app_handle,signum,0); 
     if(ret==2 || ret==-2)
       return(2);
   }
 exit(exit_value);
}  


static void Cleanup_handler_generic(int signum)
{
 int i;

 sprintf(cleanup_msg,"UNIX-SIGNAL caught:  %d  errno= %d",signum,errno);
 for(i= 0; i<signal_list_count; i++) 
   if(signum==signal_list[i]) {
     sprintf(cleanup_msg,"UNIX-SIGNAL:  %s  errno= %d",
             signal_name_list[i],errno);
 break;
   }
 Cleanup_handler_exit(1,signum,0);
}


int Cleanup_set_handlers(void *handle, Cleanup_app_handler_T handler, int flag)
/*
 bit0= set to default handlers
 bit1= set to ignore
 bit2= set cleanup_perform_app_handler_first
 bit3= set SIGABRT to handler (makes sense with bits 0 or 1)
*/
{
 int i,j,max_sig= -1,min_sig= 0x7fffffff;
 sighandler_t sig_handler;

 cleanup_msg[0]= 0;
 cleanup_app_handle= handle;
 cleanup_app_handler= handler;

 /* <<< make cleanup_exiting thread safe to get rid of this */
 if(flag&4)
   cleanup_perform_app_handler_first= 1;


 if(flag&1)
   sig_handler= SIG_DFL;
 else if(flag&2)
   sig_handler= SIG_IGN;
 else
   sig_handler= Cleanup_handler_generic;
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
       signal(i,Cleanup_handler_generic);
     else
       signal(i,sig_handler);
   }
 }
 return(1);
}


#ifdef Cleanup_standalonE

struct Demo_apP {
 char *msg;
};


int Demo_app_handler(struct Demo_apP *demoapp, int signum, int flag)
{
 printf("Handling exit of demo application on signal %d. msg=\"%s\"\n",
        signum,demoapp->msg);
 return(1);
}


main()
{
 struct Demo_apP demoapp;

 demoapp.msg= "Good Bye";
 Cleanup_set_handlers(&demoapp,(Cleanup_app_handler_T) Demo_app_handler,0);

 if(1) { /* change to 0 in order to wait for external signals */
   char *cpt= NULL, c= ' ';
   printf("Intentionally provoking SIGSEGV ...\n");
   c= *cpt;
   printf("Strange: The system ignored a SIGSEGV: c= %u\n", (unsigned int) c);
 } else {
   printf("killme: %d\n",getpid());
   sleep(3600);
 }

 Cleanup_set_handlers(NULL,NULL,1);
 exit(0);
}

#endif /* Cleanup_standalonE */
