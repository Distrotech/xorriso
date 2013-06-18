
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2013 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the implementation of classes SpotlistiteM, SectorbitmaP,
   CheckmediajoB which perform verifying runs on media resp. images.
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>

#include "xorriso.h"
#include "xorriso_private.h"
#include "xorrisoburn.h"


/* ------------------------------ SpotlisT -------------------------------- */


struct SpotlistiteM {
 int start_lba;
 int blocks;
 int quality;
 struct SpotlistiteM *next;
};


int Spotlistitem_new(struct SpotlistiteM **o, int start_lba, int blocks,
                     int quality, int flag)
{
 struct SpotlistiteM *m;

 m= TSOB_FELD(struct SpotlistiteM,1);
 if(m==NULL)
   return(-1);
 *o= m;
 m->start_lba= start_lba;
 m->blocks= blocks;
 m->quality= quality;
 m->next= NULL;
 return(1);
}


int Spotlistitem_destroy(struct SpotlistiteM **o, int flag)
{  
 if((*o) == NULL)
   return(0);
 free((char *) *o);
 *o= NULL;
 return(1);
}


struct SpotlisT {
 struct SpotlistiteM *list_start;
 struct SpotlistiteM *list_end;
 int list_count;
 struct SpotlistiteM *current_item;
 int current_idx;
};


int Spotlist_new(struct SpotlisT **o, int flag)
{
 struct SpotlisT *m;

 m= TSOB_FELD(struct SpotlisT,1);
 if(m==NULL)
   return(-1);
 *o= m;
 m->list_start= NULL;
 m->list_end= NULL;
 m->list_count= 0;
 m->current_item= NULL;
 m->current_idx= -1;
 return(1);
}


int Spotlist_destroy(struct SpotlisT **o, int flag)
{
 struct SpotlisT *m;
 struct SpotlistiteM *li, *next_li;

 if((*o) == NULL)
   return(0);
 m= *o;
 for(li= m->list_start; li != NULL; li= next_li) {
   next_li= li->next;
   Spotlistitem_destroy(&li, 0);
 }
 free((char *) *o);
 *o= NULL;
 return(1);
}


int Spotlist_add_item(struct SpotlisT *o, int start_lba, int blocks, 
                      int quality, int flag)
{
 int ret;
 struct SpotlistiteM *li;
 static int debug_verbous= 0;

 ret= Spotlistitem_new(&li, start_lba, blocks, quality, 0);
 if(ret <= 0)
   return(ret);
 if(o->list_end != NULL)
   o->list_end->next= li;
 o->list_end= li;
 if(o->list_start == NULL)
   o->list_start= li;
 (o->list_count)++;

 if(debug_verbous) {char quality_name[80];
   fprintf(stderr, "debug: lba %10d , size %10d , quality '%s'\n",
          start_lba, blocks, Spotlist__quality_name(quality, quality_name,
                                         Xorriso_read_quality_invaliD, 0) + 2);
 }

 return(1);
}


int Spotlist_count(struct SpotlisT *o, int flag)
{
 return o->list_count;
}


int Spotlist_block_count(struct SpotlisT *o, int flag)
{
 int list_blocks= 0;
 struct SpotlistiteM *li;

 for(li= o->list_start; li != NULL; li= li->next) {
   if(li->start_lba + li->blocks > list_blocks)
     list_blocks= li->start_lba + li->blocks;
 }
 return(list_blocks);
}


int Spotlist_sector_size(struct SpotlisT *o, int read_chunk, int flag)
{
 int sector_size;
 struct SpotlistiteM *li;

 sector_size= read_chunk * 2048;
 for(li= o->list_start; li != NULL; li= li->next) {
   if((li->start_lba % read_chunk) || (li->blocks % read_chunk)) {
     sector_size= 2048;
 break;
   }
 }
 return(sector_size);
}


int Spotlist_get_item(struct SpotlisT *o, int idx, 
                      int *start_lba, int *blocks, int *quality, int flag)
{
 int i;
 struct SpotlistiteM *li;
 
 if(idx < 0 || idx > o->list_count)
   return(0);
 if(idx == o->current_idx && o->current_item != NULL)
   li= o->current_item;
 else if(idx == o->current_idx + 1 && o->current_item != NULL) {
   li= o->current_item->next;
 } else {
   li= o->list_start;
   for(i= 0; i < idx; i++)
     li= li->next;
 }
 o->current_item= li;
 o->current_idx= idx;
 *start_lba= li->start_lba;
 *blocks= li->blocks;
 *quality= li->quality;
 return(1);
}


