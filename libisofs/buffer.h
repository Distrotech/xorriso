/*
 * Copyright (c) 2007 Vreixo Formoso
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

#ifndef LIBISO_BUFFER_H_
#define LIBISO_BUFFER_H_

#include <stdlib.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif

#define BLOCK_SIZE          2048

typedef struct iso_ring_buffer IsoRingBuffer;

/**
 * Create a new buffer.
 * 
 * The created buffer should be freed with iso_ring_buffer_free()
 * 
 * @param size
 *     Number of blocks in buffer. You should supply a number >= 32, otherwise
 *     size will be ignored and 32 will be used by default, which leads to a
 *     64 KiB buffer.
 * @return
 *     1 success, < 0 error
 */
int iso_ring_buffer_new(size_t size, IsoRingBuffer **rbuf);

/**
 * Free a given buffer
 */
void iso_ring_buffer_free(IsoRingBuffer *buf);

/**
 * Write count bytes into buffer. It blocks until all bytes where written or
 * reader close the buffer.
 * 
 * @param buf
 *      the buffer
 * @param data
 *      pointer to a memory region of at least coun bytes, from which data
 *      will be read.
 * @param
 *      Number of bytes to write
 * @return
 *      1 succes, 0 read finished, < 0 error
 */
int iso_ring_buffer_write(IsoRingBuffer *buf, uint8_t *data, size_t count);

/**
 * Read count bytes from the buffer into dest. It blocks until the desired
 * bytes has been read. If the writer finishes before outputting enought
 * bytes, 0 (EOF) is returned, the number of bytes already read remains
 * unknown.
 * 
 * @return
 *      1 success, 0 EOF, < 0 error
 */
int iso_ring_buffer_read(IsoRingBuffer *buf, uint8_t *dest, size_t count);

/** Backend of API call iso_ring_buffer_get_status()
 *
 * Get the status of a ring buffer.
 *
 * @param buf
 *      The ring buffer object to inquire
 * @param size
 *      Will be filled with the total size of the buffer, in bytes
 * @param free_bytes
 *      Will be filled with the bytes currently available in buffer
 * @return
 *      < 0 error, > 0 state:
 *           1="active"    : input and consumption are active
 *           2="ending"    : input has ended without error
 *           3="failing"   : input had error and ended,
 *           5="abandoned" : consumption has ended prematurely
 *           6="ended"     : consumption has ended without input error
 *           7="aborted"   : consumption has ended after input error
 */
int iso_ring_buffer_get_buf_status(IsoRingBuffer *buf, size_t *size,
                                   size_t *free_bytes);

/** 
 * Close the buffer (to be called by the writer).
 * You have to explicity close the buffer when you don't have more data to
 * write, otherwise reader will be waiting forever.
 * 
 * @param error
 *     Writer has finished prematurely due to an error
 */
void iso_ring_buffer_writer_close(IsoRingBuffer *buf, int error);

/** 
 * Close the buffer (to be called by the reader).
 * If for any reason you don't want to read more data, you need to call this
 * to let the writer thread finish.
 * 
 * @param error
 *     Reader has finished prematurely due to an error
 */
void iso_ring_buffer_reader_close(IsoRingBuffer *buf, int error);

/**
 * Get the times the buffer was full.
 */
unsigned int iso_ring_buffer_get_times_full(IsoRingBuffer *buf);

/**
 * Get the times the buffer was empty.
 */
unsigned int iso_ring_buffer_get_times_empty(IsoRingBuffer *buf);

#endif /*LIBISO_BUFFER_H_*/
