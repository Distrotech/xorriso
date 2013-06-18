/*
 * Copyright (c) 2007 Vreixo Formoso
 * Copyright (c) 2007 Mario Danic
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

#include "util.h"
#include "libisofs.h"
#include "messages.h"
#include "joliet.h"
#include "../version.h"

#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <limits.h>
#include <iconv.h>
#include <locale.h>
#include <langinfo.h>

#include <unistd.h>

/* if we don't have eaccess, we check file access by opening it */
#ifndef HAVE_EACCESS
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif


/* Produce possibly inflationary error messages directly to stderr */
static int iso_iconv_debug = 0;


struct iso_iconv_handle {
    int status;  /* bit0= open , bit1= identical mapping */
    iconv_t descr;
};


/*
   @param flag    bit0= shortcut by identical mapping is not allowed
*/
static
int iso_iconv_open(struct iso_iconv_handle *handle,
                   char *tocode, char *fromcode, int flag)
{
    handle->status = 0;
    handle->descr = (iconv_t) -1;

    if (strcmp(tocode, fromcode) == 0 && !(flag & 1)) {
        handle->status = 1 | 2;
        return 1;
    }
    handle->descr = iconv_open(tocode, fromcode);
    if (handle->descr == (iconv_t) -1) {
        if (strlen(tocode) + strlen(fromcode) <= 160 && iso_iconv_debug)
            fprintf(stderr, 
           "libisofs_DEBUG: iconv_open(\"%s\", \"%s\") failed: errno= %d %s\n",
                    tocode, fromcode, errno, strerror(errno));
        return 0;
    }
    handle->status = 1;
    return 1;
}


static
size_t iso_iconv(struct iso_iconv_handle *handle,
                 char **inbuf, size_t *inbytesleft,
                 char **outbuf, size_t *outbytesleft, int flag)
{
    size_t ret;
/* The build system might indicate iconv(,const char **inbuf,) by
   defining ICONV_CONST const
*/
#ifndef ICONV_CONST
#define ICONV_CONST
#endif
    ICONV_CONST char **local_inbuf;

    local_inbuf = (ICONV_CONST char **) inbuf;

    if (!(handle->status & 1)) {
        if (iso_iconv_debug)
            fprintf(stderr,
          "libisofs_DEBUG: iso_iconv(): iso_iconv_handle not in open state\n");
        return (size_t) -1;
    }
    if (handle->status & 2) {
        if (inbuf == NULL || outbuf == NULL) {
null_buf:;
            if (iso_iconv_debug)
                fprintf(stderr, 
"libisofs_DEBUG: iso_iconv(): NULL buffers not allowed in shortcut mapping\n");
            return (size_t) -1;
        }
        if (*inbuf == NULL || *outbuf == NULL)
            goto null_buf;
        while (*inbytesleft > 0 && *outbytesleft > 0) {
             *((*outbuf)++) = *((*inbuf)++);
             (*inbytesleft)--;
             (*outbytesleft)--;
        }
        if (*inbytesleft > 0 && *outbytesleft <= 0)
            return (size_t) -1;
        return (size_t) 0;
    }
    ret = iconv(handle->descr, local_inbuf, inbytesleft, outbuf, outbytesleft);
    if (ret == (size_t) -1) {
        if (iso_iconv_debug)
            fprintf(stderr, "libisofs_DEBUG: iconv() failed: errno= %d %s\n",
                          errno, strerror(errno));
        return (size_t) -1;
    }
    return ret;
}


static
int iso_iconv_close(struct iso_iconv_handle *handle, int flag)
{
    int ret;

    if (!(handle->status & 1)) {
        if (iso_iconv_debug)
            fprintf(stderr, 
    "libisofs_DEBUG: iso_iconv_close(): iso_iconv_handle not in open state\n");
        return -1;
    }
    handle->status &= ~1;
    if (handle->status & 2)
        return 0;

    ret = iconv_close(handle->descr);
    if (ret == -1) {
        if (iso_iconv_debug)
            fprintf(stderr,
                    "libisofs_DEBUG: iconv_close() failed: errno= %d %s\n",
                    errno, strerror(errno));
        return -1;
    }
    return ret;
}


int int_pow(int base, int power)
{
    int result = 1;
    while (--power >= 0) {
        result *= base;
    }
    return result;
}

/* This static variable can override the locale's charset by its getter
   function which should be used whenever the local character set name
   is to be inquired. I.e. instead of calling nl_langinfo(CODESET) directly.
   If the variable is empty then it forwards nl_langinfo(CODESET).
*/
static char libisofs_local_charset[4096]= {""};

/* API function */
int iso_set_local_charset(char *name, int flag)
{
    if(strlen(name) >= sizeof(libisofs_local_charset))
        return(0);
    strcpy(libisofs_local_charset, name);
    return 1;
}

/* API function */
char *iso_get_local_charset(int flag)
{
   if(libisofs_local_charset[0])
     return libisofs_local_charset;
   return nl_langinfo(CODESET);
}

int strconv(const char *str, const char *icharset, const char *ocharset,
            char **output)
{
    size_t inbytes;
    size_t outbytes;
    size_t n;
    struct iso_iconv_handle conv;
    int conv_ret;

    char *out = NULL;
    char *src;
    char *ret;
    int retval;

    inbytes = strlen(str);
    outbytes = (inbytes + 1) * MB_LEN_MAX;
    out = calloc(outbytes, 1);
    if (out == NULL) {
        retval = ISO_OUT_OF_MEM;
        goto ex;
    }

    conv_ret = iso_iconv_open(&conv, (char *) ocharset, (char *) icharset, 0);
    if (conv_ret <= 0) {
        retval = ISO_CHARSET_CONV_ERROR;
        goto ex;
    }
    src = (char *)str;
    ret = (char *)out;
    n = iso_iconv(&conv, &src, &inbytes, &ret, &outbytes, 0);
    if (n == (size_t) -1) {
        /* error */
        iso_iconv_close(&conv, 0);
        retval = ISO_CHARSET_CONV_ERROR;
        goto ex;
    }
    *ret = '\0';
    iso_iconv_close(&conv, 0);

    *output = malloc(ret - out + 1);
    if (*output == NULL) {
        retval = ISO_OUT_OF_MEM;
        goto ex;
    }
    memcpy(*output, out, ret - out + 1);
    retval = ISO_SUCCESS;
ex:;
    if (out != NULL)
        free(out);
    return retval;
}

int strnconv(const char *str, const char *icharset, const char *ocharset,
             size_t len, char **output)
{
    size_t inbytes;
    size_t outbytes;
    size_t n;
    struct iso_iconv_handle conv;
    int conv_ret;
    char *out = NULL;
    char *src;
    char *ret;
    int retval;

    inbytes = len;
    outbytes = (inbytes + 1) * MB_LEN_MAX;
    out = calloc(outbytes, 1);
    if (out == NULL) {
        retval = ISO_OUT_OF_MEM;
        goto ex;
    }
    conv_ret = iso_iconv_open(&conv, (char *) ocharset, (char *) icharset, 0);
    if (conv_ret <= 0) {
        retval = ISO_CHARSET_CONV_ERROR;
        goto ex;
    }
    src = (char *)str;
    ret = (char *)out;
    n = iso_iconv(&conv, &src, &inbytes, &ret, &outbytes, 0);
    if (n == (size_t) -1) {
        /* error */
        iso_iconv_close(&conv, 0);
        retval = ISO_CHARSET_CONV_ERROR;
        goto ex;
    }
    *ret = '\0';
    iso_iconv_close(&conv, 0);

    *output = malloc(ret - out + 1);
    if (*output == NULL) {
        retval = ISO_OUT_OF_MEM;
        goto ex;
    }
    memcpy(*output, out, ret - out + 1);
    retval = ISO_SUCCESS;
ex:;
    if (out != NULL)
        free(out);
    return retval;
}

