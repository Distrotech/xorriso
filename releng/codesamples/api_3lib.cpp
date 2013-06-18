// Just to ensure we are C++-clean. This should not spit too much noise

/* Copyright 2011 George Danchev <danchev@spnet.net>
 * Released into the public domain
 */

#if __WORDSIZE == 32
#define _LARGEFILE_SOURCE 1
#define _FILE_OFFSET_BITS 64
#endif

#include <iostream>
#include <inttypes.h>

//extern "C" {
#include "libburn/libburn.h"
#include "libisofs/libisofs.h"
// using namespace burn;
// (this was needed to before rev.4062 of libisoburn)
#include "libisoburn/libisoburn.h"
//}

int main() {
   int major=-1, minor=-1, micro=-1;
   isoburn_version(&major, &minor, &micro);
   if (major<0 || minor<0 || micro<0)
     return -1;
   std::cout 
          << " major:" << major
          << " minor:" << minor
          << " micro:" << micro
   ;
   return 0;
}
