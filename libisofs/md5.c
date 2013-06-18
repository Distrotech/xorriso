/*
 * Copyright (c) 2009 - 2013 Thomas Schmitt
 *
 * This file is part of the libisofs project; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */


#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "writer.h"
#include "messages.h"
#include "ecma119.h"
#include "image.h"
#include "util.h"

#include "md5.h"


/* This code is derived from RFC 1321 and implements computation of the
    "RSA Data Security, Inc. MD5 Message-Digest Algorithm" */

#define Libisofs_md5_S11 7
#define Libisofs_md5_S12 12
#define Libisofs_md5_S13 17
#define Libisofs_md5_S14 22
#define Libisofs_md5_S21 5
#define Libisofs_md5_S22 9
#define Libisofs_md5_S23 14
#define Libisofs_md5_S24 20
#define Libisofs_md5_S31 4
#define Libisofs_md5_S32 11
#define Libisofs_md5_S33 16
#define Libisofs_md5_S34 23
#define Libisofs_md5_S41 6
#define Libisofs_md5_S42 10
#define Libisofs_md5_S43 15
#define Libisofs_md5_S44 21


/* F, G, H and I are basic MD5 functions.
 */
#define Libisofs_md5_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define Libisofs_md5_G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define Libisofs_md5_H(x, y, z) ((x) ^ (y) ^ (z))
#define Libisofs_md5_I(x, y, z) ((y) ^ ((x) | (~z)))

/* ROTATE_LEFT rotates x left n bits.
 */
#define Libisofs_md5_ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))

/* FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4.
Rotation is separate from addition to prevent recomputation.
 */
#define Libisofs_md5_FF(a, b, c, d, x, s, ac) { \
 (a) += Libisofs_md5_F ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = Libisofs_md5_ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
  }
#define Libisofs_md5_GG(a, b, c, d, x, s, ac) { \
 (a) += Libisofs_md5_G ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = Libisofs_md5_ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
  }
#define Libisofs_md5_HH(a, b, c, d, x, s, ac) { \
 (a) += Libisofs_md5_H ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = Libisofs_md5_ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
  }
#define Libisofs_md5_II(a, b, c, d, x, s, ac) { \
 (a) += Libisofs_md5_I ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = Libisofs_md5_ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
  }


/* MD5 context. */
struct _libisofs_md5_ctx {
  uint32_t state[4];                                   /* state (ABCD) */
  uint32_t count[2];        /* number of bits, modulo 2^64 (lsb first) */
  unsigned char buffer[64];                         /* input buffer */
};

typedef struct _libisofs_md5_ctx libisofs_md5_ctx;


/* MD5 basic transformation. Transforms state based on block.
 */
