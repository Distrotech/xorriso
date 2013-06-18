/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2010 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/


#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif


#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

#include "source.h"
#include "libburn.h"
#include "file.h"
#include "async.h"
#include "init.h"

#include "libdax_msgs.h"
extern struct libdax_msgs *libdax_messenger;


/* main channel data can be padded on read, but 0 padding the subs will make
an unreadable disc */


/* This is a generic OS oriented function wrapper which compensates
   shortcommings of read() in respect to a guaranteed amount of return data.
   See  man 2 read , paragraph "RETURN VALUE".
*/
static int read_full_buffer(int fd, unsigned char *buffer, int size)
{
	int ret,summed_ret = 0;

	/* make safe against partial buffer returns */
	while (1) {
		ret = read(fd, buffer + summed_ret, size - summed_ret);
		if (ret <= 0)
	break;
		summed_ret += ret;
		if (summed_ret >= size)
	break;
	}
	if (ret < 0)		 /* error encountered. abort immediately */
		return ret;
	return summed_ret;
}


static int file_read(struct burn_source *source,
		     unsigned char *buffer,
		     int size)
{
	struct burn_source_file *fs = source->data;

	return read_full_buffer(fs->datafd, buffer, size);
}

static int file_read_sub(struct burn_source *source,
			 unsigned char *buffer,
			 int size)
{
	struct burn_source_file *fs = source->data;

	return read_full_buffer(fs->subfd, buffer, size);
}

static void file_free(struct burn_source *source)
{
	struct burn_source_file *fs = source->data;

	close(fs->datafd);
	if (source->read_sub)
		close(fs->subfd);
	free(fs);
}

static off_t file_size(struct burn_source *source)
{
	struct stat buf;
	struct burn_source_file *fs = source->data;

	if (fs->fixed_size > 0)
		return fs->fixed_size;
	if (fstat(fs->datafd, &buf) != 0)
		return (off_t) 0;
	if ((buf.st_mode & S_IFMT) != S_IFREG)
		return (off_t) 0;
	return (off_t) buf.st_size;
}


/* ts A70125 */
static int file_set_size(struct burn_source *source, off_t size)
{
	struct burn_source_file *fs = source->data;

	fs->fixed_size = size;
	return 1;
}


struct burn_source *burn_file_source_new(const char *path, const char *subpath)
{
	struct burn_source_file *fs;
	struct burn_source *src;
	int fd1 = -1, fd2 = -1;

	if (!path)
		return NULL;
	fd1 = open(path, O_RDONLY);
	if (fd1 == -1)
		return NULL;
	if (subpath != NULL) {
		fd2 = open(subpath, O_RDONLY);
		if (fd2 == -1) {
			close(fd1);
			return NULL;
		}
	}
	fs = calloc(1, sizeof(struct burn_source_file));

	/* ts A70825 */
	if (fs == NULL) {
failure:;
		close(fd1);
		if (fd2 >= 0)
			close(fd2);
		return NULL;
	}

	fs->datafd = fd1;
	fs->subfd = fd2;

	/* ts A70125 */
	fs->fixed_size = 0;

	src = burn_source_new();

	/* ts A70825 */
	if (src == NULL) {
		free((char *) fs);
		goto failure;
	}

	src->read = file_read;
	if (subpath)
		src->read_sub = file_read_sub;

	src->get_size = file_size;
	src->set_size = file_set_size;
	src->free_data = file_free;
	src->data = fs;
	return src;
}


/* ts A70126 : removed class burn_source_fd in favor of burn_source_file */

struct burn_source *burn_fd_source_new(int datafd, int subfd, off_t size)
{
	struct burn_source_file *fs;
	struct burn_source *src;

	if (datafd == -1)
		return NULL;
	fs = burn_alloc_mem(sizeof(struct burn_source_file), 1, 0);
	if (fs == NULL) /* ts A70825 */
		return NULL;
	fs->datafd = datafd;
	fs->subfd = subfd;
	fs->fixed_size = size;

	src = burn_source_new();

	/* ts A70825 */
	if (src == NULL) {
		free((char *) fs);
		return NULL;
	}

	src->read = file_read;
	if(subfd != -1)
		src->read_sub = file_read_sub;
	src->get_size = file_size;
	src->set_size = file_set_size;
	src->free_data = file_free;
	src->data = fs;
	return src;
}


/* ts A71003 */
/* ------------------------------ fifo --------------------------- */