/**
 * Convert a str in a specified codeset to WCHAR_T. 
 * The result must be free() when no more needed
 * 
 * @return
 *      1 success, < 0 error
 */
static
int str2wchar(const char *icharset, const char *input, wchar_t **output)
{
    struct iso_iconv_handle conv;
    int conv_ret;

    /* That while loop smells like a potential show stopper */
    size_t loop_counter = 0, loop_limit = 3;

    size_t inbytes;
    size_t outbytes;
    char *ret;
    char *src;
    wchar_t *wstr;
    size_t n;

    if (icharset == NULL || input == NULL || output == NULL) {
        return ISO_NULL_POINTER;
    }

    conv_ret = iso_iconv_open(&conv, "WCHAR_T", (char *) icharset, 0);
    if (conv_ret <= 0) {
        return ISO_CHARSET_CONV_ERROR;
    }

    inbytes = strlen(input);
    loop_limit = inbytes + 3;
    outbytes = (inbytes + 1) * sizeof(wchar_t);

    /* we are sure that numchars <= inbytes */
    wstr = malloc(outbytes);
    if (wstr == NULL) {
        return ISO_OUT_OF_MEM;
    }
    ret = (char *)wstr;
    src = (char *)input;

    n = iso_iconv(&conv, &src, &inbytes, &ret, &outbytes, 0);
    while (n == (size_t) -1) {

        if (errno == E2BIG) {
            /* error, should never occur */
            goto conv_error;
        } else {
            wchar_t *wret;

            /* 
             * Invalid input string charset.
             * This can happen if input is in fact encoded in a charset 
             * different than icharset.
             * We can't do anything better than replace by "_" and continue.
             */
            inbytes--;
            src++;

            wret = (wchar_t*) ret;
            *wret++ = (wchar_t) '_';
            ret = (char *) wret;
            outbytes -= sizeof(wchar_t);

            if (!inbytes)
                break;

            /* Just to appease my remorse about unclear loop ends */
            loop_counter++;
            if (loop_counter > loop_limit)
                goto conv_error;
            n = iso_iconv(&conv, &src, &inbytes, &ret, &outbytes, 0);
        }
    }
    iso_iconv_close(&conv, 0);
    *( (wchar_t *)ret )='\0';
    *output = wstr;
    return ISO_SUCCESS;

conv_error:;
    iso_iconv_close(&conv, 0);
    free(wstr);
    return ISO_CHARSET_CONV_ERROR;
}

int str2ascii(const char *icharset, const char *input, char **output)
{
    int result;
    wchar_t *wsrc_ = NULL;
    char *ret = NULL;
    char *ret_ = NULL;
    char *src;
    struct iso_iconv_handle conv;
    int conv_ret;
    int direct_conv = 0;

    /* That while loop smells like a potential show stopper */
    size_t loop_counter = 0, loop_limit = 3;

    /* Fallback in case that iconv() is too demanding for system */
    unsigned char *cpt;

    size_t numchars;
    size_t outbytes;
    size_t inbytes;
    size_t n;


    if (icharset == NULL || input == NULL || output == NULL) {
        return ISO_NULL_POINTER;
    }

    /* First try the traditional way via intermediate character set WCHAR_T.
     * Up to August 2011 this was the only way. But it will not work if
     * there is no character set "WCHAR_T". E.g. on Solaris.
     */
    /* convert the string to a wide character string. Note: outbytes
     * is in fact the number of characters in the string and doesn't
     * include the last NULL character.
     */
    conv_ret = 0;
    result = str2wchar(icharset, input, &wsrc_);
    if (result == (int) ISO_SUCCESS) {
        src = (char *)wsrc_;
        numchars = wcslen(wsrc_);

        inbytes = numchars * sizeof(wchar_t);
        loop_limit = inbytes + 3;

        ret_ = malloc(numchars + 1);
        if (ret_ == NULL) {
            return ISO_OUT_OF_MEM;
        }
        outbytes = numchars;
        ret = ret_;

        /* initialize iconv */
        conv_ret = iso_iconv_open(&conv, "ASCII", "WCHAR_T", 0);
        if (conv_ret <= 0) {
            free(wsrc_);
            free(ret_);
        }
    } else if (result != (int) ISO_CHARSET_CONV_ERROR)
        return result;

    /* If this did not succeed : Try the untraditional direct conversion.
    */
    if (conv_ret <= 0) {
        conv_ret = iso_iconv_open(&conv, "ASCII", (char *) icharset, 0);
        if (conv_ret <= 0)
            goto fallback;
        direct_conv = 1;
        src = (char *) input;
        inbytes = strlen(input);
        loop_limit = inbytes + 3;
        outbytes = (inbytes + 1) * sizeof(uint16_t);
        ret_ = malloc(outbytes);
        if (ret_ == NULL)
            return ISO_OUT_OF_MEM;
        ret = ret_;
    }

    n = iso_iconv(&conv, &src, &inbytes, &ret, &outbytes, 0);
    while (n == (size_t) -1) {
        /* The destination buffer is too small. Stops here. */
        if (errno == E2BIG)
            break;

        /* An incomplete multi bytes sequence was found. We 
         * can't do anything here. That's quite unlikely. */
        if (errno == EINVAL)
            break;

        /* The last possible error is an invalid multi bytes
         * sequence. Just replace the character with a "_". 
         * Probably the character doesn't exist in ascii like
         * "é, è, à, ç, ..." in French. */
        *ret++ = '_';
        outbytes--;

        if (!outbytes)
            break;

        /* There was an error with one character but some other remain
         * to be converted. That's probably a multibyte character.
         * See above comment. */
        if (direct_conv) {
            src++;
            inbytes--;
        } else {
            src += sizeof(wchar_t);
            inbytes -= sizeof(wchar_t);
        }

        if (!inbytes)
            break;

        /* Just to appease my remorse about unclear loop ends */
        loop_counter++;
        if (loop_counter > loop_limit)
            break;
        n = iso_iconv(&conv, &src, &inbytes, &ret, &outbytes, 0);
    }
    iso_iconv_close(&conv, 0);
    *ret = 0;
    if (wsrc_ != NULL)
        free(wsrc_);

    *output = ret_;
    return ISO_SUCCESS;

fallback:;
    /* Assume to have a single byte charset with ASCII as core.
       Anything suspicious will be mapped to '_'.
     */
    *output = strdup(input);
    for (cpt = (unsigned char *) *output; *cpt; cpt++) {
        if (*cpt < 32 || *cpt > 126)
            *cpt = '_';
    }
    return ISO_SUCCESS; 
}

static
void set_ucsbe(uint16_t *ucs, char c)
{
    char *v = (char*)ucs;
    v[0] = (char)0;
    v[1] = c;
}

/**
 * @return
 *      -1, 0, 1 if *ucs <, == or > than c
 */
static
int cmp_ucsbe(const uint16_t *ucs, char c)
{
    char *v = (char*)ucs;
    if (v[0] != 0) {
        return 1;
    } else if (v[1] == c) {
        return 0;
    } else {
        return (uint8_t)c > (uint8_t)v[1] ? -1 : 1;
    }
}

