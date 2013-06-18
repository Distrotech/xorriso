
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the implementations of classes:

   - SplitparT which represents byte intervals of data files.

   - DirseQ which crawls along a directory's content list.

   - ExclusionS which manages the list of excluded file paths and
     leaf patterns.
     Because of its structural identity it is also used for disk address
     oriented hiding at insert time as of mkisofs.

   - Xorriso_lsT which provides a generic double-linked list.

   - LinkiteM, PermiteM which temporarily record relations and states.

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
#include <utime.h>
#include <dirent.h>
#include <errno.h>


#include "xorriso.h"
#include "xorriso_private.h"


/* ---------------------------- SplitparT ------------------------- */


struct SplitparT {
 char *name;
 int partno;
 int total_parts;
 off_t offset;
 off_t bytes;
 off_t total_bytes;
};

static char Splitpart_wordS[][16]= {"part_", "_of_", "_at_", "_with_", "_of_"};


int Splitparts_new(struct SplitparT **o, int count, int flag)
{
 int i;

 (*o)= TSOB_FELD(struct SplitparT, count);
 if((*o)==NULL)
   return(-1);
 for(i= 0; i<count; i++) {
   (*o)[i].name= NULL;
   (*o)[i].partno= 0;
   (*o)[i].total_parts= 0;
   (*o)[i].offset= 0;
   (*o)[i].bytes= 0;
   (*o)[i].total_bytes= 0;
 }
 return(1);
}


int Splitparts_destroy(struct SplitparT **o, int count, int flag)
{
 int i;

 if((*o)==NULL)
   return(0);
 for(i= 0; i<count; i++) {
   if((*o)[i].name!=NULL)
     free((*o)[i].name);
 }
 free(*o);
 *o= NULL;
 return(1);
}


int Splitparts_set(struct SplitparT *o, int idx,
                   char *name, int partno, int total_parts,
                   off_t offset, off_t bytes, off_t total_bytes, int flag)
{
 if(o[idx].name!=NULL)
   free(o[idx].name);
 o[idx].name= strdup(name);
 if(o[idx].name==NULL)
   return(-1);
 o[idx].partno= partno;
 o[idx].total_parts= total_parts;
 o[idx].offset= offset;
 o[idx].bytes= bytes;
 o[idx].total_bytes= total_bytes;
 return(1);
}


int Splitparts_get(struct SplitparT *o, int idx, char **name, int *partno,
                   int *total_parts, off_t *offset, off_t *bytes,
                   off_t *total_bytes, int flag)
{
 *name= o[idx].name;
 *partno= o[idx].partno;
 *total_parts= o[idx].total_parts;
 *offset= o[idx].offset;
 *bytes= o[idx].bytes;
 *total_bytes= o[idx].total_bytes;
 return(1);
}


int Splitpart__read_next_num(char *base_pt, char **next_pt, off_t *num,
                             int flag)
{
 char *cpt, *ept, scale[4];
 double sfak;

 *num= 0;
 for(cpt= base_pt; *cpt!=0 && !isdigit(*cpt); cpt++);
 if(*cpt==0)
   return(0);
 for(ept= cpt; *ept!=0 && isdigit(*ept); ept++)
   *num= (*num)*10+(*ept)-'0';
 scale[0]= '1';
 scale[1]= *ept;
 scale[2]= 0;
 sfak= Scanf_io_size(scale, 0);
 *num *= (off_t) sfak;
 if(sfak > 1.0)
   ept++;
 *next_pt= ept;
 return(1);
}


int Splitpart__parse(char *name, int *partno, int *total_parts,
                    off_t *offset, off_t *bytes, off_t *total_bytes, int flag)

