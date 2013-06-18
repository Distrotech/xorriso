/*
 * Copyright (c) 2009 - 2011 Thomas Schmitt
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 *
 * It implements a filter facility which can pipe a IsoStream into an external
 * process, read its output and forward it as IsoStream output to an IsoFile.
 * The external processes get started according to an IsoExternalFilterCommand
 * which is described in libisofs.h.
 * 
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "../libisofs.h"
#include "../filter.h"
#include "../fsource.h"
#include "../stream.h"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#ifdef Libisofs_external_filters_selecT
#include <sys/select.h>
#endif

/*
 * A filter that starts an external process and uses its stdin and stdout
 * for classical pipe filtering.
 */

/* IMPORTANT: Any change must be reflected by extf_clone_stream() */
/*
 * Individual runtime properties exist only as long as the stream is opened.
 */
typedef struct
{
    int send_fd;
    int recv_fd;
    pid_t pid;
    off_t in_counter;
    int in_eof;
    off_t out_counter;
    int out_eof;
    uint8_t pipebuf[2048]; /* buffers in case of EAGAIN on write() */
    int pipebuf_fill;
} ExternalFilterRuntime;


static
int extf_running_new(ExternalFilterRuntime **running, int send_fd, int recv_fd,
                     pid_t child_pid, int flag)
{
    ExternalFilterRuntime *o;
    *running = o = calloc(sizeof(ExternalFilterRuntime), 1);
    if (o == NULL) {
        return ISO_OUT_OF_MEM;
    }
    o->send_fd = send_fd;
    o->recv_fd = recv_fd;
    o->pid = child_pid;
    o->in_counter = 0;
    o->in_eof = 0;
    o->out_counter = 0;
    o->out_eof = 0;
    memset(o->pipebuf, 0, sizeof(o->pipebuf));
    o->pipebuf_fill = 0;
    return 1;
}


/*
 * The data payload of an individual IsoStream from External Filter
 */
typedef struct
{
    ino_t id;

    IsoStream *orig;

    IsoExternalFilterCommand *cmd;

    off_t size; /* -1 means that the size is unknown yet */

    ExternalFilterRuntime *running; /* is non-NULL when open */

} ExternalFilterStreamData;


/* Each individual ExternalFilterStreamData needs a unique id number. */
/* >>> This is very suboptimal:
       The counter can rollover.
*/
static ino_t extf_ino_id = 0;


/* <<< */
static int print_fd= 0;


/*
 * Methods for the IsoStreamIface of an External Filter object.
 */


/*
 * @param flag  bit0= original stream is not open
 */
static
int extf_stream_close_flag(IsoStream *stream, int flag)
{
    int ret, status;
    ExternalFilterStreamData *data;

    if (stream == NULL) {
        return ISO_NULL_POINTER;
    }
    data = stream->data;

    if (data->running == NULL) {
        return 1;
    }

    /* <<< */
    if (print_fd) {
        fprintf(stderr, "libisofs_DEBUG: filter close  in  = %d , ic= %.f\n",
                data->running->recv_fd, (double) data->running->in_counter);
        fprintf(stderr, "libisofs_DEBUG: filter close  out = %d , oc= %.f\n",
                data->running->send_fd, (double) data->running->out_counter);
    }

    if(data->running->recv_fd != -1)
        close(data->running->recv_fd);
    if(data->running->send_fd != -1)
        close(data->running->send_fd);

    ret = waitpid(data->running->pid, &status, WNOHANG);
    if (ret == 0 && data->running->pid != 0) {
        kill(data->running->pid, SIGKILL);
        waitpid(data->running->pid, &status, 0);
    }
    free(data->running);
    data->running = NULL;
    if (flag & 1)
        return 1;
    return iso_stream_close(data->orig);
}


static
int extf_stream_close(IsoStream *stream)
{
    return extf_stream_close_flag(stream, 0);
}


/*
 * @param flag  bit0= do not run .get_size() if size is < 0
 */