/* The fifo mechanism consists of a burn_source proxy which is here,
   a thread management team which is located in async.c,
   and a synchronous shoveller which is here.
*/

static int fifo_sleep(int flag)
{
	static unsigned long sleeptime = 50000; /* 50 ms */

	usleep(sleeptime);
	return 0;
}


static int fifo_read(struct burn_source *source,
		     unsigned char *buffer,
		     int size)
{
	struct burn_source_fifo *fs = source->data;
	int ret, todo, rpos, bufsize, diff, counted = 0;

	if (fs->end_of_consumption) {
		/* ??? msg: reading has been ended already */;
		return 0;
	}
        if (fs->is_started == 0) {
		ret = burn_fifo_start(source, 0);
		if (ret <= 0) {
			libdax_msgs_submit(libdax_messenger, -1, 0x00020152,
				 LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				"Cannot start fifo thread", 0, 0);
			fs->end_of_consumption = 1;
			return -1;
		}
		fs->is_started = 1;
	}
	if (size == 0)
		return 0;

	/* Reading from the ring buffer */

	/* This needs no mutex because each volatile variable has one thread
	   which may write and the other which only reads and is aware of
	   volatility.
	   The feeder of the ringbuffer is in burn_fifo_source_shoveller().
	*/
	todo = size;
	bufsize = fs->chunksize * fs->chunks;
	while (todo > 0) {
		/* readpos is not volatile here , writepos is volatile */
		rpos = fs->buf_readpos;
		while (rpos == fs->buf_writepos) {
			if (fs->end_of_input)
		break;
			if (fs->input_error) {
				if (todo < size) /* deliver partial buffer */
		break;
				fs->end_of_consumption = 1;
				libdax_msgs_submit(libdax_messenger, -1,
				   0x00020154,
				   LIBDAX_MSGS_SEV_NOTE, LIBDAX_MSGS_PRIO_HIGH,
				   "Forwarded input error ends output", 0, 0);
				return -1;
			}
			if (!counted)
				fs->empty_counter++;
			counted = 1;
			fifo_sleep(0);
		}
		diff = fs->buf_writepos - rpos; /* read volatile only once */
		if (diff == 0)
	break;
		if (diff > 0)
			/* diff bytes are available */;
		else 
			/* at least (bufsize - rpos) bytes are available */
			diff =  bufsize - rpos;
		if (diff > todo)
			diff = todo;
		memcpy(buffer, fs->buf+(size-todo)+rpos, diff);
		fs->buf_readpos += diff;
		if (fs->buf_readpos >= bufsize)
			fs->buf_readpos = 0;
		todo -= diff;
	}
	if (size - todo <= 0)
		fs->end_of_consumption = 1;
	else
		fs->out_counter += size - todo;

/*
	fprintf(stderr,
		"libburn_EXPERIMENTAL: read= %d , pos= %d , out_count= %.f\n",
		(size - todo), fs->buf_readpos, (double) fs->out_counter);
*/

	fs->get_counter++;
	return (size - todo);
}


static off_t fifo_get_size(struct burn_source *source)
{
	struct burn_source_fifo *fs = source->data;

	return fs->inp->get_size(fs->inp);
}


static int fifo_set_size(struct burn_source *source, off_t size)
{
	struct burn_source_fifo *fs = source->data;

	return fs->inp->set_size(fs->inp, size);
}


static void fifo_free(struct burn_source *source)
{
	struct burn_source_fifo *fs = source->data;

	burn_fifo_abort(fs, 0);
	if (fs->inp != NULL)
		burn_source_free(fs->inp);

	if (fs->buf != NULL)
		burn_os_free_buffer(fs->buf,
			((size_t) fs->chunksize) * (size_t) fs->chunks, 0);
	free((char *) fs);
}


