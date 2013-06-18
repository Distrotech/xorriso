/*
 * libjte.c
 *
 * Copyright (c) 2010-2011 Thomas Schmitt <scdbackup@gmx.net>
 *
 * API functions of libjte
 *
 * GNU LGPL v2.1 (including option for GPL v2 or later)
 *
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif

#include <errno.h>


#include "jte.h"
#include "checksum.h"

#include "libjte_private.h"
#include "libjte.h"



/* @param flag bit0= do not free *dest before overwriting it
*/
static int libjte__set_string(char **dest, char *source, int flag)
{
    if (*dest != NULL && !(flag & 1))
        free(*dest);
    *dest = NULL;
    if (source == NULL)
        return 1;
    *dest = strdup(source);
    if (*dest == NULL)
        return -1;
    return 1;
}


/* Version inquiry API */

void libjte__version(int *major, int *minor, int *micro)
{
    *major = LIBJTE_VERSION_MAJOR;
    *minor = LIBJTE_VERSION_MINOR;
    *micro = LIBJTE_VERSION_MICRO;
}


int libjte__is_compatible(int major, int minor, int micro, int flag)
{
 int own_major, own_minor, own_micro;

 libjte__version(&own_major, &own_minor, &own_micro);
 return(own_major > major ||
        (own_major == major && (own_minor > minor ||
         (own_minor == minor && own_micro >= micro))));
}


/* Life cycle API */


int libjte_new(struct libjte_env **jte_handle, int flag)
{
    struct libjte_env *o;

    if (sizeof(off_t) < 8) {
        fprintf(stderr, "libjte: Fatal compile time misconfiguration. sizeof(off_t) = %d too small.\n\n", (int) sizeof(off_t));
        return -1;
    }

    *jte_handle = o = calloc(1, sizeof(struct libjte_env));
    if (o == NULL)
        return -1;
    o->jtemplate_out = NULL;
    o->jjigdo_out = NULL;
    o->jmd5_list = NULL;
    o->jtjigdo = NULL;
    o->jttemplate = NULL;
    o->jte_min_size = MIN_JIGDO_FILE_SIZE;
    o->checksum_algo_iso =  (CHECK_MD5_USED | \
                             CHECK_SHA1_USED | \
                             CHECK_SHA256_USED | \
                             CHECK_SHA512_USED);
    o->checksum_algo_tmpl = CHECK_MD5_USED;
    o->jte_template_compression = JTE_TEMP_GZIP;
    o->exclude_list = NULL;
    o->include_list = NULL;
    o->map_list = NULL;
    o->template_size = 0;
    o->image_size = 0;
    o->iso_context = NULL;
    o->template_context = NULL;
    o->entry_list = NULL;
    o->entry_last = NULL;
    o->t_file = NULL;
    o->j_file = NULL;
    o->num_matches = 0;
    o->num_chunks = 0;
    o->md5_list = NULL;
    o->md5_last = NULL;
    o->include_in_jigdo = 0;
    memset(o->message_buffer, 0, sizeof(o->message_buffer));
    o->error_behavior = 1; /* Print to stderr, do not exit but return -1 */
    o->msg_list = 0;
    o->uncomp_buf = NULL;
    o->uncomp_size = 0;
    o->uncomp_buf_used = 0;
    return 1;
}

int libjte_destroy(struct libjte_env **jte_handle)
{
    struct libjte_env *o;

    o = *jte_handle;
    if (o == NULL)
        return 0;

    free(o->outfile);
    free(o->jtemplate_out);
    free(o->jjigdo_out);
    free(o->jmd5_list);
    if (o->jtjigdo != NULL)
        fclose(o->jtjigdo);
    if (o->jttemplate != NULL)
        fclose(o->jttemplate);

    libjte_destroy_path_match_list(o, 0); /* include_list */
    libjte_destroy_path_match_list(o, 1); /* exclude_list */
    libjte_destroy_path_mapping(o, 0);

    if (o->iso_context != NULL)
        checksum_free_context(o->iso_context);
    if (o->template_context != NULL)
        checksum_free_context(o->template_context);

    /* >>> TODO : get rid of the following odd situation
       o->j_file and o->t_file seem to be only copies of
       o->jttemplate and o->jtjigdo */

    libjte_destroy_entry_list(o, 0);
    libjte_destroy_md5_list(o, 0);

    libjte_clear_msg_list(o, 1 | 2); /* dump pending messages to stderr */
    free(o->uncomp_buf);
    free(o);
    *jte_handle = NULL;
    return 1;
}

/* Setup API */

int libjte_set_outfile(struct libjte_env *o, char *outfile)
{
    return libjte__set_string(&(o->outfile), outfile, 0);
}

