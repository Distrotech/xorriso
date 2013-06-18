/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2009 - 2011 Thomas Schmitt
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
#include "image.h"
#include "node.h"
#include "stream.h"
#include "aaip_0_2.h"
#include "messages.h"
#include "util.h"
#include "eltorito.h"


#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <stdio.h>


struct dir_iter_data
{
    /* points to the last visited child, to NULL before start */
    IsoNode *pos;

    /* Some control flags.
     * bit 0 -> 1 if next called, 0 reseted at start or on deletion
     */
    int flag;
};

/**
 * Increments the reference counting of the given node.
 */
void iso_node_ref(IsoNode *node)
{
    ++node->refcount;
}

/**
 * Decrements the reference couting of the given node.
 * If it reach 0, the node is free, and, if the node is a directory,
 * its children will be unref() too.
 */
void iso_node_unref(IsoNode *node)
{
    if (node == NULL)
        return;
    if (--node->refcount == 0) {
        switch (node->type) {
        case LIBISO_DIR:
            {
                IsoNode *child = ((IsoDir*)node)->children;
                while (child != NULL) {
                    IsoNode *tmp = child->next;
                    child->parent = NULL;
                    iso_node_unref(child);
                    child = tmp;
                }
            }
            break;
        case LIBISO_FILE:
            {
                IsoFile *file = (IsoFile*) node;
                iso_stream_unref(file->stream);
            }
            break;
        case LIBISO_SYMLINK:
            {
                IsoSymlink *link = (IsoSymlink*) node;
                free(link->dest);
            }
            break;
        case LIBISO_BOOT:
            {
                IsoBoot *bootcat = (IsoBoot *) node;
                if (bootcat->content != NULL)
                    free(bootcat->content);
            }
            break;
        default:
            /* other kind of nodes does not need to delete anything here */
            break;
        }

        if (node->xinfo) {
            IsoExtendedInfo *info = node->xinfo;
            while (info != NULL) {
                IsoExtendedInfo *tmp = info->next;

                /* free extended info */
                info->process(info->data, 1);
                free(info);
                info = tmp;
            }
        }
        free(node->name);
        free(node);
    }
}

/**
 * Add extended information to the given node. Extended info allows
 * applications (and libisofs itself) to add more information to an IsoNode.
 * You can use this facilities to associate new information with a given
 * node.
 *
 * Each node keeps a list of added extended info, meaning you can add several
 * extended info data to each node. Each extended info you add is identified
 * by the proc parameter, a pointer to a function that knows how to manage
 * the external info data. Thus, in order to add several types of extended
 * info, you need to define a "proc" function for each type.
 *
 * @param node
 *      The node where to add the extended info
 * @param proc
 *      A function pointer used to identify the type of the data, and that
 *      knows how to manage it
 * @param data
 *      Extended info to add.
 * @return
 *      1 if success, 0 if the given node already has extended info of the
 *      type defined by the "proc" function, < 0 on error
 */
int iso_node_add_xinfo(IsoNode *node, iso_node_xinfo_func proc, void *data)
{
    IsoExtendedInfo *info;
    IsoExtendedInfo *pos;

    if (node == NULL || proc == NULL) {
        return ISO_NULL_POINTER;
    }

    pos = node->xinfo;
    while (pos != NULL) {
        if (pos->process == proc) {
            return 0; /* extended info already added */
        }
        pos = pos->next;
    }

    info = malloc(sizeof(IsoExtendedInfo));
    if (info == NULL) {
        return ISO_OUT_OF_MEM;
    }
    info->next = node->xinfo;
    info->data = data;
    info->process = proc;
    node->xinfo = info;
    return ISO_SUCCESS;
}

/**
 * Remove the given extended info (defined by the proc function) from the
 * given node.
 *
 * @return
 *      1 on success, 0 if node does not have extended info of the requested
 *      type, < 0 on error
 */
int iso_node_remove_xinfo(IsoNode *node, iso_node_xinfo_func proc)
{
    IsoExtendedInfo *pos, *prev;

    if (node == NULL || proc == NULL) {
        return ISO_NULL_POINTER;
    }

    prev = NULL;
    pos = node->xinfo;
    while (pos != NULL) {
        if (pos->process == proc) {
            /* this is the extended info we want to remove */
            pos->process(pos->data, 1);

            if (prev != NULL) {
                prev->next = pos->next;
            } else {
                node->xinfo = pos->next;
            }
            free(pos);
            return ISO_SUCCESS;
        }
        prev = pos;
        pos = pos->next;
    }
    /* requested xinfo not found */
    return 0;
}

/**
 * Get the given extended info (defined by the proc function) from the
 * given node.
 *
 * @param data
 *      Will be filled with the extended info corresponding to the given proc
 *      function
 * @return
 *      1 on success, 0 if node does not have extended info of the requested
 *      type, < 0 on error
 */
int iso_node_get_xinfo(IsoNode *node, iso_node_xinfo_func proc, void **data)
{
    IsoExtendedInfo *pos;

    if (node == NULL || proc == NULL || data == NULL) {
        return ISO_NULL_POINTER;
    }

    *data = NULL;
    pos = node->xinfo;
    while (pos != NULL) {
        if (pos->process == proc) {
            /* this is the extended info we want */
            *data = pos->data;
            return ISO_SUCCESS;
        }
        pos = pos->next;
    }
    /* requested xinfo not found */
    return 0;
}

/* API */
int iso_node_get_next_xinfo(IsoNode *node, void **handle,
                            iso_node_xinfo_func *proc, void **data)
{
    IsoExtendedInfo *xinfo;

    if (node == NULL || handle == NULL || proc == NULL || data == NULL)
        return ISO_NULL_POINTER;
    *proc = NULL;
    *data = NULL;
    xinfo = (IsoExtendedInfo *) *handle;
    if (xinfo == NULL)
        xinfo = node->xinfo;
    else
        xinfo = xinfo->next;
    *handle = xinfo;
    if (xinfo == NULL)
        return 0;
    *proc = xinfo->process;
    *data = xinfo->data;
    return ISO_SUCCESS;
}

int iso_node_remove_all_xinfo(IsoNode *node, int flag)
{
    IsoExtendedInfo *pos, *next;

    for (pos = node->xinfo; pos != NULL; pos = next) {
        next = pos->next;
        pos->process(pos->data, 1);
        free((char *) pos);
    }
    node->xinfo = NULL;
    return ISO_SUCCESS;
}

static
int iso_node_revert_xinfo_list(IsoNode *node, int flag)
{

    IsoExtendedInfo *pos, *next, *prev = NULL;

    for (pos = node->xinfo; pos != NULL; pos = next) {
        next = pos->next;
        pos->next = prev;
        prev = pos;
    }
    node->xinfo = prev;
    return ISO_SUCCESS;
}

int iso_node_clone_xinfo(IsoNode *from_node, IsoNode *to_node, int flag)
{
    void *handle = NULL, *data, *new_data;
    iso_node_xinfo_func proc;
    iso_node_xinfo_cloner cloner;
    int ret;

    iso_node_remove_all_xinfo(to_node, 0);
    while (1) {
        ret = iso_node_get_next_xinfo(from_node, &handle, &proc, &data);
        if (ret <= 0)
    break;
        ret = iso_node_xinfo_get_cloner(proc, &cloner, 0);
        if (ret == 0)
            return ISO_XINFO_NO_CLONE;
        if (ret < 0)
            return ret;
        ret = (*cloner)(data, &new_data, 0);
        if (ret < 0)
    break;
        ret = iso_node_add_xinfo(to_node, proc, new_data);
        if (ret < 0)
    break;
    }
    if (ret < 0) {
        iso_node_remove_all_xinfo(to_node, 0);
    } else {
        ret = iso_node_revert_xinfo_list(to_node, 0);
    }
    return ret;
}

/**
 * Get the type of an IsoNode.
 */
enum IsoNodeType iso_node_get_type(IsoNode *node)
{
    return node->type;
}

/**
 * Set the name of a node.
 *
 * @param name  The name in UTF-8 encoding
 */
int iso_node_set_name(IsoNode *node, const char *name)
{
    char *new;
    int ret;

    if ((IsoNode*)node->parent == node) {
        /* you can't change name of the root node */
        return ISO_WRONG_ARG_VALUE;
    }

    /* check if the name is valid */
    ret = iso_node_is_valid_name(name);
    if (ret < 0)
        return ret;

    if (node->parent != NULL) {
        /* check if parent already has a node with same name */
        if (iso_dir_get_node(node->parent, name, NULL) == 1) {
            return ISO_NODE_NAME_NOT_UNIQUE;
        }
    }

    new = strdup(name);
    if (new == NULL) {
        return ISO_OUT_OF_MEM;
    }
    free(node->name);
    node->name = new;
    if (node->parent != NULL) {
        IsoDir *parent;
        int res;
        /* take and add again to ensure correct children order */
        parent = node->parent;
        iso_node_take(node);
        res = iso_dir_add_node(parent, node, 0);
        if (res < 0) {
            return res;
        }
    }
    return ISO_SUCCESS;
}

/**
 * Get the name of a node (in UTF-8).
 * The returned string belongs to the node and should not be modified nor
 * freed. Use strdup if you really need your own copy.
 */
const char *iso_node_get_name(const IsoNode *node)
{
    return node->name;
}

/**
 * See API function iso_node_set_permissions()
 *
 * @param flag  bit0= do not adjust ACL
 * @return      >0 success , <0 error
 */
int iso_node_set_perms_internal(IsoNode *node, mode_t mode, int flag)
{
    int ret;

    node->mode = (node->mode & S_IFMT) | (mode & ~S_IFMT);

    /* If the node has ACL info : update ACL */
    ret = 1;
    if (!(flag & 1))
        ret = iso_node_set_acl_text(node, "", "", 2);

    return ret;
}

/**
 * Set the permissions for the node. This attribute is only useful when
 * Rock Ridge extensions are enabled.
 *
 * @param mode
 *     bitmask with the permissions of the node, as specified in 'man 2 stat'.
 *     The file type bitfields will be ignored, only file permissions will be
 *     modified.
 */
void iso_node_set_permissions(IsoNode *node, mode_t mode)
{
     iso_node_set_perms_internal(node, mode, 0);
}


/**
 * Get the permissions for the node
 */
mode_t iso_node_get_permissions(const IsoNode *node)
{
    return node->mode & ~S_IFMT;
}

/**
 * Get the mode of the node, both permissions and file type, as specified in
 * 'man 2 stat'.
 */
mode_t iso_node_get_mode(const IsoNode *node)
{
    return node->mode;
}

