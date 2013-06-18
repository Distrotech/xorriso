
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2013 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the implementation of text i/o functions.
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
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>


/* for -charset */
#include <iconv.h>
#include <langinfo.h>
#include <locale.h>


#ifdef Xorriso_with_readlinE
#ifdef Xorriso_with_old_readlinE
#include <readline.h>
#include <history.h>
#else /* Xorriso_with_old_readlinE */
#include <readline/readline.h>
#include <readline/history.h>
#endif /* ! Xorriso_with_old_readlinE */
#endif /* Xorriso_with_readlinE */


#include "xorriso.h"
#include "xorriso_private.h"
#include "xorrisoburn.h"


int Xorriso_protect_stdout(struct XorrisO *xorriso, int flag)
{
 if(xorriso->dev_fd_1>=0)
   return(2);
 xorriso->dev_fd_1= dup(1);
 close(1);
 dup2(2,1);
 return(1);
}


int Xorriso_dialog_input(struct XorrisO *xorriso, char line[], int linesize,
                         int flag)
/*
 bit0= do not write to history
 bit1= do not read input (but eventually write to history)
 bit2= do not write to history line which begin with "-history:" or "-history "
 bit3= enforce single line dialog mode
*/
{
 char *cpt= NULL, **argv= NULL, *linept, *why_append= "";
 int ret, argc= 0, base_length= 0, l, append_line;
#ifdef Xorriso_with_readlinE
 static char last_input[SfileadrL]= {""};
#endif /* ! Xorriso_with_readlinE */
 double tdiff;
 struct timeval tv;
 struct timezone tz;

 gettimeofday(&tv,&tz);
 tdiff= tv.tv_sec+(1.e-6*(double) tv.tv_usec);

 fflush(stdout);
 linept= line;

get_single:;
#ifdef Xorriso_with_readlinE

 if(xorriso->use_stdin || xorriso->dev_fd_1>=0) {
   if(flag&2)
     {ret= 1; goto ex;}
   if(Sfile_fgets_n(linept,linesize - base_length - 1, stdin,
                      (xorriso->dialog == 2)) == NULL) {
     /* need a very dramatic end */
     kill(getpid(),SIGHUP);
     {ret= -1; goto ex;}
   }
   goto process_single;
 }
 if(flag&2) {
   cpt= NULL;
 } else {
   cpt= readline("");
   if(cpt==NULL) {
     /* need a very dramatic end */
     kill(getpid(),SIGHUP);
     {ret= -1; goto ex;}
   }
   l= strlen(cpt);
   if(l >= linesize - base_length - 1) {
     strncpy(linept, cpt, linesize - 1);
     line[linesize - 1]= 0;
     sprintf(xorriso->info_text,"Input line too long !");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
     goto new_empty;
   } else 
     strcpy(linept, cpt);
 }

process_single:;

#else /*  Xorriso_with_readlinE */

 if(flag&2)
   {ret= 1; goto ex;}
 if(Sfile_fgets_n(linept, linesize - base_length - 1, stdin,
                  (xorriso->dialog == 2)) == NULL) {
   /* need a very dramatic end */
   kill(getpid(),SIGHUP);
   {ret= -1; goto ex;}
 }

#endif /* ! Xorriso_with_readlinE */

 if(xorriso->dialog == 2 && !(flag & 8)) {
   append_line= 0;
   if(linept != line && strcmp(linept, "@@@") == 0) {
     sprintf(xorriso->info_text, "Incomplete input line cleared by %s",
             linept);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE",0);
new_empty:;
     line[0]= 0;
     linept= line;
     sprintf(xorriso->info_text, "-------------------------------------\n");
     Xorriso_info(xorriso,0);
     sprintf(xorriso->info_text, "Enter new text for empty input line :\n");
     Xorriso_info(xorriso,0);
     goto get_single;
   }
   l= strlen(line);
   ret= Sfile_make_argv("", line, &argc, &argv, 16);
   if(ret < 0)
     goto ex;
   if(ret == 0 && !append_line) {
     /* append a newline character */
     if(l >= linesize - 1) {
       sprintf(xorriso->info_text,"Input line too long !");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
       goto new_empty;
     }
     line[l]= '\n';
     line[l + 1]= 0;
     append_line= 1;
     why_append= "Quoted newline char";
   }
   if(l > 0 && !append_line)
     if(line[l - 1] == '\\') {
       line[l - 1]= 0;
       append_line= 1;
       why_append= "Trailing backslash ";
     }
   if(append_line) {
     base_length= strlen(line);
     linept= line + base_length;
     sprintf(xorriso->info_text,
          "---------------------------------------------------------------\n");
     Xorriso_info(xorriso,0);
     sprintf(xorriso->info_text,
             "%s : Enter rest of line (or @@@ to clear it) :\n", why_append);
     Xorriso_info(xorriso,0);
     goto get_single;
   }
 }

#ifdef Xorriso_with_readlinE

 if(line[0]!=0 && strcmp(last_input,line)!=0 && !(flag&1))
   if(!((flag&4) && 
      (strncmp(line,"-history:",9)==0 || strncmp(line,"-history ",9)==0))) {
     add_history(line);
     strncpy(last_input,line,sizeof(last_input)-1);
     last_input[sizeof(last_input)-1]= 0;
   }

#endif /* ! Xorriso_with_readlinE */

 ret= 1;
ex:;
 if(cpt!=NULL)
   free(cpt);
 gettimeofday(&tv,&tz);
 xorriso->idle_time+= tv.tv_sec+(1.e-6*(double) tv.tv_usec)-tdiff;
 return(ret);
}


int Xorriso_request_confirmation(struct XorrisO *xorriso, int flag)
/*
 bit0= important operation going on: 
       demand confirmation of abort, only abort on @@@
 bit1= mark '@' and '@@' by return 4
 bit2= accept: i|n= ignore | do not remove , r|y= retry | remove , q|x= abort
 bit3= @@@ = 'done reading' rather than 'abort'
 bit4= in non-dialog mode return 6 rather than 1
*/
/* return: <=0 error
           1= go on | do not remove existing file
           2= abort
           3= redo request for confirmation
           4= see flag bit1
          (5= skip volume)
           6= retry failed operation | remove existing file
 */
{
 int ret;
 char *line= NULL, *cpt, *previous_line= NULL;
 char *abort_req_text,*abort_really_text;

 Xorriso_alloc_meM(line, char, SfileadrL);
 Xorriso_alloc_meM(previous_line, char, SfileadrL);

 if(!xorriso->dialog) {
   if(flag&16)
     {ret= 6; goto ex;}
   {ret= 1; goto ex;}
 }
 if(flag&8) {
   abort_req_text= "request to end";
   abort_really_text= "done reading";
 } else {
   abort_req_text= "request to abort";
   abort_really_text= "abort this command";
 }
 ret= Xorriso_dialog_input(xorriso,line, SfileadrL, 1);
 xorriso->result_line_counter= 0;
 xorriso->result_page_counter++;
 if(ret<=0)
   if(xorriso->result_page_length>0)
     xorriso->result_page_length= -xorriso->result_page_length;

 cpt= line;
 if(strcmp(cpt,"@@@")==0 ||
    strcmp(cpt,"x")==0 || strcmp(cpt,"X")==0 ||
    strcmp(cpt,"q")==0 || strcmp(cpt,"Q")==0) {
   if(flag&1) {
     strcpy(previous_line,cpt);
     sprintf(xorriso->info_text,
             "... [%s = %s registered. Really %s ? (y/n) ] ...\n",
             cpt,abort_req_text,abort_really_text);
     Xorriso_info(xorriso,0);
     ret= Xorriso_dialog_input(xorriso,line, SfileadrL, 1);
     if(ret<=0)
       goto ex;
     cpt= line;
     if(strcmp(cpt,previous_line)==0 || 
        ((*cpt=='Y' || *cpt=='y' || *cpt=='j' || *cpt=='J' || *cpt=='1') &&
           *(cpt+1)==0)) {
       xorriso->request_to_abort= 1;
       sprintf(xorriso->info_text,
               "------- ( %s confirmed )\n",abort_req_text);
       Xorriso_info(xorriso,0);
       {ret= 2; goto ex;}
     }
     sprintf(xorriso->info_text, "....... ( %s revoked )\n",abort_req_text);
     Xorriso_info(xorriso,0);
     {ret= 3; goto ex;}
   }
   xorriso->request_to_abort= 1;
   sprintf(xorriso->info_text,
"----------- [%s = request to abort registered. Operation ends ] ------------\n",
           cpt);
   Xorriso_info(xorriso,0);
   {ret= 2; goto ex;}
 } else if(*cpt=='@') {
   if(strcmp(cpt,"@@")==0) {
     goto klammer_affe;
     
   } else if(strcmp(cpt,"@")==0) {
klammer_affe:;
     if(xorriso->result_page_length>0)
       xorriso->result_page_length= -xorriso->result_page_length;
     if(flag&1) {
       sprintf(xorriso->info_text,
"... [@ = prompt suppression registered. Prompting disabled temporarily ] ...\n");
       Xorriso_info(xorriso,0);
     }

   } else {
     Xorriso_dialog_input(xorriso,cpt,strlen(line)+1,2); /* write to history */
     sprintf(xorriso->info_text,
 "--- Unrecognized input beginning with @. Please enter someting else.\n");
     Xorriso_info(xorriso,0);
     {ret= 3; goto ex;}
   }
   if(flag&2)
     {ret= 4; goto ex;}
   if(flag&1)
     {ret= 3; goto ex;}
   {ret= 1; goto ex;}
 } else if(flag&4) {

   if(strcmp(cpt,"i")==0 || strcmp(cpt,"I")==0 ||
      strcmp(cpt,"n")==0 || strcmp(cpt,"N")==0 ||
      *cpt==0) { 
     {ret= 1; goto ex;}
   } else if(strcmp(cpt,"r")==0 || strcmp(cpt,"R")==0 ||
             strcmp(cpt,"y")==0 || strcmp(cpt,"Y")==0) {
     {ret= 6; goto ex;}
   } else {
     /* >>> unknown input */
     sprintf(xorriso->info_text,
          "--- Please enter one of : empty line, i,n, r,y, q,x, @, @@@\n");
     Xorriso_info(xorriso,0);
     {ret= 3; goto ex;}
   }

 } else if(*cpt!=0 && !(flag&1)) {
   Xorriso_dialog_input(xorriso,cpt,strlen(line)+1,2); /* write to history */
   strcpy(xorriso->pending_option,cpt);
   xorriso->request_to_abort= 1;
   sprintf(xorriso->info_text,
"-------------- [ Input of option registered. Operation ends ] ---------------\n");
   Xorriso_info(xorriso,0);
   {ret= 2; goto ex;}

 } else if(*cpt!=0) {
   Xorriso_dialog_input(xorriso,cpt,strlen(line)+1,2); /* write to history */
   sprintf(xorriso->info_text,
           "--- Please enter one of : empty line, @, @@@\n");
   Xorriso_info(xorriso,0);
   {ret= 3; goto ex;}
 }
 ret= 1;
ex:;
 Xorriso_free_meM(line);
 Xorriso_free_meM(previous_line);
 return(ret);
}


