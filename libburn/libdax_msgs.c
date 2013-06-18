
/* libdax_msgs
   Message handling facility of libdax.
   Copyright (C) 2006 - 2010 Thomas Schmitt <scdbackup@gmx.net>,
   provided under GPL version 2 or later.
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>

/* Only this single source module is entitled to do this */ 
#define LIBDAX_MSGS_H_INTERNAL 1

/* All participants in the messaging system must do this */
#include "libdax_msgs.h"


/* ----------------------------- libdax_msgs_item ------------------------- */


static int libdax_msgs_item_new(struct libdax_msgs_item **item,
                         struct libdax_msgs_item *link, int flag)
{
 int ret;
 struct libdax_msgs_item *o;
 struct timeval tv;
 struct timezone tz;

 (*item)= o= 
        (struct libdax_msgs_item *) calloc(1, sizeof(struct libdax_msgs_item));
 if(o==NULL)
   return(-1);
 o->timestamp= 0.0;
 ret= gettimeofday(&tv,&tz);
 if(ret==0)
   o->timestamp= tv.tv_sec+0.000001*tv.tv_usec;
 o->process_id= getpid();
 o->origin= -1;
 o->severity= LIBDAX_MSGS_SEV_ALL;
 o->priority= LIBDAX_MSGS_PRIO_ZERO;
 o->error_code= 0;
 o->msg_text= NULL;
 o->os_errno= 0;
 o->prev= link;
 o->next= NULL;
 if(link!=NULL) {
   if(link->next!=NULL) {
     link->next->prev= o;
     o->next= link->next;
   }
   link->next= o;
 }
 return(1);
}


/** Detaches item from its queue and eventually readjusts start, end pointers
    of the queue */
int libdax_msgs_item_unlink(struct libdax_msgs_item *o,
                            struct libdax_msgs_item **chain_start,
                            struct libdax_msgs_item **chain_end, int flag)
{
 if(o->prev!=NULL)
   o->prev->next= o->next;
 if(o->next!=NULL)
   o->next->prev= o->prev;
 if(chain_start!=NULL)
   if(*chain_start == o)
     *chain_start= o->next;
 if(chain_end!=NULL)
   if(*chain_end == o)
     *chain_end= o->prev;
 o->next= o->prev= NULL;
 return(1);
}


int libdax_msgs_item_destroy(struct libdax_msgs_item **item,
                             int flag)
{
 struct libdax_msgs_item *o;

 o= *item;
 if(o==NULL)
   return(0);
 libdax_msgs_item_unlink(o,NULL,NULL,0); 
 if(o->msg_text!=NULL)
   free((char *) o->msg_text);
 free((char *) o);
 *item= NULL;
 return(1);
}


int libdax_msgs_item_get_msg(struct libdax_msgs_item *item,
                             int *error_code, char **msg_text, int *os_errno,
                             int flag)
{
 *error_code= item->error_code;
 *msg_text= item->msg_text;
 *os_errno= item->os_errno;
 return(1);
}


int libdax_msgs_item_get_origin(struct libdax_msgs_item *item,
                   double *timestamp, pid_t *process_id, int *origin,
                   int flag)
{
 *timestamp= item->timestamp;
 *process_id= item->process_id;
 *origin= item->origin;
 return(1);
}


int libdax_msgs_item_get_rank(struct libdax_msgs_item *item,
                              int *severity, int *priority, int flag)
{
 *severity= item->severity;
 *priority= item->priority;
 return(1);
}


/* ------------------------------- libdax_msgs ---------------------------- */


int libdax_msgs_new(struct libdax_msgs **m, int flag)
{
 struct libdax_msgs *o;

 (*m)= o= (struct libdax_msgs *) calloc(1, sizeof(struct libdax_msgs));
 if(o==NULL)
   return(-1);
 o->refcount= 1;
 o->oldest= NULL;
 o->youngest= NULL;
 o->count= 0;
 o->queue_severity= LIBDAX_MSGS_SEV_ALL;
 o->print_severity= LIBDAX_MSGS_SEV_NEVER;
 strcpy(o->print_id,"libdax: ");

#ifndef LIBDAX_MSGS_SINGLE_THREADED
 pthread_mutex_init(&(o->lock_mutex),NULL);
#endif

 return(1);
}


static int libdax_msgs_lock(struct libdax_msgs *m, int flag)
{

#ifndef LIBDAX_MSGS_SINGLE_THREADED
 int ret;

 ret= pthread_mutex_lock(&(m->lock_mutex));
 if(ret!=0)
   return(0);
#endif

 return(1);
}