{
 int ret; 
 off_t num;
 char *cpt, *ept;

 cpt= name;
 if(strncmp(cpt, Splitpart_wordS[0], strlen(Splitpart_wordS[0])) != 0)
   return(0);
 ret= Splitpart__read_next_num(cpt, &ept, &num, 0);
 if(ret<=0)
   return(ret);
 *partno= num;
 cpt= ept;
 if(strncmp(cpt, Splitpart_wordS[1], strlen(Splitpart_wordS[1])) != 0)
   return(0);
 ret= Splitpart__read_next_num(cpt, &ept, &num, 0);
 if(ret<=0)
   return(ret);
 *total_parts= num;
 cpt= ept;
 if(strncmp(cpt, Splitpart_wordS[2], strlen(Splitpart_wordS[2])) != 0)
   return(0);
 ret= Splitpart__read_next_num(cpt, &ept, offset, 0);
 if(ret<=0)
   return(ret);
 cpt= ept;
 if(strncmp(cpt, Splitpart_wordS[3], strlen(Splitpart_wordS[3])) != 0)
   return(0);
 ret= Splitpart__read_next_num(cpt, &ept, bytes, 0);
 if(ret<=0)
   return(ret);
 cpt= ept;
 if(strncmp(cpt, Splitpart_wordS[4], strlen(Splitpart_wordS[4])) != 0)
   return(0);
 ret= Splitpart__read_next_num(cpt, &ept, total_bytes, 0);
 if(ret<=0)
   return(ret);
 if(*ept != 0)
   return(0);
 return(1);
}


int Splitpart__is_part_path(char *path, int flag)
{
 int partno, total_parts, ret;
 off_t offset, bytes, total_bytes;
 char *name;

 name= strrchr(path, '/');
 if(name == NULL)
   name= path;
 else
   name++;
 ret= Splitpart__parse(name, &partno, &total_parts, &offset, &bytes,
                       &total_bytes, 0);
 return(ret > 0);
}


/* part_#_of_#_at_#_with_#_of_#
*/
int Splitpart__compose(char *adr, int partno, int total_parts,
                       off_t offset, off_t bytes, off_t total_bytes, int flag)
{
 sprintf(adr, "%s%d%s%d%s", Splitpart_wordS[0], partno, Splitpart_wordS[1],
                            total_parts, Splitpart_wordS[2]);
 if((offset % (1024*1024))==0 && offset>0) {
   Sfile_off_t_text(adr+strlen(adr), offset / (1024*1024), 0);
   strcat(adr, "m");
 } else
   Sfile_off_t_text(adr+strlen(adr), offset, 0);
 strcat(adr, Splitpart_wordS[3]); 
 if((bytes % (1024*1024))==0) {
   Sfile_off_t_text(adr+strlen(adr), bytes / (1024*1024), 0);
   strcat(adr, "m");
 } else
   Sfile_off_t_text(adr+strlen(adr), bytes, 0);
 strcat(adr, Splitpart_wordS[4]);
 Sfile_off_t_text(adr+strlen(adr), total_bytes, 0);
 return(1);
}


int Splitparts_cmp(const void *v1, const void *v2)
{
 struct SplitparT *p1, *p2;

 p1= (struct SplitparT *) v1;
 p2= (struct SplitparT *) v2;
 
 if(p1->partno>p2->partno)
   return(1);
 if(p1->partno<p2->partno)
   return(-1);
 if(p1->offset>p2->offset)
   return(1);
 if(p1->offset<p2->offset)
   return(-1);
 return(0);
}


int Splitparts_sort(struct SplitparT *o, int count, int flag)
{
 qsort(o, (size_t) count, sizeof(struct SplitparT), Splitparts_cmp);
 return(1);
}


/* ---------------------------- End SplitparT ------------------------- */


/* ------------------------------ DirseQ  ------------------------------ */


static int Dirseq_buffer_sizE= 100;

struct DirseQ {
  char adr[SfileadrL];
  DIR *dirpt;
  int count;  
  char **buffer;
  int buffer_size;
  int buffer_fill;
  int buffer_rpt;

  struct DirseQ *next;
};

int Dirseq_destroy(struct DirseQ **o, int flag);
int Dirseq_next_adrblock(struct DirseQ *o, char *replies[], int *reply_count,
                         int max_replies, int flag);


