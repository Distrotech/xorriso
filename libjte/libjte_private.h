/*
 * libjte_private.h
 *
 * Copyright (c) 2010 Thomas Schmitt <scdbackup@gmx.net>
 *
 * Environment structure for libjte
 *
 * GNU LGPL v2.1 (including option for GPL v2 or later)
 *
 */


#ifndef LIBJTE_PRIVATE_H_INCLUDED
#define LIBJTE_PRIVATE_H_INCLUDED 1

/* Opaque handles */
struct  path_match;
struct  path_mapping;


struct libjte_env {
    char *outfile;
    int  verbose;
    char *jtemplate_out;
    char *jjigdo_out;
    char *jmd5_list;
    FILE *jtjigdo;
    FILE *jttemplate;
    int  jte_min_size;
    int  checksum_algo_iso;
    int  checksum_algo_tmpl;
    jtc_t jte_template_compression;
    struct  path_match *exclude_list;
    struct  path_match *include_list;
    struct  path_mapping  *map_list;
    uint64_t template_size;
    uint64_t image_size;
    checksum_context_t *iso_context;
    checksum_context_t *template_context;
    entry_t *entry_list;
    entry_t *entry_last;
    FILE    *t_file;
    FILE    *j_file;
    int      num_matches;
    int      num_chunks;
    md5_list_entry_t *md5_list;
    md5_list_entry_t *md5_last;

    int include_in_jigdo; /* 0= put data blocks into .template, 1= do not */

    char message_buffer[4096];
    int error_behavior; /* bit0= report messages to stderr rather than to list
                           bit1= perform traditional exit(1)
                         */
    jigdo_msg_entry_t *msg_list;

    /* Static variables from write_compressed_chunk() */
    unsigned char *uncomp_buf;
    size_t uncomp_size;
    size_t uncomp_buf_used;
};


#endif /* LIBJTE_PRIVATE_H_INCLUDED */