/**
 * Set the user id for the node. This attribute is only useful when
 * Rock Ridge extensions are enabled.
 */
void iso_node_set_uid(IsoNode *node, uid_t uid)
{
    node->uid = uid;
}

/**
 * Get the user id of the node.
 */
uid_t iso_node_get_uid(const IsoNode *node)
{
    return node->uid;
}

/**
 * Set the group id for the node. This attribute is only useful when
 * Rock Ridge extensions are enabled.
 */
void iso_node_set_gid(IsoNode *node, gid_t gid)
{
    node->gid = gid;
}

/**
 * Get the group id of the node.
 */
gid_t iso_node_get_gid(const IsoNode *node)
{
    return node->gid;
}

/**
 * Set the time of last modification of the file
 */
void iso_node_set_mtime(IsoNode *node, time_t time)
{
    node->mtime = time;
}

/**
 * Get the time of last modification of the file
 */
time_t iso_node_get_mtime(const IsoNode *node)
{
    return node->mtime;
}

/**
 * Set the time of last access to the file
 */
void iso_node_set_atime(IsoNode *node, time_t time)
{
    node->atime = time;
}

/**
 * Get the time of last access to the file
 */
time_t iso_node_get_atime(const IsoNode *node)
{
    return node->atime;
}

/**
 * Set the time of last status change of the file
 */
void iso_node_set_ctime(IsoNode *node, time_t time)
{
    node->ctime = time;
}

/**
 * Get the time of last status change of the file
 */
time_t iso_node_get_ctime(const IsoNode *node)
{
    return node->ctime;
}

void iso_node_set_hidden(IsoNode *node, int hide_attrs)
{
    /* you can't hide root node */
    if ((IsoNode*)node->parent != node) {
        node->hidden = hide_attrs;
    }
}

int iso_node_get_hidden(IsoNode *node)
{
    return node->hidden;
}


/**
 * Add a new node to a dir. Note that this function don't add a new ref to
 * the node, so you don't need to free it, it will be automatically freed
 * when the dir is deleted. Of course, if you want to keep using the node
 * after the dir life, you need to iso_node_ref() it.
 *
 * @param dir
 *     the dir where to add the node
 * @param child
 *     the node to add. You must ensure that the node hasn't previously added
 *     to other dir, and that the node name is unique inside the child.
 *     Otherwise this function will return a failure, and the child won't be
 *     inserted.
 * @param replace
 *     if the dir already contains a node with the same name, whether to
 *     replace or not the old node with this.
 * @return
 *     number of nodes in dir if succes, < 0 otherwise
 */
int iso_dir_add_node(IsoDir *dir, IsoNode *child,
                     enum iso_replace_mode replace)
{
    IsoNode **pos;

    if (dir == NULL || child == NULL) {
        return ISO_NULL_POINTER;
    }
    if ((IsoNode*)dir == child) {
        return ISO_WRONG_ARG_VALUE;
    }

    /*
     * check if child is already added to another dir, or if child
     * is the root node, where parent == itself
     */
    if (child->parent != NULL || child->parent == (IsoDir*)child) {
        return ISO_NODE_ALREADY_ADDED;
    }

    iso_dir_find(dir, child->name, &pos);
    return iso_dir_insert(dir, child, pos, replace);
}

/**
 * Locate a node inside a given dir.
 *
 * @param name
 *     The name of the node
 * @param node
 *     Location for a pointer to the node, it will filled with NULL if the dir
 *     doesn't have a child with the given name.
 *     The node will be owned by the dir and shouldn't be unref(). Just call
 *     iso_node_ref() to get your own reference to the node.
 *     Note that you can pass NULL is the only thing you want to do is check
 *     if a node with such name already exists on dir.
 * @return
 *     1 node found, 0 child has no such node, < 0 error
 *     Possible errors:
 *         ISO_NULL_POINTER, if dir or name are NULL
 */
int iso_dir_get_node(IsoDir *dir, const char *name, IsoNode **node)
{
    int ret;
    IsoNode **pos;
    if (dir == NULL || name == NULL) {
        return ISO_NULL_POINTER;
    }

    ret = iso_dir_exists(dir, name, &pos);
    if (ret == 0) {
        if (node) {
            *node = NULL;
        }
        return 0; /* node not found */
    }

    if (node) {
        *node = *pos;
    }
    return 1;
}

/**
 * Get the number of children of a directory.
 *
 * @return
 *     >= 0 number of items, < 0 error
 *     Possible errors:
 *         ISO_NULL_POINTER, if dir is NULL
 */
int iso_dir_get_children_count(IsoDir *dir)
{
    if (dir == NULL) {
        return ISO_NULL_POINTER;
    }
    return dir->nchildren;
}

static
int iter_next(IsoDirIter *iter, IsoNode **node)
{
    struct dir_iter_data *data;
    if (iter == NULL || node == NULL) {
        return ISO_NULL_POINTER;
    }

    data = iter->data;

    /* clear next flag */
    data->flag &= ~0x01;

    if (data->pos == NULL) {
        /* we are at the beginning */
        data->pos = iter->dir->children;
        if (data->pos == NULL) {
            /* empty dir */
            *node = NULL;
            return 0;
        }
    } else {
        if (data->pos->parent != iter->dir) {
            /* this can happen if the node has been moved to another dir */
            /* TODO specific error */
            return ISO_ERROR;
        }
        if (data->pos->next == NULL) {
            /* no more children */
            *node = NULL;
            return 0;
        } else {
            /* free reference to current position */
            iso_node_unref(data->pos); /* it is never last ref!! */

            /* advance a position */
            data->pos = data->pos->next;
        }
    }

    /* ok, take a ref to the current position, to prevent internal errors
     * if deleted somewhere */
    iso_node_ref(data->pos);
    data->flag |= 0x01; /* set next flag */

    /* return pointed node */
    *node = data->pos;
    return ISO_SUCCESS;
}

/**
 * Check if there're more children.
 *
 * @return
 *     1 dir has more elements, 0 no, < 0 error
 *     Possible errors:
 *         ISO_NULL_POINTER, if iter is NULL
 */
static
int iter_has_next(IsoDirIter *iter)
{
    struct dir_iter_data *data;
    if (iter == NULL) {
        return ISO_NULL_POINTER;
    }
    data = iter->data;
    if (data->pos == NULL) {
        return iter->dir->children == NULL ? 0 : 1;
    } else {
        return data->pos->next == NULL ? 0 : 1;
    }
}

static
void iter_free(IsoDirIter *iter)
{
    struct dir_iter_data *data;
    data = iter->data;
    if (data->pos != NULL) {
        iso_node_unref(data->pos);
    }
    free(data);
}

static IsoNode** iso_dir_find_node(IsoDir *dir, IsoNode *node)
{
    IsoNode **pos;
    pos = &(dir->children);
    while (*pos != NULL && *pos != node) {
        pos = &((*pos)->next);
    }
    return pos;
}

/**
 * Removes a child from a directory.
 * The child is not freed, so you will become the owner of the node. Later
 * you can add the node to another dir (calling iso_dir_add_node), or free
 * it if you don't need it (with iso_node_unref).
 *
 * @return
 *     1 on success, < 0 error
 */
int iso_node_take(IsoNode *node)
{
    IsoNode **pos;
    IsoDir* dir;

    if (node == NULL) {
        return ISO_NULL_POINTER;
    }
    dir = node->parent;
    if (dir == NULL) {
        return ISO_NODE_NOT_ADDED_TO_DIR;
    }

    /* >>> Do not take root directory ! (dir == node) ? */;

    pos = iso_dir_find_node(dir, node);
    if (pos == NULL) {
        /* should never occur */
        return ISO_ASSERT_FAILURE;
    }

    /* notify iterators just before remove */
    iso_notify_dir_iters(node, 0);

    *pos = node->next;
    node->parent = NULL;
    node->next = NULL;
    dir->nchildren--;
    return ISO_SUCCESS;
}

/**
 * Removes a child from a directory and free (unref) it.
 * If you want to keep the child alive, you need to iso_node_ref() it
 * before this call, but in that case iso_node_take() is a better
 * alternative.
 *
 * @return
 *     1 on success, < 0 error
 */
int iso_node_remove(IsoNode *node)
{
    int ret;
    ret = iso_node_take(node);
    if (ret == ISO_SUCCESS) {
        iso_node_unref(node);
    }
    return ret;
}

/* API */
int iso_node_remove_tree(IsoNode *node, IsoDirIter *boss_iter)
{
    IsoDirIter *iter = NULL;
    IsoNode *sub_node;
    int ret;

    if (node->type != LIBISO_DIR) {

        /* >>> Do not remove root directory ! (node->parent == node) ? */;

        ret = iso_dir_get_children((IsoDir *) node, &iter);
        if (ret < 0)
            goto ex;
        while(1) {
            ret = iso_dir_iter_next(iter, &sub_node);
            if (ret == 0)
        break;
            ret = iso_node_remove_tree(sub_node, iter);
            if (ret < 0)
                goto ex;
        }
        if (node->parent == NULL) {
            /* node is not grafted into a boss directory */
            iso_node_unref(node);
            goto ex;
        }
    }
    if (boss_iter != NULL)
        ret = iso_dir_iter_remove(boss_iter);
    else
        ret = iso_node_remove(node);
ex:;
    if (iter != NULL)
        iso_dir_iter_free(iter);
    return ret;
}

/*
 * Get the parent of the given iso tree node. No extra ref is added to the
 * returned directory, you must take your ref. with iso_node_ref() if you
 * need it.
 *
 * If node is the root node, the same node will be returned as its parent.
 *
 * This returns NULL if the node doesn't pertain to any tree
 * (it was removed/take).
 */
IsoDir *iso_node_get_parent(IsoNode *node)
{
    return node->parent;
}

/* TODO #00005 optimize iso_dir_iter_take */
static
int iter_take(IsoDirIter *iter)
{
    struct dir_iter_data *data;
    if (iter == NULL) {
        return ISO_NULL_POINTER;
    }

    data = iter->data;

    if (!(data->flag & 0x01)) {
        return ISO_ERROR; /* next not called or end of dir */
    }

    if (data->pos == NULL) {
        return ISO_ASSERT_FAILURE;
    }

    /* clear next flag */
    data->flag &= ~0x01;

    return iso_node_take(data->pos);
}

static
int iter_remove(IsoDirIter *iter)
{
    int ret;
    IsoNode *pos;
    struct dir_iter_data *data;

    if (iter == NULL) {
        return ISO_NULL_POINTER;
    }
    data = iter->data;
    pos = data->pos;

    ret = iter_take(iter);
    if (ret == ISO_SUCCESS) {
        /* remove node */
        iso_node_unref(pos);
    }
    return ret;
}