int Dirseq_new(struct DirseQ **o, char *adr, int flag)
/*
 bit0= with non-fatal errors do not complain about failed opendir() 
*/
{
 int ret,i,severe_error;
 struct DirseQ *m;

 m= *o= TSOB_FELD(struct DirseQ,1);
 if(m==NULL)
   return(-1);
 m->adr[0]= 0;
 m->dirpt= NULL;
 m->count= 0;
 m->buffer= NULL;
 m->buffer_size= 0;
 m->buffer_fill= 0;
 m->buffer_rpt= 0;
 m->next= NULL;
 if(Sfile_str(m->adr, adr, 0)<=0)
   {ret= -1; goto failed;}
 m->buffer= TSOB_FELD(char *,Dirseq_buffer_sizE);
 if(m->buffer==NULL)
   {ret= -1; goto failed;}
 m->buffer_size= Dirseq_buffer_sizE;
 for(i= 0;i<m->buffer_size;i++)
   m->buffer[i]= NULL;
 if(adr[0]==0)
   m->dirpt= opendir(".");
 else
   m->dirpt= opendir(adr);
 if(m->dirpt==NULL) {
   severe_error= (errno && errno!=ENOENT && errno!=EACCES && errno!=ENOTDIR);
   if(severe_error || !(flag&1))
     fprintf(stderr,"opendir(%s) failed : %s\n",adr,strerror(errno));
   ret= -severe_error;
   goto failed;
 }
 return(1);
failed:;
 Dirseq_destroy(o,0);
 return(ret);
}


int Dirseq_destroy(struct DirseQ **o, int flag)
{
 int i;

 if(*o==NULL)
   return(0);
 if((*o)->dirpt!=NULL)
   closedir((*o)->dirpt);
 if((*o)->buffer!=NULL) {
   for(i=0;i<(*o)->buffer_size;i++)
     if((*o)->buffer[i]!=NULL)
       free((*o)->buffer[i]);
   free((char *) (*o)->buffer);
 }
 free((char *) *o);
 (*o)= NULL;
 return(1);
}


int Dirseq_set_next(struct DirseQ *o, struct DirseQ *next, int flag)
{
 o->next= next;
 return(1);
}


int Dirseq_get_next(struct DirseQ *o, struct DirseQ **next, int flag)
{
 *next= o->next;
 return(1);
}


int Dirseq_get_adr(struct DirseQ *o, char **adrpt, int flag)
{
 *adrpt= o->adr;
 return(1);
}


int Dirseq_rewind(struct DirseQ *o, int flag)
{
 rewinddir(o->dirpt);
 return(1);
}


int Dirseq_next_adr(struct DirseQ *o, char reply[SfileadrL], int flag)
/*
flag:
 bit0= permission to use buffer
 bit1= do not increment counter
 bit2= ignore buffer in any case
 bit3= do not exclude '.' and '..'
 bit4= sort buffer
 bit5= sort only incomplete last buffer
return:
 <0 error
 0= no more entries available
 1= ok, reply is valid
*/
{
 int ret;
 struct dirent *entry;
 char *name;

 static int override_flag_0= 0,override_flag_1= 32;
 flag= (flag&~override_flag_0)|override_flag_1;

 if((flag&1) && o->buffer_rpt>=o->buffer_fill) {
   /* permission to buffer and buffer empty : load a buffer */
   ret= Dirseq_next_adrblock(o,o->buffer,&(o->buffer_fill),
                             o->buffer_size,2|4|(flag&16));
   if(ret<=0)
     return(ret);
   o->buffer_rpt= 0;
   if((flag&32) && o->buffer_fill<o->buffer_size && o->buffer_fill>0) 
     Sort_argv(o->buffer_fill,o->buffer,0);
 }
 if(o->buffer_rpt<o->buffer_fill && !(flag&4)) {
   ret= Sfile_str(reply,o->buffer[o->buffer_rpt],0);
   Sregex_string(&(o->buffer[o->buffer_rpt]),NULL,0);
   if(ret<=0)
     return(-1);
   (o->buffer_rpt)++;
   if(!(flag&2))
     o->count++;
   return(1);
 }
 do {
   entry= readdir(o->dirpt);
   if(entry==NULL) {
     /* >>> how to distinguish error from EOF , do i need a (FILE *) ? */
     return(0);
   }
   if(strlen(entry->d_name)>=SfileadrL) {
     fprintf(stderr,"--- oversized directory entry (number %d) :\n    %s",
                    o->count+1,entry->d_name);
     return(-1);
   }
   name= entry->d_name;
   if(flag&8)
 break;
   /* skip "." and ".." */
 } while(name[0]=='.' && ((name[1]=='.' && name[2]==0) || name[1]==0));
 if(Sfile_str(reply,name,0)<=0)
   return(-1);
 if(!(flag&2))
   o->count++;
 return(1);
}


