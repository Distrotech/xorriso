/*
 * Copyright (c) 2007 Vreixo Formoso
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "util.h"
#include "libisofs.h"

#include <stdlib.h>

/*
 * This implementation of Red-Black tree is based on the public domain 
 * implementation of Julienne Walker.
 */

struct iso_rbnode
{
    void *data;
    struct iso_rbnode *ch[2];
    unsigned int red :1;
};

struct iso_rbtree
{
    struct iso_rbnode *root;
    size_t size;
    int (*compare)(const void *a, const void *b);
};

/**
 * Create a new binary tree. libisofs binary trees allow you to add any data
 * passing it as a pointer. You must provide a function suitable for compare
 * two elements.
 *
 * @param compare
 *     A function to compare two elements. It takes a pointer to both elements
 *     and return 0, -1 or 1 if the first element is equal, less or greater 
 *     than the second one.
 * @param tree
 *     Location where the tree structure will be stored.
 */
int iso_rbtree_new(int (*compare)(const void*, const void*), IsoRBTree **tree)
{
    if (compare == NULL || tree == NULL) {
        return ISO_NULL_POINTER;
    }
    *tree = calloc(1, sizeof(IsoRBTree));
    if (*tree == NULL) {
        return ISO_OUT_OF_MEM;
    }
    (*tree)->compare = compare;
    return ISO_SUCCESS;
}

static
void rbtree_destroy_aux(struct iso_rbnode *root, void (*free_data)(void *))
{
    if (root == NULL) {
        return;
    }
    if (free_data != NULL) {
        free_data(root->data);
    }
    rbtree_destroy_aux(root->ch[0], free_data);
    rbtree_destroy_aux(root->ch[1], free_data);
    free(root);
}

/**
 * Destroy a given tree.
 * 
 * Note that only the structure itself is deleted. To delete the elements, you
 * should provide a valid free_data function. It will be called for each 
 * element of the tree, so you can use it to free any related data.
 */
void iso_rbtree_destroy(IsoRBTree *tree, void (*free_data)(void *))
{
    if (tree == NULL) {
        return;
    }
    rbtree_destroy_aux(tree->root, free_data);
    free(tree);
}

static inline
int is_red(struct iso_rbnode *root)
{
    return root != NULL && root->red;
}

static
struct iso_rbnode *iso_rbtree_single(struct iso_rbnode *root, int dir)
{
    struct iso_rbnode *save = root->ch[!dir];

    root->ch[!dir] = save->ch[dir];
    save->ch[dir] = root;

    root->red = 1;
    save->red = 0;
    return save;
}

static
struct iso_rbnode *iso_rbtree_double(struct iso_rbnode *root, int dir)
{
    root->ch[!dir] = iso_rbtree_single(root->ch[!dir], !dir);
    return iso_rbtree_single(root, dir);
}

static
struct iso_rbnode *iso_rbnode_new(void *data)
{
    struct iso_rbnode *rn = malloc(sizeof(struct iso_rbnode));

    if (rn != NULL) {
        rn->data = data;
        rn->red = 1;
        rn->ch[0] = NULL;
        rn->ch[1] = NULL;
    }

    return rn;
}

/**
 * Inserts a given element in a Red-Black tree.
 *
 * @param tree
 *     the tree where to insert
 * @param data
 *     element to be inserted on the tree. It can't be NULL
 * @param item
 *     if not NULL, it will point to a location where the tree element ptr 
 *     will be stored. If data was inserted, *item == data. If data was
 *     already on the tree, *item points to the previously inserted object
 *     that is equal to data.
 * @return
 *     1 success, 0 element already inserted, < 0 error
 */
