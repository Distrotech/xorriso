/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2007 Mario Danic
 * Copyright (c) 2009 Thomas Schmitt
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

/**
 * This header defines the functions and structures needed to add RockRidge
 * extensions to an ISO image. It also handles AAIP and zisofs extensions.
 * 
 * References:
 * 
 * - SUSP (IEEE 1281).
 * System Use Sharing Protocol, draft standard version 1.12.
 * See ftp://ftp.ymi.com/pub/rockridge/susp112.ps
 * 
 * - RRIP (IEEE 1282)
 * Rock Ridge Interchange Protocol, Draft Standard version 1.12.
 * See ftp://ftp.ymi.com/pub/rockridge/rrip112.ps
 * 
 * - ECMA-119 (ISO-9660)
 * Volume and File Structure of CDROM for Information Interchange. See
 * http://www.ecma-international.org/publications/files/ECMA-ST/Ecma-119.pdf
 *
 * - AAIP
 * Arbitrary Attribute Interchange Protocol. See doc/susp_aaip_2_0.txt
 *
 * - zisofs
 * Blockwise compression of data file content with transparent read support
 * in the Linux kernel. See doc/zisofs_format.txt
 *
 */

#ifndef LIBISO_ROCKRIDGE_H
#define LIBISO_ROCKRIDGE_H

#include "ecma119.h"


#define SUSP_SIG(entry, a, b) ((entry->sig[0] == a) && (entry->sig[1] == b))

/**
 * This contains the information about the System Use Fields (SUSP, 4.1), 
 * that will be written in the System Use Areas, both in the ISO directory
 * record System Use field (ECMA-119, 9.1.13) or in a Continuation Area as
 * defined by SUSP.
 */
struct susp_info
{
    /** Number of SUSP fields in the System Use field */
    size_t n_susp_fields;
    uint8_t **susp_fields;

    /** Length of the part of the SUSP area that fits in the dirent. */
    int suf_len;

    /** Length of the part of the SUSP area that will go in a CE area. */
    uint32_t ce_block;
    uint32_t ce_len;

    size_t n_ce_susp_fields;
    uint8_t **ce_susp_fields;
};

/* SUSP 5.1 */
struct susp_CE {
    uint8_t block[8];
    uint8_t offset[8];
    uint8_t len[8];
};

/* SUSP 5.3 */
struct susp_SP {
    uint8_t be[1];
    uint8_t ef[1];
    uint8_t len_skp[1];
};

/* SUSP 5.5 */
struct susp_ER {
    uint8_t len_id[1];
    uint8_t len_des[1];
    uint8_t len_src[1];
    uint8_t ext_ver[1];
    uint8_t ext_id[1]; /*< up to len_id bytes */
    /* ext_des, ext_src */
};

/** POSIX file attributes (RRIP, 4.1.1) */
struct rr_PX {
    uint8_t mode[8];
    uint8_t links[8];
    uint8_t uid[8];
    uint8_t gid[8];
    uint8_t serial[8];
};

/** Time stamps for a file (RRIP, 4.1.6) */
struct rr_TF {
    uint8_t flags[1];
    uint8_t t_stamps[1];
};

/** Info for character and block device (RRIP, 4.1.2) */
struct rr_PN {
    uint8_t high[8];
    uint8_t low[8];
};

/** Alternate name (RRIP, 4.1.4) */
struct rr_NM {
    uint8_t flags[1];
    uint8_t name[1];
};

/** Link for a relocated directory (RRIP, 4.1.5.1) */
struct rr_CL {
    uint8_t child_loc[8];
};

/** Sim link (RRIP, 4.1.3) */
struct rr_SL {
    uint8_t flags[1];
    uint8_t comps[1];
};


/** Outdated Arbitrary Attribute (AAIP, see doc/susp_aaip_1_0.txt)
 *  It collided with pre-SUSP Apple AA field.
 */
struct aaip_AA {
    uint8_t flags[1];
    uint8_t comps[1];
};

/** Arbitrary Attribute (AAIP, see doc/susp_aaip_2_0.txt) */
struct aaip_AL {
    uint8_t flags[1];
    uint8_t comps[1];
};


/** zisofs entry  (see doc/zisofs_format.txt) */
struct zisofs_ZF {
    uint8_t parameters[1]; /* begins with BP 5 */
};


/**
 * Struct for a SUSP System User Entry (SUSP, 4.1)
 */
struct susp_sys_user_entry
{
    uint8_t sig[2];
    uint8_t len_sue[1];
    uint8_t version[1];
    union {
        struct susp_CE CE;
        struct susp_SP SP;
        struct susp_ER ER;
        struct rr_PX PX;
        struct rr_TF TF;
        struct rr_PN PN;
        struct rr_NM NM;
        struct rr_CL CL;
        struct rr_SL SL;
        struct aaip_AA AA;
        struct aaip_AL AL;
        struct zisofs_ZF ZF;
    } data; /* 5 to 4+len_sue */
};

/**
 * Compute the length needed for write all RR and SUSP entries for a given
 * node.
 * 
 * @param type
 *      0 normal entry, 1 "." entry for that node (it is a dir), 2 ".."
 *      for that node (i.e., it will refer to the parent)
 * @param space
 *      Available space in the System Use Area for the directory record.
 * @param ce
 *      Will be filled with the space needed in a CE
 * @return
 *      The size needed for the RR entries in the System Use Area
 */
size_t rrip_calc_len(Ecma119Image *t, Ecma119Node *n, int type, size_t space,
                     size_t *ce);

