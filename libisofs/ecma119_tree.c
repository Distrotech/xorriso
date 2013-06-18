/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2009 - 2012 Thomas Schmitt
 *
 * This file is part of the libisofs project; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

/* Must be before ecma119.h because of eventual Libisofs_with_rrip_rR */
#include "libisofs.h"

#include "ecma119_tree.h"
#include "ecma119.h"
#include "node.h"
#include "util.h"
#include "filesrc.h"
#include "messages.h"
#include "image.h"
#include "stream.h"
#include "eltorito.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static
int get_iso_name(Ecma119Image *img, IsoNode *iso, char **name)
{
    int ret, relaxed, free_ascii_name= 0, force_dots = 0;
    char *ascii_name;
    char *isoname= NULL;

    if (iso->name == NULL) {
        /* it is not necessarily an error, it can be the root */
        return ISO_SUCCESS;
    }

    if (img->untranslated_name_len > 0) {
        ascii_name = iso->name;
        ret = 1;
    } else {
        ret = str2ascii(img->input_charset, iso->name, &ascii_name);
        free_ascii_name = 1;
    }
    if (ret < 0) {
        iso_msg_submit(img->image->id, ret, 0,
                       "Cannot convert name '%s' to ASCII", iso->name);
        return ret;
    }

    if (img->allow_full_ascii) {
        relaxed = 2;
    } else {
        relaxed = (int)img->allow_lowercase;
    }
    if (img->allow_7bit_ascii)
        relaxed |= 4;
    if (iso->type == LIBISO_DIR && !(img->allow_dir_id_ext)) {
        if (img->untranslated_name_len > 0) {
            if (strlen(ascii_name) > img->untranslated_name_len) {
needs_transl:;
                iso_msg_submit(img->image->id, ISO_NAME_NEEDS_TRANSL, 0,
              "File name too long (%d > %d) for untranslated recording:  '%s'",
                               strlen(ascii_name), img->untranslated_name_len,
                               ascii_name);
                return ISO_NAME_NEEDS_TRANSL;
            }
            isoname = strdup(ascii_name);
        } else if (img->max_37_char_filenames) {
            isoname = iso_r_dirid(ascii_name, 37, relaxed);
        } else if (img->iso_level == 1) {

#ifdef Libisofs_old_ecma119_nameS

            if (relaxed) {
                isoname = iso_r_dirid(ascii_name, 8, relaxed);
            } else {
                isoname = iso_1_dirid(ascii_name, 0);
            }

#else /* Libisofs_old_ecma119_nameS */

            isoname = iso_1_dirid(ascii_name, relaxed);

#endif /* ! Libisofs_old_ecma119_nameS */


        } else {
            if (relaxed) {
                isoname = iso_r_dirid(ascii_name, 31, relaxed);
            } else {
                isoname = iso_2_dirid(ascii_name);
            }
        }
    } else {
        force_dots = !((img->no_force_dots & 1) || iso->type == LIBISO_DIR);
        if (img->untranslated_name_len > 0) {
            if (strlen(ascii_name) > img->untranslated_name_len)
                goto needs_transl;
            isoname = strdup(ascii_name);
        } else if (img->max_37_char_filenames) {
            isoname = iso_r_fileid(ascii_name, 36, relaxed, force_dots);
        } else if (img->iso_level == 1) {

#ifdef Libisofs_old_ecma119_nameS

            int max_len;

            if (relaxed) {
                if (strchr(ascii_name, '.') == NULL)
                    max_len = 8;
                else
                    max_len = 11;
                isoname = iso_r_fileid(ascii_name, max_len, relaxed,
                                       force_dots);
            } else {
                isoname = iso_1_fileid(ascii_name, 0, force_dots);
            }

#else /* Libisofs_old_ecma119_nameS */

            isoname = iso_1_fileid(ascii_name, relaxed, force_dots);

#endif /* ! Libisofs_old_ecma119_nameS */

        } else {
            if (relaxed || !force_dots) {
                isoname = iso_r_fileid(ascii_name, 30, relaxed, force_dots);
            } else {
                isoname = iso_2_fileid(ascii_name);
            }
        }
    }
    if (free_ascii_name)
        free(ascii_name);
    if (isoname != NULL) {
        *name = isoname;
        return ISO_SUCCESS;
    } else {
        /*
         * only possible if mem error, as check for empty names is done
         * in public tree
         */
        return ISO_OUT_OF_MEM;
    }
}

int ecma119_is_dedicated_reloc_dir(Ecma119Image *img, Ecma119Node *node)
{
    if (img->rr_reloc_node == node &&
        node != img->root && node != img->partition_root &&
        (img->rr_reloc_flags & 2))
        return 1;
    return 0;
}

