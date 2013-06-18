
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2010 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the implementation of classes FindjoB, ExprnodE,
   ExprtesT which perform tree searches in libisofs or in POSIX filesystem
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
#include <dirent.h>
#include <errno.h>


#include "xorriso.h"
#include "xorriso_private.h"
#include "xorrisoburn.h"


/* ----------------------- Exprtest ----------------------- */


int Exprtest_new( struct ExprtesT **ftest, struct FindjoB *boss, int flag)
{
 struct ExprtesT *f;

 *ftest= f= TSOB_FELD(struct ExprtesT,1);
 if(f==NULL)
   return(-1);
 f->boss= boss;
 f->invert= 0;
 f->test_type= -1;
 f->arg1= NULL;
 f->arg2= NULL;
 return(1);
}


int Exprtest_destroy(struct ExprtesT **ftest, int flag)
{
 struct ExprtesT *f;
 
 f= *ftest;
 if(f==NULL)
   return(0);

 if(f->test_type == 1 || f->test_type == 13 || f->test_type == 16) {
   if(f->arg1 != NULL)
     free(f->arg1);
   if(f->arg2 != NULL) {
     regfree(f->arg2);
     free(f->arg2);
   }
 } else if(f->test_type == 9) {
   /* arg1 is not an allocated value */;
 } else {
   if(f->arg1 != NULL)
     free(f->arg1);
   if(f->arg2 != NULL)
     free(f->arg2);
 }
 free((char *) f);
 *ftest= NULL;
 return(1);
}


/* ----------------------- Nttpfnode ----------------------- */


int Exprnode_new(struct ExprnodE **fnode, struct FindjoB *job,
                 struct ExprnodE *up, char *origin, int flag)
/*
 bit0= set invert-property
 bit1= set use_shortcuts
*/
{
 struct ExprnodE *n;
 int ret,i;

 *fnode= n= TSOB_FELD(struct ExprnodE,1);
 if(n == NULL)
   return(-1);
 for(i= 0; i < (int) sizeof(n->origin); i++)
   n->origin[i]= 0;
 strncpy(n->origin, origin, sizeof(n->origin) - 1);
 n->up= up;
 n->invert= (flag & 1);
 n->assoc= 0;
 n->use_shortcuts= !!(flag & 2);
 n->left= NULL;
 n->left_op= -1;
 n->right= NULL;
 n->right_op= -1;
 n->sub= NULL;
 n->is_if_then_else= 0;
 n->true_branch= NULL;
 n->false_branch= NULL;
 n->test= NULL;
 n->own_value= -1;
 n->composed_value= -1;

 ret= Exprtest_new(&(n->test), job, 0);
 if(ret<=0){
   Exprnode_destroy(fnode, 0);
   return(-1);
 }
 return(1);
}


int Exprnode_destroy(struct ExprnodE **fnode, int flag)
{
 if(*fnode == NULL)
   return(0);
 Exprnode_destroy(&((*fnode)->right),0);
 Exprnode_destroy(&((*fnode)->sub),0);
 Exprnode_destroy(&((*fnode)->true_branch),0);
 Exprnode_destroy(&((*fnode)->false_branch),0);
 Exprtest_destroy(&((*fnode)->test),0);
 free((char *) *fnode);
 *fnode= NULL;
 return(1);
}


int Exprnode_set_is_if(struct ExprnodE *fnode, int value, int flag)
{
 fnode->is_if_then_else= value;
 return(1);
}


int Exprnode_is_if(struct ExprnodE *fnode, int flag)
{
  return(fnode->is_if_then_else);
}


int Exprnode_set_branch(struct ExprnodE *fnode, struct ExprnodE *target,
                        int flag)
/*
 bit0= false_branch (else true_branch)
*/
{
 struct ExprnodE **branch;

 if(flag&1) 
   branch= &(fnode->false_branch);
 else
   branch= &(fnode->true_branch);
 Exprnode_destroy(branch,0);
 (*branch)= target;
 return(1);
}


int Exprnode_get_branch(struct ExprnodE *fnode, struct ExprnodE **branch,
                        int flag)
/*
 bit0= false_branch (else true_branch)
*/
{
 if(flag&1) 
   (*branch)= fnode->false_branch;
 else
   (*branch)= fnode->true_branch;
 return(1);
}


