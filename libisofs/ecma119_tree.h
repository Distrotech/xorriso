/*
 * Copyright (c) 2007 Vreixo Formoso
 *               2012 Thomas Schmitt
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

#ifndef LIBISO_ECMA119_TREE_H_
#define LIBISO_ECMA119_TREE_H_

#include "libisofs.h"
#include "ecma119.h"

enum ecma119_node_type {
    ECMA119_FILE,
    ECMA119_DIR,
    ECMA119_SYMLINK,
    ECMA119_SPECIAL,
    ECMA119_PLACEHOLDER
};

/**
 * Struct with info about a node representing a directory
 */
struct ecma119_dir_info
{
    /* Block where the directory entries will be written on image */
    size_t block;

    size_t nchildren;
    Ecma119Node **children;

    /* 
     * Size of the dir, i.e., sum of the lengths of all directory records.
     * It is computed by calc_dir_size() [ecma119.c].
     * Note that this don't include the length of any SUSP Continuation
     * Area needed by the dir, but it includes the size of the SUSP entries
     * than fit in the directory records System Use Field.
     */
    size_t len;

    /** 
     * Real parent if the dir has been reallocated. NULL otherwise.
     */
    Ecma119Node *real_parent;
};

/**
 * A node for a tree containing all the information necessary for writing
 * an ISO9660 volume.
 */
struct ecma119_node
{
    /**
     * Name in ASCII, conforming to selected ISO level.
     * Version number is not include, it is added on the fly
     */
    char *iso_name;

    Ecma119Node *parent;

    IsoNode *node; /*< reference to the iso node */

    /* >>> ts A90501 : Shouldn't this be uint32_t
                       as this is what PX will take ? */
    ino_t ino;

    nlink_t nlink;

    /**< file, symlink, special, directory or placeholder */
    enum ecma119_node_type type;
    union
    {
        IsoFileSrc *file;
        struct ecma119_dir_info *dir;
        /** this field points to the relocated directory. */
        Ecma119Node *real_me;
    } info;
};

/**
 * 
 */
int ecma119_tree_create(Ecma119Image *img);

/**
 * Free an Ecma119Node, and its children if node is a dir
 */
void ecma119_node_free(Ecma119Node *node);

/**
 * Search the tree for a certain IsoNode and return its owning Ecma119Node
 * or NULL.
 */
Ecma119Node *ecma119_search_iso_node(Ecma119Image *img, IsoNode *node);

/**
 * Tell whether node is a dedicated relocation directory which only contains
 * relocated directories.
 */
int ecma119_is_dedicated_reloc_dir(Ecma119Image *img, Ecma119Node *node);


#endif /*LIBISO_ECMA119_TREE_H_*/