static int md5__transform (uint32_t state[4], unsigned char block[64])
{
 uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];
 unsigned int i, j;

 for (i = 0, j = 0; j < 64; i++, j += 4)
   x[i] = ((uint32_t)block[j]) | (((uint32_t)block[j+1]) << 8) |
    (((uint32_t)block[j+2]) << 16) | (((uint32_t)block[j+3]) << 24);

  /* Round 1 */
  Libisofs_md5_FF (a, b, c, d, x[ 0], Libisofs_md5_S11, 0xd76aa478); /* 1 */
  Libisofs_md5_FF (d, a, b, c, x[ 1], Libisofs_md5_S12, 0xe8c7b756); /* 2 */
  Libisofs_md5_FF (c, d, a, b, x[ 2], Libisofs_md5_S13, 0x242070db); /* 3 */
  Libisofs_md5_FF (b, c, d, a, x[ 3], Libisofs_md5_S14, 0xc1bdceee); /* 4 */
  Libisofs_md5_FF (a, b, c, d, x[ 4], Libisofs_md5_S11, 0xf57c0faf); /* 5 */
  Libisofs_md5_FF (d, a, b, c, x[ 5], Libisofs_md5_S12, 0x4787c62a); /* 6 */
  Libisofs_md5_FF (c, d, a, b, x[ 6], Libisofs_md5_S13, 0xa8304613); /* 7 */
  Libisofs_md5_FF (b, c, d, a, x[ 7], Libisofs_md5_S14, 0xfd469501); /* 8 */
  Libisofs_md5_FF (a, b, c, d, x[ 8], Libisofs_md5_S11, 0x698098d8); /* 9 */
  Libisofs_md5_FF (d, a, b, c, x[ 9], Libisofs_md5_S12, 0x8b44f7af); /* 10 */
  Libisofs_md5_FF (c, d, a, b, x[10], Libisofs_md5_S13, 0xffff5bb1); /* 11 */
  Libisofs_md5_FF (b, c, d, a, x[11], Libisofs_md5_S14, 0x895cd7be); /* 12 */
  Libisofs_md5_FF (a, b, c, d, x[12], Libisofs_md5_S11, 0x6b901122); /* 13 */
  Libisofs_md5_FF (d, a, b, c, x[13], Libisofs_md5_S12, 0xfd987193); /* 14 */
  Libisofs_md5_FF (c, d, a, b, x[14], Libisofs_md5_S13, 0xa679438e); /* 15 */
  Libisofs_md5_FF (b, c, d, a, x[15], Libisofs_md5_S14, 0x49b40821); /* 16 */

  /* Round 2 */
  Libisofs_md5_GG (a, b, c, d, x[ 1], Libisofs_md5_S21, 0xf61e2562); /* 17 */
  Libisofs_md5_GG (d, a, b, c, x[ 6], Libisofs_md5_S22, 0xc040b340); /* 18 */
  Libisofs_md5_GG (c, d, a, b, x[11], Libisofs_md5_S23, 0x265e5a51); /* 19 */
  Libisofs_md5_GG (b, c, d, a, x[ 0], Libisofs_md5_S24, 0xe9b6c7aa); /* 20 */
  Libisofs_md5_GG (a, b, c, d, x[ 5], Libisofs_md5_S21, 0xd62f105d); /* 21 */
  Libisofs_md5_GG (d, a, b, c, x[10], Libisofs_md5_S22,  0x2441453); /* 22 */
  Libisofs_md5_GG (c, d, a, b, x[15], Libisofs_md5_S23, 0xd8a1e681); /* 23 */
  Libisofs_md5_GG (b, c, d, a, x[ 4], Libisofs_md5_S24, 0xe7d3fbc8); /* 24 */
  Libisofs_md5_GG (a, b, c, d, x[ 9], Libisofs_md5_S21, 0x21e1cde6); /* 25 */
  Libisofs_md5_GG (d, a, b, c, x[14], Libisofs_md5_S22, 0xc33707d6); /* 26 */
  Libisofs_md5_GG (c, d, a, b, x[ 3], Libisofs_md5_S23, 0xf4d50d87); /* 27 */
  Libisofs_md5_GG (b, c, d, a, x[ 8], Libisofs_md5_S24, 0x455a14ed); /* 28 */
  Libisofs_md5_GG (a, b, c, d, x[13], Libisofs_md5_S21, 0xa9e3e905); /* 29 */
  Libisofs_md5_GG (d, a, b, c, x[ 2], Libisofs_md5_S22, 0xfcefa3f8); /* 30 */
  Libisofs_md5_GG (c, d, a, b, x[ 7], Libisofs_md5_S23, 0x676f02d9); /* 31 */
  Libisofs_md5_GG (b, c, d, a, x[12], Libisofs_md5_S24, 0x8d2a4c8a); /* 32 */

  /* Round 3 */
  Libisofs_md5_HH (a, b, c, d, x[ 5], Libisofs_md5_S31, 0xfffa3942); /* 33 */
  Libisofs_md5_HH (d, a, b, c, x[ 8], Libisofs_md5_S32, 0x8771f681); /* 34 */
  Libisofs_md5_HH (c, d, a, b, x[11], Libisofs_md5_S33, 0x6d9d6122); /* 35 */
  Libisofs_md5_HH (b, c, d, a, x[14], Libisofs_md5_S34, 0xfde5380c); /* 36 */
  Libisofs_md5_HH (a, b, c, d, x[ 1], Libisofs_md5_S31, 0xa4beea44); /* 37 */
  Libisofs_md5_HH (d, a, b, c, x[ 4], Libisofs_md5_S32, 0x4bdecfa9); /* 38 */
  Libisofs_md5_HH (c, d, a, b, x[ 7], Libisofs_md5_S33, 0xf6bb4b60); /* 39 */
  Libisofs_md5_HH (b, c, d, a, x[10], Libisofs_md5_S34, 0xbebfbc70); /* 40 */
  Libisofs_md5_HH (a, b, c, d, x[13], Libisofs_md5_S31, 0x289b7ec6); /* 41 */
  Libisofs_md5_HH (d, a, b, c, x[ 0], Libisofs_md5_S32, 0xeaa127fa); /* 42 */
  Libisofs_md5_HH (c, d, a, b, x[ 3], Libisofs_md5_S33, 0xd4ef3085); /* 43 */
  Libisofs_md5_HH (b, c, d, a, x[ 6], Libisofs_md5_S34,  0x4881d05); /* 44 */
  Libisofs_md5_HH (a, b, c, d, x[ 9], Libisofs_md5_S31, 0xd9d4d039); /* 45 */
  Libisofs_md5_HH (d, a, b, c, x[12], Libisofs_md5_S32, 0xe6db99e5); /* 46 */
  Libisofs_md5_HH (c, d, a, b, x[15], Libisofs_md5_S33, 0x1fa27cf8); /* 47 */
  Libisofs_md5_HH (b, c, d, a, x[ 2], Libisofs_md5_S34, 0xc4ac5665); /* 48 */

  /* Round 4 */
  Libisofs_md5_II (a, b, c, d, x[ 0], Libisofs_md5_S41, 0xf4292244); /* 49 */
  Libisofs_md5_II (d, a, b, c, x[ 7], Libisofs_md5_S42, 0x432aff97); /* 50 */
  Libisofs_md5_II (c, d, a, b, x[14], Libisofs_md5_S43, 0xab9423a7); /* 51 */
  Libisofs_md5_II (b, c, d, a, x[ 5], Libisofs_md5_S44, 0xfc93a039); /* 52 */
  Libisofs_md5_II (a, b, c, d, x[12], Libisofs_md5_S41, 0x655b59c3); /* 53 */
  Libisofs_md5_II (d, a, b, c, x[ 3], Libisofs_md5_S42, 0x8f0ccc92); /* 54 */
  Libisofs_md5_II (c, d, a, b, x[10], Libisofs_md5_S43, 0xffeff47d); /* 55 */
  Libisofs_md5_II (b, c, d, a, x[ 1], Libisofs_md5_S44, 0x85845dd1); /* 56 */
  Libisofs_md5_II (a, b, c, d, x[ 8], Libisofs_md5_S41, 0x6fa87e4f); /* 57 */
  Libisofs_md5_II (d, a, b, c, x[15], Libisofs_md5_S42, 0xfe2ce6e0); /* 58 */
  Libisofs_md5_II (c, d, a, b, x[ 6], Libisofs_md5_S43, 0xa3014314); /* 59 */
  Libisofs_md5_II (b, c, d, a, x[13], Libisofs_md5_S44, 0x4e0811a1); /* 60 */
  Libisofs_md5_II (a, b, c, d, x[ 4], Libisofs_md5_S41, 0xf7537e82); /* 61 */
  Libisofs_md5_II (d, a, b, c, x[11], Libisofs_md5_S42, 0xbd3af235); /* 62 */
  Libisofs_md5_II (c, d, a, b, x[ 2], Libisofs_md5_S43, 0x2ad7d2bb); /* 63 */
  Libisofs_md5_II (b, c, d, a, x[ 9], Libisofs_md5_S44, 0xeb86d391); /* 64 */

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;

  /* Zeroize sensitive information. */
  memset ((char *) x, 0, sizeof (x));
  return(1);
}


