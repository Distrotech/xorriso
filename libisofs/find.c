/*
 * Copyright (c) 2008 Vreixo Formoso
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "libisofs.h"
#include "node.h"

#include <fnmatch.h>
#include <string.h>

struct iso_find_condition
{
    /*
     * Check whether the given node matches this condition.
     * 
     * @param cond
     *      The condition to check
     * @param node
     *      The node that should be checked
     * @return
     *      1 if the node matches the condition, 0 if not
     */
    int (*matches)(IsoFindCondition *cond, IsoNode *node);
    
    /**
     * Free condition specific data
     */
    void (*free)(IsoFindCondition*);
    
    /** condition specific data */
    void *data;
};

struct find_iter_data
{
    IsoDir *dir; /**< original dir of the iterator */
    IsoDirIter *iter;
    IsoDirIter *itersec; /**< iterator to deal with child dirs */
    IsoFindCondition *cond;
    int err; /**< error? */
    IsoNode *current; /**< node to be returned next */
    IsoNode *prev; /**< last returned node, needed for removal */
    int free_cond; /**< whether to free cond on iter_free */ 
};

static 
int get_next(struct find_iter_data *iter, IsoNode **n)
{
    int ret;
    
    if (iter->itersec != NULL) {
        ret = iso_dir_iter_next(iter->itersec, n);
        if (ret <= 0) {
            /* secondary item no more needed */
            iso_dir_iter_free(iter->itersec);
            iter->itersec = NULL;
        }
        if (ret != 0) {
            /* succes or error */
            return ret;
        }
    }
    
    /* 
     * we reach here if:
     * - no secondary item is present
     * - secondary item has no more items
     */

    while ((ret = iso_dir_iter_next(iter->iter, n)) == 1) {
        if (iter->cond->matches(iter->cond, *n)) {
            return ISO_SUCCESS;
        } else if (ISO_NODE_IS_DIR(*n)) {
            /* recurse on child dir */
            struct find_iter_data *data;
            ret = iso_dir_find_children((IsoDir*)*n, iter->cond, 
                                        &iter->itersec);
            if (ret < 0) {
                return ret;
            }
            data = iter->itersec->data;
            data->free_cond = 0; /* we don't need sec iter to free cond */
            return get_next(iter, n);
        }
    }
    return ret;
}

static
void update_next(IsoDirIter *iter)
{
    int ret;
    IsoNode *n;
    struct find_iter_data *data = iter->data;

    if (data->prev) {
        iso_node_unref(data->prev);
    }
    data->prev = data->current;
    
    if (data->itersec == NULL && data->current != NULL 
            && ISO_NODE_IS_DIR(data->current)) {
        
        /* we need to recurse on child dir */
        struct find_iter_data *data2;
        ret = iso_dir_find_children((IsoDir*)data->current, data->cond, 
                                    &data->itersec);
        if (ret < 0) {
            data->current = NULL;
            data->err = ret;
            return;
        }
        data2 = data->itersec->data;
        data2->free_cond = 0; /* we don't need sec iter to free cond */
    }
    
    ret = get_next(data, &n);
    iso_node_unref((IsoNode*)iter->dir);
    if (ret == 1) {
        data->current = n;
        iso_node_ref(n);
        data->err = 0;
        iter->dir = n->parent;
    } else {
        data->current = NULL;
        data->err = ret;
        iter->dir = data->dir;
    }
    iso_node_ref((IsoNode*)iter->dir);
}

static
int find_iter_next(IsoDirIter *iter, IsoNode **node)
{
    struct find_iter_data *data;
    
    if (iter == NULL || node == NULL) {
        return ISO_NULL_POINTER;
    }
    data = iter->data;
    
    if (data->err < 0) {
        return data->err;
    }
    *node = data->current;
    update_next(iter);
    return (*node == NULL) ? 0 : ISO_SUCCESS;
}

static
int find_iter_has_next(IsoDirIter *iter)
{
    struct find_iter_data *data = iter->data;
    
    return (data->current != NULL);
}

static
void find_iter_free(IsoDirIter *iter)
{
    struct find_iter_data *data = iter->data;
    if (data->free_cond) {
        data->cond->free(data->cond);
        free(data->cond);
    }
    
    iso_node_unref((IsoNode*)data->dir);

    /* free refs to nodes */
    if (data->prev) {
        iso_node_unref(data->prev);
    }
    if (data->current) {
        iso_node_unref(data->current);
    }

    /* free underlying iter */
    iso_dir_iter_free(data->iter);
    free(iter->data);
}

static
int find_iter_take(IsoDirIter *iter)
{
    struct find_iter_data *data = iter->data;

    if (data->prev == NULL) {
        return ISO_ERROR; /* next not called or end of dir */
    }
    return iso_node_take(data->prev);
}