/* @param flag bit0= quoted multiline mode
               bit1= release allocated memory and return 1
               bit2= with bit0: warn of empty text arguments
               bit3= deliver as single quoted text including all whitespace
                     and without any backslash interpretation
   @return -1=out of memory , 0=line format error , 1=ok, go on , 2=done
*/
int Xorriso_read_lines(struct XorrisO *xorriso, FILE *fp, int *linecount,
                       int *argc, char ***argv, int flag)
{
 char *line= NULL, *linept, *fgot;
 int l, base_length, append_line, ret, mem_linecount, i;

 Sfile_make_argv("", line, argc, argv, 2);
 if(flag & 2)
   {ret= 1; goto ex;}

 Xorriso_alloc_meM(line, char, 5 * SfileadrL + 2);

 mem_linecount= *linecount;
 linept= line;
 base_length= 0;
 while(1) {
   fgot= Sfile_fgets_n(linept, SfileadrL - base_length - 1, fp,
                       !!(flag & (1 | 8)));
   if(fgot == NULL) {
     if(ferror(fp))
       {ret= 0; goto ex;}
     if(linept != line) {
       sprintf(xorriso->info_text,"Open quotation mark at end of input");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       {ret= 0; goto ex;}
     }
     {ret= 2; goto ex;}
   }
   l= strlen(line);
   (*linecount)++;
   append_line= 0;
   if(flag & 1) { /* check whether the line is incomplete yet */
     ret= Sfile_make_argv("", line, argc, argv, 16);
     if(ret < 0)
       goto ex;
     if(ret == 0 && !append_line) {
       line[l]= '\n';
       line[l + 1]= 0;
       append_line= 1;
     }
     if(l > 0 && !append_line)
       if(line[l - 1] == '\\') {
         line[l - 1]= 0;
         append_line= 1;
       }
   }
   if(l >= SfileadrL) {
     sprintf(xorriso->info_text,"Input line too long !");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
   if(!append_line)
 break;
   base_length= strlen(line);
   linept= line + base_length;
 }
 if((flag & 1) && !(flag & 8)) {
   ret= Sfile_make_argv("", line, argc, argv,
                        1 | ((xorriso->bsl_interpretation & 3) << 5));
   if(ret < 0)
     goto ex;
   if(flag & 4)
     for(i= 0; i < *argc; i++) {
       if((*argv)[i][0] == 0) {
         sprintf(xorriso->info_text, "Empty text as quoted argument in ");
       } else if(strlen((*argv)[i]) >= SfileadrL) {
         (*argv)[i][SfileadrL - 1]= 0;
         sprintf(xorriso->info_text,
                 "Input text too long and now truncated in");
       } else
     continue;
       if(mem_linecount + 1 < *linecount)
         sprintf(xorriso->info_text + strlen(xorriso->info_text),
                 "lines %d to %d", mem_linecount + 1, *linecount);
       else
         sprintf(xorriso->info_text + strlen(xorriso->info_text),
                 "line %d", mem_linecount + 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
     }
 } else {
   (*argv)= Smem_malloC(sizeof(char *));
   if(*argv == NULL)
     {ret= -1; goto ex;}
   (*argv)[0]= strdup(line);
   if((*argv)[0] == NULL)
     {ret= -1; goto ex;}
   *argc= 1;
 }
 ret= 1;
ex:;
 Xorriso_free_meM(line);
 return(ret);
}


int Xorriso_predict_linecount(struct XorrisO *xorriso, char *line,
                              int *linecount, int flag)
{
 int width,l;
 char *spt,*ept;

 *linecount= 0;
 spt= line;
 width= xorriso->result_page_width;
 while(1) {
   ept= strchr(spt,'\n');
   if(ept==NULL)
     l= strlen(spt);
   else
     l= ept-spt;
   l+= xorriso->result_open_line_len;
   if(ept!=NULL && l==0)
     (*linecount)++;
   else {
     (*linecount)+= l/width;
     if(ept==NULL) {
       xorriso->result_open_line_len= l%width;
 break;
     }
     (*linecount)+= !!(l%width);
   }
   xorriso->result_open_line_len= 0;
   spt= ept+1;
 }
 return(1);
}


int Xorriso_pager(struct XorrisO *xorriso, char *line, int flag)
/*
 bit1= mark '@' by return 4
*/
/* return: <=0 error , 1=go on , 2=abort , 4=see flag bit1*/
{
 int ret,linecount;
 char *info_text= NULL;

 if(xorriso->result_page_length<=0 || xorriso->request_not_to_ask ||
    xorriso->dialog == 0)
   {ret= 1; goto ex;}
 Xorriso_predict_linecount(xorriso,line,&linecount,0);
 if(xorriso->result_line_counter+linecount>xorriso->result_page_length) {
ask_for_page:;
   if(info_text == NULL)
     Xorriso_alloc_meM(info_text, char, 10*SfileadrL);
   strcpy(info_text,xorriso->info_text);
   sprintf(xorriso->info_text,"\n");
   Xorriso_info(xorriso,0);
   sprintf(xorriso->info_text,
".... [Press Enter to continue. @,Enter avoids further stops. @@@ aborts] ....\n");
   Xorriso_info(xorriso,0);
   ret= Xorriso_request_confirmation(xorriso,flag&2);
   strcpy(xorriso->info_text,info_text);
   if(ret<=0)
     goto ex;
   if(ret==2)
     {ret= 2; goto ex;}
   if(ret==3)
     goto ask_for_page;
 }
 xorriso->result_line_counter+= linecount;
 ret= 1;
ex:;
 Xorriso_free_meM(info_text);
 return(ret);
}


/* @param flag bit0= no error message in case of failure
*/
static int Xorriso_obtain_lock(struct XorrisO *xorriso,
                               pthread_mutex_t *lock_handle,
                               char *purpose, int flag)
{
 int ret;
 static int complaints= 0, complaint_limit= 5; 

 ret= pthread_mutex_lock(lock_handle);
 if(ret != 0) {
   if(flag & 1)
     return(-1);
   /* Cannot report failure through the failing message output system */
   complaints++;
   if(complaints <= complaint_limit)
     fprintf(stderr,
             "xorriso : pthread_mutex_lock() for %s returns %d\n",
             purpose, ret);
   return(-1);
 }
 return(1);
}


/* @param flag bit0= no error message in case of failure
*/
static int Xorriso_release_lock(struct XorrisO *xorriso,
                               pthread_mutex_t *lock_handle,
                               char *purpose, int flag)
{
 int ret;
 static int complaints= 0, complaint_limit= 5; 

 ret= pthread_mutex_unlock(lock_handle);
 if(ret != 0) {
   if(flag & 1)
     return(0);
   /* Cannot report failure through the failing message output system */
   complaints++;
   if(complaints <= complaint_limit)
     fprintf(stderr,
             "xorriso : pthread_mutex_unlock() for %s returns %d\n",
             purpose, ret);
   return(0);
 }
 return(1);
}


static int Xorriso_lock_outlists(struct XorrisO *xorriso, int flag)
{
 int ret;

 ret= Xorriso_obtain_lock(xorriso, &(xorriso->result_msglists_lock),
                          "outlists", 0);
 return(ret);
}


static int Xorriso_unlock_outlists(struct XorrisO *xorriso, int flag)
{
 int ret;

 ret= Xorriso_release_lock(xorriso, &(xorriso->result_msglists_lock),
                           "outlists", 0);
 return(ret);
}


static int Xorriso_write_to_msglist(struct XorrisO *xorriso,
                                    struct Xorriso_lsT **xorriso_msglist,
                                    char *text, int flag)
{
 int ret, locked= 0;
 struct Xorriso_lsT *msglist;

 ret= Xorriso_lock_outlists(xorriso, 0);
 if(ret > 0)
   locked= 1;
 msglist= *xorriso_msglist;
 ret= Xorriso_lst_append_binary(&msglist, text, strlen(text) + 1, 0);
 if(ret <= 0) {
   ret= -1; goto ex;
 }
 if(*xorriso_msglist == NULL)
   *xorriso_msglist= msglist;
 ret= 1;
ex:;
 if(locked)
   Xorriso_unlock_outlists(xorriso, 0);
 return(ret);
}


int Xorriso_write_to_channel(struct XorrisO *xorriso,
                             char *in_text, int channel_no, int flag)
/*
 bit0= eventually backslash encode linefeeds
 bit1= text is the name of the log file for the given channel 
 bit2= text is the name of the consolidated packet log file for all channels 
 bit3= text is the name of the stderr redirection file
bit15= with bit1 to bit3: close depicted log file
*/
{
 char *rpt, *npt, *text= NULL, *line= NULL;
 int ret= 1, info_redirected= 0, result_redirected= 0, l;
 char prefix[16];
 FILE *logfile_fp, *pktlog_fp;
 static int num_channels= 4;
 static char channel_prefixes[4][4]= {".","R","I","M"};
 int Xorriso_sieve_filter_msg(struct XorrisO *xorriso, char *msg, int flag);

#ifdef Xorriso_fetch_with_msg_queueS
 static int complaints= 0, complaint_limit= 5;
 int locked= 0, uret;
#endif

 text= in_text; /* might change due to backslash encoding */

 if(channel_no<0 || channel_no>=num_channels)
   {ret= -1; goto ex;}

#ifdef Xorriso_fetch_with_msg_queueS
 ret= pthread_mutex_lock(&(xorriso->write_to_channel_lock));
 if(ret != 0) {
   /* Cannot report failure through the failing message output system */
   complaints++;
   if(complaints <= complaint_limit)
     fprintf(stderr,
           "xorriso : pthread_mutex_lock() for write_to_channel returns %d\n",
           ret);
   /* Intentionally not aborting here */;
 } else
   locked= 1;
#endif /* Xorriso_fetch_with_msg_queueS */

 /* Logfiles */
 logfile_fp= xorriso->logfile_fp[channel_no];
 pktlog_fp= xorriso->pktlog_fp;
 if((flag&2) && logfile_fp!=NULL) {
   fprintf(logfile_fp,
     "! end ! end ! end ! end ! end ! end ! end ! end xorriso log : %s : %s\n",
           channel_prefixes[channel_no],Sfile_datestr(time(0),1|2|256));
   fclose(logfile_fp);
   xorriso->logfile_fp[channel_no]= logfile_fp= NULL;
 }
 if((flag&4) && pktlog_fp!=NULL) {
   fprintf(pktlog_fp,
 "I:1:! end ! end ! end ! end ! end ! end ! end ! end xorriso log : %s : %s\n",
           channel_prefixes[channel_no],Sfile_datestr(time(0),1|2|256));
   fclose(pktlog_fp);
   xorriso->pktlog_fp= pktlog_fp= NULL;
 }
 if((flag & 8) && xorriso->stderr_fp != NULL) {
   fclose(xorriso->stderr_fp);
   xorriso->stderr_fp= NULL;
 }
 if(flag&(1<<15))
   {ret= 1; goto ex;}
 if((flag&2)) {
   xorriso->logfile_fp[channel_no]= logfile_fp= fopen(text,"a");
   if(logfile_fp==NULL)
     {ret= 0; goto ex;}
   fprintf(logfile_fp,
  "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! xorriso log : %s : %s\n",
           channel_prefixes[channel_no],Sfile_datestr(time(0),1|2|256));
   fflush(logfile_fp);
 }
 if((flag&4)) {
   xorriso->pktlog_fp= pktlog_fp= fopen(text,"a");
   if(pktlog_fp==NULL)
     {ret= 0; goto ex;}
   fprintf(pktlog_fp,
  "I:1:!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! xorriso log : . : %s\n",
           Sfile_datestr(time(0),1|2|256));
   fflush(pktlog_fp);
 }
 if(flag & 8) {
   truncate(text, (off_t) 0);
   xorriso->stderr_fp= fopen(text, "a");
   if(xorriso->stderr_fp == NULL)
     {ret= 0; goto ex;}
 }
 if(flag & (2 | 4 | 8))
   {ret= 1; goto ex;}

 /* Eventually perform backslash encoding of non-printable characters */
 if(((xorriso->bsl_interpretation & 32) && channel_no == 1) ||
    ((xorriso->bsl_interpretation & 64) && channel_no == 2)) {
   ret= Sfile_bsl_encoder(&text, text, strlen(text), 1 | 2 | 4);
   if(ret <= 0)
     {ret= -1; goto ex;}
 }

 /* Pick interesting words for the Xorriso_sieve API */
 Xorriso_sieve_filter_msg(xorriso, text,
                          (channel_no > 0 ? channel_no - 1 : 0));

 /* Eventually perform message redirection */
 if(xorriso->msglist_stackfill > 0) {
   if(xorriso->msglist_flags[xorriso->msglist_stackfill - 1] & 1)
     result_redirected= 1;
   if(xorriso->msglist_flags[xorriso->msglist_stackfill - 1] & 2)
     info_redirected= 1;
 }

 /* Non-redirected output */
 if(!xorriso->packet_output) {
   if(channel_no==1 || channel_no==3) {
     if(result_redirected) {
       ret= Xorriso_write_to_msglist(xorriso,
                 &(xorriso->result_msglists[xorriso->msglist_stackfill - 1]),
                 text, 0);
       if(ret <= 0)
         { ret= -1; goto ex; }
     } else {
       printf("%s",text);
       fflush(stdout);
     }
   }
   if(channel_no==2 || channel_no==3) {
     if(info_redirected) {
       ret= Xorriso_write_to_msglist(xorriso,
                 &(xorriso->info_msglists[xorriso->msglist_stackfill - 1]),
                 text, 0);
       if(ret <= 0)
         { ret= -1; goto ex; }
     } else {
       if(xorriso->stderr_fp != NULL) {
         fprintf(xorriso->stderr_fp, "%s", text);
         fflush(xorriso->stderr_fp);
       } else
         fprintf(stderr, "%s", text);
     }
   }
   if(logfile_fp!=NULL) {
     fprintf(logfile_fp,"%s",text);
     fflush(logfile_fp);
   }
   if(pktlog_fp==NULL)
     {ret= 1; goto ex;}
 }
 rpt= text;
 sprintf(prefix,"%s:x: ",channel_prefixes[channel_no]);
 while(*rpt!=0) {
   npt= strchr(rpt,'\n');
   if(npt==NULL)
     prefix[2]= '0';
   else
     prefix[2]= '1';
   if(!result_redirected) {
     if(xorriso->packet_output) {
       ret= fwrite(prefix,5,1,stdout);
       if(ret<=0)
         {ret= 0; goto ex;}
     }
   }
   if(pktlog_fp!=NULL) {
     ret= fwrite(prefix,5,1,pktlog_fp);
     if(ret<=0)
       {ret= 0; goto ex;}
   }
   if(npt==NULL) {
     if(xorriso->packet_output) {
       if(result_redirected) {
         l= strlen(rpt);
         Xorriso_alloc_meM(line, char, 5 + l + 1 + 1);
         memcpy(line, prefix, 5);
         memcpy(line + 5, rpt, l);
         line[5 + l] = '\n';
         line[5 + l + 1] = 0;
         ret= Xorriso_write_to_msglist(xorriso,
                   &(xorriso->result_msglists[xorriso->msglist_stackfill - 1]),
                   line, 0);
         Xorriso_free_meM(line);
         line= NULL;
         if(ret <= 0)
           { ret= -1; goto ex; }
       } else {
         ret= fwrite(rpt,strlen(rpt),1,stdout);
         if(ret<=0)
           {ret= 0; goto ex;}
         ret= fwrite("\n",1,1,stdout);
         if(ret<=0)
           {ret= 0; goto ex;}
       }
     }
     if(pktlog_fp!=NULL) {
       ret= fwrite(rpt,strlen(rpt),1,pktlog_fp);
       if(ret<=0)
         {ret= 0; goto ex;}
       ret= fwrite("\n",1,1,pktlog_fp);
       if(ret<=0)
         {ret= 0; goto ex;}
     }
 break;
   } else {
     if(xorriso->packet_output) {
       if(result_redirected) {
         l= npt + 1 - rpt;
         Xorriso_alloc_meM(line, char, 5 + l + 1);
         memcpy(line, prefix, 5);
         memcpy(line + 5, rpt, l);
         line[5 + l] = 0;
         ret= Xorriso_write_to_msglist(xorriso,
                   &(xorriso->result_msglists[xorriso->msglist_stackfill - 1]),
                   line, 0);
         Xorriso_free_meM(line);
         line= NULL;
         if(ret <= 0)
           { ret= -1; goto ex; }
       } else {
         ret= fwrite(rpt,npt+1-rpt,1,stdout);
         if(ret<=0)
           {ret= 0; goto ex;}
       }
     }
     if(pktlog_fp!=NULL) {
       ret= fwrite(rpt,npt+1-rpt,1,pktlog_fp);
       if(ret<=0)
         {ret= 0; goto ex;}
     }
   }
   rpt= npt+1;
 }
 if(xorriso->packet_output)
   fflush(stdout);
 if(pktlog_fp!=NULL)
   fflush(pktlog_fp);
 ret= 1;
ex:
 if(text != in_text && text != NULL)
   free(text);
 Xorriso_free_meM(line);

#ifdef Xorriso_fetch_with_msg_queueS

 if(locked) {
   uret= pthread_mutex_unlock(&(xorriso->write_to_channel_lock));
   if(uret != 0) {
     /* Cannot report failure through the failing message output system */
     complaints++;
     if(complaints <= complaint_limit)
       fprintf(stderr,
          "xorriso : pthread_mutex_unlock() for write_to_channel returns %d\n",
          uret);
   }
 }

#endif /* Xorriso_fetch_with_msg_queueS */

 return(ret);
}


int Xorriso_push_outlists(struct XorrisO *xorriso, int *stack_handle,
                          int flag)
{
 int ret, locked= 0;

 ret= Xorriso_lock_outlists(xorriso, 0);
 if(ret > 0)
   locked= 1;
 if(xorriso->msglist_stackfill + 1 >= Xorriso_max_outlist_stacK) {
   Xorriso_msgs_submit(xorriso, 0,
                "Overflow of message output redirection stack", 0, "FATAL", 0);
   ret= -1; goto ex;
 }
 if((flag & 3) == 0)
   flag|= 3;
 xorriso->msglist_stackfill++;
 xorriso->result_msglists[xorriso->msglist_stackfill - 1]= NULL;
 xorriso->info_msglists[xorriso->msglist_stackfill - 1]= NULL;
 xorriso->msglist_flags[xorriso->msglist_stackfill - 1]= flag & 3;
 *stack_handle= xorriso->msglist_stackfill - 1;
 ret= 1;
ex:;
 if(locked)
   Xorriso_unlock_outlists(xorriso, 0);
 return(ret);
 return(1);
}


int Xorriso_fetch_outlists(struct XorrisO *xorriso, int stack_handle,
                           struct Xorriso_lsT **result_list,
                           struct Xorriso_lsT **info_list, int flag)
{
 int ret, locked= 0;

#ifdef Xorriso_fetch_with_msg_queueS

 ret= Xorriso_process_msg_queues(xorriso, 0);
 if(ret <= 0)
   goto ex;

#endif /* Xorriso_fetch_with_msg_queueS */

 if((flag & 3) == 0)
   flag|= 3;

 ret= Xorriso_lock_outlists(xorriso, 0);
 if(ret > 0)
   locked= 1;

 if(stack_handle == -1)
   stack_handle= xorriso->msglist_stackfill - 1;
 if(stack_handle < 0 || stack_handle >= xorriso->msglist_stackfill) {
   Xorriso_msgs_submit(xorriso, 0,
                "Program error: Wrong message output redirection stack handle",
                0, "FATAL", 0);
   ret= -1; goto ex;
 }

 if(flag & 1) {
   *result_list= xorriso->result_msglists[stack_handle];
   xorriso->result_msglists[stack_handle]= NULL;
 }
 if(flag & 2) {
   *info_list= xorriso->info_msglists[stack_handle];
   xorriso->info_msglists[stack_handle]= NULL;
 }

 ret= 1;
ex:;
 if(locked)
   Xorriso_unlock_outlists(xorriso, 0);
 return(ret);
}


int Xorriso_peek_outlists(struct XorrisO *xorriso, int stack_handle,
                          int timeout, int flag)
{
 int ret, locked= 0, yes= 0;
 static int u_wait= 19000;
 time_t start_time;

 if((flag & 3) == 0)
   flag|= 3;
 if(stack_handle == -1)
   stack_handle= xorriso->msglist_stackfill - 1;
 start_time= time(NULL);

try_again:;
 ret= Xorriso_obtain_lock(xorriso, &(xorriso->msgw_fetch_lock),
                          "message watcher fetch operation", 0);
 if(ret <= 0)
   {yes= -2; goto ex;}
 locked= 1;

 yes= 0;
 if(stack_handle < 0 || stack_handle >= xorriso->msglist_stackfill)
   {yes= -1; goto ex;}
 if(flag & 1)
   yes|= (xorriso->result_msglists[stack_handle] != NULL);
 if(flag & 2)
   yes|= (xorriso->info_msglists[stack_handle] != NULL);
 if(xorriso->msg_watcher_state == 2 && xorriso->msgw_msg_pending)
   yes|= 2;

 ret= Xorriso_release_lock(xorriso, &(xorriso->msgw_fetch_lock),
                           "message watcher fetch operation", 0);
 if(ret <= 0)
   {yes= -2; goto ex;}
 locked= 0;

 if(yes && (flag & 4)) {
   usleep(u_wait);
   if(time(NULL) <= start_time + timeout)
     goto try_again;
 }

ex:;
 if(locked) {
   ret= Xorriso_release_lock(xorriso, &(xorriso->msgw_fetch_lock),
                             "message watcher fetch operation", 0);
   if(ret <= 0 && yes >= 0)
     yes= -2;
 }
 return(yes);
}


int Xorriso_pull_outlists(struct XorrisO *xorriso, int stack_handle,
                          struct Xorriso_lsT **result_list,
                          struct Xorriso_lsT **info_list, int flag)
{
 int i, ret, locked= 0;

 ret= Xorriso_lock_outlists(xorriso, 0);
 if(ret > 0)
   locked= 1;

 if(stack_handle == -1)
   stack_handle= xorriso->msglist_stackfill - 1;
 if(stack_handle < 0 || stack_handle >= xorriso->msglist_stackfill) {
   Xorriso_msgs_submit(xorriso, 0,
                "Program error: Wrong message output redirection stack handle",
                0, "FATAL", 0);
   ret= -1; goto ex;
 }

 /* Concatenate all redirections above stack_handle */
 *result_list= NULL;
 *info_list= NULL;
 for(i = stack_handle; i < xorriso->msglist_stackfill; i++) {
   if(*result_list == NULL)
     *result_list= xorriso->result_msglists[i];
   else
     Xorriso_lst_concat(*result_list, xorriso->result_msglists[i], 0);
   if(*info_list == NULL)
     *info_list= xorriso->info_msglists[i];
   else
     Xorriso_lst_concat(*info_list, xorriso->info_msglists[i], 0);
 }
 xorriso->msglist_stackfill= stack_handle;

 ret= 1;
ex:;
 if(locked)
   Xorriso_unlock_outlists(xorriso, 0);
 return(ret);
}


int Xorriso_info_handler_stderr(void *handle, char *text)
{
 struct XorrisO *xorriso;

 xorriso= (struct XorrisO *) handle;
 if(xorriso->stderr_fp != NULL) {
   fprintf(xorriso->stderr_fp, "%s", text);
   fflush(xorriso->stderr_fp);
 } else {
   fprintf(stderr, "%s", text);
   fflush(stderr);
 }
 return(1);
}


int Xorriso_result_handler_stdout(void *handle, char *text)
{
 printf("%s", text);
 fflush(stdout);
 return(1);
}


int Xorriso_result_handler_pkt(void *handle, char *text)
{
 int nl= -1, ret, l;
 struct XorrisO *xorriso;

 xorriso= (struct XorrisO *) handle;

 if(!xorriso->packet_output)
   return Xorriso_result_handler_stdout(handle, text);

 /* Interpret pkt_output */
 l= strlen(text);
 if(l >= 5) {
   if(strchr("RIM", text[0]) != NULL && text[1] == ':' &&
      strchr("01", text[2]) != NULL && text[3] == ':' && text[4] == ' ')
     nl= (text[2] == '1');
 }
 if(nl < 0) /* Not pkt_output format */
   return Xorriso_result_handler_stdout(handle, text);

 if(nl == 0) {
   /* Suppress newline */
   if(text[l - 1] == '\n')
     l--;
 }

 if(text[0] == 'R') {
   ret= fwrite(text + 5, l - 5, 1, stdout);
 } else {
   ret= fwrite(text + 5, l - 5, 1,
               xorriso->stderr_fp != NULL ? xorriso->stderr_fp : stderr);
 }
 if(ret <= 0)
   return(0);
 return(1);
}


int Xorriso_process_msg_lists(struct XorrisO *xorriso,
                                     struct Xorriso_lsT *result_list,
                                     struct Xorriso_lsT *info_list,
                                     int *line_count, int flag)
{
 struct Xorriso_lsT *lpt;
 int ret;
 int (*handler)(void *handle, char *text);
 void *handle;

 handler= xorriso->msgw_result_handler;
 handle= xorriso->msgw_result_handle;
 if(handler == NULL) {
   handler= Xorriso_result_handler_pkt;
   handle= xorriso;
 }
 for(lpt= result_list; lpt != NULL; lpt= lpt->next) {
   (*line_count)++;
   ret= (*handler)(handle, Xorriso_lst_get_text(lpt, 0));
   if(ret < 0)
     return(-1);
 }
 handler= xorriso->msgw_info_handler;
 handle= xorriso->msgw_info_handle;
 if(handler == NULL) {
   handler= Xorriso_info_handler_stderr;
   handle= xorriso;
 }
 for(lpt= info_list; lpt != NULL; lpt= lpt->next) {
   (*line_count)++;
   ret= (*handler)(handle, Xorriso_lst_get_text(lpt, 0));
   if(ret < 0)
     return(-1);
 }
 return(1);
}


static void *Xorriso_msg_watcher(void *state_pt)
{
 struct XorrisO *xorriso;
 int ret, u_wait= 25000, line_count, sleep_thresh= 20;
 struct Xorriso_lsT *result_list= NULL, *info_list= NULL;
 static int debug_sev= 0;

 xorriso= (struct XorrisO *) state_pt;

 if(debug_sev == 0)
   Xorriso__text_to_sev("DEBUG", &debug_sev, 0);

 xorriso->msg_watcher_state= 2;
 if(xorriso->msgw_info_handler != NULL &&
    debug_sev < xorriso->report_about_severity &&
    debug_sev < xorriso->abort_on_severity)
   (*xorriso->msgw_info_handler)(xorriso,
                     "xorriso : DEBUG : Concurrent message watcher started\n");
 while(1) {
   line_count= 0;

   /* Watch out for end request in xorriso */
   if(xorriso->msg_watcher_state == 3)
 break;

   Xorriso_obtain_lock(xorriso, &(xorriso->msgw_fetch_lock),
                       "message watcher fetch operation", 1);
   xorriso->msgw_msg_pending= 1;
   ret= Xorriso_fetch_outlists(xorriso, -1, &result_list, &info_list, 3);
   if(ret > 0) {
     /* Process fetched lines */
     xorriso->msgw_msg_pending= 2;
     Xorriso_release_lock(xorriso, &(xorriso->msgw_fetch_lock),
                          "message watcher fetch operation", 1);
     ret= Xorriso_process_msg_lists(xorriso, result_list, info_list,
                                    &line_count, 0);
     xorriso->msgw_msg_pending= 0;
     Xorriso_lst_destroy_all(&result_list, 0);
     Xorriso_lst_destroy_all(&info_list, 0);
     if(ret < 0)
 break;
   } else {
     xorriso->msgw_msg_pending= 0;
     Xorriso_release_lock(xorriso, &(xorriso->msgw_fetch_lock),
                          "message watcher fetch operation", 1);
   }
   xorriso->msgw_msg_pending= 0;

   if(ret < 0)
 break;

   if(line_count < sleep_thresh)
     usleep(u_wait);
 }
 if(xorriso->msgw_info_handler != NULL &&
    debug_sev < xorriso->report_about_severity &&
    debug_sev < xorriso->abort_on_severity)
   (*xorriso->msgw_info_handler)(xorriso,
                     "xorriso : DEBUG : Concurrent message watcher ended\n");
 xorriso->msg_watcher_state= 0;
 return(NULL);
}


int Xorriso_start_msg_watcher(struct XorrisO *xorriso,
                    int (*result_handler)(void *handle, char *text),
                    void *result_handle,
                    int (*info_handler)(void *handle, char *text),
                    void *info_handle,
                    int flag)
{
 int ret, u_wait= 1000, locked= 0, pushed= 0, uret, line_count= 0;
 struct Xorriso_lsT *result_list= NULL, *info_list= NULL;
 pthread_attr_t attr;
 pthread_attr_t *attr_pt = NULL;
 pthread_t thread;

 ret= pthread_mutex_lock(&(xorriso->msg_watcher_lock));
 if(ret != 0) {
   Xorriso_msgs_submit(xorriso, 0,
            "Cannot aquire mutex lock for managing concurrent message watcher",
            ret, "FATAL", 0);
   ret= -1; goto ex;
 }
 locked= 1;

 /* Check for running watcher */
 if(xorriso->msg_watcher_state > 0) {
   sprintf(xorriso->info_text,
          "There is already a concurrent message watcher running");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
   ret= 0; goto ex;
 }

 ret= Xorriso_push_outlists(xorriso, &(xorriso->msgw_stack_handle), 3);
 if(ret <= 0)
   goto ex;
 pushed= 1;

 /* Register watcher */
 xorriso->msgw_result_handler= result_handler;
 xorriso->msgw_result_handle= result_handle;
 xorriso->msgw_info_handler= info_handler;
 xorriso->msgw_info_handle= info_handle;
 xorriso->msg_watcher_state= 1;

 /* Start thread */
 pthread_attr_init(&attr);
 pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
 attr_pt= &attr;
 ret= pthread_create(&thread, attr_pt, Xorriso_msg_watcher, xorriso);
 if(ret != 0) {
   sprintf(xorriso->info_text,
          "Cannot create thread for concurrent message watcher");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
   ret= 0; goto ex;
 }

 /* Wait until watcher has indicated start */
 while(xorriso->msg_watcher_state == 1) {

   /* >>> have a timeout ? */;

   usleep(u_wait);
 }

 ret= 1;
ex:;
 if(ret <= 0 && pushed) {
   uret= Xorriso_pull_outlists(xorriso, xorriso->msgw_stack_handle,
                              &result_list, &info_list, 0);
   if(uret > 0) {
     xorriso->msgw_result_handler= NULL;
     xorriso->msgw_info_handler= NULL;
     Xorriso_process_msg_lists(xorriso, result_list, info_list,
                               &line_count, 0);
     Xorriso_lst_destroy_all(&result_list, 0);
     Xorriso_lst_destroy_all(&info_list, 0);
   }
 }
 if(locked) {
   uret= pthread_mutex_unlock(&(xorriso->msg_watcher_lock));
   if(uret != 0) {
     Xorriso_msgs_submit(xorriso, 0,
           "Cannot release mutex lock for managing concurrent message watcher",
           uret, "FATAL", 0);
     ret= -1;
   }
 }
 return(ret);
}


/* @param flag bit0= do not complain loudly if no wather is active
*/
int Xorriso_stop_msg_watcher(struct XorrisO *xorriso, int flag)
{
 int ret, u_wait= 1000, locked= 0, uret, line_count= 0;
 struct Xorriso_lsT *result_list= NULL, *info_list= NULL;

 if((flag & 1) && xorriso->msg_watcher_state != 2)
   /* Roughly tolerate non-running watcher */
   {ret= 0; goto ex;}

 ret= pthread_mutex_lock(&(xorriso->msg_watcher_lock));
 if(ret != 0) {
   Xorriso_msgs_submit(xorriso, 0,
            "Cannot aquire mutex lock for managing concurrent message watcher",
            ret, "FATAL", 0);
   ret= -1; goto ex;
 }
 locked= 1;

 /* Check for running watcher */
 if(xorriso->msg_watcher_state != 2) {
   sprintf(xorriso->info_text,
          "There is no concurrent message watcher running");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "SORRY", 0);
   ret= 0; goto ex;
 }

 /* Inform watcher of desire to stop it */
 xorriso->msg_watcher_state= 3;

 /* Wait until watcher has indicated its end */
 while(xorriso->msg_watcher_state != 0) {

   /* >>> have a timeout ? */;

   usleep(u_wait);
 }

 Xorriso_obtain_lock(xorriso, &(xorriso->msgw_fetch_lock),
                     "message watcher fetch operation", 1);
 xorriso->msgw_msg_pending= 1;
 ret= Xorriso_pull_outlists(xorriso, xorriso->msgw_stack_handle,
                            &result_list, &info_list, 0);
 if(ret > 0) {
   xorriso->msgw_msg_pending= 2;
   Xorriso_release_lock(xorriso, &(xorriso->msgw_fetch_lock),
                        "message watcher fetch operation", 1);
   Xorriso_process_msg_lists(xorriso, result_list, info_list,
                             &line_count, 0);
   xorriso->msgw_msg_pending= 0;
   Xorriso_lst_destroy_all(&result_list, 0);
   Xorriso_lst_destroy_all(&info_list, 0);
 } else {
   xorriso->msgw_msg_pending= 0;
   Xorriso_release_lock(xorriso, &(xorriso->msgw_fetch_lock),
                        "message watcher fetch operation", 1);
 }

 xorriso->msgw_result_handler= NULL;
 xorriso->msgw_info_handler= NULL;

 ret= 1;
ex:;
 if(locked) {
   uret= pthread_mutex_unlock(&(xorriso->msg_watcher_lock));
   if(uret != 0) {
     Xorriso_msgs_submit(xorriso, 0,
           "Cannot release mutex lock for managing concurrent message watcher",
           uret, "FATAL", 0);
     ret= -1;
   }
 }
 return(ret);
}


/* -------------------------- Xorriso_msg_sievE -------------------------- */

struct Xorriso_msg_filteR {
 char *name;
 char *prefix;
 char *separators;
 int channels;         /* What to watch: bit0=result , bit1=info , bit2=mark */

 int num_words;
 int *word_idx;
 int last_word_line_end;

                /* Oldest result gets discarded when new surpassed threshold */
 int max_results;

 struct Xorriso_lsT *results; /* Serialized tuples of num_words */
 int num_results;
 int num_delivered;
 struct Xorriso_lsT *next_to_deliver;

 struct Xorriso_msg_filteR *prev;
 struct Xorriso_msg_filteR *next;
};
int Xorriso_msg_filter_destroy(struct Xorriso_msg_filteR **o, int flag);


int Xorriso_msg_filter_new(struct Xorriso_msg_filteR **o, char *name,
                           struct Xorriso_msg_filteR *prev,
                           struct Xorriso_msg_filteR *next,
                           int flag)
{
 struct Xorriso_msg_filteR *m;

 m= (*o)= TSOB_FELD(struct Xorriso_msg_filteR, 1);
 if((*o) == NULL)
   return(-1);
 m->name= NULL;
 m->prefix= NULL;
 m->separators= NULL;
 m->channels= 7;
 m->num_words= 0;
 m->word_idx= NULL;
 m->last_word_line_end= flag & 1;
 m->max_results= 1;
 m->results= NULL;
 m->num_results= 0;
 m->num_delivered= 0;
 m->next_to_deliver= NULL;

 m->name= strdup(name);
 if(m->name == NULL)
   goto failure;

 m->prev= prev;
 if(prev != NULL)
   prev->next= m;
 m->next= next;
 if(next != NULL)
   next->prev= m;
 return(1);
failure:
 Xorriso_msg_filter_destroy(o, 0);
 return(-1);
}


int Xorriso_msg_filter_destroy(struct Xorriso_msg_filteR **o, int flag)
{
 struct Xorriso_msg_filteR *m;

 if((*o)==NULL)
   return(0);
 m= *o;
 if(m->name != NULL)
   free(m->name);
 if(m->prefix != NULL)
   free(m->prefix);
 if(m->separators != NULL)
   free(m->separators);
 if(m->word_idx != NULL)
   free((char *) m->word_idx);
 if(m->results != NULL)
   Xorriso_lst_destroy_all(&(m->results), 0);
 if(m->prev != NULL)
   m->prev->next= m->next;
 if(m->next != NULL)
   m->next->prev= m->prev;

 free(*o);
 *o= NULL;
 return(1);
}


int Xorriso_msg_filter_set_words(struct Xorriso_msg_filteR *m,
                                 int num_words, int *word_idx, int flag)
{
 int i;

 if(m->word_idx != NULL)
   free(m->word_idx);
 m->num_words= 0;
 if(num_words <= 0)
   return(1);
 m->word_idx= TSOB_FELD(int, num_words);
 if(m->word_idx == NULL)
   return(-1);
 for(i= 0; i < num_words; i++)
   m->word_idx[i]= word_idx[i];
 m->num_words= num_words;
 return(1);
}


struct Xorriso_msg_sievE {

 int num_filters;

 struct Xorriso_msg_filteR *first_filter;

};


int Xorriso_msg_sieve_new(struct Xorriso_msg_sievE **o, int flag)
{
 struct Xorriso_msg_sievE *m;

 m= (*o)= TSOB_FELD(struct Xorriso_msg_sievE, 1);
 if((*o) == NULL)
   return(-1);
 m->num_filters= 0;
 m->first_filter= NULL;
 return(1);
}


int Xorriso_msg_sieve_destroy(struct Xorriso_msg_sievE **o, int flag)
{
 struct Xorriso_msg_sievE *m;
 struct Xorriso_msg_filteR *f, *next_f= NULL;

 if((*o) == NULL)
   return(0);
 m= *o;
 for(f= m->first_filter; f != NULL; f= next_f) {
   next_f= f->next;
   Xorriso_msg_filter_destroy(&f, 0);
 }
 free(*o);
 *o= NULL;
 return(1);
}


/* API */
int Xorriso_sieve_add_filter(struct XorrisO *xorriso, char *name,
                             int channels, char *prefix, char *separators,
                             int num_words, int *word_idx, int max_results,
                             int flag)
{
 int ret;
 struct Xorriso_msg_sievE *sieve= NULL;
 struct Xorriso_msg_filteR *filter;

 if(xorriso->msg_sieve == NULL) {
   ret= Xorriso_msg_sieve_new(&sieve, 0);
   if(ret <= 0)
     goto no_mem;
   xorriso->msg_sieve= sieve;
 } else
   sieve= xorriso->msg_sieve;
 ret= Xorriso_msg_filter_new(&filter, name, NULL, sieve->first_filter,
                             flag & 1);
 if(ret <= 0)
   goto no_mem;
 sieve->first_filter= filter;
 ret= Xorriso_msg_filter_set_words(filter, num_words, word_idx, 0);
 if(ret <= 0)
   goto no_mem;
 if(prefix != NULL)
   filter->prefix= strdup(prefix);
 if(separators != NULL)
   filter->separators= strdup(separators);
 filter->channels= channels;
 filter->max_results= max_results;
 (sieve->num_filters)++;
 return(1);

no_mem:;
 Xorriso_msg_sieve_destroy(&sieve, 0);
 Xorriso_no_malloc_memory(xorriso, NULL, 0);
 return(-1);
}


/* API */
int Xorriso_sieve_dispose(struct XorrisO *xorriso, int flag)
{
 Xorriso_msg_sieve_destroy(&(xorriso->msg_sieve), 0);
 return(1);
}


/* API */
int Xorriso_sieve_clear_results(struct XorrisO *xorriso, int flag)
{
 struct Xorriso_msg_filteR *f;

 if(xorriso->msg_sieve == NULL)
   return(1);
 for(f= xorriso->msg_sieve->first_filter; f != NULL; f= f->next) {
   f->num_results= 0;
   f->num_delivered= 0;
   if(f->results != NULL)
     Xorriso_lst_destroy_all(&(f->results), 0);
   f->next_to_deliver= NULL;
 }
 return(1);
}


/* API */
/* @param flag bit0= Reset reading to first matching result
               bit1= Only inquire number of available results.
                     Do not allocate memory.
               bit2= If *argv is not NULL, then free it before attaching
                     new memory.
               bit3= Do not read recorded data but rather list all filter names
*/
int Xorriso_sieve_get_result(struct XorrisO *xorriso, char *name,
                             int *argc, char ***argv, int *available, int flag)
{
 struct Xorriso_msg_filteR *f;
 struct Xorriso_lsT *lst;
 int i;

 if(flag & 4)
   Xorriso__dispose_words(argc, argv);
 *argc= 0;
 *argv= NULL;

 if(xorriso->msg_sieve == NULL)
   return(0); 

 if(flag & 8) {
   if(xorriso->msg_sieve->num_filters <= 0)
     return(0);
   *argv= calloc(xorriso->msg_sieve->num_filters, sizeof(char *));
   if(*argv == NULL)
     goto no_mem;
   *argc= xorriso->msg_sieve->num_filters;
   for(i= 0; i < *argc; i++)
     (*argv)[i]= NULL;
   i= 0;
   for(f= xorriso->msg_sieve->first_filter; f != NULL; f= f->next) {
     (*argv)[*argc - i - 1]= strdup(f->name);
     if((*argv)[*argc - i - 1] == NULL)
       goto no_mem;
     i++;
   }
   *argc= i;
   return(1);
 }

 for(f= xorriso->msg_sieve->first_filter; f != NULL; f= f->next) {
   if(strcmp(f->name, name) != 0)
 continue;
   *available= f->num_results - f->num_delivered;
   if(*available <= 0)
     return(0);
   if(flag & 2)
     return(1);

   if(flag & 1) {
     f->num_delivered= 0;
     f->next_to_deliver= NULL;
   }
   if(f->next_to_deliver == NULL) {
     f->next_to_deliver= f->results;
     for(i= 0; i < f->num_words * f->num_delivered; i++)
       if(f->next_to_deliver != NULL)
         f->next_to_deliver= Xorriso_lst_get_next(f->next_to_deliver, 0);
   }
   if(f->next_to_deliver == NULL) {
     /* Should not happen */
     *available= 0;
 break;
   }
   if(f->num_words <= 0)
     return(1);

   *argv= calloc(f->num_words, sizeof(char *));
   if(*argv == NULL)
     goto no_mem;
   *argc= f->num_words;
   for(i= 0; i < *argc; i++)
     (*argv)[i]= NULL;

   lst= f->next_to_deliver;
   for(i= 0; i < *argc; i++) {
     if(lst != NULL) {
       (*argv)[i]= strdup(Xorriso_lst_get_text(lst, 0));
       if((*argv)[i] == NULL)
         goto no_mem;
     } else {

       /* >>> ??? should not happen */;

     }
     lst= Xorriso_lst_get_next(lst, 0);
   }
   f->next_to_deliver= lst;
   (f->num_delivered)++;
   (*available)--;
   return(1);
 }
 return(-2);
no_mem:
 if(*argv != NULL)
   Xorriso__dispose_words(argc, argv);
 Xorriso_no_malloc_memory(xorriso, NULL, 0);
 return(-1);
}


int Xorriso_sieve_big(struct XorrisO *xorriso, int flag)
{
 struct Xorriso_sieve_big_filteR {
   char *name;
   int channels;
   char *prefix;
   char *separators;
   int num_words;
   int word_idx[6];
   int max_results;
   int flag;
 };
 static struct Xorriso_sieve_big_filteR filters[] = {
   {"-changes_pending", 3, "-changes_pending", "", 1,
                                               { 0, -1, -1, -1, -1, -1}, 1, 0},
   {"?  -dev", 3, "?  -dev", "", 4, { 0,  1,  3,  4, -1, -1},
                                                                        10, 0},
   {"??  -dev", 3, "??  -dev", "", 4, { 0,  1,  3,  4, -1, -1},
                                                                        90, 0},
   {"Abstract File:", 3, "Abstract File: ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 1},
   {"After commit :", 3, "After commit :", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 0},
   {"App Id       :", 3, "App Id       : ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 1},
   {"Biblio File  :", 3, "Biblio File  : ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 1},
   {"Build timestamp   :", 3, "Build timestamp   :  ", "", 1,
                                               { 0, -1, -1, -1, -1, -1}, 1, 1},
   {"CopyrightFile:", 3, "CopyrightFile: ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 1},
   {"Creation Time:", 3, "Creation Time: ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 1},
   {"DVD obs 64 kB:", 3, "DVD obs 64 kB:", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 0},
   {"Drive current:", 3, "Drive current:", "", 2, { 0,  1, -1, -1, -1, -1},
                                                                         2, 0},
   {"Drive type   :", 3, "Drive type   :", "", 3, { 1,  3,  5, -1, -1, -1},
                                                                         2, 0},
   {"Eff. Time    :", 3, "Eff. Time    : ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 1},
   {"Expir. Time  :", 3, "Expir. Time  : ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 1},
   {"Ext. filters :", 3, "Ext. filters : ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 1},
   {"File damaged :", 3, "File damaged :", "", 4, { 0,  2,  4,  6, -1, -1},
                                                                     10000, 0},
   {"File data lba:", 3, "File data lba:", "", 5, { 0,  2,  4,  6,  8, -1},
                                                                     10000, 0},
   {"Format idx   :", 3, "Format idx ", ",: ", 4, { 0,  1,  2,  3, -1, -1},
                                                                       100, 1},
   {"Format status:", 3, "Format status:", ", ", 2, { 0,  1, -1, -1, -1, -1},
                                                                         1, 1},
   {"ISO session  :", 3, "ISO session  :", "", 4, { 0,  2,  4,  6, -1, -1},
                                                                     10000, 1},
   {"Image size   :", 3, "Image size   :", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 0},
   {"Jigdo files  :", 3, "Jigdo files  :", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 0},
   {"Local ACL    :", 3, "Local ACL    :", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 0},
   {"Local xattr  :", 3, "Local xattr  :", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 0},
   {"MD5 MISMATCH:", 3, "MD5 MISMATCH:", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                     10000, 0},
   {"MD5 tag range:", 3, "MD5 tag range:", "", 3, { 0,  2,  4, -1, -1, -1},
                                                                     10000, 1},
   {"Media blocks :", 3, "Media blocks :", "", 3, { 0,  3,  6, -1, -1, -1},
                                                                         2, 0},
   {"Media current:", 3, "Media current: ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         2, 1},
   {"Media nwa    :", 3, "Media nwa    :", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 0},
   {"Media product:", 3, "Media product:", "", 2, { 0,  2, -1, -1, -1, -1},
                                                                         2, 1},
   {"Media region :", 3, "Media region :", "", 3, { 0,  2,  4, -1, -1, -1},
                                                                     10000, 1},
   {"Media space  :", 3, "Media space  :", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 0},
   {"Media status :", 3, "Media status : ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         2, 1},
   {"Media summary:", 3, "Media summary:", "", 4, { 0,  2,  5,  7, -1, -1},
                                                                         2, 0},
   {"Modif. Time  :", 3, "Modif. Time  : ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 1},
   {"PVD address  :", 3, "PVD address  :", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 0},
   {"Preparer Id  :", 3, "Preparer Id  : ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 1},
   {"Profile      :", 3, "Profile      :", "", 2, { 0,  1, -1, -1, -1, -1},
                                                                       256, 1},
   {"Publisher Id :", 3, "Publisher Id : ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 1},
   {"Readline     :", 3, "Readline     :", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 0},
   {"Size lower   :", 3, "Size lower   :", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 0},
   {"Size upper   :", 3, "Size upper   :", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 0},
   {"System Id    :", 3, "System Id    : ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 1},
   {"Version timestamp :", 3, "Version timestamp :", "", 1,
                                               { 0, -1, -1, -1, -1, -1}, 1, 0},
   {"Volume Id    :", 3, "Volume Id    : ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 1},
   {"Volume Set Id:", 3, "Volume Set Id: ", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 1},
   {"Volume id    :", 3, "Volume id    :", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         2, 0},
   {"Write speed  :", 3, "Write speed  :", "", 2, { 0,  2, -1, -1, -1, -1},
                                                                       100, 0},
   {"Write speed H:", 3, "Write speed H:", "", 2, { 0,  2, -1, -1, -1, -1},
                                                                         1, 0},
   {"Write speed L:", 3, "Write speed L:", "", 2, { 0,  2, -1, -1, -1, -1},
                                                                         1, 0},
   {"Write speed h:", 3, "Write speed h:", "", 2, { 0,  2, -1, -1, -1, -1},
                                                                         1, 0},
   {"Write speed l:", 3, "Write speed l:", "", 2, { 0,  2, -1, -1, -1, -1},
                                                                         1, 0},
   {"libburn    in use :", 3, "libburn    in use :", "", 2,
                                               { 0,  1, -1, -1, -1, -1}, 1, 1},
   {"libburn OS adapter:", 3, "libburn OS adapter:  ", "", 1,
                                               { 0, -1, -1, -1, -1, -1}, 1, 1},
   {"libisoburn in use :", 3, "libisoburn in use :", "", 2,
                                               { 0,  1, -1, -1, -1, -1}, 1, 1},
   {"libisofs   in use :", 3, "libisofs   in use :", "", 2,
                                               { 0,  1, -1, -1, -1, -1}, 1, 1},
   {"libjte     in use :", 3, "libjte     in use :", "", 2,
                                               { 0,  1, -1, -1, -1, -1}, 1, 1},
   {"xorriso version   :", 3, "xorriso version   :", "", 1,
                                               { 0, -1, -1, -1, -1, -1}, 1, 0},
   {"zisofs       :", 3, "zisofs       :", "", 1, { 0, -1, -1, -1, -1, -1},
                                                                         1, 0},
   {"@", 0, "@", "", 0, {-1, -1, -1, -1, -1, -1}, 0, 0}
 };

 struct Xorriso_sieve_big_filteR *f;
 int ret, i, num_filters= 1000;

 for(i= 0; i < num_filters; i++) {
   f= &(filters[i]);
   if(strcmp(f->name, "@") == 0)
 break;
   ret= Xorriso_sieve_add_filter(xorriso, f->name, f->channels, f->prefix,
                                 f->separators, f->num_words, f->word_idx,
                                 f->max_results, f->flag);
   if(ret <= 0)
     goto failure;
 }
 return(1);
failure:
 Xorriso_sieve_dispose(xorriso, 0);
 return(-1);
}


/* Check for matching filter and eventually extract words.
   To be called by Xorriso_result, Xorriso_info, Xorriso_mark,
   and alike.
   Thus no own message output is allowed here !
   @param flag bit0-1= channel:
                       0= result channel
                       1= info channel
                       2= mark channel
*/
int Xorriso_sieve_filter_msg(struct XorrisO *xorriso, char *msg, int flag)
{
 int channel, ret, argc= 0, i, max_words, l, widx, skip;
 char **argv= NULL, *prefix_storage= NULL, *prefix, *cpt, *to_parse= NULL;
 struct Xorriso_msg_filteR *f;
 struct Xorriso_lsT *lst, *prev_lst, *next_lst;

 if(xorriso->msg_sieve == NULL || xorriso->msg_sieve_disabled)
   return(1);

 channel= flag & 3;

 for(f= xorriso->msg_sieve->first_filter; f != NULL; f= f->next) {
   if(!(f->channels & (1 << channel)))
 continue;
   prefix= f->prefix;

   if(prefix[0] == '?') {
     skip= 0;
     for(cpt= prefix; *cpt; cpt++)
       if(*cpt == '?')
         skip++;
       else
     break;
     l= strlen(prefix);
     if(strlen(msg) >= (unsigned int) l) {
       if(l - skip == 0 || strncmp(prefix + skip, msg + skip, l - skip) == 0) {
         Xorriso_alloc_meM(prefix_storage, char, l + 1);
         strncpy(prefix_storage, msg, l);
         prefix_storage[l]= 0;
         prefix= prefix_storage;
       }
     }
   }
   if(prefix[0])
     if(strncmp(prefix, msg, strlen(prefix)) != 0)
 continue;

   to_parse= strdup(msg);
   if(to_parse == NULL)
     goto no_mem;
   l= strlen(to_parse);
   if(l > 0)
     if(to_parse[l - 1] == '\n')
       to_parse[l - 1]= 0;

   max_words= 0;
   if(f->last_word_line_end)
     if(f->num_words > 0)                /* Let last word take rest of line */
       max_words= f->word_idx[f->num_words - 1];
   if(max_words <= 0 && f->last_word_line_end) {
     /* Copy rest of line as single word because Xorriso_parse_line understands
        max_words == 0 as unlimited number of words. But here it is desired
        to get the rest of line already in argv[0].
     */
     max_words= 0;
     argv= calloc(1, sizeof(char *));
     if(argv == NULL)
       goto no_mem;
     argc= 1;
     argv[0]= strdup(to_parse + strlen(prefix));
     if(argv[0] == NULL)
       goto no_mem;
     ret= 1;
   } else {
     ret= Xorriso_parse_line(xorriso, to_parse, prefix, f->separators,
                             max_words, &argc, &argv, 0);
   }
   if(ret < 0)
     goto ex;
   if(ret == 0)
 continue;

   if(f->last_word_line_end && argc > max_words) {
     l= strlen(argv[max_words]);
     if(l > 0)
       if(argv[max_words][l - 1] == '\n')
         argv[max_words][l - 1]= 0;
   }

   if(f->max_results > 0 && f->num_results >= f->max_results) {
     /* Dispose surplus results */
     for(i= 0; i < f->num_words; i++) {
       if(f->results != NULL) {
         next_lst= f->results->next;
         Xorriso_lst_destroy(&(f->results), 0);
         f->results= next_lst;
       }
     }
     if(f->num_delivered > 0)
       (f->num_delivered)--;
     if(f->num_delivered == 0)
       f->next_to_deliver= NULL;
     f->num_results--;
   }

   if(f->results == NULL) {
     prev_lst= NULL;
   } else {
     for(prev_lst= f->results; prev_lst->next != NULL;
         prev_lst= prev_lst->next);
   }
   for(i= 0; i < f->num_words; i++) {
     widx= f->word_idx[i];
     if(widx >= argc || widx < 0)
       ret= Xorriso_lst_new(&lst, "", prev_lst, 0);
     else if(argv[widx] == NULL)
       ret= Xorriso_lst_new(&lst, "", prev_lst, 0);
     else
       ret= Xorriso_lst_new(&lst, argv[widx], prev_lst, 0);
     if(ret <= 0)
       goto no_mem;
     if(prev_lst == NULL)
       f->results= lst;
     prev_lst= lst;
   }
   (f->num_results)++;
   Xorriso_free_meM(prefix_storage);
   prefix_storage= NULL;
   Xorriso__dispose_words(&argc, &argv);
 }
 ret= 1;
ex:
 if(to_parse != NULL)
   free(to_parse);
 Xorriso_free_meM(prefix_storage);
 Xorriso__dispose_words(&argc, &argv);
 return(ret);
no_mem:;
 Xorriso_no_malloc_memory(xorriso, NULL, 1); /* reports to stderr */
 ret= -1;
 goto ex;
}


/* ^^^^^^^^^^^^^^^^^^^^^^^^^^ Xorriso_msg_sievE ^^^^^^^^^^^^^^^^^^^^^^^^^^ */


int Xorriso_result(struct XorrisO *xorriso, int flag)
/*
 bit0= no considerations or computations or dialog. Just put out.
*/
{
 int ret, redirected= 0;

 if(flag&1)
   goto put_it_out;
 if(xorriso->request_to_abort)
   return(1);
 if(xorriso->msglist_stackfill > 0)
   if(xorriso->msglist_flags[xorriso->msglist_stackfill - 1] & 1)
     redirected= 1;
 if(xorriso->result_page_length>0 && !redirected) {
   ret= Xorriso_pager(xorriso,xorriso->result_line,2);
   if(ret<=0)
     return(ret);
   if(ret==2)
     return(1);
   if(xorriso->request_to_abort)
     return(1);
 }
put_it_out:;
 xorriso->bar_is_fresh= 0;
 ret= Xorriso_write_to_channel(xorriso, xorriso->result_line, 1,0);
 return(ret);
}


int Xorriso_info(struct XorrisO *xorriso, int flag)
/*
 bit0= use pager (as with result)
 bit1= permission to suppress output
 bit2= insist in showing output
*/
{
 int ret;
 static int note_sev= 0;

 if(flag&2)
   if(xorriso->request_to_abort)
     return(1);

 if(note_sev==0)
   Xorriso__text_to_sev("NOTE", &note_sev, 0);
 if(note_sev<xorriso->report_about_severity &&
    note_sev<xorriso->abort_on_severity && !(flag&4))
   return(1);

 if(flag&1) {
   ret= Xorriso_pager(xorriso,xorriso->info_text,2);
   if(ret<=0)
     return(ret);
   if(ret==2)
     return(1);
   if(flag&2)
     if(xorriso->request_to_abort)
       return(1);
 }
 xorriso->bar_is_fresh= 0;
 ret=Xorriso_write_to_channel(xorriso, xorriso->info_text, 2, 0);
 return(ret);
}


int Xorriso_mark(struct XorrisO *xorriso, int flag)
{
 int ret= 1,r_ret,i_ret;

 if(xorriso->mark_text[0]==0)
   return(1);
 if(xorriso->packet_output) 
   ret=Xorriso_write_to_channel(xorriso, xorriso->mark_text, 3, 0);
 else {
   sprintf(xorriso->result_line,"%s\n",xorriso->mark_text);
   r_ret= Xorriso_result(xorriso,1);
   strcpy(xorriso->info_text,xorriso->result_line);
   i_ret= Xorriso_info(xorriso,0);
   if(r_ret==0 || i_ret==0)
     ret= 0;
 }
 return(ret);
}


int Xorriso_restxt(struct XorrisO *xorriso, char *text)
{
 int ret;

 strncpy(xorriso->result_line,text,sizeof(xorriso->result_line)-1);
 xorriso->result_line[sizeof(xorriso->result_line)-1]= 0;
 ret= Xorriso_result(xorriso,0);
 return(ret);
}


/* @param flag bit0-7= purpose
                       0= ERRFILE
                       1= mark line (only to be put out if enabled)
*/
int Xorriso_process_errfile(struct XorrisO *xorriso,
                            int error_code, char msg_text[], int os_errno,
                            int flag)
{
 char ttx[41];
 int purpose;

 if(strlen(msg_text)>SfileadrL)
   return(-1);

 purpose= flag&255;
 if(purpose==1 && !(xorriso->errfile_mode&1))
   return(2);
 if(xorriso->errfile_fp!=NULL) {
   if(purpose==1)
     fprintf(xorriso->errfile_fp, "----------------- %s  %s\n",
             msg_text, Ftimetxt(time(0), ttx, 1));
   else
     fprintf(xorriso->errfile_fp, "%s\n", msg_text);
   fflush(xorriso->errfile_fp);
   return(1);
 }
 if(xorriso->errfile_log[0]==0)
   return(1);
 if(strcmp(xorriso->errfile_log, "-")==0 ||
    strcmp(xorriso->errfile_log, "-R")==0) {
   if(purpose==1)
     sprintf(xorriso->result_line, "----------------- %s  %s\n",
             msg_text, Ftimetxt(time(0), ttx, 1));
   else
     sprintf(xorriso->result_line, "%s\n", msg_text);
   Xorriso_result(xorriso, 1);
   return(1);
 }
 if(strcmp(xorriso->errfile_log, "-I")==0) {
   if(purpose==1)
     sprintf(xorriso->info_text, "ERRFILE_MARK=%s  %s\n",
             msg_text, Ftimetxt(time(0), ttx, 1));
   else
     sprintf(xorriso->info_text, "ERRFILE=%s\n", msg_text);
   Xorriso_info(xorriso, 0);
   return(1);
 }
 return(2);
}


#ifdef Xorriso_fetch_with_msg_queueS
/* Important: This function must stay thread-safe with all use of xorriso. */
#else
/* Note: It is ok to submit xorriso->info_text as msg_text here. */
#endif
/* flag: 
     bit0= for Xorriso_info() : use pager (as with result)
     bit1= for Xorriso_info() : permission to suppress output
     bit2..5= name prefix
       0="xorriso"
       1="libisofs"
       2="libburn"
       3="libisoburn"
       else: ""
     bit6= append carriage return rather than line feed (if not os_errno)
     bit7= perform Xorriso_process_msg_queues() first
     bit8= do not prepend name prefix and severity
*/
int Xorriso_msgs_submit(struct XorrisO *xorriso, 
                        int error_code, char msg_text[], int os_errno,
                        char severity[], int flag)
{
 int ret, lt, li, sev, i;
 char *sev_text= "FATAL", prefix[80], *text= NULL;
 static char pfx_list[20][16]= {
                   "xorriso : ", "libisofs: ", "libburn : ", "libisoburn: ",
                    "", "", "", "", "", "", "", "", "", "", "", "" };

 if(flag&128)
   Xorriso_process_msg_queues(xorriso, 0);

 if(strcmp(severity, "ERRFILE")==0)
   Xorriso_process_errfile(xorriso, error_code, msg_text, os_errno, 0);

 /* Set problem status */
 ret= Xorriso__text_to_sev(severity, &sev, 0);
 if(ret<=0)
   Xorriso__text_to_sev(sev_text, &sev, 0);
 else
   sev_text= severity;
 if(xorriso->problem_status<sev)
   Xorriso_set_problem_status(xorriso, sev_text, 0);

 /* Report problem event */
 if(sev<xorriso->report_about_severity && sev<xorriso->abort_on_severity)
   {ret= 2; goto ex;}
 lt= strlen(msg_text);
 if(!(flag & 256)) {
   sprintf(prefix,"%s%s : ", pfx_list[(flag>>2)&15], sev_text);
   li= strlen(prefix);
 } else {
   prefix[0]= 0;
   li= 0;
 }
 if(lt > ((int) sizeof(xorriso->info_text)) - li - 2)
   lt= sizeof(xorriso->info_text)-li-2;

#ifdef Xorriso_fetch_with_msg_queueS

 Xorriso_alloc_meM(text, char, sizeof(xorriso->info_text));

#else /* Xorriso_fetch_with_msg_queueS */

 text= xorriso->info_text;

#endif /* ! Xorriso_fetch_with_msg_queueS */

 if(msg_text == text) {
   if(li > 0) {
     for(i= lt; i>=0; i--)
       msg_text[i+li]= msg_text[i];
     for(i=0; i<li; i++)
       msg_text[i]= prefix[i];
   }
 } else {
   if(li > 0)
     strcpy(text, prefix);
   strncpy(text + li, msg_text, lt);
 }
 if((flag&64) && os_errno<=0)
   text[li+lt]= '\r';
 else
   text[li+lt]= '\n';
 text[li+lt+1]= 0;
 if(os_errno>0)
   sprintf(text + strlen(text) - 1, " : %s\n", strerror(os_errno));

#ifdef Xorriso_fetch_with_msg_queueS

 Xorriso_write_to_channel(xorriso, text, 2, 0);

#else /* Xorriso_fetch_with_msg_queueS */

 Xorriso_info(xorriso,4|(flag&3));

#endif /* ! Xorriso_fetch_with_msg_queueS */

ex:;

#ifdef Xorriso_fetch_with_msg_queueS
 Xorriso_free_meM(text);
#endif /* ! Xorriso_fetch_with_msg_queueS */

 return(ret);
}


/* To be used with isoburn_set_msgs_submit()
*/
int Xorriso_msgs_submit_void(void *xorriso,
                        int error_code, char msg_text[], int os_errno,
                        char severity[], int flag)
{
 int ret;

 ret= Xorriso_msgs_submit((struct XorrisO *) xorriso, error_code, msg_text,
                          os_errno, severity, flag);
 return(ret);
}


/** @return -1= abort , 0= no , 1= yes
*/
int Xorriso_reassure(struct XorrisO *xorriso, char *cmd, char *which_will,
                     int flag)
{
 int ret;

 if(!xorriso->do_reassure)
   return(1);
 sprintf(xorriso->info_text, "Really perform %s which will %s ? (y/n)\n",
         cmd, which_will);
 Xorriso_info(xorriso, 4);
 do {
   ret= Xorriso_request_confirmation(xorriso, 2|4|16);
 } while(ret==3);
 if(ret==6 || ret==4) {
   sprintf(xorriso->info_text, "%s confirmed", cmd);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   return(1);
 }
 if(ret==2) {
   sprintf(xorriso->info_text, "%s aborted", cmd);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   return(-1);
 }
 sprintf(xorriso->info_text, "%s revoked", cmd);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
 return(0);
}


int Xorriso_write_session_log(struct XorrisO *xorriso, int flag)
{
 FILE *fp= NULL;
 char *sfe= NULL, timetext[40], *rpt, *wpt;
 int ret;

 if(xorriso->session_logfile[0]==0)
   {ret= 2; goto ex;}

 Xorriso_alloc_meM(sfe, char, 5 * SfileadrL);

 fp= fopen(xorriso->session_logfile, "a");
 if(fp==0) {
   sprintf(xorriso->info_text, "-session_log: Cannot open file %s",
           Text_shellsafe(xorriso->session_logfile, sfe, 0));
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 wpt= sfe;
 for(rpt= xorriso->volid; *rpt!=0; rpt++) {
   if(*rpt=='\n') {
     *(wpt++)= '\\';
     *(wpt++)= 'n';
   } else
     *(wpt++)= *rpt;
 }
 *wpt= 0;
 fprintf(fp, "%s %d %d %s\n",
         Ftimetxt(time(0), timetext, 2), xorriso->session_lba,
         xorriso->session_blocks, sfe);
 fclose(fp);
 ret= 1;
ex:;
 Xorriso_free_meM(sfe);
 return(ret);
}


int Xorriso_status_filter(struct XorrisO *xorriso, char *filter, char *line,
                          int flag)
{
 if(filter!=NULL)
   if(filter[0]=='-')
     if(strncmp(filter, line, strlen(filter))!=0)
       return(0);
 return(1);
}


int Xorriso_status_result(struct XorrisO *xorriso, char *filter, FILE *fp,
                          int flag)
/* 
bit1= do only report to fp
*/
{
 int ret;

 ret= Xorriso_status_filter(xorriso, filter, xorriso->result_line, 0);
 if(ret <= 0)
   return(2);
 if(!(flag&2))
   Xorriso_result(xorriso,0);
 if(fp!=NULL) {
   ret= fwrite(xorriso->result_line,strlen(xorriso->result_line),1,fp);
   if(ret<=0)
     return(ret);
 }   
 return(1);
}


/*
 bit0= do only report non-default settings
 bit1= do only report to fp
*/
int Xorriso_boot_status_sysarea(struct XorrisO *xorriso, char *filter,
                                FILE *fp, int flag)
{
 char *line, *form= "any", *spec= "system_area=";
 int sa_type;
 
 line= xorriso->result_line;
 
 sa_type= (xorriso->system_area_options & 0xfc) >> 2;
 if(sa_type != 0) 
   return(2);
 if (xorriso->system_area_disk_path[0] == 0 && (flag & 1))
   return(2);
   
 if(xorriso->system_area_options & 1) {
   form= "grub";
   if(xorriso->system_area_options & (1 << 14))
     spec= "grub2_mbr=";
 } else if(xorriso->system_area_options & 2) {
   form= "isolinux";
 }
 sprintf(line, "-boot_image %s %s", form, spec);
 Text_shellsafe(xorriso->system_area_disk_path, line, 1);
 strcat(line, "\n");
 Xorriso_status_result(xorriso, filter, fp, flag & 2);
 return(1);
}


int Xorriso_status(struct XorrisO *xorriso, char *filter, FILE *fp, int flag)
/*
 bit0= do only report non-default settings
 bit1= do only report to fp
 bit2= report current -resume status even if bit0 is set, but only if valid
 bit3= report readline history
 bit4= report -resume options indirectly as 
              -options_from_file:${resume_state_file}_pos
*/
{
 int is_default, no_defaults, i, ret, adr_mode, do_single, behavior;
 int show_indev= 1, show_outdev= 1, show_dev= 0;
 int part_table_implicit= 0;
 char *line, *sfe= NULL, mode[80], *form, *treatment;
 char *in_pt, *out_pt, *nl_charset, *local_charset, *mode_pt;
 char *dev_filter= NULL, *xorriso_id= NULL;
 static char channel_prefixes[4][4]= {".","R","I","M"};
 static char load_names[][20]= {"auto", "session", "track", "lba", "volid"};
 static int max_load_mode= 4;
 struct Xorriso_lsT *paths, *leafs, *s, *plst, *vlst;

 Xorriso_alloc_meM(sfe, char, 5 * SfileadrL + 80);
 Xorriso_alloc_meM(xorriso_id, char, 129);

 no_defaults= flag&1;
 line= xorriso->result_line;

 if(xorriso->no_rc) {
   sprintf(line,"-no_rc\n");
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 }

 is_default= strcmp(xorriso->list_delimiter, "--") == 0;
 sprintf(line,"-list_delimiter %s\n", xorriso->list_delimiter);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= 0;
 if(xorriso->dialog == 2)
   sprintf(line,"-dialog on\n");
 else if(xorriso->dialog == 1)
   sprintf(line,"-dialog single_line\n");
 else {
   sprintf(line,"-dialog off\n");
   is_default= 1;
 }
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->result_page_length==0 && xorriso->result_page_width==80);
 sprintf(line,"-page %d %d\n",
              (xorriso->result_page_length>=0?xorriso->result_page_length
                                           :-xorriso->result_page_length),
              xorriso->result_page_width);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->use_stdin==0);
 sprintf(line,"-use_readline %s\n", (xorriso->use_stdin?"off":"on"));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->bsl_interpretation == 0);
 strcpy(line, "-backslash_codes ");
 if(xorriso->bsl_interpretation == 0)
   strcat(line, "off");
 else if(xorriso->bsl_interpretation == (3 | 16 | 32 | 64))
   strcat(line, "on");
 else {
   if((xorriso->bsl_interpretation & 3) == 1)
     strcat(line, "in_double_quotes");
   else if((xorriso->bsl_interpretation & 3) == 2)
     strcat(line, "in_quotes");
   else if((xorriso->bsl_interpretation & 3) == 3)
     strcat(line, "with_quoted_input");
   if(xorriso->bsl_interpretation & 16) {
     if(strlen(line) > 17)
       strcat(line, ":"); 
     strcat(line, "with_program_arguments");
   }
   if((xorriso->bsl_interpretation & (32 | 64)) == (32 | 64)) {
     if(strlen(line) > 17)
       strcat(line, ":"); 
     strcat(line, "encode_output");
   } else {
     if(xorriso->bsl_interpretation & 32) {
       if(strlen(line) > 17)
         strcat(line, ":"); 
       strcat(line, "encode_results");
     }
     if(xorriso->bsl_interpretation & 64) {
       if(strlen(line) > 17)
         strcat(line, ":"); 
       strcat(line, "encode_infos");
     }
   }
 }
 strcat(line, "\n");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= !xorriso->packet_output;
 sprintf(line,"-pkt_output %s\n",(xorriso->packet_output?"on":"off"));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 for(i=0;i<4;i++) {
   is_default= (xorriso->logfile[i]!=0);
   sprintf(line,"-logfile %s %s\n",
           channel_prefixes[i],Text_shellsafe(xorriso->logfile[i],sfe,0));
   if(!(is_default && no_defaults))
     Xorriso_status_result(xorriso,filter,fp,flag&2);
 }

 is_default= (xorriso->errfile_log[0]==0);
 sprintf(line,"-errfile_log %s\n",Text_shellsafe(xorriso->errfile_log,sfe,0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 if(xorriso->check_media_default == NULL) {
   is_default= 1;
   sprintf(line, "-check_media_defaults reset=now %s\n",
           xorriso->list_delimiter);
 } else {
   ret= Xorriso_check_media_list_job(xorriso, xorriso->check_media_default,
                                     line, no_defaults);
   is_default= (ret == 2);
   strcat(line, "\n");
 }
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 behavior= Xorriso__get_signal_behavior(0);
 is_default= (behavior == 1);
 treatment= "on";
 if(behavior == 0)
   treatment= "off";
 else if(behavior == 2)
   treatment= "sig_dfl";
 else if(behavior == 3)
   treatment= "sig_ign";
 sprintf(line,"-signal_handling %s\n", treatment);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->img_read_error_mode==2);
 treatment= "best_effort";
 if(xorriso->img_read_error_mode==1)
   treatment= "failure";
 else if(xorriso->img_read_error_mode==2)
   treatment= "fatal";
 sprintf(line,"-error_behavior image_loading %s\n", treatment);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 is_default= (xorriso->extract_error_mode == 1);
 treatment= "keep";
 if(xorriso->extract_error_mode == 0)
   treatment= "best_effort";
 else if(xorriso->extract_error_mode == 2)
   treatment= "delete";
 sprintf(line,"-error_behavior file_extraction %s\n", treatment);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->mark_text[0]==0);
 sprintf(line,"-mark %s\n",Text_shellsafe(xorriso->mark_text,sfe,0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->temp_mem_limit==16*1024*1024);
 if((xorriso->temp_mem_limit/1024/1024)*1024*1024==xorriso->temp_mem_limit)
   sprintf(line,"-temp_mem_limit %dm\n", xorriso->temp_mem_limit/1024/1024);
 else
   sprintf(line,"-temp_mem_limit %dk\n", xorriso->temp_mem_limit/1024);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);


 sprintf(line,"-prog %s\n",Text_shellsafe(xorriso->progname,sfe,0));
 Xorriso_status_result(xorriso,filter,fp,flag&2);

 if(xorriso->ban_stdio_write) {
   sprintf(line,"-ban_stdio_write\n");
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 }

 is_default= ((xorriso->early_stdio_test & 14) == 0);
 sprintf(line, "-early_stdio_test %s\n",
               xorriso->early_stdio_test & 6 ? xorriso->early_stdio_test & 8 ?
               "appendable_wo" : "on" : "off");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= ((xorriso->cache_default & 3) == 3);
 sprintf(line, "-data_cache_size ");
 if(xorriso->cache_default & 1)
   sprintf(line + strlen(line), "default ");
 else
   sprintf(line + strlen(line), "%d ", xorriso->cache_num_tiles);
 if(xorriso->cache_default & 2)
   sprintf(line + strlen(line), "default\n");
 else
   sprintf(line + strlen(line), "%d\n", xorriso->cache_tile_blocks);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->allow_restore==0 && xorriso->do_concat_split==1 &&
              xorriso->do_auto_chmod==0 && xorriso->drives_exclusive == 1);
 mode_pt= "off"; 
 if(xorriso->allow_restore == -2)
   mode_pt= "blocked";
 else if(xorriso->allow_restore == -1)
   mode_pt= "banned";
 else if(xorriso->allow_restore == 1)
   mode_pt= "on";
 else if(xorriso->allow_restore == 2)
   mode_pt= "device_files";
 if(xorriso->allow_restore == -1)
   sprintf(line,"-osirrox %s\n", mode_pt);
 else
   sprintf(line,"-osirrox %s:%s:%s:%s:%s:%s\n", mode_pt,
         xorriso->do_concat_split ? "concat_split_on" : "concat_split_off",
         xorriso->do_auto_chmod ? "auto_chmod_on" : "auto_chmod_off",
         xorriso->do_restore_sort_lba ? "sort_lba_on" : "sort_lba_off",
         xorriso->drives_exclusive ? "o_excl_on" : "o_excl_off",
         (xorriso->do_strict_acl & 1) ? "strict_acl_on" : "strict_acl_off"
        );
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->mount_opts_flag == 0);
  sprintf(line,"-mount_opts %s\n",
          xorriso->mount_opts_flag & 1 ? "shared" : "exclusive");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 Xorriso_boot_image_status(xorriso, filter, fp, flag & 3);

 Xorriso_boot_status_sysarea(xorriso, filter, fp, flag & 3);

 is_default= (xorriso->partition_offset == 0);
 sprintf(line,"-boot_image any partition_offset=%lu\n",
              (unsigned long int) xorriso->partition_offset);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 is_default= (xorriso->partition_secs_per_head == 0);
 sprintf(line,"-boot_image any partition_sec_hd=%lu\n",
              (unsigned long int) xorriso->partition_secs_per_head);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 is_default= (xorriso->partition_heads_per_cyl == 0);
 sprintf(line,"-boot_image any partition_hd_cyl=%lu\n",
              (unsigned long int) xorriso->partition_heads_per_cyl);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 ret= (xorriso->system_area_options & 0x300) >> 8;
 is_default= (ret == 0);
 sprintf(line,"-boot_image any partition_cyl_align=%s\n",
         ret == 0 ? "auto" : ret == 1 ? "on" : ret == 3 ? "all" : "off");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 if((xorriso->system_area_disk_path[0] || !part_table_implicit) &&
    (xorriso->partition_offset == 0 || (xorriso->system_area_options & 2))) {
   is_default= ((xorriso->system_area_options & 3) == 0);
   sprintf(line,"-boot_image %s partition_table=%s\n",
           xorriso->system_area_options & 2 ? "isolinux" : "grub",
           xorriso->system_area_options & 3 ? "on" : "off");
   if(!(is_default && no_defaults))
     Xorriso_status_result(xorriso,filter,fp,flag&2);
 }

 ret= ((xorriso->system_area_options & 0x3cfc) == 0x400);
 is_default= (ret == 0);
 sprintf(line, "-boot_image any chrp_boot_part=%s\n",
         ret == 1 ? "on" : "off");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso, filter, fp, flag & 2);

 is_default= (xorriso->prep_partition[0] == 0);
 sprintf(line,"-boot_image any prep_boot_part=%s\n", 
              Text_shellsafe(xorriso->prep_partition, sfe, 0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso, filter, fp, flag & 2);
 
 is_default= (xorriso->efi_boot_partition[0] == 0);
 sprintf(line,"-boot_image any efi_boot_part=%s\n",
              Text_shellsafe(xorriso->efi_boot_partition, sfe, 0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso, filter, fp, flag & 2);

#ifdef Xorriso_with_isohybriD
 if(strcmp(form, "isolinux") == 0) {
   static char modes[4][6]= {"off", "auto", "on", "force"};
   is_default= (xorriso->boot_image_isohybrid == 1);
   sprintf(line,"-boot_image isolinux isohybrid=%s\n",
           modes[xorriso->boot_image_isohybrid & 3]);
   if(!(is_default && no_defaults))
     Xorriso_status_result(xorriso,filter,fp,flag&2);
 }
#endif /* Xorriso_with_isohybriD */

 is_default= 1;
 for(i= 0; i < 8; i++)
   if(xorriso->hfsp_serial_number[i])
     is_default= 0;
 sprintf(line, "-boot_image any hfsplus_serial=");
 for(i= 0; i < 8; i++)
   sprintf(line + strlen(line), "%-2.2X",
           (unsigned int) xorriso->hfsp_serial_number[i]);
 strcat(line, "\n");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso, filter, fp, flag & 2);

 is_default= (xorriso->hfsp_block_size == 0);
 sprintf(line, "-boot_image any hfsplus_block_size=%d\n",
               xorriso->hfsp_block_size);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso, filter, fp, flag & 2);

 is_default= (xorriso->apm_block_size == 0);
 sprintf(line, "-boot_image any apm_block_size=%d\n",
               xorriso->apm_block_size);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso, filter, fp, flag & 2);

   sprintf(line,"-cd ");
 if(filter != NULL)
   if(strncmp(filter, "-cdi", 4) == 0)
     sprintf(line,"-cdi ");
 sprintf(line + strlen(line),"%s\n",
         (xorriso->wdi[0] ? Text_shellsafe(xorriso->wdi,sfe,0) : "'/'"));
 Xorriso_status_result(xorriso,filter,fp,flag&2);
 sprintf(line,"-cdx %s\n",
         (xorriso->wdx[0] ? Text_shellsafe(xorriso->wdx,sfe,0) : "'/'"));
 Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->split_size==0);
 strcpy(line,"-split_size ");
 if(xorriso->split_size % (1024*1024) || xorriso->split_size==0) {
   Sfile_off_t_text(line+strlen(line), xorriso->split_size, 0);
 } else {
   Sfile_off_t_text(line+strlen(line), xorriso->split_size / (1024*1024), 0);
   strcat(line, "m");
 }
 strcat(line, "\n");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->add_plainly==0);
 sprintf(line,"-add_plainly %s\n",
         (xorriso->add_plainly == 1 ? "unknown" : 
          xorriso->add_plainly == 2 ? "dashed" :
          xorriso->add_plainly == 3 ? "any" : "none"));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 ret= Exclusions_get_descrs(xorriso->disk_exclusions, &paths, &leafs, 0);
 if(ret>0) {
   for(; paths!=NULL; paths= paths->next) {
     sprintf(line, "-not_paths %s %s\n",
             Text_shellsafe(paths->text, sfe, 0), xorriso->list_delimiter);
     Xorriso_status_result(xorriso,filter,fp,flag&2);
   } 
   for(; leafs!=NULL; leafs= leafs->next) {
     sprintf(line,"-not_leaf %s\n", Text_shellsafe(leafs->text, sfe, 0));
     Xorriso_status_result(xorriso,filter,fp,flag&2);
   } 
 }

 is_default= (xorriso->file_size_limit ==
              Xorriso_default_file_size_limiT);
 if(xorriso->file_size_limit <= 0)
   sprintf(line, "-file_size_limit off %s\n", xorriso->list_delimiter);
 else
   sprintf(line, "-file_size_limit %.f %s\n",
           (double) xorriso->file_size_limit, xorriso->list_delimiter);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->disk_excl_mode==1);
 sprintf(line, "-not_mgt %s:%s:%s:%s\n",
         (xorriso->disk_excl_mode&1 ? "on" : "off"),
         (xorriso->disk_excl_mode&2 ? "param_on" : "param_off"),
         (xorriso->disk_excl_mode&4 ? "subtree_on" : "subtree_off"),
         (xorriso->disk_excl_mode&8 ? "ignore_on" : "ignore_off"));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->do_iso_rr_pattern==1);
 sprintf(line,"-iso_rr_pattern %s\n",
         (xorriso->do_iso_rr_pattern == 1 ? "on" :
         (xorriso->do_iso_rr_pattern == 2 ? "ls" : "off")));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->do_disk_pattern==2);
 sprintf(line,"-disk_pattern %s\n",
         (xorriso->do_disk_pattern == 1 ? "on" :
         (xorriso->do_disk_pattern == 2 ? "ls" : "off")));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= xorriso->volid_default;
 sprintf(line,"-volid %s\n",Text_shellsafe(xorriso->volid,sfe,0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 if(is_default && xorriso->loaded_volid[0] &&
    strcmp(xorriso->loaded_volid, xorriso->volid)!=0 && !no_defaults) {
   sprintf(line,"# loaded image effective -volid %s\n",
           Text_shellsafe(xorriso->loaded_volid,sfe,0));
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 }

 Xorriso_preparer_string(xorriso, xorriso_id, 0);
 is_default= (strcmp(xorriso->preparer_id, xorriso_id) == 0);
 sprintf(line,"-preparer_id %s\n",Text_shellsafe(xorriso->preparer_id,sfe,0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->publisher[0]==0);
 sprintf(line,"-publisher %s\n",Text_shellsafe(xorriso->publisher,sfe,0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->application_id[0]==0);
 sprintf(line,"-application_id %s\n",
         Text_shellsafe(xorriso->application_id,sfe,0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->system_id[0]==0);
 sprintf(line,"-system_id %s\n", Text_shellsafe(xorriso->system_id,sfe,0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->volset_id[0]==0);
 sprintf(line,"-volset_id %s\n", Text_shellsafe(xorriso->volset_id,sfe,0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->vol_creation_time == 0);
 sprintf(line,"-volume_date c %s\n",
         is_default ? "default" :
         Ftimetxt(xorriso->vol_creation_time, sfe, 2));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->vol_modification_time == 0);
 sprintf(line,"-volume_date m %s\n",
         xorriso->vol_uuid[0] ? "overridden" :
         is_default ? "default" :
         Ftimetxt(xorriso->vol_modification_time, sfe, 2));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->vol_expiration_time == 0);
 sprintf(line,"-volume_date x %s\n",
         is_default ? "default" :
         Ftimetxt(xorriso->vol_expiration_time, sfe, 2));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->vol_effective_time == 0);
 sprintf(line,"-volume_date f %s\n",
         is_default ? "default" :
         Ftimetxt(xorriso->vol_effective_time, sfe, 2));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->vol_uuid[0] == 0);
 sprintf(line,"-volume_date uuid %s\n",
         Text_shellsafe(xorriso->vol_uuid,sfe,0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->copyright_file[0] == 0);
 sprintf(line,"-copyright_file %s\n",
        Text_shellsafe(xorriso->copyright_file,sfe,0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->biblio_file[0]==0);
 sprintf(line,"-biblio_file %s\n",Text_shellsafe(xorriso->biblio_file,sfe,0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->abstract_file[0]==0);
 sprintf(line,"-abstract_file %s\n",
         Text_shellsafe(xorriso->abstract_file,sfe,0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->do_joliet==0);
 sprintf(line,"-joliet %s\n", (xorriso->do_joliet == 1 ? "on" : "off"));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->do_rockridge == 1);
 sprintf(line, "-rockridge %s\n", (xorriso->do_rockridge == 1 ? "on" : "off"));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->do_hfsplus == 0);
 sprintf(line,"-hfsplus %s\n", (xorriso->do_hfsplus == 1 ? "on" : "off"));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 Xorriso_lst_get_last(xorriso->jigdo_params, &plst, 0);
 Xorriso_lst_get_last(xorriso->jigdo_values, &vlst, 0);
 if(plst == NULL || vlst == NULL) {
   is_default= 1;
   sprintf(line,"-jigdo clear 'all'\n");
   if(!(is_default && no_defaults))
     Xorriso_status_result(xorriso, filter, fp, flag & 2);
 }
 while(plst != NULL && vlst != NULL) {
   sprintf(line,"-jigdo %s %s\n", Xorriso_lst_get_text(plst, 0), 
           Text_shellsafe(Xorriso_lst_get_text(vlst, 0), sfe, 0));
   Xorriso_status_result(xorriso, filter, fp, flag & 2);
   plst= Xorriso_lst_get_prev(plst, 0);
   vlst= Xorriso_lst_get_prev(vlst, 0);
 }

 if(xorriso->do_global_uid) {
   sprintf(line,"-uid %lu\n", (unsigned long) xorriso->global_uid);
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 }

 if(xorriso->do_global_gid) {
   sprintf(line,"-gid %lu\n", (unsigned long) xorriso->global_gid);
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 }

 Xorriso_status_extf(xorriso, filter, fp, flag & 2);
 Xorriso_status_zisofs(xorriso, filter, fp, flag & 3);

 is_default= !xorriso->allow_graft_points;
 sprintf(line,"-pathspecs %s\n", xorriso->allow_graft_points ? "on" : "off");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->do_follow_pattern && (!xorriso->do_follow_param)
              && xorriso->do_follow_mount && (!xorriso->do_follow_links)
              && xorriso->follow_link_limit==100);
 mode[0]= 0;
 if(xorriso->do_follow_pattern &&
    !(xorriso->do_follow_links && xorriso->do_follow_mount))
   strcat(mode,":pattern");
 if(xorriso->do_follow_param && !(xorriso->do_follow_links))
   strcat(mode,":param");
 if(xorriso->do_follow_links)
   strcat(mode,":link");
 if(xorriso->do_follow_mount)
   strcat(mode,":mount");
 if(mode[0]==0)
   strcpy(mode, ":off");
 sprintf(mode+strlen(mode), ":limit=%d", xorriso->follow_link_limit);
 sprintf(line,"-follow %s\n", mode+1);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->do_overwrite==2);
 sprintf(line,"-overwrite %s\n",(xorriso->do_overwrite == 1 ? "on" :
                             (xorriso->do_overwrite == 2 ? "nondir" : "off")));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= !xorriso->do_reassure;
 sprintf(line,"-reassure %s\n",(xorriso->do_reassure == 1 ? "on" :
                             (xorriso->do_reassure == 2 ? "tree" : "off")));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= !xorriso->do_close;
 sprintf(line,"-close %s\n",(xorriso->do_close ? "on" : "off"));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->do_tao == 0);
 sprintf(line,"-write_type %s\n",
      xorriso->do_tao == 0 ? "auto" : xorriso->do_tao > 0 ? "tao" : "sao/dao");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= !xorriso->do_dummy;
 sprintf(line,"-dummy %s\n",(xorriso->do_dummy ? "on" : "off"));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->speed==0);
 sprintf(line,"-speed %dkB/s\n", xorriso->speed);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->do_stream_recording==0);
 strcpy(mode, "off");
 if(xorriso->do_stream_recording == 1)
   strcpy(mode, "full");
 if(xorriso->do_stream_recording == 2)
   strcpy(mode, "data");
 else if(xorriso->do_stream_recording == 32)
   strcpy(mode, "on");
 else if(xorriso->do_stream_recording >= 16)
   sprintf(mode, "%ds", xorriso->do_stream_recording);
 sprintf(line,"-stream_recording %s\n", mode);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->dvd_obs == 0);
 strcpy(mode, "default");
 if(xorriso->dvd_obs == 32768 || xorriso->dvd_obs == 65536)
   sprintf(mode, "%dk", xorriso->dvd_obs / 1024);
 sprintf(line,"-dvd_obs %s\n", mode);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->stdio_sync == 0);
 strcpy(line, "-stdio_sync ");
 if(xorriso->stdio_sync == -1)
   strcat(line, "off");
 else if(xorriso->stdio_sync == 0)
   strcat(line, "on");
 else if(xorriso->stdio_sync % 512) {
   Sfile_off_t_text(line+strlen(line), (off_t) (xorriso->stdio_sync * 2048),
                    0);
 } else {
   Sfile_off_t_text(line+strlen(line), (off_t) (xorriso->stdio_sync / 512), 0);
   strcat(line, "m"); 
 }
 strcat(line, "\n"); 
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->fs==4*512);
 if((xorriso->fs/512)*512==xorriso->fs)
   sprintf(line,"-fs %dm\n", xorriso->fs/512);
 else
   sprintf(line,"-fs %dk\n", xorriso->fs*2);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->padding==300*1024);
 sprintf(line,"-padding %dk\n", xorriso->padding/1024);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 is_default= (xorriso->do_padding_by_libisofs == 0);
 sprintf(line,"-padding %s\n",
         xorriso->do_padding_by_libisofs ? "included" : "appended");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (strcmp(xorriso->report_about_text,"UPDATE")==0);
 sprintf(line,"-report_about %s\n",xorriso->report_about_text);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->scsi_log == 0);
 sprintf(line,"-scsi_log %s\n", xorriso->scsi_log ? "on" : "off");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->session_logfile[0]==0);
 sprintf(line,"-session_log %s\n",
         Text_shellsafe(xorriso->session_logfile,sfe,0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->pacifier_style==0);
 sprintf(line,"-pacifier '%s'\n",
         xorriso->pacifier_style==1 ? "mkisofs" : 
         xorriso->pacifier_style==2 ? "cdrecord" : "xorriso");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (strcmp(xorriso->return_with_text,"SORRY")==0 &&
              xorriso->return_with_value==32);
 sprintf(line,"-return_with %s %d\n",
         xorriso->return_with_text, xorriso->return_with_value);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 
 is_default= 0;
 sprintf(line,"-abort_on %s\n",xorriso->abort_on_text);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 
 if(xorriso->status_history_max!=Xorriso_status_history_maX || !no_defaults) {
   sprintf(line,"-status_history_max %d\n",xorriso->status_history_max);
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 }

#ifdef Xorriso_with_readlinE

 if((flag&8) && xorriso->status_history_max>0) {
   HIST_ENTRY **hl;
   int hc,i;

   hl= history_list();
   if(hl!=NULL) {
     for(hc= 0;hl[hc]!=NULL;hc++);
     if(hc>0)
       if(strcmp(hl[hc-1]->line,"-end")==0)
         hc--;
     if(hc>=xorriso->status_history_max)
       i= hc-xorriso->status_history_max;
     else
       i= 0;
     for(;i<hc;i++) {
       sprintf(line,"-history %s\n",Text_shellsafe(hl[i]->line,sfe,0));
       Xorriso_status_result(xorriso,filter,fp,flag&2);
     }   
   }
 }

#endif /* Xorriso_with_readlinE */

 is_default= (xorriso->toc_emulation_flag == 0);
 sprintf(line,"-rom_toc_scan %s:%s:%s\n",
         xorriso->toc_emulation_flag & 4 ? "force" :
                                xorriso->toc_emulation_flag & 1 ? "on" : "off",
         xorriso->toc_emulation_flag & 2 ? "emul_off" : "emul_on",
         xorriso->toc_emulation_flag & 8 ? "emul_wide" : "emul_narrow");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso, filter, fp, flag & 2);

 is_default= (xorriso->displacement == 0);
 sprintf(line, "-displacement %s%lu\n",
               xorriso->displacement_sign < 0 ? "-" : "",
               (unsigned long) xorriso->displacement);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso, filter, fp, flag & 2);

 adr_mode= xorriso->image_start_mode & 0xffff;
 if(adr_mode>=0 && adr_mode<=max_load_mode) {
   is_default= (adr_mode==0);
   sprintf(line,"-load %s ", load_names[adr_mode]);
   if(adr_mode==0)
     sprintf(line+strlen(line),"''\n");
   else if(adr_mode>=1 && adr_mode<=3)
     sprintf(line+strlen(line),"%s\n", xorriso->image_start_value);
   else
     sprintf(line+strlen(line),"%s\n",
              Text_shellsafe(xorriso->image_start_value, sfe, 0));
   if(!(is_default && no_defaults))
     Xorriso_status_result(xorriso,filter,fp,flag&2);
 }

 is_default= (xorriso->do_calm_drive & 1);
 sprintf(line,"-calm_drive %s\n", xorriso->do_calm_drive & 1 ? "on" : "off");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->grow_blindly_msc2<0);
 sprintf(sfe, "%d", xorriso->grow_blindly_msc2);
 sprintf(line,"-grow_blindly %s\n",
         xorriso->grow_blindly_msc2 < 0 ? "off" : sfe);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 Xorriso_get_local_charset(xorriso, &local_charset, 0);
 nl_charset= nl_langinfo(CODESET);
 is_default= (strcmp(local_charset, nl_charset) == 0);
 sprintf(line, "-local_charset %s\n", Text_shellsafe(local_charset, sfe, 0));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso, filter, fp, flag & 2);

 is_default= (xorriso->out_charset == NULL && xorriso->in_charset == NULL);
 in_pt= "";
 if(xorriso->in_charset != NULL)
   in_pt= xorriso->in_charset;
 out_pt= "";
 if(xorriso->out_charset != NULL)
   out_pt= xorriso->out_charset;
 do_single= 0;
 ret= Xorriso_status_filter(xorriso, filter, "-in_charset", 0);
 if(ret <= 0)
   ret= Xorriso_status_filter(xorriso, filter, "-out_charset", 0);
 if(ret > 0)
   do_single= 1;
 if(strcmp(in_pt, out_pt) == 0 && !do_single) {
   sprintf(line, "-charset %s\n", Text_shellsafe(in_pt, sfe, 0));
   if(!(is_default && no_defaults))
     Xorriso_status_result(xorriso, filter, fp, flag & 2);
 } else {
   sprintf(line, "-in_charset %s\n", Text_shellsafe(in_pt, sfe, 0));
   if(!(is_default && no_defaults))
     Xorriso_status_result(xorriso, filter, fp, flag & 2);
   sprintf(line, "-out_charset %s\n", Text_shellsafe(out_pt, sfe, 0));
   if(!(is_default && no_defaults))
     Xorriso_status_result(xorriso, filter, fp, flag & 2);
 }
 is_default= ((xorriso->do_aaip & (256 | 512)) == 0);
 sprintf(line,"-auto_charset %s\n", (xorriso->do_aaip & 256 ? "on" : "off"));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= ((xorriso->ino_behavior & 31) == 7);
 switch (xorriso->ino_behavior & 15) {
 case 0: form= "on";
 break; case 8: form= "without_update";
 break; default: form= "off";
 }
 sprintf(line,"-hardlinks %s:%s:%s\n", form,
         xorriso->ino_behavior & 32 ?
         "no_lsl_count" : "lsl_count",
         xorriso->ino_behavior & 16 ?
         "cheap_sorted_extract" : "normal_extract"); 
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= ((xorriso->do_aaip & (1 | 4)) == 0);
 sprintf(line,"-acl %s\n", (xorriso->do_aaip & 1 ? "on" : "off"));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 is_default= ((xorriso->do_aaip & (2 | 8)) == 0);
 sprintf(line,"-xattr %s\n", (xorriso->do_aaip & 4 ? "on" : "off"));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 is_default= ((xorriso->do_aaip & (16 | 32 | 64)) == 0);
 sprintf(line,"-disk_dev_ino %s\n",
         (xorriso->do_aaip & 16 ? (xorriso->do_aaip & 128 ? "ino_only" : "on" )
                                : "off"));
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= ((xorriso->do_md5 & 31) == 0);
 sprintf(line, "-md5 ");
 if(xorriso->do_md5 & 1) {
   if((xorriso->do_md5 & 8) == 8) {
     strcat(line, "all");
   } else {
     strcat(line, "on");
     if(xorriso->do_md5 & 8)
       strcat(line, ":stability_check_on");
   }
   if(xorriso->do_md5 & 32) 
     strcat(line, ":load_check_off");
   strcat(line, "\n");
 } else
   strcat(line, "off\n");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->scdbackup_tag_name[0] == 0 &&
              xorriso->scdbackup_tag_listname[0] == 0);
 sprintf(line, "-scdbackup_tag ");
 Text_shellsafe(xorriso->scdbackup_tag_listname, line, 1);
 strcat(line, " ");
 Text_shellsafe(xorriso->scdbackup_tag_name, line, 1);
 strcat(line, "\n");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (Xorriso_get_relax_text(xorriso, sfe, 0) == 2);
 sprintf(line,"-compliance %s\n", sfe);
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->rr_reloc_dir[0] == 0);
 sprintf(line, "-rr_reloc_dir ");
 Text_shellsafe(xorriso->rr_reloc_dir, line, 1);
 strcat(line, "\n");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);

 is_default= (xorriso->assert_volid[0] == 0);
 sprintf(line, "-assert_volid ");
 Text_shellsafe(xorriso->assert_volid, line, 1);
 strcat(line, " ");
 Text_shellsafe(xorriso->assert_volid_sev, line, 1);
 strcat(line, "\n");
 if(!(is_default && no_defaults))
   Xorriso_status_result(xorriso,filter,fp,flag&2);
 
 is_default= 1;
 if(xorriso->drive_blacklist != NULL || xorriso->drive_whitelist != NULL ||
    xorriso->drive_greylist == NULL)
   is_default= 0;
 if(xorriso->drive_greylist != NULL) {
   if(strcmp(Xorriso_get_pattern(xorriso, xorriso->drive_greylist, 0, 0),
              "/dev") != 0)
     is_default= 0;
   if(Xorriso_get_pattern(xorriso, xorriso->drive_greylist, 1, 0) != NULL)
     is_default= 0;
 }
 if(!(is_default && no_defaults)) {
   for(s= xorriso->drive_blacklist; s != NULL; s= Xorriso_lst_get_next(s, 0)) {
     sprintf(line, "-drive_class 'banned'   %s\n",
             Text_shellsafe(Xorriso_lst_get_text(s, 0), sfe, 0));
     Xorriso_status_result(xorriso,filter,fp,flag&2);
   }
   for(s= xorriso->drive_greylist; s != NULL; s= Xorriso_lst_get_next(s, 0)) {
     sprintf(line, "-drive_class 'caution'  %s\n",
             Text_shellsafe(Xorriso_lst_get_text(s, 0), sfe, 0));
     Xorriso_status_result(xorriso,filter,fp,flag&2);
   }
   for(s= xorriso->drive_whitelist; s != NULL; s= Xorriso_lst_get_next(s, 0)) {
     sprintf(line, "-drive_class 'harmless' %s\n",
             Text_shellsafe(Xorriso_lst_get_text(s, 0), sfe, 0));
     Xorriso_status_result(xorriso,filter,fp,flag&2);
   }
 }

 do_single= 0;
 dev_filter= filter;
 if(dev_filter != NULL) {
   show_dev= Xorriso_status_filter(xorriso, filter, "-dev", 0);
   if(show_dev > 0)
     dev_filter= NULL; 
 }
 if(dev_filter != NULL) {
   show_indev= Xorriso_status_filter(xorriso, filter, "-indev", 0);
   show_outdev= Xorriso_status_filter(xorriso, filter, "-outdev", 0);
   if(show_outdev > 0 || show_indev > 0)
     do_single= 1;
 }
 if(xorriso->drives_exclusive != xorriso->indev_is_exclusive &&
    xorriso->indev[0]) 
   do_single= 1;
 else if(xorriso->drives_exclusive != xorriso->outdev_is_exclusive &&
         xorriso->outdev[0]) 
   do_single= 1;
 if(strcmp(xorriso->indev, xorriso->outdev) == 0 && !do_single) {
   sprintf(line,"-dev %s\n", Text_shellsafe(xorriso->indev,sfe,0));
   Xorriso_status_result(xorriso, dev_filter, fp, flag & 2);
 } else {
   if(xorriso->drives_exclusive != xorriso->indev_is_exclusive &&
      xorriso->indev[0] && show_indev) {
     sprintf(line,"-osirrox o_excl_%s\n",
             xorriso->indev_is_exclusive ? "on" : "off");
     Xorriso_status_result(xorriso, NULL, fp, flag & 2);
   }
   sprintf(line,"-indev %s\n", Text_shellsafe(xorriso->indev,sfe,0));
   Xorriso_status_result(xorriso, dev_filter, fp, flag & 2);
   if(xorriso->drives_exclusive != xorriso->indev_is_exclusive &&
      xorriso->indev[0] && show_indev) {
     sprintf(line,"-osirrox o_excl_%s\n",
             xorriso->drives_exclusive ? "on" : "off");
     Xorriso_status_result(xorriso, NULL, fp, flag & 2);
   }

   if(xorriso->drives_exclusive != xorriso->outdev_is_exclusive &&
      xorriso->outdev[0] && show_outdev) {
     sprintf(line,"-osirrox o_excl_%s\n",
             xorriso->outdev_is_exclusive ? "on" : "off");
     Xorriso_status_result(xorriso, NULL, fp, flag & 2);
   }
   sprintf(line,"-outdev %s\n", Text_shellsafe(xorriso->outdev,sfe,0));
   Xorriso_status_result(xorriso, dev_filter, fp, flag & 2);
   if(xorriso->drives_exclusive != xorriso->outdev_is_exclusive &&
      xorriso->outdev[0] && show_outdev) {
     sprintf(line,"-osirrox o_excl_%s\n",
             xorriso->drives_exclusive ? "on" : "off");
     Xorriso_status_result(xorriso, NULL, fp, flag & 2);
   }
 }

 ret= 1;