static int libdax_msgs_unlock(struct libdax_msgs *m, int flag)
{

#ifndef LIBDAX_MSGS_SINGLE_THREADED
 int ret;

 ret= pthread_mutex_unlock(&(m->lock_mutex));
 if(ret!=0)
   return(0);
#endif

 return(1);
}


int libdax_msgs_destroy(struct libdax_msgs **m, int flag)
{
 struct libdax_msgs *o;
 struct libdax_msgs_item *item, *next_item;

 o= *m;
 if(o==NULL)
   return(0);
 if(o->refcount > 1) {
   if(libdax_msgs_lock(*m,0)<=0)
     return(-1);
   o->refcount--;
   libdax_msgs_unlock(*m,0);
   *m= NULL;
   return(1);
 }

#ifndef LIBDAX_MSGS_SINGLE_THREADED
 if(pthread_mutex_destroy(&(o->lock_mutex))!=0) {
   pthread_mutex_unlock(&(o->lock_mutex));
   pthread_mutex_destroy(&(o->lock_mutex));
 }
#endif

 for(item= o->oldest; item!=NULL; item= next_item) {
   next_item= item->next;
   libdax_msgs_item_destroy(&item,0);
 }
 free((char *) o);
 *m= NULL;
 return(1);
}


int libdax_msgs_refer(struct libdax_msgs **pt, struct libdax_msgs *m, int flag)
{
 if(libdax_msgs_lock(m,0)<=0)
   return(0);
 m->refcount++;
 *pt= m;
 libdax_msgs_unlock(m,0);
 return(1);
}


int libdax_msgs_set_severities(struct libdax_msgs *m, int queue_severity,
                               int print_severity, char *print_id, int flag)
{
 if(libdax_msgs_lock(m,0)<=0)
   return(0);
 m->queue_severity= queue_severity;
 m->print_severity= print_severity;
 strncpy(m->print_id,print_id,80);
 m->print_id[80]= 0;
 libdax_msgs_unlock(m,0);
 return(1);
}


int libdax_msgs__text_to_sev(char *severity_name, int *severity,
                             int flag)
{
 if(strncmp(severity_name,"NEVER",5)==0)
   *severity= LIBDAX_MSGS_SEV_NEVER;
 else if(strncmp(severity_name,"ABORT",5)==0)
   *severity= LIBDAX_MSGS_SEV_ABORT;
 else if(strncmp(severity_name,"FATAL",5)==0)
   *severity= LIBDAX_MSGS_SEV_FATAL;
 else if(strncmp(severity_name,"FAILURE",7)==0)
   *severity= LIBDAX_MSGS_SEV_FAILURE;
 else if(strncmp(severity_name,"MISHAP",6)==0)
   *severity= LIBDAX_MSGS_SEV_MISHAP;
 else if(strncmp(severity_name,"SORRY",5)==0)
   *severity= LIBDAX_MSGS_SEV_SORRY;
 else if(strncmp(severity_name,"WARNING",7)==0)
   *severity= LIBDAX_MSGS_SEV_WARNING;
 else if(strncmp(severity_name,"HINT",4)==0)
   *severity= LIBDAX_MSGS_SEV_HINT;
 else if(strncmp(severity_name,"NOTE",4)==0)
   *severity= LIBDAX_MSGS_SEV_NOTE;
 else if(strncmp(severity_name,"UPDATE",6)==0)
   *severity= LIBDAX_MSGS_SEV_UPDATE;
 else if(strncmp(severity_name,"DEBUG",5)==0)
   *severity= LIBDAX_MSGS_SEV_DEBUG;
 else if(strncmp(severity_name,"ERRFILE",7)==0)
   *severity= LIBDAX_MSGS_SEV_ERRFILE;
 else if(strncmp(severity_name,"ALL",3)==0)
   *severity= LIBDAX_MSGS_SEV_ALL;
 else {
   *severity= LIBDAX_MSGS_SEV_ALL;
   return(0);
 }
 return(1);
}