static
int create_ecma119_node(Ecma119Image *img, IsoNode *iso, Ecma119Node **node)
{
    Ecma119Node *ecma;

    ecma = calloc(1, sizeof(Ecma119Node));
    if (ecma == NULL) {
        return ISO_OUT_OF_MEM;
    }

    ecma->node = iso;
    iso_node_ref(iso);
    ecma->nlink = 1;
    *node = ecma;
    return ISO_SUCCESS;
}

/**
 * Create a new ECMA-119 node representing a directory from a iso directory
 * node.
 */
static
int create_dir(Ecma119Image *img, IsoDir *iso, Ecma119Node **node)
{
    int ret;
    Ecma119Node **children;
    struct ecma119_dir_info *dir_info;

    children = calloc(1, sizeof(void*) * iso->nchildren);
    if (children == NULL) {
        return ISO_OUT_OF_MEM;
    }

    dir_info = calloc(1, sizeof(struct ecma119_dir_info));
    if (dir_info == NULL) {
        free(children);
        return ISO_OUT_OF_MEM;
    }

    ret = create_ecma119_node(img, (IsoNode*)iso, node);
    if (ret < 0) {
        free(children);
        free(dir_info);
        return ret;
    }
    (*node)->type = ECMA119_DIR;
    (*node)->info.dir = dir_info;
    (*node)->info.dir->nchildren = 0;
    (*node)->info.dir->children = children;
    return ISO_SUCCESS;
}


static
int create_file_src(Ecma119Image *img, IsoFile *iso, IsoFileSrc **src)
{
    int ret;
    off_t size;

    size = iso_stream_get_size(iso->stream);
    if (size > (off_t)MAX_ISO_FILE_SECTION_SIZE && img->iso_level != 3) {
        char *ipath = iso_tree_get_node_path(ISO_NODE(iso));
        ret = iso_msg_submit(img->image->id, ISO_FILE_TOO_BIG, 0,
                              "File \"%s\" can't be added to image because "
                              "is greater than 4GB", ipath);
        free(ipath);
        return ret;
    }
    ret = iso_file_src_create(img, iso, src);
    if (ret < 0) {
        return ret;
    }
    return 0;
}


/**
 * Create a new ECMA-119 node representing a regular file from a iso file
 * node.
 */
static
int create_file(Ecma119Image *img, IsoFile *iso, Ecma119Node **node)
{
    int ret;
    IsoFileSrc *src;

    ret = create_file_src(img, iso, &src);
    if (ret < 0) {
        return ret;
    }

    ret = create_ecma119_node(img, (IsoNode*)iso, node);
    if (ret < 0) {
        /*
         * the src doesn't need to be freed, it is free together with
         * the Ecma119Image
         */
        return ret;
    }
    (*node)->type = ECMA119_FILE;
    (*node)->info.file = src;

    return ret;
}

/**
 * Create a new ECMA-119 node representing a regular file from an El-Torito
 * boot catalog
 */
static
int create_boot_cat(Ecma119Image *img, IsoBoot *iso, Ecma119Node **node)
{
    int ret;
    IsoFileSrc *src;

    ret = el_torito_catalog_file_src_create(img, &src);
    if (ret < 0) {
        return ret;
    }

    ret = create_ecma119_node(img, (IsoNode*)iso, node);
    if (ret < 0) {
        /*
         * the src doesn't need to be freed, it is free together with
         * the Ecma119Image
         */
        return ret;
    }
    (*node)->type = ECMA119_FILE;
    (*node)->info.file = src;

    return ret;
}

/**
 * Create a new ECMA-119 node representing a symbolic link from a iso symlink
 * node.
 */
static
int create_symlink(Ecma119Image *img, IsoSymlink *iso, Ecma119Node **node)
{
    int ret;

    ret = create_ecma119_node(img, (IsoNode*)iso, node);
    if (ret < 0) {
        return ret;
    }
    (*node)->type = ECMA119_SYMLINK;
    return ISO_SUCCESS;
}

/**
 * Create a new ECMA-119 node representing a special file.
 */
static
int create_special(Ecma119Image *img, IsoSpecial *iso, Ecma119Node **node)
{
    int ret;

    ret = create_ecma119_node(img, (IsoNode*)iso, node);
    if (ret < 0) {
        return ret;
    }
    (*node)->type = ECMA119_SPECIAL;
    return ISO_SUCCESS;
}