ex:;
 Xorriso_free_meM(sfe);
 Xorriso_free_meM(xorriso_id);
 return(ret);
}


int Xorriso_pacifier_reset(struct XorrisO *xorriso, int flag)
{
 xorriso->start_time= Sfile_microtime(0);
 xorriso->last_update_time= xorriso->start_time;
 xorriso->pacifier_count= 0;
 xorriso->pacifier_prev_count= 0;
 xorriso->pacifier_total= 0;
 xorriso->pacifier_byte_count= 0;
 return(1);
}


/* This call is to be issued by long running workers in short intervals.
   It will check whether enough time has elapsed since the last pacifier
   message and eventually issue an update message.
   @param what_done  A sparse description of the action, preferrably in past
                     tense. E.g. "done" , "files added".
   @param count The number of objects processed so far.
                Is ignored if <=0.
   @param todo  The number of objects to be done in total.
                Is ignored if <=0.
   @param current_object A string telling the object currently processed.
                         Ignored if "".
   @param flag  bit0= report unconditionally, no time check
                bit1= report count <=0 (no thank you for being patient then)
                bit2= report xorriso->pacifier_byte_count
                bit3= report speed
                bit4= with bit3: count is in blocks, else in bytes
                bit5= with bit3: report total speed
                bit6= report with carriage return rather than line feed
                bit7= with bit5: speed unit for outdev rather than indev
*/
int Xorriso_pacifier_callback(struct XorrisO *xorriso, char *what_done,
                              off_t count, off_t todo, char *current_object,
                              int flag)
{
 double current_time, since, interval_time, speed, speed_factor;
 char count_text[80], byte_text[80], profile_name[80], *speed_unit;
 int ret, profile_number;
 off_t amount;
 
 current_time= Sfile_microtime(0);
 interval_time= current_time - xorriso->last_update_time;
 if(interval_time < xorriso->pacifier_interval
    && !(flag&1))
   return(1);
 xorriso->last_update_time= Sfile_microtime(0);
 since= current_time - xorriso->start_time;
 if((flag&1)&&since<1.0)
   since= 1.0;
 byte_text[0]= 0;
 if(flag&4) {
   strcat(byte_text, " (");
   Sfile_scale((double) xorriso->pacifier_byte_count,
               byte_text+strlen(byte_text), 7, 1e5, 0);
   strcat(byte_text, ")");
 }
 if(count<=0.0 && !(flag&2)) {
   if(since < 2)
     return(2);
   sprintf(xorriso->info_text,
           "Thank you for being patient for %.f seconds", since);
 } else if(todo<=0.0) {
   if(count<10000000)
     sprintf(count_text, "%.f", (double) count);
   else
     Sfile_scale((double) count, count_text, 7, 1e5, 1);
   sprintf(xorriso->info_text, "%s %s%s in %.f %s",
           count_text, what_done, byte_text, since,
           (flag & 64) ? "s" : "seconds");
 } else {
   sprintf(xorriso->info_text, "%.f of %.f %s%s in %.f %s",
           (double) count, (double) todo, what_done, byte_text, since,
           (flag & (8 | 64)) ? "s" : "seconds");
 }
 speed= -1.0;
 if(flag & 4)
   amount= xorriso->pacifier_byte_count;
 else
   amount= count;
 if((flag & 8)) {
   if(flag & 32) {
     if(since > 0)
       speed= amount / since;
   } else if(amount >= xorriso->pacifier_prev_count) {
     if(interval_time > 0)
       speed= (amount - xorriso->pacifier_prev_count) / interval_time;
   }
 }
 if(speed >= 0.0) {
   if(flag & 16)
     speed*= 2048.0;
   ret= Xorriso_get_profile(xorriso, &profile_number, profile_name,
                            (flag >> 6) & 2);
   speed_factor= 1385000;
   speed_unit= "D";
   if(ret == 2) {
     speed_factor= 150.0*1024;
     speed_unit= "C";
   } else if(ret == 3) {
     speed_factor= 4495625;
     speed_unit= "B";
   }
   sprintf(xorriso->info_text+strlen(xorriso->info_text), " %s %.1fx%s",
           (flag & 32 ? "=" : ","), speed / speed_factor, speed_unit);
 }
 xorriso->pacifier_prev_count= amount;
 if(current_object[0]!=0)
   sprintf(xorriso->info_text+strlen(xorriso->info_text),
           ", now at %s", current_object);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "UPDATE", (flag&64));
 return(1);
}


