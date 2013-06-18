/*
 * endianconv.h
 *
 * Copyright (c) 2004-2006 Steve McIntyre <steve@einval.com>
 *
 * Simple helper routines for marshalling data - prototypes
 *
 * GNU GPL v2
 */

#ifndef _JTE_ENDIANCONV_H_
#define _JTE_ENDIANCONV_H_

void                  write_be64(uint64_t in, unsigned char *out);
uint64_t              read_be64(unsigned char *in);
void                  write_le64(uint64_t in, unsigned char *out);
uint64_t              read_le64(unsigned char *in);

void                  write_le48(uint64_t in, unsigned char *out);
uint64_t              read_le48(unsigned char *in);

void                  write_be32(unsigned long in, unsigned char *out);
unsigned long         read_be32(unsigned char *in);
void                  write_le32(unsigned long in, unsigned char *out);
unsigned long         read_le32(unsigned char *in);

void                  write_be16(unsigned short in, unsigned char *out);
unsigned short        read_be16(unsigned char *in);
void                  write_le16(unsigned short in, unsigned char *out);
unsigned short        read_le16(unsigned char *in);

#endif
/* _JTE_ENDIANCONV_H_ */