int str2ucs(const char *icharset, const char *input, uint16_t **output)
{
    int result;
    wchar_t *wsrc_ = NULL;
    char *src;
    char *ret = NULL;
    char *ret_ = NULL;
    struct iso_iconv_handle conv;
    int conv_ret = 0;
    int direct_conv = 0;
    
    /* That while loop smells like a potential show stopper */
    size_t loop_counter = 0, loop_limit = 3;

    size_t numchars;
    size_t outbytes;
    size_t inbytes;
    size_t n;

    if (icharset == NULL || input == NULL || output == NULL) {
        return ISO_NULL_POINTER;
    }

    /* convert the string to a wide character string. Note: outbytes
     * is in fact the number of characters in the string and doesn't
     * include the last NULL character.
     */
    /* First try the traditional way via intermediate character set WCHAR_T.
     * Up to August 2011 this was the only way. But it will not work if
     * there is no character set "WCHAR_T". E.g. on Solaris.
     */
    conv_ret = 0;
    result = str2wchar(icharset, input, &wsrc_);
    if (result == (int) ISO_SUCCESS) {
        src = (char *)wsrc_;
        numchars = wcslen(wsrc_);

        inbytes = numchars * sizeof(wchar_t);
        loop_limit = inbytes + 3;

        ret_ = malloc((numchars+1) * sizeof(uint16_t));
        if (ret_ == NULL)
            return ISO_OUT_OF_MEM;
        outbytes = numchars * sizeof(uint16_t);
        ret = ret_;

        /* initialize iconv */
        conv_ret = iso_iconv_open(&conv, "UCS-2BE", "WCHAR_T", 0);
        if (conv_ret <= 0) {
            free(wsrc_);
            free(ret_);
        }
    } else if (result != (int) ISO_CHARSET_CONV_ERROR)
        return result;

    /* If this did not succeed : Try the untraditional direct conversion.
    */
    if (conv_ret <= 0) {
        conv_ret = iso_iconv_open(&conv, "UCS-2BE", (char *) icharset, 0);
        if (conv_ret <= 0) {
            return ISO_CHARSET_CONV_ERROR;
        }            
        direct_conv = 1;
        src = (char *) input;
        inbytes = strlen(input);
        loop_limit = inbytes + 3;
        outbytes = (inbytes + 1) * sizeof(uint16_t);
        ret_ = malloc(outbytes);
        if (ret_ == NULL)
            return ISO_OUT_OF_MEM;
        ret = ret_;
    }

    n = iso_iconv(&conv, &src, &inbytes, &ret, &outbytes, 0);
    while (n == (size_t) -1) {
        /* The destination buffer is too small. Stops here. */
        if (errno == E2BIG)
            break;

        /* An incomplete multi bytes sequence was found. We 
         * can't do anything here. That's quite unlikely. */
        if (errno == EINVAL)
            break;

        /* The last possible error is an invalid multi bytes
         * sequence. Just replace the character with a "_". 
         * Probably the character doesn't exist in UCS */
        set_ucsbe((uint16_t*) ret, '_');
        ret += sizeof(uint16_t);
        outbytes -= sizeof(uint16_t);

        if (!outbytes)
            break;

        /* There was an error with one character but some other remain
         * to be converted. That's probably a multibyte character.
         * See above comment. */
        if (direct_conv) {
            src++;
            inbytes--;
        } else {
            src += sizeof(wchar_t);
            inbytes -= sizeof(wchar_t);
        }

        if (!inbytes)
            break;

        /* Just to appease my remorse about unclear loop ends */
        loop_counter++;
        if (loop_counter > loop_limit)
            break;
        n = iso_iconv(&conv, &src, &inbytes, &ret, &outbytes, 0);
    }
    iso_iconv_close(&conv, 0);

    /* close the ucs string */
    set_ucsbe((uint16_t*) ret, '\0');
    if (wsrc_ != NULL)
        free(wsrc_);

    *output = (uint16_t*)ret_;
    return ISO_SUCCESS;
}

static int valid_d_char(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c == '_');
}

static int valid_a_char(char c)
{
    return (c >= ' ' && c <= '"') || (c >= '%' && c <= '?') || 
           (c >= 'A' && c <= 'Z') || (c == '_');
}

static int valid_j_char(uint16_t c)
{
    return cmp_ucsbe(&c, ' ') != -1 && cmp_ucsbe(&c, '*') && cmp_ucsbe(&c, '/')
        && cmp_ucsbe(&c, ':') && cmp_ucsbe(&c, ';') && cmp_ucsbe(&c, '?') 
        && cmp_ucsbe(&c, '\\');
}

/* @param relaxed bit0+1  0= strict ECMA-119
                          1= additionally allow lowercase (else map to upper)
                          2= allow all 8-bit characters
                  bit2    allow all 7-bit characters (but map to upper if
                          not bit0+1 == 2)
*/
static char map_fileid_char(char c, int relaxed)
{
    char upper;

    if (c == '/')  /* Allowing slashes would cause lots of confusion */
        return '_';
    if ((relaxed & 3) == 2)
        return c;
    if (valid_d_char(c))
        return c;
    if ((relaxed & 4) && (c & 0x7f) == c && (c < 'a' || c > 'z'))
        return c; 
    upper= toupper(c);
    if (valid_d_char(upper)) {
        if (relaxed & 3) {
            /* lower chars are allowed */
            return c;
        }
        return upper;
    }
    return '_';
}

static
char *iso_dirid(const char *src, int size, int relaxed)
{
    size_t len, i;
    char name[32];

    len = strlen(src);
    if ((int) len > size) {
        len = size;
    }
    for (i = 0; i < len; i++) {

#ifdef Libisofs_old_ecma119_nameS

        char c= toupper(src[i]);
        name[i] = valid_d_char(c) ? c : '_';

#else /* Libisofs_old_ecma119_nameS */

        name[i] = map_fileid_char(src[i], relaxed);

#endif /* ! Libisofs_old_ecma119_nameS */

    }

    name[len] = '\0';
    return strdup(name);
}

char *iso_1_dirid(const char *src, int relaxed)
{
    return iso_dirid(src, 8, relaxed);
}

char *iso_2_dirid(const char *src)
{
    return iso_dirid(src, 31, 0);
}

char *iso_1_fileid(const char *src, int relaxed, int force_dots)
{
    char *dot; /* Position of the last dot in the filename, will be used 
                * to calculate lname and lext. */
    int lname, lext, pos, i;
    char dest[13]; /*  13 = 8 (name) + 1 (.) + 3 (ext) + 1 (\0) */

    if (src == NULL) {
        return NULL;
    }
    dot = strrchr(src, '.');
    if (dot == src && strlen(src) > 4)
        dot = NULL;      /* Use the long extension instead of the empty name */
    lext = dot ? strlen(dot + 1) : 0;
    lname = strlen(src) - lext - (dot ? 1 : 0);

    /* If we can't build a filename, return NULL. */
    if (lname == 0 && lext == 0) {
        return NULL;
    }

    pos = 0;

    /* Convert up to 8 characters of the filename. */
    for (i = 0; i < lname && i < 8; i++) {

#ifdef Libisofs_old_ecma119_nameS

        char c= toupper(src[i]);

        dest[pos++] = valid_d_char(c) ? c : '_';

#else /* Libisofs_old_ecma119_nameS */

        if (dot == NULL && src[i] == '.')
            dest[pos++] = '_'; /* make sure that ignored dots do not appear */
        else
            dest[pos++] = map_fileid_char(src[i], relaxed);

#endif /* ! Libisofs_old_ecma119_nameS */

    }

    /* This dot is mandatory, even if there is no extension. */
    if (force_dots || lext > 0)
        dest[pos++] = '.';

    /* Convert up to 3 characters of the extension, if any. */
    for (i = 0; i < lext && i < 3; i++) {

#ifdef Libisofs_old_ecma119_nameS

        char c= toupper(src[lname + 1 + i]);

        dest[pos++] = valid_d_char(c) ? c : '_';

#else /* Libisofs_old_ecma119_nameS */

        dest[pos++] = map_fileid_char(src[lname + 1 + i], relaxed);

#endif /* ! Libisofs_old_ecma119_nameS */

    }

    dest[pos] = '\0';
    return strdup(dest);
}