void ecma119_node_free(Ecma119Node *node)
{
    if (node == NULL) {
        return;
    }
    if (node->type == ECMA119_DIR) {
        size_t i;
        for (i = 0; i < node->info.dir->nchildren; i++) {
            ecma119_node_free(node->info.dir->children[i]);
        }
        free(node->info.dir->children);
        free(node->info.dir);
    }
    free(node->iso_name);
    iso_node_unref(node->node);
    free(node);
}

/**
 * @param flag
 *      bit0= iso is in a hidden directory. Thus hide it.
 * @return
 *      1 success, 0 node ignored,  < 0 error
 *
 */
static
int create_tree(Ecma119Image *image, IsoNode *iso, Ecma119Node **tree,
                int depth, int pathlen, int flag)
{
    int ret, hidden;
    Ecma119Node *node = NULL;
    int max_path;
    char *iso_name= NULL, *ipath = NULL;
    IsoFileSrc *src = NULL;

    if (image == NULL || iso == NULL || tree == NULL) {
        return ISO_NULL_POINTER;
    }
    *tree = NULL;

    hidden = flag & 1;
    if (iso->hidden & LIBISO_HIDE_ON_RR) {
        hidden = 1;
        if (!((iso->hidden & LIBISO_HIDE_BUT_WRITE) ||
              iso->type == LIBISO_BOOT)) {
            return 0; /* file will be ignored */
        }
    }

    if (hidden) {
        max_path= pathlen;
    } else {
        ret = get_iso_name(image, iso, &iso_name);
        if (ret < 0) {
            iso_name = NULL; /* invalid, do not free */
            goto ex;
        }
        max_path = pathlen + 1 + (iso_name ? strlen(iso_name) : 0);
        if (!image->rockridge) {
            if ((iso->type == LIBISO_DIR && depth > 8) &&
                !image->allow_deep_paths) {
	        ipath = iso_tree_get_node_path(iso);
                ret = iso_msg_submit(image->image->id, ISO_FILE_IMGPATH_WRONG,
                                     0, "File \"%s\" can't be added, "
                                     "because directory depth "
                                     "is greater than 8.", ipath);
                goto ex;
            } else if (max_path > 255 && !image->allow_longer_paths) {
                ipath = iso_tree_get_node_path(iso);
                ret = iso_msg_submit(image->image->id, ISO_FILE_IMGPATH_WRONG,
                                     0, "File \"%s\" can't be added, "
                                     "because path length "
                                     "is greater than 255 characters", ipath);
                goto ex;
            }
        }
    }

    switch (iso->type) {
    case LIBISO_FILE:
        if (hidden) {
            ret = create_file_src(image, (IsoFile *) iso, &src);
        } else {
            ret = create_file(image, (IsoFile*)iso, &node);
        }
        break;
    case LIBISO_SYMLINK:
        if (hidden) {
            ret = 0; /* Hidden means non-existing */
            goto ex;
        }
        if (image->rockridge) {
            ret = create_symlink(image, (IsoSymlink*)iso, &node);
        } else {
            /* symlinks are only supported when RR is enabled */
            char *ipath = iso_tree_get_node_path(iso);
            ret = iso_msg_submit(image->image->id, ISO_FILE_IGNORED, 0,
                "File \"%s\" ignored. Symlinks need RockRidge extensions.",
                ipath);
            free(ipath);
        }
        break;
    case LIBISO_SPECIAL:
        if (hidden) {
            ret = 0; /* Hidden means non-existing */
            goto ex;
        }
        if (image->rockridge) {
            ret = create_special(image, (IsoSpecial*)iso, &node);
        } else {
            /* special files are only supported when RR is enabled */
            char *ipath = iso_tree_get_node_path(iso);
            ret = iso_msg_submit(image->image->id, ISO_FILE_IGNORED, 0,
                "File \"%s\" ignored. Special files need RockRidge extensions.",
                ipath);
            free(ipath);
        }
        break;
    case LIBISO_BOOT:
        if (image->eltorito) {
            if (hidden) {
                ret = el_torito_catalog_file_src_create(image, &src);
            } else {
                ret = create_boot_cat(image, (IsoBoot*)iso, &node);
            }
        } else {
            /* log and ignore */
            ret = iso_msg_submit(image->image->id, ISO_FILE_IGNORED, 0,
                "El-Torito catalog found on a image without El-Torito.");
        }
        break;
    case LIBISO_DIR:
        {
            IsoNode *pos;
            IsoDir *dir = (IsoDir*)iso;

            if (!hidden) {
                ret = create_dir(image, dir, &node);
                if (ret < 0) {
                    goto ex;
                }
                if (depth == 1) { /* root is default */
                    image->rr_reloc_node = node;
                } else if (depth == 2) {
                    /* Directories in root may be used as relocation dir */
                    if (image->rr_reloc_dir != NULL)
                        if (image->rr_reloc_dir[0] != 0 &&
                            strcmp(iso->name, image->rr_reloc_dir) == 0)
                            image->rr_reloc_node = node;
                }
            }
            ret = ISO_SUCCESS;
            pos = dir->children;
            while (pos) {
                int cret;
                Ecma119Node *child;
                cret = create_tree(image, pos, &child, depth + 1, max_path,
                                   !!hidden);
                if (cret < 0) {
                    /* error */
                    ret = cret;
                    break;
                } else if (cret == ISO_SUCCESS && !hidden) {
                    /* add child to this node */
                    int nchildren = node->info.dir->nchildren++;
                    node->info.dir->children[nchildren] = child;
                    child->parent = node;
                }
                pos = pos->next;
            }
        }
        break;
    default:
        /* should never happen */
        ret = ISO_ASSERT_FAILURE;
        goto ex;
    }
    if (ret <= 0) {
        goto ex;
    }
    if (!hidden) {
        node->iso_name = iso_name;
        iso_name = NULL; /* now owned by node, do not free */
        *tree = node;
        node = NULL;     /* now owned by caller, do not free */
    }
    ret = ISO_SUCCESS;
ex:
    if (iso_name != NULL)
        free(iso_name);
    if (ipath != NULL)
        free(ipath);
    if (node != NULL)
        ecma119_node_free(node);
    if (hidden && ret == ISO_SUCCESS)
        ret = 0;
    /* The sources of hidden files are now owned by the rb-tree */
    return ret;
}

