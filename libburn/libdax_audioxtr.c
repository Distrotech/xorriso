
/* libdax_audioxtr 
   Audio track data extraction facility of libdax and libburn.
   Copyright (C) 2006 Thomas Schmitt <scdbackup@gmx.net>, provided under GPLv2+
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>


#include "libdax_msgs.h"
extern struct libdax_msgs *libdax_messenger;


/* Only this single source module is entitled to do this */
#define LIBDAX_AUDIOXTR_H_INTERNAL 1

/* All clients of the extraction facility must do this or include libburn.h */
#define LIBDAX_AUDIOXTR_H_PUBLIC 1
#include "libdax_audioxtr.h"


int libdax_audioxtr_new(struct libdax_audioxtr **xtr, char *path, int flag)
{
 int ret= -1;
 struct libdax_audioxtr *o;

 o= *xtr= (struct libdax_audioxtr *) calloc(1, sizeof(struct libdax_audioxtr));
 if(o==NULL)
   return(-1);
 strncpy(o->path,path,LIBDAX_AUDIOXTR_STRLEN-1);
 o->path[LIBDAX_AUDIOXTR_STRLEN-1]= 0;
 o->fd= -1;
 strcpy(o->fmt,"unidentified");
 o->fmt_info[0]= 0;
 o->data_size= 0;
 o->extract_count= 0;

 o->num_channels= 0;
 o->sample_rate= 0;
 o->bits_per_sample= 0;
 o->msb_first= 0;

 o->wav_subchunk2_size= 0;

 o->au_data_location= 0;
 o->au_data_size= 0xffffffff;

 ret= libdax_audioxtr_open(o,0);
 if(ret<=0)
   {ret= -2*(ret<0); goto failure;}

 return(1);
failure:
 libdax_audioxtr_destroy(xtr,0);
 return(ret);
}


int libdax_audioxtr_destroy(struct libdax_audioxtr **xtr, int flag)
{
 struct libdax_audioxtr *o;

 o= *xtr;
 if(o==NULL)
   return(0);
 if(o->fd>=0 && strcmp(o->path,"-")!=0)
   close(o->fd);
 free((char *) o);
 *xtr= NULL;
 return(1);
}


static int libdax_audioxtr_open(struct libdax_audioxtr *o, int flag)
{
 int ret;
 char msg[LIBDAX_AUDIOXTR_STRLEN+80];
 
 if(strcmp(o->path,"-")==0)
   o->fd= 0;
 else
   o->fd= open(o->path, O_RDONLY);
 if(o->fd<0) {
   sprintf(msg,"Cannot open audio source file : %s",o->path);
   libdax_msgs_submit(libdax_messenger,-1,0x00020200,
                      LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
                      msg, errno, 0);
   return(-1);
 }
 ret= libdax_audioxtr_identify(o,0);
 if(ret<=0) {
   sprintf(msg,"Audio source file has unsuitable format : %s",o->path);
   libdax_msgs_submit(libdax_messenger,-1,0x00020201,
                      LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
                      msg, 0, 0);
   return(0);
 }
 ret= libdax_audioxtr_init_reading(o,0);
 if(ret<=0) {
   sprintf(msg,"Failed to prepare reading of audio data : %s",o->path);
   libdax_msgs_submit(libdax_messenger,-1,0x00020202,
                      LIBDAX_MSGS_SEV_SORRY, LIBDAX_MSGS_PRIO_HIGH,
                      msg, 0, 0);
   return(0);
 }
 return(1);
}


static int libdax_audioxtr_identify_wav(struct libdax_audioxtr *o, int flag)
{
 int ret;
 char buf[45];

 /* check wether this is a MS WAVE file .wav */
 /* info used: http://ccrma.stanford.edu/courses/422/projects/WaveFormat/ */

 if(o->fd!=0) {
   ret= lseek(o->fd,0,SEEK_SET);
   if(ret==-1)
     return(0);
 }
 ret= read(o->fd, buf, 44);
 if(ret<44)
   return(0);
 buf[44]= 0; /* as stopper for any string operations */

 if(strncmp(buf,"RIFF",4)!=0)                                     /* ChunkID */
   return(0);
 if(strncmp(buf+8,"WAVE",4)!=0)                                    /* Format */ 
   return(0);
 if(strncmp(buf+12,"fmt ",4)!=0)                              /* Subchunk1ID */
   return(0);
 if(buf[16]!=16 || buf[17]!=0 || buf[18]!=0 || buf[19]!=0)  /* Subchunk1Size */
   return(0);
 if(buf[20]!=1 || buf[21]!=0)  /* AudioFormat must be 1 (Linear quantization) */
   return(0);

 strcpy(o->fmt,".wav");
 o->msb_first= 0;
 o->num_channels=  libdax_audioxtr_to_int(o,(unsigned char *) buf+22,2,0);
 o->sample_rate= libdax_audioxtr_to_int(o,(unsigned char *) buf+24,4,0);
 o->bits_per_sample= libdax_audioxtr_to_int(o,(unsigned char *)buf+34,2,0);
 sprintf(o->fmt_info,
         ".wav , num_channels=%d , sample_rate=%d , bits_per_sample=%d",
         o->num_channels,o->sample_rate,o->bits_per_sample);
 o->wav_subchunk2_size= libdax_audioxtr_to_int(o,(unsigned char *)buf+40,4,0);
 o->data_size= o->wav_subchunk2_size;
 return(1);
}