int burn_fifo_source_shoveller(struct burn_source *source, int flag)
{
	struct burn_source_fifo *fs = source->data;
	int ret, bufsize, diff, wpos, rpos, trans_end, free_bytes, fill;
	int counted;
	char *bufpt;
	pthread_t thread_handle_storage;

	fs->thread_handle= &thread_handle_storage;
	*((pthread_t *) fs->thread_handle)= pthread_self();
	fs->thread_pid = getpid();
	fs->thread_is_valid = 1;

	bufsize = fs->chunksize * fs->chunks;
	while (!fs->end_of_consumption) {

		/* wait for enough buffer space available */
		wpos = fs->buf_writepos;
		counted = 0;
		while (1) {
			rpos = fs->buf_readpos;
			diff = rpos - wpos;
			trans_end = 0;
			if (diff == 0)
				free_bytes = bufsize - 1;
			else if (diff > 0)
				free_bytes = diff - 1;
			else {
				free_bytes = (bufsize - wpos) + rpos - 1;
				if (bufsize - wpos < fs->inp_read_size)
					trans_end = 1;
			}
			if (free_bytes >= fs->inp_read_size)
		break;
			if (!counted)
				fs->full_counter++;
			counted = 1;
			fifo_sleep(0);
		}

		fill = bufsize - free_bytes - 1;
		if (fill < fs->total_min_fill)
			fs->total_min_fill = fill;
		if (fill < fs->interval_min_fill)
			fs->interval_min_fill = fill;

		/* prepare the receiving memory */
		bufpt = fs->buf + wpos;
		if (trans_end) {
			bufpt = burn_os_alloc_buffer(
					(size_t) fs->inp_read_size, 0);
			if (bufpt == NULL) {
				libdax_msgs_submit(libdax_messenger, -1,
				  0x00000003,
				  LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
				  "Out of virtual memory", 0, 0);
				fs->input_error = ENOMEM;
	break;
			}
		}

		/* Obtain next chunk */
		if (fs->inp->read != NULL)
			ret = fs->inp->read(fs->inp,
				 (unsigned char *) bufpt, fs->inp_read_size);
		else
			ret = fs->inp->read_xt( fs->inp,
				 (unsigned char *) bufpt, fs->inp_read_size);
		if (ret == 0) {

			/* >>> ??? ts B00326 */
			/* >>> report EOF of fifo input and fs->in_counter */;

	break; /* EOF */
		} else if (ret < 0) {
			libdax_msgs_submit(libdax_messenger, -1, 0x00020153,
				 LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				"Read error on fifo input", errno, 0);
			fs->input_error = errno;
			if(errno == 0)
				fs->input_error = EIO;
	break;
		}
		fs->in_counter += ret;
		fs->put_counter++;

		/* activate read chunk */
		if (ret > fs->inp_read_size)
					/* beware of ill custom burn_source */
			ret = fs->inp_read_size;
		if (trans_end) {
			/* copy to end of buffer */
			memcpy(fs->buf + wpos, bufpt, bufsize - wpos);
			/* copy to start of buffer */
			memcpy(fs->buf, bufpt + (bufsize - wpos),
				fs->inp_read_size - (bufsize - wpos));
			burn_os_free_buffer(bufpt, (size_t) fs->inp_read_size,
						0);
			if (ret >= bufsize - wpos)
				fs->buf_writepos = ret - (bufsize - wpos);
			else
				fs->buf_writepos += ret;
		} else if (fs->buf_writepos + ret == bufsize)
			fs->buf_writepos = 0;
		else
			fs->buf_writepos += ret;

/*
		fprintf(stderr, "[%2.2d%%] ",
			 (int) (100.0 - 100.0 * ((double) free_bytes) /
						 (double) bufsize));
		fprintf(stderr,
			"libburn_EXPERIMENTAL: writepos= %d ,in_count = %.f\n",
			fs->buf_writepos, (double) fs->in_counter);
*/
	}
	if (!fs->end_of_consumption)
		fs->end_of_input = 1;

	/* wait for end of reading by consumer */;
	while (fs->buf_readpos != fs->buf_writepos && !fs->end_of_consumption)
			fifo_sleep(0);

	/* destroy ring buffer */;
	if (!fs->end_of_consumption)
		fs->end_of_consumption = 2; /* Claim stop of consumption */

	/* This is not prone to race conditions because either the consumer
	   indicated hangup by fs->end_of_consumption = 1 or the consumer set
	   fs->buf_readpos to a value indicating the buffer is empty.
	   So in both cases the consumer is aware that reading is futile
	   or even fatal.
	*/
	if(fs->buf != NULL)
		burn_os_free_buffer(fs->buf,
			((size_t) fs->chunksize) * (size_t) fs->chunks, 0);
	fs->buf = NULL;

	fs->thread_handle= NULL;
	fs->thread_is_valid = 0;
	return (fs->input_error == 0);
}


int burn_fifo_cancel(struct burn_source *source)
{
	int ret;
	struct burn_source_fifo *fs = source->data;

	ret = burn_source_cancel(fs->inp);
	return ret;
}

