
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2011 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of classes SpotlistiteM, SpotlisT,
   SectorbitmaP, CheckmediajoB which represent media checks and their outcome.
*/


#ifndef Xorriso_pvt_check_includeD
#define Xorriso_pvt_check_includeD yes

struct SpotlisT;          /* List of intervals with different read qualities */
struct CheckmediajoB;     /* Parameters for Xorriso_check_media() */

int Xorriso_check_media_setup_job(struct XorrisO *xorriso,
                              struct CheckmediajoB *job,
                              char **argv, int old_idx, int end_idx, int flag);

int Xorriso_sectormap_to_spotlist(struct XorrisO *xorriso,
                                  struct CheckmediajoB *job,
                                  struct SpotlisT **spotlist,
                                  int flag);

/* @param flag bit0= mark untested areas as valid
*/
int Xorriso_spotlist_to_sectormap(struct XorrisO *xorriso,
                                  struct SpotlisT *spotlist,
                                  int read_chunk,
                                  struct SectorbitmaP **map,
                                  int flag);

/* Opens the -check_media data copy in for reading and writing
*/
int Xorriso_open_job_data_to(struct XorrisO *xorriso,
                             struct CheckmediajoB *job, int flag);

/* @param report Buffer of at least 10*SfileadrL
   @param flag bit0= only report non-default settings
   @return <=0 error , 1 ok , 2 with bit0: every option is on default setting
*/
int Xorriso_check_media_list_job(struct XorrisO *xorriso,
                                 struct CheckmediajoB *job,
                                 char *report, int flag);

int Xorriso_update_in_sector_map(struct XorrisO *xorriso,
                                 struct SpotlisT *spotlist, int read_chunk,
                                 struct CheckmediajoB *job, int flag);


/* Distiniction between valid and invalid sectors */
struct SectorbitmaP {
 int sectors;
 int sector_size;
 unsigned char *map;
 int map_size;
};

int Spotlist_new(struct SpotlisT **o, int flag);

int Spotlist_destroy(struct SpotlisT **o, int flag);

int Spotlist_add_item(struct SpotlisT *o, int start_lba, int blocks,
                      int quality, int flag);

int Spotlist_count(struct SpotlisT *o, int flag);

int Spotlist_block_count(struct SpotlisT *o, int flag);

int Spotlist_sector_size(struct SpotlisT *o, int read_chunk, int flag);

int Spotlist_get_item(struct SpotlisT *o, int idx,
                      int *start_lba, int *blocks, int *quality, int flag);

char *Spotlist__quality_name(int quality, char name[80], int bad_limit,
                             int flag);


#define Xorriso_read_quality_gooD         0x7fffffff
#define Xorriso_read_quality_md5_matcH    0x70000000
#define Xorriso_read_quality_sloW         0x60000000
#define Xorriso_read_quality_partiaL      0x50000000
#define Xorriso_read_quality_valiD        0x40000000
#define Xorriso_read_quality_untesteD     0x3fffffff
#define Xorriso_read_quality_invaliD      0x3ffffffe
#define Xorriso_read_quality_tao_enD      0x28000000
#define Xorriso_read_quality_off_tracK    0x20000000
#define Xorriso_read_quality_md5_mismatcH 0x10000000
#define Xorriso_read_quality_unreadablE   0x00000000


struct CheckmediajoB {
 int use_dev;        /* 0= use indev , 1= use outdev , 2= use sector map*/
 
 int min_lba;        /* if >=0 : begin checking at this address */ 
 int max_lba;        /* if >=0 : read up to this address, else use mode */
 
 int min_block_size; /* granularity desired by user
                     */
 int async_chunks;   /* >= 2 : run MD5 thread, use given number of chunks
                        else : synchronous
                     */
 int mode;           /* 0= track by track
                        1= single sweep over libisoburn medium capacity
                        2= single sweep over libburn medium capacity
                     */
 time_t start_time;  
 int time_limit;     /* Number of seconds after which to abort */
 
 int item_limit;     /* Maximum number of medium check list items as result */
 
 char abort_file_path[SfileadrL];

 char data_to_path[SfileadrL];
 int data_to_fd;
 off_t data_to_offset; /* usually 0 with image copy, negative with file copy */
 off_t data_to_limit;  /* used with file copy */
 int patch_lba0;
 int patch_lba0_msc1;

 char sector_map_path[SfileadrL];
 struct SectorbitmaP *sector_map;
 int map_with_volid;  /* 0=add quick toc to map file,
                         1=read ISO heads for toc
                      */

 int retry;          /* -1= only try full read_chunk, 1=retry with 2k blocks
                         0= retry with CD, full chunk else
                      */

 int report_mode;    /* 0= print MCL items
                        1= print damaged files
                     */

 char event_severity[20]; /* If not "ALL": trigger event of given severity
                             at the end of a check job if bad blocks were
                             discovered.
                           */

 double slow_threshold_seq; /* Time limit in seconds for the decision whether
                               a read operation is considered slow. This does
                               not apply to thr first read of an interval.
                             */

 int untested_valid; /* 1= mark untested data blocks as valid when calling
                           Xorriso_spotlist_to_sectormap()
                      */
};

int Checkmediajob_new(struct CheckmediajoB **o, int flag);

int Checkmediajob_destroy(struct CheckmediajoB **o, int flag);

int Checkmediajob_copy(struct CheckmediajoB *from, struct CheckmediajoB *to,
                       int flag);

int Sectorbitmap_new(struct SectorbitmaP **o, int sectors, int sector_size,
                     int flag);
int Sectorbitmap_destroy(struct SectorbitmaP **o, int flag);
int Sectorbitmap_from_file(struct SectorbitmaP **o, char *path, char *msg,
                           int *os_errno, int flag);
int Sectorbitmap_to_file(struct SectorbitmaP *o, char *path, char *info,
                         char *msg, int *os_errno, int flag);
int Sectorbitmap_set(struct SectorbitmaP *o, int sector, int flag);
int Sectorbitmap_set_range(struct SectorbitmaP *o,
                           int start_sector, int sectors, int flag);
int Sectorbitmap_is_set(struct SectorbitmaP *o, int sector, int flag);
int Sectorbitmap_bytes_are_set(struct SectorbitmaP *o,
                               off_t start_byte, off_t end_byte, int flag);

int Sectorbitmap_get_layout(struct SectorbitmaP *o,
                           int *sectors, int *sector_size, int flag);

int Sectorbitmap_copy(struct SectorbitmaP *from, struct SectorbitmaP *to,
                      int flag);

int Sectorbitmap_clone(struct SectorbitmaP *from, struct SectorbitmaP **clone,
                      int flag);

#endif /* ! Xorriso_pvt_check_includeD */

