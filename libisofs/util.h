/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2009 - 2012 Thomas Schmitt
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

#ifndef LIBISO_UTIL_H_
#define LIBISO_UTIL_H_

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif

#include <time.h>

#ifndef MAX
#   define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef MIN
#   define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#define DIV_UP(n,div) ((n + div - 1) / div)
#define ROUND_UP(n,mul) (DIV_UP(n, mul) * mul)

int int_pow(int base, int power);

/**
 * Set up locale by LC_* environment variables.
 */
int iso_init_locale(int flag);

/**
 * Convert the charset encoding of a given string.
 * 
 * @param input
 *      Input string
 * @param icharset
 *      Input charset. Must be supported by iconv
 * @param ocharset
 *      Output charset. Must be supported by iconv
 * @param output
 *      Location where the pointer to the ouput string will be stored
 * @return
 *      1 on success, < 0 on error
 */
int strconv(const char *input, const char *icharset, const char *ocharset,
            char **output);

int strnconv(const char *str, const char *icharset, const char *ocharset,
             size_t len, char **output);

/**
 * Convert a given string from any input charset to ASCII
 * 
 * @param icharset
 *      Input charset. Must be supported by iconv
 * @param input
 *      Input string
 * @param output
 *      Location where the pointer to the ouput string will be stored
 * @return
 *      1 on success, < 0 on error
 */
int str2ascii(const char *icharset, const char *input, char **output);

/**
 * Convert a given string from any input charset to UCS-2BE charset,
 * used for Joliet file identifiers.
 * 
 * @param icharset
 *      Input charset. Must be supported by iconv
 * @param input
 *      Input string
 * @param output
 *      Location where the pointer to the ouput string will be stored
 * @return
 *      1 on success, < 0 on error
 */
int str2ucs(const char *icharset, const char *input, uint16_t **output);

/**
 * Create a level 1 directory identifier.
 * 
 * @param src
 *      The identifier, in ASCII encoding.
 * @param relaxed
 *     0 only allow d-characters, 1 allow also lowe case chars, 
 *     2 allow all characters 
 */
char *iso_1_dirid(const char *src, int relaxed);

/**
 * Create a level 2 directory identifier.
 * 
 * @param src
 *      The identifier, in ASCII encoding.
 */
char *iso_2_dirid(const char *src);

/**
 * Create a dir name suitable for an ISO image with relaxed constraints.
 * 
 * @param src
 *      The identifier, in ASCII encoding.
 * @param size
 *     Max len for the name
 * @param relaxed
 *     0 only allow d-characters, 1 allow also lowe case chars, 
 *     2 allow all characters 
 */
char *iso_r_dirid(const char *src, int size, int relaxed);

/**
 * Create a level 1 file identifier that consists of a name, in 8.3 
 * format.
 * Note that version number is not added to the file name
 * 
 * @param src
 *      The identifier, in ASCII encoding.
 * @param relaxed
 *     0 only allow d-characters, 1 allow also lowe case chars, 
 *     2 allow all characters 
 * @param force_dots
 *      If 1 then prepend empty extension by SEPARATOR1 = '.'
 */
char *iso_1_fileid(const char *src, int relaxed, int force_dots);

/**
 * Create a level 2 file identifier.
 * Note that version number is not added to the file name
 * 
 * @param src
 *      The identifier, in ASCII encoding.
 */
char *iso_2_fileid(const char *src);

/**
 * Create a file name suitable for an ISO image with relaxed constraints.
 * 
 * @param src
 *      The identifier, in ASCII encoding.
 * @param len
 *     Max len for the name, without taken the "." into account.
 * @param relaxed
 *     0 only allow d-characters, 1 allow also lowe case chars, 
 *     2 allow all characters 
 * @param forcedot
 *     Whether to ensure that "." is added
 */
char *iso_r_fileid(const char *src, size_t len, int relaxed, int forcedot);