char *iso_2_fileid(const char *src)
{
    char *dot;
    int lname, lext, lnname, lnext, pos, i;
    char dest[32]; /* 32 = 30 (name + ext) + 1 (.) + 1 (\0) */

    if (src == NULL) {
        return NULL;
    }

    dot = strrchr(src, '.');

    /* 
     * Since the maximum length can be divided freely over the name and
     * extension, we need to calculate their new lengths (lnname and
     * lnext). If the original filename is too long, we start by trimming
     * the extension, but keep a minimum extension length of 3. 
     */
    if (dot == NULL || *(dot + 1) == '\0') {
        lname = strlen(src);
        lnname = (lname > 30) ? 30 : lname;
        lext = lnext = 0;
    } else {
        lext = strlen(dot + 1);
        lname = strlen(src) - lext - 1;
        lnext = (strlen(src) > 31 && lext > 3) ? (lname < 27 ? 30 - lname : 3)
                : lext;
        lnname = (strlen(src) > 31) ? 30 - lnext : lname;
    }

    if (lnname == 0 && lnext == 0) {
        return NULL;
    }

    pos = 0;

    /* Convert up to lnname characters of the filename. */
    for (i = 0; i < lnname; i++) {
        char c= toupper(src[i]);

        dest[pos++] = valid_d_char(c) ? c : '_';
    }
    dest[pos++] = '.';

    /* Convert up to lnext characters of the extension, if any. */
    for (i = 0; i < lnext; i++) {
        char c= toupper(src[lname + 1 + i]);

        dest[pos++] = valid_d_char(c) ? c : '_';
    }
    dest[pos] = '\0';
    return strdup(dest);
}

/**
 * Create a dir name suitable for an ISO image with relaxed constraints.
 * 
 * @param size
 *     Max len for the name
 * @param relaxed
 *     bit0+1: 0 only allow d-characters,
 *             1 allow also lowe case chars, 
 *             2 allow all 8-bit characters,
 *     bit2:   allow 7-bit characters (but map lowercase to uppercase if
 *             not bit0+1 == 2)
 */
char *iso_r_dirid(const char *src, int size, int relaxed)
{
    size_t len, i;
    char *dest;

    len = strlen(src);
    if ((int) len > size) {
        len = size;
    }
    dest = malloc(len + 1);
    if (dest == NULL)
        return NULL;
    for (i = 0; i < len; i++) {

#ifdef Libisofs_old_ecma119_nameS

        char c= src[i];
        if (relaxed == 2) {
            /* all chars are allowed */
            dest[i] = c;
        } else if (valid_d_char(c)) {
            /* it is a valid char */
            dest[i] = c;
        } else {
            c= toupper(src[i]);
            if (valid_d_char(c)) {
                if (relaxed) {
                    /* lower chars are allowed */
                    dest[i] = src[i];
                } else {
                    dest[i] = c;
                }
            } else {
                dest[i] = '_';
            }
        }

#else /* Libisofs_old_ecma119_nameS */

        dest[i] = map_fileid_char(src[i], relaxed);

#endif /* ! Libisofs_old_ecma119_nameS */

    }

    dest[len] = '\0';
    return dest;
}

/**
 * Create a file name suitable for an ISO image with level > 1 and
 * with relaxed constraints.
 * 
 * @param len
 *     Max len for the name, without taken the "." into account.
 * @param relaxed
 *     bit0+1: 0 only allow d-characters,
 *             1 allow also lowe case chars, 
 *             2 allow all 8-bit characters,
 *     bit2:   allow 7-bit characters (but map lowercase to uppercase if
 *             not bit0+1 == 2)
 * @param forcedot
 *     Whether to ensure that "." is added
 */
char *iso_r_fileid(const char *src, size_t len, int relaxed, int forcedot)
{
    char *dot, *retval = NULL;
    int lname, lext, lnname, lnext, pos, i;
    char *dest = NULL;

    dest = calloc(len + 1 + 1, 1);
    if (dest == NULL)
        goto ex;

    if (src == NULL) {
        goto ex;
    }

    dot = strrchr(src, '.');

    /* 
     * Since the maximum length can be divided freely over the name and
     * extension, we need to calculate their new lengths (lnname and
     * lnext). If the original filename is too long, we start by trimming
     * the extension, but keep a minimum extension length of 3. 
     */
    if (dot == NULL || *(dot + 1) == '\0') {
        lname = strlen(src);
        lnname = (lname > (int) len) ? (int) len : lname;
        lext = lnext = 0;
    } else {
        lext = strlen(dot + 1);
        lname = strlen(src) - lext - 1;
        lnext = (strlen(src) > len + 1 && lext > 3) ? 
                (lname < (int) len - 3 ? (int) len - lname : 3)
                : lext;
        lnname = (strlen(src) > len + 1) ? (int) len - lnext : lname;
    }

    if (lnname == 0 && lnext == 0) {
        goto ex;
    }

    pos = 0;

    /* Convert up to lnname characters of the filename. */
    for (i = 0; i < lnname; i++) {

#ifdef Libisofs_old_ecma119_nameS

        char c= src[i];
        if (relaxed == 2) {
            /* all chars are allowed */
            dest[pos++] = c;
        } else if (valid_d_char(c)) {
            /* it is a valid char */
            dest[pos++] = c;
        } else {
            c= toupper(src[i]);
            if (valid_d_char(c)) {
                if (relaxed) {
                    /* lower chars are allowed */
                    dest[pos++] = src[i];
                } else {
                    dest[pos++] = c;
                }
            } else {
                dest[pos++] = '_';
            }
        }

#else /* Libisofs_old_ecma119_nameS */

        dest[pos++] = map_fileid_char(src[i], relaxed);

#endif /* ! Libisofs_old_ecma119_nameS */

    }
    if (lnext > 0 || forcedot) {
        dest[pos++] = '.';
    }

    /* Convert up to lnext characters of the extension, if any. */
    for (i = lname + 1; i < lname + 1 + lnext; i++) {

#ifdef Libisofs_old_ecma119_nameS

        char c= src[i];
        if (relaxed == 2) {
            /* all chars are allowed */
            dest[pos++] = c;
        } else if (valid_d_char(c)) {
            /* it is a valid char */
            dest[pos++] = c;
        } else {
            c= toupper(src[i]);
            if (valid_d_char(c)) {
                if (relaxed) {
                    /* lower chars are allowed */
                    dest[pos++] = src[i];
                } else {
                    dest[pos++] = c;
                }
            } else {
                dest[pos++] = '_';
            }
        }

#else /* Libisofs_old_ecma119_nameS */

        dest[pos++] = map_fileid_char(src[i], relaxed);

#endif /* ! Libisofs_old_ecma119_nameS */

    }
    dest[pos] = '\0';

    retval = strdup(dest);

ex:;
    if (dest != NULL)
        free(dest);
    return retval;
}