void iter_notify_child_taken(IsoDirIter *iter, IsoNode *node)
{
    IsoNode *pos, *pre;
    struct dir_iter_data *data;
    data = iter->data;

    if (data->pos == node) {
        pos = iter->dir->children;
        pre = NULL;
        while (pos != NULL && pos != data->pos) {
            pre = pos;
            pos = pos->next;
        }
        if (pos == NULL || pos != data->pos) {
            return;
        }

        /* dispose iterator reference */
        iso_node_unref(data->pos);

        if (pre == NULL) {
            /* node is a first position */
            iter->dir->children = pos->next;
            data->pos = NULL;
        } else {
            pre->next = pos->next;
            data->pos = pre;
            iso_node_ref(pre); /* take iter ref */
        }
    }
}

static
struct iso_dir_iter_iface iter_class = {
        iter_next,
        iter_has_next,
        iter_free,
        iter_take,
        iter_remove,
        iter_notify_child_taken
};

int iso_dir_get_children(const IsoDir *dir, IsoDirIter **iter)
{
    IsoDirIter *it;
    struct dir_iter_data *data;

    if (dir == NULL || iter == NULL) {
        return ISO_NULL_POINTER;
    }
    it = malloc(sizeof(IsoDirIter));
    if (it == NULL) {
        return ISO_OUT_OF_MEM;
    }
    data = malloc(sizeof(struct dir_iter_data));
    if (data == NULL) {
        free(it);
        return ISO_OUT_OF_MEM;
    }

    it->class = &iter_class;
    it->dir = (IsoDir*)dir;
    data->pos = NULL;
    data->flag = 0x00;
    it->data = data;

    if (iso_dir_iter_register(it) < 0) {
        free(it);
        return ISO_OUT_OF_MEM;
    }

    iso_node_ref((IsoNode*)dir); /* tak a ref to the dir */
    *iter = it;
    return ISO_SUCCESS;
}

int iso_dir_iter_next(IsoDirIter *iter, IsoNode **node)
{
    if (iter == NULL || node == NULL) {
        return ISO_NULL_POINTER;
    }
    return iter->class->next(iter, node);
}

int iso_dir_iter_has_next(IsoDirIter *iter)
{
    if (iter == NULL) {
        return ISO_NULL_POINTER;
    }
    return iter->class->has_next(iter);
}

void iso_dir_iter_free(IsoDirIter *iter)
{
    if (iter != NULL) {
        iso_dir_iter_unregister(iter);
        iter->class->free(iter);
        iso_node_unref((IsoNode*)iter->dir);
        free(iter);
    }
}

int iso_dir_iter_take(IsoDirIter *iter)
{
    if (iter == NULL) {
        return ISO_NULL_POINTER;
    }
    return iter->class->take(iter);
}

int iso_dir_iter_remove(IsoDirIter *iter)
{
    if (iter == NULL) {
        return ISO_NULL_POINTER;
    }
    return iter->class->remove(iter);
}

/**
 * Get the destination of a node.
 * The returned string belongs to the node and should not be modified nor
 * freed. Use strdup if you really need your own copy.
 */
const char *iso_symlink_get_dest(const IsoSymlink *link)
{
    return link->dest;
}

/**
 * Set the destination of a link.
 */
int iso_symlink_set_dest(IsoSymlink *link, const char *dest)
{
    char *d;
    int ret;

    ret = iso_node_is_valid_link_dest(dest);
    if (ret < 0)
        return ret;
    d = strdup(dest);
    if (d == NULL) {
        return ISO_OUT_OF_MEM;
    }
    free(link->dest);
    link->dest = d;
    return ISO_SUCCESS;
}

/**
 * Sets the order in which a node will be written on image. High weihted files
 * will be written first, so in a disc them will be written near the center.
 *
 * @param node
 *      The node which weight will be changed. If it's a dir, this function
 *      will change the weight of all its children. For nodes other that dirs
 *      or regular files, this function has no effect.
 * @param w
 *      The weight as a integer number, the greater this value is, the
 *      closer from the begining of image the file will be written.
 */
void iso_node_set_sort_weight(IsoNode *node, int w)
{
    if (node->type == LIBISO_DIR) {
        IsoNode *child = ((IsoDir*)node)->children;
        while (child) {
            iso_node_set_sort_weight(child, w);
            child = child->next;
        }
    } else if (node->type == LIBISO_FILE) {
        ((IsoFile*)node)->sort_weight = w;
    }
}

/**
 * Get the sort weight of a file.
 */
int iso_file_get_sort_weight(IsoFile *file)
{
    return file->sort_weight;
}

/**
 * Get the size of the file, in bytes
 */
off_t iso_file_get_size(IsoFile *file)
{
    return iso_stream_get_size(file->stream);
}

/**
 * Get the IsoStream that represents the contents of the given IsoFile.
 *
 * If you open() the stream, it should be close() before image generation.
 *
 * @return
 *      The IsoStream. No extra ref is added, so the IsoStream belong to the
 *      IsoFile, and it may be freed together with it. Add your own ref with
 *      iso_stream_ref() if you need it.
 *
 * @since 0.6.4
 */
IsoStream *iso_file_get_stream(IsoFile *file)
{
    return file->stream;
}

/**
 * Get the device id (major/minor numbers) of the given block or
 * character device file. The result is undefined for other kind
 * of special files, of first be sure iso_node_get_mode() returns either
 * S_IFBLK or S_IFCHR.
 *
 * @since 0.6.6
 */
dev_t iso_special_get_dev(IsoSpecial *special)
{
    return special->dev;
}

/**
 * Get the block lba of a file node, if it was imported from an old image.
 *
 * @param file
 *      The file
 * @param lba
 *      Will be filled with the kba
 * @param flag
 *      Reserved for future usage, submit 0
 * @return
 *      1 if lba is valid (file comes from old image), 0 if file was newly
 *      added, i.e. it does not come from an old image, < 0 error
 *
 * @since 0.6.4
 */
int iso_file_get_old_image_lba(IsoFile *file, uint32_t *lba, int flag)
{
    int ret;
    int section_count;
    struct iso_file_section *sections;
    if (file == NULL || lba == NULL) {
        return ISO_NULL_POINTER;
    }
    ret = iso_file_get_old_image_sections(file, &section_count, &sections, flag);
    if (ret <= 0) {
        return ret;
    }
    if (section_count != 1) {
        free(sections);
        return ISO_WRONG_ARG_VALUE;
    }
    *lba = sections[0].block;
    free(sections);
    return 0;
}



/*
 * Like iso_file_get_old_image_lba(), but take an IsoNode.
 *
 * @return
 *      1 if lba is valid (file comes from old image), 0 if file was newly
 *      added, i.e. it does not come from an old image, 2 node type has no
 *      LBA (no regular file), < 0 error
 *
 * @since 0.6.4
 */
int iso_node_get_old_image_lba(IsoNode *node, uint32_t *lba, int flag)
{
    if (node == NULL) {
        return ISO_NULL_POINTER;
    }
    if (ISO_NODE_IS_FILE(node)) {
        return iso_file_get_old_image_lba((IsoFile*)node, lba, flag);
    } else {
        return 2;
    }
}

/**
 * Check if a given name is valid for an iso node.
 *
 * @return
 *     1 if yes, 0 if not
 */
int iso_node_is_valid_name(const char *name)
{
    /* a name can't be NULL */
    if (name == NULL) {
        return ISO_NULL_POINTER;
    }

    /* guard against the empty string or big names... */
    if (name[0] == '\0')
        return ISO_RR_NAME_RESERVED;
    if (strlen(name) > LIBISOFS_NODE_NAME_MAX)
        return ISO_RR_NAME_TOO_LONG;

    /* ...against "." and ".." names... */
    if (!strcmp(name, ".") || !strcmp(name, "..")) {
        return ISO_RR_NAME_RESERVED;
    }

    /* ...and against names with '/' */
    if (strchr(name, '/') != NULL) {
        return ISO_RR_NAME_RESERVED;
    }
    return 1;
}

/**
 * Check if a given path is valid for the destination of a link.
 *
 * @return
 *     1 if yes, 0 if not
 */
int iso_node_is_valid_link_dest(const char *dest)
{
    int ret;
    char *ptr, *brk_info, *component;

    /* a dest can't be NULL */
    if (dest == NULL) {
        return ISO_NULL_POINTER;
    }

    /* guard against the empty string or big dest... */
    if (dest[0] == '\0')
        return ISO_RR_NAME_RESERVED;
    if (strlen(dest) > LIBISOFS_NODE_PATH_MAX)
        return ISO_RR_PATH_TOO_LONG;

    /* check that all components are valid */
    if (!strcmp(dest, "/")) {
        /* "/" is a valid component */
        return 1;
    }

    ptr = strdup(dest);
    if (ptr == NULL) {
        return ISO_OUT_OF_MEM;
    }

    ret = 1;
    component = strtok_r(ptr, "/", &brk_info);
    while (component) {
        if (strcmp(component, ".") && strcmp(component, "..")) {
            ret = iso_node_is_valid_name(component);
            if (ret < 0) {
                break;
            }
        }
        component = strtok_r(NULL, "/", &brk_info);
    }
    free(ptr);

    return ret;
}

void iso_dir_find(IsoDir *dir, const char *name, IsoNode ***pos)
{
    *pos = &(dir->children);
    while (**pos != NULL && strcmp((**pos)->name, name) < 0) {
        *pos = &((**pos)->next);
    }
}

int iso_dir_exists(IsoDir *dir, const char *name, IsoNode ***pos)
{
    IsoNode **node;

    iso_dir_find(dir, name, &node);
    if (pos) {
        *pos = node;
    }
    return (*node != NULL && !strcmp((*node)->name, name)) ? 1 : 0;
}

int iso_dir_insert(IsoDir *dir, IsoNode *node, IsoNode **pos,
                   enum iso_replace_mode replace)
{
    if (*pos != NULL && !strcmp((*pos)->name, node->name)) {
        /* a node with same name already exists */
        switch(replace) {
        case ISO_REPLACE_NEVER:
            return ISO_NODE_NAME_NOT_UNIQUE;
        case ISO_REPLACE_IF_NEWER:
            if ((*pos)->mtime >= node->mtime) {
                /* old file is newer */
                return ISO_NODE_NAME_NOT_UNIQUE;
            }
            break;
        case ISO_REPLACE_IF_SAME_TYPE_AND_NEWER:
            if ((*pos)->mtime >= node->mtime) {
                /* old file is newer */
                return ISO_NODE_NAME_NOT_UNIQUE;
            }
            /* fall down */
        case ISO_REPLACE_IF_SAME_TYPE:
            if ((node->mode & S_IFMT) != ((*pos)->mode & S_IFMT)) {
                /* different file types */
                return ISO_NODE_NAME_NOT_UNIQUE;
            }
            break;
        case ISO_REPLACE_ALWAYS:
            break;
        default:
            /* CAN'T HAPPEN */
            return ISO_ASSERT_FAILURE;
        }

        /* if we are reach here we have to replace */
        node->next = (*pos)->next;
        (*pos)->parent = NULL;
        (*pos)->next = NULL;
        iso_node_unref(*pos);
        *pos = node;
        node->parent = dir;
        return dir->nchildren;
    }

