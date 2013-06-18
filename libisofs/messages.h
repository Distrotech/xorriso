/*
 * Copyright (c) 2007 Vreixo Formoso
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

/*
 * Message handling for libisofs
 */

#ifndef MESSAGES_H_
#define MESSAGES_H_

/**
 * Take and increment this variable to get a valid identifier for message
 * origin.
 */
extern int iso_message_id;

/**
 * Submit a debug message.
 */
void iso_msg_debug(int imgid, const char *fmt, ...);


/**
 * Inquire whether the given error code triggers the abort threshold
 */
int iso_msg_is_abort(int errcode);


/**
 * 
 * @param errcode
 *      The error code.
 * @param causedby
 *      Error that was caused the errcode. If this error is a FATAL error,
 *      < 0 will be returned in any case. Use 0 if there is no previous 
 *      cause for the error.
 * @return
 *      0 on success, < 0 if function must abort asap.
 */
int iso_msg_submit(int imgid, int errcode, int causedby, const char *fmt, ...);


/* To be called with events which report incidents with individual input 
   files from the local filesystem. Not with image nodes, files containing an
   image or similar file-like objects.
*/
int iso_report_errfile(char *path, int error_code, int os_errno, int flag);


/* Drains the libjte message list and puts out the messages via
   iso_msg_submit()
*/
int iso_libjte_forward_msgs(void *libjte_handle,
                            int imgid, int errcode, int flag);

#endif /*MESSAGES_H_*/