int Exprnode_is_defined(struct ExprnodE *fnode, int flag)
{
 struct ExprtesT *ftest;

 if(fnode==NULL)
   return(0);
 if(fnode->sub!=NULL)
   return(1);
 ftest= fnode->test;
 if(ftest==NULL)
   return(0);
 if(ftest->test_type>=0)
   return(1);
 return(0);
}


int Exprnode_own_value(struct XorrisO *xorriso, struct ExprnodE *fnode, 
                       void *node, char *name, char *path,
                       struct stat *boss_stbuf, struct stat *stbuf, int flag)
/*
flag:
return: (also from Exprtest_match() and Exprnode_tree_value() )
 <0 = error
  0 = does not match
  1 = does match
  2 = immediate decision : does not match
  3 = immediate decision : does match
*/
{
 int ret;

 if(fnode==NULL)
   return(1);
 if(fnode->sub!=NULL) {
   ret= Exprnode_tree_value(xorriso, fnode->sub, -1,
                            node, name, path, boss_stbuf, stbuf, 0);
 } else {
   ret= Exprtest_match(xorriso, fnode->test, node, name, path,
                       boss_stbuf, stbuf, 0);
 }
 if(ret<0)
   return(ret);
 if(ret>1)
   return(ret);
 if(fnode->invert)
   ret= !ret;
 return(ret);
}


int Exprnode_op(int value1, int value2, int op, int flag)
{
 int ret;

 if(op==0)
   ret= value1 || value2 ;
 else
   ret= value1 && value2 ;
 return(ret);
}


int Exprnode_tree_value(struct XorrisO *xorriso, struct ExprnodE *fnode,
                        int left_value, void *node, char *name, char *path,
                        struct stat *boss_stbuf, struct stat *stbuf, int flag)
/*
 bit0-7= testmode: 0=head , 1=filename
return: (also from Nntpftest_match() and Nntpfnode_own_value() )
 <0 = error
  0 = does not match
  1 = does match
  2 = immediate decision : does not match
  3 = immediate decision : does match
*/
{
 int value= 1,ret;

 if(fnode==NULL)
   return(1);
 if(!Exprnode_is_defined(fnode,0))
   return(1);

 if(fnode->use_shortcuts && fnode->left!=NULL){
   fnode->composed_value= left_value;
   if(fnode->left_op==0) {/* OR */
     if(left_value!=0) 
       goto ex;
   } else {                /* AND */
     if(left_value==0)
       goto ex;
   }
 }
 fnode->composed_value= fnode->own_value= 
    Exprnode_own_value(xorriso, fnode, node, name, path, boss_stbuf, stbuf, 0);
 if(fnode->own_value < 0 || fnode->own_value > 1)
   return(fnode->own_value);

 if(fnode->assoc == 0){ /* left associative */
   if(fnode->left != NULL && left_value >= 0)
     fnode->composed_value= 
       Exprnode_op(left_value, fnode->own_value, fnode->left_op, 0);
   /* compute right value */
   /* is the right value relevant ? */
   if(fnode->right!=NULL){
     if(fnode->use_shortcuts){
       if(fnode->right_op==0) {/* OR */
         if(fnode->composed_value!=0)
           goto ex;
       } else {                /* AND */
         if(fnode->composed_value==0)
           goto ex;
       }  
     }
     value= Exprnode_tree_value(xorriso, fnode->right,fnode->composed_value,
                               node, name, path, boss_stbuf, stbuf, 0);
     if(value<0 || value>1)
       return(value);
     fnode->composed_value= value;
   }
 }else{ /* right associative */
   if(fnode->right!=NULL){
     /* is the right value relevant ? */
     if(fnode->use_shortcuts){
       if(fnode->right_op==0) {/* OR */
         if(fnode->composed_value!=0)
           goto ex;
       } else {                /* AND */
         if(fnode->composed_value==0)
           goto ex;
       }
     }
     value= Exprnode_tree_value(xorriso, fnode->right,fnode->own_value,
                               node, name, path, boss_stbuf, stbuf, 0);
     if(value<0||value>1)
       return(value);
   } else
     value= fnode->own_value;
   fnode->composed_value= value;
   if(fnode->left!=NULL && left_value>=0)
     fnode->composed_value=
       Exprnode_op(left_value,fnode->composed_value,fnode->left_op,0);
 }
ex:
 ret= fnode->composed_value;
 if(fnode->is_if_then_else) {
   /* The if-condition is evaluated. Now follow the chosen branch */
   struct ExprnodE *branch;
   if(ret>0) 
     branch= fnode->true_branch;
   else 
     branch= fnode->false_branch;
   if(branch!=NULL) {
     ret= Exprnode_tree_value(xorriso, branch, -1,
                              node, name, path, boss_stbuf, stbuf, 0);
     if(ret<0)
       return(ret);
     if(ret>1)
       return(ret);
   }
   fnode->composed_value= ret;
 }
 return(fnode->composed_value);
}