static
int find_iter_remove(IsoDirIter *iter)
{
    struct find_iter_data *data = iter->data;

    if (data->prev == NULL) {
        return ISO_ERROR; /* next not called or end of dir */
    }
    return iso_node_remove(data->prev);
}

void find_notify_child_taken(IsoDirIter *iter, IsoNode *node)
{
    struct find_iter_data *data = iter->data;
    
    if (data->prev == node) {
        /* free our ref */
        iso_node_unref(node);
        data->prev = NULL;
    } else if (data->current == node) {
        iso_node_unref(node);
        data->current = NULL;
        update_next(iter);
    }
}

static
struct iso_dir_iter_iface find_iter_class = {
        find_iter_next,
        find_iter_has_next,
        find_iter_free,
        find_iter_take,
        find_iter_remove,
        find_notify_child_taken
};

int iso_dir_find_children(IsoDir* dir, IsoFindCondition *cond, 
                          IsoDirIter **iter)
{
    int ret;
    IsoDirIter *children;
    IsoDirIter *it;
    struct find_iter_data *data;

    if (dir == NULL || cond == NULL || iter == NULL) {
        return ISO_NULL_POINTER;
    }
    it = malloc(sizeof(IsoDirIter));
    if (it == NULL) {
        return ISO_OUT_OF_MEM;
    }
    data = malloc(sizeof(struct find_iter_data));
    if (data == NULL) {
        free(it);
        return ISO_OUT_OF_MEM;
    }
    ret = iso_dir_get_children(dir, &children);
    if (ret < 0) {
        free(it);
        free(data);
        return ret;
    }

    it->class = &find_iter_class;
    it->dir = (IsoDir*)dir;
    data->iter = children;
    data->itersec = NULL;
    data->cond = cond;
    data->free_cond = 1;
    data->err = 0;
    data->prev = data->current = NULL;
    it->data = data;
    
    if (iso_dir_iter_register(it) < 0) {
        free(it);
        return ISO_OUT_OF_MEM;
    }

    iso_node_ref((IsoNode*)dir);
    
    /* take another ref to the original dir */
    data->dir = (IsoDir*)dir;
    iso_node_ref((IsoNode*)dir);

    update_next(it);
    
    *iter = it;
    return ISO_SUCCESS;
}

/*************** find by name wildcard condition *****************/

static
int cond_name_matches(IsoFindCondition *cond, IsoNode *node)
{
    char *pattern = (char*) cond->data;
    int ret = fnmatch(pattern, node->name, 0);
    return ret == 0 ? 1 : 0;
}

static
void cond_name_free(IsoFindCondition *cond)
{
    free(cond->data);
}

/**
 * Create a new condition that checks if the node name matches the given
 * wildcard.
 * 
 * @param wildcard
 * @result
 *      The created IsoFindCondition, NULL on error.
 * 
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_name(const char *wildcard)
{
    IsoFindCondition *cond;
    if (wildcard == NULL) {
        return NULL;
    }
    cond = malloc(sizeof(IsoFindCondition));
    if (cond == NULL) {
        return NULL;
    }
    cond->data = strdup(wildcard);
    cond->free = cond_name_free;
    cond->matches = cond_name_matches;
    return cond;
}

/*************** find by mode condition *****************/

static
int cond_mode_matches(IsoFindCondition *cond, IsoNode *node)
{
    mode_t *mask = (mode_t*) cond->data;
    return node->mode & *mask ? 1 : 0;
}

static
void cond_mode_free(IsoFindCondition *cond)
{
    free(cond->data);
}

/**
 * Create a new condition that checks the node mode against a mode mask. It
 * can be used to check both file type and permissions.
 * 
 * For example:
 * 
 * iso_new_find_conditions_mode(S_IFREG) : search for regular files
 * iso_new_find_conditions_mode(S_IFCHR | S_IWUSR) : search for character 
 *     devices where owner has write permissions.
 * 
 * @param mask
 *      Mode mask to AND against node mode.
 * @result
 *      The created IsoFindCondition, NULL on error.
 * 
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_mode(mode_t mask)
{
    IsoFindCondition *cond;
    mode_t *data;
    cond = malloc(sizeof(IsoFindCondition));
    if (cond == NULL) {
        return NULL;
    }
    data = malloc(sizeof(mode_t));
    if (data == NULL) {
        free(cond);
        return NULL;
    }
    *data = mask;
    cond->data = data;
    cond->free = cond_mode_free;
    cond->matches = cond_mode_matches;
    return cond;
}

/*************** find by gid condition *****************/

