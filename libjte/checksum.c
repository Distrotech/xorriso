/*
 * checksum.c
 *
 * Copyright (c) 2008- Steve McIntyre <steve@einval.com>
 *
 * Implementation of a generic checksum interface, used in JTE.
 *
 * GNU GPL v2+
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <sys/types.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif

#include "md5.h"
#include "sha1.h"
#include "sha256.h"
#include "sha512.h"
#include "checksum.h"

#ifdef THREADED_CHECKSUMS
#include <pthread.h>
#endif

static void md5_init(void *context)
{
    mk_MD5Init(context);
}
static void md5_update(void *context, unsigned char const *buf, unsigned int len)
{
    mk_MD5Update(context, buf, len);
}
static void md5_final(unsigned char *digest, void *context)
{
    mk_MD5Final(digest, context);
}

static void sha1_init(void *context)
{
    sha1_init_ctx(context);
}
static void sha1_update(void *context, unsigned char const *buf, unsigned int len)
{
    sha1_write(context, buf, len);
}
static void sha1_final(unsigned char *digest, void *context)
{
    sha1_finish_ctx(context);
    memcpy(digest, sha1_read(context), 20);
}

static void sha256_init(void *context)
{
    sha256_init_ctx(context);
}
static void sha256_update(void *context, unsigned char const *buf, unsigned int len)
{
    sha256_process_bytes(buf, len, context);
}
static void sha256_final(unsigned char *digest, void *context)
{
    sha256_finish_ctx(context, digest);
}

static void sha512_init(void *context)
{
    sha512_init_ctx(context);
}
static void sha512_update(void *context, unsigned char const *buf, unsigned int len)
{
    sha512_process_bytes(buf, len, context);
}
static void sha512_final(unsigned char *digest, void *context)
{
    sha512_finish_ctx(context, digest);
}

struct checksum_details
{
    char          *name;
    char          *prog;
    int            digest_size;
    int            context_size;
    void          (*init)(void *context);
    void          (*update)(void *context, unsigned char const *buf, unsigned int len);
    void          (*final)(unsigned char *digest, void *context);
    int           check_used_value;
};

static const struct checksum_details algorithms[] = 
{
    {
        "MD5",
        "md5sum",
        16,
        sizeof(struct mk_MD5Context),
        md5_init,
        md5_update,
        md5_final,
        CHECK_MD5_USED
    },
    {
        "SHA1",
        "sha1sum",
        20,
        sizeof(SHA1_CONTEXT),
        sha1_init,
        sha1_update,
        sha1_final,
        CHECK_SHA1_USED
    },
    {
        "SHA256",
        "sha256sum",
        32,
        sizeof(struct sha256_ctx),
        sha256_init,
        sha256_update,
        sha256_final,
        CHECK_SHA256_USED
    },
    {
        "SHA512",
        "sha512sum",
        64,
        sizeof(struct sha512_ctx),
        sha512_init,
        sha512_update,
        sha512_final,
        CHECK_SHA512_USED
    }
};

struct algo_context
{
    void                     *context;
    unsigned char            *digest;
    int                       enabled;
    int                       finalised;
    char                     *hexdump;
#ifdef THREADED_CHECKSUMS
    unsigned char const      *buf;
    unsigned int              len;
    int                       which;
    pthread_t                 thread;
    struct _checksum_context *parent;
    pthread_mutex_t           start_mutex;
    pthread_cond_t            start_cv;
#endif
};

struct _checksum_context
{
#ifdef THREADED_CHECKSUMS
    unsigned int           index;
    unsigned int           threads_running;
    unsigned int           threads_desired;
    pthread_mutex_t        done_mutex;
    pthread_cond_t         done_cv;
#endif
    char                  *owner;
    struct algo_context    algo[NUM_CHECKSUMS];
};

struct checksum_info *checksum_information(enum checksum_types which)
{
    return (struct checksum_info *)&algorithms[which];
}

/* Dump a buffer in hex */
static void hex_dump_to_buffer(char *output_buffer, unsigned char *buf, size_t buf_size)
{
    unsigned int i;
    char *p = output_buffer;

    memset(output_buffer, 0, 1 + (2*buf_size));
    for (i = 0; i < buf_size ; i++)
        p += sprintf(p, "%2.2x", buf[i]);
}