/**
 * Compare the iso name of two ECMA-119 nodes
 */
static
int cmp_node_name(const void *f1, const void *f2)
{
    Ecma119Node *f = *((Ecma119Node**)f1);
    Ecma119Node *g = *((Ecma119Node**)f2);
    return strcmp(f->iso_name, g->iso_name);
}

/**
 * Sorts a the children of each directory in the ECMA-119 tree represented
 * by \p root, acording to the order specified in ECMA-119, section 9.3.
 */
static
void sort_tree(Ecma119Node *root)
{
    size_t i;

    qsort(root->info.dir->children, root->info.dir->nchildren, sizeof(void*),
          cmp_node_name);
    for (i = 0; i < root->info.dir->nchildren; i++) {
        if (root->info.dir->children[i]->type == ECMA119_DIR)
            sort_tree(root->info.dir->children[i]);
    }
}

/**
 * Ensures that the ISO name of each children of the given dir is unique,
 * changing some of them if needed.
 * It also ensures that resulting filename is always <= than given
 * max_name_len, including extension. If needed, the extension will be reduced,
 * but never under 3 characters.
 */
static
int mangle_single_dir(Ecma119Image *img, Ecma119Node *dir, int max_file_len,
                      int max_dir_len)
{
    int ret;
    int i, nchildren;
    Ecma119Node **children;
    IsoHTable *table;
    int need_sort = 0;

    nchildren = dir->info.dir->nchildren;
    children = dir->info.dir->children;

    /* a hash table will temporary hold the names, for fast searching */
    ret = iso_htable_create((nchildren * 100) / 80, iso_str_hash,
                            (compare_function_t)strcmp, &table);
    if (ret < 0) {
        return ret;
    }
    for (i = 0; i < nchildren; ++i) {
        char *name = children[i]->iso_name;
        ret = iso_htable_add(table, name, name);
        if (ret < 0) {
            goto mangle_cleanup;
        }
    }

    for (i = 0; i < nchildren; ++i) {
        char *name, *ext;
        char full_name[40];
        int max; /* computed max len for name, without extension */
        int j = i;
        int digits = 1; /* characters to change per name */

        /* first, find all child with same name */
        while (j + 1 < nchildren && !cmp_node_name(children + i, children + j
                + 1)) {
            ++j;
        }
        if (j == i) {
            /* name is unique */
            continue;
        }

        if (img->untranslated_name_len) {
            /* This should not happen because no two IsoNode names should be
               identical and only unaltered IsoNode names should be seen here.
               Thus the Ema119Node names should be unique.
            */
            iso_msg_submit(img->image->id, ISO_NAME_NEEDS_TRANSL, 0,
                           "ECMA-119 file name collision: '%s'",
                           children[i]->iso_name);
            ret = ISO_NAME_NEEDS_TRANSL;
            goto mangle_cleanup;
        }

        /*
         * A max of 7 characters is good enought, it allows handling up to
         * 9,999,999 files with same name. We can increment this to
         * max_name_len, but the int_pow() function must then be modified
         * to return a bigger integer.
         */
        while (digits < 8) {
            int ok, k;
            char *dot;
            int change = 0; /* number to be written */

            /* copy name to buffer */
            strcpy(full_name, children[i]->iso_name);

            /* compute name and extension */
            dot = strrchr(full_name, '.');
            if (dot != NULL &&
                (children[i]->type != ECMA119_DIR || img->allow_dir_id_ext)) {

                /*
                 * File (normally not dir) with extension
                 * Note that we don't need to check for placeholders, as
                 * tree reparent happens later, so no placeholders can be
                 * here at this time.
                 */
                int extlen;
                full_name[dot - full_name] = '\0';
                name = full_name;
                ext = dot + 1;

                /*
                 * For iso level 1 we force ext len to be 3, as name
                 * can't grow on the extension space
                 */
                extlen = (max_file_len == 12) ? 3 : strlen(ext);
                max = max_file_len - extlen - 1 - digits;
                if (max <= 0) {
                    /* this can happen if extension is too long */
                    if (extlen + max > 3) {
                        /*
                         * reduce extension len, to give name an extra char
                         * note that max is negative or 0
                         */
                        extlen = extlen + max - 1;
                        ext[extlen] = '\0';
                        max = max_file_len - extlen - 1 - digits;
                    } else {
                        /*
                         * error, we don't support extensions < 3
                         * This can't happen with current limit of digits.
                         */
                        ret = ISO_ERROR;
                        goto mangle_cleanup;
                    }
                }
                /* ok, reduce name by digits */
                if (name + max < dot) {
                    name[max] = '\0';
                }
            } else {
                /* Directory (normally), or file without extension */
                if (children[i]->type == ECMA119_DIR) {
                    max = max_dir_len - digits;
                    dot = NULL; /* dots (normally) have no meaning in dirs */
                } else {
                    max = max_file_len - digits;
                }
                name = full_name;
                if ((size_t) max < strlen(name)) {
                    name[max] = '\0';
                }
                /* let ext be an empty string */
                ext = name + strlen(name);
            }

            ok = 1;
            /* change name of each file */
            for (k = i; k <= j; ++k) {
                char tmp[40];
                char fmt[16];
                if (dot != NULL) {
                    sprintf(fmt, "%%s%%0%dd.%%s", digits);
                } else {
                    sprintf(fmt, "%%s%%0%dd%%s", digits);
                }
                while (1) {
                    sprintf(tmp, fmt, name, change, ext);
                    ++change;
                    if (change > int_pow(10, digits)) {
                        ok = 0;
                        break;
                    }
                    if (!iso_htable_get(table, tmp, NULL)) {
                        /* the name is unique, so it can be used */
                        break;
                    }
                }
                if (ok) {
                    char *new = strdup(tmp);
                    if (new == NULL) {
                        ret = ISO_OUT_OF_MEM;
                        goto mangle_cleanup;
                    }

#ifdef Libisofs_extra_verbose_debuG
                    iso_msg_debug(img->image->id, "\"%s\" renamed to \"%s\"",
                                  children[k]->iso_name, new);
#endif

                    iso_htable_remove_ptr(table, children[k]->iso_name, NULL);
                    free(children[k]->iso_name);
                    children[k]->iso_name = new;
                    iso_htable_add(table, new, new);

                    /*
                     * if we change a name we need to sort again children
                     * at the end
                     */
                    need_sort = 1;
                } else {
                    /* we need to increment digits */
                    break;
                }
            }
            if (ok) {
                break;
            } else {
                ++digits;
            }
        }
        if (digits == 8) {
            ret = ISO_MANGLE_TOO_MUCH_FILES;
            goto mangle_cleanup;
        }
        i = j;
    }

    /*
     * If needed, sort again the files inside dir
     */
    if (need_sort) {
        qsort(children, nchildren, sizeof(void*), cmp_node_name);
    }

    ret = ISO_SUCCESS;

mangle_cleanup : ;
    iso_htable_destroy(table, NULL);
    return ret;
}

