/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Provided under GPL version 2 or later.
*/


#ifndef __TOC_H
#define __TOC_H

struct command;

#include "libburn.h"
#include "structure.h"

/* return if a given entry refers to a track position */
#define TOC_ENTRY_IS_TRACK(drive, entrynum) \
	((drive)->toc_entry[entrynum].point < 100)

/* return if a given entry is in audio or data format */
#define TOC_ENTRY_IS_AUDIO(drive, entrynum) \
	(~(drive)->toc_entry[entrynum].control & 4)

/* return the point value for a given entry number */
#define TOC_POINT(drive, entrynum) ((drive)->toc_entry[entrynum].point)

/* return the track struct for a given entry number */
#define TOC_TRACK(drive, entrynum) \
	((drive)->track[TOC_POINT(drive, entrynum) - 1])

/* return the lba of a toc entry */
#define TOC_ENTRY_PLBA(drive, entrynum) \
	burn_msf_to_lba((drive)->toc_entry[(entrynum)].pmin, \
	                   (drive)->toc_entry[(entrynum)].psec, \
	                   (drive)->toc_entry[(entrynum)].pframe)

/* flags for the q subchannel control field */
#define TOC_CONTROL_AUDIO                       (0)
#define TOC_CONTROL_DATA                        (1 << 2)
#define TOC_CONTROL_AUDIO_TWO_CHANNELS          (0)
#define TOC_CONTROL_AUDIO_FOUR_CHANNELS         (1 << 3)
#define TOC_CONTROL_AUDIO_PRE_EMPHASIS          (1 << 0)
#define TOC_CONTROL_DATA_RECORDED_UNINTERRUPTED (0)
#define TOC_CONTROL_DATA_RECORDED_INCREMENT     (1 << 0)
#define TOC_CONTROL_COPY_PROHIBITED             (0)
#define TOC_CONTROL_COPY_PERMITTED              (1 << 1)

/** read a sector from each track on disc to determine modes
    @param d The drive.
*/
void toc_find_modes(struct burn_drive *d);

#endif /*__TOC_H*/
