
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of functions which perform the
   fundamental operations of the XorrisO object.
*/


#ifndef Xorriso_pvt_base_obj_includeD
#define Xorriso_pvt_base_obj_includeD yes


#ifdef NIX
/* <<< */
unsigned long Xorriso_get_di_counteR= 0;
#endif /* NIX */

struct XorrisO;


int Xorriso_destroy_re(struct XorrisO *m, int flag);

int Xorriso__get_signal_behavior(int flag);


#endif /* ! Xorriso_pvt_base_obj_includeD */