static
int mangle_dir(Ecma119Image *img, Ecma119Node *dir, int max_file_len,
               int max_dir_len)
{
    int ret;
    size_t i;

    ret = mangle_single_dir(img, dir, max_file_len, max_dir_len);
    if (ret < 0) {
        return ret;
    }

    /* recurse */
    for (i = 0; i < dir->info.dir->nchildren; ++i) {
        if (dir->info.dir->children[i]->type == ECMA119_DIR) {
            ret = mangle_dir(img, dir->info.dir->children[i], max_file_len,
                             max_dir_len);
            if (ret < 0) {
                /* error */
                return ret;
            }
        }
    }
    return ISO_SUCCESS;
}

static
int mangle_tree(Ecma119Image *img, Ecma119Node *dir, int recurse)
{
    int max_file, max_dir;
    Ecma119Node *root;

    if (img->untranslated_name_len > 0) {
        max_file = max_dir = img->untranslated_name_len;
    } else if (img->max_37_char_filenames) {
        max_file = max_dir = 37;
    } else if (img->iso_level == 1) {
        max_file = 12; /* 8 + 3 + 1 */
        max_dir = 8;
    } else {
        max_file = max_dir = 31;
    }
    if (dir != NULL) {
        root = dir;
    } else if (img->eff_partition_offset > 0) {
        root = img->partition_root;
    } else {
        root = img->root;
    }
    if (recurse) {
        return mangle_dir(img, root, max_file, max_dir);
    } else {
        return mangle_single_dir(img, root, max_file, max_dir);
    }
}

