/*
 * Copyright (c) 2009 Thomas Schmitt
 *
 * This file is part of the libisofs project; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

#ifndef LIBISO_MD5_H_
#define LIBISO_MD5_H_


/* The MD5 computation API is in libisofs.h : iso_md5_start() et.al. */


/** Create a writer object for checksums and add it to the writer list of
    the given Ecma119Image.
*/
int checksum_writer_create(Ecma119Image *target);


/* Write stream detectable checksum tag to extra block.
 * All tag ranges start at the beginning of the System Area (i.e. t->ms_block)
 * and stem from the same MD5 computation context. Tag types 2 and 3 are
 * intermediate checksums. Type 2 announces the existence of type 3.
 * If both match, then at least the directory tree is trustworthy.
 * Type 1 is written at the very end of the session. If it matches, then
 * the whole image is trustworthy.
 * @param t      The image being written
 * @flag         bit0-7= tag type
 *               1= session tag (End checksumming.)
 *               2= superblock tag (System Area and Volume Descriptors)
 *               3= tree tag (ECMA-119 and Rock Ridge tree)
 */
int iso_md5_write_tag(Ecma119Image *t, int flag);


#endif /* ! LIBISO_MD5_H_ */