/*
   @param flag bit0= allow larger read chunks
*/
struct burn_source *burn_fifo_source_new(struct burn_source *inp,
		 		int chunksize, int chunks, int flag)
{
	struct burn_source_fifo *fs;
	struct burn_source *src;

	if (((double) chunksize) * ((double) chunks) > 1024.0*1024.0*1024.0) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00020155,
				 LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				"Desired fifo buffer too large (> 1GB)", 0, 0);
		return NULL;
	}
	if (chunksize < 1 || chunks < 2) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00020156,
				 LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
				"Desired fifo buffer too small", 0, 0);
		return NULL;
	}
	fs = burn_alloc_mem(sizeof(struct burn_source_fifo), 1, 0);
	if (fs == NULL)
		return NULL;
	fs->is_started = 0;
	fs->thread_handle = NULL;
	fs->thread_pid = 0;
	fs->thread_is_valid = 0;
	fs->inp = NULL; /* set later */
	if (flag & 1)
		fs->inp_read_size = 32 * 1024;
	else
		fs->inp_read_size = chunksize;
	fs->chunksize = chunksize;
	fs->chunks = chunks;
	fs->buf = NULL;
	fs->buf_writepos = fs->buf_readpos = 0;
	fs->end_of_input = 0;
	fs->input_error = 0;
	fs->end_of_consumption = 0;
	fs->in_counter = fs->out_counter = 0;
	fs->total_min_fill = fs->interval_min_fill = 0;
	fs->put_counter = fs->get_counter = 0;
	fs->empty_counter = fs->full_counter = 0;

	src = burn_source_new();
	if (src == NULL) {
		free((char *) fs);
		return NULL;
	}
	src->read = NULL;
	src->read_sub = NULL;
	src->get_size = fifo_get_size;
	src->set_size = fifo_set_size;
	src->free_data = fifo_free;
	src->data = fs;
	src->version= 1;
	src->read_xt = fifo_read;
	src->cancel= burn_fifo_cancel;
	fs->inp = inp;
	inp->refcount++; /* make sure inp lives longer than src */

	return src;
}


/* ts A71003 : API */
int burn_fifo_inquire_status(struct burn_source *source,
		 int *size, int *free_bytes, char **status_text)
{
	struct burn_source_fifo *fs = source->data;
	int ret = 0, diff, wpos, rpos;
	static char *(states[8]) = {
			"standby", "active", "ending", "failing",
			"unused", "abandoned", "ended", "aborted"};

	*status_text = NULL;
	*size = 0;

	if (source->free_data != fifo_free) {
		libdax_msgs_submit(libdax_messenger, -1, 0x00020157,
				 LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
		  "burn_source is not a fifo object", 0, 0);
		return -1;
	}
	*size = fs->chunksize * fs->chunks;
	rpos = fs->buf_readpos;
	wpos = fs->buf_writepos;
	diff = rpos - wpos;
	if (diff == 0)
		*free_bytes = *size - 1;
	else if (diff > 0)
		*free_bytes = diff - 1;
	else
		*free_bytes = (*size - wpos) + rpos - 1;
	ret = 0;
	if (fs->end_of_consumption > 0)
		ret |= 4;
	if (fs->input_error)
		ret |= 3;
	else if (fs->end_of_input)
		ret |= 2;
	else if(fs->buf != NULL)
		ret |= 1;
	*status_text = states[ret];
	return ret;
}


/* ts A91125 : API */
void burn_fifo_get_statistics(struct burn_source *source,
                             int *total_min_fill, int *interval_min_fill,
                             int *put_counter, int *get_counter,
                             int *empty_counter, int *full_counter)
{
	struct burn_source_fifo *fs = source->data;

	*total_min_fill = fs->total_min_fill;
	*interval_min_fill = fs->interval_min_fill;
	*put_counter = fs->put_counter;
	*get_counter = fs->get_counter;
	*empty_counter = fs->empty_counter;
	*full_counter = fs->full_counter;
}


/* ts A91125 : API */
void burn_fifo_next_interval(struct burn_source *source,
                            int *interval_min_fill)
{ 
	struct burn_source_fifo *fs = source->data;
	int size, free_bytes, ret;
	char *status_text;

	*interval_min_fill = fs->interval_min_fill;
	ret = burn_fifo_inquire_status(source,
					 &size, &free_bytes, &status_text);
	if (ret < 0)
		return;
	fs->interval_min_fill = size - free_bytes - 1;
}


