/*
  data source for libisoburn.

  Copyright 2007 - 2012 Vreixo Formoso Lopes <metalpain2002@yahoo.es>
                    and Thomas Schmitt <scdbackup@gmx.net>
  Provided under GPL version 2 or later.
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <stdio.h>


#ifndef Xorriso_standalonE

#include <libburn/libburn.h>

#include <libisofs/libisofs.h>

#else /* ! Xorriso_standalonE */

#include "../libisofs/libisofs.h"
#include "../libburn/libburn.h"

#endif /* Xorriso_standalonE */


#include "isoburn.h"


/* Cached reading of image tree data by multiple tiles */


/* Debugging only: This reports cache loads on stderr.
#define Libisoburn_read_cache_reporT 1
*/


struct isoburn_cache_tile {
 char *cache_data;
 uint32_t cache_lba;
 uint32_t last_error_lba;
 uint32_t last_aligned_error_lba;
 int cache_hits;
 int age;
};

struct isoburn_cached_drive {
 struct burn_drive *drive;
 struct isoburn_cache_tile **tiles;
 int num_tiles;
 int tile_blocks;
 int current_age;

 /**    
  Offset to be applied to all block addresses to compensate for an
  eventual displacement of the block addresses relative to the image
  start block address that was assumed when the image was created.
  E.g. if track number 2 gets copied into a disk file and shall then
  be loaded as ISO filesystem.
  If displacement_sign is 1 then the displacement number will be
  added to .read_block() addresses, if -1 it will be subtracted.
  Else it will be ignored.
 */
 uint32_t displacement;
 int      displacement_sign;

};

#define Libisoburn_max_agE 2000000000

static int ds_inc_age(struct isoburn_cached_drive *icd, int idx, int flag);


int ds_read_block(IsoDataSource *src, uint32_t lba, uint8_t *buffer)
{
 int ret, i, oldest, oldest_age;
 struct burn_drive *d;
 off_t count;
 uint32_t aligned_lba;
 char msg[80];
 struct isoburn_cache_tile **tiles;
 struct isoburn_cached_drive *icd;

 if(src == NULL || buffer == NULL)
   /* It is not required by the specs of libisofs but implicitely assumed
      by its current implementation that a data source read result <0 is
      a valid libisofs error code.
   */
   return ISO_NULL_POINTER;

 icd = (struct isoburn_cached_drive *) src->data;
 d = (struct burn_drive*) icd->drive;

 if(d == NULL) {
   /* This would happen if libisoburn saw output data in the fifo and
      performed early drive release and afterwards libisofs still tries
      to read data.
      That would constitute a bad conceptual problem in libisoburn.
   */
   isoburn_msgs_submit(NULL, 0x00060000,
     "Programming error: Drive released while libisofs still attempts to read",
     0, "FATAL", 0);
   return ISO_ASSERT_FAILURE;
 }

 tiles = icd->tiles;

 if(icd->displacement_sign == 1) {
   if(lba + icd->displacement < lba) {
address_rollover:;
      return ISO_DISPLACE_ROLLOVER;
   } else
     lba += icd->displacement;
 } else if(icd->displacement_sign == -1) {
   if(lba < icd->displacement )
     goto address_rollover;
   else
     lba -= icd->displacement;
 }

 aligned_lba= lba & ~(icd->tile_blocks - 1);

 for (i = 0; i < icd->num_tiles; i++) {
   if(aligned_lba == tiles[i]->cache_lba &&
      tiles[i]->cache_lba != 0xffffffff) {
     (tiles[i]->cache_hits)++;
     memcpy(buffer, tiles[i]->cache_data + (lba - aligned_lba) * 2048, 2048);
     count= 2048;
     ds_inc_age(icd, i, 0);
     return 1;
   }
 }

 /* find oldest tile */
 oldest_age= Libisoburn_max_agE;
 oldest= 0;
 for(i = 0; i < icd->num_tiles; i++) {
   if(tiles[i]->cache_lba == 0xffffffff) {
     oldest= i;
 break;
   }
   if(tiles[i]->age < oldest_age) {
     oldest_age= tiles[i]->age;
     oldest= i;
   }
 }

 tiles[oldest]->cache_lba= 0xffffffff; /* invalidate cache */
 if(tiles[oldest]->last_aligned_error_lba == aligned_lba) {
   ret = 0;
 } else {
   ret = burn_read_data(d, (off_t) aligned_lba * (off_t) 2048,
                        (char *) tiles[oldest]->cache_data,
                        icd->tile_blocks * 2048, &count, 2);
 }
 if (ret <= 0 ) {
   tiles[oldest]->last_aligned_error_lba = aligned_lba;

   /* Read-ahead failure ? Try to read 2048 directly. */
   if(tiles[oldest]->last_error_lba == lba)
     ret = 0;
   else
     ret = burn_read_data(d, (off_t) lba * (off_t) 2048, (char *) buffer,
                        2048, &count, 0);
   if (ret > 0)
     return 1;
   tiles[oldest]->last_error_lba = lba;
   sprintf(msg, "ds_read_block(%lu) returns %lX",
           (unsigned long) lba, (unsigned long) ret);
   isoburn_msgs_submit(NULL, 0x00060000, msg, 0, "DEBUG", 0);
   return ISO_DATA_SOURCE_MISHAP;
 }

#ifdef Libisoburn_read_cache_reporT
 fprintf(stderr, "Tile %2.2d : After %3d hits, new load from %8x , count= %d\n",
         oldest, tiles[oldest]->cache_hits, aligned_lba, (int) count);
#endif

 tiles[oldest]->cache_lba= aligned_lba;
 tiles[oldest]->cache_hits= 1;
 ds_inc_age(icd, oldest, 0);

 memcpy(buffer, tiles[oldest]->cache_data + (lba - aligned_lba) * 2048, 2048);
 count= 2048;

 return 1;
}