/*
   bit0= no_force_dots
   bit1= allow 103 characters rather than 64
*/
uint16_t *iso_j_file_id(const uint16_t *src, int flag)
{
    uint16_t *dot, *retval = NULL;
    size_t lname, lext, lnname, lnext, pos, i, maxchar = 64;
    uint16_t *dest = NULL;

    LIBISO_ALLOC_MEM_VOID(dest, uint16_t, LIBISO_JOLIET_NAME_MAX);
                               /* was: 66 = 64 (name + ext) + 1 (.) + 1 (\0) */

    if (src == NULL) {
        goto ex;
    }
    if (flag & 2)
        maxchar = 103;

    dot = ucsrchr(src, '.');

    /* 
     * Since the maximum length can be divided freely over the name and
     * extension, we need to calculate their new lengths (lnname and
     * lnext). If the original filename is too long, we start by trimming
     * the extension, but keep a minimum extension length of 3. 
     */
    if (dot == NULL || cmp_ucsbe(dot + 1, '\0') == 0) {
        lname = ucslen(src);
        lnname = (lname > maxchar) ? maxchar : lname;
        lext = lnext = 0;
    } else {
        lext = ucslen(dot + 1);
        lname = ucslen(src) - lext - 1;
        lnext = (ucslen(src) > maxchar + 1 && lext > 3)
                ? (lname < maxchar - 3 ? maxchar - lname : 3)
                : lext;
        lnname = (ucslen(src) > maxchar + 1) ? maxchar - lnext : lname;
    }

    if (lnname == 0 && lnext == 0) {
        goto ex;
    }

    pos = 0;

    /* Convert up to lnname characters of the filename. */
    for (i = 0; i < lnname; i++) {
        uint16_t c = src[i];
        if (valid_j_char(c)) {
            dest[pos++] = c;
        } else {
            set_ucsbe(dest + pos, '_');
            pos++;
        }
    }

    if ((flag & 1) && lnext <= 0)
        goto is_done;

    set_ucsbe(dest + pos, '.');
    pos++;

    /* Convert up to lnext characters of the extension, if any. */
    for (i = 0; i < lnext; i++) {
        uint16_t c = src[lname + 1 + i];
        if (valid_j_char(c)) {
            dest[pos++] = c;
        } else {
            set_ucsbe(dest + pos, '_');
            pos++;
        }
    }

is_done:;
    set_ucsbe(dest + pos, '\0');
    retval = ucsdup(dest);
ex:;
    LIBISO_FREE_MEM(dest);
    return retval;
}

/* @param flag bit1= allow 103 characters rather than 64
*/
uint16_t *iso_j_dir_id(const uint16_t *src, int flag)
{
    size_t len, i, maxchar = 64;
    uint16_t *dest = NULL, *retval = NULL;
                                                    /* was: 65 = 64 + 1 (\0) */
    LIBISO_ALLOC_MEM_VOID(dest, uint16_t, LIBISO_JOLIET_NAME_MAX);

    if (src == NULL) {
        goto ex;
    }
    if (flag & 2)
        maxchar = 103;

    len = ucslen(src);
    if (len > maxchar) {
        len = maxchar;
    }
    for (i = 0; i < len; i++) {
        uint16_t c = src[i];
        if (valid_j_char(c)) {
            dest[i] = c;
        } else {
            set_ucsbe(dest + i, '_');
        }
    }
    set_ucsbe(dest + len, '\0');
    retval = ucsdup(dest);
ex:
    LIBISO_FREE_MEM(dest);
    return retval;
}

size_t ucslen(const uint16_t *str)
{
    size_t i;

    for (i = 0; str[i]; i++)
        ;
    return i;
}

uint16_t *ucsrchr(const uint16_t *str, char c)
{
    size_t len = ucslen(str);

    while (len-- > 0) {
        if (cmp_ucsbe(str + len, c) == 0) {
            return (uint16_t*)(str + len);
        }
    }
    return NULL;
}

uint16_t *ucsdup(const uint16_t *str)
{
    uint16_t *ret;
    size_t len = ucslen(str);
    
    ret = malloc(2 * (len + 1));
    if (ret == NULL)
        return NULL;
    if (ret != NULL) {
        memcpy(ret, str, 2 * (len + 1));
    }
    return ret;
}

/**
 * Although each character is 2 bytes, we actually compare byte-by-byte
 * because the words are big-endian. Comparing possibly swapped words
 * would make the sorting order depend on the machine byte order.
 */
int ucscmp(const uint16_t *s1, const uint16_t *s2)
{
    const uint8_t *s = (const uint8_t*)s1;
    const uint8_t *t = (const uint8_t*)s2;
    size_t len1 = ucslen(s1);
    size_t len2 = ucslen(s2);
    size_t i, len = MIN(len1, len2) * 2;

    for (i = 0; i < len; i++) {
        if (s[i] < t[i]) {
            return -1;
        } else if (s[i] > t[i]) {
            return 1;
        }
    }

    if (len1 < len2)
        return -1;
    else if (len1 > len2)
        return 1;
    return 0;
}

uint16_t *ucscpy(uint16_t *dest, const uint16_t *src)
{
    size_t n = ucslen(src) + 1;
    memcpy(dest, src, n*2);
    return dest;
}

uint16_t *ucsncpy(uint16_t *dest, const uint16_t *src, size_t n)
{
    n = MIN(n, ucslen(src) + 1);
    memcpy(dest, src, n*2);
    return dest;
}

int str2d_char(const char *icharset, const char *input, char **output)
{
    int ret;
    char *ascii;
    size_t len, i;

    if (output == NULL) {
        return ISO_OUT_OF_MEM;
    }

    /** allow NULL input */
    if (input == NULL) {
        *output = NULL;
        return 0;
    }

    /* this checks for NULL parameters */
    ret = str2ascii(icharset, input, &ascii);
    if (ret < 0) {
        *output = NULL;
        return ret;
    }

    len = strlen(ascii);

    for (i = 0; i < len; ++i) {
        char c= toupper(ascii[i]);
        ascii[i] = valid_d_char(c) ? c : '_';
    }

    *output = ascii;
    return ISO_SUCCESS;
}

int str2a_char(const char *icharset, const char *input, char **output)
{
    int ret;
    char *ascii;
    size_t len, i;

    if (output == NULL) {
        return ISO_OUT_OF_MEM;
    }

    /** allow NULL input */
    if (input == NULL) {
        *output = NULL;
        return 0;
    }

    /* this checks for NULL parameters */
    ret = str2ascii(icharset, input, &ascii);
    if (ret < 0) {
        *output = NULL;
        return ret;
    }

    len = strlen(ascii);

    for (i = 0; i < len; ++i) {
        char c= toupper(ascii[i]);
        ascii[i] = valid_a_char(c) ? c : '_';
    }

    *output = ascii;
    return ISO_SUCCESS;
}

void iso_lsb(uint8_t *buf, uint32_t num, int bytes)
{
    int i;

    for (i = 0; i < bytes; ++i)
        buf[i] = (num >> (8 * i)) & 0xff;
}

void iso_msb(uint8_t *buf, uint32_t num, int bytes)
{
    int i;

    for (i = 0; i < bytes; ++i)
        buf[bytes - 1 - i] = (num >> (8 * i)) & 0xff;
}

