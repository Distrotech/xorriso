/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2009 Thomas Schmitt
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

/*
 * This file contains functions related to the reading of SUSP, 
 * Rock Ridge and AAIP extensions on an ECMA-119 image.
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "libisofs.h"
#include "ecma119.h"
#include "util.h"
#include "rockridge.h"
#include "messages.h"

#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>

struct susp_iterator
{
    uint8_t* base;
    int pos;
    int size;
    IsoDataSource *src;
    int msgid;

    /* block and offset for next continuation area */
    uint32_t ce_block;
    uint32_t ce_off;
    
    /** Length of the next continuation area, 0 if no more CA are specified */
    uint32_t ce_len; 

    uint8_t *buffer; /*< If there are continuation areas */
};

SuspIterator*
susp_iter_new(IsoDataSource *src, struct ecma119_dir_record *record,
              uint8_t len_skp, int msgid)
{
    int pad = (record->len_fi[0] + 1) % 2;
    struct susp_iterator *iter = malloc(sizeof(struct susp_iterator));
    if (iter == NULL) {
        return NULL;
    }

    iter->base = record->file_id + record->len_fi[0] + pad;
    iter->pos = len_skp; /* 0 in most cases */
    iter->size = record->len_dr[0] - record->len_fi[0] - 33 - pad;
    iter->src = src;
    iter->msgid = msgid;

    iter->ce_len = 0;
    iter->buffer = NULL;

    return iter;
}

int susp_iter_next(SuspIterator *iter, struct susp_sys_user_entry **sue)
{
    struct susp_sys_user_entry *entry;

    entry = (struct susp_sys_user_entry*)(iter->base + iter->pos);

    if ( (iter->pos + 4 > iter->size) || (SUSP_SIG(entry, 'S', 'T'))) {

        /* 
         * End of the System Use Area or Continuation Area.
         * Note that ST is not needed when the space left is less than 4.
         * (IEEE 1281, SUSP. section 4) 
         */
        if (iter->ce_len) {
            uint32_t block, nblocks;

            /* A CE has found, there is another continuation area */
            nblocks = DIV_UP(iter->ce_off + iter->ce_len, BLOCK_SIZE);
            iter->buffer = realloc(iter->buffer, nblocks * BLOCK_SIZE);

            /* read all blocks needed to cache the full CE */
            for (block = 0; block < nblocks; ++block) {
                int ret;
                ret = iter->src->read_block(iter->src, iter->ce_block + block,
                                            iter->buffer + block * BLOCK_SIZE);
                if (ret < 0) {
                    return ret;
                }
            }
            iter->base = iter->buffer + iter->ce_off;
            iter->pos = 0;
            iter->size = iter->ce_len;
            iter->ce_len = 0;
            entry = (struct susp_sys_user_entry*)iter->base;
        } else {
            return 0;
        }
    }

    if (entry->len_sue[0] == 0) {
        /* a wrong image with this lead us to a infinity loop */
        iso_msg_submit(iter->msgid, ISO_WRONG_RR, 0,
                      "Damaged RR/SUSP information.");
        return ISO_WRONG_RR;
    }

    iter->pos += entry->len_sue[0];

    if (SUSP_SIG(entry, 'C', 'E')) {
        /* Continuation entry */
        if (iter->ce_len) {
            int ret;
            ret = iso_msg_submit(iter->msgid, ISO_UNSUPPORTED_SUSP, 0,
                "More than one CE System user entry has found in a single "
                "System Use field or continuation area. This breaks SUSP "
                "standard and it's not supported. Ignoring last CE. Maybe "
                "the image is damaged.");
            if (ret < 0) {
                return ret;
            }
        } else {
            iter->ce_block = iso_read_bb(entry->data.CE.block, 4, NULL);
            iter->ce_off = iso_read_bb(entry->data.CE.offset, 4, NULL);
            iter->ce_len = iso_read_bb(entry->data.CE.len, 4, NULL);
        }

        /* we don't want to return CE entry to the user */
        return susp_iter_next(iter, sue);
    } else if (SUSP_SIG(entry, 'P', 'D')) {
        /* skip padding */
        return susp_iter_next(iter, sue);
    }

    *sue = entry;
    return ISO_SUCCESS;
}

void susp_iter_free(SuspIterator *iter)
{
    free(iter->buffer);
    free(iter);
}

/**
 * Fills a struct stat with the values of a Rock Ridge PX entry (RRIP, 4.1.1).
 * 
 * @return 
 *      1 on success, < 0 on error
 */