/**
 * Create a Joliet file identifier that consists of name and extension. The 
 * combined name and extension length will normally not exceed 64 characters
 * (= 128 bytes). The name and the extension will be separated (.).
 * All characters consist of 2 bytes and the resulting string is
 * NULL-terminated by a 2-byte NULL. 
 * 
 * Note that version number and (;1) is not appended.
 * @param flag
 *        bit0= no_force_dots
 *        bit1= allow 103 characters rather than 64
 * @return 
 *        NULL if the original name and extension both are of length 0.
 */
uint16_t *iso_j_file_id(const uint16_t *src, int flag);

/**
 * Create a Joliet directory identifier that consists of name and optionally
 * extension. The combined name and extension length will not exceed 128 bytes,
 * and the name and extension will be separated (.). All characters consist of 
 * 2 bytes and the resulting string is NULL-terminated by a 2-byte NULL. 
 * 
 * @param flag
 *        bit1= allow 103 characters rather than 64
 * @return 
 *        NULL if the original name and extension both are of length 0.
 */
uint16_t *iso_j_dir_id(const uint16_t *src, int flag);

/**
 * Like strlen, but for Joliet strings.
 */
size_t ucslen(const uint16_t *str);

/**
 * Like strrchr, but for Joliet strings.
 */
uint16_t *ucsrchr(const uint16_t *str, char c);

/**
 * Like strdup, but for Joliet strings.
 */
uint16_t *ucsdup(const uint16_t *str);

/**
 * Like strcmp, but for Joliet strings.
 */
int ucscmp(const uint16_t *s1, const uint16_t *s2);

/**
 * Like strcpy, but for Joliet strings.
 */
uint16_t *ucscpy(uint16_t *dest, const uint16_t *src);

/**
 * Like strncpy, but for Joliet strings.
 * @param n
 *      Maximum number of characters to copy (2 bytes per char).
 */
uint16_t *ucsncpy(uint16_t *dest, const uint16_t *src, size_t n);

/**
 * Convert a given input string to d-chars.
 * @return
 *      1 on succes, < 0 error, 0 if input was null (output is set to null)
 */
int str2d_char(const char *icharset, const char *input, char **output);
int str2a_char(const char *icharset, const char *input, char **output);

void iso_lsb(uint8_t *buf, uint32_t num, int bytes);
void iso_msb(uint8_t *buf, uint32_t num, int bytes);
void iso_bb(uint8_t *buf, uint32_t num, int bytes);

/* An alternative to iso_lsb() which advances the write pointer
*/
int iso_lsb_to_buf(char **wpt, uint32_t value, int bytes, int flag);

uint32_t iso_read_lsb(const uint8_t *buf, int bytes);
uint32_t iso_read_msb(const uint8_t *buf, int bytes);

/**
 * if error != NULL it will be set to 1 if LSB and MSB integers don't match.
 */
uint32_t iso_read_bb(const uint8_t *buf, int bytes, int *error);

/** 
 * Records the date/time into a 7 byte buffer (ECMA-119, 9.1.5)
 * 
 * @param buf
 *      Buffer where the date will be written
 * @param t
 *      The time to be written
 * @param always_gmt
 *      Always write the date in GMT and not in local time.
 */
void iso_datetime_7(uint8_t *buf, time_t t, int always_gmt);

/** Records the date/time into a 17 byte buffer (ECMA-119, 8.4.26.1) */
void iso_datetime_17(uint8_t *buf, time_t t, int always_gmt);

time_t iso_datetime_read_7(const uint8_t *buf);
time_t iso_datetime_read_17(const uint8_t *buf);

/**
 * Check whether the caller process has read access to the given local file.
 * 
 * @return 
 *     1 on success (i.e, the process has read access), < 0 on error 
 *     (including ISO_FILE_ACCESS_DENIED on access denied to the specified file
 *     or any directory on the path).
 */
int iso_eaccess(const char *path);

/**
 * Copy up to \p len chars from \p buf and return this newly allocated
 * string. The new string is null-terminated.
 */
char *iso_util_strcopy(const char *buf, size_t len);