static int md5__encode(unsigned char *output, uint32_t *input,
                       unsigned int len)
{
  unsigned int i, j;

  for (i = 0, j = 0; j < len; i++, j += 4) {
    output[j] = (unsigned char)(input[i] & 0xff);
    output[j+1] = (unsigned char)((input[i] >> 8) & 0xff);
    output[j+2] = (unsigned char)((input[i] >> 16) & 0xff);
    output[j+3] = (unsigned char)((input[i] >> 24) & 0xff);
  }
  return(1);
}



static int md5_init(libisofs_md5_ctx *ctx, int flag)
{
 ctx->count[0] = ctx->count[1] = 0;
 /* Load magic initialization constants. */
 ctx->state[0] = 0x67452301;
 ctx->state[1] = 0xefcdab89;
 ctx->state[2] = 0x98badcfe;
 ctx->state[3] = 0x10325476;
 return(1);
}


/* MD5 block update operation. Continues an MD5 message-digest
  operation, processing another message block, and updating the
  context.
 */
static int md5_update(libisofs_md5_ctx *ctx, unsigned char *data,
                      int datalen, int flag)
{
 int i, index, partlen;

 /* Compute number of bytes mod 64 */
 index = ((ctx->count[0] >> 3) & 0x3F);
 /* Update number of bits */
 if ((ctx->count[0] += ((uint32_t) datalen << 3)) < 
     ((uint32_t) datalen << 3))
   ctx->count[1]++;
 ctx->count[1] += ((uint32_t) datalen >> 29);
 partlen = 64 - index;

 /* Transform as many times as possible. */
 if (datalen >= partlen) {
   memcpy((char *) &ctx->buffer[index], (char *) data, partlen);
   md5__transform(ctx->state, ctx->buffer);
   for (i = partlen; i + 63 < datalen; i += 64)
     md5__transform(ctx->state, &data[i]);
   index = 0;
 } else
   i = 0;

 /* Buffer remaining data */
 memcpy((char *) &ctx->buffer[index], (char *) &data[i],datalen-i);

 return(1);
}


