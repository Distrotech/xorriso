/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2012 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/
#ifndef BURN__CRC_H
#define BURN__CRC_H


#ifdef Xorriso_standalonE
/* Source module crc.c of yet unclear ancestry is excluded from GNU xorriso */
/* ts B20219 : The functions have been re-implemented from scratch after
               studying texts about CRC computation and understanding the
               meaning of the underlying ECMA-130 specs.
               Nevertheless, there is no need to include them into xorriso
               because it does neither CD-TEXT nor raw CD writing.
*/
#ifndef Libburn_no_crc_C
#define Libburn_no_crc_C 1
#endif
#endif


#ifndef Libburn_no_crc_C

unsigned short crc_ccitt(unsigned char *, int len);
unsigned int crc_32(unsigned char *, int len);

#endif /* Libburn_no_crc_C */


#endif /* BURN__CRC_H */
