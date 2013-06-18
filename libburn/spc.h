/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2012 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/


#ifndef __SPC
#define __SPC

#include "libburn.h"

void spc_inquiry(struct burn_drive *);
void spc_prevent(struct burn_drive *);
void spc_allow(struct burn_drive *);
void spc_sense_caps(struct burn_drive *);
void spc_sense_error_params(struct burn_drive *);
void spc_select_error_params(struct burn_drive *,
			     const struct burn_read_opts *);
void spc_getcaps(struct burn_drive *d);
void spc_sense_write_params(struct burn_drive *);
void spc_select_write_params(struct burn_drive *,
			     struct burn_session *, int,
			     const struct burn_write_opts *);
void spc_probe_write_modes(struct burn_drive *);
void spc_request_sense(struct burn_drive *d, struct buffer *buf);
int spc_block_type(enum burn_block_types b);
int spc_get_erase_progress(struct burn_drive *d);

/* ts A70315 : test_unit_ready with result parameters */
int spc_test_unit_ready_r(struct burn_drive *d, int *key, int *asc, int *ascq,
				int *progress);

int spc_test_unit_ready(struct burn_drive *d);

/* ts A70315 */
/** Wait until the drive state becomes clear in or until max_sec elapsed */
int spc_wait_unit_attention(struct burn_drive *d, int max_sec, char *cmd_text,
				int flag);

/* ts A61021 : the spc specific part of sg.c:enumerate_common()
*/
int spc_setup_drive(struct burn_drive *d);

/* ts A61021 : the general SCSI specific part of sg.c:enumerate_common()
   @param flag Bitfield for control purposes
               bit0= do not setup spc/sbc/mmc
*/
int burn_scsi_setup_drive(struct burn_drive *d, int bus_no, int host_no,
			int channel_no, int target_no, int lun_no, int flag);

/* ts A61115 moved from sg-*.h */
enum response { RETRY, FAIL, GO_ON };
enum response scsi_error(struct burn_drive *, unsigned char *, int);

/* ts A61122 */
enum response scsi_error_msg(struct burn_drive *d, unsigned char *sense,
                             int senselen, char msg[161],
                             int *key, int *asc, int *ascq);

/* ts A61030 */
/* @param flag bit0=do report conditions which are considered not an error */
int scsi_notify_error(struct burn_drive *, struct command *c,
			unsigned char *sense, int senselen, int flag);

/* ts A70519 */
int scsi_init_command(struct command *c, unsigned char *opcode, int oplen);

/* ts A91106 */
int scsi_show_cmd_text(struct command *c, void *fp, int flag);

/* ts B11110 */
/** Logs command (before execution). */
int scsi_log_command(unsigned char *opcode, int oplen, int data_dir,
                     unsigned char *data, int bytes,
                     void *fp_in, int flag);

/* ts A91218 (former sg_log_cmd ts A70518) */
/** Legacy frontend to scsi_log_command() */
int scsi_log_cmd(struct command *c, void *fp, int flag);

/* ts B11110 */
/** Logs outcome of a sg command.
    @param flag  bit0 causes an error message 
                 bit1 do not print duration
*/
int scsi_log_reply(unsigned char *opcode, int data_dir, unsigned char *data,
                   int dxfer_len, void *fp_in, unsigned char sense[18],
                   int sense_len, int duration, int flag);

/* ts A91221 (former sg_log_err ts A91108) */
/** Legacy frontend to scsi_log_reply().
    @param flag  bit0 causes an error message
                 bit1 do not print duration
*/
int scsi_log_err(struct command *c, void *fp, unsigned char sense[18], 
                 int sense_len, int duration, int flag);

/* ts B00728 */
int spc_decode_sense(unsigned char *sense, int senselen,
                     int *key, int *asc, int *ascq);

/* ts B00808 */
/** Evaluates outcome of a single SCSI command, eventually logs sense data,
    and issues DEBUG error message in case the command is evaluated as done.
    @param flag   bit1 = do not print duration
    @return 0 = not yet done , 1 = done , -1 = error
*/
int scsi_eval_cmd_outcome(struct burn_drive *d, struct command *c, void *fp_in,
                        unsigned char *sense, int sense_len,
                        int duration, time_t start_time, int timeout_ms,
			int loop_count, int flag);


/* The waiting time before eventually retrying a failed SCSI command.
   Before each retry wait Libburn_scsi_retry_incR longer than with
   the previous one. At most wait for Libburn_scsi_retry_umaX microseconds.
*/
#define Libburn_scsi_retry_usleeP 100000
#define Libburn_scsi_retry_incR   100000
#define Libburn_scsi_retry_umaX   500000

/* The retry waiting time for commands WRITE(10) and WRITE(12).
*/
#define Libburn_scsi_write_retry_usleeP     0
#define Libburn_scsi_write_retry_incR    2000
#define Libburn_scsi_write_retry_umaX   25000


/* ts B11124 */
/* Millisecond timeout for quickly responding SPC, SBC, and MMC commands */
#define Libburn_scsi_default_timeouT          30000

/* WRITE(10) and WRITE(12) */
#define Libburn_scsi_write_timeouT           200000

/* RESERVE TRACK */
#define Libburn_mmc_reserve_timeouT          200000

/* CLOSE TRACK/SESSION (with Immed bit) */
#define Libburn_mmc_close_timeouT            200000

/* BLANK , FORMAT UNIT (with Immed bit) */
#define Libburn_mmc_blank_timeouT            200000

/* SEND OPC INFORMATION */
#define Libburn_mmc_opc_timeouT              200000

/* MMC_SYNC_CACHE */
#define Libburn_mmc_sync_timeouT             200000

/* START STOP UNIT with Start bit and Load bit set */
#define Libburn_mmc_load_timeouT             300000

#endif /*__SPC*/