/**
 * Fill a struct susp_info with the RR/SUSP entries needed for a given
 * node.
 * 
 * @param type
 *      0 normal entry, 1 "." entry for that node (it is a dir), 2 ".."
 *      for that node (i.e., it will refer to the parent)
 * @param space
 *      Available space in the System Use Area for the directory record.
 * @param info
 *      Pointer to the struct susp_info where the entries will be stored.
 *      If some entries need to go to a Continuation Area, they will be added
 *      to the existing ce_susp_fields, and ce_len will be incremented
 *      propertly. Please ensure ce_block is initialized propertly.
 * @return
 *      1 success, < 0 error
 */
int rrip_get_susp_fields(Ecma119Image *t, Ecma119Node *n, int type,
                         size_t space, struct susp_info *info);

/**
 * Write the given SUSP fields into buf. Note that Continuation Area
 * fields are not written.
 * If info does not contain any SUSP entry this function just return. 
 * After written, the info susp_fields array will be freed, and the counters
 * updated propertly.
 */
void rrip_write_susp_fields(Ecma119Image *t, struct susp_info *info,
                            uint8_t *buf);

/**
 * Write the Continuation Area entries for the given struct susp_info, using
 * the iso_write() function.
 * After written, the ce_susp_fields array will be freed.
 */
int rrip_write_ce_fields(Ecma119Image *t, struct susp_info *info);

/**
 * The SUSP iterator is used to iterate over the System User Entries
 * of a ECMA-168 directory record.
 * It takes care about Continuation Areas, handles the end of the different
 * system user entries and skip padding areas. Thus, using an iteration
 * we are accessing just to the meaning entries.
 */
typedef struct susp_iterator SuspIterator;

SuspIterator *
susp_iter_new(IsoDataSource *src, struct ecma119_dir_record *record, 
              uint8_t len_skp, int msgid);

/**
 * Get the next SUSP System User Entry using given iterator.
 * 
 * @param sue
 *      Pointer to the next susp entry. It refers to an internal buffer and 
 *      it's not guaranteed to be allocated after calling susp_iter_next() 
 *      again. Thus, if you need to keep some entry you have to do a copy.
 * @return
 *      1 on success, 0 if no more entries, < 0 error
 */
int susp_iter_next(SuspIterator *iter, struct susp_sys_user_entry **sue);

/**
 * Free a given susp iterator.
 */
void susp_iter_free(SuspIterator *iter);


/**
 * Fills a struct stat with the values of a Rock Ridge PX entry (RRIP, 4.1.1).
 * 
 * @return 
 *    < 0 on error
 *      1 on success with no inode number,
 *      2 on success with inode number,
 */
int read_rr_PX(struct susp_sys_user_entry *px, struct stat *st);

/**
 * Fills a struct stat with the values of a Rock Ridge TF entry (RRIP, 4.1.6)
 * 
 * @return 
 *      1 on success, < 0 on error
 */
int read_rr_TF(struct susp_sys_user_entry *tf, struct stat *st);

/**
 * Read a RR NM entry (RRIP, 4.1.4), and appends the name stored there to
 * the given name. You can pass a pointer to NULL as name.
 * 
 * @return
 *      1 on success, < 0 on error
 */
int read_rr_NM(struct susp_sys_user_entry *nm, char **name, int *cont);

/**
 * Read a SL RR entry (RRIP, 4.1.3), checking if the destination continues.
 * 
 * @param cont
 *      0 not continue, 1 continue, 2 continue component 
 * @return
 *      1 on success, < 0 on error
 */
int read_rr_SL(struct susp_sys_user_entry *sl, char **dest, int *cont);

/**
 * Fills a struct stat with the values of a Rock Ridge PN entry (RRIP, 4.1.2).
 * 
 * @return 
 *      1 on success, < 0 on error
 */
int read_rr_PN(struct susp_sys_user_entry *pn, struct stat *st);


/**
 * Collects the AAIP field string from single AAIP fields.
 * (see doc/susp_aaip_1_0.txt)
 * @param aa_string   Storage location of the emerging string.
 *                    Begin with *aa_string == NULL, or own malloc() storage.
 * @param aa_size     Current allocated size of aa_string.
 *                    Begin with *aa_size == 0, or own storage size.
 * @param aa_len      Current occupied size of aa_string.
 *                    Begin with *aa_len == 0
 * @param prev_field  Returns the index of start of the previous field
 *                    in the string.
 * @param is_done     The current completion state of the AAIP field string.
 *                    Fields will be ignored as soon as it is 1.
 *                    Begin with *is_done == 0
 * @param flag        Unused yet. Submit 0.
 * @return 
 *      1 on success, < 0 on error
 */
int read_aaip_AA(struct susp_sys_user_entry *sue,
                 unsigned char **aa_string, size_t *aa_size, size_t *aa_len,
                 size_t *prev_field, int *is_done, int flag);

/**
 * Collects the AAIP field string from single AL fields.
 * (see doc/susp_aaip_2_0.txt)
 */
int read_aaip_AL(struct susp_sys_user_entry *sue,
                 unsigned char **aa_string, size_t *aa_size, size_t *aa_len,
                 size_t *prev_field, int *is_done, int flag);

/**
 * Reads the zisofs parameters from a ZF field (see doc/zisofs_format.txt).
 *
 * @return
 *      1 on success, < 0 on error
 */
int read_zisofs_ZF(struct susp_sys_user_entry *zf, uint8_t algorithm[2],
                   uint8_t *header_size_div4, uint8_t *block_size_log2,
                   uint32_t *uncompressed_size, int flag);

#endif /* LIBISO_ROCKRIDGE_H */