static
int extf_stream_open_flag(IsoStream *stream, int flag)
{
    ExternalFilterStreamData *data;
    ExternalFilterRuntime *running = NULL;
    pid_t child_pid;
    int send_pipe[2], recv_pipe[2], ret, stream_open = 0;

    send_pipe[0] = send_pipe[1] = recv_pipe[0] = recv_pipe[1] = -1;

    if (stream == NULL) {
        return ISO_NULL_POINTER;
    }
    data = (ExternalFilterStreamData*)stream->data;
    if (data->running != NULL) {
        return ISO_FILE_ALREADY_OPENED;
    }
    if (data->size < 0 && !(flag & 1)) {
      /* Do the size determination run now, so that the size gets cached
         and .get_size() will not fail on an opened stream.
      */
      stream->class->get_size(stream);
    }

    ret = pipe(send_pipe);
    if (ret == -1) {
        ret = ISO_OUT_OF_MEM;
        goto parent_failed;
    }
    ret = pipe(recv_pipe);
    if (ret == -1) {
        ret = ISO_OUT_OF_MEM;
        goto parent_failed;
    }

    child_pid= fork();
    if (child_pid == -1) {
        ret = ISO_DATA_SOURCE_FATAL;
        goto parent_failed;
    }

    if (child_pid != 0) {
        /* parent */
        ret = extf_running_new(&running, send_pipe[1], recv_pipe[0], child_pid,
                               0);
        if (ret < 0) {
            goto parent_failed;
        }
        data->running = running;

        /* <<< */
        if (print_fd) {
            fprintf(stderr, "libisofs_DEBUG: filter parent in  = %d\n",
                    data->running->recv_fd);
            fprintf(stderr, "libisofs_DEBUG: filter parent out = %d\n",
                    data->running->send_fd);
        }

        /* Give up the child-side pipe ends */
        close(send_pipe[0]);
        close(recv_pipe[1]);

        /* Open stream only after forking so that the child does not know
           the pipe inlets of eventually underlying other filter streams.
           They would stay open and prevent those underlying filter children
           from seeing EOF at their input.
        */
        ret = iso_stream_open(data->orig);


        /* <<< TEST <<<
        ret= ISO_FILE_READ_ERROR;
        */

        if (ret < 0) {
            /* Dispose pipes and child */
            extf_stream_close_flag(stream, 1);
            return ret;
        }
        stream_open = 1;
        /* Make filter outlet non-blocking */
        ret = fcntl(recv_pipe[0], F_GETFL);
        if (ret != -1) {
            ret |= O_NONBLOCK;
            fcntl(recv_pipe[0], F_SETFL, ret);
        }
        /* Make filter sink non-blocking */
        ret = fcntl(send_pipe[1], F_GETFL);
        if (ret != -1) {
            ret |= O_NONBLOCK;
            fcntl(send_pipe[1], F_SETFL, ret);
        }
        return 1;
    }

    /* child */

    /* Give up the parent-side pipe ends */
    close(send_pipe[1]);
    close(recv_pipe[0]);

    /* attach pipe ends to stdin and stdout */;
    close(0);
    ret = dup2(send_pipe[0], 0);
    if (ret == -1) {
        goto child_failed;
    }
    close(1);
    ret = dup2(recv_pipe[1], 1);
    if (ret == -1) {
        goto child_failed;
    }

    /* <<< */
    if (print_fd) {
        fprintf(stderr, "libisofs_DEBUG: filter child  in  = %d\n",
                send_pipe[0]);
        fprintf(stderr, "libisofs_DEBUG: filter child  out = %d\n",
                recv_pipe[1]);
    }

    /* Self conversion into external program */
    execv(data->cmd->path, data->cmd->argv); /* should never come back */

child_failed:;
    fprintf(stderr,"--- execution of external filter command failed:\n");
    fprintf(stderr,"    %s\n", data->cmd->path);
    exit(127);

parent_failed:;

    /* <<< */
    if (print_fd) {
        fprintf(stderr, "libisofs_DEBUG: FAILED : filter parent in  = %d\n",
                recv_pipe[0]);
        fprintf(stderr, "libisofs_DEBUG: FAILED : filter parent out = %d\n",
                send_pipe[1]);
    }

    if (stream_open)
        iso_stream_close(data->orig);
    if(send_pipe[0] != -1)
        close(send_pipe[0]);
    if(send_pipe[1] != -1)
        close(send_pipe[1]);
    if(recv_pipe[0] != -1)
        close(recv_pipe[0]);
    if(recv_pipe[1] != -1)
        close(recv_pipe[1]);
    return ret;
}


static
int extf_stream_open(IsoStream *stream)
{
    return extf_stream_open_flag(stream, 0);
}


#ifdef Libisofs_external_filters_selecT