char *Spotlist__quality_name(int quality, char name[80], int bad_limit,
                             int flag)
{
 if(quality == Xorriso_read_quality_untesteD ||
   quality == Xorriso_read_quality_tao_enD ||
   quality == Xorriso_read_quality_off_tracK)
   strcpy(name, "0 ");
 else if(quality <= bad_limit)
   strcpy(name, "- ");
 else
   strcpy(name, "+ ");
 if(quality == Xorriso_read_quality_gooD)
   strcat(name, "good");
 else if(quality == Xorriso_read_quality_md5_matcH)
   strcat(name, "md5_match");
 else if(quality == Xorriso_read_quality_sloW)
   strcat(name, "slow");
 else if(quality == Xorriso_read_quality_partiaL)
   strcat(name, "partial");
 else if(quality == Xorriso_read_quality_valiD)
   strcat(name, "valid");
 else if(quality == Xorriso_read_quality_untesteD)
   strcat(name, "untested");
 else if(quality == Xorriso_read_quality_invaliD)
   strcat(name, "invalid");
 else if(quality == Xorriso_read_quality_tao_enD)
   strcat(name, "tao_end");
 else if(quality == Xorriso_read_quality_off_tracK)
   strcat(name, "off_track");
 else if(quality == Xorriso_read_quality_md5_mismatcH)
   strcat(name, "md5_mismatch");
 else if(quality == Xorriso_read_quality_unreadablE)
   strcat(name, "unreadable");
 else
   sprintf(name, "0 0x%8.8X", (unsigned int) quality);
 return(name);
}


/* ---------------------------- End SpotlisT ------------------------------ */

/* ---------------------------- SectorbitmaP ------------------------------ */

int Sectorbitmap_new(struct SectorbitmaP **o, int sectors, int sector_size,
                     int flag)
{
 struct SectorbitmaP *m;

 m= TSOB_FELD(struct SectorbitmaP,1);
 if(m==NULL)
   return(-1);
 *o= m;
 m->sectors= sectors;
 m->sector_size= sector_size;
 m->map= NULL;
 m->map_size= sectors / 8 + 1;

 m->map= calloc(m->map_size, 1);
 if(m->map == NULL)
   goto failure;
 return(1);
failure:;
 Sectorbitmap_destroy(o, 0);
 return(-1);
}


int Sectorbitmap_destroy(struct SectorbitmaP **o, int flag)
{  
 if((*o) == NULL)
   return(0);
 if((*o)->map != NULL)
   free((char *) (*o)->map);
 free((char *) *o);
 *o= NULL;
 return(1);
}