static
int cond_gid_matches(IsoFindCondition *cond, IsoNode *node)
{
    gid_t *gid = (gid_t*) cond->data;
    return node->gid == *gid ? 1 : 0;
}

static
void cond_gid_free(IsoFindCondition *cond)
{
    free(cond->data);
}

/**
 * Create a new condition that checks the node gid.
 * 
 * @param gid
 *      Desired Group Id.
 * @result
 *      The created IsoFindCondition, NULL on error.
 * 
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_gid(gid_t gid)
{
    IsoFindCondition *cond;
    gid_t *data;
    cond = malloc(sizeof(IsoFindCondition));
    if (cond == NULL) {
        return NULL;
    }
    data = malloc(sizeof(gid_t));
    if (data == NULL) {
        free(cond);
        return NULL;
    }
    *data = gid;
    cond->data = data;
    cond->free = cond_gid_free;
    cond->matches = cond_gid_matches;
    return cond;
}

/*************** find by uid condition *****************/

static
int cond_uid_matches(IsoFindCondition *cond, IsoNode *node)
{
    uid_t *uid = (uid_t*) cond->data;
    return node->uid == *uid ? 1 : 0;
}

static
void cond_uid_free(IsoFindCondition *cond)
{
    free(cond->data);
}

/**
 * Create a new condition that checks the node uid.
 * 
 * @param uid
 *      Desired User Id.
 * @result
 *      The created IsoFindCondition, NULL on error.
 * 
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_uid(uid_t uid)
{
    IsoFindCondition *cond;
    uid_t *data;
    cond = malloc(sizeof(IsoFindCondition));
    if (cond == NULL) {
        return NULL;
    }
    data = malloc(sizeof(uid_t));
    if (data == NULL) {
        free(cond);
        return NULL;
    }
    *data = uid;
    cond->data = data;
    cond->free = cond_uid_free;
    cond->matches = cond_uid_matches;
    return cond;
}

/*************** find by timestamps condition *****************/

struct cond_times
{
    time_t time;
    int what_time; /* 0 atime, 1 mtime, 2 ctime */
    enum iso_find_comparisons comparison;
};

static
int cond_time_matches(IsoFindCondition *cond, IsoNode *node)
{
    time_t node_time;
    struct cond_times *data = cond->data;
    
    switch (data->what_time) {
    case 0: node_time = node->atime; break;
    case 1: node_time = node->mtime; break;
    default: node_time = node->ctime; break;
    }
    
    switch (data->comparison) {
    case ISO_FIND_COND_GREATER:
        return node_time > data->time ? 1 : 0;
    case ISO_FIND_COND_GREATER_OR_EQUAL:
        return node_time >= data->time ? 1 : 0;
    case ISO_FIND_COND_EQUAL:
        return node_time == data->time ? 1 : 0;
    case ISO_FIND_COND_LESS:
        return node_time < data->time ? 1 : 0;
    case ISO_FIND_COND_LESS_OR_EQUAL:
        return node_time <= data->time ? 1 : 0;
    }
    /* should never happen */
    return 0;
}

static
void cond_time_free(IsoFindCondition *cond)
{
    free(cond->data);
}

/**
 * Create a new condition that checks the time of last access.
 * 
 * @param time
 *      Time to compare against IsoNode atime.
 * @param comparison
 *      Comparison to be done between IsoNode atime and submitted time.
 *      Note that ISO_FIND_COND_GREATER, for example, is true if the node
 *      time is greater than the submitted time.
 * @result
 *      The created IsoFindCondition, NULL on error.
 * 
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_atime(time_t time, 
                      enum iso_find_comparisons comparison)
{
    IsoFindCondition *cond;
    struct cond_times *data;
    cond = malloc(sizeof(IsoFindCondition));
    if (cond == NULL) {
        return NULL;
    }
    data = malloc(sizeof(struct cond_times));
    if (data == NULL) {
        free(cond);
        return NULL;
    }
    data->time = time;
    data->comparison = comparison;
    data->what_time = 0; /* atime */
    cond->data = data;
    cond->free = cond_time_free;
    cond->matches = cond_time_matches;
    return cond;
}

/**
 * Create a new condition that checks the time of last modification.
 * 
 * @param time
 *      Time to compare against IsoNode mtime.
 * @param comparison
 *      Comparison to be done between IsoNode mtime and submitted time.
 *      Note that ISO_FIND_COND_GREATER, for example, is true if the node
 *      time is greater than the submitted time.
 * @result
 *      The created IsoFindCondition, NULL on error.
 * 
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_mtime(time_t time, 
                      enum iso_find_comparisons comparison)
{
    IsoFindCondition *cond;
    struct cond_times *data;
    cond = malloc(sizeof(IsoFindCondition));
    if (cond == NULL) {
        return NULL;
    }
    data = malloc(sizeof(struct cond_times));
    if (data == NULL) {
        free(cond);
        return NULL;
    }
    data->time = time;
    data->comparison = comparison;
    data->what_time = 1; /* mtime */
    cond->data = data;
    cond->free = cond_time_free;
    cond->matches = cond_time_matches;
    return cond;
}