/* @param flag bit0= do not copy to buf but only wait until the fifo has read
                     bufsize or input ended.
                     The same happens if buf is NULL.
               bit1= fill to max fifo size
*/
int burn_fifo_fill_data(struct burn_source *source, char *buf, int bufsize,
                        int flag)
{
	int size, free_bytes, ret, wait_count= 0;
	char *status_text;
	struct burn_source_fifo *fs = source->data;

	if (buf == NULL)
		flag |= 1;

	/* Eventually start fifo thread by reading 0 bytes */
	ret = fifo_read(source, (unsigned char *) NULL, 0);
	if (ret<0)
		{ret = 0; goto ex;}

	/* wait for at least bufsize bytes being ready */
	while (1) {
		ret= burn_fifo_inquire_status(source,
					 &size, &free_bytes, &status_text);
		if (flag & 2) {
			bufsize = size - (size % fs->inp_read_size) -
					fs->inp_read_size;
			if (bufsize <= 0)
				{ret = 0; goto ex;}
		}
		if (size - fs->inp_read_size < bufsize) {
			if (flag & 1) {
				bufsize = size - (size % fs->inp_read_size) -
						fs->inp_read_size;
				if (bufsize <= 0)
					{ret = 0; goto ex;}
			} else {
				libdax_msgs_submit(libdax_messenger, -1,
					0x0002015c, LIBDAX_MSGS_SEV_FAILURE,
					 LIBDAX_MSGS_PRIO_HIGH,
			 	"Fifo size too small for desired peek buffer",
					 0, 0);
				{ret = -1; goto ex;}
			}
		}
		if (fs->out_counter > 0 || (ret & 4) || fs->buf == NULL) {
			libdax_msgs_submit(libdax_messenger, -1, 0x0002015e,
			LIBDAX_MSGS_SEV_FATAL, LIBDAX_MSGS_PRIO_HIGH,
	 	"Fifo is already under consumption when peeking is desired",
			0, 0);
			{ret = -1; goto ex;}
		}
		if(size - free_bytes >= bufsize) {

			/* <<<
			fprintf(stderr,
		"libburn_DEBUG: after waiting cycle %d : fifo %s , %d bytes\n",
			 wait_count, status_text, size - free_bytes);
			*/
			if(!(flag & 1))
				memcpy(buf, fs->buf, bufsize);
			{ret = 1; goto ex;}
		}

		if (ret & 2) {
			/* input has ended, not enough data arrived */
			if (flag & 1)
				{ret = 0; goto ex;}
			libdax_msgs_submit(libdax_messenger, -1, 0x0002015d,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
		 	"Fifo input ended short of desired peek buffer size",
			0, 0);
			{ret = 0; goto ex;}
		}

		if (free_bytes < fs->inp_read_size) {
			/* Usable fifo size filled, not enough data arrived */
			if (flag & 1)
				{ret = 0; goto ex;}
			libdax_msgs_submit(libdax_messenger, -1, 0x00020174,
			LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
		 	"Fifo alignment does not allow desired read size",
			0, 0);
			{ret = 0; goto ex;}
		}

		usleep(100000);
		wait_count++;

		/* <<<
		if(wait_count%10==0)
			fprintf(stderr,
		 "libburn_DEBUG: waiting cycle %d : fifo %s , %d bytes\n",
			 wait_count, status_text, size - free_bytes);
		*/

	}
	ret = 0;
ex:;
	fs->total_min_fill = fs->interval_min_fill = fs->buf_writepos;
	return(ret);
}


/* ts A80713 : API */
int burn_fifo_peek_data(struct burn_source *source, char *buf, int bufsize,
                        int flag)
{
	return burn_fifo_fill_data(source, buf, bufsize, 0);
}


/* ts A91125 : API */
int burn_fifo_fill(struct burn_source *source, int bufsize, int flag)
{
	return burn_fifo_fill_data(source, NULL, bufsize,
					1 | ((flag & 1) << 1));
}


/* ----------------------------- Offset source ----------------------------- */
/* ts B00922 */

static void offst_free(struct burn_source *source);

/* @param flag bit0 = do not check for burn_source_offst, do not return NULL
*/
static struct burn_source_offst *offst_auth(struct burn_source *source,
								int flag)
{
	if (source->free_data != offst_free && !(flag & 1)) {
		libdax_msgs_submit(libdax_messenger, -1, 0x0002017a,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
 			"Expected offset source object as parameter",
			0, 0);
		return NULL;
	}
	return (struct burn_source_offst *) source->data;
}