/* --------------------- Findjob -------------------- */


int Findjob_new(struct FindjoB **o, char *start_path, int flag)
{
 struct FindjoB *m;
 int ret;

 m= *o= TSOB_FELD(struct FindjoB,1);
 if(m==NULL)
   return(-1);
 m->start_path= NULL;
 m->test_tree= NULL;
 m->cursor= NULL;
 m->invert= 0;
 m->use_shortcuts= 1;
 m->action= 0; /* print */
 m->prune= 0;
 m->target= NULL; /* a mere pointer, not managed memory */
 m->text_2= NULL; /* a mere pointer, not managed memory */
 m->user= 0;
 m->group= 0;
 m->type= 0;
 m->date= 0;
 m->start_path= strdup(start_path);
 if(m->start_path==NULL)
   goto failed;
 m->found_path= NULL;
 m->estim_upper_size= 0;
 m->estim_lower_size= 0;
 m->subjob= NULL;
 m->errmsg[0]= 0;
 m->errn= 0;
 m->match_count= 0;

 ret= Exprnode_new(&(m->test_tree), m, NULL, "-find", (m->use_shortcuts)<<1);
 if(ret<=0)
   goto failed;
 m->cursor= m->test_tree;
 return(1);

failed:;
 Findjob_destroy(o, 0);
 return(-1);
}


int Findjob_destroy(struct FindjoB **o, int flag)
{
 struct FindjoB *m;
 
 m= *o;
 if(m==NULL)
   return(0);
 if(m->test_tree != NULL)
   Exprnode_destroy(&(m->test_tree), 0); 
 if(m->start_path != NULL)
   free(m->start_path);
 if(m->found_path != NULL)
   free(m->found_path);
 free((char *) *o);
 *o= NULL;
 return(1);
}


int Findjob_set_start_path(struct FindjoB *o, char *start_path, int flag)
{
 if(o->start_path!=NULL)
   free(o->start_path);
 if(start_path!=NULL) {
   o->start_path= strdup(start_path);
   if(o->start_path==NULL)
     return(-1);
 } else
   o->start_path= NULL;
 return(1);
}


int Findjob_get_start_path(struct FindjoB *o, char **start_path, int flag)
{
 *start_path= o->start_path;
 return(1);
}


int Findjob_cursor_complete( struct FindjoB *job, int flag)
{
 int ret;

 if(job==NULL)
   return(0);
 ret= Exprnode_is_defined(job->cursor,0);
 return(ret);
}


int Findjob_is_restrictive(struct FindjoB *job, int flag)
{
 if(job == NULL)
   return(0);
 if(job->test_tree == NULL)
   return(0);
 if(!Exprnode_is_defined(job->test_tree, 0))
   return(0);
 return(1);
}


int Findjob_new_node(struct FindjoB *job, struct ExprnodE **fnode,
                     char *origin, int flag)
/*
 bit0= open new branch 
 bit1= with bit1 : do not register as sub-node of job->cursor
*/
{
 int ret;
 struct ExprnodE *f;

 ret= Exprnode_new(fnode,job,NULL,origin,
                   job->invert|((job->use_shortcuts)<<1));
 if(ret<=0)
   return(ret);
 f= *fnode;
 if(flag&1) {
   f->up= job->cursor;
   if(job->cursor!=NULL && !(flag&2)) {
     if(job->cursor->sub!=NULL) {
       /* This would become a memory leak */
       job->errn= -2;
       sprintf(job->errmsg,
               "Program error while parsing -job : sub branch overwrite");
       return(0);
     } else
       job->cursor->sub= f;
   }
 } else {
   f->up= job->cursor->up;
   f->left= job->cursor;
   if(job->cursor!=NULL)
     job->cursor->right= f;
 }
 job->invert= 0;
 return(1);
}


