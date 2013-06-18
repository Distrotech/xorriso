
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2013 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the implementation of options as mentioned in man page
   or info file derived from xorriso.texi.
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

#include "xorriso.h"
#include "xorriso_private.h"
#include "xorrisoburn.h"


/* Option -iso_rr_pattern "on"|"ls"|"off" */
int Xorriso_option_iso_rr_pattern(struct XorrisO *xorriso, char *mode,int flag)
{
 if(strcmp(mode, "off")==0)
   xorriso->do_iso_rr_pattern= 0;
 else if(strcmp(mode, "on")==0)
   xorriso->do_iso_rr_pattern= 1;
 else if(strcmp(mode, "ls")==0)
   xorriso->do_iso_rr_pattern= 2;
 else {
   sprintf(xorriso->info_text, "-iso_rr_pattern: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(1);
}


/* Option -jigdo aspect argument */
int Xorriso_option_jigdo(struct XorrisO *xorriso, char *aspect, char *arg,
                         int flag)
{
 int ret;

 ret= Xorriso_jigdo_interpreter(xorriso, aspect, arg, 0);
 return(ret);
}


/* Option -joliet "on"|"off" */
int Xorriso_option_joliet(struct XorrisO *xorriso, char *mode, int flag)
{
 if(strcmp(mode, "off")==0)
   xorriso->do_joliet= 0;
 else if(strcmp(mode, "on")==0)
   xorriso->do_joliet= 1;
 else {
   sprintf(xorriso->info_text, "-joliet: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(1);
}


/* Command -launch_frontend */
int Xorriso_option_launch_frontend(struct XorrisO *xorriso,
                                   int argc, char **argv, int *idx, int flag)
{
 int ret, end_idx;

 end_idx= Xorriso_end_idx(xorriso, argc, argv, *idx, 1);

 if(xorriso->launch_frontend_banned) {
   sprintf(xorriso->info_text,
           "-launch_frontend was already executed in this xorriso run");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   ret= 0; goto ex;
 }
 xorriso->launch_frontend_banned= 1;
 if(end_idx <= *idx)
   {ret= 1; goto ex;}
 if(argv[*idx][0] == 0)
   {ret= 1; goto ex;}
 xorriso->dialog= 2;
 ret= Xorriso_launch_frontend(xorriso, end_idx - *idx, argv + *idx,
                              "", "", 0);
ex:;
 (*idx)= end_idx;
 return(ret);
}


/* Option -list_arg_sorting */
int Xorriso_option_list_arg_sorting(struct XorrisO *xorriso, int flag)
{
 int ret;

 ret= Xorriso_cmd_sorting_rank(xorriso, 0, NULL, 0, 1);
 return(ret);
}


/* Option -list_delimiter */
int Xorriso_option_list_delimiter(struct XorrisO *xorriso, char *text,
                                  int flag)
{
 int ret, argc;
 char **argv= NULL;

 if(text[0] == 0) {
   sprintf(xorriso->info_text,
           "-list_delimiter: New delimiter text is empty");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 if(strlen(text) > 80) {
   sprintf(xorriso->info_text,
           "-list_delimiter: New delimiter text is too long");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 ret= Sfile_make_argv(xorriso->progname, text, &argc, &argv, 4);
 if(ret > 0) {
   if(argc > 2) {
     sprintf(xorriso->info_text,
            "-list_delimiter: New delimiter text contains more than one word");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   }
   Sfile_make_argv(xorriso->progname, text, &argc, &argv, 2);
   if(argc > 2)
     return(0);
 }
 if(strchr(text, '"') != NULL || strchr(text, '\'') != NULL) {
   sprintf(xorriso->info_text,
           "-list_delimiter: New delimiter text contains quotation marks");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 strcpy(xorriso->list_delimiter, text);
 return(1);
}


/* Option -list_extras */
int Xorriso_option_list_extras(struct XorrisO *xorriso, char *mode, int flag)
{
 int ret;

 ret= Xorriso_list_extras(xorriso, mode, 0);
 return(ret);
}


/* Option -list_formats */
int Xorriso_option_list_formats(struct XorrisO *xorriso, int flag)
{
 int ret;

 ret= Xorriso_list_formats(xorriso, 0);
 return(ret);
}


/* Option -list_speeds */
int Xorriso_option_list_speeds(struct XorrisO *xorriso, int flag)
{
 int ret;

 ret= Xorriso_list_speeds(xorriso, 0);
 return(ret);
}


/* Option -list_profiles */
int Xorriso_option_list_profiles(struct XorrisO *xorriso, char *which,
                                 int flag)
{
 int ret;
 int mode= 0;

 if(strncmp(which,"in",2)==0)
   mode|= 1;
 else if(strncmp(which,"out",3)==0)
   mode|= 2;
 else
   mode|= 3;
 if(mode & 1) {
   ret= Xorriso_toc(xorriso, 1 | 16 | 32);
   if(ret > 0)
     Xorriso_list_profiles(xorriso, 0);
 }
 if((mode & 2) && xorriso->in_drive_handle != xorriso->out_drive_handle) {
   ret= Xorriso_toc(xorriso, 1 | 2 | 16 | 32);
   if(ret > 0)
     Xorriso_list_profiles(xorriso, 2);
 }
 return(1);
}


/* Command -lns alias -lnsi */
int Xorriso_option_lnsi(struct XorrisO *xorriso, char *target, char *path,
                        int flag)
{
 int ret;
 char *eff_path= NULL;

 Xorriso_alloc_meM(eff_path, char, SfileadrL);

 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, path, eff_path, 1);
 if(ret < 0)
   {ret= 0; goto ex;}
 if(ret > 0) { 
   sprintf(xorriso->info_text, "-lns: Address already existing: ");
   Text_shellsafe(eff_path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 } 
 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, path, eff_path, 2);
 if(ret < 0)
   {ret= 0; goto ex;}
 ret= Xorriso_graft_in(xorriso, NULL, target, eff_path, (off_t) 0, (off_t) 0,
                       1024);
 if(ret <= 0)
   {ret= 0; goto ex;}
 ret= 1;
ex:;
 Xorriso_free_meM(eff_path);
 return(ret);
}


/* Option -load session|track|sbsector value */
/* @param flag bit0= with adr_mode sbsector: adr_value is possibly 16 too high
   @return <=0 error , 1 success, 2 revoked by -reassure
*/
int Xorriso_option_load(struct XorrisO *xorriso, char *adr_mode,
                          char *adr_value, int flag)
{
 int ret;

 if(Xorriso_change_is_pending(xorriso, 0)) {
   sprintf(xorriso->info_text,
           "-load: Image changes pending. -commit or -rollback first");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 ret= Xorriso_reassure(xorriso, "-load", "loads an alternative image", 0);
 if(ret<=0)
   return(2);
 ret= Xorriso_decode_load_adr(xorriso, "-load", adr_mode, adr_value,
                              &(xorriso->image_start_mode),
                              xorriso->image_start_value, flag & 1);
 if(ret <= 0)
   return(ret);
 xorriso->image_start_mode|= (1<<30); /* enable non-default msc1 processing */
 if(strlen(xorriso->indev)>0) {
   ret= Xorriso_option_rollback(xorriso, 1); /* Load image, no -reassure */
   if(ret<=0)
     return(ret);
 }
 return(1);
}


/* Option -logfile */
int Xorriso_option_logfile(struct XorrisO *xorriso, char *channel,
                                                      char *fileadr, int flag)
{
 int hflag,channel_no= 0, ret;
   
 if(channel[0]==0) {
logfile_wrong_form:;
   sprintf(xorriso->info_text,"Wrong form. Correct would be: -logfile \".\"|\"R\"|\"I\"|\"M\" file_address");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 hflag= 2;
 if(channel[0]=='R')
   channel_no= 1;
 else if(channel[0]=='I')
   channel_no= 2;
 else if(channel[0]=='M')
   channel_no= 3;
 else if(channel[0]=='.')
   hflag= 4;
 else
   goto logfile_wrong_form;
 if(strcmp(fileadr,"-")==0 || fileadr[0]==0)
     hflag|= (1<<15);
 xorriso->logfile[channel_no][0]= 0;
 ret= Xorriso_write_to_channel(xorriso, fileadr, channel_no, hflag);
 if(ret<=0) {
   sprintf(xorriso->info_text, "Cannot open logfile:  %s", fileadr);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
 } else if(!(hflag&(1<<15)))
   if(Sfile_str(xorriso->logfile[channel_no], fileadr, 0)<=0)
     return(-1);
 return(ret>0);
}


/* Options -ls  alias -lsi   and -lsl  alias -lsli
       and -lsd alias -lsdi  and -lsdl alias -lsdli
       and -du  alias -dui   and -dus  alias -dusi
   @param flag bit0= long format (-lsl , -du)
               bit1= do not expand patterns but use literally
               bit2= du rather than ls
               bit3= list directories as themselves (ls -d) 
*/
int Xorriso_option_lsi(struct XorrisO *xorriso, int argc, char **argv,
                      int *idx, int flag)
{
 int ret, end_idx, filec= 0, nump, i;
 char **filev= NULL, **patterns= NULL;
 off_t mem= 0;
 struct stat stbuf;

 end_idx= Xorriso_end_idx(xorriso, argc, argv, *idx, 1);
 if(xorriso->do_iso_rr_pattern==0)
   flag|= 2;

 nump= end_idx - *idx;
 if((flag&2) && nump>0 ) {
   ;
 } else if(nump <= 0) {
   if(Xorriso_iso_lstat(xorriso, xorriso->wdi, &stbuf, 0)<0) {
     sprintf(xorriso->info_text,
             "Current -cd path does not yet exist in the ISO image");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
     {ret= 0; goto ex;}
   }
   if(!S_ISDIR(stbuf.st_mode)) {
     sprintf(xorriso->info_text,
             "Current -cd meanwhile points to a non-directory in ISO image");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
     {ret= 0; goto ex;}
   }
   patterns= calloc(1, sizeof(char *));
   if(patterns == NULL) {
no_memory:;
     sprintf(xorriso->info_text,
             "Cannot allocate enough memory for pattern expansion");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
     {ret= -1; goto ex;}
   }
   nump= 1;
   if(flag&8)
     patterns[0]= ".";
   else 
     patterns[0]= "*";
   flag&= ~2;
 } else {
   patterns= calloc(nump, sizeof(char *));
   if(patterns==NULL)
     goto no_memory;
   for(i= 0; i<nump; i++) {
     if(argv[i + *idx][0]==0)
       patterns[i]= "*";
     else
       patterns[i]= argv[i + *idx];
   }
 }

 if((flag & 1) && !(xorriso->ino_behavior & 32)) {
   ret= Xorriso_make_hln_array(xorriso, 0); /* for stbuf.st_nlink */
   if(ret < 0)
     return(ret);
 }
 if(flag&2) {
   ret= Xorriso_ls_filev(xorriso, xorriso->wdi, nump, argv + (*idx), mem,
                         flag&(1|4|8)); 
 } else if(nump==1 && strcmp(patterns[0],"*")==0 && !(flag&4)){
   /* save temporary memory by calling simpler function */
   ret= Xorriso_ls(xorriso, (flag&1)|4);
 } else {
   ret= Xorriso_expand_pattern(xorriso, nump, patterns, 0, &filec, &filev,
                               &mem, 0);
   if(ret<=0)
     {ret= 0; goto ex;}
   ret= Xorriso_ls_filev(xorriso, xorriso->wdi, filec, filev, mem,
                         flag&(1|4|8)); 
 }
 if(ret<=0)
   {ret= 0; goto ex;}

 ret= 1;
ex:;
 if(patterns!=NULL)
   free((char *) patterns);
 Sfile_destroy_argv(&filec, &filev, 0);
 (*idx)= end_idx;
 return(ret);
}


/* Options -lsx, -lslx, -lsdx , -lsdlx , -dux , -dusx
   @param flag bit0= long format (-lslx , -dux)
               bit1= do not expand patterns but use literally
               bit2= du rather than ls
               bit3= list directories as themselves (ls -d) 
*/
int Xorriso_option_lsx(struct XorrisO *xorriso, int argc, char **argv,
                      int *idx, int flag)
{
 int ret, end_idx, filec= 0, nump, i;
 char **filev= NULL, **patterns= NULL;
 off_t mem= 0;

 end_idx= Xorriso_end_idx(xorriso, argc, argv, *idx, 1|2);
 if(xorriso->do_disk_pattern==0)
   flag|= 2;

 nump= end_idx - *idx;
 if((flag&2) && nump>0) {
   ;
 } else if(nump <= 0) {
   patterns= calloc(1, sizeof(char *));
   if(patterns == NULL) {
no_memory:;
     sprintf(xorriso->info_text,
             "Cannot allocate enough memory for pattern expansion");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
     {ret= -1; goto ex;}
   }
   nump= 1;
   if(flag&8)
     patterns[0]= ".";
   else
     patterns[0]= "*";
   flag&= ~2;
 } else {
   patterns= calloc(nump, sizeof(char *));
   if(patterns==NULL)
     goto no_memory;
   for(i= 0; i<nump; i++) {
     if(argv[i + *idx][0]==0)
       patterns[i]= "*";
     else
       patterns[i]= argv[i + *idx];
   }
 }
 if(flag&2) {
   ret= Xorriso_lsx_filev(xorriso, xorriso->wdx,
                          nump, argv + (*idx), mem, flag&(1|4|8)); 

#ifdef Not_yeT
 } else if(nump==1 && strcmp(patterns[0],"*")==0 && !(flag&4)){
   /* save temporary memory by calling simpler function */
   ret= Xorriso_ls(xorriso, (flag&1)|4);
#endif

 } else {
   ret= Xorriso_expand_disk_pattern(xorriso, nump, patterns, 0, &filec, &filev,
                                    &mem, 0);
   if(ret<=0)
     {ret= 0; goto ex;}
   ret= Xorriso_lsx_filev(xorriso, xorriso->wdx, filec, filev, mem,
                          flag&(1|4|8)); 
 }
 if(ret<=0)
   {ret= 0; goto ex;}

 ret= 1;
ex:;
 if(patterns!=NULL)
   free((char *) patterns);
 Sfile_destroy_argv(&filec, &filev, 0);
 (*idx)= end_idx;
 return(ret);
}


/* Option -map , -map_single */
/* @param flag bit0=do not report the added item
               bit1=do not reset pacifier, no final pacifier message
               bit5=eventually do not insert directory tree
*/
int Xorriso_option_map(struct XorrisO *xorriso, char *disk_path,
                       char *iso_path, int flag)
{
 int ret;
 char *eff_origin= NULL, *eff_dest= NULL, *ipth;

 Xorriso_alloc_meM(eff_origin, char, SfileadrL);
 Xorriso_alloc_meM(eff_dest, char, SfileadrL);

 if(!(flag&2))
   Xorriso_pacifier_reset(xorriso, 0);

 ipth= iso_path;
 if(ipth[0]==0)
   ipth= disk_path;
 if(disk_path[0]==0) {
   sprintf(xorriso->info_text, "-map: Empty disk_path given");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 1);
   {ret= 0; goto ex;}
 }
 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdx, disk_path, eff_origin,
                                 2|4);
 if(ret<=0)
   goto ex;
 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, ipth, eff_dest, 2);
 if(ret<=0)
   goto ex;
 ret= Xorriso_graft_in(xorriso, NULL, eff_origin, eff_dest,
                       (off_t) 0, (off_t) 0, 2|(flag&32));
 if(!(flag&2))
   Xorriso_pacifier_callback(xorriso, "files added", xorriso->pacifier_count,
                             xorriso->pacifier_total, "", 1);
 if(ret<=0)
   goto ex;

 if(!(flag&1)) {
   sprintf(xorriso->info_text, "Added to ISO image: %s '%s'='%s'\n",
           (ret>1 ? "directory" : "file"), (eff_dest[0] ? eff_dest : "/"),
           eff_origin);
   Xorriso_info(xorriso,0);
 }
 ret= 1;
ex:;
 Xorriso_free_meM(eff_origin);
 Xorriso_free_meM(eff_dest);
 return(ret);
}


/* Options -map_l , -compare_l , -update_l , -extract_l */
/* @param flag bit4= do not establish and dispose xorriso->di_array
                     for update_l
               bit8-11= mode 0= -map_l
                             1= -compare_l
                             2= -update_l
                             3= -extract_l
*/
int Xorriso_option_map_l(struct XorrisO *xorriso, int argc, char **argv,
                         int *idx, int flag)
{
 int ret, end_idx, optc= 0, was_failure= 1, i, fret, mode, problem_count;
 int ns_flag= 2|4, nt_flag= 2, opt_args_flag= 2;
 char *source_prefix= NULL, *target_prefix= NULL, *cmd, **optv= NULL;
 char *eff_source= NULL, *eff_target= NULL, *source_pt, *s_wd, *t_wd;
 char **eff_src_array= NULL, **eff_tgt_array= NULL;

 cmd= "-map_l";
 s_wd= xorriso->wdx;
 t_wd= xorriso->wdi;
 Xorriso_pacifier_reset(xorriso, 0);
 mode= (flag>>8) & 15;

 if(mode==1)
   cmd= "-compare_l";
 else if(mode==2)
   cmd= "-update_l";
 else if(mode==3) {
   cmd= "-extract_l";
   ns_flag= 2;
   s_wd= xorriso->wdi;
   nt_flag= 2|4;
   t_wd= xorriso->wdx;
   opt_args_flag= 0;
 }

 end_idx= Xorriso_end_idx(xorriso, argc, argv, *idx, 1|2);
 if(end_idx - (*idx) < 3) {
   sprintf(xorriso->info_text, "%s: Not enough arguments given (%d < 3)", cmd,
           end_idx - (*idx));
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 1);
   ret= 0; goto ex;
 }

 Xorriso_alloc_meM(source_prefix, char, SfileadrL);
 Xorriso_alloc_meM(target_prefix, char, SfileadrL);
 Xorriso_alloc_meM(eff_source, char, SfileadrL);
 Xorriso_alloc_meM(eff_target, char, SfileadrL);

 ret= Xorriso_normalize_img_path(xorriso, s_wd, argv[*idx],
                                 source_prefix, ns_flag | 64);
 if(ret<=0)
   goto ex;
 ret= Xorriso_normalize_img_path(xorriso, t_wd, argv[(*idx)+1],
                                 target_prefix, nt_flag);
 if(ret<=0)
   goto ex;
 ret= Xorriso_opt_args(xorriso, cmd, argc, argv, (*idx)+2, &end_idx,
                       &optc, &optv, opt_args_flag);
 if(ret<=0)
   goto ex;


 if(mode == 3 &&
    (xorriso->do_restore_sort_lba || !(xorriso->ino_behavior & 4))) {
   eff_src_array= calloc(optc, sizeof(char *));
   eff_tgt_array= calloc(optc, sizeof(char *));
   if(eff_src_array == NULL || eff_tgt_array == NULL) {
     Xorriso_no_malloc_memory(xorriso, NULL, 0);
     ret= -1; goto ex;
   }
   for(i= 0; i < optc; i++)
     eff_src_array[i]= eff_tgt_array[i]= NULL;
 }
 if(mode == 2 && !((xorriso->ino_behavior & 2) || (flag & 16) ||
                   xorriso->di_array != NULL)) {
   /* Create all-image node array sorted by isofs.di */
   ret= Xorriso_make_di_array(xorriso, 0);
   if(ret <= 0)
     goto ex;
 }

 for(i= 0; i<optc; i++) {
   ret= Xorriso_normalize_img_path(xorriso, s_wd, optv[i],
                                   eff_source, ns_flag);
   if(ret<=0)
     goto ex;
   strcpy(eff_target, target_prefix);
   source_pt= eff_source;
   if(source_prefix[0]) {
     if(strncmp(source_prefix, eff_source, strlen(source_prefix))!=0) {
       sprintf(xorriso->info_text, "%s: disk_path ", cmd);
       Text_shellsafe(eff_source, xorriso->info_text, 1);
       strcat(xorriso->info_text, " does not begin with disk_prefix ");
       Text_shellsafe(source_prefix, xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 1);
       ret= 0; goto ex;
     }
     source_pt+= strlen(source_prefix);
   }
   strcat(eff_target, source_pt);

   if(mode==0)
     ret= Xorriso_option_map(xorriso, eff_source, eff_target, 2);
   else if(mode==1)
     ret= Xorriso_option_compare(xorriso, eff_source, eff_target, 2|8);
   else if(mode==2)
     ret= Xorriso_option_update(xorriso, eff_source, eff_target, 2 | 8 | 16);
   else if(mode==3) {
     if(eff_src_array != NULL) {
       eff_src_array[i]= strdup(eff_source);
       eff_tgt_array[i]= strdup(eff_target);
       if(eff_src_array[i] == NULL || eff_tgt_array[i] == NULL) {
         Xorriso_no_malloc_memory(xorriso, &(eff_src_array[i]), 0);
         ret= -1; goto ex;
       }
     } else {
       ret= Xorriso_option_extract(xorriso, eff_source, eff_target, 2 | 4);
     }
   }

   if(ret>0 && !xorriso->request_to_abort)
 continue; /* regular bottom of loop */
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1 | 2);
   if(fret>=0)
 continue;
   goto ex;
 }

 ret= 1;
 if(mode == 3 && eff_src_array != NULL) {
   ret= Xorriso_lst_append_binary(&(xorriso->node_disk_prefixes),
                                  target_prefix, strlen(target_prefix) + 1, 0);
   if(ret <= 0)
     goto ex;
   ret= Xorriso_lst_append_binary(&(xorriso->node_img_prefixes),
                                  source_prefix, strlen(source_prefix) + 1, 0);
   if(ret <= 0)
     goto ex;
   ret= Xorriso_restore_sorted(xorriso, optc, eff_src_array, eff_tgt_array,
                               &problem_count, 0);
   if(ret <= 0 || problem_count > 0)
     was_failure= 1;
 }
 if(mode==0)
   Xorriso_pacifier_callback(xorriso, "files added", xorriso->pacifier_count,
                             xorriso->pacifier_total, "", 1);
 else if(mode==1 || mode==2)
   Xorriso_pacifier_callback(xorriso, "content bytes read",
                             xorriso->pacifier_count, 0, "", 1 | 8 | 32);
 else if(mode==3)
   Xorriso_pacifier_callback(xorriso, "files restored",xorriso->pacifier_count,
                             xorriso->pacifier_total, "", 1|4);
ex:;
 Xorriso_destroy_node_array(xorriso, 0);
 i= optc;
 Sfile_destroy_argv(&i, &eff_src_array, 0);
 i= optc;
 Sfile_destroy_argv(&i, &eff_tgt_array, 0);
 Xorriso_free_meM(source_prefix);
 Xorriso_free_meM(target_prefix);
 Xorriso_free_meM(eff_source);
 Xorriso_free_meM(eff_target);
 (*idx)= end_idx;
 Xorriso_opt_args(xorriso, cmd, argc, argv, *idx, &end_idx, &optc, &optv, 256);
 if(ret<=0)
   return(ret);
 return(!was_failure);
}


/* Option -mark */
int Xorriso_option_mark(struct XorrisO *xorriso, char *mark, int flag)
{
 if(mark[0]==0)
   xorriso->mark_text[0]= 0;
 else
   strncpy(xorriso->mark_text,mark,sizeof(xorriso->mark_text)-1);
 xorriso->mark_text[sizeof(xorriso->mark_text)-1]= 0;
 return(1);
}


/* Option -md5 "on"|"all"|"off" */
int Xorriso_option_md5(struct XorrisO *xorriso, char *mode, int flag)
{
 char *npt, *cpt;
 int l;

 npt= cpt= mode;
 for(; npt!=NULL; cpt= npt+1) {
   npt= strchr(cpt,':');
   if(npt==NULL)
     l= strlen(cpt);
   else
     l= npt-cpt;
   if(l == 0)
 continue;
   if(l == 3 && strncmp(cpt, "off", l) == 0)
     xorriso->do_md5&= ~31;
   else if(l == 2 && strncmp(cpt, "on", l) == 0)
     xorriso->do_md5= (xorriso->do_md5 & ~31) | 7 | 16;
   else if(l == 3 && strncmp(cpt, "all", l) == 0)
     xorriso->do_md5|= 31;
   else if(l == 18 && strncmp(cpt, "stability_check_on", l) == 0)
     xorriso->do_md5|= 8;
   else if(l == 19 && strncmp(cpt, "stability_check_off", l) == 0)
     xorriso->do_md5&= ~8;
   else if(l == 13 && strncmp(cpt, "load_check_on", l) == 0)
     xorriso->do_md5&= ~32;
   else if(l == 14 && strncmp(cpt, "load_check_off", l) == 0)
     xorriso->do_md5|= 32;
   else {
     sprintf(xorriso->info_text, "-md5: unknown mode ");
     Text_shellsafe(cpt, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
 }
 return(1);
}


/* Option -mkdir alias -mkdiri */
int Xorriso_option_mkdiri(struct XorrisO *xorriso, int argc, char **argv,
                          int *idx, int flag)
{
 int i, end_idx, ret, was_failure= 0, fret;

 end_idx= Xorriso_end_idx(xorriso, argc, argv, *idx, 0);

 for(i= *idx; i<end_idx; i++) {
   ret= Xorriso_mkdir(xorriso, argv[i], 0);
   if(ret>0 && !xorriso->request_to_abort)
 continue; /* regular bottom of loop */
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret>=0)
 continue;
   goto ex;
 }
 ret= 1; 
ex:;
 (*idx)= end_idx;
 if(ret<=0)
   return(ret);
 return(!was_failure);
}


/* Options -mount , -mount_cmd , -session_string */
/* @param bit0= -mount_cmd: print mount command to result channel rather
                            than performing it
          bit1= perform -session_string rather than -mount_cmd
*/
int Xorriso_option_mount(struct XorrisO *xorriso, char *dev, char *adr_mode,
                         char *adr, char *cmd, int flag)
{
 int ret, entity_code= 0, m_flag;
 char entity_id[81], *mnt;

 if(flag & 1)
   mnt= "-mount_cmd";
 else if(flag & 2)
   mnt= "-session_string";
 else {
   mnt= "-mount";
   if(xorriso->allow_restore <= 0) {
     sprintf(xorriso->info_text,
          "-mount: image-to-disk features are not enabled by option -osirrox");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
   if(Xorriso_change_is_pending(xorriso, 0)) {
     sprintf(xorriso->info_text,
             "%s: Image changes pending. -commit or -rollback first", mnt);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
 }
 ret= Xorriso_decode_load_adr(xorriso, mnt, adr_mode, adr,
                              &entity_code, entity_id, 0);
 if(ret <= 0)
   return(ret);
 if(flag & 2)
   m_flag= 1 | 4;
 else
   m_flag= (flag & 1) | 2;
 ret= Xorriso_mount(xorriso, dev, entity_code, entity_id, cmd, m_flag);
 return(ret);
}


/* Option -mount_opts option[:...] */
int Xorriso_option_mount_opts(struct XorrisO *xorriso, char *mode, int flag)
{
 int was, l;
 char *cpt, *npt;

 was= xorriso->mount_opts_flag;
 npt= cpt= mode;
 for(cpt= mode; npt!=NULL; cpt= npt+1) {
   npt= strchr(cpt,':');
   if(npt==NULL)
     l= strlen(cpt);
   else
     l= npt-cpt;
   if(l==0)
     goto unknown_mode;
   if(strncmp(cpt, "shared", l)==0) {
     xorriso->mount_opts_flag|= 1;
   } else if(strncmp(cpt, "exclusive", l)==0) {
     xorriso->mount_opts_flag&= ~1;
   } else {
unknown_mode:;
     if(l<SfileadrL)
       sprintf(xorriso->info_text, "-mount_opts: unknown option '%s'", cpt);
     else
       sprintf(xorriso->info_text, "-mount_opts: oversized parameter (%d)",l);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     xorriso->mount_opts_flag= was;
     return(0);
   }
 }
 return(1);
}


/* Command -move */
int Xorriso_option_move(struct XorrisO *xorriso, char *origin, char *dest,
                        int flag)
{
 int ret;
 char *eff_origin= NULL, *eff_dest= NULL;

 Xorriso_alloc_meM(eff_origin, char, SfileadrL);
 Xorriso_alloc_meM(eff_dest, char, SfileadrL);

 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, origin, eff_origin, 0);
 if(ret <= 0)
   goto ex;
 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, dest, eff_dest, 0);
 if(ret <= 0)
   goto ex;
 ret= Xorriso_rename(xorriso, NULL, eff_origin, eff_dest, 0);
 if(ret <= 0)
   goto ex;
 ret= 1;
ex:;
 Xorriso_free_meM(eff_origin);
 Xorriso_free_meM(eff_dest);
 return(ret);
}

 
/* Command -msg_op */
int Xorriso_option_msg_op(struct XorrisO *xorriso, char *what, char *arg,
                          int flag)
{
 int ret, available, argc, pargc, i, pflag, max_words, input_lines, msd_mem;
 char **argv= NULL, **pargv= NULL, *msg= "";
 char *prefix, *separators;

 msd_mem= xorriso->msg_sieve_disabled;

 ret= 1;
 if(strcmp(what, "parse") == 0 || strcmp(what, "parse_bulk") == 0) {
   ret= Xorriso_parse_line(xorriso, arg, "", "", 5, &argc, &argv, 0);
   prefix= "";
   if(argc > 0) 
     prefix= argv[0];
   separators= "";
   if(argc > 1)
     separators= argv[1];
   max_words= 0;
   if(argc > 2)
     sscanf(argv[2], "%d", &max_words);
   pflag= 0;
   if(argc > 3)
     sscanf(argv[3], "%d", &pflag);
   input_lines= 1;
   if(argc > 4)
     sscanf(argv[4], "%d", &input_lines);
   if(strcmp(what, "parse") == 0) {
     ret= Xorriso_msg_op_parse(xorriso, "", prefix, separators,
                               max_words, pflag, input_lines, 0);
   } else {
     ret= Xorriso_msg_op_parse_bulk(xorriso, prefix, separators,
                               max_words, pflag, input_lines, 0);
   }
   if(ret <= 0)
     goto ex;
   xorriso->msg_sieve_disabled= msd_mem;
   Xorriso__dispose_words(&argc, &argv);
 } else if(strcmp(what, "start_sieve") == 0) {
   Xorriso_sieve_dispose(xorriso, 0);
   ret= Xorriso_sieve_big(xorriso, 0);
   msg= "Message sieve enabled";

/* >>> } else if(strcmp(what, "add_filter_rule") == 0) { */;

 } else if(strcmp(what, "clear_sieve") == 0) {
   ret= Xorriso_sieve_clear_results(xorriso, 0);
   msg= "Recorded message sieve results disposed";
 } else if(strcmp(what, "end_sieve") == 0) {
   ret= Xorriso_sieve_dispose(xorriso, 0);
   msg= "Message sieve disabled";
 } else if(strcmp(what, "read_sieve") == 0) {
   ret= Xorriso_sieve_get_result(xorriso, arg, &pargc, &pargv, &available, 0);
   xorriso->msg_sieve_disabled= 1;
   sprintf(xorriso->result_line, "%d\n", ret);
   Xorriso_result(xorriso, 1);
   if(ret > 0) {
     sprintf(xorriso->result_line, "%d\n", pargc);
     Xorriso_result(xorriso, 1);
     for(i= 0; i < pargc; i++) {
       ret= Sfile_count_char(pargv[i], '\n') + 1;
       sprintf(xorriso->result_line, "%d\n", ret);
       Xorriso_result(xorriso, 1);
       Sfile_str(xorriso->result_line, pargv[i], 0);
       strcat(xorriso->result_line, "\n");
       Xorriso_result(xorriso, 1);
     }
   } else {
     strcpy(xorriso->result_line, "0\n");
     Xorriso_result(xorriso, 1);
     available= 0;
   }
   sprintf(xorriso->result_line, "%d\n", available);
   Xorriso_result(xorriso, 1);
   xorriso->msg_sieve_disabled= msd_mem;
   Xorriso__dispose_words(&pargc, &pargv);
   ret= 1;
 } else if(strcmp(what, "show_sieve") == 0) {
   ret= Xorriso_sieve_get_result(xorriso, "", &pargc, &pargv, &available, 8);
   xorriso->msg_sieve_disabled= 1;
   sprintf(xorriso->result_line, "%d\n", ret);
   Xorriso_result(xorriso, 1);
   if(ret > 0) {
     sprintf(xorriso->result_line, "%d\n", pargc);
     Xorriso_result(xorriso, 1);
     for(i= 0; i < pargc; i++) {
       sprintf(xorriso->result_line, "%s\n", pargv[i]);
       Xorriso_result(xorriso, 1);
     }
   }
   xorriso->msg_sieve_disabled= msd_mem;
   Xorriso__dispose_words(&pargc, &pargv);
 } else if(strcmp(what, "compare_sev") == 0) {
   ret= Xorriso_parse_line(xorriso, arg, "", ",", 2, &argc, &argv, 0);
   if(argc < 2) {
     sprintf(xorriso->info_text,
             "-msg_op cmp_sev: malformed severity pair '%s'", arg);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   } else {
     ret= Xorriso__severity_cmp(argv[0], argv[1]);
     sprintf(xorriso->result_line, "%d\n", ret);
     Xorriso_result(xorriso, 1);
   }
   Xorriso__dispose_words(&argc, &argv);
 } else if(strcmp(what, "list_sev") == 0) {
   sprintf(xorriso->result_line, "%s\n", Xorriso__severity_list(0));
   Xorriso_result(xorriso, 1);
 } else {
   sprintf(xorriso->info_text, "-msg_op: unknown operation '%s'", what);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   ret= 0;
 }
 if(ret > 0 && msg[0])
   Xorriso_msgs_submit(xorriso, 0, msg, 0, "NOTE", 0);

ex:;
 xorriso->msg_sieve_disabled= msd_mem;
 return(ret);
}


/* Option -mv alias -mvi */
int Xorriso_option_mvi(struct XorrisO *xorriso, int argc, char **argv,
                      int *idx, int flag)
{
 int i, end_idx_dummy, ret, is_dir= 0, was_failure= 0, fret;
 char *eff_origin= NULL, *eff_dest= NULL, *dest_dir= NULL;
 char *leafname= NULL;
 int optc= 0;
 char **optv= NULL;

 Xorriso_alloc_meM(eff_origin, char, SfileadrL);
 Xorriso_alloc_meM(eff_dest, char, SfileadrL);
 Xorriso_alloc_meM(dest_dir, char, SfileadrL);
 Xorriso_alloc_meM(leafname, char, SfileadrL);

 ret= Xorriso_cpmv_args(xorriso, "-mvi", argc, argv, idx,
                        &optc, &optv, eff_dest, 0);
 if(ret<=0)
   goto ex;
 if(ret==2) {
   is_dir= 1;
   strcpy(dest_dir, eff_dest);
 }
 /* Perform movements */
 for(i= 0; i<optc; i++) {
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi,optv[i],eff_origin,0);
   if(ret<=0 || xorriso->request_to_abort)
     goto problem_handler;
   if(is_dir) {
     ret= Sfile_leafname(eff_origin, leafname, 0);
     if(ret<=0)
       goto problem_handler;
     strcpy(eff_dest, dest_dir);
     ret= Sfile_add_to_path(eff_dest, leafname, 0);
     if(ret<=0) {
       sprintf(xorriso->info_text, "Effective path gets much too long (%d)",
             (int) (strlen(eff_dest)+strlen(leafname)+1));
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       goto problem_handler;
     }
   }
   ret= Xorriso_rename(xorriso, NULL, eff_origin, eff_dest, 0);
   if(ret<=0 || xorriso->request_to_abort)
     goto problem_handler;
   sprintf(xorriso->info_text, "Renamed in ISO image: ");
   Text_shellsafe(eff_origin, xorriso->info_text, 1);
   strcat(xorriso->info_text, " to ");
   Text_shellsafe(eff_dest, xorriso->info_text, 1 | 2);
   strcat(xorriso->info_text, "\n");
   Xorriso_info(xorriso, 0);

 continue; /* regular bottom of loop */
problem_handler:;
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret>=0)
 continue;
   goto ex;
 }
 ret= !was_failure;
ex:;
 Xorriso_free_meM(eff_origin);
 Xorriso_free_meM(eff_dest);
 Xorriso_free_meM(dest_dir);
 Xorriso_free_meM(leafname);
 Xorriso_opt_args(xorriso, "-mvi",
                  argc, argv, *idx, &end_idx_dummy, &optc, &optv, 256);
 return(ret);
}


/* Option -no_rc */
int Xorriso_option_no_rc(struct XorrisO *xorriso, int flag)
{
 xorriso->no_rc= 1;
 return(1);
}


/* Option -not_leaf , (-hide_disk_leaf resp. -as mkisofs -hide) */
/* @param flag  bit0-bit5= hide rather than adding to disk_exclusions
                  bit0= add to iso_rr_hidings
                  bit1= add to joliet_hidings
                  bit2= add to hfsplus_hidings
*/
int Xorriso_option_not_leaf(struct XorrisO *xorriso, char *pattern, int flag)
{
 regex_t re;
 char *regexpr= NULL;
 int ret= 0;

 Xorriso_alloc_meM(regexpr, char, 2 * SfileadrL + 2);

 if(pattern[0]==0)
   {ret= 0; goto cannot_add;}
 Xorriso__bourne_to_reg(pattern, regexpr, 0);
 if(regcomp(&re, regexpr, 0)!=0)
   {ret= 0; goto cannot_add;}
 if(flag & 63) {
   if(flag & 1) {
     ret= Exclusions_add_not_leafs(xorriso->iso_rr_hidings, pattern, &re, 0);
     if(ret<=0)
       goto cannot_add;
   }
   if(flag & 2) {
     ret= Exclusions_add_not_leafs(xorriso->joliet_hidings, pattern, &re, 0);
     if(ret<=0)
       goto cannot_add;
   }
   if(flag & 4) {
     ret= Exclusions_add_not_leafs(xorriso->hfsplus_hidings, pattern, &re, 0);
     if(ret<=0)
       goto cannot_add;
   }
 } else {
   ret= Exclusions_add_not_leafs(xorriso->disk_exclusions, pattern, &re, 0);
 }
 if(ret<=0) {
cannot_add:;
   sprintf(xorriso->info_text,"Cannot add pattern: %s ",
           (flag & 3) ? "-hide_disk_leaf" : "-not_leaf");
   Text_shellsafe(pattern, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   goto ex;
 }
 ret= 1;
ex:;
 Xorriso_free_meM(regexpr);
 return(ret);
}


/* Option -not_list , -quoted_not_list */
/* @param flag bit0= -quoted_not_list */
int Xorriso_option_not_list(struct XorrisO *xorriso, char *adr, int flag)
{
 int ret, linecount= 0, insertcount= 0, null= 0, argc= 0, i;
 FILE *fp= NULL;
 char **argv= NULL;
 
 Xorriso_pacifier_reset(xorriso, 0);
 if(adr[0]==0) {
   sprintf(xorriso->info_text, "Empty file name given with %s",
           (flag & 1) ? "-quoted_not_list" : "-not_list");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 ret= Xorriso_afile_fopen(xorriso, adr, "rb", &fp, 0);
 if(ret <= 0)
   return(0);
 while(1) {
   ret= Xorriso_read_lines(xorriso, fp, &linecount, &argc, &argv,
                           4 | (flag & 1) );
   if(ret <= 0)
     goto ex;
   if(ret == 2)
 break;
   for(i= 0; i < argc; i++) {
     if(argv[i][0] == 0)
   continue;
     if(strchr(argv[i], '/')!=NULL) {
       null= 0;
       ret= Xorriso_option_not_paths(xorriso, 1, argv + i, &null, 0);
     } else
       ret= Xorriso_option_not_leaf(xorriso, argv[i], 0);
     if(ret<=0)
       goto ex;
     insertcount++;
   }
 }
 ret= 1;
ex:;
 if(fp != NULL && fp != stdin)
   fclose(fp);
 Xorriso_read_lines(xorriso, fp, &linecount, &argc, &argv, 2);
 if(ret<=0) {
   sprintf(xorriso->info_text, "Aborted reading of file ");
   Text_shellsafe(adr, xorriso->info_text, 1);
   sprintf(xorriso->info_text + strlen(xorriso->info_text),
           " in line number %d", linecount);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
 }
 sprintf(xorriso->info_text, "Added %d exclusion list items from file ",
         insertcount);
 Text_shellsafe(adr, xorriso->info_text, 1);
 strcat(xorriso->info_text, "\n");
 Xorriso_info(xorriso,0);
 return(ret);
}


/* Option -not_mgt */
int Xorriso_option_not_mgt(struct XorrisO *xorriso, char *setting, int flag)
{
 int ret;
 char *what_data= NULL, *what, *what_next;

 Xorriso_alloc_meM(what_data, char, SfileadrL);

 if(Sfile_str(what_data, setting, 0)<=0) {
   sprintf(xorriso->info_text,
           "-not_mgt: setting string is much too long (%d)",
           (int) strlen(setting));
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 } 
 for(what= what_data; what!=NULL; what= what_next) {
   what_next= strchr(what, ':');
   if(what_next!=NULL) {
     *what_next= 0;
     what_next++;
   }

   if(strcmp(what, "reset")==0 || strcmp(what, "erase")==0) {
     if(strcmp(what, "reset")==0)
       xorriso->disk_excl_mode= 1;
     Exclusions_destroy(&(xorriso->disk_exclusions), 0);
     ret= Exclusions_new(&(xorriso->disk_exclusions), 0);
     if(ret<=0) {
       Xorriso_no_malloc_memory(xorriso, NULL, 0);
       goto ex;
     }
   } else if(strcmp(what, "on")==0) {
     xorriso->disk_excl_mode|= 1;
   } else if(strcmp(what, "off")==0) {
     xorriso->disk_excl_mode&= ~1;
   } else if(strcmp(what, "param_on")==0) {
     xorriso->disk_excl_mode|= 2;
   } else if(strcmp(what, "param_off")==0) {
     xorriso->disk_excl_mode&= ~2;
   } else if(strcmp(what, "subtree_on")==0) {
     xorriso->disk_excl_mode|= 4;
   } else if(strcmp(what, "subtree_off")==0) {
     xorriso->disk_excl_mode&= ~4;
   } else if(strcmp(what, "ignore_on")==0) {
     xorriso->disk_excl_mode|= 8;
   } else if(strcmp(what, "ignore_off")==0) {
     xorriso->disk_excl_mode&= ~8;
   } else {
     sprintf(xorriso->info_text, "-not_mgt: unknown setting '%s'", what);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
 }
 ret= 1;
ex:;
 Xorriso_free_meM(what_data);
 return(ret);
}


/* Option -not_paths , (-hide_disk_paths resp. -as mkisofs -hide) */
/* @param flag  bit0= add to iso_rr_hidings rather than disk_exclusions
                bit1= add to joliet_hidings rather than disk_exclusions
                bit2= enable disk pattern expansion regardless of -disk_pattern
                bit8-13= consolidated hide state bits, duplicating bit0-1
                   bit8= add to iso_rr_hidings
                   bit9= add to joliet_hidings
                  bit10= add to hfsplus_hidings
*/
int Xorriso_option_not_paths(struct XorrisO *xorriso, int argc, char **argv,
                             int *idx, int flag)
{
 int ret, end_idx, num_descr= 0, dummy, optc= 0, i;
 char **descr= NULL, **optv= NULL, *eff_path= NULL, *hpt= NULL;

 end_idx= Xorriso_end_idx(xorriso, argc, argv, *idx,
                          (xorriso->do_disk_pattern == 1 || (flag & 4)) | 2);
 if(end_idx<=0)
   {ret= end_idx; goto ex;}

 num_descr= end_idx - *idx;
 if(num_descr<=0)
   {ret= 1; goto ex;}
 
 /* produce absolute patterns */
 Xorriso_alloc_meM(eff_path, char, SfileadrL);
 descr= TSOB_FELD(char *, num_descr);
 if(descr==NULL) {
no_memory:;
   Xorriso_no_pattern_memory(xorriso, sizeof(char *) * (off_t) num_descr, 0);
   ret= -1; goto ex;
 }
 for(i= 0; i<num_descr; i++)
   descr[i]= NULL;
 for(i= 0; i<num_descr; i++) {
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdx, argv[i+*idx],
                                   eff_path, 2|4);
   if(ret<=0)
     goto ex;
   descr[i]= strdup(eff_path); 
   if(descr[i]==NULL)
     goto no_memory;
 }

 ret= Xorriso_opt_args(xorriso,
                       (flag & 0x3f03) ? "-hide_disk_paths" : "-not_paths",
                       num_descr, descr, 0, &dummy, &optc, &optv,
                       2 | ((flag & 4) << 7));
 if(ret<=0)
   goto ex; 
 if(flag & 0x3f03) {
   if(flag & 0x0101) {
     ret= Exclusions_add_not_paths(xorriso->iso_rr_hidings,
                                   num_descr, descr, optc, optv, 0);
     if(ret<=0) {
no_hide:;
       sprintf(xorriso->info_text, "Cannot add path list: -hide_disk_paths ");
       hpt= Xorriso__hide_mode_text(flag & 0x3f03, 0);
       if(hpt != NULL)
         sprintf(xorriso->info_text + strlen(xorriso->info_text), "%s ", hpt);
       Xorriso_free_meM(hpt);
       Text_shellsafe(argv[*idx], xorriso->info_text, 1);
       strcat(xorriso->info_text, num_descr > 1 ? " ... " : " ");
       strcat(xorriso->info_text, xorriso->list_delimiter);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       goto ex;
     }
   }
   if(flag & 0x0202) {
     ret= Exclusions_add_not_paths(xorriso->joliet_hidings,
                                   num_descr, descr, optc, optv, 0);
     if(ret<=0)
       goto no_hide;
   }
   if(flag & 0x0400) {
     ret= Exclusions_add_not_paths(xorriso->hfsplus_hidings,
                                   num_descr, descr, optc, optv, 0);
     if(ret<=0)
       goto no_hide;
   }
 } else {
   ret= Exclusions_add_not_paths(xorriso->disk_exclusions,
                                 num_descr, descr, optc, optv, 0);
   if(ret<=0) {
     sprintf(xorriso->info_text,"Cannot add path list: -not_paths ");
     Text_shellsafe(argv[*idx], xorriso->info_text, 1);
     strcat(xorriso->info_text, num_descr > 1 ? " ... " : " ");
     strcat(xorriso->info_text, xorriso->list_delimiter);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   }
 }
ex:;
 (*idx)= end_idx;
 Xorriso_opt_args(xorriso, "-not_paths",
                  num_descr, descr, 0, &dummy, &optc, &optv, 256);
 if(descr!=NULL) {
   for(i= 0; i<num_descr; i++)
     if(descr[i]!=NULL)
       free(descr[i]);
   free((char *) descr);
   descr= NULL;
 }
 Xorriso_free_meM(eff_path);
 return(ret);
}


/* Option -options_from_file */
int Xorriso_option_options_from_file(struct XorrisO *xorriso, char *adr,
                                     int flag)
/*
 bit0= called from Xorriso_prescan_args,
       therefore execute via that same function
*/
/*
return:
 <=0 error , 1 = success , 3 = end program run
*/
{
 int ret,linecount= 0, argc= 0, was_failure= 0, fret;
 FILE *fp= NULL;
 char **argv= NULL;
 int linec= 0;
 char *line= NULL, **linev= NULL;

 if(adr[0]==0) {
   sprintf(xorriso->info_text,"Empty file name given with -options_from_file");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 if(xorriso->is_dialog) {
   sprintf(xorriso->info_text,"+ performing command lines from file ");
   Text_shellsafe(adr, xorriso->info_text, 1);
   strcat(xorriso->info_text, " :\n");
   Xorriso_info(xorriso,1);
 }
 ret= Xorriso_afile_fopen(xorriso, adr, "rb", &fp, 0);
 if(ret <= 0)
   return(0);
 sprintf(xorriso->info_text, "Command file:  ");
 Text_shellsafe(adr, xorriso->info_text, 1);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
 while(1) {
   ret= Xorriso_read_lines(xorriso, fp, &linecount, &linec, &linev, 1 | 8);
   if(ret <= 0)
     goto ex; /* no problem_handler because there is no sense in going on */
   if(ret == 2)
 break;
   line= linev[0];
   if(line[0]==0 || line[0]=='#')
 continue;

   if(flag&1) {
     ret= Sfile_make_argv(xorriso->progname, line, &argc, &argv,
                          4 | 8 | ((xorriso->bsl_interpretation & 3) << 5));
     if(ret<=0)
       goto problem_handler;
     ret= Xorriso_prescan_args(xorriso,argc,argv,1);
     if(ret==0)
       {ret= 3; goto ex;}
     if(ret<0)
       goto problem_handler;
   } else {
     if(xorriso->is_dialog) {
       sprintf(xorriso->info_text,"+ %d:  %s\n",linecount,line);
       Xorriso_info(xorriso,1);
     }
     ret= Xorriso_execute_option(xorriso,line,1|(1<<16));
     if(ret==3)
       goto ex;
     if(ret<=0)
       goto problem_handler;
   }

 continue; /* regular bottom of loop */
problem_handler:;
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1);
   if(fret>=0)
 continue;
   goto ex;
 }
 ret= 1;
ex:;
 Sfile_make_argv("", "", &argc, &argv, 2); /* release memory */
 Xorriso_read_lines(xorriso, fp, &linecount, &linec, &linev, 2);
 Xorriso_reset_counters(xorriso,0);
 if(fp != NULL && fp != stdin)
   fclose(fp);
 if(ret<=0) {
   sprintf(xorriso->info_text,
           "error triggered by line %d of file:\n    ", linecount);
   Text_shellsafe(adr, xorriso->info_text, 1);
   strcat(xorriso->info_text, "\n");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 1);
 }
 sprintf(xorriso->info_text, "Command file end:  ");
 Text_shellsafe(adr, xorriso->info_text, 1);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
 if(ret!=1)
   return(ret);
 return(!was_failure);
}


/* Option -osirrox "on"|"off" */
int Xorriso_option_osirrox(struct XorrisO *xorriso, char *mode, int flag)
{
 int l, allow_restore;
 char *npt, *cpt;

 allow_restore= xorriso->allow_restore;

 npt= cpt= mode;
 for(cpt= mode; npt!=NULL; cpt= npt+1) {
   npt= strchr(cpt,':');
   if(npt==NULL)
     l= strlen(cpt);
   else
     l= npt-cpt;
   if(l==0 && mode[0]!=0)
     goto unknown_mode;
   if(strncmp(cpt, "off", l)==0 && l >= 3)
     allow_restore= 0;
   else if(strncmp(cpt, "banned", l)==0 && l >= 5)
     allow_restore= -1;
   else if(strncmp(cpt, "blocked", l)==0 && l >= 7)
     allow_restore= -2;
   else if(strncmp(cpt, "unblock", l)==0 && l >= 7) {
     if(xorriso->allow_restore == -2)
       xorriso->allow_restore= 0;
     allow_restore= 1;
   } else if(strncmp(cpt, "device_files", l)==0 && l >= 12)
     allow_restore= 2;
   else if((strncmp(cpt, "on", l)==0 && l >= 2) || mode[0]==0)
     allow_restore= 1;
   else if(strncmp(cpt, "concat_split_on", l)==0 && l >= 15)
     xorriso->do_concat_split= 1;
   else if(strncmp(cpt, "concat_split_off", l)==0 && l >= 16)
     xorriso->do_concat_split= 0;
   else if(strncmp(cpt, "auto_chmod_on", l)==0 && l >= 13)
     xorriso->do_auto_chmod= 1;
   else if(strncmp(cpt, "auto_chmod_off", l)==0 && l >= 14)
     xorriso->do_auto_chmod= 0;
   else if(strncmp(cpt, "sort_lba_on", l)==0 && l >= 11)
     xorriso->do_restore_sort_lba= 1;
   else if(strncmp(cpt, "sort_lba_off", l)==0 && l >= 12)
     xorriso->do_restore_sort_lba= 0;
   else if(strncmp(cpt, "o_excl_on", l)==0 && l >= 9)
     xorriso->drives_exclusive= 1;
   else if(strncmp(cpt, "o_excl_off", l)==0 && l >= 10)
     xorriso->drives_exclusive= 0;
   else if(strncmp(cpt, "strict_acl_on", l)==0 && l >= 13)
     xorriso->do_strict_acl|= 1;
   else if(strncmp(cpt, "strict_acl_off", l)==0 && l >= 14)
     xorriso->do_strict_acl&= ~1;
   else {
unknown_mode:;
     sprintf(xorriso->info_text, "-osirrox: unknown mode '%s'", cpt);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
 }
 if(allow_restore > 0 && xorriso->allow_restore == -1) {
   sprintf(xorriso->info_text,
     "-osirrox: was already permanently disabled by setting 'banned'");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 if(allow_restore > 0 && xorriso->allow_restore == -2) {
   sprintf(xorriso->info_text,
           "-osirrox: is currently disabled by setting 'blocked'");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 if(xorriso->allow_restore != -1)
   xorriso->allow_restore= allow_restore;
 sprintf(xorriso->info_text,
         "Copying of file objects from ISO image to disk filesystem is: %s\n",
         xorriso->allow_restore > 0 ? "Enabled" : "Disabled");
 Xorriso_info(xorriso, 0);
 return(1);
}


/* Option -overwrite "on"|"nondir"|"off" */
int Xorriso_option_overwrite(struct XorrisO *xorriso, char *mode, int flag)
{
 if(strcmp(mode, "off")==0)
   xorriso->do_overwrite= 0;
 else if(strcmp(mode, "on")==0)
   xorriso->do_overwrite= 1;
 else if(strcmp(mode, "nondir")==0)
   xorriso->do_overwrite= 2;
 else {
   sprintf(xorriso->info_text, "-overwrite: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(1);
}