/**
 * Create a new ECMA-119 node representing a placeholder for a relocated
 * dir.
 *
 * See IEEE P1282, section 4.1.5 for details
 */
static
int create_placeholder(Ecma119Node *parent, Ecma119Node *real,
                       Ecma119Node **node)
{
    Ecma119Node *ret;

    ret = calloc(1, sizeof(Ecma119Node));
    if (ret == NULL) {
        return ISO_OUT_OF_MEM;
    }

    /*
     * TODO
     * If real is a dir, while placeholder is a file, ISO name restricctions
     * are different, what to do?
     */
    ret->iso_name = strdup(real->iso_name);
    if (ret->iso_name == NULL) {
        free(ret);
        return ISO_OUT_OF_MEM;
    }

    /* take a ref to the IsoNode */
    ret->node = real->node;
    iso_node_ref(real->node);
    ret->parent = parent;
    ret->type = ECMA119_PLACEHOLDER;
    ret->info.real_me = real;
    ret->ino = real->ino;
    ret->nlink = real->nlink;

    *node = ret;
    return ISO_SUCCESS;
}

static
size_t max_child_name_len(Ecma119Node *dir)
{
    size_t ret = 0, i;
    for (i = 0; i < dir->info.dir->nchildren; i++) {
        size_t len = strlen(dir->info.dir->children[i]->iso_name);
        ret = MAX(ret, len);
    }
    return ret;
}

/**
 * Relocates a directory, as specified in Rock Ridge Specification
 * (see IEEE P1282, section 4.1.5). This is needed when the number of levels
 * on a directory hierarchy exceeds 8, or the length of a path is higher
 * than 255 characters, as specified in ECMA-119, section 6.8.2.1
 */
static
int reparent(Ecma119Node *child, Ecma119Node *parent)
{
    int ret;
    size_t i;
    Ecma119Node *placeholder;

    /* replace the child in the original parent with a placeholder */
    for (i = 0; i < child->parent->info.dir->nchildren; i++) {
        if (child->parent->info.dir->children[i] == child) {
            ret = create_placeholder(child->parent, child, &placeholder);
            if (ret < 0) {
                return ret;
            }
            child->parent->info.dir->children[i] = placeholder;
            break;
        }
    }

    /* just for debug, this should never happen... */
    if (i == child->parent->info.dir->nchildren) {
        return ISO_ASSERT_FAILURE;
    }

    /* keep track of the real parent */
    child->info.dir->real_parent = child->parent;

    /* add the child to its new parent */
    child->parent = parent;
    parent->info.dir->nchildren++;
    parent->info.dir->children = realloc(parent->info.dir->children,
                                 sizeof(void*) * parent->info.dir->nchildren);
    parent->info.dir->children[parent->info.dir->nchildren - 1] = child;
    return ISO_SUCCESS;
}

/**
 * Reorder the tree, if necessary, to ensure that
 *  - the depth is at most 8
 *  - each path length is at most 255 characters
 * This restriction is imposed by ECMA-119 specification (ECMA-119, 6.8.2.1).
 *
 * @param dir
 *      Dir we are currently processing
 * @param level
 *      Level of the directory in the hierarchy
 * @param pathlen
 *      Length of the path until dir, including it
 * @return
 *      1 success, < 0 error
 */