int read_rr_PX(struct susp_sys_user_entry *px, struct stat *st)
{
    if (px == NULL || st == NULL) {
        return ISO_NULL_POINTER;
    }
    if (px->sig[0] != 'P' || px->sig[1] != 'X') {
        return ISO_WRONG_ARG_VALUE;
    }
    
    if (px->len_sue[0] != 44 && px->len_sue[0] != 36) {
        return ISO_WRONG_RR;
    }
    
    st->st_mode = iso_read_bb(px->data.PX.mode, 4, NULL);
    st->st_nlink = iso_read_bb(px->data.PX.links, 4, NULL);
    st->st_uid = iso_read_bb(px->data.PX.uid, 4, NULL);
    st->st_gid = iso_read_bb(px->data.PX.gid, 4, NULL);
    st->st_ino = 0;
    if (px->len_sue[0] == 44) {
        /* this corresponds to RRIP 1.12, so we have inode serial number */
        st->st_ino = iso_read_bb(px->data.PX.serial, 4, NULL);
        /* Indicate that st_ino is valid */
        return 2;
    }
    return 1;
}

/**
 * Fills a struct stat with the values of a Rock Ridge TF entry (RRIP, 4.1.6)
 * 
 * @return 
 *      1 on success, < 0 on error
 */
int read_rr_TF(struct susp_sys_user_entry *tf, struct stat *st)
{
    time_t time;
    int s;
    int nts = 0;
    
    if (tf == NULL || st == NULL) {
        return ISO_NULL_POINTER;
    }
    if (tf->sig[0] != 'T' || tf->sig[1] != 'F') {
        return ISO_WRONG_ARG_VALUE;
    }
    
    if (tf->data.TF.flags[0] & (1 << 7)) {
        /* long form */
        s = 17;
    } else {
        s = 7;
    }
    
    /* 1. Creation time */
    if (tf->data.TF.flags[0] & (1 << 0)) {
        
        /* the creation is the recording time. we ignore this */
        /* TODO maybe it would be good to manage it in ms discs, where
         *      the recording time could be different than now!! */     
        ++nts;
    }
    
    /* 2. modify time */
    if (tf->data.TF.flags[0] & (1 << 1)) {
        if (tf->len_sue[0] < 5 + (nts+1) * s) {
            /* RR TF entry too short. */
            return ISO_WRONG_RR;
        }
        if (s == 7) {
            time = iso_datetime_read_7(&tf->data.TF.t_stamps[nts*7]);
        } else {
            time = iso_datetime_read_17(&tf->data.TF.t_stamps[nts*17]);
        }
        st->st_mtime = time;
        ++nts;
    }
    
    /* 3. access time */
    if (tf->data.TF.flags[0] & (1 << 2)) {
        if (tf->len_sue[0] < 5 + (nts+1) * s) {
            /* RR TF entry too short. */
            return ISO_WRONG_RR;
        }
        if (s == 7) {
            time = iso_datetime_read_7(&tf->data.TF.t_stamps[nts*7]);
        } else {
            time = iso_datetime_read_17(&tf->data.TF.t_stamps[nts*17]);
        }
        st->st_atime = time;
        ++nts;
    }
    
    /* 4. attributes time */
    if (tf->data.TF.flags[0] & (1 << 3)) {
        if (tf->len_sue[0] < 5 + (nts+1) * s) {
            /* RR TF entry too short. */
            return ISO_WRONG_RR;
        }
        if (s == 7) {
            time = iso_datetime_read_7(&tf->data.TF.t_stamps[nts*7]);
        } else {
            time = iso_datetime_read_17(&tf->data.TF.t_stamps[nts*17]);
        }
        st->st_ctime = time;
        ++nts;
    }
    
    /* we ignore backup, expire and effect times */
    
    return ISO_SUCCESS;
}

/**
 * Read a RR NM entry (RRIP, 4.1.4), and appends the name stored there to
 * the given name. You can pass a pointer to NULL as name.
 * 
 * @return
 *      1 on success, < 0 on error
 */
int read_rr_NM(struct susp_sys_user_entry *nm, char **name, int *cont)
{
    if (nm == NULL || name == NULL) {
        return ISO_NULL_POINTER;
    }
    if (nm->sig[0] != 'N' || nm->sig[1] != 'M') {
        return ISO_WRONG_ARG_VALUE;
    }
    
    if (nm->len_sue[0] == 5) {
        if (nm->data.NM.flags[0] & 0x2) {
            /* it is a "." entry */
            if (*name == NULL) {
                return ISO_SUCCESS;
            } else {
                /* we can't have a previous not-NULL name */
                return ISO_WRONG_RR;
            }
        }
    }
    
    if (nm->len_sue[0] <= 5) {
        /* ".." entry is an error, as we will never call it */
        return ISO_WRONG_RR;
    }
        
    /* concatenate the results */
    if (*cont) {
        *name = realloc(*name, strlen(*name) + nm->len_sue[0] - 5 + 1);
        strncat(*name, (char*)nm->data.NM.name, nm->len_sue[0] - 5);
    } else {
        *name = iso_util_strcopy((char*)nm->data.NM.name, nm->len_sue[0] - 5);
    }
    if (*name == NULL) {
        return ISO_OUT_OF_MEM;
    }

    /* and set cond according to the value of CONTINUE flag */
    *cont = nm->data.NM.flags[0] & 0x01;
    return ISO_SUCCESS;
}