void iso_bb(uint8_t *buf, uint32_t num, int bytes)
{
    iso_lsb(buf, num, bytes);
    iso_msb(buf+bytes, num, bytes);
}

/* An alternative to iso_lsb() which advances the write pointer
*/
int iso_lsb_to_buf(char **wpt, uint32_t value, int bytes, int flag)
{
    int b, bits;

    bits = bytes * 8;
    for (b = 0; b < bits; b += 8)
        *((unsigned char *) ((*wpt)++)) = (value >> b) & 0xff;
    return (1);
}

uint32_t iso_read_lsb(const uint8_t *buf, int bytes)
{
    int i;
    uint32_t ret = 0;

    for (i=0; i<bytes; i++) {
        ret += ((uint32_t) buf[i]) << (i*8);
    }
    return ret;
}

uint32_t iso_read_msb(const uint8_t *buf, int bytes)
{
    int i;
    uint32_t ret = 0;

    for (i=0; i<bytes; i++) {
        ret += ((uint32_t) buf[bytes-i-1]) << (i*8);
    }
    return ret;
}

uint32_t iso_read_bb(const uint8_t *buf, int bytes, int *error)
{
    uint32_t v1 = iso_read_lsb(buf, bytes);

    if (error) {
        uint32_t v2 = iso_read_msb(buf + bytes, bytes);
        if (v1 != v2) 
            *error = 1;
    }
    return v1;
}

void iso_datetime_7(unsigned char *buf, time_t t, int always_gmt)
{
    static int tzsetup = 0;
    int tzoffset;
    struct tm tm;

    if (!tzsetup) {
        tzset();
        tzsetup = 1;
    }

    memset(&tm, 0, sizeof(tm));
    tm.tm_isdst = -1;  /* some OSes change tm_isdst only if it is -1 */
    localtime_r(&t, &tm);

#ifdef HAVE_TM_GMTOFF
    tzoffset = tm.tm_gmtoff / 60 / 15;
#else
    if (tm.tm_isdst < 0)
        tm.tm_isdst = 0;
    tzoffset = ( - timezone / 60 / 15 ) + 4 * tm.tm_isdst;
#endif

    if (tzoffset > 52 || tzoffset < -48 || always_gmt) {
        /* absurd timezone offset, represent time in GMT */
        gmtime_r(&t, &tm);
        tzoffset = 0;
    }
    buf[0] = tm.tm_year;
    buf[1] = tm.tm_mon + 1;
    buf[2] = tm.tm_mday;
    buf[3] = tm.tm_hour;
    buf[4] = tm.tm_min;
    buf[5] = tm.tm_sec;
    buf[6] = tzoffset;
}

void iso_datetime_17(unsigned char *buf, time_t t, int always_gmt)
{
    static int tzsetup = 0;
    static int tzoffset;
    struct tm tm;

    if (t == (time_t) - 1) {
        /* unspecified time */
        memset(buf, '0', 16);
        buf[16] = 0;
        return;
    }
    
    if (!tzsetup) {
        tzset();
        tzsetup = 1;
    }

    memset(&tm, 0, sizeof(tm));
    tm.tm_isdst = -1;  /* some OSes change tm_isdst only if it is -1 */
    localtime_r(&t, &tm);

    localtime_r(&t, &tm);

#ifdef HAVE_TM_GMTOFF
    tzoffset = tm.tm_gmtoff / 60 / 15;
#else
    if (tm.tm_isdst < 0)
        tm.tm_isdst = 0;
    tzoffset = ( - timezone / 60 / 15 ) + 4 * tm.tm_isdst;
#endif

    if (tzoffset > 52 || tzoffset < -48 || always_gmt) {
        /* absurd timezone offset, represent time in GMT */
        gmtime_r(&t, &tm);
        tzoffset = 0;
    }

    sprintf((char*)&buf[0], "%04d", tm.tm_year + 1900);
    sprintf((char*)&buf[4], "%02d", tm.tm_mon + 1);
    sprintf((char*)&buf[6], "%02d", tm.tm_mday);
    sprintf((char*)&buf[8], "%02d", tm.tm_hour);
    sprintf((char*)&buf[10], "%02d", tm.tm_min);
    sprintf((char*)&buf[12], "%02d", MIN(59, tm.tm_sec));
    memcpy(&buf[14], "00", 2);
    buf[16] = tzoffset;

}

#ifndef HAVE_TIMEGM

/* putenv is SVr4, POSIX.1-2001, 4.3BSD , setenv is 4.3BSD, POSIX.1-2001.
   So putenv is more widely available.
   Also, setenv spoils eventual putenv expectation of applications because
   putenv installs the original string which then may be altered from
   its owner. setenv installs a copy that may not be altered.
   Both are slow.
   Thus first try with a naive implementation that assumes no leap seconds.
   If it fails a test with gmtime() then use the slow function with mktime().
*/
#define Libisofs_use_putenV yes

static
time_t env_timegm(struct tm *tm)
{
    time_t ret;
    char *tz;

#ifdef Libisofs_use_putenV

    static char unset_name[] = {"TZ"};

    tz = getenv("TZ");
    putenv("TZ=");
    tzset();
    ret = mktime(tm);
    if (tz != NULL) {
        /* tz is a pointer to the value part in a string of form "TZ="value */
        putenv(tz - 3);
    } else
        putenv(unset_name); /* not daring to submit constant */
    tzset();

#else /* Libisofs_use_putenV */

    tz = getenv("TZ");
    setenv("TZ", "", 1);
    tzset();
    ret = mktime(tm);
    if (tz)
        setenv("TZ", tz, 1);
    else
        unsetenv("TZ");
    tzset();

#endif /* ! Libisofs_use_putenV */

    return ret;
}

static
int ts_is_leapyear(int tm_year) /* years since 1900 */
{
  return ((tm_year % 4) == 0 && ((tm_year % 100) != 0 ||
                                 (tm_year % 400) == 100));
}