/* If an operator is expected : use -and
   @param flag bit0= prepare for a pseudo-test:
                     if an operator is expected, do nothing and return 2
*/
int Findjob_default_and(struct FindjoB *o, int flag)
{
 int ret;

 if(Findjob_cursor_complete(o, 0)) {
   if(flag & 1) 
     return(2);
   ret= Findjob_and(o, 0);
   if(ret <= 0)
     return(ret);
 }
 return(1);
}


int Findjob_open_bracket(struct FindjoB *job, int flag)
{
 int ret;
 struct ExprnodE *fnode;

 ret= Findjob_default_and(job, 0);
 if(ret <= 0)
   return(ret);
 ret= Findjob_new_node(job, &fnode, "-sub", 1);
 if(ret <= 0)
   return(ret);
 job->cursor= fnode;
 return(1);
}


int Findjob_close_bracket(struct FindjoB *job, int flag)
{
 if(!Findjob_cursor_complete(job, 0)) {
   job->errn= -3;
   sprintf(job->errmsg,
     "Unary operator or expression expected, closing-bracket found");
   return(0);
 }

 if(job->cursor->up==NULL){
   job->errn= -1;
   sprintf(job->errmsg,
           "No bracket open when encountering closing bracket.");
   return(0);
 }
 job->cursor= job->cursor->up;
 return(1);
}


int Findjob_not(struct FindjoB *job, int flag)
{
 int ret;

 ret= Findjob_default_and(job, 0);
 if(ret <= 0)
   return(ret);
 job->cursor->invert= !job->cursor->invert;
 return(1);
}


int Findjob_and(struct FindjoB *job, int flag)
{
 int ret;
 struct ExprnodE *fnode;

 if(!Findjob_cursor_complete(job, 0)) {
   job->errn= -3;
   sprintf(job->errmsg,
           "Unary operator or expression expected, binary operator found");
   return(0);
 }

 ret= Findjob_new_node(job, &fnode, "-and", 0);
 if(ret<=0)
   return(ret);
 job->cursor->right_op= 1;
 job->cursor->assoc= 1;        /* compute right side first */
 fnode->left_op= 1;
 fnode->assoc= 0;              /* compute left side first */
 job->cursor= fnode;
 return(1);
}


int Findjob_or(struct FindjoB *job, int flag)
{
 int ret;
 struct ExprnodE *fnode;

 if(!Findjob_cursor_complete(job, 0)) {
   job->errn= -3;
   sprintf(job->errmsg,
           "Unary operator or expression expected, binary operator found");
   return(0);
 }

 ret= Findjob_new_node(job, &fnode, "-or", 0);
 if(ret<=0)
   return(ret);
 job->cursor->right= fnode;
 job->cursor->right_op= 0;
                                    /* if existing : compute left side first */
 job->cursor->assoc= (job->cursor->left == NULL);
 fnode->left= job->cursor;
 fnode->left_op= 0;
 fnode->assoc= 0;            /* no right side yet : compute left side first */
 job->cursor= fnode;
 return(1);
}


int Findjob_if(struct FindjoB *job, int flag)
{
 int ret;
 struct ExprnodE *fnode;

 ret= Findjob_default_and(job, 0);
 if(ret <= 0)
   return(ret);
 ret= Findjob_new_node(job, &fnode, "-if", 1);
 if(ret<=0)
   return(ret);
 Exprnode_set_is_if(fnode,1,0);
 job->cursor= fnode;
 return(1);
}