static
int reorder_tree(Ecma119Image *img, Ecma119Node *dir,
                 int dir_level, int dir_pathlen)
{
    int ret, level, pathlen, newpathlen;
    size_t max_path, i;
    Ecma119Node *reloc, *child;

    /* might change by relocation */
    level = dir_level;
    pathlen = dir_pathlen;

    max_path = pathlen + 1 + max_child_name_len(dir);

    if (level > 8 || max_path > 255) {
        reloc = img->rr_reloc_node;
        if (reloc == NULL) {
            if (img->eff_partition_offset > 0) {
                reloc = img->partition_root;
            } else {
                reloc = img->root;
            }
        }
        ret = reparent(dir, reloc);
        if (ret < 0) {
            return ret;
        }

        if (reloc == img->root || reloc == img->partition_root) {
            /*
             * we are appended to the root's children now, so there is no
             * need to recurse (the root will hit us again)
             */
            return ISO_SUCCESS;
        }

        /* dir is now the relocated Ecma119Node */
        pathlen = 37 + 1; /* The dir name might get longer by mangling */
        level = 2;
        if (img->rr_reloc_dir != NULL) {
            pathlen += strlen(img->rr_reloc_node->iso_name) + 1;
            if(img->rr_reloc_dir[0] != 0)
              level = 3;
        }
    }

    if (ecma119_is_dedicated_reloc_dir(img, (Ecma119Node *) dir))
        return ISO_SUCCESS;

    for (i = 0; i < dir->info.dir->nchildren; i++) {
        child = dir->info.dir->children[i];
        if (child->type == ECMA119_DIR) {
            newpathlen = pathlen + 1 + strlen(child->iso_name);
            ret = reorder_tree(img, child, level + 1, newpathlen);
            if (ret < 0)
                return ret;
        }
    }
    return ISO_SUCCESS;
}

/*
 * @param flag
 *     bit0= recursion
 *     bit1= count nodes rather than fill them into *nodes
 * @return
 *     <0 error
 *     bit0= saw ino == 0
 *     bit1= saw ino != 0
 */
static
int make_node_array(Ecma119Image *img, Ecma119Node *dir,
                    Ecma119Node **nodes, size_t nodes_size, size_t *node_count,
                    int flag)
{
    int ret, result = 0;
    size_t i;
    Ecma119Node *child;

    if (!(flag & 1)) {
        *node_count = 0;
        if (!(flag & 2)) {
            /* Register the tree root node */
            if (*node_count >= nodes_size) {
                iso_msg_submit(img->image->id, ISO_ASSERT_FAILURE, 0,
                         "Programming error: Overflow of hardlink sort array");
                return ISO_ASSERT_FAILURE;
            }
            nodes[*node_count] = dir;
        }
        result|= (dir->ino == 0 ? 1 : 2);
        (*node_count)++;
    }
        
    for (i = 0; i < dir->info.dir->nchildren; i++) {
        child = dir->info.dir->children[i];
        if (!(flag & 2)) {
            if (*node_count >= nodes_size) {
                iso_msg_submit(img->image->id, ISO_ASSERT_FAILURE, 0,
                         "Programming error: Overflow of hardlink sort array");
                return ISO_ASSERT_FAILURE;
            }
            nodes[*node_count] = child;
        }
        result|= (child->ino == 0 ? 1 : 2);
        (*node_count)++;

        if (child->type == ECMA119_DIR) {
            ret = make_node_array(img, child,
                                  nodes, nodes_size, node_count, flag | 1);
            if (ret < 0)
                return ret;
        }
    }
    return result;
}

/*
 * @param flag
 *     bit0= compare stat properties and attributes 
 *     bit1= treat all nodes with image ino == 0 as unique
 */
static
int ecma119_node_cmp_flag(const void *v1, const void *v2, int flag)
{
    int ret;
    Ecma119Node *n1, *n2;

    n1 = *((Ecma119Node **) v1);
    n2 = *((Ecma119Node **) v2);
    if (n1 == n2)
        return 0;

    ret = iso_node_cmp_flag(n1->node, n2->node, flag & (1 | 2));
    return ret;
}

static 
int ecma119_node_cmp_hard(const void *v1, const void *v2)
{
    return ecma119_node_cmp_flag(v1, v2, 1);
}   

static 
int ecma119_node_cmp_nohard(const void *v1, const void *v2)
{
    return ecma119_node_cmp_flag(v1, v2, 1 | 2);
}   

static
int family_set_ino(Ecma119Image *img, Ecma119Node **nodes, size_t family_start,
                   size_t next_family, ino_t img_ino, ino_t prev_ino, int flag)
{
    size_t i;

    if (img_ino != 0) {
        /* Check whether this is the same img_ino as in the previous
           family (e.g. by property divergence of imported hardlink).
        */
        if (img_ino == prev_ino)
            img_ino = 0;
    }
    if (img_ino == 0) {
        img_ino = img_give_ino_number(img->image, 0);
    }
    for (i = family_start; i < next_family; i++) {
        nodes[i]->ino = img_ino;
        nodes[i]->nlink = next_family - family_start;
    }
    return 1;
}