/* Performance is weaker than with non-blocking i/o and usleep(). */

static
int extf_wait_for_io(int fd_in, int fd_out, int microsec, int flag)
{
    struct timeval wt;
    fd_set rds,wts,exs;
    int ready, fd_max;

    fd_max = fd_out;
    if (fd_in > fd_out)
        fd_max = fd_in;

    FD_ZERO(&rds);
    FD_ZERO(&wts);
    FD_ZERO(&exs);
    if (fd_in >= 0) {
        FD_SET(fd_in,&rds);
        FD_SET(fd_in,&exs);
    }
    if (fd_out >= 0) {
        FD_SET(fd_out,&rds);
        FD_SET(fd_in,&exs);
    }
    wt.tv_sec =  microsec/1000000;
    wt.tv_usec = microsec%1000000;
    ready = select(fd_max + 1, &rds, &wts, &exs, &wt);
    if (ready <= 0)
        return 0;
    if (fd_in >= 0) {
        if (FD_ISSET(fd_in, &rds))
            return 1;
    }
    if (fd_out >= 0) {
        if (FD_ISSET(fd_out, &rds))
            return 2;
    }
    if (fd_in >= 0) {
        if (FD_ISSET(fd_in, &exs))
            return -1;
    }
    if (fd_out >= 0) {
        if (FD_ISSET(fd_out, &exs))
            return -2;
    }
    return(0);
}

#endif /* Libisofs_external_filters_selecT */


static
int extf_stream_read(IsoStream *stream, void *buf, size_t desired)
{
    int ret, blocking = 0;
    ExternalFilterStreamData *data;
    ExternalFilterRuntime *running;
    size_t fill = 0;

    if (stream == NULL) {
        return ISO_NULL_POINTER;
    }
    data = stream->data;
    running= data->running;
    if (running == NULL) {
        return ISO_FILE_NOT_OPENED;
    }
    if (running->out_eof) {
        return 0;
    }

    while (1) {
        if (running->in_eof && !blocking) {
            /* Make filter outlet blocking */
            ret = fcntl(running->recv_fd, F_GETFL);
            if (ret != -1) {
                ret &= ~O_NONBLOCK;
                fcntl(running->recv_fd, F_SETFL, ret);
            }
            blocking = 1;
        }

        /* Try to read desired amount from filter */;
        while (1) {
            ret = read(running->recv_fd, ((char *) buf) + fill,
                       desired - fill);
            if (ret < 0) {
                if (errno == EAGAIN)
        break;
                return ISO_FILE_READ_ERROR;
            }
            fill += ret;
            if (ret == 0) {
                running->out_eof = 1;
            }
            if (ret == 0 || fill >= desired) {
                running->out_counter += fill;
                return fill;
            }
        }

        if (running->in_eof) {
            usleep(1000); /* just in case it is still non-blocking */
    continue;
        }
        if (running->pipebuf_fill) {
            ret = running->pipebuf_fill;
            running->pipebuf_fill = 0;
        } else {
            ret = iso_stream_read(data->orig, running->pipebuf,
                                  sizeof(running->pipebuf));
            if (ret > 0)
                running->in_counter += ret;
        }
        if (ret < 0) {
            running->in_eof = 1;
            return ret;
        }
        if (ret == 0) {

            /* <<< */
            if (print_fd) {
                fprintf(stderr,
                 "libisofs_DEBUG: filter close  out = %d , ic= %.f\n",
                 running->send_fd, (double) running->in_counter);
            }

            running->in_eof = 1;
            close(running->send_fd); /* Tell the filter: it is over */
            running->send_fd = -1;
        } else {
            running->pipebuf_fill = ret;
            ret = write(running->send_fd, running->pipebuf,
                        running->pipebuf_fill);
            if (ret == -1) {
                if (errno == EAGAIN) {

#ifdef Libisofs_external_filters_selecT

                    /* This select() based waiting saves 10 % CPU load but
                       needs 50 % more real time */

                    ret = extf_wait_for_io(running->recv_fd, running->send_fd,
                                           100000, 0);
                    if (ret < 0)
                        usleep(1000); /* To make sure sufficient laziness */

#else

                    /* No sleeping needs 90 % more CPU and saves 6 % time */
                    usleep(1000); /* go lazy because the filter is slow */

#endif /* ! Libisofs_external_filters_selecT */

    continue;
                }

                /* From the view of the caller it _is_ a read error */
                running->in_eof = 1;
                return ISO_FILE_READ_ERROR;
            }
            running->pipebuf_fill = 0;
        }
    }
    return ISO_FILE_READ_ERROR; /* should never be hit */
}