int Findjob_then(struct FindjoB *job, int flag)
{
 int ret;
 struct ExprnodE *fnode,*branch= NULL;

 if(! Findjob_cursor_complete(job,0)) {
   job->errn= -3;
   sprintf(job->errmsg,
           "Unary operator or expression expected, -then-operator found");
   return(0);
 } 
 /* Finding the -if that matches this -then
    Do not go up one node but look for the leftmost one.
    If everything is right we are at level of the -if node */
 while(job->cursor->left!=NULL)
   job->cursor= job->cursor->left;
 Exprnode_get_branch(job->cursor, &branch, 0);
 if(!Exprnode_is_if(job->cursor, 0) || branch != NULL) {
   job->errn= -5;
   sprintf(job->errmsg, "-then-operator found outside its proper range.");
   return(0);
 }
 ret= Findjob_new_node(job, &fnode, "-then", 1|2);
 if(ret <= 0)
   return(ret);
 Exprnode_set_branch(job->cursor, fnode, 0);
 job->cursor= fnode;
 return(1);
}


int Findjob_else(struct FindjoB *job, int flag)
{
 int ret;
 struct ExprnodE *fnode, *true_branch, *false_branch;

 if(! Findjob_cursor_complete(job, 0)) {
   job->errn= -3;
   sprintf(job->errmsg,
           "Unary operator or expression expected, -else-operator found");
   return(0);
 } 
 if(job->cursor->up == NULL)
   goto improper_range;
 job->cursor= job->cursor->up;
 Exprnode_get_branch(job->cursor, &true_branch, 0);
 Exprnode_get_branch(job->cursor, &false_branch, 1);
 if(!Exprnode_is_if(job->cursor, 0) || 
    true_branch == NULL || false_branch != NULL) {
improper_range:;
   job->errn= -5;
   sprintf(job->errmsg, "-else-operator found outside its proper range.");
   return(0);
 }
 ret= Findjob_new_node(job, &fnode, "-else", 1 | 2);
 if(ret <= 0)
   return(ret);
 Exprnode_set_branch(job->cursor, fnode, 1);
 job->cursor= fnode;
 return(1);
}


int Findjob_elseif(struct FindjoB *job, int flag)
{
 int ret;
 struct ExprnodE *true_branch, *false_branch;

 if(!Findjob_cursor_complete(job, 0)) {
   job->errn= -3;
   sprintf(job->errmsg,
           "Unary operator or expression expected, -elseif-operator found");
   return(0);
 } 
 if(job->cursor->up == NULL)
   goto improper_range;
 job->cursor= job->cursor->up;
 Exprnode_get_branch(job->cursor, &true_branch, 0);
 Exprnode_get_branch(job->cursor, &false_branch, 1);
 if(!Exprnode_is_if(job->cursor, 0) || 
    true_branch==NULL || false_branch!=NULL) {
improper_range:;
   job->errn= -5;
   sprintf(job->errmsg,
           "-elseif-operator found outside its proper range.");
   return(0);
 }
 job->cursor= job->cursor->up;
 /* -elseif is equivalent to the three-step sequence :  -endif -or -if
    ( -endif has already been performed by following job->cursor->up ) */
 ret= Findjob_or(job, 0);
 if(ret <= 0)
   return(0);
 ret= Findjob_if(job, 0);
 if(ret <= 0)
   return(0);
 return(1);
}


int Findjob_endif(struct FindjoB *job, int flag)
{
 struct ExprnodE *true_branch;

 if(!Findjob_cursor_complete(job,0)) {
   job->errn= -3;
   sprintf(job->errmsg,
           "Unary operator or expression expected, -endif found");
   return(0);
 }
 if(job->cursor->up==NULL)
   goto improper_range;
 /* test wether parent node is -if */
 job->cursor= job->cursor->up;
 Exprnode_get_branch(job->cursor, &true_branch, 0);
 if(!Exprnode_is_if(job->cursor,0) || true_branch == NULL) {
improper_range:;
   job->errn= -5;
   sprintf(job->errmsg, "-endif-mark found outside its proper range.");
   return(0);
 }
 /* go to grand parent node */
 job->cursor= job->cursor->up;
 return(1);
}


