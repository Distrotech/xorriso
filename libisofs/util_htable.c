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
#include <string.h>

/*
 * Hash table implementation
 */

struct iso_hnode
{
    void *key;
    void *data;
    
    /** next node for chaining */
    struct iso_hnode *next;
};

struct iso_htable
{
    struct iso_hnode **table;

    size_t size; /**< number of items in table */
    size_t cap; /**< number of slots in table */
    
    hash_funtion_t hash;
    compare_function_t compare;
};

static
struct iso_hnode *iso_hnode_new(void *key, void *data)
{
    struct iso_hnode *node = malloc(sizeof(struct iso_hnode));
    if (node == NULL)
        return NULL;
    
    node->data = data;
    node->key = key;
    node->next = NULL;
    return node;
}

/**
 * Put an element in a Hash Table. The element will be identified by
 * the given key, that you should use to retrieve the element again.
 * 
 * This function allow duplicates, i.e., two items with the same key. In those
 * cases, the value returned by iso_htable_get() is undefined. If you don't
 * want to allow duplicates, use iso_htable_put() instead;
 * 
 * Both the key and data pointers will be stored internally, so you should
 * free the objects they point to. Use iso_htable_remove() to delete an 
 * element from the table.
 */
int iso_htable_add(IsoHTable *table, void *key, void *data)
{
    struct iso_hnode *node;
    struct iso_hnode *new;
    unsigned int hash;
    
    if (table == NULL || key == NULL) {
        return ISO_NULL_POINTER;
    }
    
    new = iso_hnode_new(key, data);
    if (new == NULL) {
        return ISO_OUT_OF_MEM;
    }
    
    hash = table->hash(key) % table->cap;
    node = table->table[hash];

    table->size++;
    new->next = node;
    table->table[hash] = new;
    return ISO_SUCCESS;
}

/**
 * Like iso_htable_add(), but this doesn't allow dulpicates.
 * 
 * @return
 *     1 success, 0 if an item with the same key already exists, < 0 error
 */
int iso_htable_put(IsoHTable *table, void *key, void *data)
{
    struct iso_hnode *node;
    struct iso_hnode *new;
    unsigned int hash;
    
    if (table == NULL || key == NULL) {
        return ISO_NULL_POINTER;
    }
    
    hash = table->hash(key) % table->cap;
    node = table->table[hash];

    while (node) {
        if (!table->compare(key, node->key)) {
            return 0;
        }
        node = node->next;
    }   
    
    new = iso_hnode_new(key, data);
    if (new == NULL) {
        return ISO_OUT_OF_MEM;
    }
    
    table->size++;
    new->next = table->table[hash];
    table->table[hash] = new;
    return ISO_SUCCESS;
}

/**
 * Retrieve an element from the given table. 
 * 
 * @param table
 *     Hash table
 * @param key
 *     Key of the element that will be removed
 * @param data
 *     Will be filled with the element found. Remains untouched if no
 *     element with the given key is found.
 * @return
 *      1 if found, 0 if not, < 0 on error
 */
int iso_htable_get(IsoHTable *table, void *key, void **data)
{
    struct iso_hnode *node;
    unsigned int hash;
    
    if (table == NULL || key == NULL) {
        return ISO_NULL_POINTER;
    }
    
    hash = table->hash(key) % table->cap;
    node = table->table[hash];
    while (node) {
        if (!table->compare(key, node->key)) {
            if (data) {
                *data = node->data;
            }
            return 1;
        }
        node = node->next;
    }
    return 0;
}

/**
 * Remove an item with the given key from the table. In tables that allow 
 * duplicates, it is undefined the element that will be deleted.
 * 
 * @param table
 *     Hash table
 * @param key
 *     Key of the element that will be removed
 * @param free_data
 *     Function that will be called passing as parameters both the key and 
 *     the element that will be deleted. The user can use it to free the
 *     element. You can pass NULL if you don't want to delete the item itself.
 * @return
 *     1 success, 0 no element exists with the given key, < 0 error
 */