static int md5_final(libisofs_md5_ctx *ctx, char result[16], int flag)
{
 unsigned char bits[8], *respt;
 unsigned int index, padlen;
 static unsigned char PADDING[64] = {
   0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
 };

 /* Save number of bits */
 md5__encode(bits, ctx->count, 8);
 /* Pad out to 56 mod 64.  */
 index = (unsigned int)((ctx->count[0] >> 3) & 0x3f);
 padlen = (index < 56) ? (56 - index) : (120 - index);
 md5_update(ctx, PADDING, padlen,0);
 /* Append length (before padding) */
 md5_update(ctx, bits, 8,0);
 /* Store state in result */
 respt= (unsigned char *) result;
 md5__encode(respt, ctx->state, 16);
 /* Zeroize sensitive information.  */
 memset ((char *) ctx, 0, sizeof (*ctx));
 return(1);
}

/** Compute a MD5 checksum from one or more calls of this function.
    The first call has to be made with flag bit0 == 1. It may already submit
    processing payload in data and datalen.
    The last call has to be made with bit15 set. Normally bit1 will be set
    too in order to receive the checksum before it gets disposed. 
    bit1 may only be set in the last call or together with bit2.
    The combination of bit1 and bit2 may be used to get an intermediate
    result without hampering an ongoing checksum computation.

    @param ctx      the checksum context which stores the state between calls.
                    It gets created with flag bit0 and disposed with bit15.
                    With flag bit0, *ctx has to be NULL or point to freeable
                    memory.
    @param data     the bytes to be checksummed
    @param datalen  the number of bytes in data
    @param result   returns the 16 bytes of checksum if called with bit1 set
    @param flag     bit0= allocate and init *ctx
                    bit1= transfer ctx to result
                    bit2= with bit 0 : clone new *ctx from data
                    bit15= free *ctx
*/
static
int libisofs_md5(void **ctx_in, char *data, int datalen,
                 char result[16], int flag)
/* *ctx has to be NULL or point to freeable memory */
/*
 bit0= allocate and init *ctx
 bit1= transfer ctx to result
 bit2= with bit 0 : clone new *ctx from data
 bit15= free *ctx
*/
{
 unsigned char *datapt;
 libisofs_md5_ctx **ctx;

 ctx= (libisofs_md5_ctx **) ctx_in;
 if(flag&1) {
   if(*ctx!=NULL)
     free((char *) *ctx);
   *ctx= calloc(1, sizeof(libisofs_md5_ctx));
   if(*ctx==NULL)
     return(-1);
   md5_init(*ctx,0);
   if(flag&4)
     memcpy((char *) *ctx,data,sizeof(libisofs_md5_ctx));
 }
 if(*ctx==NULL)
   return(0);
 if(datalen>0) {
   datapt= (unsigned char *) data;
   md5_update(*ctx, datapt, datalen, 0);
 }
 if(flag&2)
   md5_final(*ctx, result, 0);
 if(flag&(1<<15)) {
   free((char *) *ctx);
   *ctx= NULL;
 }
 return(1);
}


