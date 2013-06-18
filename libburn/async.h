/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

#ifndef BURN__ASYNC_H
#define BURN__ASYNC_H

void burn_async_join_all(void);
struct burn_write_opts;

/* ts A70930 */
/* To be called when the first read() call comes to a fifo */
int burn_fifo_start(struct burn_source *source, int flag);

/* ts A81108 */
/* To abort a running fifo thread before the fifo object gets deleted */
int burn_fifo_abort(struct burn_source_fifo *fs, int flag);


#endif /* BURN__ASYNC_H */