static
off_t extf_stream_get_size(IsoStream *stream)
{
    int ret, ret_close;
    off_t count = 0;
    ExternalFilterStreamData *data;
    char buf[64 * 1024];
    size_t bufsize = 64 * 1024;

    if (stream == NULL) {
        return ISO_NULL_POINTER;
    }
    data = stream->data;

    if (data->size >= 0) {
        return data->size;
    }

    /* Run filter command and count output bytes */
    ret = extf_stream_open_flag(stream, 1);
    if (ret < 0) {
        return ret;
    }
    while (1) {
        ret = extf_stream_read(stream, buf, bufsize);
        if (ret <= 0)
            break;
        count += ret;
    }
    ret_close = extf_stream_close(stream);
    if (ret < 0)
        return ret;
    if (ret_close < 0)
        return ret_close;

    data->size = count;
    return count;
}


static
int extf_stream_is_repeatable(IsoStream *stream)
{
    /* Only repeatable streams are accepted as orig */
    return 1;
}


static
void extf_stream_get_id(IsoStream *stream, unsigned int *fs_id, 
                        dev_t *dev_id, ino_t *ino_id)
{
    ExternalFilterStreamData *data;

    data = stream->data;
    *fs_id = ISO_FILTER_FS_ID;
    *dev_id = ISO_FILTER_EXTERNAL_DEV_ID;
    *ino_id = data->id;
}


static
void extf_stream_free(IsoStream *stream)
{
    ExternalFilterStreamData *data;

    if (stream == NULL) {
        return;
    }
    data = stream->data;
    if (data->running != NULL) {
        extf_stream_close(stream);
    }
    iso_stream_unref(data->orig);
    if (data->cmd->refcount > 0)
        data->cmd->refcount--;
    free(data);
}


static
int extf_update_size(IsoStream *stream)
{
    /* By principle size is determined only once */
    return 1;
}


static
IsoStream *extf_get_input_stream(IsoStream *stream, int flag)
{
    ExternalFilterStreamData *data;

    if (stream == NULL) {
        return NULL;
    }
    data = stream->data;
    return data->orig;
}

static
int extf_clone_stream(IsoStream *old_stream, IsoStream **new_stream, int flag)
{
    int ret;
    IsoStream *new_input_stream, *stream;
    ExternalFilterStreamData *stream_data, *old_stream_data;
    
    if (flag)
        return ISO_STREAM_NO_CLONE; /* unknown option required */

    stream_data = calloc(1, sizeof(ExternalFilterStreamData));
    if (stream_data == NULL)
        return ISO_OUT_OF_MEM;
    ret = iso_stream_clone_filter_common(old_stream, &stream,
                                         &new_input_stream, 0);
    if (ret < 0) {
        free((char *) stream_data);
        return ret;
    }
    old_stream_data = (ExternalFilterStreamData *) old_stream->data;
    stream_data->id = ++extf_ino_id;
    stream_data->orig = new_input_stream;
    stream_data->cmd = old_stream_data->cmd;
    stream_data->cmd->refcount++;
    stream_data->size = old_stream_data->size;
    stream_data->running = NULL;
    stream->data = stream_data;
    *new_stream = stream;
    return ISO_SUCCESS;
}

static
int extf_cmp_ino(IsoStream *s1, IsoStream *s2);
/* Function is defined after definition of extf_stream_class */


IsoStreamIface extf_stream_class = {
    4,
    "extf",
    extf_stream_open,
    extf_stream_close,
    extf_stream_get_size,
    extf_stream_read,
    extf_stream_is_repeatable,
    extf_stream_get_id,
    extf_stream_free,
    extf_update_size,
    extf_get_input_stream,
    extf_cmp_ino,
    extf_clone_stream
};


static
int extf_cmp_ino(IsoStream *s1, IsoStream *s2)
{
    ExternalFilterStreamData *data1, *data2;

    if (s1->class != &extf_stream_class || s2->class != &extf_stream_class)
        return iso_stream_cmp_ino(s1, s2, 1);
    data1 = (ExternalFilterStreamData*) s1->data;
    data2 = (ExternalFilterStreamData*) s2->data;
    if (data1->cmd != data2->cmd)
        return (data1->cmd < data2->cmd ? -1 : 1);
    return iso_stream_cmp_ino(data1->orig, data2->orig, 0);
}