/**
 * Copy up to \p len chars from \p buf and return this newly allocated
 * string. The new string is null-terminated.
 * Any trailing blanks will be removed.
 */
char *iso_util_strcopy_untail(const char *buf, size_t len);

/**
 * Copy up to \p max characters from \p src to \p dest. If \p src has less than
 * \p max characters, we pad dest with " " characters.
 */
void strncpy_pad(char *dest, const char *src, size_t max);

/**
 * Convert a Joliet string with a length of \p len bytes to a new string
 * in local charset.
 */
char *ucs2str(const char *buf, size_t len);

typedef struct iso_rbtree IsoRBTree;
typedef struct iso_htable IsoHTable;

typedef unsigned int (*hash_funtion_t)(const void *key);
typedef int (*compare_function_t)(const void *a, const void *b);
typedef void (*hfree_data_t)(void *key, void *data);

/**
 * Create a new binary tree. libisofs binary trees allow you to add any data
 * passing it as a pointer. You must provide a function suitable for compare
 * two elements.
 *
 * @param compare
 *     A function to compare two keys. It takes a pointer to both keys
 *     and return 0, -1 or 1 if the first key is equal, less or greater 
 *     than the second one.
 * @param tree
 *     Location where the tree structure will be stored.
 */
int iso_rbtree_new(int (*compare)(const void*, const void*), IsoRBTree **tree);

/**
 * Destroy a given tree.
 * 
 * Note that only the structure itself is deleted. To delete the elements, you
 * should provide a valid free_data function. It will be called for each 
 * element of the tree, so you can use it to free any related data.
 */
void iso_rbtree_destroy(IsoRBTree *tree, void (*free_data)(void *));

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
int iso_rbtree_insert(IsoRBTree *tree, void *data, void **item);

/**
 * Get the number of elements in a given tree.
 */
size_t iso_rbtree_get_size(IsoRBTree *tree);

/**
 * Get an array view of the elements of the tree.
 * 
 * @param include_item
 *    Function to select which elements to include in the array. It that takes
 *    a pointer to an element and returns 1 if the element should be included,
 *    0 if not. If you want to add all elements to the array, you can pass a
 *    NULL pointer.
 * @param size
 *    If not null, will be filled with the number of elements in the array,
 *    without counting the final NULL item.
 * @return
 *    A sorted array with the contents of the tree, or NULL if there is not
 *    enought memory to allocate the array. You should free(3) the array when
 *    no more needed. Note that the array is NULL-terminated, and thus it
 *    has size + 1 length.
 */
void **iso_rbtree_to_array(IsoRBTree *tree, int (*include_item)(void *), 
                           size_t *size);

/**
 * Create a new hash table.
 * 
 * @param size
 *     Number of slots in table.
 * @param hash
 *     Function used to generate
 */
int iso_htable_create(size_t size, hash_funtion_t hash, 
                      compare_function_t compare, IsoHTable **table);

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
int iso_htable_add(IsoHTable *table, void *key, void *data);

/**
 * Like iso_htable_add(), but this doesn't allow dulpicates.
 * 
 * @return
 *     1 success, 0 if an item with the same key already exists, < 0 error
 */
int iso_htable_put(IsoHTable *table, void *key, void *data);

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
int iso_htable_get(IsoHTable *table, void *key, void **data);

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
int iso_htable_remove(IsoHTable *table, void *key, hfree_data_t free_data);

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
int iso_htable_remove_ptr(IsoHTable *table, void *key, hfree_data_t free_data);

/**
 * Destroy the given hash table.
 * 
 * Note that you're responsible to actually destroy the elements by providing
 * a valid free_data function. You can pass NULL if you only want to delete
 * the hash structure.
 */
void iso_htable_destroy(IsoHTable *table, hfree_data_t free_data);

/**
 * Hash function suitable for keys that are char strings.
 */
unsigned int iso_str_hash(const void *key);

/**
 * Encode an integer as LEN,BYTES for being a component in certain AAIP
 * attribute values.
 */
int iso_util_encode_len_bytes(uint32_t data, char *buffer, int data_len,
                              int *result_len, int flag);