int Dirseq_next_adrblock(struct DirseQ *o, char *replies[], int *reply_count,
                         int max_replies, int flag)
/* @param replies A vector of Sregex_string pointers */
/*
flag:
 bit0= permission to use buffer
 bit1= do not increment counter
 bit2= ignore buffer in any case
 bit4= sort replies
return:
 <0 error
 0= no more entries available
 1= ok, reply is valid
*/
{
 int i,ret;
 char *reply= NULL;

 reply= TSOB_FELD(char, SfileadrL);
 if(reply == NULL)
   return(-1);

 *reply_count= 0;
 for(i=0;i<max_replies;i++) {
   ret= Dirseq_next_adr(o,reply,flag&(1|2|4));
   if(ret<0)
     goto ex;
   if(ret==0)
 break;
   if(Sregex_string(&(replies[i]),reply,0)<=0)
     {ret= -1; goto ex;}
   (*reply_count)++;
 }
 if((*reply_count)==0)
   {ret= 0; goto ex;}
 if(flag&16) 
   Sort_argv(*reply_count,replies,0);
 ret= 1;
ex:;
 free(reply);
 return(ret);
}


/* ---------------------------- End DirseQ  ----------------------------- */


/* ---------------------------- Xorriso_lsT ----------------------------- */


/*
 @param flag Bitfield for control purposes
        bit0= insert before link rather than after it
        bit1= do not copy data (e.g. because *data is invalid)
        bit2= attach data directly by pointer rather than by copying
*/
int Xorriso_lst_new_binary(struct Xorriso_lsT **lstring, char *data,
                           int data_len, struct Xorriso_lsT *link, int flag)
{
 struct Xorriso_lsT *s;

 s= TSOB_FELD(struct Xorriso_lsT,1);
 if(s==NULL)
   return(-1);
 s->text= NULL;
 s->next= s->prev= NULL;

 if(flag & 4) {
   s->text= data;
 } else {
   if(data_len<=0)
     goto failed;
   s->text= Smem_malloC(data_len);
   if(s->text==NULL)
     goto failed;
   if(!(flag&2))
     memcpy(s->text,data,data_len);
 }

 if(link==NULL) {
   ;
 } else if(flag&1) {
   s->next= link;
   s->prev= link->prev;
   if(link->prev!=NULL) 
     link->prev->next= s;
   link->prev= s;
 } else {
   s->prev= link;
   s->next= link->next;
   if(link->next!=NULL) 
     link->next->prev= s;
   link->next= s;
 }
 *lstring= s;
 return(1);
failed:;
 *lstring= s;
 Xorriso_lst_destroy(lstring,0);
 return(-1);
}


/*
 @param flag Bitfield for control purposes
             see Xorriso_lst_new_binary()
*/
int Xorriso_lst_new(struct Xorriso_lsT **lstring, char *text,
                    struct Xorriso_lsT *link, int flag)
{
 int ret;

 ret= Xorriso_lst_new_binary(lstring,text,strlen(text)+1,link,flag);
 return(ret);
}


/*
 @param flag Bitfield for control purposes
             bit0= do not set *lstring to NULL
*/
int Xorriso_lst_destroy(struct Xorriso_lsT **lstring, int flag)
{
 struct Xorriso_lsT *s;

 s= *lstring;
 if(s==NULL)
   return(0);
 if(s->prev!=NULL)
   s->prev->next= s->next;
 if(s->next!=NULL)
   s->next->prev= s->prev;
 if(s->text!=NULL)
   Smem_freE(s->text);
 Smem_freE((char *) s);
 if(!(flag&1))
   *lstring= NULL;
 return(1);
}