/* @param flag bit0-1: 0= -name , 1= -wholename , 2= -disk_name , 3= -disk_path
*/
int Findjob_set_name_expr(struct FindjoB *o, char *name_expr, int flag)
{
 char *regexpr= NULL;
 regex_t *name_re;
 struct ExprtesT *t;
 int ret;

 regexpr= TSOB_FELD(char, 2*SfileadrL+2);
 if(regexpr == NULL)
   {ret= -1; goto ex;}
 if(strlen(name_expr)>=SfileadrL)
   {ret= 0; goto ex;};

 ret= Findjob_default_and(o, 0);
 if(ret <= 0)
   goto ex;
 t= o->cursor->test;
 t->test_type= 1;
 if ((flag & 3) == 1)
   t->test_type= 13;
 else if((flag & 3) == 2)
   t->test_type= 16;
 else if((flag & 3) == 3)
   t->test_type= 20;
 t->arg1= strdup(name_expr);
 if(t->arg1 == NULL)
   {ret= -1; goto ex;};

 if((flag & 3) == 3)
   {ret= 1; goto ex;}

 name_re= (regex_t *) calloc(1, sizeof(regex_t));
 if(name_re == NULL)
   {ret= -1; goto ex;};
 Xorriso__bourne_to_reg(name_expr, regexpr, 0);
 if(regcomp(name_re, regexpr, 0) != 0) {
   free((char *) name_re);
   {ret= 0; goto ex;};
 }
 t->arg2= name_re;
 ret= 1;
ex:;
 Xorriso_free_meM(regexpr);
 return(ret);
}


int Findjob_set_file_type(struct FindjoB *o, char file_type, int flag)
{
 static char known[]= {"bcdpf-lsmeX"};
 struct ExprtesT *t;
 int ret;

 ret= Findjob_default_and(o, 0);
 if(ret <= 0)
   return(ret);

 if(file_type != 0)
   if(strchr(known, file_type) == NULL)
     return(0);
 t= o->cursor->test;
 t->test_type= 2;
 t->arg1= calloc(1, 1);
 if(t->arg1 == NULL)
   return(-1);
 *((char *) t->arg1)= file_type;
 return(1);
}

 
/* @param value -1= only without property, 1= only with property
   @param flag bit0= pseudo-test:
                     if no operator is open, do nothing and return 2
*/
int Findjob_set_prop_filter(struct FindjoB *o, int test_type, int value,
                            int flag)
{
 struct ExprtesT *t;
 int ret;

 ret= Findjob_default_and(o, flag & 1);
 if(ret <= 0 || ret == 2)
   return(ret);

 t= o->cursor->test;
 t->test_type= test_type;
 if(value < 0)
   t->invert= !t->invert;
 return(1);
}
 
 
/* @param value -1= only undamaged files, 1= only damaged files
*/
int Findjob_set_damage_filter(struct FindjoB *o, int value, int flag)
{
 int ret;

 ret= Findjob_set_prop_filter(o, 3, value, 0);
 return(ret);
}
 

int Findjob_set_lba_range(struct FindjoB *o, int start_lba, int count,
                          int flag)
{
 struct ExprtesT *t;
 int ret;

 ret= Findjob_default_and(o, 0);
 if(ret <= 0)
   return(ret);

 t= o->cursor->test;
 t->test_type= 4;
 t->arg1= calloc(sizeof(int), 1);
 t->arg2= calloc(sizeof(int), 1);
 if(t->arg1 == NULL || t->arg2 == NULL)
   return(-1);
 *((int *) t->arg1)= start_lba;
 if(start_lba > 0)
   *((int *) t->arg2)= start_lba + count - 1;
 else
   *((int *) t->arg2)= start_lba - count + 1;
 return(1);
}


int Findjob_set_test_hidden(struct FindjoB *o, int mode, int flag)
{
 struct ExprtesT *t;
 int ret;

 ret= Findjob_default_and(o, 0);
 if(ret <= 0)
   return(ret);

 t= o->cursor->test;
 t->test_type= 17;
 t->arg1= calloc(sizeof(int), 1);
 if(t->arg1 == NULL)
   return(-1);
 *((int *) t->arg1)= mode;
 return(1);
}


/* @param value -1= files without ACL, 1= only files with ACL
*/
int Findjob_set_acl_filter(struct FindjoB *o, int value, int flag)
{
 int ret;

 ret= Findjob_set_prop_filter(o, 5, value, 0);
 return(ret);
}