/* ------------------------------------------------------------------------- */

static
void extf_filter_free(FilterContext *filter)
{
    /* no data are allocated */;
}


/* To be called by iso_file_add_filter().
 * The FilterContext input parameter is not furtherly needed for the 
 * emerging IsoStream.
 */
static
int extf_filter_get_filter(FilterContext *filter, IsoStream *original, 
                  IsoStream **filtered)
{
    IsoStream *str;
    ExternalFilterStreamData *data;
    IsoExternalFilterCommand *cmd;

    if (filter == NULL || original == NULL || filtered == NULL) {
        return ISO_NULL_POINTER;
    }
    cmd = (IsoExternalFilterCommand *) filter->data;
    if (cmd->refcount + 1 <= 0) {
        return ISO_EXTF_TOO_OFTEN;
    }

    str = malloc(sizeof(IsoStream));
    if (str == NULL) {
        return ISO_OUT_OF_MEM;
    }
    data = malloc(sizeof(ExternalFilterStreamData));
    if (data == NULL) {
        free(str);
        return ISO_OUT_OF_MEM;
    }


    /* These data items are not owned by this filter object */
    data->id = ++extf_ino_id;
    data->orig = original;
    data->cmd = cmd;
    data->size = -1;
    data->running = NULL;

    /* get reference to the source */
    iso_stream_ref(data->orig);

    str->refcount = 1;
    str->data = data;
    str->class = &extf_stream_class;

    *filtered = str;

    cmd->refcount++;
    return ISO_SUCCESS;
}


/* Produce a parameter object suitable for iso_file_add_filter().
 * It may be disposed by free() after all those calls are made.
 *
 * This is an internal call of libisofs to be used by an API call that
 * attaches an IsoExternalFilterCommand to one or more IsoFile objects.
 * See libisofs.h for IsoExternalFilterCommand.
 */
static
int extf_create_context(IsoExternalFilterCommand *cmd,
                        FilterContext **filter, int flag)
{
    FilterContext *f;
    
    *filter = f = calloc(1, sizeof(FilterContext));
    if (f == NULL) {
        return ISO_OUT_OF_MEM;
    }
    f->refcount = 1;
    f->version = 0;
    f->data = cmd;
    f->free = extf_filter_free;
    f->get_filter = extf_filter_get_filter;
    return ISO_SUCCESS;
}


/*
 * A function which adds a filter to an IsoFile shall create a temporary
 * FilterContext by iso_extf_create_context(), use it in one or more calls
 * of filter.c:iso_file_add_filter() and finally dispose it by free().
 */

int iso_file_add_external_filter(IsoFile *file, IsoExternalFilterCommand *cmd,
                                 int flag)
{
    int ret;
    FilterContext *f = NULL;
    IsoStream *stream;
    off_t original_size = 0, filtered_size = 0;

    if (cmd->behavior & (1 | 2 | 4)) {
        original_size = iso_file_get_size(file);
        if (original_size <= 0 ||
            ((cmd->behavior & 4) && original_size <= 2048)) {
            return 2;
        }
    }
    ret = extf_create_context(cmd, &f, 0);
    if (ret < 0) {
        return ret;
    }
    ret = iso_file_add_filter(file, f, 0);
    free(f);
    if (ret < 0) {
        return ret;
    }
    /* Run a full filter process getsize so that the size is cached */
    stream = iso_file_get_stream(file);
    filtered_size = iso_stream_get_size(stream);
    if (filtered_size < 0) {
        iso_file_remove_filter(file, 0);
        return filtered_size;
    }
    if (((cmd->behavior & 2) && filtered_size >= original_size) ||
        ((cmd->behavior & 4) && filtered_size / 2048 >= original_size / 2048)){
        ret = iso_file_remove_filter(file, 0);
        if (ret < 0) {
            return ret;
        }
        return 2;
    }
    return ISO_SUCCESS;
}


int iso_stream_get_external_filter(IsoStream *stream,
                                   IsoExternalFilterCommand **cmd, int flag)
{
    ExternalFilterStreamData *data;

    if (stream->class != &extf_stream_class)
        return 0;
    data = stream->data;
    *cmd = data->cmd;
    return 1;
}

