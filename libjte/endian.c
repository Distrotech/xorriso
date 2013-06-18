/*
 * endian.c
 *
 * Copyright (c) 2004-2006 Steve McIntyre <steve@einval.com>
 *
 * Simple helper routines for marshalling data
 *
 * GNU GPL v2+
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

#include "endianconv.h"

/* Write a 64-bit quantity out into memory in BIG ENDIAN order */
void write_be64(uint64_t in, unsigned char *out)
{
    out[0] = (in >> 56) & 0xFF;
    out[1] = (in >> 48) & 0xFF;
    out[2] = (in >> 40) & 0xFF;
    out[3] = (in >> 32) & 0xFF;
    out[4] = (in >> 24) & 0xFF;
    out[5] = (in >> 16) & 0xFF;
    out[6] = (in >> 8) & 0xFF;
    out[7] = in & 0xFF;
}

/* Read in a 64-bit BIG ENDIAN quantity */
uint64_t read_be64(unsigned char *in)
{
    uint64_t result = 0;

    result |= (uint64_t)in[0] << 56;
    result |= (uint64_t)in[1] << 48;
    result |= (uint64_t)in[2] << 40;
    result |= (uint64_t)in[3] << 32;
    result |= (uint64_t)in[4] << 24;
    result |= (uint64_t)in[5] << 16;
    result |= (uint64_t)in[6] << 8;
    result |= (uint64_t)in[7];
    
    return result;
}

/* Write a 64-bit quantity out into memory in LITTLE ENDIAN order */
void write_le64(uint64_t in, unsigned char *out)
{
    out[0] = in & 0xFF;
    out[1] = (in >> 8) & 0xFF;
    out[2] = (in >> 16) & 0xFF;
    out[3] = (in >> 24) & 0xFF;
    out[4] = (in >> 32) & 0xFF;
    out[5] = (in >> 40) & 0xFF;
    out[6] = (in >> 48) & 0xFF;
    out[7] = (in >> 56) & 0xFF;
}

/* Read in a 64-bit LITTLE ENDIAN quantity */
uint64_t read_le64(unsigned char *in)
{
    uint64_t result = 0;

    result |= (uint64_t)in[0];
    result |= (uint64_t)in[1] << 8;
    result |= (uint64_t)in[2] << 16;
    result |= (uint64_t)in[3] << 24;
    result |= (uint64_t)in[4] << 32;
    result |= (uint64_t)in[5] << 40;
    result |= (uint64_t)in[6] << 48;
    result |= (uint64_t)in[7] << 56;
    
    return result;
}

/* Write a 48-bit quantity out into memory in LITTLE ENDIAN order */
void write_le48(uint64_t in, unsigned char *out)
{
    out[0] = in & 0xFF;
    out[1] = (in >> 8) & 0xFF;
    out[2] = (in >> 16) & 0xFF;
    out[3] = (in >> 24) & 0xFF;
    out[4] = (in >> 32) & 0xFF;
    out[5] = (in >> 40) & 0xFF;
}

/* Read in a 48-bit LITTLE ENDIAN quantity */
uint64_t read_le48(unsigned char *in)
{
    uint64_t result = 0;

    result |= (uint64_t)in[0];
    result |= (uint64_t)in[1] << 8;
    result |= (uint64_t)in[2] << 16;
    result |= (uint64_t)in[3] << 24;
    result |= (uint64_t)in[4] << 32;
    result |= (uint64_t)in[5] << 40;
    
    return result;
}

/* Write a 32-bit quantity out into memory in BIG ENDIAN order */
void write_be32(unsigned long in, unsigned char *out)
{
    out[0] = (in >> 24) & 0xFF;
    out[1] = (in >> 16) & 0xFF;
    out[2] = (in >> 8) & 0xFF;
    out[3] = in & 0xFF;
}

/* Read in a 32-bit BIG ENDIAN quantity */
unsigned long read_be32(unsigned char *in)
{
    unsigned long result = 0;

    result |= (unsigned long)in[0] << 24;
    result |= (unsigned long)in[1] << 16;
    result |= (unsigned long)in[2] << 8;
    result |= (unsigned long)in[3];
    
    return result;
}

/* Write a 32-bit quantity out into memory in LITTLE ENDIAN order */
void write_le32(unsigned long in, unsigned char *out)
{
    out[0] = in & 0xFF;
    out[1] = (in >> 8) & 0xFF;
    out[2] = (in >> 16) & 0xFF;
    out[3] = (in >> 24) & 0xFF;
}

/* Read in a 32-bit LITTLE ENDIAN quantity */
unsigned long read_le32(unsigned char *in)
{
    unsigned long result = 0;

    result |= (unsigned long)in[0];
    result |= (unsigned long)in[1] << 8;
    result |= (unsigned long)in[2] << 16;
    result |= (unsigned long)in[3] << 24;
    
    return result;
}

/* Write a 16-bit quantity out into memory in BIG ENDIAN order */
void write_be16(unsigned short in, unsigned char *out)
{
    out[0] = (in >> 8) & 0xFF;
    out[1] = in & 0xFF;
}
    
/* Read in a 16-bit BIG ENDIAN quantity */
unsigned short read_be16(unsigned char *in)
{
    unsigned short result = 0;
    
    result |= (unsigned short)in[0] << 8;
    result |= (unsigned short)in[1];
    return result;
}

/* Write a 16-bit quantity out into memory in LITTLE ENDIAN order */
void write_le16(unsigned short in, unsigned char *out)
{
    out[0] = in & 0xFF;
    out[1] = in & 0xFF >> 8;
}
    
/* Read in a 16-bit LITTLE ENDIAN quantity */
unsigned short read_le16(unsigned char *in)
{
    unsigned short result = 0;
    
    result |= (unsigned short)in[0];
    result |= (unsigned short)in[1] << 8;
    return result;
}

