/**
 Copyright (c) 2006 Thomas Schmitt <scdbackup@gmx.net>
 Provided under GPL version 2 or later.

 This file bundles variables which disable changes in libburn which are
 not yet completely accepted.

 The use of these variables is *strongly discouraged* unless you have sincere
 reason and are willing to share your gained knowledge with the libburn
 developers.

 Do *not silently rely* on these variables with your application. Tell us
 that you needed one or more of them. They are subject to removal as soon
 as consense has been found about correctness of the change they revoke.

 Value 0 means that the new behavior is enabled. Any other value enables
 the described old time behavior.

 If you doubt one of the changes here broke your application, then do
 *in your application*, *not here* :

 -  #include "libburn/back_hacks.h" like you include "libburn/libburn.h"

 -  Set the libburn_back_hack_* variable of your choice to 1. 
    In your app. Not here.

 -  Then start and use libburn as usual. Watch out for results.

 -  If you believe to have detected a flaw in our change, come forward
    and report it to the libburn developers. Thanks in advance. :)

*/

/** Do not define this macro in your application. Only libburn/init.c is
    entitled to set it.
*/ 
#ifdef BURN_BACK_HACKS_INIT


/** Corresponds to http://libburn.pykix.org/ticket/42 
    Reinstates the old ban not to blank appendable CD-RW. We see no reason
    for this ban yet. It appears unusual. But maybe it patches a bug.
*/
int libburn_back_hack_42= 0;


#else /* BURN_BACK_HACKS_INIT */

/* Note: no application programmer info beyond this point */


extern int libburn_back_hack_42;

#endif /* ! BURN_BACK_HACKS_INIT */