int iso_rbtree_insert(IsoRBTree *tree, void *data, void **item)
{
    struct iso_rbnode *new;
    int added = 0; /* has a new node been added? */

    if (tree == NULL || data == NULL) {
        return ISO_NULL_POINTER;
    }

    if (tree->root == NULL) {
        /* Empty tree case */
        tree->root = iso_rbnode_new(data);
        if (tree->root == NULL) {
            return ISO_OUT_OF_MEM;
        }
        new = data;
        added = 1;
    } else {
        struct iso_rbnode head = { 0, {NULL, NULL}, 0 }; /* False tree root */

        struct iso_rbnode *g, *t; /* Grandparent & parent */
        struct iso_rbnode *p, *q; /* Iterator & parent */
        int dir = 0, last = 0;
        int comp;

        /* Set up helpers */
        t = &head;
        g = p = NULL;
        q = t->ch[1] = tree->root;

        /* Search down the tree */
        while (1) {
            if (q == NULL) {
                /* Insert new node at the bottom */
                p->ch[dir] = q = iso_rbnode_new(data);
                if (q == NULL) {
                    return ISO_OUT_OF_MEM;
                }
                added = 1;
            } else if (is_red(q->ch[0]) && is_red(q->ch[1])) {
                /* Color flip */
                q->red = 1;
                q->ch[0]->red = 0;
                q->ch[1]->red = 0;
            }

            /* Fix red violation */
            if (is_red(q) && is_red(p)) {
                int dir2 = (t->ch[1] == g);

                if (q == p->ch[last]) {
                    t->ch[dir2] = iso_rbtree_single(g, !last);
                } else {
                    t->ch[dir2] = iso_rbtree_double(g, !last);
                }
            }

            if (q->data == data) {
                comp = 0;
            } else {
                comp = tree->compare(q->data, data);
            }

            /* Stop if found */
            if (comp == 0) {
                new = q->data;
                break;
            }

            last = dir;
            dir = (comp < 0);

            /* Update helpers */
            if (g != NULL)
                t = g;
            g = p, p = q;
            q = q->ch[dir];
        }

        /* Update root */
        tree->root = head.ch[1];
    }

    /* Make root black */
    tree->root->red = 0;

    if (item != NULL) {
        *item = new;
    }
    if (added) {
        /* a new element has been added */
        tree->size++;
        return 1;
    } else {
        return 0;
    }
}

/**
 * Get the number of elements in a given tree.
 */
size_t iso_rbtree_get_size(IsoRBTree *tree)
{
    return tree->size;
}

static
size_t rbtree_to_array_aux(struct iso_rbnode *root, void **array, size_t pos,
                        int (*include_item)(void *))
{
    if (root == NULL) {
        return pos;
    }
    pos = rbtree_to_array_aux(root->ch[0], array, pos, include_item);
    if (include_item == NULL || include_item(root->data)) {
        array[pos++] = root->data;
    }
    pos = rbtree_to_array_aux(root->ch[1], array, pos, include_item);
    return pos;
}

/**
 * Get an array view of the elements of the tree.
 * 
 * @param include_item
 *    Function to select which elements to include in the array. It that takes
 *    a pointer to an element and returns 1 if the element should be included,
 *    0 if not. If you want to add all elements to the array, you can pass a
 *    NULL pointer.
 * @return
 *    A sorted array with the contents of the tree, or NULL if there is not
 *    enought memory to allocate the array. You should free(3) the array when
 *    no more needed. Note that the array is NULL-terminated, and thus it
 *    has size + 1 length.
 */
void ** iso_rbtree_to_array(IsoRBTree *tree, int (*include_item)(void *), 
                            size_t *size)
{
    size_t pos;
    void **array, **new_array;

    array = malloc((tree->size + 1) * sizeof(void*));
    if (array == NULL) {
        return NULL;
    }

    /* fill array */
    pos = rbtree_to_array_aux(tree->root, array, 0, include_item);
    array[pos] = NULL;

    new_array = realloc(array, (pos + 1) * sizeof(void*));
    if (new_array == NULL) {
        free((char *) array);
        return NULL;
    }
    array= new_array;
    if (size) {
        *size = pos;
    }
    return array;
}

