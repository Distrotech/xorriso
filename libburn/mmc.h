/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

/* Copyright (c) 2004 - 2006 Derek Foreman, Ben Jansens
   Copyright (c) 2006 - 2010 Thomas Schmitt <scdbackup@gmx.net>
   Provided under GPL version 2 or later.
*/


#ifndef __MMC
#define __MMC

struct burn_drive;
struct burn_write_opts;
struct command;
struct buffer;
struct cue_sheet;

/* MMC commands */

void mmc_read(struct burn_drive *);

/* ts A61009 : removed redundant parameter d in favor of o->drive */
/* void mmc_close_session(struct burn_drive *, struct burn_write_opts *); */
/* void mmc_close_disc(struct burn_drive *, struct burn_write_opts *); */
void mmc_close_session(struct burn_write_opts *o);
void mmc_close_disc(struct burn_write_opts *o);

void mmc_close(struct burn_drive *, int session, int track);
void mmc_get_event(struct burn_drive *);
int mmc_write(struct burn_drive *, int start, struct buffer *buf);
void mmc_write_12(struct burn_drive *d, int start, struct buffer *buf);
void mmc_sync_cache(struct burn_drive *);
void mmc_load(struct burn_drive *);
void mmc_eject(struct burn_drive *);
void mmc_erase(struct burn_drive *, int);
void mmc_read_toc(struct burn_drive *);
void mmc_read_disc_info(struct burn_drive *);
void mmc_read_atip(struct burn_drive *);
int mmc_read_cd(struct burn_drive *d, int start, int len,
                int sec_type, int main_ch,
                const struct burn_read_opts *o, struct buffer *buf, int flag);
void mmc_set_speed(struct burn_drive *, int, int);
void mmc_read_lead_in(struct burn_drive *, struct buffer *);
void mmc_perform_opc(struct burn_drive *);
void mmc_get_configuration(struct burn_drive *);

/* ts A61110 : added parameters trackno, lba, nwa. Redefined return value.
   @return 1=nwa is valid , 0=nwa is not valid , -1=error */
int mmc_get_nwa(struct burn_drive *d, int trackno, int *lba, int *nwa);

/* ts B11228 : changed from void to int */ 
int mmc_send_cue_sheet(struct burn_drive *, struct cue_sheet *);

/* ts A61023 : get size and free space of drive buffer */
int mmc_read_buffer_capacity(struct burn_drive *d);

/* ts A61021 : the mmc specific part of sg.c:enumerate_common()
*/
int mmc_setup_drive(struct burn_drive *d);

/* ts A61219 : learned much from dvd+rw-tools-7.0: plus_rw_format()
               and mmc5r03c.pdf, 6.5 FORMAT UNIT */
int mmc_format_unit(struct burn_drive *d, off_t size, int flag);

/* ts A61225 : obtain write speed descriptors via ACh GET PERFORMANCE */
int mmc_get_write_performance(struct burn_drive *d);


/* ts A61229 : outsourced from spc_select_write_params() */
/* Note: Page data is not zeroed here to allow preset defaults. Thus
           memset(pd, 0, 2 + d->mdata->write_page_length); 
         is the eventual duty of the caller.
*/
int mmc_compose_mode_page_5(struct burn_drive *d,
                            struct burn_session *s, int tno,
                            const struct burn_write_opts *o,
                            unsigned char *pd);

/* ts A70201 */
int mmc_four_char_to_int(unsigned char *data);

/* ts A70201 :
   Common track info fetcher for mmc_get_nwa() and mmc_fake_toc()
*/
int mmc_read_track_info(struct burn_drive *d, int trackno, struct buffer *buf,
                        int alloc_len);

/* ts A70812 : return 0 = ok , return BE_CANCELLED = error occured */
int mmc_read_10(struct burn_drive *d, int start, int amount,
                struct buffer *buf);

/* ts A81210 : Determine the upper limit of readable data size */
int mmc_read_capacity(struct burn_drive *d);

/* ts A61201 */
char *mmc_obtain_profile_name(int profile_number);


/* mmc5r03c.pdf 4.3.4.4.1 d) "The maximum number of RZones is 2 302." */
#define BURN_MMC_FAKE_TOC_MAX_SIZE 2302


/* ts A90903 */
/* MMC backend of API call burn_get_media_product_id()
*/
int mmc_get_media_product_id(struct burn_drive *d,
        char **product_id, char **media_code1, char **media_code2,
	char **book_type, int flag);


/* ts A60910 (estimated) */
int mmc_function_spy(struct burn_drive *d, char * text);

/* ts A91118 */
int mmc_start_if_needed(struct burn_drive *d, int flag);

/* ts B00924 */
int mmc_get_bd_spare_info(struct burn_drive *d,
                                int *alloc_blocks, int *free_blocks, int flag);

/* ts B10801 */
int mmc_get_phys_format_info(struct burn_drive *d, int *disk_category,
                        char **book_name, int *part_version, int *num_layers,
                        int *num_blocks, int flag);

/* ts B11201 */
int mmc_get_leadin_text(struct burn_drive *d,
                        unsigned char **text_packs, int *num_packs, int flag);


#ifdef Libburn_develop_quality_scaN
/* B21108 ts */
int mmc_nec_optiarc_f3(struct burn_drive *d, int sub_op,
                       int start_lba, int rate_period,
                       int *eba, int *error_rate1, int *error_rate2);
#endif

#endif /*__MMC*/