static off_t offst_get_size(struct burn_source *source)
{
        struct burn_source_offst *fs;

	if ((fs = offst_auth(source, 0)) == NULL)
		return (off_t) 0;
        return fs->nominal_size;
}

static int offst_set_size(struct burn_source *source, off_t size)
{
	struct burn_source_offst *fs;

	if ((fs = offst_auth(source, 0)) == NULL)
		return 0;

	fs->nominal_size = size;
	if (fs->size <= 0 || fs->size_adjustable)
		fs->size = size;
	return 1;
}

static void offst_free(struct burn_source *source)
{
	struct burn_source_offst *fs;

	if ((fs = offst_auth(source, 0)) == NULL)
		return;
	if (fs->prev != NULL)
		offst_auth(fs->prev, 1)->next = fs->next;
	if (fs->next != NULL)
		offst_auth(fs->next, 1)->prev = fs->prev;
	if (fs->inp != NULL)
		burn_source_free(fs->inp); /* i.e. decrement refcount */
	free(source->data);
}

static int offst_read(struct burn_source *source, unsigned char *buffer,
			int size)
{
	int ret, to_read, todo;
	struct burn_source_offst *fs;

	if ((fs = offst_auth(source, 0)) == NULL)
		return -1;

	/* Eventually skip bytes up to start position */;
	if (!fs->running) {
		if (fs->prev != NULL)
			fs->pos = offst_auth(fs->prev, 1)->pos;
		fs->running= 1;
	}
	if(fs->pos < fs->start) {
		todo = fs->start - fs->pos;
		while (todo > 0) {
			to_read = todo;
			if (to_read > size)
				to_read = size;
			ret = burn_source_read(fs->inp, buffer, to_read);
			if (ret <= 0)
				return ret;
			todo -= ret;
			fs->pos += ret;
		}
	}

	/* Produce EOF if source size is exhausted.
	   burn_source delivers no incomplete sector buffers.
	*/
	if (fs->pos + size > fs->start + fs->size)
		return 0;

	/* Read payload */
	ret = burn_source_read(fs->inp, buffer, size);
	if (ret > 0)
		fs->pos += ret;
	return ret;
}

static int offst_cancel(struct burn_source *source)
{
	int ret;
	struct burn_source_offst *fs;

	if ((fs = offst_auth(source, 0)) == NULL)
		return -1;
	ret = burn_source_cancel(fs->inp);
	return ret;
}

struct burn_source *burn_offst_source_new(
		struct burn_source *inp, struct burn_source *prev,
		off_t start, off_t size, int flag)
{
	struct burn_source *src;
	struct burn_source_offst *fs, *prev_fs = NULL;

	if (prev != NULL)
		if ((prev_fs = offst_auth(prev, 0)) == NULL)
			return NULL; /* Not type burn_source_offst */

	fs = calloc(1, sizeof(struct burn_source_offst));
	if (fs == NULL)
		return NULL;
        src = burn_source_new();
        if (src == NULL) {
                free((char *) fs);
                return NULL;
        }
        src->read = NULL;
        src->read_sub = NULL;
        src->get_size = offst_get_size;
        src->set_size = offst_set_size;
        src->free_data = offst_free;
        src->data = fs;
        src->version= 1;
        src->read_xt = offst_read;
        src->cancel= offst_cancel;
        fs->inp = inp;
	fs->prev = prev;
	fs->next = NULL;
	if (prev != NULL) {
		if (prev_fs->next != NULL) {
			offst_auth(prev_fs->next, 1)->prev = src;
			fs->next = prev_fs->next;
		}
		prev_fs->next = src;
		if (prev_fs->start + prev_fs->size > start) {
			libdax_msgs_submit(libdax_messenger, -1, 0x00020179,
			LIBDAX_MSGS_SEV_FAILURE, LIBDAX_MSGS_PRIO_HIGH,
 		"Offset source start address is before end of previous source",
			0, 0);
			return NULL;
		}
	}
	fs->start = start;
	fs->size = size;
	fs->size_adjustable = !(flag & 1);
	fs->nominal_size = size;
	fs->running = 0;
	fs->pos = 0;
        inp->refcount++; /* make sure inp lives longer than src */

        return src;
}