    node->next = *pos;
    *pos = node;
    node->parent = dir;

    return ++dir->nchildren;
}

/* iterators are stored in a linked list */
struct iter_reg_node {
    IsoDirIter *iter;
    struct iter_reg_node *next;
};

/* list header */
static
struct iter_reg_node *iter_reg = NULL;

/**
 * Add a new iterator to the registry. The iterator register keeps track of
 * all iterators being used, and are notified when directory structure
 * changes.
 */
int iso_dir_iter_register(IsoDirIter *iter)
{
    struct iter_reg_node *new;
    new = malloc(sizeof(struct iter_reg_node));
    if (new == NULL) {
        return ISO_OUT_OF_MEM;
    }
    new->iter = iter;
    new->next = iter_reg;
    iter_reg = new;
    return ISO_SUCCESS;
}

/**
 * Unregister a directory iterator.
 */
void iso_dir_iter_unregister(IsoDirIter *iter)
{
    struct iter_reg_node **pos;
    pos = &iter_reg;
    while (*pos != NULL && (*pos)->iter != iter) {
        pos = &(*pos)->next;
    }
    if (*pos) {
        struct iter_reg_node *tmp = (*pos)->next;
        free(*pos);
        *pos = tmp;
    }
}

void iso_notify_dir_iters(IsoNode *node, int flag)
{
    struct iter_reg_node *pos = iter_reg;
    while (pos != NULL) {
        IsoDirIter *iter = pos->iter;
        if (iter->dir == node->parent) {
            iter->class->notify_child_taken(iter, node);
        }
        pos = pos->next;
    }
}

int iso_node_new_root(IsoDir **root)
{
    IsoDir *dir;

    dir = calloc(1, sizeof(IsoDir));
    if (dir == NULL) {
        return ISO_OUT_OF_MEM;
    }
    dir->node.refcount = 1;
    dir->node.type = LIBISO_DIR;
    dir->node.atime = dir->node.ctime = dir->node.mtime = time(NULL);
    dir->node.mode = S_IFDIR | 0555;

    /* set parent to itself, to prevent root to be added to another dir */
    dir->node.parent = dir;
    *root = dir;
    return ISO_SUCCESS;
}

int iso_node_new_dir(char *name, IsoDir **dir)
{
    IsoDir *new;
    int ret;

    if (dir == NULL || name == NULL) {
        return ISO_NULL_POINTER;
    }

    /* check if the name is valid */
    ret = iso_node_is_valid_name(name);
    if (ret < 0)
        return ret;

    new = calloc(1, sizeof(IsoDir));
    if (new == NULL) {
        return ISO_OUT_OF_MEM;
    }
    new->node.refcount = 1;
    new->node.type = LIBISO_DIR;
    new->node.name = name;
    new->node.mode = S_IFDIR;
    *dir = new;
    return ISO_SUCCESS;
}

int iso_node_new_file(char *name, IsoStream *stream, IsoFile **file)
{
    IsoFile *new;
    int ret;

    if (file == NULL || name == NULL || stream == NULL) {
        return ISO_NULL_POINTER;
    }

    /* check if the name is valid */
    ret = iso_node_is_valid_name(name);
    if (ret < 0)
        return ret;

    new = calloc(1, sizeof(IsoFile));
    if (new == NULL) {
        return ISO_OUT_OF_MEM;
    }
    new->node.refcount = 1;
    new->node.type = LIBISO_FILE;
    new->node.name = name;
    new->node.mode = S_IFREG;
    new->sort_weight = 0;
    new->stream = stream;

    *file = new;
    return ISO_SUCCESS;
}

int iso_node_new_symlink(char *name, char *dest, IsoSymlink **link)
{
    IsoSymlink *new;
    int ret;

    if (link == NULL || name == NULL || dest == NULL) {
        return ISO_NULL_POINTER;
    }

    /* check if the name is valid */
    ret = iso_node_is_valid_name(name);
    if (ret < 0)
        return ret;

    /* check if destination is valid */
    ret = iso_node_is_valid_link_dest(dest);
    if (ret < 0) 
        return ret;

    new = calloc(1, sizeof(IsoSymlink));
    if (new == NULL) {
        return ISO_OUT_OF_MEM;
    }
    new->node.refcount = 1;
    new->node.type = LIBISO_SYMLINK;
    new->node.name = name;
    new->dest = dest;
    new->node.mode = S_IFLNK;
    new->fs_id = 0;
    new->st_dev = 0;
    new->st_ino = 0;
    *link = new;
    return ISO_SUCCESS;
}

int iso_node_new_special(char *name, mode_t mode, dev_t dev,
                         IsoSpecial **special)
{
    IsoSpecial *new;
    int ret;

    if (special == NULL || name == NULL) {
        return ISO_NULL_POINTER;
    }
    if (S_ISLNK(mode) || S_ISREG(mode) || S_ISDIR(mode)) {
        return ISO_WRONG_ARG_VALUE;
    }

    /* check if the name is valid */
    ret = iso_node_is_valid_name(name);
    if (ret < 0)
        return ret;

    new = calloc(1, sizeof(IsoSpecial));
    if (new == NULL) {
        return ISO_OUT_OF_MEM;
    }
    new->node.refcount = 1;
    new->node.type = LIBISO_SPECIAL;
    new->node.name = name;

    new->node.mode = mode;
    new->dev = dev;
    new->fs_id = 0;
    new->st_dev = 0;
    new->st_ino = 0;
    *special = new;
    return ISO_SUCCESS;
}


/* @param flag    bit0= inverse: cleanout everything but del_name
*/
static
int attrs_cleanout_name(char *del_name, size_t *num_attrs, char **names,
                        size_t *value_lengths, char **values, int flag)
{
    size_t i, w;

    for (w = i = 0; i < *num_attrs; i++) {
        if ((strcmp(names[i], del_name) == 0) ^ (flag & 1))
            continue;
        if (w == i) {
            w++;
            continue;
        }
        names[w] = names[i];
        value_lengths[w] = value_lengths[i];
        values[w] = values[i];
        names[i] = values[i] = NULL;
        value_lengths[i] = 0;
        w++;
    }
    *num_attrs = w;
    return 1;
}


/**
 * Backend of iso_node_get_attrs() with parameter node replaced by the
 * AAIP string from where to get the attribute list.
 * All other parameter specs apply.
 */
int iso_aa_get_attrs(unsigned char *aa_string, size_t *num_attrs,
              char ***names, size_t **value_lengths, char ***values, int flag)
{
    struct aaip_state *aaip= NULL;
    unsigned char *rpt;
    size_t len, todo, consumed;
    int is_done = 0, first_round= 1, ret;

    if (flag & (1 << 15))
        aaip_get_decoded_attrs(&aaip, num_attrs, names,
                               value_lengths, values, 1 << 15);
    *num_attrs = 0;
    *names = NULL;
    *value_lengths = NULL;
    *values = NULL;
    if (flag & (1 << 15))
        return 1;

    rpt = aa_string;
    len = aaip_count_bytes(rpt, 0);
    while (!is_done) {
        todo = len - (rpt - aa_string);
        if (todo > 2048)
            todo = 2048;
        if (todo == 0) {
           /* Out of data while still prompted to submit */
           ret = ISO_AAIP_BAD_AASTRING;
           goto ex;
        }
        /* Allow 1 million bytes of memory consumption, 100,000 attributes */
        ret = aaip_decode_attrs(&aaip, (size_t) 1000000, (size_t) 100000,
                                rpt, todo, &consumed, first_round);
        rpt+= consumed;
        first_round= 0;
        if (ret == 1)
            continue;
        if (ret == 2)
             break;

         /* aaip_decode_attrs() reports error */
         ret = ISO_AAIP_BAD_AASTRING;
         goto ex;
    }

    if ((size_t) (rpt - aa_string) != len) {
         /* aaip_decode_attrs() returns 2 but still bytes are left */
         ret = ISO_AAIP_BAD_AASTRING;
         goto ex;
    }

    ret = aaip_get_decoded_attrs(&aaip, num_attrs, names,
                                 value_lengths, values, 0);
    if (ret != 1) {
         /* aaip_get_decoded_attrs() failed */
         ret = ISO_AAIP_BAD_AASTRING;
         goto ex;
    }
    if (!(flag & 1)) {
        /* Clean out eventual ACL attribute resp. all other xattr */
        attrs_cleanout_name("", num_attrs, *names, *value_lengths, *values,
                            !!(flag & 4));
    }

    ret = 1;
ex:;
    aaip_decode_attrs(&aaip, (size_t) 1000000, (size_t) 100000,
                      rpt, todo, &consumed, 1 << 15);
    return ret;
}


/**
 * Search given name. Eventually calloc() and copy value. Add trailing 0 byte
 * for caller convenience.
 *
 * @return 1= found , 0= not found , <0 error
 */
int iso_aa_lookup_attr(unsigned char *aa_string, char *name,
                       size_t *value_length, char **value, int flag)
{
    size_t num_attrs = 0, *value_lengths = NULL;
    char **names = NULL, **values = NULL;
    int i, ret = 0, found = 0;

    ret = iso_aa_get_attrs(aa_string, &num_attrs, &names,
                           &value_lengths, &values, 0);
    if (ret < 0)
        return ret;
    for (i = 0; i < (int) num_attrs; i++) {
        if (strcmp(names[i], name))
    continue;
        *value_length = value_lengths[i];
        *value = calloc(*value_length + 1, 1);
        if (*value == NULL) {
            found = ISO_OUT_OF_MEM;
    break;
        }
        if (*value_length > 0)
            memcpy(*value, values[i], *value_length);
        (*value)[*value_length] = 0;
        found = 1;
    break;
    }
    iso_aa_get_attrs(aa_string, &num_attrs, &names,
                     &value_lengths, &values, 1 << 15);
    return found;
}


