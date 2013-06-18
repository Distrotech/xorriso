/*
 * Copyright (c) 2007 Vreixo Formoso
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */
#ifndef LIBISO_IMAGE_TREE_H_
#define LIBISO_IMAGE_TREE_H_

#include "image.h"

/**
 * Recursively add a given directory to the image tree.
 * 
 * @return
 *      1 continue, 0 stop, < 0 error
 */
int iso_add_dir_src_rec(IsoImage *image, IsoDir *parent, IsoFileSource *dir);

#endif /*LIBISO_IMAGE_TREE_H_*/