#ifdef THREADED_CHECKSUMS
static void *checksum_thread(void *arg)
{
    struct algo_context *a = arg;
    struct _checksum_context *c = a->parent;
    int num_blocks_summed = 0;

    while (1)
    {
        /* wait to be given some work to do */
        pthread_mutex_lock(&a->start_mutex);
        while (a->buf == NULL)
        {
            pthread_cond_wait(&a->start_cv, &a->start_mutex);
        }
        pthread_mutex_unlock(&a->start_mutex);

        /* if we're given a zero-length buffer, then that means we're
         * done */
        if (a->len == 0)
            break;

        /* actually do the checksum on the supplied buffer */
        algorithms[a->which].update(a->context, a->buf, a->len);
        num_blocks_summed++;
        a->buf = NULL;

        /* and tell the main thread that we're done with that
         * buffer */
        pthread_mutex_lock(&c->done_mutex);
        c->threads_running--;
        if (c->threads_running == 0)
            pthread_cond_signal(&c->done_cv);
        pthread_mutex_unlock(&c->done_mutex);
    }

    pthread_exit(0);
}
#endif

checksum_context_t *checksum_init_context(int checksums, const char *owner)
{
    int i = 0;
#ifdef THREADED_CHECKSUMS
    int ret = 0;
#endif

    struct _checksum_context *context = calloc(1, sizeof(struct _checksum_context));

    if (!context)
        return NULL;

    context->owner = strdup(owner);
    if (!context->owner)
    {
        free(context);
        return NULL;
    }   

#ifdef THREADED_CHECKSUMS
    pthread_mutex_init(&context->done_mutex, NULL);
    pthread_cond_init(&context->done_cv, NULL);
    context->index = 0;
    context->threads_running = 0;
    context->threads_desired = 0;

    for (i = 0; i < NUM_CHECKSUMS; i++)
        if ( (1 << i) & checksums)
            context->threads_desired++;    
#endif

    for (i = 0; i < NUM_CHECKSUMS; i++)
    {
        struct algo_context *a = &context->algo[i];
        if ( (1 << i) & checksums)
        {
            a->context = malloc(algorithms[i].context_size);
            if (!a->context)
            {
                checksum_free_context(context);
                return NULL;
            }
            a->digest = malloc(algorithms[i].digest_size);
            if (!a->digest)
            {
                checksum_free_context(context);
                return NULL;
            }
            a->hexdump = malloc(1 + (2*algorithms[i].digest_size));
            if (!a->hexdump)
            {
                checksum_free_context(context);
                return NULL;
            }
            algorithms[i].init(a->context);
            a->enabled = 1;
            a->finalised = 0;
#ifdef THREADED_CHECKSUMS
            a->which = i;
            a->parent = context;
            a->buf = NULL;
            a->len = 0;
            pthread_mutex_init(&a->start_mutex, NULL);
            pthread_cond_init(&a->start_cv, NULL);
            ret = pthread_create(&a->thread, NULL, checksum_thread, a);
            if (ret != 0)
            {
                /* libjte issues an own message:
                  fprintf(stderr, "failed to create new thread: %d\n", ret);
                */
                checksum_free_context(context);
                return NULL;
            }
#endif
        }
        else
            a->enabled = 0;
    }
    
    return context;
}

void checksum_free_context(checksum_context_t *context)
{
    int i = 0;
    struct _checksum_context *c = context;

    for (i = 0; i < NUM_CHECKSUMS; i++)
    {
        struct algo_context *a = &c->algo[i];

#ifdef THREADED_CHECKSUMS
        if (a->thread)
        {
            void *ret;
            pthread_cancel(a->thread);
            pthread_join(a->thread, &ret);
            a->thread = 0;
        }
#endif
        free(a->context);
        free(a->digest);
        free(a->hexdump);
    }
    free(c->owner);
    free(c);
}

#ifdef THREADED_CHECKSUMS
void checksum_update(checksum_context_t *context,
                     unsigned char const *buf, unsigned int len)
{
    int i = 0;
    struct _checksum_context *c = context;

    /* >>> TODO : Find out for what purpose index shall serve.
                  It is defined here and incremented. Not more.
    */
    static int index = 0;

    index++;

    c->threads_running = c->threads_desired;    
    for (i = 0; i < NUM_CHECKSUMS; i++)
    {
        if (c->algo[i].enabled)
        {
            struct algo_context *a = &c->algo[i];
            pthread_mutex_lock(&a->start_mutex);
            a->len = len;
            a->buf = buf;
            pthread_cond_signal(&a->start_cv);
            pthread_mutex_unlock(&a->start_mutex);
        }
    }

    /* Should now all be running, wait on them all to return */
    pthread_mutex_lock(&c->done_mutex);
    while (c->threads_running > 0)
    {
        pthread_cond_wait(&c->done_cv, &c->done_mutex);
    }
    pthread_mutex_unlock(&c->done_mutex);
}

#else /* THREADED_CHECKSUMS */

void checksum_update(checksum_context_t *context,
                     unsigned char const *buf, unsigned int len)
{
    int i = 0;
    struct _checksum_context *c = context;
    
    for (i = 0; i < NUM_CHECKSUMS; i++)
    {
        if (c->algo[i].enabled)
        {
            struct algo_context *a = &c->algo[i];
            algorithms[i].update(a->context, buf, len);
        }
    }
}