static int ds_open(IsoDataSource *src)
{
 /* nothing to do, device is always grabbed */
 return 1;
}

static int ds_close(IsoDataSource *src)
{
 /* nothing to do, device is always grabbed */
 return 1;
}


static int isoburn_cache_tile_destroy(struct isoburn_cache_tile **o,
                                      int flag)
{
 if (*o == NULL)
   return(0);
 if ((*o)->cache_data != NULL)
   free((*o)->cache_data);
 free(*o);
 *o = NULL;
 return(1);
}


static int isoburn_cache_tile_new(struct isoburn_cache_tile **o,
                                  int tile_blocks, int flag)
{
 struct isoburn_cache_tile *t;

 *o = t = calloc(1, sizeof(struct isoburn_cache_tile));
 if (t == NULL)
   goto fail;
 t->cache_data = NULL;
 t->cache_lba = 0xffffffff;
 t->cache_hits = 0;
 t->last_error_lba = 0xffffffff;
 t->last_aligned_error_lba = 0xffffffff;
 t->age= 0;

 t->cache_data = calloc(1, tile_blocks * 2048);
 if (t->cache_data == NULL)
   goto fail;

 return(1);
fail:;
 isoburn_cache_tile_destroy(o, 0);
 return(-1);
}


static int isoburn_cached_drive_destroy(struct isoburn_cached_drive **o,
                                        int flag)
{
 struct isoburn_cached_drive *c;
 int i;

 if (*o == NULL)
   return(0);
 c= *o;
 if (c->tiles != NULL) {
   for (i = 0; i < c->num_tiles; i++) 
     isoburn_cache_tile_destroy(&(c->tiles[i]), 0);
   free(c->tiles);
 }
 free(c);
 *o= NULL;
 return(1);
}


static int isoburn_cached_drive_new(struct isoburn_cached_drive **o,
                                    struct burn_drive *d, int cache_tiles,
                                    int tile_blocks, int flag)
{
 struct isoburn_cached_drive *icd;
 int i, ret;

 *o = icd = calloc(1,sizeof(struct isoburn_cached_drive));
 if (*o == NULL)
   return(-1);
 icd->drive = d;
 icd->tiles = NULL;
 icd->num_tiles = cache_tiles;
 icd->tile_blocks = tile_blocks;
 icd->current_age = 0;
 icd->displacement = 0;
 icd->displacement_sign = 0;

 icd->tiles = calloc(1, sizeof(struct isoburn_cache_tile *) * icd->num_tiles);
 if (icd->tiles == NULL)
   goto fail;
 for (i = 0; i < icd->num_tiles; i++) {
   ret = isoburn_cache_tile_new(&(icd->tiles[i]), icd->tile_blocks, 0);
   if (ret <= 0)
     goto fail;
 }
 return(1);
fail:;
 isoburn_cached_drive_destroy(o, 0);
 return(-1);
}


static void ds_free_data(IsoDataSource *src)
{
 struct isoburn_cached_drive *icd;

 if(src->data != NULL) {
   icd= (struct isoburn_cached_drive *) src->data;
   isoburn_cached_drive_destroy(&icd, 0);
 }
 src->data= NULL;
}


int isoburn_data_source_shutdown(IsoDataSource *src, int flag)
{
 struct isoburn_cached_drive *icd;

 if(src==NULL)
   return(0);
 icd= (struct isoburn_cached_drive *) src->data;
 icd->drive= NULL;
 return(1);
}


IsoDataSource *isoburn_data_source_new(struct burn_drive *d,
                                  uint32_t displacement, int displacement_sign,
                                  int cache_tiles, int tile_blocks)
{
 IsoDataSource *src;
 struct isoburn_cached_drive *icd= NULL;
 int ret;

 if (d==NULL)
   return NULL;
 src = malloc(sizeof(IsoDataSource));
 if (src == NULL)
   return NULL;
 ret = isoburn_cached_drive_new(&icd, d, cache_tiles, tile_blocks, 0);
 if (ret <= 0) {
   free(src);
   return NULL;
 }
 src->version = 0;
 src->refcount = 1;
 src->read_block = ds_read_block;
 src->open = ds_open;
 src->close = ds_close;
 src->free_data = ds_free_data;
 src->data = icd;
 icd->displacement = displacement;
 icd->displacement_sign = displacement_sign;
 return src;
}


static int ds_inc_age(struct isoburn_cached_drive *icd, int idx, int flag)
{
 int i;

 (icd->current_age)++;
 if(icd->current_age>=Libisoburn_max_agE) { /* reset all ages (allow waste) */
   for(i = 0; i < icd->num_tiles; i++)
     (icd->tiles)[i]->age= 0;
   icd->current_age= 1;
 }
 (icd->tiles)[idx]->age= icd->current_age;
 return(1);
}