int iso_htable_remove(IsoHTable *table, void *key, hfree_data_t free_data)
{
    struct iso_hnode *node, *prev;
    unsigned int hash;
    
    if (table == NULL || key == NULL) {
        return ISO_NULL_POINTER;
    }
    
    hash = table->hash(key) % table->cap;
    node = table->table[hash];
    prev = NULL;
    while (node) {
        if (!table->compare(key, node->key)) {
            if (free_data)
                free_data(node->key, node->data);
            if (prev) {
                prev->next = node->next;
            } else {
                table->table[hash] = node->next;
            }
            free(node);
            table->size--;
            return 1;
        }
        prev = node;
        node = node->next;
    }
    return 0;
}

/**
 * Like remove, but instead of checking for key equality using the compare
 * function, it just compare the key pointers. If the table allows duplicates,
 * and you provide different keys (i.e. different pointers) to elements 
 * with same key (i.e. same content), this function ensure the exact element
 * is removed. 
 * 
 * It has the problem that you must provide the same key pointer, and not just
 * a key whose contents are equal. Moreover, if you use the same key (same
 * pointer) to identify several objects, what of those are removed is 
 * undefined.
 * 
 * @param table
 *     Hash table
 * @param key
 *     Key of the element that will be removed
 * @param free_data
 *     Function that will be called passing as parameters both the key and 
 *     the element that will be deleted. The user can use it to free the
 *     element. You can pass NULL if you don't want to delete the item itself.
 * @return
 *     1 success, 0 no element exists with the given key, < 0 error
 */
int iso_htable_remove_ptr(IsoHTable *table, void *key, hfree_data_t free_data)
{
    struct iso_hnode *node, *prev;
    unsigned int hash;
    
    if (table == NULL || key == NULL) {
        return ISO_NULL_POINTER;
    }
    
    hash = table->hash(key) % table->cap;
    node = table->table[hash];
    prev = NULL;
    while (node) {
        if (key == node->key) {
            if (free_data)
                free_data(node->key, node->data);
            if (prev) {
                prev->next = node->next;
            } else {
                table->table[hash] = node->next;
            }
            free(node);
            table->size--;
            return 1;
        }
        prev = node;
        node = node->next;
    }
    return 0;
}

/**
 * Hash function suitable for keys that are char strings.
 */
unsigned int iso_str_hash(const void *key)
{
    int i, len;
    const char *p = key;
    unsigned int h = 2166136261u;

    len = strlen(p);
    for (i = 0; i < len; i++)
        h = (h * 16777619 ) ^ p[i];

    return h;
}

/**
 * Destroy the given hash table.
 * 
 * Note that you're responsible to actually destroy the elements by providing
 * a valid free_data function. You can pass NULL if you only want to delete
 * the hash structure.
 */
void iso_htable_destroy(IsoHTable *table, hfree_data_t free_data)
{
    size_t i;
    struct iso_hnode *node, *tmp;
    
    if (table == NULL) {
        return;
    }
    
    for (i = 0; i < table->cap; ++i) {
        node = table->table[i];
        while (node) {
            tmp = node->next;
            if (free_data)
                free_data(node->key, node->data);
            free(node);
            node = tmp;
        }
    }
    free(table->table);
    free(table);
}

/**
 * Create a new hash table.
 * 
 * @param size
 *     Number of slots in table.
 * @param hash
 *     Function used to generate
 */
int iso_htable_create(size_t size, hash_funtion_t hash, 
                      compare_function_t compare, IsoHTable **table)
{
    IsoHTable *t;
    
    if (table == NULL) {
        return ISO_OUT_OF_MEM;
    }
    
    t = malloc(sizeof(IsoHTable));
    if (t == NULL) {
        return ISO_OUT_OF_MEM;
    }
    t->table = calloc(size, sizeof(void*));
    if (t->table == NULL) {
        free(t);
        return ISO_OUT_OF_MEM;
    }
    t->cap = size;
    t->size = 0;
    t->hash = hash;
    t->compare = compare;

    *table = t;
    return ISO_SUCCESS;
}
