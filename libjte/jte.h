/*
 * jte.c
 *
 * Copyright (c) 2004-2006 Steve McIntyre <steve@einval.com>
 * Copyright (c) 2010 Thomas Schmitt <scdbackup@gmx.net>
 * Copyright (c) 2010 George Danchev <danchev@spnet.net>
 *
 * Prototypes and declarations for JTE
 *
 * GNU GPL v2
 */

#ifndef _JTE_JTE_H_
#define _JTE_JTE_H_

/* The API environment handle which replaces the old global variables */
struct libjte_env;

#include <stdio.h>
#include <unistd.h>
#include <regex.h>

typedef int BOOL;


extern int write_jt_header(struct libjte_env *o,
                            FILE *template_file, FILE *jigdo_file);
extern int write_jt_footer(struct libjte_env *o);
extern int jtwrite(struct libjte_env *o,
                            void *buffer, int size, int count);
extern int write_jt_match_record(struct libjte_env *o,
                            char *filename, char *mirror_name, int sector_size,
                            off_t size, unsigned char md5[16]);
extern int  list_file_in_jigdo(struct libjte_env *o,
                            char *filename, off_t size, char **realname,
                            unsigned char md5[16]);
extern int  jte_add_exclude(struct libjte_env *o, char *pattern);
extern int  jte_add_include(struct libjte_env *o, char *pattern);
extern int  jte_add_mapping(struct libjte_env *o, char *arg);

int libjte_destroy_path_match_list(struct libjte_env *o, int flag);
int libjte_destroy_path_mapping(struct libjte_env *o, int flag);
int libjte_destroy_entry_list(struct libjte_env *o, int flag);
int libjte_destroy_md5_list(struct libjte_env *o, int flag);

int libjte_report_no_mem(struct libjte_env *o, size_t size, int flag);


typedef enum _jtc_e
{
    JTE_TEMP_GZIP = 0,
    JTE_TEMP_BZIP2
} jtc_t;



#define MIN_JIGDO_FILE_SIZE 1024

/*	
	Simple list to hold the results of -jigdo-exclude and
	-jigdo-force-match command line options. Seems easiest to do this
	using regexps.
*/
struct path_match
{
    regex_t  match_pattern;
    char    *match_rule;
    struct path_match *next;
};

/* List of mappings e.g. Debian=/mirror/debian */
struct path_mapping
{
    char                *from;
    char                *to;
    struct path_mapping *next;
};

/* List of files that we've seen, ready to write into the template and
   jigdo files */
typedef struct _file_entry
{
    unsigned char       md5[16];
    off_t               file_length;
    uint64_t            rsyncsum;
    char               *filename;
} file_entry_t;

typedef struct _unmatched_entry
{
    off_t uncompressed_length;
} unmatched_entry_t;    

typedef struct _entry
{
    int entry_type; /* JTET_TYPE as above */
    struct _entry *next;
    union
    {
        file_entry_t      file;
        unmatched_entry_t chunk;
    } data;
} entry_t;

typedef struct _jigdo_file_entry
{
    unsigned char type;
    unsigned char fileLen[6];
    unsigned char fileRsync[8];
    unsigned char fileMD5[16];
} jigdo_file_entry_t;

typedef struct _jigdo_chunk_entry
{
    unsigned char type;
    unsigned char skipLen[6];
} jigdo_chunk_entry_t;

typedef struct _jigdo_image_entry
{
    unsigned char type;
    unsigned char imageLen[6];
    unsigned char imageMD5[16];
    unsigned char blockLen[4];
} jigdo_image_entry_t;

typedef struct _md5_list_entry
{
    struct _md5_list_entry *next;
    unsigned char       MD5[16];
    uint64_t size;
    char               *filename;
} md5_list_entry_t;


typedef struct _jigdo_msg_entry
{
    struct _jigdo_msg_entry *next;
    char *message;
} jigdo_msg_entry_t;
int libjte_add_msg_entry(struct libjte_env *o, char *message, int flag);
/* Destructor is API call  libjte_clear_msg_list() */

#endif
/*_JTE_JTE_H_*/