int Xorriso_reset_counters(struct XorrisO *xorriso, int flag)
{
 xorriso->error_count= 0;
 xorriso->insert_count= 0;
 xorriso->insert_bytes= 0;
 Xorriso_pacifier_reset(xorriso, 0);
 return(1);
}


/* @param flag bit0= to stderr rather than Xorriso_msgs_submit
*/
int Xorriso_no_malloc_memory(struct XorrisO *xorriso, char **to_free, int flag)
{
 if(to_free!=NULL)
   if(*to_free!=NULL) {
     /* Eventual memory sacrifice to get on going */
     free(*to_free);
     *to_free= NULL;
   }
 sprintf(xorriso->info_text, "Out of virtual memory");
 if(flag & 1) {
   fprintf(stderr, "%s", xorriso->info_text);
   /* (No need to first check for problem status worse than ABORT) */
   Xorriso_set_problem_status(xorriso, "ABORT", 0);
 } else
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "ABORT", 0);
 return(1);
}


/* @param flag bit0=path is in source filesystem , bit1= unconditionally */
int Xorriso_much_too_long(struct XorrisO *xorriso, int len, int flag)
{
 if(len>=SfileadrL || (flag&2)) {
   sprintf(xorriso->info_text,
           "Path given for %s is much too long (%d)",
           ((flag&1) ? "local filesystem" : "ISO image"), len);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(1);
}


int Xorriso_no_findjob(struct XorrisO *xorriso, char *cmd, int flag)
{
 sprintf(xorriso->info_text, "%s: cannot create find job object", cmd);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
 return(1);
}


int Xorriso_report_md5_outcome(struct XorrisO *xorriso, char *severity,
                               int flag)
{
 int has_md5;

 has_md5= Xorriso_image_has_md5(xorriso, 0);
 if(xorriso->find_check_md5_result & 1) {
   sprintf(xorriso->result_line,
           "Mismatch detected between file contents and MD5 checksums.\n");
 } else if(xorriso->find_check_md5_result & 8) {
   sprintf(xorriso->result_line,
           "File contents and their MD5 checksums match.\n");
 } else {
   sprintf(xorriso->result_line,
           "Not a single file with MD5 checksum was found.");
   if(has_md5 <= 0)
     strcat(xorriso->result_line,
            " (There is no MD5 checksum array loaded.)\n");
   else
     strcat(xorriso->result_line, "\n");
 }
 Xorriso_result(xorriso,0);
 if(xorriso->find_check_md5_result & 2) {
   sprintf(xorriso->result_line,
           "Encountered errors other than non-match during MD5 checking.\n");
   Xorriso_result(xorriso,0);
 }
 if((xorriso->find_check_md5_result & 4) && has_md5) {
   sprintf(xorriso->result_line,
   "There were data files which have no MD5 and thus could not be checked.\n");
   Xorriso_result(xorriso,0);
 }
 if((xorriso->find_check_md5_result & 3) && strcmp(severity, "ALL") != 0) {
   sprintf(xorriso->info_text, "Event triggered by MD5 comparison mismatch");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, severity, 0);
 }
 return(1);
}