#endif /* THREADED_CHECKSUMS */

void checksum_final(checksum_context_t *context)
{
    int i = 0;
    struct _checksum_context *c = context;
    
#ifdef THREADED_CHECKSUMS
    /* Clean up the threads */
    c->threads_running = c->threads_desired;    

    for (i = 0; i < NUM_CHECKSUMS; i++)
    {
        if (c->algo[i].enabled)
        {
            void *ret = 0;
            struct algo_context *a = &c->algo[i];

            pthread_mutex_lock(&a->start_mutex);
            a->len = 0;
            a->buf = (unsigned char *)-1;
            pthread_cond_signal(&a->start_cv);
            pthread_mutex_unlock(&a->start_mutex);
            pthread_join(a->thread, &ret);
            a->thread = 0;
        }
    }
#endif

    for (i = 0; i < NUM_CHECKSUMS; i++)
    {
        struct algo_context *a = &c->algo[i];
        if (a->enabled)
        {
            algorithms[i].final(a->digest, a->context);
            hex_dump_to_buffer(a->hexdump, a->digest, algorithms[i].digest_size);
            a->finalised = 1;
        }
    }
}

void checksum_copy(checksum_context_t *context,
                   enum checksum_types which,
                   unsigned char *digest)
{
    struct _checksum_context *c = context;

    if (c->algo[which].enabled)
    {
        if (c->algo[which].finalised)
            memcpy(digest, c->algo[which].digest, algorithms[which].digest_size);
        else
            memset(digest, 0, algorithms[which].digest_size);
    }

    else
        /* >>> TODO : ??? Can this happen ? Why print and then go on ? */
        fprintf(stderr, "Asked for %s checksum, not enabled!\n",
                algorithms[which].name);
}

const char *checksum_hex(checksum_context_t *context,
                         enum checksum_types which)
{
    struct _checksum_context *c = context;

    if (c->algo[which].enabled && c->algo[which].finalised)
        return c->algo[which].hexdump;

    /* else */
    return NULL;
}


/* Parse the command line options for which checksums to use */
int parse_checksum_algo(char *arg, int *algo)
{
    int i = 0;
    char *start_ptr = arg;
    int len = 0;

    (*algo) |= CHECK_MD5_USED; 

    if (!strcasecmp(arg, "all"))
    {
        *algo = 0xFF;
        return 0;
    }
    
    while (*start_ptr != 0)
    {
        int match = 0;
        len = 0;

        while (start_ptr[len] != ',' && start_ptr[len] != 0)
            len++;
        
        if (len)
        {
            for (i = 0; i < NUM_CHECKSUMS; i++)
            {
                if (len == (int) strlen(algorithms[i].name) &&
                    !strncasecmp(start_ptr, algorithms[i].name, len))
                {
                    match = 1;
                    *algo |= algorithms[i].check_used_value;
                }
            }
        
            if (!match)
            {
                return EINVAL;
            }
        }
        
        if (start_ptr[len] == 0)
            break;
            
        start_ptr += len + 1;
    }
   
    return 0;
}

#ifdef CHECKSUM_SELF_TEST
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    char buf[1024];
    int fd = -1;
    char *filename;
    int err = 0;
    static checksum_context_t *test_context = NULL;
    int i = 0;

    if (argc != 2)
    {
        fprintf(stderr, "Need a filename to act on!\n");
        return 1;
    }

    filename = argv[1];
    fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        fprintf(stderr, "Unable to open file %s, errno %d\n", filename, errno);
        return 1;
    }

    test_context = checksum_init_context(CHECK_ALL_USED, "test");
    if (!test_context)
    {
        fprintf(stderr, "Unable to initialise checksum context\n");
        close(fd);
        return 1;
    }

    while(1)
    {
        err = read(fd, buf, sizeof(buf));
        if (err < 0)
        {
            fprintf(stderr, "Failed to read from file, errno %d\n", errno);
            return 1;
        }

        if (err == 0)
            break; /* EOF */

        /* else */
        checksum_update(test_context, buf, err);
    }
    close(fd);
    checksum_final(test_context);

    for (i = 0; i < NUM_CHECKSUMS; i++)
    {
        struct checksum_info *info;
        unsigned char r[64];
        int j = 0;

        info = checksum_information(i);
        memset(r, 0, sizeof(r));

        checksum_copy(test_context, i, r);

        printf("OUR %s:\n", info->name);
        for (j = 0; j < info->digest_size; j++)
            printf("%2.2x", r[j]);
        printf("  %s\n", filename);
        printf("system checksum program (%s):\n", info->prog);
        sprintf(buf, "%s %s", info->prog, filename);
        system(buf);
        printf("\n");
    }
    return 0;
}
#endif /* CHECKSUM_SELF_TEST */