/* API */
int iso_node_lookup_attr(IsoNode *node, char *name,
                         size_t *value_length, char **value, int flag)
{
    void *xipt;
    unsigned char *aa_string = NULL;
    int ret;

    *value_length= 0;
    *value= NULL;
    ret = iso_node_get_xinfo(node, aaip_xinfo_func, &xipt);
    if (ret != 1)
        return 0;
    aa_string = (unsigned char *) xipt;
    ret = iso_aa_lookup_attr(aa_string, name, value_length, value, 0);
    return ret;
}


/* API */
int iso_node_get_attrs(IsoNode *node, size_t *num_attrs,
              char ***names, size_t **value_lengths, char ***values, int flag)
{
    void *xipt;
    unsigned char *aa_string = NULL;
    int ret;

    if (flag & (1 << 15)) {
        iso_aa_get_attrs(aa_string, num_attrs, names, value_lengths, values,
                         1 << 15);
        return 1;
    }
    *num_attrs = 0;
    *names = NULL;
    *value_lengths = NULL;
    *values = NULL;
    ret = iso_node_get_xinfo(node, aaip_xinfo_func, &xipt);
    if (ret != 1)
        return 1;
    aa_string = (unsigned char *) xipt;
    ret = iso_aa_get_attrs(aa_string, num_attrs, names, value_lengths, values,
                           flag);
    return ret;
}


/* Enlarge attribute list */
static
int attr_enlarge_list(char ***names, size_t **value_lengths, char ***values,
                      size_t new_num, int flag)
{
    void *newpt;

    newpt = realloc(*names, new_num * sizeof(char *));
    if (newpt == NULL) 
        return ISO_OUT_OF_MEM;
    *names = (char **) newpt;
    newpt = realloc(*values, new_num * sizeof(char *));
    if (newpt == NULL) 
        return ISO_OUT_OF_MEM;
    *values = (char **) newpt;
    newpt = realloc(*value_lengths, new_num * sizeof(size_t));
    if (newpt == NULL) 
        return ISO_OUT_OF_MEM;
    *value_lengths = (size_t *) newpt;
    return 1;
}


/* Merge attribute list of node and given new attribute list into
   attribute list returned by  m_* parameters.
   The m_* paramters have finally to be freed by a call with bit15 set.
   @param flag          Bitfield for control purposes
                        bit0= delete all old names which begin by "user."     
                              (but not if bit2 is set)
                        bit2= delete the given names rather than overwrite
                              their content
                        bit4= do not overwrite value of empty name
                        bit5= do not overwrite isofs attributes 
                        bit15= release memory and return 1
*/
static
int iso_node_merge_xattr(IsoNode *node, size_t num_attrs, char **names,
                         size_t *value_lengths, char **values,
                         size_t *m_num_attrs, char ***m_names,
                         size_t **m_value_lengths, char ***m_values, int flag)
{
    int ret;
    size_t new_names = 0, deleted = 0, i, j, w;

    if (flag & (1 << 15)) {
        iso_node_get_attrs(node, m_num_attrs, m_names, m_value_lengths,
                           m_values, 1 << 15);
        return 1;
    }

    ret = iso_node_get_attrs(node, m_num_attrs, m_names, m_value_lengths,
                             m_values, 1);
    if (ret < 0)
        return ret;

    if ((flag & 1) && (!(flag & 4))) {
        /* Delete unmatched user space pairs */
        for (j = 0; j < *m_num_attrs; j++) {
            if (strncmp((*m_names)[j], "user.", 5) != 0)
                continue;
            for (i = 0; i < num_attrs; i++) {
                if (names[i] == NULL || (*m_names)[j] == NULL)
                    continue;
                if (strcmp(names[i], (*m_names)[j]) == 0)
                    break;
            }
            if (i >= num_attrs) {    
                /* Delete unmatched pair */
                free((*m_names)[j]);
                (*m_names)[j] = NULL;
                deleted++;
            }
        }
    }

    /* Handle existing names, count non-existing names */
    for (i = 0; i < num_attrs; i++) {
        if (names[i] == NULL)
            continue;
        if (names[i][0] == 0 && (flag & 16))
            continue;
        if ((flag & 32) && strncmp(names[i], "isofs.", 6) == 0)
            continue;
        for (j = 0; j < *m_num_attrs; j++) {
            if ((*m_names)[j] == NULL)
                continue;
            if (strcmp(names[i], (*m_names)[j]) == 0) {
                if ((*m_values)[j] != NULL)
                    free((*m_values)[j]);
                (*m_values)[j] = NULL;
                (*m_value_lengths)[j] = 0;
                if (flag & 4) {
                    /* Delete pair */
                    free((*m_names)[j]);
                    (*m_names)[j] = NULL;
                    deleted++;
                } else {
                    (*m_values)[j] = calloc(value_lengths[i] + 1, 1);
                    if ((*m_values)[j] == NULL)
                        return ISO_OUT_OF_MEM;
                    memcpy((*m_values)[j], values[i], value_lengths[i]);
                    (*m_values)[j][value_lengths[i]] = 0;
                    (*m_value_lengths)[j] = value_lengths[i];
                }
                break;
            }
        }
        if (j >= *m_num_attrs)
            new_names++;
    }

    if (new_names > 0 && (flag & 4)) {

        /* >>> warn of non-existing name on delete ? */;

    } else if (new_names > 0) {
        ret = attr_enlarge_list(m_names, m_value_lengths, m_values,
                                *m_num_attrs + new_names, 0);
        if (ret < 0)
            return ret;

        /* Set new pairs */;
        w = *m_num_attrs;
        for (i = 0; i < num_attrs; i++) {
            if (names[i] == NULL)
                continue;
            if (names[i][0] == 0 && (flag & 16))
                continue;
            if ((flag & 32) && strncmp(names[i], "isofs.", 6) == 0)
                continue;
            for (j = 0; j < *m_num_attrs; j++) {
                if ((*m_names)[j] == NULL)
                    continue;
                if (strcmp(names[i], (*m_names)[j]) == 0)
                    continue;
            }
            if (j < *m_num_attrs) /* Name is not new */ 
                continue;
            (*m_names)[w] = strdup(names[i]);
            if ((*m_names)[w] == NULL)
                return ISO_OUT_OF_MEM;
            (*m_values)[w] = calloc(value_lengths[i] + 1, 1);
            if ((*m_values)[w] == NULL)
                return ISO_OUT_OF_MEM;
            memcpy((*m_values)[w], values[i], value_lengths[i]);
            (*m_values)[w][value_lengths[i]] = 0;
            (*m_value_lengths)[w] = value_lengths[i];
            w++;
        }
        *m_num_attrs = w;
    }
    if (deleted > 0) {
        /* Garbage collection */
        w = 0;
        for (j = 0; j < *m_num_attrs; j++) {
            if ((*m_names)[j] == NULL)
                continue;
            (*m_names)[w] = (*m_names)[j];
            (*m_values)[w] = (*m_values)[j];
            (*m_value_lengths)[w] = (*m_value_lengths)[j];
            w++;
        }
        *m_num_attrs = w;
    }
    return 1;
}


int iso_node_set_attrs(IsoNode *node, size_t num_attrs, char **names,
                       size_t *value_lengths, char **values, int flag)
{
    int ret, acl_saved = 0;
    size_t sret, result_len, m_num = 0, *m_value_lengths = NULL, i;
    unsigned char *result;
    char *a_acl = NULL, *d_acl = NULL, **m_names = NULL, **m_values = NULL;

    if (!(flag & 8))
        for (i = 0; i < num_attrs; i++)
            if (strncmp(names[i], "user.", 5) != 0 && names[i][0] != 0) 
                return ISO_AAIP_NON_USER_NAME;  
    if ((flag & (2 | 4 | 16)) || !(flag & 8)) {
        /* Merge old and new lists */
        ret = iso_node_merge_xattr(
                  node, num_attrs, names, value_lengths, values,
                  &m_num, &m_names, &m_value_lengths, &m_values,
                  (flag & 4) | (!(flag & 2)) | ((!(flag & 1)) << 4) |
                  ((flag & 16) << 1));
        if (ret < 0)
            goto ex;
        num_attrs = m_num;
        names = m_names;
        value_lengths = m_value_lengths;
        values = m_values;
    } else if (!(flag & 1)) {
        iso_node_get_acl_text(node, &a_acl, &d_acl, 16);
        acl_saved = 1;
    }

    if (num_attrs == 0) {
        ret = iso_node_remove_xinfo(node, aaip_xinfo_func);
        if (ret < 0)
            goto ex;
        if (acl_saved && (a_acl != NULL || d_acl != NULL)) {
            ret = iso_node_set_acl_text(node, a_acl, d_acl, 0);
            if (ret < 0)
                goto ex;
        }
        ret = 1;
        goto ex;
    }
    sret = aaip_encode(num_attrs, names, value_lengths, values,
                       &result_len, &result, 0);
    if (sret == 0) {
        ret = ISO_OUT_OF_MEM;
        goto ex;
    }

    ret = iso_node_remove_xinfo(node, aaip_xinfo_func);
    if (ret < 0)
        goto ex;
    ret = iso_node_add_xinfo(node, aaip_xinfo_func, result);
    if (ret < 0)
        goto ex;
    if (ret == 0) {

        /* >>> something is messed up with xinfo: an aa_string still exists */;

        ret = ISO_ERROR;
        goto ex;
    }
    if (acl_saved) {
        ret = iso_node_set_acl_text(node, a_acl, d_acl, 0);
        if (ret < 0)
            goto ex;
    }
    ret = 1;
ex:;
    /* Dispose eventual merged list */
    iso_node_merge_xattr(node, num_attrs, names, value_lengths, values,
                       &m_num, &m_names, &m_value_lengths, &m_values, 1 << 15);
    return ret;
} 


static
int iso_decode_acl(unsigned char *v_data, size_t v_len, size_t *consumed,
                   char **text, size_t *text_fill, int flag)
{
    int ret;

    *text= NULL;
    ret = aaip_decode_acl(v_data, v_len,
                          consumed, NULL, (size_t) 0, text_fill, 1);
    if (ret <= 0)
        return 0;
    if (*text_fill == 0)
        return ret;
    *text = calloc(*text_fill + 42, 1); /* 42 for aaip_update_acl_st_mode */
    if (*text == NULL)
        return ISO_OUT_OF_MEM;
    ret = aaip_decode_acl(v_data, v_len,
                          consumed, *text, *text_fill, text_fill, 0);
    if (ret <= 0) {
        free(*text);
        *text= NULL;
        return 0;
    }
    return ret;
}


/**
 * Backend of iso_node_get_acl_text() with parameter node replaced by the
 * attribute list from where to get the ACL and by the associated st_mode
 * permission bits. All other parameter specs apply.
 */