int libjte_set_verbose(struct libjte_env *o, int verbose)
{
    o->verbose = !!verbose;
    return 1;
}

int libjte_set_template_path(struct libjte_env *o, char *jtemplate_out)
{
    return libjte__set_string(&(o->jtemplate_out), jtemplate_out, 0);
}

int libjte_set_jigdo_path(struct libjte_env *o, char *jjigdo_out)
{
    return libjte__set_string(&(o->jjigdo_out), jjigdo_out, 0);
}

int libjte_set_md5_path(struct libjte_env *o, char *jmd5_list)
{
    return libjte__set_string(&(o->jmd5_list), jmd5_list, 0);
}

int libjte_set_min_size(struct libjte_env *o, int jte_min_size)
{
    o->jte_min_size = jte_min_size;
    return 1;
}

static int libjte_decode_checksum_string(struct libjte_env *o,
                                         char *checksum_code, int default_algo,
                                         int *algo)
{

#ifdef NIX

    *algo = 0;
    if (strcmp(checksum_code, "default") != 0) {
        if (strstr(checksum_code, "md5") != NULL)
            *algo |= CHECK_MD5_USED;
        if (strstr(checksum_code, "sha1") != NULL)
            *algo |= CHECK_SHA1_USED;
        if (strstr(checksum_code, "sha256") != NULL)
            *algo |= CHECK_SHA256_USED;
        if (strstr(checksum_code, "sha512") != NULL)
            *algo |= CHECK_SHA512_USED;
    }
    if (*algo == 0)
        *algo = default_algo;

#else /* NIX */

    int ret;

    ret = parse_checksum_algo(checksum_code, algo);
    if (ret) {
        *algo = default_algo;
        sprintf(o->message_buffer, "Invalid checksum algorithm name in '%s'",
                checksum_code);
        libjte_add_msg_entry(o, o->message_buffer, 0);
        return 0;
    }

#endif /* ! NIX */

    return 1;
}

int libjte_set_checksum_iso(struct libjte_env *o, char *checksum_code)
{
    int ret;
    int checksum_algo_iso = (CHECK_MD5_USED | 
                             CHECK_SHA1_USED | 
                             CHECK_SHA256_USED |
                             CHECK_SHA512_USED);

    ret = libjte_decode_checksum_string(o, checksum_code, checksum_algo_iso,
                                        &checksum_algo_iso);
    if (ret <= 0)
        return ret;
    o->checksum_algo_iso = checksum_algo_iso;
    return 1;
}

int libjte_set_checksum_template(struct libjte_env *o, char *checksum_code)
{
    int ret;
    int checksum_algo_tmpl = CHECK_MD5_USED;

    ret = libjte_decode_checksum_string(o, checksum_code, checksum_algo_tmpl,
                                        &checksum_algo_tmpl);
    if (ret <= 0)
        return ret;
    o->checksum_algo_tmpl = checksum_algo_tmpl;
    return 1;
}

int libjte_set_compression(struct libjte_env *o, char *compression_code)
{
    jtc_t compr = JTE_TEMP_GZIP;

    if (strcmp(compression_code, "default") != 0) {
        if (strcmp(compression_code, "gzip") == 0)
            compr = JTE_TEMP_GZIP;
        else if (strcmp(compression_code, "bzip2") ==0)
            compr = JTE_TEMP_BZIP2;
        else {
            sprintf(o->message_buffer,
                    "libjte: Unknown compression code. Known: gzip bzip2");
            libjte_add_msg_entry(o, o->message_buffer, 0);
            return 0;
        }
    }

#ifndef LIBJTE_WITH_LIBBZ2

    if (compr == JTE_TEMP_BZIP2) {
        sprintf(o->message_buffer,
                "libjte: Usage of libbz2 not enabled at compile time\n");
        libjte_add_msg_entry(o, o->message_buffer, 0);
        return 0;
    }

#endif /* LIBJTE_WITH_LIBBZ2 */
 
    o->jte_template_compression = compr;
    return 1;
}

int libjte_add_exclude(struct libjte_env *o, char *pattern)
{
    int ret;

    ret = jte_add_exclude(o, pattern);
    return !ret;
}

int libjte_add_md5_demand(struct libjte_env *o, char *pattern)
{
    int ret;

    ret = jte_add_include(o, pattern);
    return !ret;
}

int libjte_add_mapping(struct libjte_env *o, char *arg)
{
    int ret;

    ret = jte_add_mapping(o, arg);
    return !ret;
}

int libjte_set_image_size(struct libjte_env *o, off_t image_size)
{
    o->image_size = image_size;
    return 1;
}


/* Operation API */