/* ----------------------------------------------------------------------- */

/* Public MD5 computing facility */

/* API */
int iso_md5_start(void **md5_context)
{
    int ret;

    ret = libisofs_md5(md5_context, NULL, 0, NULL, 1);
    if (ret <= 0)
        return ISO_OUT_OF_MEM;
    return 1;
}


/* API */
int iso_md5_compute(void *md5_context, char *data, int datalen)
{
    int ret;

    ret = libisofs_md5(&md5_context, data, datalen, NULL, 0);
    if (ret <= 0)
        return ISO_NULL_POINTER;
    return 1;
}


/* API */
int iso_md5_clone(void *old_md5_context, void **new_md5_context)
{
    int ret;

    ret = libisofs_md5(new_md5_context, old_md5_context, 0, NULL, 1 | 4);
    if (ret < 0)
        return ISO_OUT_OF_MEM;
    if (ret == 0)
        return ISO_NULL_POINTER;
    return 1;
}


/* API */
int iso_md5_end(void **md5_context, char result[16])
{
    int ret;

    ret = libisofs_md5(md5_context, NULL, 0, result, 2 | (1 << 15));
    if (ret <= 0)
        return ISO_NULL_POINTER;
    return 1;
}


/* API */
int iso_md5_match(char first_md5[16], char second_md5[16])
{
    int i;

    for (i= 0; i < 16; i++)
        if (first_md5[i] != second_md5[i])
            return 0;
    return 1;
}


/* ----------------------------------------------------------------------- */


/* Function to identify and manage md5sum indice of the old image.
 * data is supposed to be a 4 byte integer, bit 31 shall be 0,
 * value 0 of this integer means that it is not a valid index.
 */
int checksum_cx_xinfo_func(void *data, int flag)
{
    /* data is an int disguised as pointer. It does not point to memory. */
    return 1;
}

/* The iso_node_xinfo_cloner function which gets associated to
 * checksum_cx_xinfo_func by iso_init() resp. iso_init_with_flag() via
 * iso_node_xinfo_make_clonable()
 */
int checksum_cx_xinfo_cloner(void *old_data, void **new_data, int flag)
{
    *new_data = NULL;
    if (flag)
        return ISO_XINFO_NO_CLONE;
    if (old_data == NULL)
        return 0;
    /* data is an int disguised as pointer. It does not point to memory. */
    *new_data = old_data;
    return 0;
}


/* Function to identify and manage md5 sums of unspecified providence stored
 * directly in this xinfo.
 */
int checksum_md5_xinfo_func(void *data, int flag)
{
    if (data == NULL)
        return 1;
    free(data);
    return 1;
}

/* The iso_node_xinfo_cloner function which gets associated to
 * checksum_md5_xinfo_func by iso_init() resp. iso_init_with_flag() via
 * iso_node_xinfo_make_clonable()
 */
int checksum_md5_xinfo_cloner(void *old_data, void **new_data, int flag)
{
    *new_data = NULL;
    if (flag)
        return ISO_XINFO_NO_CLONE;
    if (old_data == NULL)
        return 0;
    *new_data = calloc(1, 16);
    if (*new_data == NULL)
        return ISO_OUT_OF_MEM;
    memcpy(*new_data, old_data, 16);
    return 16;
}


/* ----------------------------------------------------------------------- */

/* MD5 checksum image writer */

