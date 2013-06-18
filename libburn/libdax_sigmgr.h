
/* libdax_sigmgr
   Signal management facility of libdax and libburn.
   Copyright (C) 2006 Thomas Schmitt <scdbackup@gmx.net>, provided under GPLv2+
*/


/*
  *Never* set this macro outside libdax_sigmgr.c !
  The entrails of this facility are not to be seen by
  the other library components or the applications.
*/
#ifdef LIBDAX_SIGMGR_H_INTERNAL

/** Layout of an application provided cleanup function using an application
    provided handle as first argument and the signal number as second
    argument. The third argument is a flag bit field with no defined bits yet.
    If the handler returns -2 then it has delegated exit() to some other
    instance and the Cleanup handler shall return rather than exit.
*/
typedef int (*libdax_sigmgr_handler_t)(void *, int, int);


struct libdax_sigmgr {

 /* The thread to be handled by this handler */
 pid_t control_pid;

 /* The libdax api objects to be handled */
 struct libdax_api *api_chain;

 char msg[4096];
 int exiting;

};


#endif /* LIBDAX_SIGMGR_H_INTERNAL */


#ifndef LIBDAX_SIGMGR_H_INCLUDED
#define LIBDAX_SIGMGR_H_INCLUDED 1


#ifndef LIBDAX_SIGMGR_H_INTERNAL

                          /* Public Opaque Handles */

/** A pointer to this is a opaque handle to a signal handling manager */
struct libdax_sigmgr;


#endif /* ! LIBDAX_SIGMGR_H_INTERNAL */


                            /* Public Macros */


                            /* Public Functions */



               /* Calls initiated from inside libdax/libburn */


     /* Calls from applications (to be forwarded by libdax/libburn) */



#ifdef LIDBAX_SIGMGR________________


-- place documentation text here ---


#endif /* LIDBAX_SIGMGR_________________ */



#ifdef LIBDAX_SIGMGR_H_INTERNAL

                             /* Internal Functions */

#endif /* LIBDAX_SIGMGR_H_INTERNAL */


#endif /* ! LIBDAX_SIGMGR_H_INCLUDED */