static
int match_hardlinks(Ecma119Image *img, Ecma119Node *dir, int flag)
{
    int ret;
    size_t nodes_size = 0, node_count = 0, i, family_start;
    Ecma119Node **nodes = NULL;
    unsigned int fs_id;
    dev_t dev_id;
    ino_t img_ino = 0, prev_ino = 0;

    ret = make_node_array(img, dir, nodes, nodes_size, &node_count, 2);
    if (ret < 0)
        return ret;
    nodes_size = node_count;
    nodes = (Ecma119Node **) calloc(sizeof(Ecma119Node *), nodes_size);
    if (nodes == NULL)
        return ISO_OUT_OF_MEM;
    ret = make_node_array(img, dir, nodes, nodes_size, &node_count, 0);
    if (ret < 0)
        goto ex;

    /* Sort according to id tuples, IsoFileSrc identity, properties, xattr. */
    if (img->hardlinks)
        qsort(nodes, node_count, sizeof(Ecma119Node *), ecma119_node_cmp_hard);
    else
        qsort(nodes, node_count, sizeof(Ecma119Node *),
              ecma119_node_cmp_nohard);

    /* Hand out image inode numbers to all Ecma119Node.ino == 0 .
       Same sorting rank gets same inode number.
       Split those image inode number families where the sort criterion
       differs.
    */
    iso_node_get_id(nodes[0]->node, &fs_id, &dev_id, &img_ino, 1);
    family_start = 0;
    for (i = 1; i < node_count; i++) {
        if (nodes[i]->type != ECMA119_DIR &&
            ecma119_node_cmp_hard(nodes + (i - 1), nodes + i) == 0) {
            /* Still in same ino family */
            if (img_ino == 0) { /* Just in case any member knows its img_ino */
                iso_node_get_id(nodes[0]->node, &fs_id, &dev_id, &img_ino, 1);
            }
    continue;
        }
        family_set_ino(img, nodes, family_start, i, img_ino, prev_ino, 0);
        prev_ino = img_ino;
        iso_node_get_id(nodes[i]->node, &fs_id, &dev_id, &img_ino, 1);
        family_start = i;
    }
    family_set_ino(img, nodes, family_start, i, img_ino, prev_ino, 0);

    ret = ISO_SUCCESS;
ex:;
    if (nodes != NULL)
        free((char *) nodes);
    return ret;
}

int ecma119_tree_create(Ecma119Image *img)
{
    int ret;
    Ecma119Node *root;

    ret = create_tree(img, (IsoNode*)img->image->root, &root, 1, 0, 0);
    if (ret <= 0) {
        if (ret == 0) {
            /* unexpected error, root ignored!! This can't happen */
            ret = ISO_ASSERT_FAILURE;
        }
        return ret;
    }
    if (img->eff_partition_offset > 0) {
        img->partition_root = root;
    } else {
        img->root = root;
    }

    iso_msg_debug(img->image->id, "Matching hardlinks...");
    ret = match_hardlinks(img, root, 0);
    if (ret < 0) {
        return ret;
    }

    iso_msg_debug(img->image->id, "Sorting the low level tree...");
    sort_tree(root);

    iso_msg_debug(img->image->id, "Mangling names...");
    ret = mangle_tree(img, NULL, 1);
    if (ret < 0) {
        return ret;
    }

    if (img->rockridge && !img->allow_deep_paths) {

        /* Relocate deep directories, acording to RRIP, 4.1.5 */
        ret = reorder_tree(img, root, 1, 0);
        if (ret < 0) {
            return ret;
        }

        /*
         * and we need to remangle the root directory, as the function
         * above could insert new directories into the relocation directory.
         * Note that recurse = 0, as we don't need to recurse.
         */
        ret = mangle_tree(img, img->rr_reloc_node, 0);
        if (ret < 0) {
            return ret;
        }
    }

    return ISO_SUCCESS;
}

/**
 * Search the tree for a certain IsoNode and return its owning Ecma119Node
 * or NULL.
 */
static
Ecma119Node *search_iso_node(Ecma119Node *root, IsoNode *node)
{
    size_t i;
    Ecma119Node *res = NULL;

    if (root->node == node)
        return root;
    for (i = 0; i < root->info.dir->nchildren && res == NULL; i++) {
        if (root->info.dir->children[i]->type == ECMA119_DIR)
            res = search_iso_node(root->info.dir->children[i], node);
        else if (root->info.dir->children[i]->node == node)
            res = root->info.dir->children[i];
    }
    return res;
}


Ecma119Node *ecma119_search_iso_node(Ecma119Image *img, IsoNode *node)
{
    Ecma119Node *res = NULL;

    if (img->root != NULL)
        res = search_iso_node(img->root, node);
    return res;
}