/*
  @flag bit0= recursion
        bit1= session will be appended to an existing image
*/
static
int checksum_copy_old_nodes(Ecma119Image *target, IsoNode *node, int flag)
{
    IsoNode *pos;
    IsoFile *file;
    IsoImage *img;
    int ret, i;
    size_t value_length;
    unsigned int idx = 0, old_idx = 0;
    char *value = NULL, *md5_pt = NULL;
    void *xipt;

    img = target->image;
    if (target->checksum_buffer == NULL)
        return 0;

    if (node->type == LIBISO_FILE) {
        file = (IsoFile *) node;
        if (file->from_old_session && target->appendable) {
            /* Look for checksums at various places */

            /* Try checksum directly stored with node */
            if (md5_pt == NULL) {
                ret = iso_node_get_xinfo(node, checksum_md5_xinfo_func, &xipt);
                if (ret < 0)
                    return ret;
                if (ret == 1)
                    md5_pt = (char *) xipt;
            }

            /* Try checksum index to image checksum buffer */
            if (md5_pt == NULL && img->checksum_array != NULL) {
                ret = iso_node_get_xinfo(node, checksum_cx_xinfo_func, &xipt);
                if (ret <= 0)
                    return ret;
                /* xipt is an int disguised as void pointer */
                old_idx = 0;
                for (i = 0; i < 4; i++)
                    old_idx = (old_idx << 8) | ((unsigned char *) &xipt)[i];
    
                if (old_idx == 0 || old_idx > img->checksum_idx_count - 1)
                    return 0;
                md5_pt = img->checksum_array + 16 * old_idx;
            }

            if (md5_pt == NULL)
                return 0;

            if (!target->will_cancel) {
                ret = iso_node_lookup_attr(node, "isofs.cx", &value_length,
                                           &value, 0);
                if (ret == 1 && value_length == 4) {
                    for (i = 0; i < 4; i++)
                        idx = (idx << 8) | ((unsigned char *) value)[i];
                    if (idx > 0 && idx <= target->checksum_idx_counter) {
                        memcpy(target->checksum_buffer + 16 * idx, md5_pt, 16);
                    }
                }
                if (value != NULL)
                    free(value);

                /* ts B30114 : It is unclear why these are removed here.
                               At least with the opts->will_cancel runs,
                               this is not appropriate.
                */
                iso_node_remove_xinfo(node, checksum_md5_xinfo_func);
            }
            iso_node_remove_xinfo(node, checksum_cx_xinfo_func);
        }
    } else if (node->type == LIBISO_DIR) {
        for (pos = ((IsoDir *) node)->children; pos != NULL; pos = pos->next) {
            ret = checksum_copy_old_nodes(target, pos, 1);
            if (ret < 0)
                return ret;
        }
    }
    return ISO_SUCCESS;
}


static
int checksum_writer_compute_data_blocks(IsoImageWriter *writer)
{
    size_t size;
    Ecma119Image *t;
    int ret;

    if (writer == NULL) {
        return ISO_ASSERT_FAILURE;
    }
    t = writer->target;

    t->checksum_array_pos = t->curblock;
                               /* (t->curblock already contains t->ms_block) */ 
    t->checksum_range_start = t->ms_block;
    size = (t->checksum_idx_counter + 2) / 128;
    if (size * 128 < t->checksum_idx_counter + 2)
        size++;
    t->curblock += size;
    t->checksum_range_size = t->checksum_array_pos + size
                             - t->checksum_range_start;

    /* Extra block for stream detectable checksum tag */
    t->checksum_tag_pos =  t->curblock;
    t->curblock++;

    /* Allocate array of MD5 sums */
    t->checksum_buffer = calloc(size, 2048);
    if (t->checksum_buffer == NULL)
        return ISO_OUT_OF_MEM;

    /* Copy MD5 from nodes of old image into writer->data */
    ret = checksum_copy_old_nodes(t, (IsoNode *) t->image->root, 0);
    if (ret < 0)
        return ret;

    /* Record lba,count,size,cecksum_type in "isofs.ca" of root node */
    ret = iso_root_set_isofsca((IsoNode *) t->image->root,
                               t->checksum_range_start, 
                               t->checksum_array_pos,
                               t->checksum_idx_counter + 2, 16, "MD5", 0);
    if (ret < 0)
        return ret;
    return ISO_SUCCESS;
}


static
int checksum_writer_write_vol_desc(IsoImageWriter *writer)
{

    /* The superblock checksum tag has to be written after
       the Volume Descriptor Set Terminator and thus may not be
       written by this function. (It would have been neat, though).
    */

    return ISO_SUCCESS;
}


