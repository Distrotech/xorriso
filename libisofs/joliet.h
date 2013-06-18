/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2007 Mario Danic
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

/**
 * Declare Joliet related structures.
 */

#ifndef LIBISO_JOLIET_H
#define LIBISO_JOLIET_H

#include "libisofs.h"
#include "ecma119.h"

/* was formerly 66 = 64 + 2. Now 105 = 103 + 2.
*/
#define LIBISO_JOLIET_NAME_MAX 105

enum joliet_node_type {
	JOLIET_FILE,
	JOLIET_DIR
};

struct joliet_dir_info {
    JolietNode **children;
	size_t nchildren;
	size_t len;
	size_t block;
};

struct joliet_node
{
	uint16_t *name; /**< Name in UCS-2BE. */

    JolietNode *parent;

    IsoNode *node; /*< reference to the iso node */

	enum joliet_node_type type;
	union {
	    IsoFileSrc *file;
		struct joliet_dir_info *dir;
	} info;
};

/**
 * Create a IsoWriter to deal with Joliet estructures, and add it to the given
 * target.
 * 
 * @return
 *      1 on success, < 0 on error
 */
int joliet_writer_create(Ecma119Image *target);


/* Not to be called but only for comparison with target->writers[i]
*/
int joliet_writer_write_vol_desc(IsoImageWriter *writer);


#endif /* LIBISO_JOLIET_H */