/**
 * Read a SL RR entry (RRIP, 4.1.3), checking if the destination continues.
 * 
 * @param cont
 *      0 not continue, 1 continue, 2 continue component 
 * @return
 *      1 on success, < 0 on error
 */
int read_rr_SL(struct susp_sys_user_entry *sl, char **dest, int *cont)
{
    int pos;
    
    if (sl == NULL || dest == NULL) {
        return ISO_NULL_POINTER;
    }
    if (sl->sig[0] != 'S' || sl->sig[1] != 'L') {
        return ISO_WRONG_ARG_VALUE;
    }
    
    for (pos = 0; pos + 5 < sl->len_sue[0]; 
        pos += 2 + sl->data.SL.comps[pos + 1]) {
        char *comp;
        uint8_t len;
        uint8_t flags = sl->data.SL.comps[pos];
        
        if (flags & 0x2) {
            /* current directory */
            len = 1;
            comp = ".";
        } else if (flags & 0x4) {
            /* parent directory */
            len = 2;
            comp = "..";
        } else if (flags & 0x8) {
            /* root directory */
            len = 1;
            comp = "/";
        } else if (flags & ~0x01) {
            /* unsupported flag component */
            return ISO_UNSUPPORTED_RR;
        } else {
            len = sl->data.SL.comps[pos + 1];
            comp = (char*)&sl->data.SL.comps[pos + 2];
        } 
        
        if (*cont == 1) {
            /* new component */
            size_t size = strlen(*dest);
            *dest = realloc(*dest, strlen(*dest) + len + 2);
            if (*dest == NULL) {
                return ISO_OUT_OF_MEM;
            }
            /* it is a new compoenent, add the '/' */
            if ((*dest)[size-1] != '/') {
                (*dest)[size] = '/';
                (*dest)[size+1] = '\0';
            }
            strncat(*dest, comp, len);
        } else if (*cont == 2) {
            /* the component continues */
            *dest = realloc(*dest, strlen(*dest) + len + 1);
            if (*dest == NULL) {
                return ISO_OUT_OF_MEM;
            }
            /* we don't have to add the '/' */
            strncat(*dest, comp, len);
        } else {
            *dest = iso_util_strcopy(comp, len);
        }
        if (*dest == NULL) {
            return ISO_OUT_OF_MEM;
        }
        /* do the component continue or not? */
        *cont = (flags & 0x01) ? 2 : 1;
    }
    
    if (*cont == 2) {
        /* TODO check that SL flag is set to continute too ?*/
    } else {
        *cont = sl->data.SL.flags[0] & 0x1 ? 1 : 0;
    }
    
    return ISO_SUCCESS;
}

/**
 * Fills a struct stat with the values of a Rock Ridge PN entry (RRIP, 4.1.2).
 * 
 * @return 
 *      1 on success, < 0 on error
 */
int read_rr_PN(struct susp_sys_user_entry *pn, struct stat *st)
{
    int high_shift= 0;

    if (pn == NULL || pn == NULL) {
        return ISO_NULL_POINTER;
    }
    if (pn->sig[0] != 'P' || pn->sig[1] != 'N') {
        return ISO_WRONG_ARG_VALUE;
    }
    
    if (pn->len_sue[0] != 20) {
        return ISO_WRONG_RR;
    }

    /* (dev_t << 32) causes compiler warnings on FreeBSD
        because sizeof(dev_t) is 4.
    */
    st->st_rdev = (dev_t)iso_read_bb(pn->data.PN.low, 4, NULL);
    if (sizeof(st->st_rdev) > 4) {
        high_shift = 32;
        st->st_rdev |= (dev_t)((dev_t)iso_read_bb(pn->data.PN.high, 4, NULL) <<
                               high_shift);
    }

/* was originally:
    st->st_rdev = (dev_t)((dev_t)iso_read_bb(pn->data.PN.high, 4, NULL) << 32)
                  | (dev_t)iso_read_bb(pn->data.PN.low, 4, NULL);
*/

    return ISO_SUCCESS;
}