/* @param flag bit0= do not issue prompt messages on info channel
               bit1= use line rather than asking at dialog input
*/
int Xorriso_msg_op_parse(struct XorrisO *xorriso, char *line,
                         char *prefix, char *separators,
                         int max_words, int pflag, int input_lines,
                         int flag)
{
 int ret, i, l, pargc= 0, bsl_mem;
 char *pline= NULL, **pargv= NULL, *parse_line, *text= NULL, *text_pt;

 Xorriso_alloc_meM(pline, char, SfileadrL);

 if(!(flag & 1)) {
   if(input_lines > 1)
     sprintf(xorriso->info_text, "-msg_op parse: Enter %d lines of text\n",
                                 input_lines);
   else
     sprintf(xorriso->info_text, "-msg_op parse: Enter text line\n");
   Xorriso_info(xorriso, 0);
 }

 if(flag & 2) {
   parse_line= line;
 } else {
   pline[0]= 0;
   for(i= 0; i < input_lines; i++) {
     l= strlen(pline);
     ret= Xorriso_dialog_input(xorriso, pline + l, SfileadrL - l - 1, 8);
     if(ret <= 0)
       goto ex;
     if(i < input_lines - 1)
       strcat(pline, "\n");
   }
   parse_line= pline;
 }
 ret= Xorriso_parse_line(xorriso, parse_line, prefix, separators, max_words,
                         &pargc, &pargv, pflag);

 /* Temporarily disable backslash encoding of result channel */
 bsl_mem= xorriso->bsl_interpretation;
 xorriso->bsl_interpretation&= ~32;

 xorriso->msg_sieve_disabled= 1;
 sprintf(xorriso->result_line, "%d\n", ret);
 Xorriso_result(xorriso, 1);
 if(ret == 1) {
   sprintf(xorriso->result_line, "%d\n", pargc);
   Xorriso_result(xorriso, 1);
   for(i= 0; i < pargc; i++) {
     text_pt= pargv[i];
     if (bsl_mem & 32) {
       ret= Sfile_bsl_encoder(&text, pargv[i], strlen(pargv[i]), 4);
       if(ret >  0)
         text_pt= text;
     }
     ret= Sfile_count_char(text_pt, '\n') + 1;
     sprintf(xorriso->result_line, "%d\n", ret);
     Xorriso_result(xorriso, 1);
     Sfile_str(xorriso->result_line, text_pt, 0);
     strcat(xorriso->result_line, "\n");
     Xorriso_result(xorriso, 1);
     Xorriso_free_meM(text);
     text= NULL;
   }
 } else {
   sprintf(xorriso->result_line, "0\n");
   Xorriso_result(xorriso, 1);
 }
 xorriso->bsl_interpretation= bsl_mem;
 ret= 1;
ex:;
 Xorriso__dispose_words(&pargc, &pargv);
 Xorriso_free_meM(text);
 Xorriso_free_meM(pline);
 return ret;
}