static
int checksum_writer_write_data(IsoImageWriter *writer)
{
    int wres, res;
    size_t i, size;
    Ecma119Image *t;
    void *ctx = NULL;
    char md5[16];

    if (writer == NULL) {
        return ISO_ASSERT_FAILURE;
    }

    t = writer->target;
    iso_msg_debug(t->image->id, "Writing Checksums...");

    /* Write image checksum to index 0 */
    if (t->checksum_ctx != NULL) {
        res = iso_md5_clone(t->checksum_ctx, &ctx);
        if (res > 0) {
            res = iso_md5_end(&ctx, t->image_md5);
            if (res > 0)
                memcpy(t->checksum_buffer + 0 * 16, t->image_md5, 16);
        }
    }

    size = (t->checksum_idx_counter + 2) / 128;
    if (size * 128 < t->checksum_idx_counter + 2)
        size++;
 
    /* Write checksum of checksum array as index t->checksum_idx_counter + 1 */
    res = iso_md5_start(&ctx);
    if (res > 0) {
        for (i = 0; i < t->checksum_idx_counter + 1; i++)
            iso_md5_compute(ctx,
                          t->checksum_buffer + ((size_t) i) * (size_t) 16, 16);
        res = iso_md5_end(&ctx, md5);
        if (res > 0)
           memcpy(t->checksum_buffer + (t->checksum_idx_counter + 1) * 16,
                  md5, 16);
    }

    for (i = 0; i < size; i++) {
        wres = iso_write(t, t->checksum_buffer + ((size_t) 2048) * i, 2048);
        if (wres < 0) {
            res = wres;
            goto ex;
        }
    }
    if (t->checksum_ctx == NULL) {
        res = ISO_SUCCESS;
        goto ex;
    }

    /* Write stream detectable checksum tag to extra block */;
    res = iso_md5_write_tag(t, 1);
    if (res < 0)
        goto ex;

    res = ISO_SUCCESS;
ex:;
    if (ctx != NULL)
        iso_md5_end(&ctx, md5);
    return(res);
}


static
int checksum_writer_free_data(IsoImageWriter *writer)
{
    /* nothing was allocated at writer->data */
    return ISO_SUCCESS;
}


int checksum_writer_create(Ecma119Image *target)
{
    IsoImageWriter *writer;
    
    writer = malloc(sizeof(IsoImageWriter));
    if (writer == NULL) {
        return ISO_OUT_OF_MEM;
    }
    
    writer->compute_data_blocks = checksum_writer_compute_data_blocks;
    writer->write_vol_desc = checksum_writer_write_vol_desc;
    writer->write_data = checksum_writer_write_data;
    writer->free_data = checksum_writer_free_data;
    writer->data = NULL;
    writer->target = target;

    /* add this writer to image */
    target->writers[target->nwriters++] = writer;
    /* Account for superblock checksum tag */
    if (target->md5_session_checksum) {
        target->checksum_sb_tag_pos = target->curblock;
        target->curblock++;
    }
    return ISO_SUCCESS;
}


static
int iso_md5_write_scdbackup_tag(Ecma119Image *t, char *tag_block, int flag)
{
    void *ctx = NULL;
    off_t pos = 0, line_start;
    int record_len, block_len, ret, i;
    char postext[40], md5[16], *record = NULL;

    LIBISO_ALLOC_MEM(record, char, 160);
    line_start = strlen(tag_block);
    iso_md5_compute(t->checksum_ctx, tag_block, line_start);
    ret = iso_md5_clone(t->checksum_ctx, &ctx);
    if (ret < 0)
        goto ex;
    ret = iso_md5_end(&ctx, md5);

    pos = (off_t) t->checksum_tag_pos * (off_t) 2048 + line_start;
    if(pos >= 1000000000)
        sprintf(postext, "%u%9.9u", (unsigned int) (pos / 1000000000),
                                    (unsigned int) (pos % 1000000000));
    else
        sprintf(postext, "%u", (unsigned int) pos);
    sprintf(record, "%s %s ", t->scdbackup_tag_parm, postext);
    record_len = strlen(record);
    for (i = 0; i < 16; i++)
         sprintf(record + record_len + 2 * i,
                 "%2.2x", ((unsigned char *) md5)[i]);
    record_len += 32;

    ret = iso_md5_start(&ctx);
    if (ret < 0)
        goto ex;
    iso_md5_compute(ctx, record, record_len);
    iso_md5_end(&ctx, md5);

    sprintf(tag_block + line_start, "scdbackup_checksum_tag_v0.1 %s %d %s ",
            postext, record_len, record);
    block_len = strlen(tag_block);
    for (i = 0; i < 16; i++)
        sprintf(tag_block + block_len + 2 * i,
                "%2.2x", ((unsigned char *) md5)[i]);
    block_len+= 32;
    tag_block[block_len++]= '\n';

    if (t->scdbackup_tag_written != NULL)
    	strncpy(t->scdbackup_tag_written, tag_block + line_start,
                block_len - line_start);
    ret = ISO_SUCCESS;
ex:;
    if (ctx != NULL)
        iso_md5_end(&ctx, md5);
    LIBISO_FREE_MEM(record);
    return ret;
}