static
int iso_attr_get_acl_text(size_t num_attrs, char **names,
                          size_t *value_lengths, char **values, mode_t st_mode,
                          char **access_text, char **default_text, int flag)
{
    size_t i, consumed, text_fill = 0;
    size_t v_len;
    unsigned char *v_data;
    int ret, from_posix= 0;

    if (flag & (1 << 15)) {
        if (*access_text != NULL)
            free(*access_text);
        *access_text = NULL;
        if (*default_text != NULL)
            free(*default_text);
        *default_text = NULL;
        return 1;
    }

    *access_text = *default_text = NULL;
    for(i = 0; i < num_attrs; i++) {
        if (names[i][0]) /* searching the empty name */
            continue;

        v_data = (unsigned char *) values[i];
        v_len = value_lengths[i];

        /* "access" ACL  */
        ret = iso_decode_acl(v_data, v_len,
                             &consumed, access_text, &text_fill, 0);
        if (ret <= 0)
            goto bad_decode;
        if (ret == 2) {
            v_data += consumed;
            v_len -= consumed;
            ret = iso_decode_acl(v_data, v_len,
                                 &consumed, default_text, &text_fill, 0);
            if (ret == 0)
                goto bad_decode;
        }
        break;
    }
    
    if (*access_text == NULL && !(flag & 16)) {
        from_posix = 1;
        *access_text = calloc(42, 1); /* 42 for aaip_update_acl_st_mode */
    }
    if (*access_text != NULL) {
        aaip_add_acl_st_mode(*access_text, st_mode, 0);
        text_fill = strlen(*access_text);
    }

    if (*access_text == NULL && *default_text == NULL)
        ret = 0;
    else
        ret = 1 + from_posix;
ex:;
    return ret;

bad_decode:;
    ret = ISO_AAIP_BAD_ACL;
    goto ex;
}


int iso_node_get_acl_text(IsoNode *node,
                          char **access_text, char **default_text, int flag)
{
    size_t num_attrs = 0, *value_lengths = NULL;
    char **names = NULL, **values = NULL;
    mode_t st_mode = 0;
    int ret;

    if (flag & (1 << 15)) {
        iso_attr_get_acl_text(num_attrs, names, value_lengths, values, st_mode,
                              access_text, default_text, 1 << 15);
        return 1;
    }
    ret = iso_node_get_attrs(node, &num_attrs, &names,
                             &value_lengths, &values, 1);
    if (ret < 0)
        return ret;
    st_mode = iso_node_get_permissions(node);
    ret = iso_attr_get_acl_text(num_attrs, names, value_lengths, values,
                                st_mode, access_text, default_text, flag);
    iso_node_get_attrs(node, &num_attrs, &names,
                       &value_lengths, &values, 1 << 15); /* free memory */
    return ret;
}


int iso_aa_get_acl_text(unsigned char *aa_string, mode_t st_mode,
                        char **access_text, char **default_text, int flag)
{
    int ret;
    size_t num_attrs = 0, *value_lengths = NULL;
    char **names = NULL, **values = NULL;

    if (flag & (1 << 15)) {
        iso_attr_get_acl_text(num_attrs, names, value_lengths, values, st_mode,
                              access_text, default_text, 1 << 15);
        return 1;
    }
    ret = iso_aa_get_attrs(aa_string, &num_attrs, &names,
                           &value_lengths, &values, 1);
    if (ret < 0)
        goto ex;
    ret = iso_attr_get_acl_text(num_attrs, names, value_lengths, values,
                                st_mode, access_text, default_text, flag);
ex:;
    iso_aa_get_attrs(aa_string, &num_attrs, &names, &value_lengths, &values,
                     1 << 15);
    return ret;
}


int iso_node_set_acl_text(IsoNode *node, char *access_text, char *default_text,
                          int flag)
{
    size_t num_attrs = 0, *value_lengths = NULL, i, j, consumed;
    size_t a_text_fill = 0, d_text_fill = 0;
    size_t v_len, acl_len= 0;
    char **names = NULL, **values = NULL, *a_text = NULL, *d_text = NULL;

    unsigned char *v_data, *acl= NULL;
    int ret;
    mode_t st_mode;

    st_mode = iso_node_get_permissions(node);
    if (!(flag & 2)) { /* want not to update ACL by st_mode */

        /* >>> validate and rectify text */;

    }

    ret = iso_node_get_attrs(node, &num_attrs, &names,
                             &value_lengths, &values, 1);
    if (ret < 0)
        return ret;

    for(i = 0; i < num_attrs; i++) {
        if (names[i][0]) /* searching the empty name */
            continue;
        v_data = (unsigned char *) values[i];
        v_len = value_lengths[i];
        if (flag & 2) { /* update "access" ACL by st_mode */
            /* read "access" ACL */
            ret = iso_decode_acl(v_data, v_len, &consumed,
                                 &a_text, &a_text_fill, 0);
            if (ret == 0)
                goto bad_decode;
            if (ret < 0)
                goto ex;
            if (ret == 2) {
                /* read "default" ACL */
                v_data += consumed;
                v_len -= consumed;
                ret = iso_decode_acl(v_data, v_len, &consumed, &d_text,
                                     &d_text_fill, 0);
                if (ret == 0)
                    goto bad_decode;
                if (ret < 0)
                    goto ex;
            }
            /* Update "access" ACL by st_mode */
            if (a_text == NULL) {
                ret = 1;
                goto ex;
            }
            ret = aaip_cleanout_st_mode(a_text, &st_mode,  8);
            if (ret < 0) {
                ret = ISO_AAIP_BAD_ACL_TEXT;
                goto ex;
            }
            ret = 1;
            if (a_text != NULL || d_text != NULL)
                ret = aaip_encode_both_acl(a_text, d_text, st_mode,
                                           &acl_len, &acl, 2 | 8);
        } else {
            ret = 1;
            if (access_text != NULL || default_text != NULL)
                ret = aaip_encode_both_acl(access_text, default_text, st_mode,
                                           &acl_len, &acl, 2 | 8);
        }
        if (ret == -1)
            ret = ISO_OUT_OF_MEM;
        else if (ret <= 0 && ret >= -3)
            ret = ISO_AAIP_BAD_ACL_TEXT;
        if (ret <= 0)
            goto ex;

        if(acl == NULL) { /* Delete whole ACL attribute */
            /* Update S_IRWXG by eventual "group::" ACL entry.
               With ACL it reflected the "mask::" entry.
            */
            if (a_text != NULL)
                free(a_text);
            ret = iso_decode_acl(v_data, v_len, &consumed,
                                 &a_text, &a_text_fill, 0);
            if (ret == 0)
                goto bad_decode;
            if (ret < 0)
                goto ex;
            ret = aaip_cleanout_st_mode(a_text, &st_mode, 4 | 16);
            if (ret < 0)
                goto ex;
            iso_node_set_perms_internal(node, st_mode, 1);

            /* Delete the attribute pair */
            if (values[i] != NULL)
                free(values[i]);
            for (j = i + 1; j < num_attrs; j++) {
                 names[j - 1] = names[j];
                 value_lengths[j - 1] = value_lengths[j];
                 values[j - 1] = values[j];
            }
            num_attrs--;
        } else {
            /* replace variable value */;
            if (values[i] != NULL)
                free(values[i]);
            values[i] = (char *) acl;
            acl = NULL;
            value_lengths[i] = acl_len;
        }

        /* Encode attributes and attach to node */
        ret = iso_node_set_attrs(node, num_attrs, names, value_lengths, values,
                                 1 | 8);
        if (ret <= 0)
            goto ex;
        goto update_perms;
    }

    /* There is no ACL yet */
    if ((flag & 2) || (access_text == NULL && default_text == NULL)) {
        /* thus no need to update ACL by st_mode or to delete ACL */
        ret = 1;
        goto ex;
    }
    ret = aaip_encode_both_acl(access_text, default_text,
                               st_mode, &acl_len, &acl, 2 | 8);
    if (ret < -3)
        goto ex;
    if (ret <= 0) {
        ret = ISO_AAIP_BAD_ACL_TEXT;
        goto ex;
    }

    ret = attr_enlarge_list(&names, &value_lengths, &values, num_attrs + 1, 0);
    if (ret < 0)
        goto ex;

    /* Set new ACL attribute */
    names[num_attrs] = strdup("");
    if (names[num_attrs] == NULL) {
        ret = ISO_OUT_OF_MEM;
        goto ex;
    }
    values[num_attrs] = (char *) acl;
    acl = NULL;
    value_lengths[num_attrs] = acl_len;
    num_attrs++;
    
    /* Encode attributes and attach to node */
    ret = iso_node_set_attrs(node, num_attrs, names, value_lengths, values,
                             1 | 8);
    if (ret < 0)
        goto ex;

update_perms:;
    if(access_text != NULL && !(flag & (1 | 2))) {
        /* Update node permissions by acl_text */
        st_mode = iso_node_get_permissions(node);
        ret = aaip_cleanout_st_mode(access_text, &st_mode, 4);
        if (ret < 0) {
            ret = ISO_AAIP_BAD_ACL_TEXT;
            goto ex;
        }
        iso_node_set_perms_internal(node, st_mode, 1);
    }

    ret = 1;
ex:;
    iso_node_get_attrs(node, &num_attrs, &names,
                       &value_lengths, &values, 1 << 15); /* free memory */
    if (a_text != NULL)
        free(a_text);
    if (d_text != NULL)
        free(d_text);
    if(acl != NULL)
       free(acl);
    return ret;

bad_decode:;
    ret = ISO_AAIP_BAD_ACL;
    goto ex;
}


mode_t iso_node_get_perms_wo_acl(const IsoNode *node)
{
    mode_t st_mode;
    int ret;
    char *a_text = NULL, *d_text = NULL;

    st_mode = iso_node_get_permissions(node);

    ret = iso_node_get_acl_text((IsoNode *) node, &a_text, &d_text, 16);
    if (ret != 1) 
        goto ex;
    aaip_cleanout_st_mode(a_text, &st_mode, 4 | 16);
ex:;
    iso_node_get_acl_text((IsoNode *) node, &a_text, &d_text, 1 << 15);
    return st_mode;
}


/* Function to identify and manage ZF parameters.
 * data is supposed to be a pointer to struct zisofs_zf_info
 */
int zisofs_zf_xinfo_func(void *data, int flag)
{
    if (flag & 1) {
        free(data);
    }
    return 1;
}

/* The iso_node_xinfo_cloner function which gets associated to
 * zisofs_zf_xinfo_func by iso_init() resp. iso_init_with_flag() via
 * iso_node_xinfo_make_clonable()
 */