static int libdax_audioxtr_identify_au(struct libdax_audioxtr *o, int flag)
{
 int ret,encoding;
 char buf[24];

 /* Check wether this is a Sun Audio, .au file */
 /* info used: http://ccrma.stanford.edu/courses/422/projects/WaveFormat/ */

 if(o->fd!=0) {
   ret= lseek(o->fd,0,SEEK_SET);
   if(ret==-1)
     return(0);
 }
 ret= read(o->fd, buf, 24);
 if(ret<24)
   return(0);

 if(strncmp(buf,".snd",4)!=0)
   return(0);
 strcpy(o->fmt,".au");
 o->msb_first= 1;
 o->au_data_location= libdax_audioxtr_to_int(o,(unsigned char *)buf+4,4,1);
 o->au_data_size= libdax_audioxtr_to_int(o,(unsigned char *)buf+8,4,1);
 encoding= libdax_audioxtr_to_int(o,(unsigned char *)buf+12,4,1);
 if(encoding==2)
   o->bits_per_sample= 8;
 else if(encoding==3)
   o->bits_per_sample= 16;
 else if(encoding==4)
   o->bits_per_sample= 24;
 else if(encoding==5)
   o->bits_per_sample= 32;
 else
   o->bits_per_sample= -encoding;
 o->sample_rate= libdax_audioxtr_to_int(o,(unsigned char *)buf+16,4,1);
 o->num_channels= libdax_audioxtr_to_int(o,(unsigned char *)buf+20,4,1);
 if(o->au_data_size!=0xffffffff)
   o->data_size= o->au_data_size;
 else
   o->data_size= 0;
 sprintf(o->fmt_info,
         ".au , num_channels=%d , sample_rate=%d , bits_per_sample=%d",
         o->num_channels,o->sample_rate,o->bits_per_sample);

 /* <<< for testing only */; 
 return(1);

 return(o->bits_per_sample>0); /* Audio format must be linear PCM */
}


static int libdax_audioxtr_identify(struct libdax_audioxtr *o, int flag)
{
 int ret;

 ret= libdax_audioxtr_identify_wav(o, 0);
 if(ret!=0)
   return(ret);
 if(o->fd==0) /* cannot rewind stdin */
   return(0);
 ret= libdax_audioxtr_identify_au(o, 0);
 if(ret!=0)
   return(ret);
 return(0);
}


/* @param flag bit0=msb_first */
static unsigned libdax_audioxtr_to_int(struct libdax_audioxtr *o,
                                      unsigned char *bytes, int len, int flag)
{
 unsigned int ret= 0;
 int i;

 if(flag&1)
   for(i= 0; i<len; i++)
     ret= ret*256+bytes[i];
 else
   for(i= len-1; i>=0; i--)
     ret= ret*256+bytes[i];
 return(ret);
}


static int libdax_audioxtr_init_reading(struct libdax_audioxtr *o, int flag)
{
 int ret;


 /* currently this only works for MS WAVE files .wav and Sun .au*/;
 if(o->fd==0) /* stdin: hope no read came after libdax_audioxtr_identify() */
   return(1);

 o->extract_count= 0;
 if(strcmp(o->fmt,".wav")==0)
   ret= lseek(o->fd,44,SEEK_SET);
 else if(strcmp(o->fmt,".au")==0)
   ret= lseek(o->fd,o->au_data_location,SEEK_SET);
 else
   ret= -1;
 if(ret==-1)
   return(0);

 return(1);
}


int libdax_audioxtr_get_id(struct libdax_audioxtr *o,
                     char **fmt, char **fmt_info,
                     int *num_channels, int *sample_rate, int *bits_per_sample,
                     int *msb_first, int flag)
{
 *fmt= o->fmt;
 *fmt_info= o->fmt_info;
 *num_channels= o->num_channels;
 *sample_rate= o->sample_rate;
 *bits_per_sample= o->bits_per_sample;
 *msb_first= o->msb_first;
 return(1);
}


int libdax_audioxtr_get_size(struct libdax_audioxtr *o, off_t *size, int flag)
{
 *size= o->data_size;
 return(1);
}


int libdax_audioxtr_read(struct libdax_audioxtr *o,
                         char buffer[], int buffer_size, int flag)
{
 int ret;

 if(buffer_size<=0 || o->fd<0)
   return(-2);
 if(o->data_size>0 && !(flag&1))
   if(buffer_size > o->data_size - o->extract_count)
     buffer_size= o->data_size - o->extract_count;
 if(buffer_size<=0)
   return(0);
 ret= read(o->fd,buffer,buffer_size);
 if(ret>0)
   o->extract_count+= ret;
 return(ret);
}


int libdax_audioxtr_detach_fd(struct libdax_audioxtr *o, int *fd, int flag)
{
 if(o->fd<0)
   return(-1);
 if(strcmp(o->fmt,".wav")!=0 && strcmp(o->fmt,".au")!=0)
   return(0);
 if(flag&1) {
   *fd= o->fd;
 } else {
   *fd= dup(o->fd);
   if(*fd>=0 && strcmp(o->path,"-")!=0)
     close(o->fd);
 }
 if(*fd>=0) {
   o->fd= -1;
   return(1);
 }
 return(-1);
}