/* AA is the obsolete field signature of AAIP versions < 2.0
*/
int read_aaip_AA(struct susp_sys_user_entry *sue,
                 unsigned char **aa_string, size_t *aa_size, size_t *aa_len,
                 size_t *prev_field, int *is_done, int flag)
{
     unsigned char *aapt;

     if (*is_done) {

         /* To coexist with Apple ISO :
            Gracefully react on eventually trailing Apple AA
         */
         if (sue->version[0] != 1 || sue->len_sue[0] == 7)
             return ISO_SUCCESS;

         return ISO_WRONG_RR;
     }


     /* Eventually create or grow storage */
     if (*aa_size == 0 || *aa_string == NULL) {

         /* Gracefully react on eventually leading Apple AA
         */
         if (sue->version[0] != 1 || sue->len_sue[0] < 9) {
             return ISO_SUCCESS;
         }

         *aa_size = *aa_len + sue->len_sue[0];
         *aa_string = calloc(*aa_size, 1);
         *aa_len = 0;
     } else if (*aa_len + sue->len_sue[0] > *aa_size) {

         if (sue->version[0] != 1) {
             /* Apple ISO within the AAIP field group is not AAIP compliant
             */
             return ISO_WRONG_RR;
         }

         *aa_size += *aa_len + sue->len_sue[0];
         *aa_string = realloc(*aa_string, *aa_size);
     }
     if (*aa_string == NULL)
         return ISO_OUT_OF_MEM;

     if (*aa_len > 0) {
         /* Mark prev_field as being continued */
         (*aa_string)[*prev_field + 4] = 1;
     }

     *prev_field = *aa_len;

     /* Compose new SUSP header with signature aa[], cont == 0 */
     aapt = *aa_string + *aa_len;

     aapt[0] = 'A';
     aapt[1] = 'L';
     aapt[2] = sue->len_sue[0];
     aapt[3] = 1;
     aapt[4] = 0;

     /* Append sue payload */
     memcpy(aapt + 5, sue->data.AL.comps, sue->len_sue[0] - 5);
     *is_done = !(sue->data.AL.flags[0] & 1);
     *aa_len += sue->len_sue[0];

     return ISO_SUCCESS;
}


/* AL is the field signature of AAIP versions >= 2.0
*/
int read_aaip_AL(struct susp_sys_user_entry *sue,
                 unsigned char **aa_string, size_t *aa_size, size_t *aa_len,
                 size_t *prev_field, int *is_done, int flag)
{
     unsigned char *aapt;

     if (*is_done)
         return ISO_WRONG_RR;
     if (sue->version[0] != 1)
         return ISO_WRONG_RR;

     /* Eventually create or grow storage */
     if (*aa_size == 0 || *aa_string == NULL) {
         *aa_size = *aa_len + sue->len_sue[0];
         *aa_string = calloc(*aa_size, 1);
         *aa_len = 0;
     } else if (*aa_len + sue->len_sue[0] > *aa_size) {
         *aa_size += *aa_len + sue->len_sue[0];
         *aa_string = realloc(*aa_string, *aa_size);
     }
     if (*aa_string == NULL)
         return ISO_OUT_OF_MEM;

     if (*aa_len > 0) {
         /* Mark prev_field as being continued */
         (*aa_string)[*prev_field + 4] = 1;
     }

     *prev_field = *aa_len;

     /* Compose new SUSP header with signature aa[], cont == 0 */
     aapt = *aa_string + *aa_len;

     aapt[0] = 'A';
     aapt[1] = 'L';
     aapt[2] = sue->len_sue[0];
     aapt[3] = 1;
     aapt[4] = 0;

     /* Append sue payload */
     memcpy(aapt + 5, sue->data.AL.comps, sue->len_sue[0] - 5);
     *is_done = !(sue->data.AL.flags[0] & 1);
     *aa_len += sue->len_sue[0];

     return ISO_SUCCESS;
}

/**
 * Reads the zisofs parameters from a ZF field (see doc/zisofs_format.txt).
 * 
 * @return 
 *      1 on success, < 0 on error
 */
int read_zisofs_ZF(struct susp_sys_user_entry *zf, uint8_t algorithm[2],
                   uint8_t *header_size_div4, uint8_t *block_size_log2,
                   uint32_t *uncompressed_size, int flag)
{
    if (zf == NULL) {
        return ISO_NULL_POINTER;
    }
    if (zf->sig[0] != 'Z' || zf->sig[1] != 'F') {
        return ISO_WRONG_ARG_VALUE;
    }
    if (zf->len_sue[0] != 16) {
        return ISO_WRONG_RR;
    }
    algorithm[0] = zf->data.ZF.parameters[0];
    algorithm[1] = zf->data.ZF.parameters[1];
    *header_size_div4 = zf->data.ZF.parameters[2];
    *block_size_log2 = zf->data.ZF.parameters[3];
    *uncompressed_size = iso_read_bb(&(zf->data.ZF.parameters[4]), 4, NULL);
    return ISO_SUCCESS;
}