int zisofs_zf_xinfo_cloner(void *old_data, void **new_data, int flag)
{
    *new_data = NULL;
    if (flag)
        return ISO_XINFO_NO_CLONE;
    if (old_data == NULL)
        return 0;
    *new_data = calloc(1, sizeof(struct zisofs_zf_info));
    if (*new_data == NULL)
        return ISO_OUT_OF_MEM;
    memcpy(*new_data, old_data, sizeof(struct zisofs_zf_info));
    return (int) sizeof(struct zisofs_zf_info);
}

/* Checks whether a file effectively bears a zisofs file header and eventually
 * marks this by a struct zisofs_zf_info as xinfo of the file node.
 * @param flag bit0= inquire the most original stream of the file
 *             bit1= permission to overwrite existing zisofs_zf_info
 *             bit2= if no zisofs header is found:
 *                   create xinfo with parameters which indicate no zisofs
 * @return 1= zf xinfo added, 0= no zisofs data found ,
 *         2= found existing zf xinfo and flag bit1 was not set
 *         <0 means error
 */
int iso_file_zf_by_magic(IsoFile *file, int flag)
{
    int ret, stream_type, header_size_div4, block_size_log2;
    uint32_t uncompressed_size;
    IsoStream *stream, *input_stream;
    struct zisofs_zf_info *zf = NULL;
    void *xipt;

    /* Intimate friendship with this function in filters/zisofs.c */
    int ziso_is_zisofs_stream(IsoStream *stream, int *stream_type,
                              int *header_size_div4, int *block_size_log2,
                              uint32_t *uncompressed_size, int flag);

    ret = iso_node_get_xinfo((IsoNode *) file, zisofs_zf_xinfo_func, &xipt);
    if (ret == 1) {
        if (!(flag & 2))
            return 2;
        ret = iso_node_remove_xinfo((IsoNode *) file, zisofs_zf_xinfo_func);
        if (ret < 0)
            return ret;
    }
    input_stream = stream = iso_file_get_stream(file);
    while (flag & 1) {
        input_stream = iso_stream_get_input_stream(stream, 0);
        if (input_stream == NULL)
    break;
        stream = input_stream;
    }
    ret = ziso_is_zisofs_stream(stream, &stream_type, &header_size_div4,
                                &block_size_log2, &uncompressed_size, 3);
    if (ret < 0)
        return ret;
    if (ret != 1 || stream_type != 2) {
        if (flag & 4)
            return 0;
        header_size_div4 = 0;
        block_size_log2 = 0;
        uncompressed_size = 0;
    }
    zf = calloc(1, sizeof(struct zisofs_zf_info));
    if (zf == NULL)
        return ISO_OUT_OF_MEM;
    zf->uncompressed_size = uncompressed_size;
    zf->header_size_div4 = header_size_div4;
    zf->block_size_log2 = block_size_log2;
    ret = iso_node_add_xinfo((IsoNode *) file, zisofs_zf_xinfo_func, zf);
    return ret;
}


/* API */
int iso_node_zf_by_magic(IsoNode *node, int flag)
{
    int ret = 1, total_ret = 0, hflag;
    IsoFile *file;
    IsoNode *pos;
    IsoDir *dir;

    if (node->type == LIBISO_FILE)
        return iso_file_zf_by_magic((IsoFile *) node, flag);
    if (node->type != LIBISO_DIR || (flag & 8))
        return 0;

    dir = (IsoDir *) node;
    pos = dir->children;
    while (pos) {
        ret = 1;
        if (pos->type == LIBISO_FILE) {
            file = (IsoFile *) pos;
            if ((flag & 16) && file->from_old_session)
                return 0;
            if (!((flag & 1) && file->from_old_session)) {
                if (strncmp(file->stream->class->type, "ziso", 4) == 0)
                    return 1; /* The stream is enough of marking */
                if (strncmp(file->stream->class->type, "osiz", 4) == 0) {
                    if (flag & 2)
                        iso_node_remove_xinfo(pos, zisofs_zf_xinfo_func);
                    return 0; /* Will not be zisofs format */
                }
            }
            hflag = flag & ~6;
            if ((flag & 1) && file->from_old_session)
                hflag |= 1;
            ret = iso_file_zf_by_magic(file, hflag);
        } else if (pos->type == LIBISO_DIR) {
            ret = iso_node_zf_by_magic(pos, flag);
        }
        if (ret < 0) {
            total_ret = ret;
            ret = iso_msg_submit(-1, ret, 0, NULL);
            if (ret < 0) {
                return ret; /* cancel due error threshold */
            }
        } else if (total_ret >= 0) {
            total_ret |= ret;
        }
        pos = pos->next;
    }
    return total_ret;
}


int iso_px_ino_xinfo_func(void *data, int flag)
{
    if (flag == 1) {
        free(data);
    }
    return 1;
}

/* The iso_node_xinfo_cloner function which gets associated to
 * iso_px_ino_xinfo_func by iso_init() resp. iso_init_with_flag() via
 * iso_node_xinfo_make_clonable()
 */
int iso_px_ino_xinfo_cloner(void *old_data, void **new_data, int flag)
{
    *new_data = NULL;
    if (flag)
        return ISO_XINFO_NO_CLONE; 
    *new_data = calloc(1, sizeof(ino_t));
    if (*new_data == NULL)
        return ISO_OUT_OF_MEM;
    memcpy(*new_data, old_data, sizeof(ino_t));
    return (int) sizeof(ino_t);
}

/*
 * @param flag
 *     bit0= do only retrieve id if node is in imported ISO image
 *           or has an explicit xinfo inode number
 * @return
 *     1= reply is valid from stream, 2= reply is valid from xinfo
 *     0= no id available,           <0= error
 *     (fs_id, dev_id, ino_id) will be (0,0,0) in case of return <= 0
 */
int iso_node_get_id(IsoNode *node, unsigned int *fs_id, dev_t *dev_id,
                    ino_t *ino_id, int flag)
{
    int ret;
    IsoFile *file;
    IsoSymlink *symlink;
    IsoSpecial *special;
    void *xipt;
    
    ret = iso_node_get_xinfo(node, iso_px_ino_xinfo_func, &xipt);
    if (ret < 0)
        goto no_id;
    if (ret == 1) {
        *fs_id = ISO_IMAGE_FS_ID;
        *dev_id = 0;
        *ino_id = *((ino_t *) xipt);
        return 2;
    }

    if (node->type == LIBISO_FILE) {
        file= (IsoFile *) node;
        iso_stream_get_id(file->stream, fs_id, dev_id, ino_id);
        if (*fs_id != ISO_IMAGE_FS_ID && (flag & 1)) {
            ret = 0;
            goto no_id;
        }
        return 1;

    } else if (node->type == LIBISO_SYMLINK) {
        symlink = (IsoSymlink *) node;
        if (symlink->fs_id != ISO_IMAGE_FS_ID && (flag & 1)) {
            ret = 0;
            goto no_id;
        }
        *fs_id = symlink->fs_id;
        *dev_id = symlink->st_dev;
        *ino_id = symlink->st_ino;
        return 1;

    } else if (node->type == LIBISO_SPECIAL) {
        special = (IsoSpecial *) node;
        if (special->fs_id != ISO_IMAGE_FS_ID && (flag & 1)) {
            ret = 0;
            goto no_id;
        }
        *fs_id = special->fs_id;
        *dev_id = special->st_dev;
        *ino_id = special->st_ino;
        return 1;

    }

    ret = 0;
no_id:;
    *fs_id = 0;
    *dev_id = 0;
    *ino_id = 0;
    return ret;
}


static
int iso_node_set_ino_xinfo(IsoNode *node, ino_t ino, int flag)
{
    int ret;
    void *xipt;

    if (flag & 1) {
        ret = iso_node_remove_xinfo(node, iso_px_ino_xinfo_func);
        if (ret < 0)
            return ret;
    }
    xipt = calloc(1, sizeof(ino_t));
    if (xipt == NULL)
        return ISO_OUT_OF_MEM;
    memcpy(xipt, &ino, sizeof(ino_t));
    ret = iso_node_add_xinfo(node, iso_px_ino_xinfo_func, xipt);
    return ret;
}

int iso_node_set_ino(IsoNode *node, ino_t ino, int flag)
{
    int ret;
    IsoFile *file;
    IsoSymlink *symlink;
    IsoSpecial *special;
    void *xipt;
    
    ret = iso_node_get_xinfo(node, iso_px_ino_xinfo_func, &xipt);
    if (ret < 0)
        return ret;
    if (ret == 1) {
        ret = iso_node_set_ino_xinfo(node, ino, 1);
        if (ret < 0)
            return ret;
        return 2;
    }
    if (node->type == LIBISO_FILE) {
        file= (IsoFile *) node;
        ret = iso_stream_set_image_ino(file->stream, ino, 0);
        if (ret < 0 || ret == 1)
            return ret;

    } else if (node->type == LIBISO_SYMLINK) {
        symlink = (IsoSymlink *) node;
        if (symlink->fs_id == ISO_IMAGE_FS_ID) {
            symlink->st_ino = ino;
            return 1;
        }

    } else if (node->type == LIBISO_SPECIAL) {
        special = (IsoSpecial *) node;
        if (special->fs_id == ISO_IMAGE_FS_ID) {
            special->st_ino = ino;
            return 1;
        }

    }
    ret = iso_node_set_ino_xinfo(node, ino, 0);
    if (ret < 0)
        return ret;
    return 2;
}


int iso_node_set_unique_id(IsoNode *node, IsoImage *image, int flag)
{
    int ret;
    ino_t ino;

    ino = img_give_ino_number(image, 0);
    ret = iso_node_set_ino(node, ino, 0);
    return ret;
}

/*
 * Note to programmers: It is crucial not to break the following constraints.
 * Anti-symmetry: cmp(X,Y) == - cmp(Y,X)
 * Transitivity : if cmp(A,B) < 0 && cmp(B,C) < 0 then cmp(A,C) < 0
 *                if cmp(A,B) == 0 && cmp(B,C) == 0 then cmp(A,C) == 0
 * A big transitivity hazard are tests which do not apply to some nodes.
 * In this case for any A that is applicable and any B that is not applicable
 * the comparison must have the same non-zero result. I.e. a pair of applicable
 * and non-applicable node must return that non-zero result before the test
 * for a pair of applicable nodes would happen.
 * 
 * @param flag
 *     bit0= compare stat properties and attributes 
 *     bit1= treat all nodes with image ino == 0 as unique
 */