/* Write stream detectable checksum tag to extra block.
 * @flag bit0-7= tag type
 *               1= session tag (End checksumming.)
 *               2= superblock tag (System Area and Volume Descriptors)
 *               3= tree tag (ECMA-119 and Rock Ridge tree)
 *               4= relocated superblock tag (at LBA 0 of overwriteable media)
 *                  Write to target->opts_overwrite rather than to iso_write().
 */
int iso_md5_write_tag(Ecma119Image *t, int flag)
{
    int ret, mode, l, i, wres, tag_id_len;
    void *ctx = NULL;
    char md5[16], *tag_block = NULL, *tag_id;
    uint32_t size = 0, pos = 0, start;

    LIBISO_ALLOC_MEM(tag_block, char, 2048);
    start = t->checksum_range_start;
    mode = flag & 255;
    if (mode < 1 || mode > 4)
        {ret = ISO_WRONG_ARG_VALUE; goto ex;}
    ret = iso_md5_clone(t->checksum_ctx, &ctx);
    if (ret < 0)
        goto ex;
    ret = iso_md5_end(&ctx, md5);
    if (mode == 1) {
        size = t->checksum_range_size;
        pos = t->checksum_tag_pos;
    } else {
        if (mode == 2) {
            pos = t->checksum_sb_tag_pos;
        } else if (mode == 3) {
            pos = t->checksum_tree_tag_pos;
        } else if (mode == 4) {
            pos = t->checksum_rlsb_tag_pos;
            start = pos - (pos % 32);
        }
        size = pos - start;
    }
    if (ret < 0)
        goto ex;

    iso_util_tag_magic(mode, &tag_id, &tag_id_len, 0);
    sprintf(tag_block, "%s pos=%u range_start=%u range_size=%u",
            tag_id, pos, start, size);

    l = strlen(tag_block);
    if (mode == 2) {
        sprintf(tag_block + l, " next=%u", t->checksum_tree_tag_pos);
    } else if (mode == 3) {
        sprintf(tag_block + l, " next=%u", t->checksum_tag_pos);
    } else if (mode == 4) {
        sprintf(tag_block + l, " session_start=%u", t->ms_block);
    }
    strcat(tag_block + l, " md5=");
    l = strlen(tag_block);
    for (i = 0; i < 16; i++)
        sprintf(tag_block + l + 2 * i, "%2.2x",
                ((unsigned char *) md5)[i]);
    l+= 32;
        
    ret = iso_md5_start(&ctx);
    if (ret > 0) {
        iso_md5_compute(ctx, tag_block, l);
        iso_md5_end(&ctx, md5);
        strcpy(tag_block + l, " self=");
        l += 6;
        for (i = 0; i < 16; i++)
            sprintf(tag_block + l + 2 * i, "%2.2x",
                    ((unsigned char *) md5)[i]);
    }
    tag_block[l + 32] = '\n';

    if (mode == 1 && t->scdbackup_tag_parm[0]) {
        if (t->ms_block > 0) {
            iso_msg_submit(t->image->id, ISO_SCDBACKUP_TAG_NOT_0, 0, NULL);
        } else {
            ret = iso_md5_write_scdbackup_tag(t, tag_block, 0);
            if (ret < 0)
                goto ex;
        }
    }

    if (mode == 4) {
        if (t->opts_overwrite != NULL)
            memcpy(t->opts_overwrite + pos * 2048, tag_block, 2048);
    } else {
        wres = iso_write(t, tag_block, 2048);
        if (wres < 0) {
            ret = wres;
            goto ex;
        }
    }

    ret = ISO_SUCCESS;
ex:;
    if (ctx != NULL)
        iso_md5_end(&ctx, md5);
    LIBISO_FREE_MEM(tag_block);
    return ret;
}


