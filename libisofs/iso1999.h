/*
 * Copyright (c) 2007 Vreixo Formoso
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

/**
 * Structures related to ISO/IEC 9660:1999, that is version 2 of ISO-9660 
 * "See doc/devel/cookbook/ISO 9660-1999" and
 * ISO/IEC DIS 9660:1999(E) "Information processing. Volume and file structure
 *                           of CD­-ROM for Information Interchange"
 * for further details.
 */

#ifndef LIBISO_ISO1999_H
#define LIBISO_ISO1999_H

#include "libisofs.h"
#include "ecma119.h"

enum iso1999_node_type {
	ISO1999_FILE,
	ISO1999_DIR
};

struct iso1999_dir_info {
    Iso1999Node **children;
	size_t nchildren;
	size_t len;
	size_t block;
};

struct iso1999_node
{
	char *name; /**< Name chosen output charset. */

	Iso1999Node *parent;

    IsoNode *node; /*< reference to the iso node */

	enum iso1999_node_type type;
	union {
	    IsoFileSrc *file;
		struct iso1999_dir_info *dir;
	} info;
};

/**
 * Create a IsoWriter to deal with ISO 9660:1999 estructures, and add it to 
 * the given target.
 * 
 * @return
 *      1 on success, < 0 on error
 */
int iso1999_writer_create(Ecma119Image *target);

#endif /* LIBISO_ISO1999_H */