int libjte_write_header(struct libjte_env *o)
{
    int ret;

    if (o->jtemplate_out == NULL || o->jjigdo_out == NULL ||
        o->outfile == NULL || o->jmd5_list == NULL) {
        sprintf(o->message_buffer,
               "Undefined: template_path, jigdo_path, md5_paths, or outfile.");
        libjte_add_msg_entry(o, o->message_buffer, 0);
        return 0;
    }
    
    o->jttemplate = fopen(o->jtemplate_out, "wb");
    if (o->jttemplate == NULL) {
        sprintf(o->message_buffer,
                "Cannot open template file '%1.1024s' for writing. errno=%d",
                o->jtemplate_out, errno);
        libjte_add_msg_entry(o, o->message_buffer, 0);
        return 0;
    }
    o->jtjigdo = fopen(o->jjigdo_out, "wb");
    if (o->jtjigdo == NULL) {
        sprintf(o->message_buffer,
                "Cannot open jigdo file '%1.1024s' for writing. errno=%d",
                o->jjigdo_out, errno);
        libjte_add_msg_entry(o, o->message_buffer, 0);
        return 0;
    }

    ret = write_jt_header(o, o->jttemplate, o->jtjigdo);
    if (ret <= 0)
        return ret;
    return 1;
}

int libjte_write_footer(struct libjte_env *o)
{
    int ret;

    ret = write_jt_footer(o);
    if (o->jtjigdo != NULL)
        fclose(o->jtjigdo);
    if (o->jttemplate != NULL)
        fclose(o->jttemplate);
    o->jtjigdo = NULL;
    o->jttemplate = NULL;
    if (ret <= 0)
        return ret;
    return 1;
}

/* Traditional Data File API */

int libjte_write_unmatched(struct libjte_env *o, void *buffer, int size,
                            int count)
{
    int ret;

    ret = jtwrite(o, buffer, size, count);
    return ret;
}

int libjte_write_match_record(struct libjte_env *o,
                            char *filename, char *mirror_name, int sector_size,
                            off_t size, unsigned char md5[16])
{
    int ret;

    ret = write_jt_match_record(o, filename, mirror_name, sector_size, size,
                                md5);
    if (ret <= 0)
        return ret;
    return 1;
}

int libjte_decide_file_jigdo(struct libjte_env *o,
                             char *filename, off_t size, char **mirror_name,
                             unsigned char md5[16])
{
    int ret;
    char *md5_name;

    *mirror_name = NULL;
    ret = list_file_in_jigdo(o, filename, size, &md5_name, md5);
    if (ret <= 0)
        return ret;
    *mirror_name = strdup(md5_name);
    if (*mirror_name == NULL) {
        libjte_report_no_mem(o, strlen(md5_name) + 1, 0);
        return -1;
    }
    return 1;
}

/* Simplified Data File API */

int libjte_begin_data_file(struct libjte_env *o, char *filename,
                           int sector_size, off_t size)
{
    int ret;
    char *mirror_name;
    unsigned char md5[16];

    o->include_in_jigdo = 0;
    ret = list_file_in_jigdo(o, filename, size, &mirror_name, md5);
    if (ret < 0)
        return ret;
    if (ret == 0)
        return 2;
    write_jt_match_record(o, filename, mirror_name, sector_size,
                          size, md5);
    o->include_in_jigdo = 1;
    return 1;
}

int libjte_show_data_chunk(struct libjte_env *o,
                           void *buffer, int sector_size,
                           int count)
{
    o->image_size += count * sector_size;
    if (o->include_in_jigdo)
        return 2;
    jtwrite(o, buffer, sector_size, count);
    return 1;
}

int libjte_end_data_file(struct libjte_env *o)
{
    o->include_in_jigdo = 0;
    return 1;
}


/* Error reporting API */

int libjte_set_error_behavior(struct libjte_env *o,
                              int to_stderr, int with_exit)
{
    o->error_behavior = 0;
    if (to_stderr > 0)
        o->error_behavior |= 1;
    if (with_exit> 0)
        o->error_behavior |= 2;
    return 1;
}

char *libjte_get_next_message(struct libjte_env *o)
{
    jigdo_msg_entry_t *old_entry;
    char *msg;

    if (o->msg_list == NULL)
        return NULL;
    old_entry = o->msg_list;
    o->msg_list = old_entry->next;
    msg = old_entry->message;
    free(old_entry);
    return msg;
}

/* @param flag bit0= print pending messages to stderr
*/
int libjte_clear_msg_list(struct libjte_env *o, int flag)
{
    char *msg;

    if ((flag & 2) && o->msg_list != NULL)
        fprintf(stderr,
                    "libjte: -- have to dump error messages to stderr --\n");
    while (1) {
        msg = libjte_get_next_message(o);
        if (msg == NULL)
    break;
        if (flag & 1)
            fprintf(stderr, "libjte: %s\n", msg);
        free(msg);
    }
    return 1;
}