/**
 * Create a new condition that checks the time of last status change.
 * 
 * @param time
 *      Time to compare against IsoNode ctime.
 * @param comparison
 *      Comparison to be done between IsoNode ctime and submitted time.
 *      Note that ISO_FIND_COND_GREATER, for example, is true if the node
 *      time is greater than the submitted time.
 * @result
 *      The created IsoFindCondition, NULL on error.
 * 
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_ctime(time_t time, 
                      enum iso_find_comparisons comparison)
{
    IsoFindCondition *cond;
    struct cond_times *data;
    cond = malloc(sizeof(IsoFindCondition));
    if (cond == NULL) {
        return NULL;
    }
    data = malloc(sizeof(struct cond_times));
    if (data == NULL) {
        free(cond);
        return NULL;
    }
    data->time = time;
    data->comparison = comparison;
    data->what_time = 2; /* ctime */
    cond->data = data;
    cond->free = cond_time_free;
    cond->matches = cond_time_matches;
    return cond;
}

/*************** logical operations on conditions *****************/

struct logical_binary_conditions {
    IsoFindCondition *a;
    IsoFindCondition *b;
};

static
void cond_logical_binary_free(IsoFindCondition *cond)
{
    struct logical_binary_conditions *data;
    data = cond->data;
    data->a->free(data->a);
    free(data->a);
    data->b->free(data->b);
    free(data->b);
    free(cond->data);
}

static
int cond_logical_and_matches(IsoFindCondition *cond, IsoNode *node)
{
    struct logical_binary_conditions *data = cond->data;
    return data->a->matches(data->a, node) && data->b->matches(data->b, node);
}

/**
 * Create a new condition that check if the two given conditions are
 * valid.
 * 
 * @param a
 * @param b
 *      IsoFindCondition to compare
 * @result
 *      The created IsoFindCondition, NULL on error.
 * 
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_and(IsoFindCondition *a, 
                                              IsoFindCondition *b)
{
    IsoFindCondition *cond;
    struct logical_binary_conditions *data;
    cond = malloc(sizeof(IsoFindCondition));
    if (cond == NULL) {
        return NULL;
    }
    data = malloc(sizeof(struct logical_binary_conditions));
    if (data == NULL) {
        free(cond);
        return NULL;
    }
    data->a = a;
    data->b = b;
    cond->data = data;
    cond->free = cond_logical_binary_free;
    cond->matches = cond_logical_and_matches;
    return cond;
}

static
int cond_logical_or_matches(IsoFindCondition *cond, IsoNode *node)
{
    struct logical_binary_conditions *data = cond->data;
    return data->a->matches(data->a, node) || data->b->matches(data->b, node);
}

/**
 * Create a new condition that check if at least one the two given conditions 
 * is valid.
 * 
 * @param a
 * @param b
 *      IsoFindCondition to compare
 * @result
 *      The created IsoFindCondition, NULL on error.
 * 
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_or(IsoFindCondition *a, 
                                              IsoFindCondition *b)
{
    IsoFindCondition *cond;
    struct logical_binary_conditions *data;
    cond = malloc(sizeof(IsoFindCondition));
    if (cond == NULL) {
        return NULL;
    }
    data = malloc(sizeof(struct logical_binary_conditions));
    if (data == NULL) {
        free(cond);
        return NULL;
    }
    data->a = a;
    data->b = b;
    cond->data = data;
    cond->free = cond_logical_binary_free;
    cond->matches = cond_logical_or_matches;
    return cond;
}

static
void cond_not_free(IsoFindCondition *cond)
{
    IsoFindCondition *negate = cond->data;
    negate->free(negate);
    free(negate);
}

static
int cond_not_matches(IsoFindCondition *cond, IsoNode *node)
{
    IsoFindCondition *negate = cond->data;
    return !(negate->matches(negate, node));
}

/**
 * Create a new condition that check if the given conditions is false.
 * 
 * @param negate
 * @result
 *      The created IsoFindCondition, NULL on error.
 * 
 * @since 0.6.4
 */
IsoFindCondition *iso_new_find_conditions_not(IsoFindCondition *negate)
{
    IsoFindCondition *cond;
    cond = malloc(sizeof(IsoFindCondition));
    if (cond == NULL) {
        return NULL;
    }
    cond->data = negate;
    cond->free = cond_not_free;
    cond->matches = cond_not_matches;
    return cond;
}

