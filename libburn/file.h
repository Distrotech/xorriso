/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2010 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/

#ifndef BURN__FILE_H
#define BURN__FILE_H

struct burn_source_file
{
	char magic[4];

	int datafd;
	int subfd;
	off_t fixed_size;
};

/* ts A70126 : burn_source_file obsoleted burn_source_fd */


/* ts A70930 */
struct burn_source_fifo {
	char magic[4];

	/* The fifo stays inactive and unequipped with eventual resources
	   until its read() method is called for the first time.
	   Only then burn_fifo_start() gets called, allocates the complete
           resources, starts a thread with burn_fifo_source_shuffler()
	   which shuffles data and finally destroys the resources.
	   This late start is to stay modest in case of multiple tracks
	   in one disc.
	*/
	int is_started;

	void *thread_handle; /* actually a pointer to a thread_t */
	int thread_pid;
	int thread_is_valid;

	/* the burn_source for which this fifo is acting as proxy */
	struct burn_source *inp;
	int inp_read_size;

	/* <<< up to now it was only a pipe. This is on its way out. */
	int outlet[2];

	/* The ring buffer mechanism */
	int chunksize;
	int chunks;
	char *buf;
	volatile int buf_writepos;
	volatile int buf_readpos;
	volatile int end_of_input;
	volatile int input_error;
	volatile int end_of_consumption;

	off_t in_counter;
	off_t out_counter;

	int total_min_fill;
	int interval_min_fill;
	int put_counter;
	int get_counter;
	int empty_counter;
	int full_counter;

};


/** The worker behind the fifo thread.
    Gets started from burn_fifo_start() in async.c
*/
int burn_fifo_source_shoveller(struct burn_source *source, int flag);


/* ts B00922 */
struct burn_source_offst {

	/* See burn_offst_source_new() */
	struct burn_source *inp;
	struct burn_source *prev;
	off_t start;
	off_t size;
	int size_adjustable;

	/* for set_size/get_size */
	int nominal_size;

	/* To help offst_free() */
	struct burn_source *next;

	/* The current reading position */
	int running;
	off_t pos;

};

#endif /* LIBBURN__FILE_H */