int Xorriso_lst_destroy_all(struct Xorriso_lsT **lstring, int flag)
{
 struct Xorriso_lsT *s,*next;

 if(lstring==NULL)
   return(-1);
 if((*lstring)==NULL)
   return(0);
 for(s= *lstring; s->prev!=NULL; s= s->prev);
 for(;s!=NULL;s= next){
   next= s->next;
   Xorriso_lst_destroy(&s,0);
 }
 *lstring= NULL;
 return(1);
}


int Xorriso_lst_append_binary(struct Xorriso_lsT **entry,
                              char *data, int data_len, int flag)
{
 struct Xorriso_lsT *target= NULL,*newby;

 if(*entry!=NULL)
   for(target= *entry; target->next!=NULL; target= target->next);
 if(Xorriso_lst_new_binary(&newby, data, data_len, target, flag & ~1)<=0)
   return(-1);
 if(*entry==NULL || (flag & 1))
   *entry= newby;
 return(1);
}


struct Xorriso_lsT *Xorriso_lst_get_next(struct Xorriso_lsT *entry, int flag)
{
 return(entry->next);
}


struct Xorriso_lsT *Xorriso_lst_get_prev(struct Xorriso_lsT *entry, int flag)
{
 return(entry->prev);
}


char *Xorriso_lst_get_text(struct Xorriso_lsT *entry, int flag)
{
 return(entry->text);
}


int Xorriso_lst_detach_text(struct Xorriso_lsT *entry, int flag)
{
 entry->text= NULL;
 return(1);
}


int Xorriso_lst_get_last(struct Xorriso_lsT *entry, struct Xorriso_lsT **last,
                         int flag)
{
 *last= NULL;
 if(entry != NULL)
   for((*last)= entry; (*last)->next != NULL; (*last)= (*last)->next);
 return(1);
} 


int Xorriso_lst_concat(struct Xorriso_lsT *first, struct Xorriso_lsT *second,
                       int flag)
{
 struct Xorriso_lsT *last;

 Xorriso_lst_get_last(first, &last, 0);
 if(last != NULL)
   last->next= second;
 if(second != NULL)
   second->prev= last;
 return(1);
}

/* --------------------------- End Xorriso_lsT ---------------------------- */


/* ------------------------------ ExclusionS ------------------------------ */


struct ExclusionS {

 /* Absolute input patterns which lead to not_paths */
 struct Xorriso_lsT *not_paths_descr;

 /* Actually banned absolute paths */
 struct Xorriso_lsT *not_paths;

 /* Input patterns which lead to not_leafs */
 struct Xorriso_lsT *not_leafs_descr;

 /* Compiled not_leaf patterns. Caution: not char[] but  regex_t */
 struct Xorriso_lsT *not_leafs;
 
};


int Exclusions_new(struct ExclusionS **o, int flag)
{
 struct ExclusionS *m;

 m= *o= TSOB_FELD(struct ExclusionS, 1);
 if(m==NULL)
   return(-1);
 m->not_paths_descr= NULL;
 m->not_paths= NULL;
 m->not_leafs_descr= NULL;
 m->not_leafs= NULL;
 return(1);
}


int Exclusions_destroy(struct ExclusionS **o, int flag)
{
 struct Xorriso_lsT *s,*next;

 if((*o)==NULL)
   return(0);
 Xorriso_lst_destroy_all(&((*o)->not_paths_descr), 0);
 Xorriso_lst_destroy_all(&((*o)->not_paths), 0);
 Xorriso_lst_destroy_all(&((*o)->not_leafs_descr), 0);
 for(s= (*o)->not_leafs; s!=NULL; s= next){
   next= s->next;
   regfree((regex_t *) s->text);
   Xorriso_lst_destroy(&s, 0);
 }
 free((char *) *o);
 (*o)= NULL;
 return(1);
}


