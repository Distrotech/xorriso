
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2012 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the implementation of options -a* to -c* as mentioned
   in man page or info file derived from xorriso.texi.
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

/* for -charset */
#include <iconv.h>
#include <langinfo.h>
#include <locale.h>


#include "xorriso.h"
#include "xorriso_private.h"
#include "xorrisoburn.h"


/* Option -abort_on */
int Xorriso_option_abort_on(struct XorrisO *xorriso, char *in_severity,
                            int flag)
{
 int ret, sev;
 char severity[20], *official;

 Xorriso__to_upper(in_severity, severity, (int) sizeof(severity), 0);
 ret= Xorriso__text_to_sev(severity, &sev, 0);
 if(ret<=0) {
   sprintf(xorriso->info_text, "-abort_on: Not a known severity name : ");
   Text_shellsafe(in_severity, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(ret);
 }
 ret= Xorriso__sev_to_text(sev, &official, 0);
 if(ret <= 0)
   official= severity;
 if(Sfile_str(xorriso->abort_on_text, official, 0) <= 0)
   return(-1);
 xorriso->abort_on_severity= sev;
 xorriso->abort_on_is_default= 0;
 Xorriso_set_abort_severity(xorriso, 0);
 return(1);
}


/* Option -abstract_file */
int Xorriso_option_abstract_file(struct XorrisO *xorriso, char *name, int flag)
{
 if(Xorriso_check_name_len(xorriso, name,
                           (int) sizeof(xorriso->abstract_file),
                           "-abstract_file", 0) <= 0)
   return(0);
 strcpy(xorriso->abstract_file, name);
 Xorriso_set_change_pending(xorriso, 1);
 return(1);
}

/* Option -acl "on"|"off" */
int Xorriso_option_acl(struct XorrisO *xorriso, char *mode, int flag)
{
 int ret;

 if(strcmp(mode, "off")==0)
   xorriso->do_aaip&= ~3;
 else if(strcmp(mode, "on")==0)
   xorriso->do_aaip|= (1 | 2);
 else {
   sprintf(xorriso->info_text, "-acl: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 ret= Xorriso_set_ignore_aclea(xorriso, 0);
 if(ret <= 0)
   return(ret);
 return(1);
}


/* Option -add */
/* @param flag bit0=do not report the added item
               bit1=do not reset pacifier, no final pacifier message
               bit2= prepend ISO working directory in any case
               bit3= unescape \\
*/
int Xorriso_option_add(struct XorrisO *xorriso, int argc, char **argv,
                       int *idx, int flag)
{
 int i, end_idx, ret, was_failure= 0, fret, optc= 0, split;
 char *target= NULL, *source= NULL, *ept, *eff_path= NULL;
 char **optv= NULL, *rpt, *wpt;

 ret= Xorriso_opt_args(xorriso, "-add", argc, argv, *idx, &end_idx,
                       &optc, &optv, ((!!xorriso->allow_graft_points)<<2)|2);
 if(ret<=0)
   goto ex;

 Xorriso_alloc_meM(target, char, SfileadrL);
 Xorriso_alloc_meM(source, char, SfileadrL);
 Xorriso_alloc_meM(eff_path, char, SfileadrL);

 if(!(flag&2))
   Xorriso_pacifier_reset(xorriso, 0);
 for(i= 0; i<optc; i++) {
   if(Sfile_str(target,optv[i],0)<=0)
     {ret= -1; goto ex;}
   strcpy(source, optv[i]);
   split= 0;
   if(xorriso->allow_graft_points) {
     ret= Fileliste__target_source_limit(target, '=', &ept, 0);
     if(ret>0) {
       *ept= 0;
       strcpy(source, ept+1);
       split= 1;
     }
     /* unescape \= */;
     if(split)
       rpt= wpt= target;
     else
       rpt= wpt= source;
     for(; *rpt!=0; rpt++) {
       if(*rpt=='\\') {
         if(*(rpt+1)=='=')
     continue;
         if((flag & 8) && *(rpt + 1) == '\\')
           rpt++;
       }
       *(wpt++)= *rpt;
     }
     *wpt= 0;
   }
   if(split==0)
     strcpy(target, source);
   if(flag & 4) {
     ret= Sfile_prepend_path(xorriso->wdi, target, 0);
     if(ret<=0) {
       sprintf(xorriso->info_text, "Effective path gets much too long (%d)",
               (int) (strlen(xorriso->wdi)+strlen(target)+1));
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       goto problem_handler;
     }
   }

   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, target, eff_path, 2);
   if(ret<=0)
     goto problem_handler;
   strcpy(target, eff_path);
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdx, source,eff_path,2|4);
   if(ret<=0)
     goto problem_handler;
   strcpy(source, eff_path);

   ret= Xorriso_graft_in(xorriso, NULL, source, target, (off_t)0, (off_t)0, 0);
   if(ret<=0 || xorriso->request_to_abort)
     goto problem_handler;
   sprintf(xorriso->info_text, "Added to ISO image: %s '%s'='%s'\n",
           (ret>1 ? "directory" : "file"), (target[0] ? target : "/"), source);
   if(!(flag&1))
     Xorriso_info(xorriso, 0);

 continue; /* regular bottom of loop */
problem_handler:;
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret>=0)
 continue;
   goto ex;
 } 
 if(!(flag&2))
   Xorriso_pacifier_callback(xorriso, "files added", xorriso->pacifier_count,
                             xorriso->pacifier_total, "", 1);
 ret= 1;
ex:;
 (*idx)= end_idx;
 Xorriso_free_meM(target);
 Xorriso_free_meM(source);
 Xorriso_free_meM(eff_path);
 Xorriso_opt_args(xorriso, "-add", argc, argv, *idx, &end_idx, &optc, &optv,
                  256);
 if(ret<=0)
   return(ret);
 return(!was_failure);
}