int libdax_msgs__sev_to_text(int severity, char **severity_name,
                             int flag)
{
 if(flag&1) {
   *severity_name= "ALL ERRFILE DEBUG UPDATE NOTE HINT WARNING SORRY MISHAP FAILURE FATAL ABORT NEVER";
   return(1);
 }
 *severity_name= "";
 if(severity>=LIBDAX_MSGS_SEV_NEVER)
   *severity_name= "NEVER";
 else if(severity>=LIBDAX_MSGS_SEV_ABORT)
   *severity_name= "ABORT";
 else if(severity>=LIBDAX_MSGS_SEV_FATAL)
   *severity_name= "FATAL";
 else if(severity>=LIBDAX_MSGS_SEV_FAILURE)
   *severity_name= "FAILURE";
 else if(severity>=LIBDAX_MSGS_SEV_MISHAP)
   *severity_name= "MISHAP";
 else if(severity>=LIBDAX_MSGS_SEV_SORRY)
   *severity_name= "SORRY";
 else if(severity>=LIBDAX_MSGS_SEV_WARNING)
   *severity_name= "WARNING";
 else if(severity>=LIBDAX_MSGS_SEV_HINT)
   *severity_name= "HINT";
 else if(severity>=LIBDAX_MSGS_SEV_NOTE)
   *severity_name= "NOTE";
 else if(severity>=LIBDAX_MSGS_SEV_UPDATE)
   *severity_name= "UPDATE";
 else if(severity>=LIBDAX_MSGS_SEV_DEBUG)
   *severity_name= "DEBUG";
 else if(severity>=LIBDAX_MSGS_SEV_ERRFILE)
   *severity_name= "ERRFILE";
 else if(severity>=LIBDAX_MSGS_SEV_ALL)
   *severity_name= "ALL";
 else {
   *severity_name= "";
   return(0);
 }
 return(1);
}


int libdax_msgs_submit(struct libdax_msgs *m, int origin, int error_code,
                       int severity, int priority, char *msg_text,
                       int os_errno, int flag)
{
 int ret;
 char *textpt,*sev_name,sev_text[81];
 struct libdax_msgs_item *item= NULL;

 if(severity >= m->print_severity) {
   if(msg_text==NULL)
     textpt= "";
   else
     textpt= msg_text;
   sev_text[0]= 0;
   ret= libdax_msgs__sev_to_text(severity,&sev_name,0);
   if(ret>0)
     sprintf(sev_text,"%s : ",sev_name);

   fprintf(stderr,"%s%s%s\n",m->print_id,sev_text,textpt);
   if(os_errno!=0) {
     ret= libdax_msgs_lock(m,0);
     if(ret<=0)
       return(-1);
     fprintf(stderr,"%s( Most recent system error: %d  '%s' )\n",
                    m->print_id,os_errno,strerror(os_errno));
     libdax_msgs_unlock(m,0);
   }

 }
 if(severity < m->queue_severity)
   return(0);

 ret= libdax_msgs_lock(m,0);
 if(ret<=0)
   return(-1);
 ret= libdax_msgs_item_new(&item,m->youngest,0);
 if(ret<=0)
   goto failed;
 item->origin= origin;
 item->error_code= error_code;
 item->severity= severity;
 item->priority= priority;
 if(msg_text!=NULL) {
   item->msg_text= calloc(1, strlen(msg_text)+1);
   if(item->msg_text==NULL)
     goto failed;
   strcpy(item->msg_text,msg_text);
 }
 item->os_errno= os_errno;
 if(m->oldest==NULL)
   m->oldest= item;
 m->youngest= item;
 m->count++;
 libdax_msgs_unlock(m,0);

/*
fprintf(stderr,"libdax_experimental: message submitted to queue (now %d)\n",
                m->count);
*/

 return(1);
failed:;
 libdax_msgs_item_destroy(&item,0);
 libdax_msgs_unlock(m,0);
 return(-1);
}


int libdax_msgs_obtain(struct libdax_msgs *m, struct libdax_msgs_item **item,
                       int severity, int priority, int flag)
{
 int ret;
 struct libdax_msgs_item *im, *next_im= NULL;

 *item= NULL;
 ret= libdax_msgs_lock(m,0);
 if(ret<=0)
   return(-1);
 for(im= m->oldest; im!=NULL; im= next_im) {
   for(; im!=NULL; im= next_im) {
     next_im= im->next;
     if(im->severity>=severity)
   break;
     libdax_msgs_item_unlink(im,&(m->oldest),&(m->youngest),0);
     libdax_msgs_item_destroy(&im,0); /* severity too low: delete */
   }
   if(im==NULL)
 break;
   if(im->priority>=priority)
 break;
 }
 if(im==NULL)
   {ret= 0; goto ex;}
 libdax_msgs_item_unlink(im,&(m->oldest),&(m->youngest),0);
 *item= im;
 ret= 1;
ex:;
 libdax_msgs_unlock(m,0);
 return(ret);
}


int libdax_msgs_destroy_item(struct libdax_msgs *m,
                             struct libdax_msgs_item **item, int flag)
{
 int ret;

 ret= libdax_msgs_lock(m,0);
 if(ret<=0)
   return(-1);
 ret= libdax_msgs_item_destroy(item,0);
 libdax_msgs_unlock(m,0);
 return(ret);
}

