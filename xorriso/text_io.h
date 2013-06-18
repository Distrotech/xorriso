
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of text i/o functions.
*/


#ifndef Xorriso_pvt_textio_includeD
#define Xorriso_pvt_textio_includeD yes


int Xorriso_dialog_input(struct XorrisO *xorriso, char line[], int linesize,
                         int flag);

/** @return -1= abort , 0= no , 1= yes
*/
int Xorriso_reassure(struct XorrisO *xorriso, char *cmd, char *which_will,
                     int flag);

int Xorriso_request_confirmation(struct XorrisO *xorriso, int flag);

/* @param flag bit0= quoted multiline mode
               bit1= release allocated memory and return 1
               bit2= with bit0: warn of empty text arguments
               bit3= deliver as single quoted text including all whitespace
                     and without any backslash interpretation
   @return -1=out of memory , 0=line format error , 1=ok, go on , 2=done
*/
int Xorriso_read_lines(struct XorrisO *xorriso, FILE *fp, int *linecount,
                       int *argc, char ***argv, int flag);

int Xorriso_write_to_channel(struct XorrisO *xorriso,
                             char *in_text, int channel_no, int flag);

int Xorriso_result(struct XorrisO *xorriso, int flag);

int Xorriso_restxt(struct XorrisO *xorriso, char *text);

int Xorriso_info(struct XorrisO *xorriso, int flag);

int Xorriso_mark(struct XorrisO *xorriso, int flag);


int Xorriso_write_session_log(struct XorrisO *xorriso, int flag);


int Xorriso_status_result(struct XorrisO *xorriso, char *filter, FILE *fp,
                          int flag);

int Xorriso_status(struct XorrisO *xorriso, char *filter, FILE *fp, int flag);

int Xorriso_pacifier_reset(struct XorrisO *xorriso, int flag);

/* This call is to be issued by long running workers in short intervals.
   It will check whether enough time has elapsed since the last pacifier
   message and eventually issue an update message.
   @param what_done  A sparse description of the action, preferrably in past
                     tense. E.g. "done" , "files added".
   @param count The number of objects processed so far.
                Is ignored if <=0.
   @param todo  The number of objects to be done in total.
                Is ignored if <=0.
   @param current_object A string telling the object currently processed.
                         Ignored if "".
   @param flag  bit0= report unconditionally, no time check
*/
int Xorriso_pacifier_callback(struct XorrisO *xorriso, char *what_done,
                              off_t count, off_t todo, char *current_object,
                              int flag);

int Xorriso_reset_counters(struct XorrisO *xorriso, int flag);

int Xorriso_no_malloc_memory(struct XorrisO *xorriso, char **to_free,
                             int flag);

int Xorriso_much_too_long(struct XorrisO *xorriso, int len, int flag);

int Xorriso_no_findjob(struct XorrisO *xorriso, char *cmd, int flag);

int Xorriso_report_md5_outcome(struct XorrisO *xorriso, char *severity,
                               int flag);

int Xorriso_protect_stdout(struct XorrisO *xorriso, int flag);

int Xorriso_msg_op_parse(struct XorrisO *xorriso, char *line,
                         char *prefix, char *separators,
                         int max_words, int pflag, int input_lines,
                         int flag);

int Xorriso_msg_op_parse_bulk(struct XorrisO *xorriso,
                              char *prefix, char *separators,
                              int max_words, int pflag, int bulk_lines,
                              int flag);

int Xorriso_launch_frontend(struct XorrisO *xorriso, int argc, char **argv,
                           char *cmd_pipe_adr, char *reply_pipe_adr, int flag);

#endif /* ! Xorriso_pvt_textio_includeD */