/* Option -add_plainly "none"|"unknown" */
int Xorriso_option_add_plainly(struct XorrisO *xorriso, char *mode,int flag)
{
 if(strcmp(mode, "none")==0)
   xorriso->add_plainly= 0;
 if(strcmp(mode, "unknown")==0)
   xorriso->add_plainly= 1;
 else if(strcmp(mode, "dashed")==0)
   xorriso->add_plainly= 2;
 else if(strcmp(mode, "any")==0)
   xorriso->add_plainly= 3;
 else {
   sprintf(xorriso->info_text, "-add_plainly: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(1);
}


/* Option -alter_date , -alter_date_r */
/* @param flag bit0=recursive (-alter_date_r)
*/
int Xorriso_option_alter_date(struct XorrisO *xorriso,
                               char *time_type, char *timestring,
                               int argc, char **argv, int *idx, int flag)
{
 int i, ret, was_failure= 0, t_type= 0, end_idx, fret;
 time_t t;
 int optc= 0;
 char **optv= NULL;
 struct FindjoB *job= NULL;
 struct stat dir_stbuf;

 ret= Xorriso_opt_args(xorriso, "-alter_date", argc, argv, *idx, &end_idx,
                       &optc, &optv, 0);
 if(ret<=0)
   goto ex; 
 ret= Xorriso_convert_datestring(xorriso, "-alter_date", time_type, timestring,
                                 &t_type, &t, 0);
 if(ret<=0)
   goto ex;
 for(i= 0; i<optc; i++) {
   if(flag&1) {
     ret= Findjob_new(&job, optv[i], 0);
     if(ret<=0) {
       Xorriso_no_findjob(xorriso, "-alter_date", 0);
       {ret= -1; goto ex;}
     }
     Findjob_set_action_ad(job, t_type, t, 0);
     ret= Xorriso_findi(xorriso, job, NULL,  (off_t) 0,
                        NULL, optv[i], &dir_stbuf, 0, 0);
     Findjob_destroy(&job, 0);
   } else
     ret= Xorriso_set_time(xorriso, optv[i], t, t_type);
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
 Xorriso_opt_args(xorriso, "-alter_date", argc, argv, *idx, &end_idx, &optc,
                  &optv, 256);
 Findjob_destroy(&job, 0);
 if(ret<=0)
   return(ret);
 return(!was_failure);
}


/* Option -append_partition */
int Xorriso_option_append_partition(struct XorrisO *xorriso, char *partno_text,
                                   char *type_text, char *image_path, int flag)
{
 int partno = 0, type_code= -1, i;
 unsigned int unum;
 char *tpt;
 static char *part_type_names[] = {"FAT12", "FAT16", "Linux",   "", NULL};
 static int part_type_codes[]   = {   0x01,    0x06,   0x83,  0x00};

 sscanf(partno_text, "%d", &partno);
 if(partno < 1 || partno > Xorriso_max_appended_partitionS) {
   sprintf(xorriso->info_text,
          "-append_partition:  Partition number '%s' is out of range (1...%d)",
          partno_text, Xorriso_max_appended_partitionS);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 for(i= 0; part_type_names[i] != NULL; i++)
   if(strcmp(part_type_names[i], type_text) == 0)
 break;
 if(part_type_names[i] != NULL)
   type_code= part_type_codes[i];
 if(type_code < 0) {
   tpt= type_text;
   if(strncmp(tpt, "0x", 2) == 0)
     tpt+= 2;
   else
     goto bad_type;
   unum= 0xffffffff;
   sscanf(tpt, "%X", &unum);
   if(unum > 0xff) {
bad_type:;
     sprintf(xorriso->info_text,
       "-append_partition: Partition type '%s' is out of range (0x00...0xff)",
       type_text);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
   type_code= unum;
 }

 if(xorriso->appended_partitions[partno - 1] != NULL)
   free(xorriso->appended_partitions[partno - 1]);
 xorriso->appended_partitions[partno - 1]= strdup(image_path);
 if(xorriso->appended_partitions[partno - 1] == NULL) {
   Xorriso_no_malloc_memory(xorriso, NULL, 0);
   return(-1);
 }
 xorriso->appended_part_types[partno - 1]= type_code;
 return(1);
}


/* Option -application_id */
int Xorriso_option_application_id(struct XorrisO *xorriso, char *name,
                                  int flag)
{
 if(Xorriso_check_name_len(xorriso, name,
                           (int) sizeof(xorriso->application_id),
                           "-application_id", 0) <= 0)
   return(0);
 if(strcmp(name, "@xorriso@") == 0)
   Xorriso_preparer_string(xorriso, xorriso->application_id, 0);
 else
   strcpy(xorriso->application_id,name);
 Xorriso_set_change_pending(xorriso, 1);
 return(1);
}


/* Option -as */
/* @param flag bit0=do not report the added item
               bit1=do not reset pacifier, no final pacifier message
*/
int Xorriso_option_as(struct XorrisO *xorriso, int argc, char **argv,
                      int *idx, int flag)
{
 int end_idx, ret, idx_count;

 end_idx= Xorriso_end_idx(xorriso, argc, argv, *idx, 1);
 idx_count= end_idx-(*idx);
 if(end_idx<=0 || (*idx)>=argc) {
   if(idx_count<1)
     sprintf(xorriso->info_text,
             "-as : Not enough arguments given. Needed: whom do_what %s",
             xorriso->list_delimiter);
   else
     sprintf(xorriso->info_text,
             "-as %s : Not enough arguments given. Needed: do_what %s",
             argv[*idx], xorriso->list_delimiter);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   ret= 0; goto ex;
 }
 if(strcmp(argv[*idx], "cdrecord")==0 || strcmp(argv[*idx], "wodim")==0 ||
    strcmp(argv[*idx], "cdrskin")==0 || strcmp(argv[*idx], "xorrecord")==0) {
   ret= Xorriso_cdrskin(xorriso, argv[*idx], end_idx-(*idx)-1, argv+(*idx)+1,
                        0);
   if(ret<=0)
     goto ex;
 } else if(strcmp(argv[*idx], "mkisofs")==0 ||
           strcmp(argv[*idx], "genisoimage")==0 ||
           strcmp(argv[*idx], "genisofs")==0 ||
           strcmp(argv[*idx], "xorrisofs")==0) {
   ret= Xorriso_genisofs(xorriso, argv[*idx], end_idx-(*idx)-1, argv+(*idx)+1,
                         0);
   if(ret<=0)
     goto ex;
 } else {
   sprintf(xorriso->info_text,
           "-as : Not a known emulation personality: '%s'", argv[*idx]);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   ret= 0; goto ex;
 }

 ret= 1;
ex:;
 (*idx)= end_idx;
 return(ret);
}


/* Option -assert_volid */
int Xorriso_option_assert_volid(struct XorrisO *xorriso, char *pattern,
                                char *severity, int flag)
{
 int ret, sev;
 char *sev_text= "", off_severity[20];

 if(strlen(pattern)>=sizeof(xorriso->assert_volid)) {
   sprintf(xorriso->info_text,
           "Name too long with option -application_id (%d > %d)",
           (int) strlen(pattern), (int) sizeof(xorriso->assert_volid)-1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 if(pattern[0]) {
   ret= Sregex_match(pattern, "", 1);
   if(ret <= 0) {
     sprintf(xorriso->info_text, "-assert_volid: Cannot use given pattern.");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
 }
 if(severity[0] != 0 || pattern[0] != 0) {
   if(severity[0] == 0)
     sev_text= xorriso->abort_on_text;
   else
     sev_text= severity;
   if(strcmp(sev_text, "NEVER") == 0)
     sev_text= "ABORT";
   Xorriso__to_upper(sev_text, off_severity, (int) sizeof(off_severity), 0);
   sev_text= off_severity;
   ret= Xorriso__text_to_sev(sev_text, &sev, 0);
   if(ret<=0) {
     sprintf(xorriso->info_text, "-assert_volid: Not a known severity name : ");
     Text_shellsafe(severity, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(ret);
   }
 }
 if(Sfile_str(xorriso->assert_volid, pattern,0) <= 0)
   return(-1);
 strcpy(xorriso->assert_volid_sev, sev_text);
 return(1);
}


/* Option -auto_charset "on"|"off" */
int Xorriso_option_auto_charset(struct XorrisO *xorriso, char *mode, int flag)
{
 if(strcmp(mode, "off")==0)
   xorriso->do_aaip&= ~(256 | 512);
 else if(strcmp(mode, "on")==0)
   xorriso->do_aaip|= (256 | 512);
 else {
   sprintf(xorriso->info_text, "-auto_charset: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(1);
}


/* Option -backslash_codes */
int Xorriso_option_backslash_codes(struct XorrisO *xorriso, char *mode,
                                   int flag)
{
 char *npt, *cpt;
 int l, was;

 was= xorriso->bsl_interpretation;
 xorriso->bsl_interpretation= 0;
 npt= cpt= mode;
 for(; npt!=NULL; cpt= npt+1) {
   npt= strchr(cpt,':');
   if(npt==NULL)
     l= strlen(cpt);
   else
     l= npt-cpt;
   if(l == 0)
 continue;
   if(l == 3 && strncmp(cpt, "off", l)==0) {
     xorriso->bsl_interpretation= 0;
   } else if(l == 16 && strncmp(cpt, "in_double_quotes", l)==0) {
     xorriso->bsl_interpretation= (xorriso->bsl_interpretation & ~3) | 1;
   } else if(l == 9 && strncmp(cpt, "in_quotes", l)==0) {
     xorriso->bsl_interpretation= (xorriso->bsl_interpretation & ~3) | 2;
   } else if(l == 17 && strncmp(cpt, "with_quoted_input", l)==0) {
     xorriso->bsl_interpretation= (xorriso->bsl_interpretation & ~3) | 3;
   } else if(l == 22 && strncmp(cpt, "with_program_arguments", l)==0) {
     xorriso->bsl_interpretation= xorriso->bsl_interpretation | 16;
   } else if(l == 13 && strncmp(cpt, "encode_output", l)==0) {
     xorriso->bsl_interpretation= xorriso->bsl_interpretation | 32 | 64;
   } else if(l == 14 && strncmp(cpt, "encode_results", l)==0) {
     xorriso->bsl_interpretation= xorriso->bsl_interpretation | 32;
   } else if(l == 12 && strncmp(cpt, "encode_infos", l)==0) {
     xorriso->bsl_interpretation= xorriso->bsl_interpretation | 64;
   } else if(l == 2 && strncmp(cpt, "on", l)==0) {
     xorriso->bsl_interpretation= 3 | 16 | 32 | 64;
   } else {
     if(l<SfileadrL)
       sprintf(xorriso->info_text, "-backslash_codes: unknown mode '%s'", cpt);
     else
       sprintf(xorriso->info_text,
               "-backslash_codes: oversized mode parameter (%d)", l);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     xorriso->bsl_interpretation= was;
     return(0);
   }
 }
 return(1);
}


/* Option -ban_stdio_write */
int Xorriso_option_ban_stdio_write(struct XorrisO *xorriso, int flag)
{
 xorriso->ban_stdio_write= 1;
 return(1);
}


/* Option -biblio_file */
int Xorriso_option_biblio_file(struct XorrisO *xorriso, char *name, int flag)
{
 if(Xorriso_check_name_len(xorriso, name, (int) sizeof(xorriso->biblio_file),
                           "-biblio_file", 0) <= 0)
   return(0);
 strcpy(xorriso->biblio_file, name);
 Xorriso_set_change_pending(xorriso, 1);
 return(1);
}


/* Option -blank and -format */
/* @param flag bit0= format rather than blank
   @return <=0 error , 1 success, 2 revoked by -reassure 
*/
int Xorriso_option_blank(struct XorrisO *xorriso, char *in_mode, int flag)
{
 char *cmd= "-blank", *mode;
 int aq_ret, ret, mode_flag= 0, as_needed= 0, idx, do_force= 0;
 off_t size= 0;

 if(flag&1)
   cmd= "-format";
 if(xorriso->out_drive_handle == NULL) {
   sprintf(xorriso->info_text,
           "%s: No output drive set by -dev -or -outdev", cmd);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 if(xorriso->in_drive_handle == xorriso->out_drive_handle) {
   if(Xorriso_change_is_pending(xorriso, 0)) {
     sprintf(xorriso->info_text,
             "%s: Image changes pending. -commit or -rollback first.", cmd);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
 }
 ret= Xorriso_reassure(xorriso, cmd,
                       "possibly make unreadable data on outdev", 0);
 if(ret<=0)
   return(2);

 if(strncmp(in_mode, "force:", 6) == 0) {
   do_force= 1;
   mode= in_mode + 6;
 } else
   mode= in_mode;
 if(strcmp(mode, "as_needed")==0 || mode[0]==0)
   as_needed= 1;
 else if(strcmp(mode, "all")==0 || strcmp(mode, "full")==0)
   mode_flag= 0;
 else if((strcmp(mode, "deformat")==0 ||
          strcmp(mode, "deformat_sequential")==0) && !(flag&1))
   mode_flag= 2;
 else if((strcmp(mode, "deformat_quickest")==0 ||
          strcmp(mode, "deformat_sequential_quickest")==0) && !(flag&1))
   mode_flag= 3;
 else if(strcmp(mode, "fast")==0)
   mode_flag= 1;
 else if(strncmp(mode, "by_index_", 9)==0 && (flag&1)) {
   mode_flag= 128;
   idx= -1;
   if(strlen(mode)>9)
     sscanf(mode+9, "%d", &idx);
   if(idx<0 || idx>255) {
unusable_index:;
     sprintf(xorriso->info_text,
             "-format: mode '%s' provides unusable index number", mode);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
   mode_flag|= (idx<<8);
 } else if(strncmp(mode, "fast_by_index_", 14)==0 && (flag&1)) {
   mode_flag= 1 | 128;
   idx= -1;
   if(strlen(mode)>14)
     sscanf(mode+14, "%d", &idx);
   if(idx<0 || idx>255)
     goto unusable_index;
   mode_flag|= (idx<<8);
 } else if(strncmp(mode, "by_size_", 8) == 0 && (flag & 1)) {
   size= (off_t) Scanf_io_size(mode + 8, 0);
   if(size <= 0) {
unusable_size:;
     sprintf(xorriso->info_text,
             "-format: mode '%s' provides unusable size value", mode);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
   mode_flag= 2;
 } else if(strncmp(mode, "fast_by_size_", 13) == 0 && (flag & 1)) {
   size= (off_t) Scanf_io_size(mode + 13, 0);
   if(size <= 0)
     goto unusable_size;
   mode_flag= 3;
 } else {
   sprintf(xorriso->info_text,
           "%s: Unknown %s mode '%s'",
           cmd, ((flag&1) ? "-format" : "-blank"), mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 if(do_force) {
   ret= Xorriso_pretend_full_disc(xorriso, 0);
   if(ret <= 0)
     return(ret);
 }
 if(as_needed)
   ret= Xorriso_blank_as_needed(xorriso, (flag&1)<<2);
 else if(flag&1)
   ret= Xorriso_format_media(xorriso, size, mode_flag & 0xff83);
 else
   ret= Xorriso_blank_media(xorriso, mode_flag&3);
 if(ret==0)
   return(ret);
 if(ret <= 0) { /* in case of success, above functions will have re-aquired */
   aq_ret= Xorriso_reaquire_outdev(xorriso, 0); /* actually give up drive */
   if(ret<aq_ret)
     return(ret);
   if(aq_ret<=0)
     return(aq_ret);
 }
 return(1);
}


/* Option -boot_image */
int Xorriso_option_boot_image(struct XorrisO *xorriso, char *form,
                              char *treatment, int flag)
{
 int was_ok= 1, ret, isolinux_grub= 0, count, bin_count;
 unsigned int u;
 char *formpt, *treatpt, *eff_path= NULL;
 uint8_t sn[8];
 double num;

 Xorriso_alloc_meM(eff_path, char, SfileadrL);
 formpt= form;
 if(formpt[0]=='-')
   formpt++;
 treatpt= treatment;
 if(treatpt[0]=='-')
   treatpt++;

 if(strcmp(formpt, "isolinux")==0 || strcmp(formpt, "grub") == 0)
   isolinux_grub= 1;
 if(strcmp(treatpt, "keep")==0) {
   if(xorriso->boot_count > 0) {
cannot_keep_or_patch:;
       sprintf(xorriso->info_text,
      "Loaded boot image has already been replaced. Cannot keep or patch it.");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
      {ret= 0; goto ex;}
   }
   if(isolinux_grub)
     goto treatment_patch;
   xorriso->keep_boot_image= 1;
   xorriso->patch_isolinux_image= (xorriso->patch_isolinux_image & ~3) | 0;
   xorriso->boot_image_bin_path[0]= 0;
   xorriso->patch_system_area= 0;

 } else if(strcmp(treatpt, "patch")==0) {
treatment_patch:;
   if(xorriso->boot_count > 0)
     goto cannot_keep_or_patch;
   xorriso->keep_boot_image= 0;
   xorriso->patch_isolinux_image= (xorriso->patch_isolinux_image & ~3) | 1;
   xorriso->boot_image_bin_path[0]= 0;
   if(strcmp(formpt, "grub") == 0) {
     xorriso->patch_isolinux_image|= 2;
     xorriso->patch_system_area= 1;
   } else if(strcmp(formpt, "isolinux") == 0)
     xorriso->patch_system_area= 2;
   else
     xorriso->patch_system_area= 0;

 } else if(strcmp(treatpt, "discard")==0) {
   xorriso->keep_boot_image= 0;
   xorriso->patch_isolinux_image= (xorriso->patch_isolinux_image & ~3) | 0;
   xorriso->boot_image_bin_path[0]= 0;
   xorriso->patch_system_area= 0;
   if((xorriso->system_area_options & 0xfc ) == 0)
     xorriso->system_area_options= 0; /* Reset eventual type 0 flags */
   if(xorriso->boot_count > 0) {
     ret= Xorriso_attach_boot_image(xorriso, 2); /* dispose boot images */
     if(ret <= 0)
       goto ex;
   }

 } else if(strcmp(treatpt, "next") == 0) {
   ret= Xorriso_attach_boot_image(xorriso, 0);
   if(ret <= 0)
     goto ex;

 } else if(strcmp(treatpt, "show_status")==0) {
   sprintf(xorriso->result_line, "------------------------------------\n");
   Xorriso_result(xorriso, 0);
   sprintf(xorriso->result_line, "Status of loaded boot image        :\n");
   Xorriso_result(xorriso, 0);
   sprintf(xorriso->result_line, "------------------------------------\n");
   Xorriso_result(xorriso, 0);
   Xorriso_show_boot_info(xorriso, 0);
   sprintf(xorriso->result_line, "------------------------------------\n");
   Xorriso_result(xorriso, 0);
   sprintf(xorriso->result_line, "Boot image settings for next commit:\n");
   Xorriso_result(xorriso, 0);
   sprintf(xorriso->result_line, "------------------------------------\n");
   Xorriso_result(xorriso, 0);
   Xorriso_status(xorriso, "-boot_image", NULL, 0);
   sprintf(xorriso->result_line, "------------------------------------\n");
   Xorriso_result(xorriso, 0);

 } else if(strcmp(treatpt, "cat_path=") == 0) {
   xorriso->boot_image_cat_path[0] = 0;
 } else if(strncmp(treatpt, "cat_path=", 9) == 0) {
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, treatpt + 9,
                                     xorriso->boot_image_cat_path, 2);
   if(ret <= 0)
     goto ex;

 } else if(strncmp(treatpt, "cat_hidden=", 11) == 0) {
   ret= Xorriso__hide_mode(treatpt + 11, 0);
   if(ret >= 0)
     xorriso->boot_image_cat_hidden= ret;
   else
     was_ok= 0;

 } else if(strncmp(treatpt, "dir=", 4) == 0) {
   if(strcmp(formpt, "isolinux")==0) {
     /* ISOLINUX */
     /* The three locations mentioned in http://syslinux.zytor.com/iso.php */
     if(strcmp(treatpt + 4, "/") == 0)
       strcpy(xorriso->boot_image_bin_path, "/");
     else if(strcmp(treatpt + 4, "isolinux") == 0
        || strcmp(treatpt + 4, "/isolinux") == 0)
       strcpy(xorriso->boot_image_bin_path, "/isolinux/");
     else if(strcmp(treatpt + 4, "boot/isolinux") == 0
        || strcmp(treatpt + 4, "/boot/isolinux") == 0
        || strcmp(treatpt + 4, "boot") == 0
        || strcmp(treatpt + 4, "/boot") == 0)
       strcpy(xorriso->boot_image_bin_path, "/boot/isolinux/");
     else {
       sprintf(xorriso->info_text,
               "Unrecognized keyword with -boot_image %s %s",
               form, treatment);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       sprintf(xorriso->info_text,
               "Allowed with dir= are / , /isolinux . /boot/isolinux");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
       {ret= 0; goto ex;}
     }
     strcpy(xorriso->boot_image_cat_path, xorriso->boot_image_bin_path);
     strcat(xorriso->boot_image_bin_path, "isolinux.bin");
     strcat(xorriso->boot_image_cat_path, "boot.cat");
     xorriso->boot_image_load_size= 4 * 512;
     xorriso->keep_boot_image= 0;
     xorriso->patch_isolinux_image= (xorriso->patch_isolinux_image & ~3) | 1;
     strcpy(xorriso->boot_image_bin_form, formpt);
     {ret= 1; goto ex;}

   } else if(strcmp(formpt, "grub") == 0) {

     /* >>> GRUB */
     was_ok= 0;

     strcpy(xorriso->boot_image_bin_form, formpt);

   } else
     was_ok= 0;

 } else if(strcmp(treatpt, "bin_path=") == 0) {
   xorriso->boot_image_bin_path[0] = 0;
   xorriso->boot_efi_default= 0;
 } else if(strncmp(treatpt, "bin_path=", 9) == 0) {
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, treatpt + 9,
                                   xorriso->boot_image_bin_path, 2);
   if(ret <= 0)
     goto ex;
   xorriso->keep_boot_image= 0;
   if(isolinux_grub) {
     xorriso->patch_isolinux_image= (xorriso->patch_isolinux_image & ~3) | 1;
     if(xorriso->boot_image_bin_path[0])
       xorriso->boot_image_load_size= 4 * 512;
     strcpy(xorriso->boot_image_bin_form, formpt);
   } else
     strcpy(xorriso->boot_image_bin_form, "any");
   xorriso->boot_efi_default= 0;

 } else if(strcmp(treatpt, "efi_path=") == 0) {
   xorriso->boot_image_bin_path[0] = 0;
   xorriso->boot_efi_default= 0;
 } else if(strncmp(treatpt, "efi_path=", 9) == 0) {
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, treatpt + 9,
                                   xorriso->boot_image_bin_path, 2);
   if(ret <= 0)
     goto ex;
   xorriso->keep_boot_image= 0;
   xorriso->boot_efi_default= 1;

 } else if(strncmp(treatpt, "mips_path=", 10) == 0) {
   sprintf(eff_path, "-boot_image %s mips_path=", formpt);
   ret= Xorriso_coordinate_system_area(xorriso, 1, 0, eff_path, 0);
   if(ret <= 0)
     goto ex;
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, treatpt + 10,
                                   eff_path, 2);
   if(ret <= 0)
     goto ex;
   ret= Xorriso_add_mips_boot_file(xorriso, eff_path, 0);
   if(ret <= 0)
     goto ex;

 } else if(strncmp(treatpt, "mipsel_path=", 12) == 0) {
   sprintf(eff_path, "-boot_image %s mipsel_path=", formpt);
   ret= Xorriso_coordinate_system_area(xorriso, 2, 0, eff_path, 0);
   if(ret <= 0)
     goto ex;
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, treatpt + 12,
                                   eff_path, 2);
   if(ret <= 0)
     goto ex;
   ret= Xorriso_add_mips_boot_file(xorriso, eff_path, 2);
   if(ret <= 0)
     goto ex;

 } else if(strcmp(treatpt, "mips_discard") == 0 ||
           strcmp(treatpt, "mipsel_discard") == 0 ||
           strcmp(treatpt, "sparc_discard") == 0) {
   xorriso->system_area_options&= ~0xfc; /* system area type 0 */
   Xorriso_add_mips_boot_file(xorriso, "", 1); /* give up MIPS boot files */

 } else if(strncmp(treatpt, "sparc_label=", 12) == 0) {
   sprintf(eff_path, "-boot_image %s sparc_label=", formpt);
   ret= Xorriso_coordinate_system_area(xorriso, 3, 0, eff_path, 0);
   if(ret <= 0)
     goto ex;
   strncpy(xorriso->ascii_disc_label, treatpt + 12,
           Xorriso_disc_label_sizE - 1);
   xorriso->ascii_disc_label[Xorriso_disc_label_sizE - 1] = 0;

 } else if(strncmp(treatpt, "grub2_sparc_core=", 17) == 0) {
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, treatpt + 17,
                                     xorriso->grub2_sparc_core, 2);
   if(ret <= 0)
     goto ex;

 } else if(strncmp(treatpt, "boot_info_table=", 16)==0) {
   if(strcmp(treatpt + 16, "off") == 0)
     xorriso->patch_isolinux_image= (xorriso->patch_isolinux_image & ~3) | 0;
   else if(strcmp(treatpt + 16, "on") == 0)
     xorriso->patch_isolinux_image= (xorriso->patch_isolinux_image & ~3) |
                                    1 | (2 * (strcmp(treatpt, "grub") == 0));
   else
     was_ok= 0;

 } else if(strncmp(treatpt, "grub2_boot_info=", 16)==0) {
   if(strcmp(treatpt + 16, "off") == 0)
     xorriso->patch_isolinux_image= xorriso->patch_isolinux_image & ~512;
   else if(strcmp(treatpt + 16, "on") == 0)
     xorriso->patch_isolinux_image= xorriso->patch_isolinux_image | 512;
   else
     was_ok= 0;

 } else if(strncmp(treatpt, "load_size=", 10) == 0) {
   num= Scanf_io_size(treatpt + 10, 0);
   if(num < 512 && isolinux_grub) {
     sprintf(xorriso->info_text,
             "-boot_image %s : load_size too small (%s < 512)",
             formpt, treatpt + 10);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
   xorriso->boot_image_load_size= num;

 } else if(strncmp(treatpt, "id_string=", 10) == 0) {
   memset(xorriso->boot_id_string, 0, 29);
   if(strlen(treatpt + 10) == 56) {
     ret= Hex_to_bin(treatpt + 10, 28, &count, xorriso->boot_id_string, 0);
   } else
     ret= 0;
   if(ret <= 0)
     strncpy((char *) xorriso->boot_id_string, treatpt + 10, 28);

 } else if(strncmp(treatpt, "sel_crit=", 9) == 0) {
   memset(xorriso->boot_selection_crit, 0, 21);
   count= 0;
   ret= Hex_to_bin(treatpt + 9, 20, &count, xorriso->boot_selection_crit, 0);
   if(ret <= 0) {
     sprintf(xorriso->info_text,
      "-boot_image %s sel_crit= : Wrong form. Need even number of hex digits.",
             formpt);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }

 } else if(strncmp(treatpt, "system_area=", 12) == 0) {
   if(strcmp(formpt, "isolinux")==0) {
     ret= Xorriso_coordinate_system_area(xorriso, 0, 2,
                                       "-boot_image isolinux system_area=", 0);
     if(ret <= 0)
       goto ex;
   }
   ret= Xorriso_set_system_area_path(xorriso, treatpt + 12, 0);
   if(ret <= 0)
     goto ex;

 } else if(strncmp(treatpt, "partition_table=", 16)==0) {
   if(strcmp(treatpt + 16, "off") == 0) {
     xorriso->system_area_options&= ~3;
   } else if(strcmp(treatpt + 16, "on") == 0) {
     sprintf(eff_path, "-boot_image %s partition_table=", formpt);
     if(strcmp(formpt, "isolinux")==0)
       ret= Xorriso_coordinate_system_area(xorriso, 0, 2, eff_path, 0);
     else
       ret= Xorriso_coordinate_system_area(xorriso, 0, 1, eff_path, 0);
     if(ret <= 0)
       goto ex;
   } else
     was_ok= 0;

 } else if(strncmp(treatpt, "partition_offset=", 17)==0) {
   u= 0;
   sscanf(treatpt + 17, "%u", &u);
   if(u > 0 && u < 16) {
     sprintf(xorriso->info_text,
      "-boot_image %s partition_offset= : Non-zero number too small (<16).",
             formpt);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
   xorriso->partition_offset= u;

 } else if(strncmp(treatpt, "partition_hd_cyl=", 17)==0) {
   u= 0;
   sscanf(treatpt + 17, "%u", &u);
   if(u > 255) {
     sprintf(xorriso->info_text,
      "-boot_image %s partition_hd_cyl= : Number too large (>255).", formpt);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
   xorriso->partition_heads_per_cyl= u;

 } else if(strncmp(treatpt, "partition_sec_hd=", 17)==0) {
   u= 0;
   sscanf(treatpt + 17, "%u", &u);
   if(u > 63) {
     sprintf(xorriso->info_text,
      "-boot_image %s partition_sec_hd= : Number too large (>63).", formpt);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
   xorriso->partition_secs_per_head= u;

 } else if(strncmp(treatpt, "partition_cyl_align=", 20)==0) {
   if(strcmp(treatpt + 20, "auto") == 0)
     xorriso->system_area_options= (xorriso->system_area_options & ~0x300);
   else if(strcmp(treatpt + 20, "on") == 0)
     xorriso->system_area_options=
                               (xorriso->system_area_options & ~0x300) | 0x100;
   else if(strcmp(treatpt + 20, "off") == 0)
     xorriso->system_area_options=
                               (xorriso->system_area_options & ~0x300) | 0x200;
   else if(strcmp(treatpt + 20, "all") == 0)
     xorriso->system_area_options=
                               (xorriso->system_area_options & ~0x300) | 0x300;
   else {
     sprintf(xorriso->info_text,
             "-boot_image %s partition_cyl_align: unknown mode : %s",
             formpt, treatpt + 20);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
     {ret= 0; goto ex;}
   }

 } else if(strncmp(treatpt, "platform_id=", 12)==0) {
   if(strncmp(treatpt + 12, "0x", 2) == 0)
     sscanf(treatpt + 14, "%x", &u);
   else
     sscanf(treatpt + 12, "%u", &u);
   if(u > 0xff) {
     sprintf(xorriso->info_text,
             "-boot_image %s : platform_id too large (%s > 0xff)",
             formpt, treatpt + 12);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
   xorriso->boot_platform_id= u;
   
 } else if(strncmp(treatpt, "emul_type=", 10)==0) {
   if(strcmp(treatpt + 10, "none") == 0 ||
      strcmp(treatpt + 10, "no_emulation") == 0) {
     xorriso->boot_image_emul= 0;
     xorriso->boot_emul_default= 0;
   } else if(strcmp(treatpt + 10, "hard_disk") == 0) {
     xorriso->boot_image_emul= 1;
     xorriso->boot_emul_default= 0;
   } else if(strcmp(treatpt + 10, "floppy") == 0 ||
             strcmp(treatpt + 10, "diskette") == 0) {
     xorriso->boot_image_emul= 2;
     xorriso->boot_emul_default= 0;
   } else {
     sprintf(xorriso->info_text,
             "-boot_image %s : Unknown media_type : %s",
             formpt, treatpt + 10);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }

 } else if(strncmp(treatpt, "hfsplus_serial=", 15) == 0) {
   ret= Hex_to_bin(treatpt + 15, 8, &bin_count, (unsigned char *) sn, 0);
   if(ret <= 0 || bin_count != 8) {
     sprintf(xorriso->info_text,
             "boot_image %s : Malformed hfsplus_serial : %s",
             formpt, treatpt + 15);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     Xorriso_msgs_submit(xorriso, 0,
                         "Expected is a string of 16 hex digits [0-9a-fA-F]",
                         0, "HINT", 0);
     ret= 0; goto ex;
   } else {
     memcpy(xorriso->hfsp_serial_number, sn, 8);
   }

 } else if(strncmp(treatpt, "hfsplus_block_size=", 19) == 0) {
   u= 0;
   sscanf(treatpt + 19, "%u", &u);
   if(u != 0 && u!= 512 && u != 2048) {
     sprintf(xorriso->info_text,
             "boot_image %s : Malformed hfsplus_block_size : %s",
             formpt, treatpt + 19);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     Xorriso_msgs_submit(xorriso, 0, "Expected are 0, 512, or 2048",
                         0, "HINT", 0);
     ret= 0; goto ex;
   }
   xorriso->hfsp_block_size= u;

 } else if(strncmp(treatpt, "apm_block_size=", 15) == 0) {
   u= 0;
   sscanf(treatpt + 15, "%u", &u);
   if(u != 0 && u!= 512 && u != 2048) {
     sprintf(xorriso->info_text,
             "boot_image %s : Malformed apm_block_size : %s",
             formpt, treatpt + 15);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     Xorriso_msgs_submit(xorriso, 0, "Expected are 0, 512, or 2048",
                         0, "HINT", 0);
     ret= 0; goto ex;
   }
   xorriso->apm_block_size= u;

 } else if(strncmp(treatpt, "efi_boot_part=", 14) == 0) {
   if(Sfile_str(xorriso->efi_boot_partition, treatpt + 14, 0) <= 0)
     {ret= -1; goto ex;}

 } else if(strncmp(treatpt, "prep_boot_part=", 15) == 0) {
   if(Sfile_str(xorriso->prep_partition, treatpt + 15, 0) <= 0)
     {ret= -1; goto ex;}

 } else if(strncmp(treatpt, "chrp_boot_part=", 15) == 0) {
   if(strcmp(treatpt + 15, "on") == 0) {
     xorriso->system_area_options= (xorriso->system_area_options & ~0x3cfc) |
                                   0x400;
   } else if(strcmp(treatpt + 15, "off") == 0) {
     xorriso->system_area_options= xorriso->system_area_options & ~0x3c00;
   } else {
     sprintf(xorriso->info_text,
             "-boot_image %s chrp_boot_part: unknown mode : %s",
             formpt, treatpt + 15);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
     {ret= 0; goto ex;}
   }

 } else if(strncmp(treatpt, "isohybrid=", 10) == 0 &&
           strcmp(formpt, "isolinux")==0) {

#ifdef Xorriso_with_isohybriD

   if(strcmp(treatpt + 10, "off") == 0)
     xorriso->boot_image_isohybrid= 0;
   else if(strcmp(treatpt + 10, "auto") == 0)
     xorriso->boot_image_isohybrid= 1;
   else if(strcmp(treatpt + 10, "on") == 0)
     xorriso->boot_image_isohybrid= 2;
   else if(strcmp(treatpt + 10, "force") == 0)
     xorriso->boot_image_isohybrid= 3;
   else {
     sprintf(xorriso->info_text,
             "Unrecognized keyword with -boot_image %s %s",
             form, treatment);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     sprintf(xorriso->info_text,
             "Allowed with isohybrid= are: off , auto , on , force");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
     {ret= 0; goto ex;}
   }

#else

   if(strcmp(treatpt + 10, "off") == 0) {
     xorriso->boot_image_isohybrid= 0;
   } else {
     sprintf(xorriso->info_text,
             "isohybrid MBR generation has been disabled on request of its inventor H. Peter Anvin on 31 Mar 2010");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     sprintf(xorriso->info_text,
     "It has been replaced by -boot_image isolinux system_area=External-File");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
   }

#endif /* ! Xorriso_with_isohybriD */

 } else if(strncmp(treatpt, "grub2_mbr=", 9) == 0 &&
           strcmp(formpt, "grub")==0) {

   if(strcmp(treatpt + 9, "off") == 0)
     xorriso->system_area_options&= ~0x4000;
   else if(strcmp(treatpt + 9, "on") == 0)
     xorriso->system_area_options=
                                  (xorriso->system_area_options & ~2) | 0x4000;
   else {
     sprintf(xorriso->info_text,
             "Unrecognized keyword with -boot_image %s %s",
             form, treatment);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     sprintf(xorriso->info_text,
             "Allowed with grub2_mbr= are: off , on");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
     {ret= 0; goto ex;}
   }

 } else
   was_ok= 0;

 if(!was_ok) {
   sprintf(xorriso->info_text, "Unrecognized options with -boot_image: %s %s",
           form, treatment);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 ret= 1;
ex:
 Xorriso_free_meM(eff_path);
 return(ret);
}


/* Option -calm_drive */
int Xorriso_option_calm_drive(struct XorrisO *xorriso, char *which, int flag)
{
 int gu_flag= 0, ret;

 if(strcmp(which,"in")==0)
   gu_flag= 1;
 else if(strcmp(which,"out")==0)
   gu_flag= 2;
 else if(strcmp(which,"on")==0) {
   xorriso->do_calm_drive|= 1;
 } else if(strcmp(which,"off")==0) {
   xorriso->do_calm_drive&= ~1;
 } else if(strcmp(which,"revoke")==0) {
   gu_flag= 7;
 } else
   gu_flag= 3;
 ret= Xorriso_drive_snooze(xorriso, gu_flag);
 return(ret);
}


/* Option -cd alias -cdi */
int Xorriso_option_cdi(struct XorrisO *xorriso, char *iso_rr_path, int flag)
{
 char *path= NULL, *eff_path= NULL;
 int ret;

 Xorriso_alloc_meM(path, char, SfileadrL);
 Xorriso_alloc_meM(eff_path, char, SfileadrL);

 if (strlen(iso_rr_path)>sizeof(xorriso->wdi)) {
   sprintf(xorriso->info_text,"-cdi: iso_rr_path too long (%d > %d)",
           (int) strlen(iso_rr_path), (int) sizeof(xorriso->wdi)-1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 Xorriso_warn_of_wildcards(xorriso, iso_rr_path, 1);
 sprintf(xorriso->info_text,"previous working directory:\n");
 Xorriso_info(xorriso,0);
 Text_shellsafe(xorriso->wdi, xorriso->result_line, 0);
 strcat(xorriso->result_line, "/\n");
 Xorriso_result(xorriso,0);
 if(strcmp(iso_rr_path,"/")==0 || iso_rr_path[0]==0) {
   strcpy(xorriso->wdi,"");
   Xorriso_option_pwdi(xorriso, 0);
   {ret= 1; goto ex;}
 } else if(iso_rr_path[0]!='/') {
   strcpy(path, xorriso->wdi);
   if(Sfile_add_to_path(path,iso_rr_path,0)<=0)
     {ret= -1; goto ex;}
 } else {
   if(Sfile_str(path,iso_rr_path,0)<=0)
     {ret= -1; goto ex;}
 }

 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, path, eff_path, 1);
 if(ret<0)
   goto ex;
 if(ret==0) {
   sprintf(xorriso->info_text, "-cdi: not existing yet in ISO image : ");
   Text_shellsafe(path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, path, eff_path, 2);
   if(ret<=0)
     goto ex;
 } else if(ret!=2) {
   sprintf(xorriso->info_text, "-cdi: not a directory : ");
   Text_shellsafe(eff_path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 strcpy(xorriso->wdi, eff_path);

 Xorriso_option_pwdi(xorriso, 0);
 ret= 1;
ex:;
 Xorriso_free_meM(path);
 Xorriso_free_meM(eff_path);
 return(ret);
}


/* Option -cdx */
int Xorriso_option_cdx(struct XorrisO *xorriso, char *disk_path, int flag)
{
 char *path= NULL, *eff_path= NULL;
 int ret;

 Xorriso_alloc_meM(path, char, SfileadrL);
 Xorriso_alloc_meM(eff_path, char, SfileadrL);

 if (strlen(disk_path)>sizeof(xorriso->wdx)) {
   sprintf(xorriso->info_text,"-cdx: disk_path too long (%d > %d)",
           (int) strlen(disk_path), (int) sizeof(xorriso->wdx)-1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 Xorriso_warn_of_wildcards(xorriso, disk_path, 1|2);
 sprintf(xorriso->info_text,"previous working directory on hard disk:\n");
 Xorriso_info(xorriso,0);
 Text_shellsafe(xorriso->wdx, xorriso->result_line, 0);
 strcat(xorriso->result_line, "/\n");
 Xorriso_result(xorriso,0);
 if(strcmp(disk_path,"/")==0) {
   strcpy(xorriso->wdx,"");
   Xorriso_option_pwdx(xorriso, 0);
   {ret= 1; goto ex;}
 } else if(disk_path[0]!='/') {
   strcpy(path, xorriso->wdx);
   if(Sfile_add_to_path(path,disk_path,0)<=0)
     {ret= -1; goto ex;}
 } else {
   if(Sfile_str(path,disk_path,0)<=0)
     {ret= -1; goto ex;}
 }

 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdx, path, eff_path, 2|4);
 if(ret<=0)
   goto ex;
 if(eff_path[0]) {
   ret= Sfile_type(eff_path,1|4|8);
   if(ret<0) {
     Xorriso_msgs_submit(xorriso, 0, eff_path, 0, "ERRFILE", 0);
     sprintf(xorriso->info_text,"-cdx: file not found : ");
     Text_shellsafe(eff_path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
   if(ret!=2) {
     Xorriso_msgs_submit(xorriso, 0, eff_path, 0, "ERRFILE", 0);
     sprintf(xorriso->info_text, "-cdx: not a directory : ");
     Text_shellsafe(eff_path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
 }
 if(Sfile_str(xorriso->wdx,eff_path,0)<=0)
   {ret= -1; goto ex;}
 Xorriso_option_pwdx(xorriso, 0);
 ret= 1;
ex:;
 Xorriso_free_meM(path);
 Xorriso_free_meM(eff_path);
 return(ret);
}


/* Option -changes_pending */
int Xorriso_option_changes_pending(struct XorrisO *xorriso, char *state,
                                   int flag)
{
 if(strcmp(state, "no") == 0)
   xorriso->volset_change_pending= 0;
 else if(strcmp(state, "yes") == 0)
   xorriso->volset_change_pending= 1;
 else if(strcmp(state, "mkisofs_printed") == 0)
   xorriso->volset_change_pending= 2;
 else if(strcmp(state, "show_status") == 0) {
   strcpy(xorriso->result_line, "-changes_pending ");
   if(xorriso->volset_change_pending == 0)
     strcat(xorriso->result_line, "no");
   else if(xorriso->volset_change_pending == 2)
     strcat(xorriso->result_line, "mkisofs_printed");
   else
     strcat(xorriso->result_line, "yes");
   strcat(xorriso->result_line, "\n");
   Xorriso_result(xorriso,0);
 } else {
   sprintf(xorriso->info_text, "-changes_pending: unknown state code '%s'",
           state);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(1);
}


/* Option -charset */
/* @param flag bit0= set in_charset
               bit1= set out_charset
               bit2= set local_charset
*/
int Xorriso_option_charset(struct XorrisO *xorriso, char *name, int flag)
{
 int ret;
 char *name_pt= NULL, *local_charset;
 iconv_t iconv_ret= (iconv_t) -1;

 if(name != NULL)
   if(name[0] != 0)
     name_pt= name;
 if(flag & 4) {
   ret= Xorriso_set_local_charset(xorriso, name_pt, 0);
   if(ret <= 0)
     return(ret);
 }
 if(flag & 1) {
   if(name_pt != NULL) {
     Xorriso_get_local_charset(xorriso, &local_charset, 0);
     iconv_ret= iconv_open(local_charset, name_pt);
     if(iconv_ret == (iconv_t) -1) {
       sprintf(xorriso->info_text,
               "-%scharset: Cannot convert from character set ",
               flag & 2 ? "" : "in_");
       Text_shellsafe(name_pt, xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE",
                           0);
       return(0);
     } else
       iconv_close(iconv_ret);
   }
   if(Sregex_string(&(xorriso->in_charset), name_pt, 0) <= 0) {
     Xorriso_no_malloc_memory(xorriso, NULL, 0);
     return(-1);
   }
 }
 if(flag & 2) {
   if(name_pt != NULL) {
     Xorriso_get_local_charset(xorriso, &local_charset, 0);
     iconv_ret= iconv_open(local_charset, name_pt);
     if(iconv_ret == (iconv_t) -1) {
       sprintf(xorriso->info_text, "-%scharset: Cannot convert to charset ",
               flag & 1 ? "" : "out_");
       Text_shellsafe(name_pt, xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE",
                           0);
       return(0);
     } else
       iconv_close(iconv_ret);
   }
   if(Sregex_string(&(xorriso->out_charset), name_pt, 0) <= 0) {
     Xorriso_no_malloc_memory(xorriso, NULL, 0);
     return(-1);
   }
 }
 if(flag & 3) {
   if(name_pt == NULL)
     Xorriso_get_local_charset(xorriso, &name_pt, 0);
   sprintf(xorriso->info_text, "Character set for %sconversion is now: ",
           (flag & 3) == 1 ? "input " : (flag & 3) == 2 ? "output " : "");
   Text_shellsafe(name_pt, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
 }
 return(1);
}


/* Options -check_md5 and -check_md5_r
   @param flag bit0= issue summary message
               bit1= do not reset pacifier, no final pacifier message
         >>>   bit2= do not issue pacifier messages at all
               bit3= recursive: -check_md5_r
*/
int Xorriso_option_check_md5(struct XorrisO *xorriso,
                             int argc, char **argv, int *idx, int flag)
{
 int ret, i, mem_pci, end_idx, fret, sev, do_report= 0;
 int optc= 0;
 char **optv= NULL, *cpt, *severity= "ALL", off_severity[20];
 struct FindjoB *job= NULL;
 double mem_lut= 0.0;

 mem_pci= xorriso->pacifier_interval;

 ret= Xorriso_opt_args(xorriso, "-check_md5", argc, argv, *idx + 1, 
                       &end_idx, &optc, &optv, 128);
 if(ret<=0)
   goto ex;

 /* Interpret argv[*idx] as severity */
 if(argc <= *idx) {
   sprintf(xorriso->info_text,
           "-check_md5: No event severity given for case of mismatch");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   ret= 0; goto ex;
 }
 severity= argv[*idx];
 Xorriso__to_upper(severity, off_severity, (int) sizeof(off_severity), 0);
 severity= off_severity;
 ret= Xorriso__text_to_sev(severity, &sev, 0);
 if(ret<=0) {
   sprintf(xorriso->info_text, "-check_md5: Not a known severity name : ");
   Text_shellsafe(severity, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   goto ex;
 }

 if(!(flag & (2 | 4))) {
   Xorriso_pacifier_reset(xorriso, 0);
   mem_lut= xorriso->last_update_time;
 }
 xorriso->pacifier_interval= 5.0;

 xorriso->find_check_md5_result= 0;

 if(optc == 0) {
   ret= Xorriso_check_session_md5(xorriso, severity, 0);
   do_report= 1;
   goto ex;
 } 

 for(i= 0; i < optc; i++) {
   if(flag & 8) {
     ret= Findjob_new(&job, optv[i], 0);
     if(ret<=0) {
       Xorriso_no_findjob(xorriso, "-check_md5_r", 0);
       {ret= -1; goto ex;}
     }
     Findjob_set_action_target(job, 35, severity, 0);
     cpt= optv[i];
     ret= Xorriso_findi_sorted(xorriso, job, (off_t) 0, 1, &cpt, 0);
     Findjob_destroy(&job, 0);
     if(ret > 0)
       ret= xorriso->find_compare_result;
     else {
       ret= -1;
       xorriso->find_check_md5_result|= 2;
     }
   } else {
     ret= Xorriso_check_md5(xorriso, NULL, optv[i], 4);
     if(ret < 0) 
       xorriso->find_check_md5_result|= 2;
     else if(ret == 0)
       xorriso->find_check_md5_result|= 1;
     else if(ret == 1)
       xorriso->find_check_md5_result|= 8;
     else if(ret == 2)
       xorriso->find_check_md5_result|= 4;
   }
   if(ret>0 && !xorriso->request_to_abort)
 continue; /* regular bottom of loop */
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret>=0)
 continue;
   ret= 0; goto report_outcome;
 }
 ret= 1;

report_outcome:;
 do_report= 1;

ex:;
 if(!(flag & (2 | 4))) {
   xorriso->pacifier_interval= mem_pci;
   if(mem_lut!=xorriso->last_update_time && !(flag&2))
     Xorriso_pacifier_callback(xorriso, "content bytes read",
                               xorriso->pacifier_count, 0, "", 1 | 8 | 32);
 }
 if(do_report) {
   if(optc == 0) {
     if(ret <= 0) {
       sprintf(xorriso->result_line,
               "MD5 MISMATCH WITH DATA OF LOADED SESSION !\n");
       Xorriso_result(xorriso,0);
       if(strcmp(severity, "ALL") != 0) {
         sprintf(xorriso->info_text,
                 "Event triggered by MD5 comparison mismatch");
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, severity, 0);
       }
     } else {
       sprintf(xorriso->result_line, "Ok, session data match recorded md5.\n");
       Xorriso_result(xorriso,0);
     }
   } else {
     Xorriso_report_md5_outcome(xorriso, severity, 0);
   }
 }
 (*idx)= end_idx;
 Xorriso_opt_args(xorriso, "-getfacl", argc, argv, *idx, &end_idx,
                  &optc, &optv, 256); 
 Findjob_destroy(&job, 0);
 if(ret <= 0)
   return(ret);
 return((xorriso->find_check_md5_result & 3) == 0);
}


/* Option -check_media */
int Xorriso_option_check_media(struct XorrisO *xorriso,
                               int argc, char **argv, int *idx, int flag)
{
 int ret, i, count, lba, blocks, quality, pass, was_md5= 0, was_event= 0;
 int end_idx, old_idx, os_errno;
 char quality_name[80], *head_buffer= NULL;
 struct SpotlisT *spotlist= NULL;
 struct CheckmediajoB *job= NULL;
 struct FindjoB *findjob= NULL;
 struct stat dir_stbuf;

 old_idx= *idx;
 end_idx= Xorriso_end_idx(xorriso, argc, argv, *idx, 1);
 (*idx)= end_idx;

 Xorriso_alloc_meM(head_buffer, char, 64 * 1024);

 ret= Checkmediajob_new(&job, 0);
 if(ret <= 0)
   goto ex;
 ret= Xorriso_check_media_setup_job(xorriso, job, argv, old_idx, end_idx, 0);
 if(ret <= 0)
   goto ex;

 if((job->report_mode == 1 || job->report_mode == 2) && job->use_dev == 1) {
   sprintf(xorriso->info_text,
           "-check_media: cannot report=*files while use=outdef");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   ret= 0; goto ex;
 }
 if(job->patch_lba0 && job->data_to_path[0] == 0) {
   sprintf(xorriso->info_text,
      "-check_media: cannot apply patch_lba0= while data_to= has empty value");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   ret= 0; goto ex;
 }

 if(job->use_dev == 2) {
   if(job->sector_map_path[0] == 0) {
     sprintf(xorriso->info_text,
             "-check_media: option use=sector_map but sector_map=''");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }
   ret= Sectorbitmap_from_file(&(job->sector_map), job->sector_map_path,
                               xorriso->info_text, &os_errno, 0);
   if(ret <= 0) {
     if(xorriso->info_text[0])
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, os_errno,
                           "FAILURE", 0);
     goto ex;
   }
   ret= Xorriso_sectormap_to_spotlist(xorriso, job, &spotlist, 0);
   if(ret <= 0)
     goto ex;
   Sectorbitmap_destroy(&(xorriso->in_sector_map), 0);
   ret= Sectorbitmap_clone(job->sector_map, &(xorriso->in_sector_map), 0);
   if(ret <= 0) 
     goto ex;
 } else {
   ret= Xorriso_check_media(xorriso, &spotlist, job, 0);
   if(ret <= 0)
     goto ex;
 }

 if(job->patch_lba0) {
   ret= Xorriso_open_job_data_to(xorriso, job, 0);
   if(ret <= 0)
     goto ex;
   if(ret == 1) {
     ret= Xorriso_update_iso_lba0(xorriso, job->patch_lba0_msc1, 0,
                                  head_buffer, job,
                                  (8 * (job->patch_lba0 == 1)) |
                                  4 | (job->patch_lba0_msc1 < 0));
     if(ret <= 0)
       goto ex;
   }
 }

 if(job->report_mode == 0 || job->report_mode == 2) { /* report blocks */
   for(pass= 0; pass < 2; pass++) {
     if(pass == 0) {
       sprintf(xorriso->result_line,
               "Media checks :        lba ,       size , quality\n");
     } else {
       if(!was_md5)
   break;
       sprintf(xorriso->result_line,
               "MD5 checks   :        lba ,       size , result\n");
     }
     Xorriso_result(xorriso,0);
     count= Spotlist_count(spotlist, 0);
     for(i= 0; i < count; i++) {
       ret= Spotlist_get_item(spotlist, i, &lba, &blocks, &quality, 0);
       if(ret <= 0)
     continue;
       if(pass == 0) {
         if(quality == Xorriso_read_quality_md5_mismatcH ||
            quality == Xorriso_read_quality_unreadablE) {
           was_event= 1;
         }
         if(quality == Xorriso_read_quality_md5_matcH ||
            quality == Xorriso_read_quality_md5_mismatcH) {
           was_md5= 1;
     continue;
         }
       }
       else if(pass == 1 && !(quality == Xorriso_read_quality_md5_matcH ||
                              quality == Xorriso_read_quality_md5_mismatcH))
     continue;
       sprintf(xorriso->result_line, "%s: %10d , %10d , %s\n",
               pass == 0 ? "Media region " : "MD5 tag range",
               lba, blocks, Spotlist__quality_name(quality, quality_name,
                                           xorriso->check_media_bad_limit, 0));
       Xorriso_result(xorriso,0);
     }
   }
 }
 if(job->report_mode == 1 || job->report_mode == 2) { /* report files */
   ret= Findjob_new(&findjob, "/", 0);
   if(ret<=0) {
     Xorriso_no_findjob(xorriso, "-check_media report=files", 0);
     {ret= -1; goto ex;}
   }
   Findjob_set_damage_filter(findjob, 1, 0);
   Findjob_set_action_target(findjob, 21, NULL, 0);
   ret= Xorriso_findi(xorriso, findjob, NULL, (off_t) 0,
                      NULL, "/", &dir_stbuf, 0, 0);
   Findjob_destroy(&findjob, 0);
   if(ret <= 0)
     goto ex;
 }
 ret= 1;
ex:;
 if(was_event && strcmp(job->event_severity, "ALL") != 0) {
   sprintf(xorriso->info_text,
           "Event triggered by media read error or MD5 comparison mismatch");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, job->event_severity,
                       0);
 }
 Spotlist_destroy(&spotlist, 0);
 Checkmediajob_destroy(&job, 0);
 Xorriso_free_meM(head_buffer);
 return(ret);
}


/* Option -check_media_defaults */
int Xorriso_option_check_media_defaults(struct XorrisO *xorriso,
                                     int argc, char **argv, int *idx, int flag)
{
 int ret, old_idx, end_idx;
 struct CheckmediajoB *job= NULL;

 old_idx= *idx;
 end_idx= Xorriso_end_idx(xorriso, argc, argv, *idx, 1);
 (*idx)= end_idx;

 ret= Checkmediajob_new(&job, 0);
 if(ret <= 0)
   goto ex;
 ret= Xorriso_check_media_setup_job(xorriso, job, argv, old_idx, end_idx, 0);
 if(ret <= 0)
   goto ex;
 Checkmediajob_destroy(&(xorriso->check_media_default), 0);
 xorriso->check_media_default= job;
 job= NULL;
 ret= 1;
ex:;
 Checkmediajob_destroy(&job, 0);
 return(ret);
}


/* Option -chgrp alias -chgrpi , chgrp_r alias chgrpi */
/* @param flag bit0=recursive (-chgrp_r)
*/
int Xorriso_option_chgrpi(struct XorrisO *xorriso, char *gid,
                          int argc, char **argv, int *idx, int flag)
{
 int i, ret, was_failure= 0, end_idx, fret;
 gid_t gid_number;
 int optc= 0;
 char **optv= NULL;
 struct FindjoB *job= NULL;
 struct stat dir_stbuf;

 ret= Xorriso_opt_args(xorriso, "-chgrpi", argc, argv, *idx, &end_idx, &optc,
                       &optv, 0);
 if(ret<=0)
   goto ex; 
 ret= Xorriso_convert_gidstring(xorriso, gid, &gid_number, 0);
 if(ret<=0)
   goto ex; 
 for(i= 0; i<optc; i++) {
   if(flag&1) {
     ret= Findjob_new(&job, optv[i], 0);
     if(ret<=0) {
       Xorriso_no_findjob(xorriso, "-chgrp_r", 0);
       {ret= -1; goto ex;}
     }
     Findjob_set_action_chgrp(job, gid_number, 0);
     ret= Xorriso_findi(xorriso, job, NULL,  (off_t) 0,
                        NULL, optv[i], &dir_stbuf, 0, 0);
     Findjob_destroy(&job, 0);
   } else 
     ret= Xorriso_set_gid(xorriso, optv[i], gid_number, 0);
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
 Xorriso_opt_args(xorriso, "-chgrpi", argc, argv, *idx, &end_idx, &optc, &optv,
                  256); /* clean up */
 if(ret<=0)
   return(ret);
 Findjob_destroy(&job, 0);
 return(!was_failure);
}


/* Option -chmod alias -chmodi , -chmod_r alias chmod_ri */
/* @param flag bit0=recursive (-chmod_r)
*/
int Xorriso_option_chmodi(struct XorrisO *xorriso, char *mode,
                          int argc, char **argv, int *idx, int flag)
{
 int i, ret, was_failure= 0, end_idx, fret;
 mode_t mode_and= ~0, mode_or= 0;
 int optc= 0;
 char **optv= NULL;
 struct FindjoB *job= NULL;
 struct stat dir_stbuf;

 ret= Xorriso_opt_args(xorriso, "-chmodi", argc, argv, *idx, &end_idx, &optc,
                       &optv, 0);
 if(ret<=0)
   goto ex; 
 ret= Xorriso_convert_modstring(xorriso, "-chmodi",
                                mode, &mode_and, &mode_or, 0);
 if(ret<=0)
   goto ex;
 for(i= 0; i<optc; i++) {
   if(flag&1) {
     ret= Findjob_new(&job, optv[i], 0);
     if(ret<=0) {
       Xorriso_no_findjob(xorriso, "-chmod_r", 0);
       {ret= -1; goto ex;}
     }
     Findjob_set_action_chmod(job, mode_and, mode_or, 0);
     ret= Xorriso_findi(xorriso, job, NULL, (off_t) 0,
                        NULL, optv[i], &dir_stbuf, 0, 0);
     Findjob_destroy(&job, 0);
   } else {
     ret= Xorriso_set_st_mode(xorriso, optv[i], mode_and, mode_or, 0);
   }
   if(ret>0 && !xorriso->request_to_abort)
 continue; /* regular bottom of loop */
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret>=0)
 continue;
   ret= 0; goto ex;
 }
 ret= 1;
ex:;
 (*idx)= end_idx;
 Xorriso_opt_args(xorriso, "-chmodi", argc, argv, *idx, &end_idx, &optc, &optv,
                  256);
 Findjob_destroy(&job, 0);
 if(ret<=0)
   return(ret);
 return(!was_failure);
}


/* Option -chown alias -chowni , chown_r alias chown_ri */
/* @param flag bit0=recursive (-chown_r)
*/
int Xorriso_option_chowni(struct XorrisO *xorriso, char *uid,
                          int argc, char **argv, int *idx, int flag)
{
 int i, ret, was_failure= 0, end_idx, fret;
 uid_t uid_number;
 int optc= 0;
 char **optv= NULL;
 struct FindjoB *job= NULL;
 struct stat dir_stbuf;

 ret= Xorriso_opt_args(xorriso, "-chowni", argc, argv, *idx, &end_idx,
                       &optc, &optv, 0);
 if(ret<=0)
   goto ex;
 ret= Xorriso_convert_uidstring(xorriso, uid, &uid_number, 0);
 if(ret<=0)
   goto ex; 
 for(i= 0; i<optc; i++) {
   if(flag&1) {
     ret= Findjob_new(&job, optv[i], 0);
     if(ret<=0) {
       Xorriso_no_findjob(xorriso, "-chown_r", 0);
       {ret= -1; goto ex;}
     }
     Findjob_set_action_chown(job, uid_number, 0);
     ret= Xorriso_findi(xorriso, job, NULL, (off_t) 0,
                        NULL, optv[i], &dir_stbuf, 0, 0);
     Findjob_destroy(&job, 0);
   } else 
     ret= Xorriso_set_uid(xorriso, optv[i], uid_number, 0);
   if(ret>0 && !xorriso->request_to_abort)
 continue; /* regular bottom of loop */
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret>=0)
 continue;
   ret= 0; goto ex;
 }
 ret= 1;
ex:;
 (*idx)= end_idx;
 Xorriso_opt_args(xorriso, "-chowni", argc, argv, *idx, &end_idx,
                  &optc, &optv, 256);
 Findjob_destroy(&job, 0);
 if(ret<=0)
   return(ret);
 return(!was_failure);
}


/* Option -clone */
int Xorriso_option_clone(struct XorrisO *xorriso, char *origin, char *dest,
                         int flag)
{
 int ret;
 
 ret= Xorriso_clone_tree(xorriso, NULL, origin, dest, 0);
 return(ret);
}


/* Option -close "on"|"off" */
int Xorriso_option_close(struct XorrisO *xorriso, char *mode, int flag)
{
 xorriso->do_close= !!strcmp(mode, "off");
 return(1);
}


/* Option -close_damaged */
int Xorriso_option_close_damaged(struct XorrisO *xorriso, char *mode, int flag)
{
 int ret, force= 0;
 
 if(strcmp(mode, "as_needed") == 0 || strcmp(mode, "") == 0)
   force= 0;
 else if(strcmp(mode, "force") == 0)
   force= 1;
 else {
   sprintf(xorriso->info_text, "-close_damaged: unknown mode ");
   Text_shellsafe(mode, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 ret= Xorriso_reassure(xorriso, "-close_damaged",
                       "Close damaged track and session", 0);
 if(ret <= 0)
   {ret= 2; goto ex;}
 ret= Xorriso_close_damaged(xorriso, force);
 if(ret <= 0)
   goto ex;
 ret= 1;
ex:;
 return(ret);
}


/* Option -close_filter_list */
int Xorriso_option_close_filter_list(struct XorrisO *xorriso, int flag)
{
 xorriso->filter_list_closed= 1;
 return(1);
}


/* Option -commit */
/* @param flag bit0= leave indrive and outdrive aquired as they were,
                     i.e. do not aquire outdrive as new in-out-drive
               bit1= do not perform eventual -reassure
   @return <=0 error , 1 success, 2 revoked by -reassure , 3 no change pending
*/
int Xorriso_option_commit(struct XorrisO *xorriso, int flag)
{
 int ret;

 if(!Xorriso_change_is_pending(xorriso, 0)) {
   sprintf(xorriso->info_text, "-commit: No image modifications pending");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
   {ret= 3; goto ex;}
 }
 if(!(flag&2)) {
   ret= Xorriso_reassure(xorriso, "-commit",
                         "write the pending image changes to the medium", 0);
   if(ret<=0)
     {ret= 2; goto ex;}
 }
 Xorriso_process_errfile(xorriso, 0, "burn session start", 0, 1);
 ret= Xorriso_write_session(xorriso, 0);
 Xorriso_process_errfile(xorriso, 0, "burn session end", 0, 1);
 if(ret<=0)
   goto ex;
 Xorriso_write_session_log(xorriso, 0);
 xorriso->volset_change_pending= 0;
 xorriso->no_volset_present= 0;
 if(flag&1)
   {ret= 1; goto ex;}
 if(Sregex_string(&(xorriso->in_charset), xorriso->out_charset, 0) <= 0)
   {ret= -1; goto ex;}
 if(xorriso->grow_blindly_msc2>=0)
   ret= Xorriso_option_dev(xorriso, "", 3|4);
 else {
   xorriso->displacement= 0;
   xorriso->displacement_sign= 0;
   ret= Xorriso_reaquire_outdev(xorriso, 3);
   if(xorriso->in_drive_handle == NULL)
     xorriso->image_start_mode= 0; /* session setting is invalid by now */
 }
ex:;
 return(ret);
}


/* Option -commit_eject */
/* @return <=0 error , 1 success, 2 revoked by -reassure
*/
int Xorriso_option_commit_eject(struct XorrisO *xorriso, char *which, int flag)
{
 int ret, eret;

 ret= Xorriso_option_commit(xorriso, 1);
 if(ret<=0 || ret==2 || ret==3)
   return(ret);
 if(strcmp(which, "none")==0)
   eret= 1;
 else
   eret= Xorriso_option_eject(xorriso, which, 1);
 ret= Xorriso_option_dev(xorriso, "", 3|4);
 if(eret<ret)
   return(eret);
 return(ret);
}


/* Options -compare and -compare_r
   @param flag bit0= issue summary message
               bit1= do not reset pacifier, no final pacifier message
               bit2= do not issue pacifier messages at all
               bit3= recursive: -compare_r
*/
int Xorriso_option_compare(struct XorrisO *xorriso, char *disk_path,
                           char *iso_path, int flag)
{
 int ret, mem_pci, zero= 0, result, follow_links;
 double mem_lut= 0.0;
 char *ipth, *argv[6], *eff_origin= NULL, *eff_dest= NULL;

 Xorriso_alloc_meM(eff_origin, char, SfileadrL);
 Xorriso_alloc_meM(eff_dest, char, SfileadrL);

 ipth= iso_path;
 if(ipth[0]==0)
   ipth= disk_path;
 if(disk_path[0]==0) {
   sprintf(xorriso->info_text, "-compare: Empty disk_path given");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 1);
   {ret= 0; goto ex;}
 }
 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdx, disk_path, eff_origin,
                                 2|4|8);
 if(ret<=0)
   goto ex;
 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, ipth, eff_dest, 2|8);
 if(ret<=0)
   goto ex;

 if(xorriso->disk_excl_mode&8)
   ret= Xorriso_path_is_excluded(xorriso, eff_origin, 1);
 else
   ret= 0;
 if(ret!=0)
   goto report_outcome;
 if(!(flag&2)) {
   Xorriso_pacifier_reset(xorriso, 0);
   mem_lut= xorriso->last_update_time;
 }
 mem_pci= xorriso->pacifier_interval;
 xorriso->pacifier_interval= 5.0;

 if(flag&8) {
   xorriso->find_compare_result= 1;
   argv[0]= eff_dest;
   argv[1]= "-exec";
   argv[2]= "compare";
   argv[3]= eff_origin;
   zero= 0;
   ret= Xorriso_option_find(xorriso, 4, argv, &zero, 2); /* -findi */
   if(ret>0) {
     argv[0]= eff_origin;
     argv[1]= "-exec";
     argv[2]= "not_in_iso";
     argv[3]= eff_dest;
     zero= 0;
     ret= Xorriso_option_find(xorriso, 4, argv, &zero, 1|2); /* -findx */
     if(ret>0 && !xorriso->do_follow_mount) {
       argv[0]= eff_origin;
       argv[1]= "-type";
       argv[2]= "m";
       argv[3]= "-exec";
       argv[4]= "is_full_in_iso";
       argv[5]= eff_dest;
       zero= 0;
       ret= Xorriso_option_find(xorriso, 6, argv, &zero, 1|2); /* -findx */
     }
     if(ret>0)
       ret= xorriso->find_compare_result;
     else
       ret= -1;
   } else
     ret= -1;
 } else {
   follow_links= (xorriso->do_follow_links || xorriso->do_follow_param) << 28;
   ret= Xorriso_compare_2_files(xorriso, eff_origin, eff_dest, "", &result,
                                  2 | follow_links | ((flag&4)<<27) | (1<<30));
 }

 xorriso->pacifier_interval= mem_pci;
 if(mem_lut!=xorriso->last_update_time && !(flag&2))
   Xorriso_pacifier_callback(xorriso, "content bytes read",
                             xorriso->pacifier_count, 0, "", 1 | 8 | 32);
report_outcome:;
 if(ret>0) {
   sprintf(xorriso->result_line,
           "Both file objects match as far as expectable.\n");
 } else if(ret==0) {
   sprintf(xorriso->result_line, "Differences detected.\n");
 } else {
   sprintf(xorriso->result_line, "Comparison failed due to error.\n");
 }
 if(flag&1)
   Xorriso_result(xorriso,0);
 if(ret<0)
   goto ex;
 ret= 1;
ex:;
 Xorriso_free_meM(eff_origin);
 Xorriso_free_meM(eff_dest);
 return(ret);
}


/* Option -compliance */
int Xorriso_option_compliance(struct XorrisO *xorriso, char *mode,
                                    int flag)
{
 return(Xorriso_relax_compliance(xorriso, mode, 0));
}


/* Option -copyright_file */
int Xorriso_option_copyright_file(struct XorrisO *xorriso, char *name, int flag)
{
 if(Xorriso_check_name_len(xorriso, name,
                           (int) sizeof(xorriso->copyright_file),
                           "-copyright_file", 0) <= 0)
   return(0);
 strcpy(xorriso->copyright_file, name);
 Xorriso_set_change_pending(xorriso, 1);
 return(1);
}


/* Option -cp_clone */
int Xorriso_option_cp_clone(struct XorrisO *xorriso, int argc, char **argv,
                            int *idx, int flag)
{
 int i, end_idx_dummy, ret, is_dir= 0, was_failure= 0, fret, pass;
 char *eff_origin= NULL, *eff_dest= NULL, *dest_dir= NULL, *leafname= NULL;
 int optc= 0;
 char **optv= NULL;
 struct stat stbuf;

 Xorriso_alloc_meM(eff_origin, char, SfileadrL);
 Xorriso_alloc_meM(eff_dest, char, SfileadrL);
 Xorriso_alloc_meM(dest_dir, char, SfileadrL);
 Xorriso_alloc_meM(leafname, char, SfileadrL);

 ret= Xorriso_cpmv_args(xorriso, "-cp_clone", argc, argv, idx,
                        &optc, &optv, eff_dest, 1);
 if(ret<=0)
   goto ex;
 if(ret == 1 && optc > 1) {
nondir_exists:;
   sprintf(xorriso->info_text,
           "-cp_clone: Copy address already exists and is not a directory: ");
   Text_shellsafe(eff_dest, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 strcpy(dest_dir, eff_dest);
 if(optc == 1) {
   ret= Xorriso_iso_lstat(xorriso, eff_dest, &stbuf, 0);
   if(ret >= 0) {
     if(S_ISDIR(stbuf.st_mode))/* target directory exists */
       is_dir= 1;
     else
       goto nondir_exists;
   }
 } else {
   is_dir= 1;
   ret= Xorriso_mkdir(xorriso, dest_dir, 1 | 2);
   if(ret < 0)
     {ret= -(ret != -1); goto ex;}
 }

 /* Pass 0 checks whether the way is clear, pass 1 does the cloning */
 for(pass= 0; pass < 2; pass++) {
   for(i= 0; i<optc; i++) {
     ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi,
                                     optv[i], eff_origin, !!pass);
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
     ret= Xorriso_iso_lstat(xorriso, eff_dest, &stbuf, 0);
     if(pass == 0) {
       /* It is ok if both are directories */;
       if(ret >= 0 && S_ISDIR(stbuf.st_mode)) {
         ret= Xorriso_iso_lstat(xorriso, eff_origin, &stbuf, 0);
         if (ret >= 0  && S_ISDIR(stbuf.st_mode))
           ret= -1;
       }
       if(ret >= 0) {
         sprintf(xorriso->info_text, "Cloning: May not overwrite: ");
         Text_shellsafe(eff_dest, xorriso->info_text, 1);
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         goto problem_handler;
       }
     } else {
       ret= Xorriso_clone_tree(xorriso, NULL, eff_origin, eff_dest, 1);
       if(ret <= 0)
         goto problem_handler;
     }

   continue; /* regular bottom of loop */
problem_handler:;
     was_failure= 1;
     fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
     if(fret>=0)
   continue;
     goto ex;
   }
 }
 ret= !was_failure;
ex:;
 Xorriso_opt_args(xorriso, "-cp_clone",
                  argc, argv, *idx, &end_idx_dummy, &optc, &optv, 256);
 Xorriso_free_meM(eff_origin);
 Xorriso_free_meM(eff_dest);
 Xorriso_free_meM(dest_dir);
 Xorriso_free_meM(leafname);
 return(ret);
}


/* Option -cpr alias -cpri */
int Xorriso_option_cpri(struct XorrisO *xorriso, int argc, char **argv,
                         int *idx, int flag)
{
 int i, ret, is_dir= 0, was_failure= 0, fret, end_idx_dummy;
 char *eff_origin= NULL, *eff_dest= NULL, *dest_dir= NULL, *leafname= NULL;
 int optc= 0;
 char **optv= NULL;

 Xorriso_alloc_meM(eff_origin, char, SfileadrL);
 Xorriso_alloc_meM(eff_dest, char, SfileadrL);
 Xorriso_alloc_meM(dest_dir, char, SfileadrL);
 Xorriso_alloc_meM(leafname, char, SfileadrL);

 ret= Xorriso_cpmv_args(xorriso, "-cpri", argc, argv, idx,
                        &optc, &optv, eff_dest, 1|2);
 if(ret<=0)
   goto ex;
 if(ret==2) {
   is_dir= 1;
   strcpy(dest_dir, eff_dest);
 }

 /* Perform graft-ins */
 Xorriso_pacifier_reset(xorriso, 0);
 for(i= 0; i<optc && !xorriso->request_to_abort; i++) {
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdx, optv[i], eff_origin,
                                   2|4);
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
             (int) (strlen(eff_dest)+ strlen(leafname)+1));
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       goto problem_handler;
     }
   }
   ret= Xorriso_graft_in(xorriso, NULL, eff_origin, eff_dest,
                         (off_t) 0, (off_t) 0, 0);
   if(ret<=0 || xorriso->request_to_abort)
     goto problem_handler;
   sprintf(xorriso->info_text, "Added to ISO image: %s '%s'='%s'\n",
           (ret>1 ? "directory" : "file"), (eff_dest[0] ? eff_dest : "/"),
           eff_origin);
   if(!(flag&1))
     Xorriso_info(xorriso, 0);
 continue; /* regular bottom of loop */
problem_handler:;
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret>=0)
 continue;
   goto ex;
 }
 Xorriso_pacifier_callback(xorriso, "files added", xorriso->pacifier_count,
                               xorriso->pacifier_total, "", 1);
 ret= !was_failure;
ex:;
 Xorriso_opt_args(xorriso, "-cpri",
                  argc, argv, *idx, &end_idx_dummy, &optc, &optv, 256);
 Xorriso_free_meM(eff_origin);
 Xorriso_free_meM(eff_dest);
 Xorriso_free_meM(dest_dir);
 Xorriso_free_meM(leafname);
 return(ret);
}


/* Options -cpx , -cpax, -cp_rx , -cp_rax */
/* @param flag bit0= recursive (-cp_rx, -cp_rax)
               bit1= full property restore (-cpax, -cp_rax)
*/
int Xorriso_option_cpx(struct XorrisO *xorriso, int argc, char **argv,
                         int *idx, int flag)
{
 int i, ret, is_dir= 0, was_failure= 0, fret, end_idx_dummy, problem_count;
 char *eff_origin= NULL, *eff_dest= NULL, *dest_dir= NULL, *leafname= NULL;
 char **eff_src_array= NULL, **eff_tgt_array= NULL;
 int optc= 0;
 char **optv= NULL;
 struct stat stbuf;

 Xorriso_alloc_meM(eff_origin, char, SfileadrL);
 Xorriso_alloc_meM(eff_dest, char, SfileadrL);
 Xorriso_alloc_meM(dest_dir, char, SfileadrL);
 Xorriso_alloc_meM(leafname, char, SfileadrL);

 ret= Xorriso_cpmv_args(xorriso, "-cp*x", argc, argv, idx,
                        &optc, &optv, eff_dest, 1|4);
 if(ret<=0)
   goto ex;
 if(ret==2) {
   is_dir= 1;
   strcpy(dest_dir, eff_dest);
 }
 if(xorriso->allow_restore <= 0) {
   sprintf(xorriso->info_text,
          "-cpx: image-to-disk copies are not enabled by option -osirrox");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   ret= 0; goto ex;
 }

 if(xorriso->do_restore_sort_lba || !(xorriso->ino_behavior & 4)) {
   eff_src_array= calloc(optc, sizeof(char *));
   eff_tgt_array= calloc(optc, sizeof(char *));
   if(eff_src_array == NULL || eff_tgt_array == NULL) {
     Xorriso_no_malloc_memory(xorriso, NULL, 0);
     ret= -1; goto ex;
   }
   for(i= 0; i < optc; i++)
     eff_src_array[i]= eff_tgt_array[i]= NULL;
 }

 /* Perform copying */
 Xorriso_pacifier_reset(xorriso, 0);
 for(i= 0; i<optc && !xorriso->request_to_abort; i++) {
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, optv[i], eff_origin,
                                   2|8);
   if(ret<=0 || xorriso->request_to_abort)
     goto problem_handler;

   ret= Xorriso_iso_lstat(xorriso, eff_origin, &stbuf, 2|4);
   if(ret==-1)
     goto problem_handler;
   if(S_ISDIR(stbuf.st_mode) && !(flag&1)) {
     /* only allow directories if they actually represent split data files */
     ret= 0;
     if(xorriso->do_concat_split)
       ret= Xorriso_is_split(xorriso, eff_origin, NULL, 0);
     if(ret<0)
       goto problem_handler;
     if(ret==0) {
       sprintf(xorriso->info_text, "-cpx: May not copy directory ");
       Text_shellsafe(eff_origin, xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto problem_handler;
     }
   }

   if(is_dir && strcmp(eff_origin, "/")!=0) {
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
   if(eff_src_array != NULL) {
     eff_src_array[i]= strdup(eff_origin);
     eff_tgt_array[i]= strdup(eff_dest);
     if(eff_src_array[i] == NULL || eff_tgt_array[i] == NULL) {
       Xorriso_no_malloc_memory(xorriso, &(eff_src_array[i]), 0);
       ret= -1; goto ex;
     }
   } else {
     ret= Xorriso_restore(xorriso, eff_origin, eff_dest, (off_t) 0, (off_t) 0,
                          16 | ((!(flag&2))<<6));
     if(ret<=0 || xorriso->request_to_abort)
       goto problem_handler;
     if(ret==3 || (flag&1))
 continue;
     sprintf(xorriso->info_text,
             "Copied from ISO image to disk: %s '%s' = '%s'\n",
             (ret>1 ? "directory" : "file"), eff_origin, eff_dest);
     Xorriso_info(xorriso, 0);
   }
 continue; /* regular bottom of loop */
problem_handler:;
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret>=0)
 continue;
   goto ex;
 }

 if(eff_src_array != NULL) {
   ret= Xorriso_restore_sorted(xorriso, optc, eff_src_array, eff_tgt_array,
                               &problem_count, 0);
   if(ret <= 0 || problem_count > 0)
     was_failure= 1;
 }
 if(xorriso->pacifier_count>0)
   Xorriso_pacifier_callback(xorriso, "files restored",xorriso->pacifier_count,
                             xorriso->pacifier_total, "", 1|4);
 ret= !was_failure;
ex:;
 i= optc;
 Sfile_destroy_argv(&i, &eff_src_array, 0);
 i= optc; 
 Sfile_destroy_argv(&i, &eff_tgt_array, 0);
 Xorriso_opt_args(xorriso, "-cp*x",
                  argc, argv, *idx, &end_idx_dummy, &optc, &optv, 256);
 Xorriso_free_meM(eff_origin);
 Xorriso_free_meM(eff_dest);
 Xorriso_free_meM(dest_dir);
 Xorriso_free_meM(leafname);
 return(ret);
}


/* Option -cut_out */
int Xorriso_option_cut_out(struct XorrisO *xorriso, char *disk_path,
               char *start, char *count, char *iso_rr_path, int flag)
{
 int ret;
 double num;
 off_t startbyte, bytecount;
 
 num= Scanf_io_size(start, 0);
 if(num<0 || num > 1.0e18) { /* 10^18 = 10^3 ^ 6 < 2^10 ^ 6 = 2^60 */
   sprintf(xorriso->info_text,
         "-cut_out: startbyte address negative or much too large (%s)", start);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 startbyte= num;
 num= Scanf_io_size(count, 0);
 if(num<=0 || num > 1.0e18) {
   sprintf(xorriso->info_text,
           "-cut_out: bytecount zero, negative or much too large (%s)", count);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 bytecount= num;
 sprintf(xorriso->info_text, 
         "-cut_out from %s , byte %.f to %.f, and graft as %s",
         disk_path, (double) startbyte, (double) (startbyte+bytecount),
         iso_rr_path);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);

 ret= Xorriso_cut_out(xorriso, disk_path, startbyte, bytecount,
                      iso_rr_path, 0); 
 return(ret);
}