int Exclusions_add_not_paths(struct ExclusionS *o, int descrc, char **descrs,
                             int pathc, char **paths, int flag)
{
 struct Xorriso_lsT *s, *new_s;
 int i, ret;

 s= NULL;
 if(o->not_paths_descr!=NULL) 
   for(s= o->not_paths_descr; s->next!=NULL; s= s->next);
 for(i= 0; i<descrc; i++) {
   ret= Xorriso_lst_new(&new_s, descrs[i], s, 0);
   if(ret<=0)
     return(ret);
   if(o->not_paths_descr==NULL)
     o->not_paths_descr= new_s;
   s= new_s;
 }
 s= NULL;
 if(o->not_paths!=NULL) 
   for(s= o->not_paths; s->next!=NULL; s= s->next);
 for(i= 0; i<pathc; i++) {
   ret= Xorriso_lst_new(&new_s, paths[i], s, 0);
   if(ret<=0)
     return(ret);
   if(o->not_paths==NULL)
     o->not_paths= new_s;
   s= new_s;
 }
 return(1);
}


/* @return -1=cannot store , 0=cannot compile regex , 1=ok
*/
int Exclusions_add_not_leafs(struct ExclusionS *o, char *not_leafs_descr,
                             regex_t *re, int flag)
{
 int ret;

 ret= Xorriso_lst_append_binary(&(o->not_leafs_descr),
                            not_leafs_descr, strlen(not_leafs_descr)+1, 0);
 if(ret<=0)
   return(-1);
 ret= Xorriso_lst_append_binary(&(o->not_leafs), (char *) re, sizeof(regex_t), 0);
 if(ret<=0)
   return(-1);
 return(1);
}


/* @param flag bit0= whole subtree is banned with -not_paths 
   @return 0=no match , 1=not_paths , 2=not_leafs, <0=error
*/
int Exclusions_match(struct ExclusionS *o, char *abs_path, int flag)
{
 struct Xorriso_lsT *s;
 char *leaf= NULL, *leaf_pt;
 regmatch_t match[1];
 int ret, was_non_slash, l;

 /* test abs_paths */
 if(flag&1) {
   for(s= o->not_paths; s!=NULL; s= s->next) {
     l= strlen(s->text);
     if(strncmp(abs_path, s->text, l)==0)
       if(abs_path[l]=='/' || abs_path[l]==0)
         {ret= 1; goto ex;}
   }
 } else {
   for(s= o->not_paths; s!=NULL; s= s->next)
     if(strcmp(abs_path, s->text)==0)
       {ret= 1; goto ex;}
 }

 /* determine leafname */
 was_non_slash= 0;
 for(leaf_pt= abs_path+strlen(abs_path); leaf_pt >= abs_path; leaf_pt--) {
   if(*leaf_pt=='/') {
     if(was_non_slash) {
       leaf_pt++;
 break;
     }
   } else if(*leaf_pt!=0)
     was_non_slash= 1;
 }
 if(strlen(leaf_pt)>=SfileadrL)
   {ret= -1; goto ex;}
 leaf= strdup(leaf_pt);
 leaf_pt= strchr(leaf, '/');
 if(leaf_pt!=NULL)
   *leaf_pt= 0;

 /* test with leaf expressions */
 for(s= o->not_leafs; s!=NULL; s= s->next) {
   ret= regexec((regex_t *) s->text, leaf, 1, match, 0);
   if(ret==0)
     {ret= 2; goto ex;}
 }
 ret= 0;
ex:
 if(leaf != NULL)
   free(leaf);
 return(ret);
}


int Exclusions_get_descrs(struct ExclusionS *o,
                          struct Xorriso_lsT **not_paths_descr,
                          struct Xorriso_lsT **not_leafs_descr, int flag)
{
 *not_paths_descr= o->not_paths_descr;
 *not_leafs_descr= o->not_leafs_descr;
 return(1);
}

/* ---------------------------- End ExclusionS ---------------------------- */


/* ------------------------------ LinkiteM -------------------------------- */

struct LinkiteM {
 char *link_path;
 dev_t target_dev;
 ino_t target_ino;
 int link_count;
 struct LinkiteM *next;
};


int Linkitem_new(struct LinkiteM **o, char *link_path, dev_t target_dev,
                 ino_t target_ino, struct LinkiteM *next, int flag)
{
 struct LinkiteM *m;

 m= *o= TSOB_FELD(struct LinkiteM,1);
 if(m==NULL)
   return(-1);
 m->target_dev= target_dev;
 m->target_ino= target_ino;
 m->next= next;
 m->link_count= 1;
 if(next!=NULL)
   m->link_count= m->next->link_count+1;
 m->link_path= strdup(link_path);
 if(m->link_path==NULL)
   goto failed;
 return(1);
failed:;
 Linkitem_destroy(o, 0);
 return(-1);
}