/* @param value -1= files without xattr, 1= only files with xattr
   @param flag bit0=-has_any_xattr rather than -has_xattr
*/
int Findjob_set_xattr_filter(struct FindjoB *o, int value, int flag)
{
 int ret;

 ret= Findjob_set_prop_filter(o, (flag & 1 ? 14 : 6), value, 0);
 return(ret);
}


/* @param value -1= files without aaip, 1= only files with aaip
*/
int Findjob_set_aaip_filter(struct FindjoB *o, int value, int flag)
{
 int ret;

 ret= Findjob_set_prop_filter(o, 7, value, 0);
 return(ret);
}


/* @param value -1= files without filter, 1= files with filter
*/
int Findjob_set_filter_filter(struct FindjoB *o, int value, int flag)
{
 int ret;

 ret= Findjob_set_prop_filter(o, 8, value, 0);
 return(ret);
}


int Findjob_set_crtp_filter(struct FindjoB *o, char *creator, char *hfs_type,
                            int flag)
{
 struct ExprtesT *t;
 int ret;

 ret= Findjob_default_and(o, 0);
 if(ret <= 0)
   return(ret);

 t= o->cursor->test;
 t->test_type= 18;
 t->arg1= calloc(1, strlen(creator) + 1);
 t->arg2= calloc(1, strlen(hfs_type) + 1);
 if(t->arg1 == NULL || t->arg2 == NULL)
   return(-1);
 strcpy(t->arg1, creator);
 strcpy(t->arg2, hfs_type);
 return(1);
}


int Findjob_set_bless_filter(struct XorrisO *xorriso, struct FindjoB *o,
                             char *blessing, int flag)
{
 struct ExprtesT *t;
 int ret;

 ret= Findjob_default_and(o, 0);
 if(ret <= 0)
   return(ret);

 t= o->cursor->test;
 t->test_type= 19;
 t->arg1= calloc(1, sizeof(int));
 if(t->arg1 == NULL)
   return(-1);
 ret= Xorriso_hfsplus_bless(xorriso, "", NULL, blessing, 4 | 8);
 if(ret <= 0)
   return(ret);
 *((int *) t->arg1)= ret - 1;
 return(1);
}


int Findjob_set_wanted_node(struct FindjoB *o, void *wanted_node, int flag)
{
 struct ExprtesT *t;
 int ret;

 ret= Findjob_default_and(o, 0);
 if(ret <= 0)
   return(ret);

 t= o->cursor->test;
 t->test_type= 9;
 t->arg1= wanted_node;
 return(1);
}


int Findjob_set_commit_filter_2(struct FindjoB *o, int flag)
{
 int ret;

 ret= Findjob_default_and(o, 0);
 if(ret <= 0)
   return(ret);

 o->cursor->test->test_type= 10;
 return(1);
}


int Findjob_set_decision(struct FindjoB *o, char *decision, int flag)
{
 struct ExprtesT *t;
 int ret;

 ret= Findjob_default_and(o, 0);
 if(ret <= 0)
   return(ret);

 t= o->cursor->test;
 t->test_type= 11;
 t->arg1= strdup(decision);
 if(t->arg1 == NULL)
   return(-1);
 return(1);
}


/* @param value -1= true, 1= false
   @param flag bit0= pseudo-test:
                     if no operator is open, do nothing and return 2
*/
int Findjob_set_false(struct FindjoB *o, int value, int flag)
{
 int ret;

 ret= Findjob_set_prop_filter(o, 0, value, flag & 1);
 return(ret);
}


int Findjob_set_prune(struct FindjoB *o, int flag)
{
 int ret;

 ret= Findjob_set_prop_filter(o, 12, 0, 0);
 return(ret);
}


int Findjob_set_found_path(struct FindjoB *o, char *path, int flag)
{
 if(o->found_path != NULL)
   free(o->found_path);
 if(path != NULL) {
   o->found_path= strdup(path);
   if(o->found_path == NULL)
     return(-1);
 } else
   o->found_path= NULL;
 return(1);
}


int Findjob_get_found_path(struct FindjoB *o, char **path, int flag)
{
 *path= o->found_path;
 return(1);
}


int Findjob_get_action(struct FindjoB *o, int flag)
{
 return(o->action);
}  