int iso_node_cmp_flag(IsoNode *n1, IsoNode *n2, int flag)
{
    int ret1, ret2;
    unsigned int fs_id1, fs_id2;
    dev_t dev_id1, dev_id2;
    ino_t ino_id1, ino_id2;
    IsoFile *f1 = NULL, *f2 = NULL;
    IsoSymlink *l1 = NULL, *l2 = NULL;
    IsoSpecial *s1 = NULL, *s2 = NULL;
    void *x1, *x2;

    if (n1 == n2)
        return 0;
    if (n1->type != n2->type)
        return (n1->type < n2->type ? -1 : 1);

    /* Imported or explicite ISO image node id has priority */
    ret1 = (iso_node_get_id(n1, &fs_id1, &dev_id1, &ino_id1, 1) > 0);
    ret2 = (iso_node_get_id(n2, &fs_id2, &dev_id2, &ino_id2, 1) > 0);
    if (ret1 != ret2)
        return (ret1 < ret2 ? -1 : 1);
    if (ret1) {
        /* fs_id and dev_id do not matter here.
           Both nodes have explicit inode numbers of the emerging image.
         */
        if (ino_id1 != ino_id2)
            return (ino_id1 < ino_id2 ? -1 : 1);
        if (ino_id1 == 0) /* Image ino 0 is always unique */
            return (n1 < n2 ? -1 : 1);
        goto image_inode_match;
    }

    if (n1->type == LIBISO_FILE) {

        f1 = (IsoFile *) n1;
        f2 = (IsoFile *) n2;
        ret1 = iso_stream_cmp_ino(f1->stream, f2->stream, 0);
        if (ret1)
            return ret1;
        goto inode_match;

    } else if (n1->type == LIBISO_SYMLINK) {

        l1 = (IsoSymlink *) n1;
        l2 = (IsoSymlink *) n2;
        fs_id1 = l1->fs_id;
        dev_id1 = l1->st_dev;
        ino_id1 = l1->st_ino;
        fs_id2 = l2->fs_id;
        dev_id2 = l2->st_dev;
        ino_id2 = l2->st_ino;

    } else if (n1->type == LIBISO_SPECIAL) {

        s1 = (IsoSpecial *) n1;
        s2 = (IsoSpecial *) n2;
        fs_id1 = s1->fs_id;
        dev_id1 = s1->st_dev;
        ino_id1 = s1->st_ino;
        fs_id2 = s2->fs_id;
        dev_id2 = s2->st_dev;
        ino_id2 = s2->st_ino;

    } else {
        return (n1 < n2 ? -1 : 1); /* case n1 == n2 is handled above */
    }
    if (fs_id1 != fs_id2)
        return (fs_id1 < fs_id2 ? -1 : 1);
    if (dev_id1 != dev_id2)
        return (dev_id1 < dev_id2 ? -1 : 1);
    if (ino_id1 != ino_id2)
        return (ino_id1 < ino_id2 ? -1 : 1);
    if (fs_id1 == 0 && dev_id1 == 0 && ino_id1 == 0)
        return (n1 < n2 ? -1 : 1);

inode_match:;

    if (flag & 2) {
        /* What comes here has no predefined image ino resp. image_ino == 0 .
           Regard this as not equal.
        */
        return (n1 < n2 ? -1 : 1);
    }

image_inode_match:;

    if (!(flag & 1))
        return 0;
    if (n1->type == LIBISO_SYMLINK) {
        l1 = (IsoSymlink *) n1;
        l2 = (IsoSymlink *) n2;
        ret1 = strcmp(l1->dest, l2->dest);
        if (ret1)
            return ret1;
    } else if (n1->type == LIBISO_SPECIAL) {
        s1 = (IsoSpecial *) n1;
        s2 = (IsoSpecial *) n2;
        if (s1->dev != s2->dev)
            return (s1->dev < s2->dev ? -1 : 1);
    }

    if (n1->mode != n2->mode)
        return (n1->mode < n2->mode ? -1 : 1);
    if (n1->uid != n2->uid)
        return (n1->uid < n2->uid ? -1 : 1);
    if (n1->gid != n2->gid)
        return (n1->gid < n2->gid ? -1 : 1);
    if (n1->atime != n2->atime)
        return (n1->atime < n2->atime ? -1 : 1);
    if (n1->mtime != n2->mtime)
        return (n1->mtime < n2->mtime ? -1 : 1);
    if (n1->ctime != n2->ctime)
        return (n1->ctime < n2->ctime ? -1 : 1);

    /* Compare xinfo */
    /* :( cannot compare general xinfo because data length is not known :( */

    /* compare aa_string */
    ret1 = iso_node_get_xinfo(n1, aaip_xinfo_func, &x1);
    ret2 = iso_node_get_xinfo(n2, aaip_xinfo_func, &x2);
    if (ret1 != ret2)
        return (ret1 < ret2 ? -1 : 1);
    if (ret1 == 1) {
        ret1 = aaip_count_bytes((unsigned char *) x1, 0);
        ret2 = aaip_count_bytes((unsigned char *) x2, 0);
        if (ret1 != ret2)
            return (ret1 < ret2 ? -1 : 1);
        ret1 = memcmp(x1, x2, ret1);
        if (ret1)
            return ret1;
    }

    return 0;
}

/* API */
int iso_node_cmp_ino(IsoNode *n1, IsoNode *n2, int flag)
{
    return iso_node_cmp_flag(n1, n2, 1);
}


int iso_file_set_isofscx(IsoFile *file, unsigned int checksum_index,
                         int flag)
{
    static char *names = "isofs.cx";
    static size_t value_lengths[1] = {4};
    unsigned char value[4];
    char *valuept;
    int i, ret;

    for(i = 0; i < 4; i++)
        value[3 - i] = (checksum_index >> (8 * i)) & 0xff;
    valuept= (char *) value;
    ret = iso_node_set_attrs((IsoNode *) file, (size_t) 1,
                             &names, value_lengths, &valuept, 2 | 8);
    return ret;
}


int iso_root_set_isofsca(IsoNode *node, uint32_t start_lba, uint32_t end_lba,
                         uint32_t count, uint32_t size, char *typetext,
                         int flag)
{
    char buffer[5 + 5 + 5 + 2 + 81], *wpt = buffer, *valuept = buffer;
    int result_len, ret;
    static char *names = "isofs.ca";
    static size_t value_lengths[1];

    /* Set value of isofs.ca with
       4 byte START, 4 byte END, 4 byte COUNT, SIZE = 16,  MD5 */
    iso_util_encode_len_bytes(start_lba, wpt, 4, &result_len, 0);
    wpt += result_len;
    iso_util_encode_len_bytes(end_lba, wpt, 4, &result_len, 0);
    wpt += result_len;
    iso_util_encode_len_bytes(count, wpt, 4, &result_len, 0);
    wpt += result_len;
    iso_util_encode_len_bytes(size, wpt, 1, &result_len, 0);
    wpt += result_len;
    strncpy(wpt, typetext, 80);
    if (strlen(typetext) > 80)
        wpt += 80;
    else
        wpt += strlen(typetext);
    value_lengths[0] = wpt - buffer;
    ret = iso_node_set_attrs(node, (size_t) 1,
                             &names, value_lengths, &valuept, 2 | 8);
    return ret;
}


int iso_root_get_isofsca(IsoNode *node, uint32_t *start_lba, uint32_t *end_lba,
                         uint32_t *count, uint32_t *size, char typetext[81],
                         int flag)
{
    int ret, len;
    size_t value_len;
    char *value = NULL, *rpt;

    ret = iso_node_lookup_attr(node, "isofs.ca", &value_len, &value, 0);
    if (ret <= 0)
        goto ex;

    /* Parse value of isofs.ca with
       4 byte START, 4 byte END, 4 byte COUNT, SIZE = 16,  MD5 */
    rpt = value;
    iso_util_decode_len_bytes(start_lba, rpt, &len,
                              value_len - (rpt - value), 0);
    rpt += len + 1;
    iso_util_decode_len_bytes(end_lba, rpt, &len,
                              value_len - (rpt - value), 0);
    rpt += len + 1;
    iso_util_decode_len_bytes(count, rpt, &len,
                              value_len - (rpt - value), 0);
    rpt += len + 1;
    iso_util_decode_len_bytes(size, rpt, &len,
                              value_len - (rpt - value), 0);
    rpt += len + 1;
    len = value_len - (rpt - value);
    if (len > 80)
        len = 80;
    memcpy(typetext, rpt, len);
    typetext[len] = 0;

    ret= ISO_SUCCESS;
ex:;
    if (value != NULL)
        free(value);
    return ret;
}


/* API */
int iso_file_get_md5(IsoImage *image, IsoFile *file, char md5[16], int flag)
{
    int ret, i;
    size_t value_len;
    char *value = NULL;
    uint32_t idx = 0;
    void *xipt;

    /* xinfo MD5 overrides everything else */
    ret = iso_node_get_xinfo((IsoNode *) file, checksum_md5_xinfo_func, &xipt);
    if (ret == 1) {
        memcpy(md5, (char *) xipt, 16);
        return 1;
    }

    if (image->checksum_array == NULL)
        return 0;
    ret = iso_node_lookup_attr((IsoNode *) file, "isofs.cx",
                               &value_len, &value, 0);
    if (ret <= 0)
        goto ex;
    if (value_len > 4) {
        ret = 0;
        goto ex;
    }
    for (i = 0; i < (int) value_len; i++)
        idx = (idx << 8) | ((unsigned char *) value)[i];
    if (idx == 0 || idx > image->checksum_idx_count - 1) {
                                       /* (last index is not MD5 of a file) */
        ret = 0;
        goto ex;
    }
    if (!(flag & 1)) {
        memcpy(md5, image->checksum_array + ((size_t) 16) * ((size_t) idx),
               16);
    }
    ret = 1;
ex:;
    if (value != NULL)
        free(value);
    return ret;
}


/* API */
int iso_file_make_md5(IsoFile *file, int flag)
{
    int ret, dig = 0;
    char *md5 = NULL;

    if (file->from_old_session)
        dig = 1;
    md5= calloc(16, 1);
    ret = iso_stream_make_md5(file->stream, md5, dig);
    if (ret < 0)
        goto ex;
    iso_node_remove_xinfo((IsoNode *) file, checksum_md5_xinfo_func);
    ret = iso_node_add_xinfo((IsoNode *) file, checksum_md5_xinfo_func, md5);
    if (ret == 0)
        ret = ISO_ERROR; /* should not happen after iso_node_remove_xinfo() */
    if (ret < 0) {
        free(md5);
        goto ex;
    }
    ret = 1;
ex:;
    return ret;
}