int Xorriso_msg_op_parse_bulk(struct XorrisO *xorriso,
                              char *prefix, char *separators,
                              int max_words, int pflag, int bulk_lines,
                              int flag)
{
 int ret, input_lines, i, j, l;
 char line[80];
 struct Xorriso_lsT *input_list= NULL, *input_end= NULL, *new_lst, *lst;
 char *pline= NULL;

 sprintf(xorriso->info_text,
    "Enter %d groups of lines. Each group begins by a line which tells the\n",
    bulk_lines);
 Xorriso_info(xorriso, 0);
 sprintf(xorriso->info_text,
    "number of following lines in the group. Then come the announced lines\n");
 Xorriso_info(xorriso, 0);
 sprintf(xorriso->info_text,
    "Do this blindly. No further prompt will appear. Best be a computer.\n");
 Xorriso_info(xorriso, 0);

 Xorriso_alloc_meM(pline, char, SfileadrL);

 for(i= 0; i < bulk_lines; i++) {
   ret= Xorriso_dialog_input(xorriso, line, sizeof(line), 8);
   if(ret <= 0)
     goto ex;
   input_lines= -1;
   sscanf(line, "%d", &input_lines);
   pline[0]= 0;
   for(j= 0; j < input_lines; j++) {
     l= strlen(pline);
     ret= Xorriso_dialog_input(xorriso, pline + l, SfileadrL - l - 1, 8);
     if(ret <= 0)
       goto ex;
     if(j < input_lines - 1)
       strcat(pline, "\n");
   }
   ret= Xorriso_lst_new(&new_lst, pline, input_end, 0);
   if(ret <= 0)
     goto ex;
   if(input_list == NULL)
     input_list= new_lst;
   input_end= new_lst;
 }
 
 for(lst= input_list; lst != NULL; lst= Xorriso_lst_get_next(lst, 0)) {
   ret= Xorriso_msg_op_parse(xorriso, Xorriso_lst_get_text(lst, 0),
                             prefix, separators, max_words, pflag,
                             input_lines, 1 | 2);
   if(ret <= 0)
     goto ex;
 }

 ret= 1;
ex:;
 Xorriso_lst_destroy_all(&input_list, 0);
 Xorriso_free_meM(pline);
 return(1);
}