/* @return <0 error, >=0 see above struct FindjoB.action
*/
int Findjob_get_action_parms(struct FindjoB *o, char **target, char **text_2,
                             uid_t *user, gid_t *group,
                             mode_t *mode_and, mode_t *mode_or,
                             int *type, time_t *date, struct FindjoB **subjob,
                             int flag)
{
 *target= o->target;
 *text_2= o->text_2;
 *user= o->user;
 *group= o->group;
 *mode_and= o->mode_and;
 *mode_or= o->mode_or;
 *type= o->type;
 *date= o->date;
 *subjob= o->subjob;
 return(o->action);
}


int Findjob_test_2(struct XorrisO *xorriso, struct FindjoB *o,
                   void *node, char *name, char *path,
                   struct stat *boss_stbuf, struct stat *stbuf, int flag)
{
 int ret;

 ret= Exprnode_tree_value(xorriso, o->test_tree, -1,
                          node, name, path, boss_stbuf, stbuf, 0);
 if(ret == 3)
   ret= 1;
 else if(ret == 2)
   ret= 0;
 return(ret);
}


int Findjob_set_action_target(struct FindjoB *o, int action, char *target,
                              int flag)
{
 o->action= action;
 o->target= target;
 return(1);
}


int Findjob_set_action_type(struct FindjoB *o, int action, int type,
                              int flag)
{
 o->action= action;
 o->type= type;
 return(1);
}


int Findjob_set_action_text_2(struct FindjoB *o, int action, char *target,
                              char* text_2, int flag)
{
 o->action= action;
 o->target= target;
 o->text_2= text_2;
 return(1);
}


/* @param flag bit0= recursive
*/
int Findjob_set_action_chown(struct FindjoB *o, uid_t user,int flag)
{
 int ret;

 if(flag&1) {
   o->action= 0;
   Findjob_destroy(&(o->subjob), 0);
   ret= Findjob_new(&(o->subjob), "", 0);
   if(ret<=0)
     return(-1);
   Findjob_set_action_chown(o->subjob, user, 0);
   o->action= 9;
 } else {
   o->action= 4;
   o->user= user;
 }
 return(1);
}


/* @param flag bit0= recursive
*/
int Findjob_set_action_chgrp(struct FindjoB *o, gid_t group, int flag)
{
 int ret;

 if(flag&1) {
   o->action= 0;
   Findjob_destroy(&(o->subjob), 0);
   ret= Findjob_new(&(o->subjob), "", 0);
   if(ret<=0)
     return(-1);
   Findjob_set_action_chgrp(o->subjob, group, 0);
   o->action= 10;
 } else {
   o->action= 5;
   o->group= group;
 }
 return(1);
}


/* @param flag bit0= recursive
*/
int Findjob_set_action_chmod(struct FindjoB *o,
                             mode_t mode_and, mode_t mode_or, int flag)
{
 int ret;

 if(flag&1) {
   o->action= 0;
   Findjob_destroy(&(o->subjob), 0);
   ret= Findjob_new(&(o->subjob), "", 0);
   if(ret<=0)
     return(-1);
   Findjob_set_action_chmod(o->subjob, mode_and, mode_or, 0);
   o->action= 11;
 } else {
   o->action= 6;
   o->mode_and= mode_and;
   o->mode_or= mode_or;
 }
 return(1);
}


/* @param flag bit0= recursive
*/
int Findjob_set_action_ad(struct FindjoB *o, int type, time_t date, int flag)
{
 int ret;

 if(flag&1) {
   o->action= 0;
   Findjob_destroy(&(o->subjob), 0);
   ret= Findjob_new(&(o->subjob), "", 0);
   if(ret<=0)
     return(-1);
   Findjob_set_action_ad(o->subjob, type, date, 0);
   o->action= 12;
 } else {
   o->action= 7;
   o->type= type;
   o->date= date;
 }
 return(1);
}


int Findjob_set_action_subjob(struct FindjoB *o, int action,
                              struct FindjoB *subjob, int flag)
{
 o->action= action;
 Findjob_destroy(&(o->subjob), 0);
 o->subjob= subjob;
 return(1);
}


int Findjob_set_action_found_path(struct FindjoB *o, int flag)
{
 o->action= 23;
 Findjob_set_found_path(o, NULL, 0);
 return(1);
}