int Sectorbitmap_from_file(struct SectorbitmaP **o, char *path, char *msg,
                           int *os_errno, int flag)
{
 int ret, fd= -1, sectors, sector_size, i, todo, map_size, skip, bufsize= 1024;
 unsigned char *map;
 unsigned char *buf;

 buf= TSOB_FELD(unsigned char, bufsize);
 if(buf == NULL)
   return(-1);

 *os_errno= 0;
 if(msg != NULL)
   msg[0]= 0;
 fd= open(path, O_RDONLY);
 if(fd == -1) {
   *os_errno= errno;
   if(msg != NULL) {
     strcpy(msg, "Cannot open path ");
     Text_shellsafe(path, msg+strlen(msg), 0);
   }
   {ret= 0; goto ex;}
 }
 ret= read(fd, buf, 32);
 if(ret < 32) {
wrong_filetype:;
   if(ret == -1)
     *os_errno= errno;
   if(msg != NULL) {
     strcpy(msg, "Not a sector bitmap file: ");
     Text_shellsafe(path, msg+strlen(msg), 0);
   }
   ret= 0; goto ex;
 }
 if(strncmp((char *) buf, "xorriso sector bitmap v1        ", 32) == 0)
   /* ok */;
 else if(strncmp((char *) buf, "xorriso sector bitmap v2 ", 25) == 0) {
   skip= -1;
   sscanf(((char *) buf) + 25, "%d", &skip);
   if(skip < 0)
     {ret= 0; goto wrong_filetype;}
   for(i= 0; i < skip; i+= bufsize) {
     todo= bufsize;
     if(i + todo > skip)
       todo= skip - i;
     ret= read(fd, buf, todo);
     if(ret < todo)
       goto wrong_filetype;
   }
 } else
   {ret= 0; goto wrong_filetype;}
 ret= read(fd, buf, 8);
 if(ret < 4)
   goto wrong_filetype;
 sectors= (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
 sector_size= (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
 if(sectors <= 0 || sector_size <= 0)
   goto wrong_filetype;
 ret= Sectorbitmap_new(o, sectors, sector_size, 0);
 if(ret <= 0) {
   if(msg != NULL)
     sprintf(msg, "Cannot allocate bitmap memory for %d sectors", sectors);
   ret= -1; goto ex;
 }
 map= (*o)->map;
 map_size= (*o)->map_size;
 for(i= 0; i < map_size; i+= bufsize) {
   todo= bufsize;
   if(i + todo > map_size)
     todo= map_size - i;
   ret= read(fd, buf, todo);
   if(ret != todo)
     goto wrong_filetype;
   memcpy(map + i, buf, todo);
 }
 ret= 1;
ex:;
 if(fd != -1)
   close(fd);
 if(buf != NULL)
   free(buf);
 if(ret <= 0)
   Sectorbitmap_destroy(o, 0);
 return(ret);
}


int Sectorbitmap_to_file(struct SectorbitmaP *o, char *path, char *info,
                         char *msg, int *os_errno, int flag)
{
 int ret, fd= -1, j, l;
 unsigned char buf[40];

 *os_errno= 0;
 fd= open(path, O_WRONLY | O_CREAT,  S_IRUSR | S_IWUSR);
 if(fd == -1) {
   *os_errno= errno;
   if(msg != NULL) {
     strcpy(msg, "Cannot open path ");
     Text_shellsafe(path, msg+strlen(msg), 0);
   }
   return(0);
 }

 l= 0;
 if(info != NULL)
   l= strlen(info);
 if(l > 999999) {
   strcpy(msg, "Info string is longer than 999999 bytes");
   return(0);
 }
 sprintf((char *) buf, "xorriso sector bitmap v2 %-6d\n", l);

 ret= write(fd, buf, 32);
 if(ret != 32) {
cannot_write:;
   *os_errno= errno;
   if(msg != NULL) {
     strcpy(msg, "Cannot write to ");
     Text_shellsafe(path, msg+strlen(msg), 0);
   }
   ret= 0; goto ex;
 }
 if(l > 0) {
   ret= write(fd, info, l);
   if(ret != l)
     goto cannot_write;
 }

 for(j= 0; j < 4; j++) {
   buf[j]= o->sectors >> (24 - j * 8);
   buf[j+4]= o->sector_size >> (24 - j * 8);
 }
 ret= write(fd, buf, 8);
 if(ret != 8)
   goto cannot_write;
 ret= write(fd, o->map, o->map_size);
 if(ret != o->map_size)
   goto cannot_write;

 ret= 1;
ex:;
 if(fd != -1)
   close(fd);
 return(ret);
}


/* @param flag bit0= sector bit value
*/
int Sectorbitmap_set(struct SectorbitmaP *o, int sector, int flag)
{
 if(sector < 0 || sector >= o->sectors)
   return(0);
 if(flag & 1)
   o->map[sector / 8]|= 1 << (sector % 8);
 else
   o->map[sector / 8]&= ~(1 << (sector % 8));
 return(1);
}


/* @param flag bit0= sector bit value
*/
int Sectorbitmap_set_range(struct SectorbitmaP *o,
                           int start_sector, int sectors, int flag)
{
 int start_i, end_i, i;
 unsigned char value;

 if(start_sector < 0 || start_sector + sectors > o->sectors || sectors < 1)
   return(0);
 if(flag & 1)
   value= ~0;
 else
   value= 0;
 start_i= start_sector / 8;
 end_i= (start_sector + sectors - 1) / 8;
 for(i= start_sector; i / 8 == start_i && i < start_sector + sectors; i++)
   Sectorbitmap_set(o, i, flag & 1);
 for(i= start_i + 1; i < end_i; i++)
   o->map[i]= value;
 if(end_i > start_i)
   for(i= end_i * 8; i < start_sector + sectors; i++)
     Sectorbitmap_set(o, i, flag & 1);
 return(1);
}


int Sectorbitmap_is_set(struct SectorbitmaP *o, int sector, int flag)
{
 if(sector < 0 || sector >= o->sectors)
   return(0);
 return(!! (o->map[sector / 8] & (1 << (sector % 8))));
}


int Sectorbitmap_bytes_are_set(struct SectorbitmaP *o,
                               off_t start_byte, off_t end_byte, int flag)
{
 int end_sector, i;

 end_sector= end_byte / o->sector_size;
 for(i= start_byte / o->sector_size; i <= end_sector; i++)
   if(!Sectorbitmap_is_set(o, i, 0))
     return(0);
 return(1);
}
                               

int Sectorbitmap_get_layout(struct SectorbitmaP *o,
                           int *sectors, int *sector_size, int flag)
{
 *sectors= o->sectors;
 *sector_size= o->sector_size;
 return(1);
}


int Sectorbitmap_copy(struct SectorbitmaP *from, struct SectorbitmaP *to,
                      int flag)
{
 int i, run_start, run_value, start_sec, limit_sec, start_aligned;
 int end_complete;

 if(((off_t) from->sectors) * ((off_t) from->sector_size) >
    ((off_t) to->sectors) * ((off_t) to->sector_size))
   return(-1);
 if(from->sector_size == to->sector_size) {
   for(i= 0; i < from->map_size; i++)
     to->map[i]= from->map[i];
   return(1);
 }
 run_start= 0;
 run_value= Sectorbitmap_is_set(from, 0, 0);
 for(i= 1; i <= from->sectors; i++) {
   if(i < from->sectors)
     if(Sectorbitmap_is_set(from, i, 0) == run_value)
 continue;
   start_sec= run_start * from->sector_size / to->sector_size;
   start_aligned=
                (start_sec * to->sector_size == run_start * from->sector_size);
   limit_sec= i * from->sector_size / to->sector_size;
   end_complete= (limit_sec * to->sector_size == i * from->sector_size);
   if(run_value) {
     if(!start_aligned)
       start_sec++;
   } else {
     if(!end_complete)
       limit_sec++;
   }
   if(start_sec < limit_sec)
     Sectorbitmap_set_range(to, start_sec, limit_sec - 1 - start_sec,
                            !!run_value);
   run_value= !run_value;
   run_start= i;
 }
 return(1);
}


int Sectorbitmap_clone(struct SectorbitmaP *from, struct SectorbitmaP **clone,
                      int flag)
{
 int ret;

 ret= Sectorbitmap_new(clone, from->sectors, from->sector_size, 0);
 if(ret <= 0)
   return(ret);
 ret= Sectorbitmap_copy(from, *clone, 0);
 if(ret <= 0)
   Sectorbitmap_destroy(clone, 0);
 return(ret);
}


/* -------------------------- End SectorbitmaP ---------------------------- */

/* ---------------------------- CheckmediajoB ----------------------------- */

int Checkmediajob_new(struct CheckmediajoB **o, int flag)
{
 struct CheckmediajoB *m;

 m= TSOB_FELD(struct CheckmediajoB,1);
 if(m==NULL)
   return(-1);
 *o= m;
 m->use_dev= 0;
 m->min_lba= -1;
 m->max_lba= -1;
 m->min_block_size= 0;
 m->async_chunks= 0;
 m->mode= 0;
 m->start_time= time(NULL);
 m->time_limit= 28800;
 m->item_limit= 100000;
 strcpy(m->abort_file_path, "/var/opt/xorriso/do_abort_check_media");
 m->data_to_path[0]= 0;
 m->data_to_fd= -1;
 m->data_to_offset= 0;
 m->data_to_limit= -1;
 m->patch_lba0= 0;
 m->patch_lba0_msc1= -1;
 m->sector_map_path[0]= 0;
 m->sector_map= NULL;
 m->map_with_volid= 0;
 m->retry= 0;
 m->report_mode= 0;
 strcpy(m->event_severity, "ALL");
 m->slow_threshold_seq= 1.0;
 m->untested_valid= 0;
 return(1);
}


int Checkmediajob_destroy(struct CheckmediajoB **o, int flag)
{  
 if((*o) == NULL)
   return(0);
 if((*o)->data_to_fd != -1)
   close((*o)->data_to_fd);
 Sectorbitmap_destroy(&((*o)->sector_map), 0);
 free((char *) *o);
 *o= NULL;
 return(1);
}


int Checkmediajob_copy(struct CheckmediajoB *from, struct CheckmediajoB *to,
                       int flag)
{  
 to->use_dev= from->use_dev;
 to->min_lba= from->min_lba;
 to->max_lba= from->max_lba;
 to->min_block_size= from->min_block_size;
 to->mode= from->mode;
 to->time_limit= from->time_limit;
 to->item_limit= from->item_limit;
 strcpy(to->abort_file_path, from->abort_file_path);
 strcpy(to->data_to_path, from->data_to_path);
 /* not copied: data_to_fd */
 to->data_to_offset= from->data_to_offset;
 to->data_to_limit= from->data_to_limit;
 to->patch_lba0= from->patch_lba0;
 to->patch_lba0_msc1= from->patch_lba0_msc1;
 strcpy(to->sector_map_path, from->sector_map_path);
 /* not copied: sector_map */
 to->map_with_volid= from->map_with_volid;
 to->retry= from->retry;
 to->report_mode= from->report_mode;
 strcpy(to->event_severity, from->event_severity);
 to->slow_threshold_seq= from->slow_threshold_seq;
 to->untested_valid= from->untested_valid;
 return(1);
}


/* -------------------------- End CheckmediajoB --------------------------- */


int Xorriso_check_media_setup_job(struct XorrisO *xorriso,
                               struct CheckmediajoB *job,
                               char **argv, int old_idx, int end_idx, int flag)
{
 int ret, i, sev;
 double num;
 struct CheckmediajoB *default_job;
 char sev_text[20];

 if(xorriso->check_media_default != NULL)
   Checkmediajob_copy(xorriso->check_media_default, job, 0);
 for(i= old_idx; i < end_idx; i++) {
   if(strncmp(argv[i], "abort_file=", 11) == 0) {
     ret= Sfile_str(job->abort_file_path, argv[i] + 11, 0);
     if(ret <= 0)
       goto ex;
   } else if(strncmp(argv[i], "async_chunks=", 13) == 0) {
     num= Scanf_io_size(argv[i] + 13, 1);
     if(num >= 0 && num <= 1024)
       job->async_chunks= num;
     else
       goto bad_value;
   } else if(strncmp(argv[i], "bad_limit=", 10) == 0) {
     if(strcmp(argv[i] + 10, "good") == 0)
       xorriso->check_media_bad_limit= Xorriso_read_quality_gooD;
     else if(strcmp(argv[i] + 10, "md5_match") == 0)
       xorriso->check_media_bad_limit= Xorriso_read_quality_md5_matcH;
     else if(strcmp(argv[i] + 10, "slow") == 0)
       xorriso->check_media_bad_limit= Xorriso_read_quality_sloW;
     else if(strcmp(argv[i] + 10, "partial") == 0)
       xorriso->check_media_bad_limit= Xorriso_read_quality_partiaL;
     else if(strcmp(argv[i] + 10, "valid") == 0)
       xorriso->check_media_bad_limit= Xorriso_read_quality_valiD;
     else if(strcmp(argv[i] + 10, "untested") == 0)
       xorriso->check_media_bad_limit= Xorriso_read_quality_untesteD;
     else if(strcmp(argv[i] + 10, "invalid") == 0)
       xorriso->check_media_bad_limit= Xorriso_read_quality_invaliD;
     else if(strcmp(argv[i] + 10, "tao_end") == 0)
       xorriso->check_media_bad_limit= Xorriso_read_quality_tao_enD;
     else if(strcmp(argv[i] + 10, "off_track") == 0)
       xorriso->check_media_bad_limit= Xorriso_read_quality_off_tracK;
     else if(strcmp(argv[i] + 10, "md5_mismatch") == 0)
       xorriso->check_media_bad_limit= Xorriso_read_quality_md5_mismatcH;
     else if(strcmp(argv[i] + 10, "unreadable") == 0)
       xorriso->check_media_bad_limit= Xorriso_read_quality_unreadablE;
     else
       goto unknown_value;
   } else if(strncmp(argv[i], "data_to=", 8) == 0) {
     ret= Sfile_str(job->data_to_path, argv[i] + 8, 0);
     if(ret <= 0)
       goto ex;
   } else if(strncmp(argv[i], "chunk_size=", 11) == 0) {
     num= Scanf_io_size(argv[i] + 11, 1);
     if(num >= 2048 || num == 0)
       job->min_block_size= num / 2048;
     else
       goto bad_value;
   } else if(strncmp(argv[i], "event=", 6) == 0) {
     strncpy(sev_text, argv[i] + 6, 19);
     sev_text[19]= 0;
     ret= Xorriso__text_to_sev(sev_text, &sev, 0);
     if(ret <= 0) {
       strcpy(xorriso->info_text, "-check_media event=");
       Text_shellsafe(sev_text, xorriso->info_text, 1);
       strcat(xorriso->info_text, " : Not a known severity name");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       goto ex;
     }
     strcpy(job->event_severity, sev_text);
   } else if(strncmp(argv[i], "map_with_volid=", 15) == 0) {
     if(strcmp(argv[i] + 15, "on") == 0)
       job->map_with_volid= 1;
     else if(strcmp(argv[i] + 15, "off") == 0)
       job->map_with_volid= 0;
     else
       goto unknown_value;
   } else if(strncmp(argv[i], "max_lba=", 8) == 0 ||
      strncmp(argv[i], "min_lba=", 8) == 0) {
     num= -1;
     sscanf(argv[i] + 8, "%lf", &num);
     if(num > 0x7fffffff || num < 0)
       num= -1;
     if(strncmp(argv[i], "max_lba=", 8) == 0)
       job->max_lba= num;
     else
       job->min_lba= num;
   } else if(strncmp(argv[i], "patch_lba0=", 11) == 0) {
     job->patch_lba0_msc1= -1;
     if(strcmp(argv[i] + 11, "on") == 0)
       job->patch_lba0= 1;
     else if(strcmp(argv[i] + 11, "off") == 0)
       job->patch_lba0= 0;
     else if(strcmp(argv[i] + 11, "force") == 0)
       job->patch_lba0= 2;
     else if(argv[i][11] >= '1' && argv[i][11] <= '9') {
       num= -1;
       sscanf(argv[i] + 11, "%lf", &num);
       if(num > 0x7fffffff || num < 0)
         goto bad_value;
       job->patch_lba0_msc1= num;
       job->patch_lba0= (num >= 32) + (strstr(argv[i] + 11, ":force") != NULL);
     } else 
       goto unknown_value;
   } else if(strncmp(argv[i], "report=", 7) == 0) {
     if(strcmp(argv[i] + 7, "blocks") == 0)
       job->report_mode= 0;
     else if(strcmp(argv[i] + 7, "files") == 0)
       job->report_mode= 1;
     else if(strcmp(argv[i] + 7, "blocks_files") == 0)
       job->report_mode= 2;
     else
       goto unknown_value;
   } else if(strcmp(argv[i], "reset=now") == 0) {
     ret= Checkmediajob_new(&default_job, 0);
     if(ret <= 0) {
       sprintf(xorriso->info_text,
               "-check_media: Cannot reset options due to lack of resources");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
       ret= -1; goto ex;
     }
     Checkmediajob_copy(default_job, job, 0);
     Checkmediajob_destroy(&default_job, 0);
     xorriso->check_media_bad_limit= Xorriso_read_quality_invaliD;
   } else if(strncmp(argv[i], "retry=", 6) == 0) {
     if(strcmp(argv[i] + 6, "on") == 0)
       job->retry= 1;
     else if(strcmp(argv[i] + 6, "off") == 0)
       job->retry= -1;
     else if(strcmp(argv[i] + 6, "default") == 0)
       job->retry= 0;
     else
       goto unknown_value;
   } else if(strncmp(argv[i], "sector_map=", 11) == 0) {
     ret= Sfile_str(job->sector_map_path, argv[i] + 11, 0);
     if(ret <= 0)
       goto ex;
   } else if(strncmp(argv[i], "slow_limit=", 11) == 0) {
     sscanf(argv[i] + 11, "%lf", &(job->slow_threshold_seq));
   } else if(strncmp(argv[i], "time_limit=", 11) == 0 ||
             strncmp(argv[i], "item_limit=", 11) == 0 ) {
     num= -1;
     sscanf(argv[i] + 11, "%lf", &num);
     if(num > 0x7fffffff || num < 0)
       num= -1;
     if(strncmp(argv[i], "time_limit=", 11) == 0)
       job->time_limit= num;
     else
       job->item_limit= num;

#ifdef NIX
   } else if(strncmp(argv[i], "untested=", 9) == 0) {
     if(strcmp(argv[i] + 9, "damaged") == 0)
       job->untested_valid= 0;
     if(strcmp(argv[i] + 9, "undamaged") == 0 ||
        strcmp(argv[i] + 9, "ok") == 0)
       job->untested_valid= 1;
     else
       goto unknown_value;
#endif

   } else if(strncmp(argv[i], "use=", 4) == 0) {
     if(strcmp(argv[i] + 4, "outdev") == 0)
       job->use_dev= 1;
     else if(strcmp(argv[i] + 4, "indev") == 0)
       job->use_dev= 0;
     else if(strcmp(argv[i] + 4, "sector_map") == 0)
       job->use_dev= 2;
     else
       goto unknown_value;
   } else if(strncmp(argv[i], "what=", 5) == 0) {
     if(strcmp(argv[i]+5, "tracks") == 0)
       job->mode= 0;
     else if(strcmp(argv[i]+5, "image")== 0)
       job->mode= 1;
     else if(strcmp(argv[i]+5, "disc")== 0)
       job->mode= 2;
     else {
unknown_value:;
       sprintf(xorriso->info_text,
               "-check_media: Unknown value with option %s", argv[i]);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
bad_value:;
       sprintf(xorriso->info_text,
               "-check_media: Unsuitable value with option %s", argv[i]);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
     }
   } else {
     sprintf(xorriso->info_text, "-check_media: Unknown option '%s'", argv[i]);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }
 }
 ret= 1;
ex:;
 return(ret);
}


/* @param report Buffer of at least 10*SfileadrL
   @param flag bit0= only report non-default settings
   @return <=0 error , 1 ok , 2 with bit0: every option is on default setting
*/
int Xorriso_check_media_list_job(struct XorrisO *xorriso,
                                 struct CheckmediajoB *job,
                                 char *report, int flag)
{
 int all, ret;
 char *default_report= NULL, quality_name[80];
 struct CheckmediajoB *dflt= NULL;

 Xorriso_alloc_meM(default_report, char, 161);

 all= !(flag&1);
 report[0]= 0;
 ret= Checkmediajob_new(&dflt, 0);
 if(ret <= 0)
   {ret= -1; goto ex;}
 sprintf(report, "-check_media_defaults");
 if(!all)
   strcat(report, " reset=now");
 if(all || job->use_dev != dflt->use_dev) 
   sprintf(report + strlen(report), " use=%s",
           job->use_dev == 1 ? "outdev" :
           job->use_dev == 2 ? "sector_map" : "indev");
 if(all || job->mode != dflt->mode)
   sprintf(report + strlen(report), " what=%s",
           job->mode == 1 ? "disc" : "tracks");
 if(all || job->min_lba != dflt->min_lba)
   sprintf(report + strlen(report), " min_lba=%d", job->min_lba);
 if(all || job->max_lba != dflt->max_lba)
   sprintf(report + strlen(report), " max_lba=%d", job->max_lba);
 if(all || job->retry != dflt->retry)
   sprintf(report + strlen(report), " retry=%s",
           job->retry == 1 ? "on" : job->retry == -1 ? "off" : "default");
 if(all || job->time_limit != dflt->time_limit)
   sprintf(report + strlen(report), " time_limit=%d", job->time_limit);
 if(all || job->item_limit != dflt->item_limit)
   sprintf(report + strlen(report), " item_limit=%d", job->item_limit);
 if(all || strcmp(job->abort_file_path, dflt->abort_file_path)) {
   strcat(report, " abort_file=");
   Text_shellsafe(job->abort_file_path, report + strlen(report), 0);
 }
 if(strlen(report) > 4 * SfileadrL)
   {ret= 0; goto ex;}
 if(all || strcmp(job->data_to_path, dflt->data_to_path)) {
   strcat(report, " data_to=");
   Text_shellsafe(job->data_to_path, report + strlen(report), 0);
 }
 if(strlen(report) > 4 * SfileadrL)
   {ret= 0; goto ex;}
 if(all || strcmp(job->sector_map_path, dflt->sector_map_path)) {
   strcat(report, " sector_map=");
   Text_shellsafe(job->sector_map_path, report + strlen(report), 0);
 }
 if(all || job->map_with_volid != dflt->map_with_volid)
   sprintf(report + strlen(report), " map_with_volid=%s",
           job->map_with_volid == 1 ? "on" : "off");
 if(all || job->patch_lba0 != dflt->patch_lba0) {
   sprintf(report + strlen(report), " patch_lba0=");
   if(job->patch_lba0 == 0)
     sprintf(report + strlen(report), "off");
   else if(job->patch_lba0_msc1 >= 0)
     sprintf(report + strlen(report), "%d%s",
             job->patch_lba0_msc1, job->patch_lba0 == 2 ? ":force" : "");
   else
     sprintf(report + strlen(report), "%s",
             job->patch_lba0 == 2 ? "force" : "on");
 }
 if(all || job->report_mode != dflt->report_mode)
   sprintf(report + strlen(report), " report=%s",
           job->report_mode == 0 ? "blocks" :
           job->report_mode == 1 ? "files" : "blocks_files");
 if(all || job->slow_threshold_seq != dflt->slow_threshold_seq)
   sprintf(report + strlen(report), " slow_limit=%f", job->slow_threshold_seq);
 if(all || xorriso->check_media_bad_limit != Xorriso_read_quality_invaliD)
   sprintf(report + strlen(report), " bad_limit=%s",
           Spotlist__quality_name(xorriso->check_media_bad_limit, quality_name,
                                  Xorriso_read_quality_invaliD, 0) + 2);
 if(all || job->min_block_size != dflt->min_block_size)
   sprintf(report + strlen(report), " chunk_size=%ds", job->min_block_size);
 if(all || strcmp(job->event_severity, "ALL") != 0)
   sprintf(report + strlen(report), " event=%s", job->event_severity);
 if(strlen(report) > 4 * SfileadrL)
   {ret= 0; goto ex;}
 ret= 1;
ex:;
 strcat(report, " ");
 strcat(report, xorriso->list_delimiter);
 Checkmediajob_destroy(&dflt, 0);
 sprintf(default_report, "-check_media_defaults reset=now %s",
         xorriso->list_delimiter);
 Xorriso_free_meM(default_report);
 if(ret > 0 && strcmp(report, default_report) == 0)
   return(2);
 return(ret);
}


int Xorriso_sectormap_to_spotlist(struct XorrisO *xorriso,
                                  struct CheckmediajoB *job,
                                  struct SpotlisT **spotlist,
                                  int flag)
{
 struct SectorbitmaP *map;
 int ret, i, sectors, sector_size, value, old_value= -1, old_start= -1;

 map= job->sector_map;
 if(map == NULL)
   return(-1);
 ret= Spotlist_new(spotlist, 0); 
 if(ret <= 0)
   {ret= -1; goto ex;}
 Sectorbitmap_get_layout(map, &sectors, &sector_size, 0);
 sector_size/= 2048;
 if(job->max_lba >= 0)
   sectors= (job->max_lba + 1) / sector_size;
 i= 0;
 if(job->min_lba >= 0)
   i= job->min_lba / sector_size;
 for(; i < sectors; i++) {
   value= Sectorbitmap_is_set(map, i, 0);
   if(value == old_value)
 continue;
   if(old_value >= 0) {
     ret= Spotlist_add_item(*spotlist, old_start, i * sector_size - old_start,
                            (old_value ? Xorriso_read_quality_valiD :
                                         Xorriso_read_quality_invaliD), 0);
     if(ret <= 0)
       goto ex;
     if(job->item_limit > 0 &&
        Spotlist_count(*spotlist, 0) + 1 >= job->item_limit) {
       sprintf(xorriso->info_text, "-check_media: Reached item_limit=%d",
               job->item_limit);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
       if(sectors - i > 1) {
         ret= Spotlist_add_item(*spotlist, i * sector_size,
                                (sectors - i - 1) * sector_size,
                                Xorriso_read_quality_untesteD, 0);
         if(ret <= 0)
           goto ex;
       }
       ret= 2; goto ex;
     }
   }
   old_value= value;
   old_start= i * sector_size;
 }
 if(old_value >= 0) {
   ret= Spotlist_add_item(*spotlist, old_start, i * sector_size - old_start,
                          (old_value ? Xorriso_read_quality_valiD :
                                       Xorriso_read_quality_invaliD), 0);
   if(ret <= 0)
     goto ex;
 }
 ret= 1;
ex:;
 if(ret <= 0)
   Spotlist_destroy(spotlist, 0);
 return(ret);
}


/* @param flag bit0= mark untested areas as valid
*/
int Xorriso_spotlist_to_sectormap(struct XorrisO *xorriso,
                                  struct SpotlisT *spotlist,
                                  int read_chunk,
                                  struct SectorbitmaP **map,
                                  int flag)
{
 struct SectorbitmaP *m;
 int map_sectors= -1, map_sector_size= -1, valid;
 int list_sectors, list_blocks, sector_size, sector_blocks;
 int replace_map= 0, count, i, lba, blocks, quality, ret, pass;

 sector_size= Spotlist_sector_size(spotlist, read_chunk, 0);
 sector_blocks= sector_size / 2048;
 if(*map != NULL)
   Sectorbitmap_get_layout(*map, &map_sectors, &map_sector_size, 0);

 count= Spotlist_count(spotlist, 0);
 list_blocks= Spotlist_block_count(spotlist, 0);
 
 /* >>> ??? insist in list_blocks % sector_blocks == 0 */

 list_sectors= list_blocks / sector_blocks;
 if(list_sectors * sector_blocks < list_blocks)
   list_sectors++;
 if(*map != NULL && map_sectors * (map_sector_size / 2048) >= list_blocks &&
    map_sector_size == sector_size)
   m= *map;
 else {
   if(*map != NULL) {
     if(((off_t) (*map)->sectors) * ((off_t) (*map)->sector_size) >
        ((off_t) list_sectors)    * ((off_t) sector_size))
       list_sectors= (((off_t) (*map)->sectors) *
                      ((off_t) (*map)->sector_size)) / ((off_t) sector_size)
                     + 1;
   }
   ret= Sectorbitmap_new(&m, list_sectors, sector_size, 0);
   if(ret <= 0)
     return(-1);
   replace_map= 1;
   if(*map != NULL) {
     ret= Sectorbitmap_copy(*map, m, 0);
     if(ret <= 0) {
       Sectorbitmap_destroy(&m, 0);
       return(0);
     }
   }
 }

 count= Spotlist_count(spotlist, 0);
 /* first set good bits, then eventually override by bad bits */
 for(pass= 0; pass < 2; pass++) {
   for(i= 0; i < count; i++) {
     ret= Spotlist_get_item(spotlist, i, &lba, &blocks, &quality, 0);
     if(ret <= 0)
   continue;
     valid= quality > xorriso->check_media_bad_limit;
     if(quality == Xorriso_read_quality_untesteD && (flag & 1))
       valid= 1;
     else if(pass == 0 && !valid)
   continue;
     else if(pass == 1 && valid)
   continue;
     Sectorbitmap_set_range(m, lba / sector_blocks, blocks / sector_blocks,
                            valid);
   }
 }
 if(replace_map) {
   Sectorbitmap_destroy(map, 0);
   *map= m;
 }
 return(1);
}

 
int Xorriso_open_job_data_to(struct XorrisO *xorriso,
                             struct CheckmediajoB *job, int flag)
{
 if(job->data_to_path[0] == 0)
   return(2);
 job->data_to_fd= open(job->data_to_path, O_RDWR | O_CREAT,
                       S_IRUSR | S_IWUSR);
 if(job->data_to_fd == -1) {
   sprintf(xorriso->info_text, "Cannot open path ");
   Text_shellsafe(job->data_to_path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
   return(0);
 }
 return(1);
}


int Xorriso_update_in_sector_map(struct XorrisO *xorriso,
                                 struct SpotlisT *spotlist, int read_chunk,
                                 struct CheckmediajoB *job, int flag)
{
 int sectors, sector_size, sector_blocks, ret;
 struct SectorbitmaP *map;

 Sectorbitmap_destroy(&(xorriso->in_sector_map), 0);
 if(job->use_dev == 1)
   return(1);
 map= job->sector_map;
 sectors= Spotlist_block_count(spotlist, 0);
 if(sectors <= 0)
   return(0);
 sector_size= Spotlist_sector_size(spotlist, read_chunk, 0);
 sector_blocks= sector_size / 2048;
 if(sector_blocks > 1)
   sectors= sectors / sector_blocks + !!(sectors % sector_blocks);
 ret= Sectorbitmap_new(&(xorriso->in_sector_map), sectors, sector_size, 0);
 if(ret <= 0)
   return(ret);
 if(map != NULL)
   Sectorbitmap_copy(map, xorriso->in_sector_map, 0);
 ret= Xorriso_spotlist_to_sectormap(xorriso, spotlist, read_chunk,
                                    &(xorriso->in_sector_map), 1);
 return(ret);
}
 