/**
 * Decode an integer as LEN,BYTES for being a component in certain AAIP
 * attribute values.
 * @param data        returns the decoded value
 * @param buffer      contains the encoded value
 * @param data_len    returns the number of value bytes (without len byte)
 * @param buffer_len  tells the number of valid buffer bytes
 */
int iso_util_decode_len_bytes(uint32_t *data, char *buffer, int *data_len,
                              int buffer_len, int flag);

    
/* Evaluate a data block whether it is a libisofs session checksum tag of
   desired type and eventually use it to verify the MD5 checksum computed
   so far.
   @param block      The data block to be evaluated
   @param desired    Bit map which tells what tag types are expected
                     (0 to 30)
   @param lba        The address from where block was read
   @param ctx        The checksum context computed so far
   @param ctx_start_lba  The block address where checksum computing started
   @param tag_type   Returns the tag type (0 means invalid tag type)
   @param flag       Bitfield for control purposes, unused yet, submit 0
   @return           1= tag is desired and matches
                     0= not a recognizable tag or a undesired tag
                    <0 is error or mismatch
*/
int iso_util_eval_md5_tag(char *block, int desired, uint32_t lba,
                          void *ctx, uint32_t ctx_start_lba,
                          int *tag_type, uint32_t *next_tag, int flag);


int iso_util_tag_magic(int tag_type, char **tag_magic, int *len, int flag);


/* ------------------------------------------------------------------------- */

/* In md5.h these function prototypes would be neighbors of (Ecma119Image *)
   which needs inclusion of ecma119.h and more. So, being generic, they ended
   up here.
*/

/* Function to identify and manage md5sum indice of the old image.
 * data is supposed to be a 4 byte integer, bit 31 shall be 0,
 * value 0 of this integer means that it is not a valid index.
 */
int checksum_cx_xinfo_func(void *data, int flag);

/* The iso_node_xinfo_cloner function which gets associated to 
 * checksum_cx_xinfo_func by iso_init() resp. iso_init_with_flag() via 
 * iso_node_xinfo_make_clonable()
 */
int checksum_cx_xinfo_cloner(void *old_data, void **new_data, int flag);


/* Function to identify and manage md5 sums of unspecified providence stored
 * directly in this xinfo. This is supposed to override any other recorded
 * MD5 of the node unless data get copied and checksummed during that copying.
 */
int checksum_md5_xinfo_func(void *data, int flag);

/* The iso_node_xinfo_cloner function which gets associated to 
 * checksum_md5_xinfo_func by iso_init() resp. iso_init_with_flag() via 
 * iso_node_xinfo_make_clonable()
 */
int checksum_md5_xinfo_cloner(void *old_data, void **new_data, int flag);

/* The iso_node_xinfo_cloner function which gets associated to
 * iso_hfsplus_xinfo_func by iso_init() resp. iso_init_with_flag() via
 * iso_node_xinfo_make_clonable()
 */
int iso_hfsplus_xinfo_cloner(void *old_data, void **new_data, int flag);


/* ------------------------------------------------------------------------- */


void *iso_alloc_mem(size_t size, size_t count, int flag);

#define LIBISO_ALLOC_MEM(pt, typ, count) { \
        pt= (typ *) iso_alloc_mem(sizeof(typ), (size_t) (count), 0); \
        if(pt == NULL) { \
                ret= ISO_OUT_OF_MEM; goto ex; \
        } }

#define LIBISO_ALLOC_MEM_VOID(pt, typ, count) { \
        pt= (typ *) iso_alloc_mem(sizeof(typ), (size_t) (count), 0); \
        if(pt == NULL) { \
                goto ex; \
        } }

#define LIBISO_FREE_MEM(pt) { \
        if(pt != NULL) \
                free((char *) pt); \
        }


/* ------------------------------------------------------------------------- */


/* To avoid the need to include more system header files */
uint16_t iso_ntohs(uint16_t v);
uint16_t iso_htons(uint16_t v);


#endif /*LIBISO_UTIL_H_*/
