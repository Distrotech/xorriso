/* Declaration of functions and data types used for SHA256 sum computing
   library functions.
   Copyright (C) 2007 Free Software Foundation, Inc.

   Copied here from the GNU C Library version 2.7 on the 10 May 2009
   by Steve McIntyre <93sam@debian.org>. This code was under GPL v2.1
   in glibc, and that license gives us the option to use and
   distribute the code under the terms of the GPL v2 instead. I'm
   taking that option.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef _SHA256_H
#define _SHA256_H 1

#include <limits.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif

#include <stdio.h>


/* Structure to save state of computation between the single steps.  */
struct sha256_ctx
{
  uint32_t H[8];

  uint32_t total[2];
  uint32_t buflen;
  char buffer[128] __attribute__ ((__aligned__ (__alignof__ (uint32_t))));
};

/* Initialize structure containing state of computation.
   (FIPS 180-2: 5.3.2)  */
extern void sha256_init_ctx (struct sha256_ctx *ctx);

/* Starting with the result of former calls of this function (or the
   initialization function update the context for the next LEN bytes
   starting at BUFFER.
   It is NOT required that LEN is a multiple of 64.  */
extern void sha256_process_bytes (const void *buffer, size_t len,
				    struct sha256_ctx *ctx);

/* Process the remaining bytes in the buffer and put result from CTX
   in first 32 bytes following RESBUF.

   IMPORTANT: On some systems it is required that RESBUF is correctly
   aligned for a 32 bits value.  */
extern void *sha256_finish_ctx (struct sha256_ctx *ctx, void *resbuf);

#endif /* sha256.h */