/* Fast implementation without leap seconds.
   Inspired by but not copied from code by Kungliga Tekniska Hgskolan
   (Royal Institute of Technology, Stockholm, Sweden),
   which was modified by Andrew Tridgell for Samba4.
   I claim own copyright 2011 Thomas Schmitt <scdbackup@gmx.net>.
*/ 
static
time_t ts_timegm(struct tm *tm)
{
    time_t ret;
    static int month_length_normal[12] =
                {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    static int month_length_leap[12] =
                {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int *month_length_pt;
    int years, i;

    ret = 0;

    years = tm->tm_year - 70; /* Years since 1970 */
    if (years < 0)
        return ret;
    for (i = 0; i < years; i++) {
        ret += 365 * 86400;
        if (ts_is_leapyear(70 + i))
            ret += 86400;
    }
    if (ts_is_leapyear(tm->tm_year))
        month_length_pt = month_length_leap;
    else
        month_length_pt = month_length_normal;
    for (i = 0; i < tm->tm_mon; i++)
        ret += month_length_pt[i] * 86400;
    ret += (tm->tm_mday - 1) * 86400;
    ret += tm->tm_hour * 3600;
    ret += tm->tm_min * 60;
    ret += tm->tm_sec;
    return ret;
}

static
time_t timegm(struct tm *tm)
{
    time_t raw_t, ret;
    struct tm *test_tm, input_tm_copy;

    /* Beware of ill effects if tm is result of gmtime() or alike */
    memcpy(&input_tm_copy, tm, sizeof(struct tm));

    /* Try without leapseconds (which are rarely implemented, as it seems) */
    raw_t = ts_timegm(tm);
    if (raw_t == 0)
        return raw_t;

    /* Check whether this translates back to the input values */
    test_tm = gmtime(&raw_t);
    if (input_tm_copy.tm_sec == test_tm->tm_sec &&
        input_tm_copy.tm_min == test_tm->tm_min &&
        input_tm_copy.tm_hour == test_tm->tm_hour &&
        input_tm_copy.tm_mday == test_tm->tm_mday &&
        input_tm_copy.tm_mon == test_tm->tm_mon &&
        input_tm_copy.tm_year == test_tm->tm_year) {
        ret = raw_t;
    } else {
        /* Mismatch. Use slow method around mktime() */
        ret = env_timegm(&input_tm_copy);
    }
    return ret;
}


#endif /* ! HAVE_TIMEGM */


time_t iso_datetime_read_7(const uint8_t *buf)
{
    struct tm tm;

    tm.tm_year = buf[0];
    tm.tm_mon = buf[1] - 1;
    tm.tm_mday = buf[2];
    tm.tm_hour = buf[3];
    tm.tm_min = buf[4];
    tm.tm_sec = buf[5];

    return timegm(&tm) - ((int8_t)buf[6]) * 60 * 15;
}

time_t iso_datetime_read_17(const uint8_t *buf)
{
    struct tm tm;

    sscanf((char*)&buf[0], "%4d", &tm.tm_year);
    sscanf((char*)&buf[4], "%2d", &tm.tm_mon);
    sscanf((char*)&buf[6], "%2d", &tm.tm_mday);
    sscanf((char*)&buf[8], "%2d", &tm.tm_hour);
    sscanf((char*)&buf[10], "%2d", &tm.tm_min);
    sscanf((char*)&buf[12], "%2d", &tm.tm_sec);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;

    return timegm(&tm) - ((int8_t)buf[6]) * 60 * 15;
}

/**
 * Check whether the caller process has read access to the given local file.
 * 
 * @return 
 *     1 on success (i.e, the process has read access), < 0 on error 
 *     (including ISO_FILE_ACCESS_DENIED on access denied to the specified file
 *     or any directory on the path).
 */
int iso_eaccess(const char *path)
{
    int access;
    
    /* use non standard eaccess when available, open() otherwise */
#ifdef HAVE_EACCESS
    access = !eaccess(path, R_OK);
#else 
    int fd = open(path, O_RDONLY);
    if (fd != -1) {
        close(fd);
        access = 1;
    } else {
        access = 0;
    }
#endif
    
    if (!access) {
        int err;

        /* error, choose an appropriate return code */
        switch (errno) {
        case EACCES:
            err = ISO_FILE_ACCESS_DENIED;
            break;
        case ENOTDIR:
        case ENAMETOOLONG:
        case ELOOP:
            err = ISO_FILE_BAD_PATH;
            break;
        case ENOENT:
            err = ISO_FILE_DOESNT_EXIST;
            break;
        case EFAULT:
        case ENOMEM:
            err = ISO_OUT_OF_MEM;
            break;
        default:
            err = ISO_FILE_ERROR;
            break;
        }
        return err;
    }
    return ISO_SUCCESS;
}

char *iso_util_strcopy(const char *buf, size_t len)
{
    char *str;
    
    str = calloc(len + 1, 1);
    if (str == NULL) {
        return NULL;
    }
    strncpy(str, buf, len);
    str[len] = '\0';
    return str;
}

char *iso_util_strcopy_untail(const char *buf, size_t len_in)
{
    char *str;
    int len;
    
    str = iso_util_strcopy(buf, len_in);
    if (str == NULL) {
        return NULL;
    }
    /* remove trailing spaces */
    for (len = len_in - 1; len >= 0; --len) {
        if (str[len] != ' ')
    break;
        str[len] = 0; 
    }
    return str;
}

/**
 * Copy up to \p max characters from \p src to \p dest. If \p src has less than
 * \p max characters, we pad dest with " " characters.
 */
void strncpy_pad(char *dest, const char *src, size_t max)
{
    size_t len, i;
    
    if (src != NULL) {
        len = MIN(strlen(src), max);
        for (i = 0; i < len; ++i)
            dest[i] = src[i];
    } else {
        len = 0;
    }
    
    for (i = len; i < max; ++i) 
        dest[i] = ' ';
}

char *ucs2str(const char *buf, size_t len)
{
    size_t outbytes, inbytes;
    char *str, *src, *out = NULL, *retval = NULL;
    struct iso_iconv_handle conv;
    int conv_ret;
    size_t n;
    
    inbytes = len;
    
    outbytes = (inbytes+1) * MB_LEN_MAX;
    
    /* ensure enought space */
    out = calloc(outbytes, 1);
    if (out == NULL)
        return NULL;

    /* convert to local charset */
    conv_ret = iso_iconv_open(&conv, iso_get_local_charset(0), "UCS-2BE", 0);
    if (conv_ret <= 0) {
        goto ex;
    }
    src = (char *)buf;
    str = (char *)out;

    n = iso_iconv(&conv, &src, &inbytes, &str, &outbytes, 0);
    iso_iconv_close(&conv, 0);
    if (n == (size_t) -1) {
        /* error */
        goto ex;
    }
    *str = '\0';

    /* remove trailing spaces */
    for (len = strlen(out) - 1; out[len] == ' ' && len > 0; --len)
        out[len] = '\0';

    retval = strdup(out);

ex:;
    if (out != NULL)
        free(out);
    return retval;
}

void iso_lib_version(int *major, int *minor, int *micro)
{
    *major = LIBISOFS_MAJOR_VERSION;
    *minor = LIBISOFS_MINOR_VERSION;
    *micro = LIBISOFS_MICRO_VERSION;
}

int iso_lib_is_compatible(int major, int minor, int micro)
{
    int cmajor, cminor, cmicro;
    
    /* for now, the rule is that library is compitable if requested
     * version is lower */
    iso_lib_version(&cmajor, &cminor, &cmicro);

    return cmajor > major 
           || (cmajor == major 
               && (cminor > minor 
                   || (cminor == minor && cmicro >= micro)));
}

int iso_init_locale(int flag)
{
    setlocale(LC_CTYPE, "");
    return 1;
}


int iso_util_encode_len_bytes(uint32_t data, char *buffer, int data_len,
                              int *result_len, int flag)
{
    uint32_t x;
    int i, l;
    char *wpt = buffer;

    if (data_len <= 0) {
        x = data;
        for (i = 0; i < 4 && x != 0; i++)
            x = x >> 8;
        l = i;
        if (l == 0)
            l = 1;
    } else
        l = data_len;
    *((unsigned char *) (wpt++)) = l;
    for (i = 0; i < l; i++)
        *((unsigned char *) (wpt++)) = data >> (8 * (l - i - 1));
    *result_len = l + 1;
    return ISO_SUCCESS;
}


int iso_util_decode_len_bytes(uint32_t *data, char *buffer, int *data_len,
                              int buffer_len, int flag)
{
    int i;

    *data = 0;
    *data_len = ((unsigned char *) buffer)[0];
    if (*data_len > buffer_len - 1)
        *data_len = buffer_len - 1;
    for (i = 1; i <= *data_len; i++)
        *data = (*data << 8) | ((unsigned char *) buffer)[i];
    return ISO_SUCCESS;
}


int iso_util_dec_to_uint32(char *dec, uint32_t *value, int flag)
{
    double num;

    sscanf(dec, "%lf", &num);
    if (num < 0 || num > 4294967295.0)
        return 0;
    *value = num;
    return 1;
}


int iso_util_hex_to_bin(char *hex, char *bin, int bin_size, int *bin_count,
                        int flag)
{
    static char *allowed = {"0123456789ABCDEFabcdef"};
    char b[3];
    int i;
    unsigned int u;

    b[2] = 0;
    *bin_count = 0;
    for (i = 0; i < bin_size; i++) {
        b[0] = hex[2 * i];
        b[1] = hex[2 * i + 1];
        if (strchr(allowed, b[0]) == NULL || strchr(allowed, b[1]) == NULL)
    break;
        sscanf(b, "%x", &u);
        ((unsigned char *) bin)[i] = u;
        (*bin_count)++;
    }
    return (*bin_count > 0);
}


int iso_util_tag_magic(int tag_type, char **tag_magic, int *len, int flag)
{
    static char *magic[] = {"",
        "libisofs_checksum_tag_v1",
        "libisofs_sb_checksum_tag_v1",
        "libisofs_tree_checksum_tag_v1",
        "libisofs_rlsb32_checksum_tag_v1"};
    static int magic_len[]= {0, 24, 27, 29, 31};
    static int magic_max = 4;

    *tag_magic = NULL;
    *len = 0;
    if (tag_type < 0 || tag_type > magic_max)
        return ISO_WRONG_ARG_VALUE;
    *tag_magic = magic[tag_type];
    *len = magic_len[tag_type];
    return magic_max;
}


int iso_util_decode_md5_tag(char data[2048], int *tag_type, uint32_t *pos,
                            uint32_t *range_start, uint32_t *range_size,
                            uint32_t *next_tag, char md5[16], int flag)
{
    int ret, bin_count, i, mode, magic_first = 1, magic_last = 4;
    int magic_len = 0;
    char *cpt, self_md5[16], tag_md5[16], *tag_magic;
    void *ctx = NULL;

    *next_tag = 0;
    mode = flag & 255;
    if (mode > magic_last)
        return ISO_WRONG_ARG_VALUE;
    if (mode > 0)
        magic_first = magic_last = mode;
    for (i = magic_first; i <= magic_last; i++) {
        iso_util_tag_magic(i, &tag_magic, &magic_len, 0);
        if (strncmp(data, tag_magic, magic_len) == 0)
    break;
    }
    if (i > magic_last )
        return 0;
    *tag_type = i;
    cpt = data + magic_len + 1;
    if (strncmp(cpt, "pos=", 4) != 0)
        return 0;
    cpt+= 4;
    ret = iso_util_dec_to_uint32(cpt, pos, 0);
    if (ret <= 0)
        return 0;
    cpt = strstr(cpt, "range_start=");
    if (cpt == NULL)
        return(0);
    ret = iso_util_dec_to_uint32(cpt + 12, range_start, 0);
    if (ret <= 0)
        return 0;
    cpt = strstr(cpt, "range_size=");
    if (cpt == NULL)
        return(0);
    ret = iso_util_dec_to_uint32(cpt + 11, range_size, 0);
    if (ret <= 0)
        return 0;
    if (*tag_type == 2 || *tag_type == 3) {
        cpt = strstr(cpt, "next=");
        if (cpt == NULL)
            return(0);
        ret = iso_util_dec_to_uint32(cpt + 5, next_tag, 0);
        if (ret <= 0)
            return 0;
    } else if (*tag_type == 4) {
        cpt = strstr(cpt, "session_start=");
        if (cpt == NULL)
            return(0);
        ret = iso_util_dec_to_uint32(cpt + 14, next_tag, 0);
        if (ret <= 0)
            return 0;
    }
    cpt = strstr(cpt, "md5=");
    if (cpt == NULL)
        return(0);
    ret = iso_util_hex_to_bin(cpt + 4, md5, 16, &bin_count, 0);
    if (ret <= 0 || bin_count != 16)
        return 0;

    cpt += 4 + 32;
    ret = iso_md5_start(&ctx);
    if (ret < 0)
        return ret;
    iso_md5_compute(ctx, data , cpt - data);
    iso_md5_end(&ctx, tag_md5);
    cpt = strstr(cpt, "self=");
    if (cpt == NULL)
        return(0);
    ret = iso_util_hex_to_bin(cpt + 5, self_md5, 16, &bin_count, 0);
    if (ret <= 0 || bin_count != 16)
        return 0;
    for(i= 0; i < 16; i++)
      if(self_md5[i] != tag_md5[i])
        return ISO_MD5_AREA_CORRUPTED;
    if (*(cpt + 5 + 32) != '\n')
        return 0;
    return(1);
}


int iso_util_eval_md5_tag(char *block, int desired, uint32_t lba,
                          void *ctx, uint32_t ctx_start_lba, 
                          int *tag_type, uint32_t *next_tag, int flag)
{
    int decode_ret, ret;
    char md5[16], cloned_md5[16];
    uint32_t pos, range_start, range_size;
    void *cloned_ctx = NULL;

    *tag_type = 0;
    decode_ret = iso_util_decode_md5_tag(block, tag_type, &pos,
                                  &range_start, &range_size, next_tag, md5, 0);
    if (decode_ret != 1 && decode_ret != (int) ISO_MD5_AREA_CORRUPTED)
        return 0;
    if (*tag_type > 30)
        goto unexpected_type;

    if (decode_ret == (int) ISO_MD5_AREA_CORRUPTED) {
        ret = decode_ret; 
        goto ex;
    } else if (!((1 << *tag_type) & desired)) {
unexpected_type:;
        iso_msg_submit(-1, ISO_MD5_TAG_UNEXPECTED, 0, NULL);
        ret = 0;
        goto ex;
    } else if (pos != lba) {
        if (*tag_type == 2) { /* Superblock tag */
            if (lba < 32) {
                /* Check whether this is a copied superblock */
                range_start -= (off_t) pos - (off_t) lba;
                if (range_start != ctx_start_lba) {

                    /* >>> check for matching MD5 ? */;

                    ret = ISO_MD5_TAG_MISPLACED;
                } else
                    ret = ISO_MD5_TAG_COPIED;
                goto ex;
            }
        }
        ret = ISO_MD5_TAG_MISPLACED;
        goto ex;
    } else if (range_start != ctx_start_lba) {
        ret = ISO_MD5_TAG_MISPLACED;
    }
    ret = iso_md5_clone(ctx, &cloned_ctx);
    if (ret < 0)
        goto ex;
    iso_md5_end(&cloned_ctx, cloned_md5);
    if (! iso_md5_match(cloned_md5, md5)) {
        ret = ISO_MD5_TAG_MISMATCH;
        goto ex;
    }
    ret = 1;
ex:;
    if (ret < 0)
        iso_msg_submit(-1, ret, 0, NULL);
    return ret;
}


void *iso_alloc_mem(size_t size, size_t count, int flag)
{
    void *pt;

    pt = calloc(size, count);
    if(pt == NULL)
	iso_msg_submit(-1, ISO_OUT_OF_MEM, 0, "Out of virtual memory");
    return pt;
}


uint16_t iso_ntohs(uint16_t v)
{
    return iso_read_msb((uint8_t *) &v, 2);
}

uint16_t iso_htons(uint16_t v)
{
    uint16_t ret;

    iso_msb((uint8_t *) &ret, (uint32_t) v, 2);

    return ret;
}   