int Xorriso_launch_frontend(struct XorrisO *xorriso, int argc, char **argv,
                            char *cmd_pipe_adr, char *reply_pipe_adr, int flag)
{
 int command_pipe[2], reply_pipe[2], ret, i, cpid, is_banned= 0;
 char **exec_argv= NULL, *sfe= NULL, *adrpt;
 struct stat stbuf;

 Xorriso_alloc_meM(sfe, char, 5 * SfileadrL);
 for(i= 0; i < 2; i++)
   command_pipe[i]= reply_pipe[i]= -1;

#ifndef Xorriso_allow_launch_frontenD
 /* To be controlled by: configure --enable-external-filters */
 
 sprintf(xorriso->info_text, "-launch_frontend : Banned at compile time.");
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
 sprintf(xorriso->info_text,
"This may be changed at compile time by ./configure option --enable-external-filters");
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
 is_banned= 1;

#endif /* ! Xorriso_allow_launch_frontenD */

#ifndef Xorriso_allow_launch_frontend_suiD

 /* To be controlled by: configure --enable-external-filters-setuid */

 if(getuid() != geteuid()) {
   sprintf(xorriso->info_text,
          "-set_filter: UID and EUID differ. Will not run external programs.");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   sprintf(xorriso->info_text,
"This may be changed at compile time by ./configure option --enable-external-filters-setuid");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
   is_banned= 1;
 }

#endif /* ! Xorriso_allow_launch_frontend_suiD */

 if(is_banned)
   {ret= 0; goto ex;}

 if(argc > 0) {
   if(strchr(argv[0], '/') == NULL) {
     sprintf(xorriso->info_text,
           "-launch_frontend : Command path does not contain a '/'-character");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }

   /* Add a NULL pointer for execv() */
   Xorriso_alloc_meM(exec_argv, char *, argc + 1);
   if(exec_argv == NULL) {
     Xorriso_no_malloc_memory(xorriso, NULL, 0);
     ret= -1; goto ex;
   }
   for(i= 0; i < argc; i++)
     exec_argv[i]= argv[i];
   exec_argv[argc]= NULL;
 } else if(cmd_pipe_adr[0] == 0 || reply_pipe_adr[0] == 0)
   {ret= 0; goto ex;}

 if(cmd_pipe_adr[0] && reply_pipe_adr[0]) {
   /* Create named pipes if needed */
   for(i= 0; i < 2; i++) {
     if(i == 0)
       adrpt= cmd_pipe_adr;
     else
       adrpt= reply_pipe_adr;
     ret= stat(adrpt, &stbuf);
     if(ret == -1) {
       ret= mknod(adrpt, S_IFIFO | S_IRWXU | S_IRWXG | S_IRWXO | S_IRWXO,
                  (dev_t) 0);
       if(ret == -1) {
          sprintf(xorriso->info_text,
                  "-launch_frontend: Cannot create named pipe %s",
                  Text_shellsafe(adrpt, sfe, 0));
          Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno,
                              "FAILURE", 0);
          ret= 0; goto ex;
       }
     }
   }
 } else {
   ret= pipe(command_pipe);
   if (ret == -1) {
no_pipe_open:
     sprintf(xorriso->info_text,
             "-launch_frontend: Failed to create a nameless pipe object");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
     ret= 0; goto ex;
   }
   ret= pipe(reply_pipe);
   if (ret == -1)
     goto no_pipe_open;
 }

 if(argc > 0) {
   cpid = fork();
   if (cpid == -1) {
     sprintf(xorriso->info_text,
             "-launch_frontend: Failed to create a child process");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
     ret= 0; goto ex;
   }
 } else
   cpid= -1; /* Dummy child id */
 
 if (cpid != 0) {
   /* Parent becomes the xorriso slave */

   if(cmd_pipe_adr[0] && reply_pipe_adr[0]) {
      command_pipe[0]= open(cmd_pipe_adr, O_RDONLY);
      if(command_pipe[0] == -1) {
         sprintf(xorriso->info_text,
                 "-launch_frontend: Failed to open named command pipe %s",
                 Text_shellsafe(cmd_pipe_adr, sfe, 0));
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno,
                             "FAILURE", 0);
         ret= 0; goto ex;
      }
      reply_pipe[1]= open(reply_pipe_adr, O_WRONLY | O_APPEND);
      if(reply_pipe[1] == -1) {
         sprintf(xorriso->info_text,
                 "-launch_frontend: Failed to open named reply pipe %s",
                 Text_shellsafe(reply_pipe_adr, sfe, 0));
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno,
                             "FAILURE", 0);
         ret= 0; goto ex;
      }
   } else {
     /* Close unused pipe ends */
     close(command_pipe[1]);
     close(reply_pipe[0]);
   }
   close(0);
   close(1);
   close(2);
   ret= dup2(command_pipe[0], 0);
   if(ret == -1) {
no_dup:;
     sprintf(xorriso->info_text,
  "-launch_frontend: Failed to connect pipe to xorriso standard i/o channels");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
     ret= -1; goto ex;
   }
   ret= dup2(reply_pipe[1], 1);
   if(ret == -1)
     goto no_dup;
   ret= dup2(reply_pipe[1], 2);
   if(ret == -1)
     goto no_dup;
   ret= 1; goto ex;
 }

 /* Child becomes the frontend program */

 /* Close unused pipe ends */;
 if(cmd_pipe_adr[0] && reply_pipe_adr[0]) {
   command_pipe[1]= open(cmd_pipe_adr, O_WRONLY | O_APPEND);
   if(command_pipe[1] == -1) {
     fprintf(stderr,
         "xorriso: -launch_frontend: Failed to open named command pipe '%s'\n",
         cmd_pipe_adr);
     perror("xorriso: -launch_frontend");
     exit(1); 
   }
   reply_pipe[0]= open(reply_pipe_adr, O_RDONLY);
   if(reply_pipe[0] == -1) {
     fprintf(stderr,
           "xorriso: -launch_frontend: Failed to open named reply pipe '%s'\n",
           reply_pipe_adr);
     exit(1); 
   }
 } else {
   close(command_pipe[0]);
   close(reply_pipe[1]);
 }
 close(0);
 close(1);
 ret= dup2(command_pipe[1], 1);
 if(ret == -1) {
   perror("xorriso: -launch_frontend: Error on redirecting standard output for frontend");
   exit(1);
 }
 ret= dup2(reply_pipe[0], 0);
 if(ret == -1) {
   perror("xorriso: -launch_frontend: Error on redirecting standard input for frontend");
   exit(1);
 }

 execv(exec_argv[0], exec_argv);
 fprintf(stderr, "xorriso: -launch_frontend: Failure to start program '%s'\n",
         exec_argv[0]);
 perror("xorriso: -launch_frontend");
 exit(1);
 
ex:;
 Xorriso_free_meM(exec_argv);
 Xorriso_free_meM(sfe);
 return(ret);
}

