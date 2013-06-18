/* sha1.c - SHA1 hash function
 * Copyright (C) 1998, 2001, 2002, 2003, 2008 Free Software Foundation, Inc.
 *
 * This file was part of Libgcrypt.
 *
 * Libgcrypt is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * Libgcrypt is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* Borrowed and adapted slightly for use in JTE by Steve McIntyre
 * <steve@einval.com> October 2010 */

/* Structure to save state of computation between the single steps.  */
typedef struct 
{
  uint32_t           h0,h1,h2,h3,h4;
  uint32_t           nblocks;
  unsigned char buf[64];
  int           count;
} SHA1_CONTEXT;

/* Initialize structure containing state of computation. */
void sha1_init_ctx  (void *context);

/* Starting with the result of former calls of this function (or the
   initialization function update the context for the next inlen bytes
   starting at inbuf_arg.
   It is NOT required that inlen is a multiple of 64.  */
void sha1_write     (void *context, const void *inbuf_arg, size_t inlen);

/* Process the remaining bytes in the buffer and finish the checksum
   calculation. */
void sha1_finish_ctx(void *context);

/* Read the checksum result - 20 bytes. The result is always in little
   endian byte order, so that a byte-wise output yields to the wanted
   ASCII representation of the message digest. */
unsigned char *sha1_read(void *context);