int Linkitem_destroy(struct LinkiteM **o, int flag)
{
 if((*o)==NULL)
   return(0);
 if((*o)->link_path!=NULL)
   free((*o)->link_path);
 free((char *) (*o));
 *o= NULL;
 return(1);
}


int Linkitem_reset_stack(struct LinkiteM **o, struct LinkiteM *to, int flag)
{
 struct LinkiteM *m, *m_next= NULL;

 /* Prevent memory corruption */
 for(m= *o;  m!=to; m= m->next)
   if(m==NULL) { /* this may actually not happen */
     *o= to;
     return(-1);
   }

 for(m= *o; m!=to; m= m_next) {
   m_next= m->next;
   Linkitem_destroy(&m, 0);
 }
 *o= to;
 return(1);
}


int Linkitem_find(struct LinkiteM *stack, dev_t target_dev, ino_t target_ino,
                  struct LinkiteM **result, int flag)
{
 struct LinkiteM *m;

 for(m= stack; m!=NULL; m= m->next) {
   if(target_dev == m->target_dev && target_ino == m->target_ino) {
     *result= m;
     return(1);
   }
 }
 return(0);
}


int Linkitem_get_link_count(struct LinkiteM *item, int flag)
{
 return(item->link_count);
}


/* ------------------------------ PermstacK ------------------------------- */


struct PermiteM {
 char *disk_path;
 struct stat stbuf;
 struct PermiteM *next;
};


int Permstack_push(struct PermiteM **o, char *disk_path, struct stat *stbuf,
                   int flag)
{
 struct PermiteM *m;

 m= TSOB_FELD(struct PermiteM,1);
 if(m==NULL)
   return(-1);
 m->disk_path= NULL;
 memcpy(&(m->stbuf), stbuf, sizeof(struct stat));
 m->next= *o;

 m->disk_path= strdup(disk_path);
 if(m->disk_path==NULL)
   goto failed;

 *o= m;
 return(1);
failed:;
 if(m->disk_path!=NULL)
   free(m->disk_path);
 free((char *) m);
 return(-1);
}


/* @param flag bit0= minimal transfer: access permissions only
               bit1= do not set timestamps
*/ 
int Permstack_pop(struct PermiteM **o, struct PermiteM *stopper,
                  struct XorrisO *xorriso, int flag)
{
 int ret;
 struct utimbuf utime_buffer;
 struct PermiteM *m, *m_next;

 if((*o)==stopper)
   return(1);
 for(m= *o; m!=NULL; m= m->next)
   if(m->next==stopper)
 break;
 if(m==NULL) {
   sprintf(xorriso->info_text,
           "Program error: Permstack_pop() : cannot find stopper");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   return(-1);
 }

 for(m= *o; m!=stopper; m= m_next) {
   ret= chmod(m->disk_path, m->stbuf.st_mode);
   if(ret==-1) {
     if(xorriso!=NULL) {
       sprintf(xorriso->info_text,
             "Cannot change access permissions of disk directory: chmod %o ",
             (unsigned int) (m->stbuf.st_mode & 07777));
       Text_shellsafe(m->disk_path, xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE",
                           0);
     }
   }
   if(!(flag&1)) {
     ret= chown(m->disk_path, m->stbuf.st_uid, m->stbuf.st_gid);
                                               /* don't complain if it fails */
     if(!(flag&2)) {
       utime_buffer.actime= m->stbuf.st_atime;
       utime_buffer.modtime= m->stbuf.st_mtime;
       ret= utime(m->disk_path,&utime_buffer);
       if(ret==-1 && xorriso!=NULL) {
         sprintf(xorriso->info_text,
                 "Cannot change timestamps of disk directory: ");
         Text_shellsafe(m->disk_path, xorriso->info_text, 1);
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE",
                             0);
       }
     }
   }
   m_next= m->next;
   free(m->disk_path);
   free((char *) m);
   *o= m_next;
 }
 return(1);
}


/* ---------------------------- End PermstacK ----------------------------- */

