/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* scsi block commands */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2010 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <string.h>
#include <unistd.h>

#include "transport.h"
#include "sbc.h"
#include "spc.h"
#include "options.h"


/* ts A70910
   debug: for tracing calls which might use open drive fds
          or for catching SCSI usage of emulated drives. */
int mmc_function_spy(struct burn_drive *d, char * text);


/* START STOP UNIT as of SBC-1 and SBC-2
   0:  Opcode 0x1B
   1:  bit0= Immed
       bit1-7= reserved 
   2:  reserved
   3:  reserved
   4:  bit0= Start (else Stop unit)
       bit1= Load/Eject (according to Start resp. Stop)
       bit2-3= reserved
       bit4-7= Power Condition
               0= Start Valid: process Start and Load/Eject bits
               1= assume Active state
               2= assume Idle state
               3= assume Stanby state
              (5= SBC-1 only: assume Sleep state)
               7= transfer control of power conditions to logical unit
              10= force idle condition timer to 0
              11= force standby condition timer to 0
              All others are reserved.
   5:  Control (set to 0)
*/
static unsigned char SBC_LOAD[] = { 0x1b, 0, 0, 0, 3, 0 };
static unsigned char SBC_UNLOAD[] = { 0x1b, 0, 0, 0, 2, 0 };
static unsigned char SBC_START_UNIT[] = { 0x1b, 0, 0, 0, 1, 0 };
static unsigned char SBC_STOP_UNIT[] = { 0x1b, 0, 0, 0, 0, 0 };

void sbc_load(struct burn_drive *d)
{
	struct command *c;

	c = &(d->casual_command);
	if (mmc_function_spy(d, "load") <= 0)
		return;

	scsi_init_command(c, SBC_LOAD, sizeof(SBC_LOAD));
	c->retry = 1;

	/* ts A70921 : Had to revoke Immed because of LG GSA-4082B */
	/* c->opcode[1] |= 1; / * ts A70918 : Immed */

	c->dir = NO_TRANSFER;
	c->timeout = Libburn_mmc_load_timeouT;
	d->issue_command(d, c);
	if (c->error)
		return;
	/* ts A70923 : Needed regardless of Immed bit. Was once 1 minute, now
           5 minutes for loading. If this does not suffice then other commands
	   shall fail righteously. */
	spc_wait_unit_attention(d, 300, "waiting after START UNIT (+ LOAD)",0);
}

void sbc_eject(struct burn_drive *d)
{
	struct command *c;

	c = &(d->casual_command);
	if (mmc_function_spy(d, "eject") <= 0)
		return;

	scsi_init_command(c, SBC_UNLOAD, sizeof(SBC_UNLOAD));
	/* c->opcode[1] |= 1; / * ts A70918 : Immed , ts B00109 : revoked */
	c->page = NULL;
	c->dir = NO_TRANSFER;
	d->issue_command(d, c);
	/* ts A70918 : Wait long. A late eject could surprise or hurt user.
	   ts B00109 : Asynchronous eject revoked, as one cannot reliably
	               distinguish out from unready.
	if (c->error)
		return;
	spc_wait_unit_attention(d, 1800, "STOP UNIT (+ EJECT)", 0);
	*/
}


/* ts A91112 : Now with flag */
/* @param flag bit0= asynchronous waiting
*/
int sbc_start_unit_flag(struct burn_drive *d, int flag)
{
	struct command *c;
	int ret;

	c = &(d->casual_command);
	if (mmc_function_spy(d, "start_unit") <= 0)
		return 0;

	scsi_init_command(c, SBC_START_UNIT, sizeof(SBC_START_UNIT));
	c->retry = 1;
	c->opcode[1] |= (flag & 1); /* ts A70918 : Immed */
	c->dir = NO_TRANSFER;
	d->issue_command(d, c);
	if (c->error)
		return 0;
	if (!(flag & 1))
		return 1;
	/* ts A70918 : asynchronous */
	ret = spc_wait_unit_attention(d, 1800, "START UNIT", 0);
	return ret;
}


int sbc_start_unit(struct burn_drive *d)
{
	int ret;

	d->is_stopped = 0; /* no endless starting attempts */

	/* Asynchronous, not to block controller by waiting */
	ret = sbc_start_unit_flag(d, 1);
	if (ret <= 0)
		return ret;
	/* Synchronous to catch Pioneer DVR-216D which is ready too early.
	   A pending START UNIT can prevent ejecting of the tray.
	*/
	ret = sbc_start_unit_flag(d, 0);
	return ret;
}


/* ts A90824 : Trying to reduce drive noise */
int sbc_stop_unit(struct burn_drive *d)
{
	struct command *c;
	int ret;

	c = &(d->casual_command);
	if (mmc_function_spy(d, "stop_unit") <= 0)
		return 0;

	scsi_init_command(c, SBC_STOP_UNIT, sizeof(SBC_STOP_UNIT));
	c->retry = 0;
	c->opcode[1] |= 1; /* Immed */
	c->dir = NO_TRANSFER;
	d->issue_command(d, c);
	if (c->error)
		return 0;
	ret = spc_wait_unit_attention(d, 1800, "STOP UNIT", 0);
	d->is_stopped = 1;
	return ret;
}



/* ts A61021 : the sbc specific part of sg.c:enumerate_common()
*/
int sbc_setup_drive(struct burn_drive *d)
{
	d->eject = sbc_eject;
	d->load = sbc_load;
	d->start_unit = sbc_start_unit;
	d->stop_unit = sbc_stop_unit;
	d->is_stopped = 0;
	return 1;
}

