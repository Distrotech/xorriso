/*
 cleanup.c , Copyright 2006 Thomas Schmitt <scdbackup@gmx.net>

 A signal handler which cleans up an application and exits.

 Provided under GPLv2+ within GPL projects, BSD license elsewise.
*/

#ifndef Cleanup_includeD
#define Cleanup_includeD 1


/** Layout of an application provided cleanup function using an application
    provided handle as first argument and the signal number as second
    argument. The third argument is a flag bit field with no defined bits yet.
    If the handler returns 2 or -2 then it has delegated exit() to some other
    instance and the Cleanup handler shall return rather than exit.
*/
typedef int (*Cleanup_app_handler_T)(void *, int, int);


/** Establish exiting signal handlers on (hopefully) all signals that are
    not ignored by default or non-catchable.
    @param handle Opaque object which knows how to cleanup application
    @param handler Function which uses handle to perform application cleanup
    @param flag Control Bitfield
           bit0= reset to default signal handling
*/
int Cleanup_set_handlers(void *handle, Cleanup_app_handler_T handler,
                         int flag);


#endif /* ! Cleanup_includeD */

