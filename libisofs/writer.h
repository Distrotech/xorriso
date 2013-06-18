/*
 * Copyright (c) 2007 Vreixo Formoso
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */
#ifndef LIBISO_IMAGE_WRITER_H_
#define LIBISO_IMAGE_WRITER_H_

#include "ecma119.h"

struct Iso_Image_Writer
{
    /**
     * 
     */
    int (*compute_data_blocks)(IsoImageWriter *writer);

    int (*write_vol_desc)(IsoImageWriter *writer);

    int (*write_data)(IsoImageWriter *writer);

    int (*free_data)(IsoImageWriter *writer);

    void *data;
    Ecma119Image *target;
};

/**
 * This is the function all Writers shoudl call to write data to image.
 * Currently, it is just a wrapper for write(2) Unix system call. 
 * 
 * It is implemented in ecma119.c
 * 
 * @return
 *      1 on sucess, < 0 error
 */
int iso_write(Ecma119Image *target, void *buf, size_t count);

int ecma119_writer_create(Ecma119Image *target);

#endif /*LIBISO_IMAGE_WRITER_H_*/
