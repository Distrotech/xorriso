
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2013 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the implementation of functions which deal with parsing
   and interpretation of command input.
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
#include <pwd.h>
#include <grp.h>
#include <sys/resource.h>
#include <sys/wait.h>


#include "xorriso.h"
#include "xorriso_private.h"
#include "xorrisoburn.h"


#ifdef Xorriso_fetch_with_msg_queueS
#include <pthread.h>
#endif


/* @param flag bit0= do not warn of wildcards
               bit1= these are disk_paths
*/
int Xorriso_end_idx(struct XorrisO *xorriso,
                    int argc, char **argv, int idx, int flag)
{
 int i, warned= 0;

 for(i= idx; i<argc; i++) {
   if(strcmp(argv[i], xorriso->list_delimiter)==0)
 break;
   if(!((flag&1) || warned))
     warned= Xorriso_warn_of_wildcards(xorriso, argv[i], flag&2);
 }
 return(i);
}


/* Returns a vector of strings which belong to an open ended arg list.
   If expansion is enabled, the vector might be allocated, else it is
   a pointer into the argv input vector.
   Thus the release of that memory is an expert task to be done by this
   function only. Use bit8 for that. With bit8 parameter argc MUST be the
   same value as with the call which might have allocated memory.
   @param xorriso The environment object
   @param argc Length of argv
   @param argv The vector with arguments, eventual list_delimiter ("--")
               and then eventual unrelated words
   @param idx  Start index in argv of the argument list
   @param optc Length of the effective possibly expanded option vector
   @param optv The option vector. Maybe a pointer into argv or maybe
               an own allocated vector.
   @param flag bit0= do not warn of wildcards
               bit1= these are disk_paths
               bit2= never expand wildcards
               bit3= do not expand last argument
               bit4= ignore last argument
               bit5= demand exactly one match
               bit6= with bit5 allow 0 matches if pattern is a constant
               bit7= silently tolerate empty argument list
               bit8= free the eventually allocated sub_vector
               bit9= always expand wildcards
*/
int Xorriso_opt_args(struct XorrisO *xorriso, char *cmd,
                     int argc, char **argv, int idx,
                     int *end_idx, int *optc, char ***optv, int flag)
{
 int i, do_expand, nump, was_empty= 0, filec= 0, ret;
 char **filev= NULL, **patterns= NULL;
 off_t mem= 0;

 if(flag&2)
   do_expand= (xorriso->do_disk_pattern==1 && !(flag&4)) || (flag & 512);
 else
   do_expand= (xorriso->do_iso_rr_pattern==1 && !(flag&4)) || (flag & 512);
 if(flag&256) {
   if(*optv<argv || *optv>=argv+argc)
     Sfile_destroy_argv(optc, optv, 0);
   return(1);
 }
 if(idx>=argc) {
   *end_idx= argc;
   *optc= 0;
   *optv= NULL;
   sprintf(xorriso->info_text, "%s : Not enough arguments given", cmd);
   if((flag & 128))
     return(1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 *end_idx= Xorriso_end_idx(xorriso, argc, argv, idx,
                           ((flag&1) || do_expand) | (flag&2));
 if(*end_idx<0)
   return(*end_idx);
 if((flag&16) && (*end_idx)>idx)
   (*end_idx)--;
 *optc= *end_idx - idx;
 *optv= argv+idx;
 if(*optc<=0 || !do_expand)
   return(1);
 patterns= calloc(*optc, sizeof(char *));
 if(patterns==NULL) {
no_memory:;
   sprintf(xorriso->info_text,
           "%s : Cannot allocate enough memory for pattern expansion", cmd);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   {ret= -1; goto ex;}
 }
 nump= 0;
 if(flag&8) {
   was_empty= 1;
   mem+= strlen(argv[idx + *optc - 1])+1+sizeof(char *);
 }
 for(i= 0; i<*optc-!!(flag&8); i++) {
   if(argv[i + idx][0]==0) {
     was_empty++;
     mem+= sizeof(char *); /* as upper limit for size of an empty string */
 continue;
   }
   patterns[nump++]= argv[i + idx];
 }
 if(nump<=0) { /* Only empty texts. May the caller get happy with them. */
   free(patterns);
   return(1);
 }
 if(flag&2)
   ret= Xorriso_expand_disk_pattern(xorriso, nump, patterns, was_empty,
                                    &filec, &filev, &mem, (flag>>5)&3);
 else
   ret= Xorriso_expand_pattern(xorriso, nump, patterns, was_empty, 
                               &filec, &filev, &mem, (flag>>5)&3);
 if(ret<=0)
   {ret= 0; goto ex;}
 for(i= 0; i<was_empty; i++) {
   if(i==was_empty-1 && (flag&8))
     filev[filec++]= strdup(argv[idx + *optc - 1]);
   else
     filev[filec++]= strdup("");
   if(filev[filec-1]==NULL)
     goto no_memory;
 }

#ifdef Xorriso_verbous_pattern_expansioN
{ int l;
 sprintf(xorriso->info_text, "Pattern expansion yields %d items:", filec);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
 l= 0;
 xorriso->info_text[0]= 0;
 for(i= 0; i<filec; i++) {
   l= strlen(xorriso->info_text);
   if(l>0 && l+1+strlen(filev[i])>60) {
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
     xorriso->info_text[0]= 0;
     l= 0;
   }
   sprintf(xorriso->info_text+l, " %s", filev[i]);
 }
 l= strlen(xorriso->info_text);
 if(l>0)
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
}
#endif /* Xorriso_verbous_pattern_expansioN */

 ret= 1;
ex:;
 if(patterns!=NULL)
   free((char *) patterns);
 if(ret<=0) {
   Sfile_destroy_argv(&filec, &filev, 0);
 } else {
   *optc= filec;
   *optv= filev;
 }
 return(ret);
}


int Xorriso_get_problem_status(struct XorrisO *xorriso, char severity[80],
                               int flag)
{
 strcpy(severity, xorriso->problem_status_text);
 return(xorriso->problem_status);
}


int Xorriso_set_problem_status(struct XorrisO *xorriso, char *severity, 
                               int flag)
{
 char *sev_text= "ALL";
 int sev, ret;

#ifdef Xorriso_fetch_with_msg_queueS
 int locked= 0, uret;
 static int complaints= 0, complaint_limit= 5;
#endif

 if(severity[0])
   sev_text= severity;
 ret= Xorriso__text_to_sev(sev_text, &sev, 0);
 if(ret<=0)
   return(0);

#ifdef Xorriso_fetch_with_msg_queueS

 ret= pthread_mutex_lock(&(xorriso->problem_status_lock));
 if(ret != 0) {
   /* Cannot report failure through the failing message output system */
   complaints++;
   if(complaints < complaint_limit)
     fprintf(stderr,
        "xorriso : pthread_mutex_lock() for problem_status returns %d\n",
        ret);
 } else
   locked= 1;

#endif /* Xorriso_fetch_with_msg_queueS */

 xorriso->problem_status= sev;
 strcpy(xorriso->problem_status_text, sev_text);
 if(sev > xorriso->eternal_problem_status) {
   xorriso->eternal_problem_status= sev;
   strcpy(xorriso->eternal_problem_status_text, sev_text);
 }

#ifdef Xorriso_fetch_with_msg_queueS

 if(locked) {
   uret= pthread_mutex_unlock(&(xorriso->problem_status_lock));
   if(uret != 0) {
     /* Cannot report failure through the failing message output system */
     complaints++;
     if(complaints < complaint_limit)
       fprintf(stderr,
            "xorriso : pthread_mutex_unlock() for problem_status returns %d\n",
            uret);
   }
 }

#endif /* Xorriso_fetch_with_msg_queueS */

 return(1);
}


/**
    @param flag       bit0= do not issue own event messages
                      bit1= take xorriso->request_to_abort as reason for abort
    @return           Gives the advice:
                        2= pardon was given, go on
                        1= no problem, go on
                        0= function failed but xorriso would not abort, go on
                       <0= do abort
                           -1 = due to problem_status
                           -2 = due to xorriso->request_to_abort
*/
int Xorriso_eval_problem_status(struct XorrisO *xorriso, int ret, int flag)
{
 static int sev= 0;
 if(sev==0)
   Xorriso__text_to_sev("SORRY", &sev, 0);

 if((flag&2) && xorriso->request_to_abort)
   return(-2);

 Xorriso_process_msg_queues(xorriso, 0);
 if(ret>0 && xorriso->problem_status <= 0)
   return(1);

 if(xorriso->problem_status < xorriso->abort_on_severity &&
    xorriso->problem_status > 0) {
   if(xorriso->problem_status >= sev && !(flag&1)) {
     sprintf(xorriso->info_text,
             "xorriso : NOTE : Tolerated problem event of severity '%s'\n",
             xorriso->problem_status_text);
     Xorriso_info(xorriso, 0);/* submit not as problem event */
   }
   ret= 2;
 } else if(xorriso->problem_status > 0) {
   sprintf(xorriso->info_text,
           "xorriso : aborting : -abort_on '%s' encountered '%s'\n",
           xorriso->abort_on_text, xorriso->problem_status_text);
   if(!(flag&1))
     Xorriso_info(xorriso, 0);/* submit not as problem event */
   ret= -1;
 } else if(ret>0)
   ret= 1;
 else
   ret= 2;
 return(ret);
}


/* @param flag bit0= a non-existing target of multiple sources is a directory
               bit1= all paths except the last one are disk_paths
               bit2= the last path is a disk_path
   @return <=0 is error, 1= leaf file object, 2= directory
*/
int Xorriso_cpmv_args(struct XorrisO *xorriso, char *cmd,
                      int argc, char **argv, int *idx,
                      int *optc, char ***optv, char eff_dest[SfileadrL],
                      int flag)
{
 int destc= 0, is_dir=0, end_idx, ret, i;
 char **destv= NULL;

 end_idx= Xorriso_end_idx(xorriso, argc, argv, *idx,
                          (xorriso->do_iso_rr_pattern==1)|(flag&2));
 if(end_idx - *idx < 2) {
   sprintf(xorriso->info_text, "%s: not enough arguments", cmd);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   {ret= 0; goto ex;}
 }

 ret= Xorriso_opt_args(xorriso, cmd, argc, argv, *idx, &end_idx, optc, optv,
                       1 | (flag&2) | 16); /* ignore last argument */
 if(ret<=0)
   goto ex;
 /* demand one match, or 0 with a constant */
 ret= Xorriso_opt_args(xorriso, cmd, argc, argv, end_idx, &end_idx, &destc,
                       &destv, 1 | ((flag&4)>>1) | 32 | 64);
 if(ret<=0)
   goto ex;

 /* Evaluate target address */
 if(flag&4)
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdx, destv[0], eff_dest,
                                   2|4|16);
 else
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, destv[0], eff_dest,
                                   1);
 if(ret<0)
   {ret= 0; goto ex;}
 if(ret==2 || ((flag&1) && *optc > 1 && ret==0)) {
   is_dir= 1;
 } else if(*optc > 1) {
   if(flag & 2)
     for(i= 0; i<*optc; i++)
       Xorriso_msgs_submit(xorriso, 0, (*optv)[i], 0, "ERRFILE", 0);
   sprintf(xorriso->info_text,
        "%s: more than one origin given, destination is a non-directory: ",
        cmd);
   Text_shellsafe(destv[0], xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 if(ret==0) { /* compute complete eff_dest */
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, destv[0], eff_dest,
                                   2 | (flag&4));
   if(ret<0)
     {ret= 0; goto ex;}
 }

 ret= 1+is_dir;
ex:;
 Xorriso_opt_args(xorriso, cmd, argc, argv, *idx, &end_idx, &destc, &destv,
                  256);
 (*idx)= end_idx;
 return(ret);
}


/* @param flag bit0= with adr_mode sbsector: adr_value is possibly 16 too high
*/
int Xorriso_decode_load_adr(struct XorrisO *xorriso, char *cmd,
                            char *adr_mode, char *adr_value,
                            int *entity_code, char entity_id[81],
                            int flag)
{
 double num;
 int l;

 if(strcmp(adr_mode, "auto")==0)
   *entity_code= 0;
 else if(strcmp(adr_mode, "session")==0)
   *entity_code= 1;
 else if(strcmp(adr_mode, "track")==0)
   *entity_code= 2;
 else if(strcmp(adr_mode, "lba")==0 || strcmp(adr_mode, "sbsector")==0)
   *entity_code= 3 | ((flag&1) << 16);
 else if(strcmp(adr_mode, "volid")==0)
   *entity_code= 4;
 else {
   sprintf(xorriso->info_text, "%s: unknown address mode '%s'", cmd, adr_mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 l= strlen(adr_value);
 if(l==0)
   *entity_code= 0;

 if(*entity_code>=1 && *entity_code<= 3) {
   num= Scanf_io_size(adr_value, 0);
   if(*entity_code==3 &&
      (adr_value[l-1]<'0' || adr_value[l-1]>'9'))
     num/= 2048.0;
   sprintf(entity_id, "%.f", num);
 } else {
   if(strlen(adr_value)>80) {
     sprintf(xorriso->info_text, "%s: address value too long (80 < %d)",
             cmd, (int) strlen(adr_value));
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
   strcpy(entity_id, adr_value);
 }
 return(1);
}

 
int Xorriso_check_name_len(struct XorrisO *xorriso, char *name, int size,
                           char *cmd, int flag)
{
 if((int) strlen(name) >= size) {
   sprintf(xorriso->info_text,
           "Name too long with option %s (%d > %d)", cmd,
           (int) strlen(name), size - 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 return(1);
}
 


/* @return <0 error , >=0 number of skipped dashes
*/
int Xorriso_normalize_command(struct XorrisO *xorriso, char *original_cmd,
                              int argno, char *cmd_data, int sizeof_cmd_data,
                              char **cmd, int flag)
{
 int was_dashed= 0;
 char *dash_pt;

 if((int) strlen(original_cmd) >= sizeof_cmd_data) {
   if(argno>=0)
     sprintf(xorriso->info_text, "Oversized argument #%d (length %d)\n",
             argno, (int) strlen(original_cmd));
   else
     sprintf(xorriso->info_text, "Oversized option (length %d)\n",
             (int) strlen(original_cmd));
   return(-1);
 }
 strcpy(cmd_data, original_cmd);
 *cmd= cmd_data;
 if(strcmp(*cmd, xorriso->list_delimiter)==0)
   return(1);
 while((*cmd)[0]=='-') {
   if((*cmd)[1]==0)
 break;
   was_dashed++;
   (*cmd)++;
 }
 for(dash_pt= *cmd; *dash_pt!=0; dash_pt++)
   if(*dash_pt=='-')
     *dash_pt= '_';
 return(was_dashed);
}


/* @param flag bit0= do not warn of unknown option
   @return <=0 error,
           1=count is valid, 2=dashed unknown, 3=undashed unknown
*/
int Xorriso_count_args(struct XorrisO *xorriso, int argc, char **argv,
                       int *count, int flag)
{
 int ret, was_dashed= 0, i, cmd_data_size= 2 * SfileadrL;
 char *cmd, *cmd_data= NULL;
 static char arg0_commands[][40]= {
    "ban_stdio_write","close_filter_list","commit",
    "device_links","devices","end",
    "for_backup", "help",
    "list_arg_sorting","list_formats","list_speeds",
    "no_rc","print_size","pvd_info","pwd","pwdi","pwdx",
    "read_mkisofsrc","rollback","rollback_end",
    "tell_media_space","toc","version",
    ""
 };
 static char arg1_commands[][40]= {
    "abort_on","acl","add_plainly","application_id","auto_charset",
    "abstract_file",
    "backslash_codes","blank","biblio_file",
    "calm_drive","cd","cdi","cdx","changes_pending","charset",
    "close","close_damaged",
    "commit_eject","compliance","copyright_file",
    "dev","dialog","disk_dev_ino","disk_pattern","displacement",
    "dummy","dvd_obs","early_stdio_test", "eject",
    "iso_rr_pattern","follow","format","fs","gid","grow_blindly","hardlinks",
    "hfsplus","history","indev","in_charset","joliet",
    "list_delimiter","list_extras","list_profiles","local_charset",
    "mark","md5","mount_opts","not_leaf","not_list","not_mgt",
    "options_from_file","osirrox","outdev","out_charset","overwrite",
    "pacifier","padding","path_list","pathspecs","pkt_output",
    "preparer_id","print","print_info","print_mark","prompt",
    "prog","prog_help","publisher","quoted_not_list","quoted_path_list",
    "reassure","report_about","rockridge",
    "rom_toc_scan","rr_reloc_dir","scsi_log",
    "session_log","signal_handling","sleep",
    "speed","split_size","status","status_history_max",
    "stdio_sync","stream_recording","system_id","temp_mem_limit","toc_of",
    "uid","unregister_filter","use_readline","volid","volset_id",
    "write_type","xattr","zisofs",
    ""
 };
 static char arg2_commands[][40]= {
    "assert_volid","boot_image","clone","compare","compare_r","drive_class",
    "data_cache_size",
    "errfile_log","error_behavior","extract","extract_single",
    "jigdo","lns","lnsi","load","logfile",
    "map","map_single","move","msg_op","page","return_with",
    "scdbackup_tag","update","update_r","volume_date",
    ""
 };
 static char arg3_commands[][40]= {
    "append_partition",
    ""
 };
 static char arg4_commands[][40]= {
    "cut_out","extract_cut","mount","mount_cmd","paste_in","session_string",
    ""
 };
 static char argn_commands[][40]= {
    "add","alter_date","alter_date_r","as",
    "check_md5","check_md5_r","check_media","check_media_defaults",
    "chgrp","chgrpi","chgrp_r","chgrp_ri","chmod","chmodi",
    "chmod_r","chmod_ri","chown","chowni","chown_r","chown_ri",
    "compare_l","cp_clone","cp_rax","cp_rx","cpr","cpri", "cpax","cpx",
    "du","dui","dus","dusi","dux","dusx","external_filter","extract_l",
    "file_size_limit","find","findi","finds","findx",
    "getfacl","getfacli","getfacl_r","getfacl_ri",
    "getfattr","getfattri","getfattr_r","getfattr_ri","hide",
    "launch_frontend","ls","lsi","lsl","lsli","lsd","lsdi","lsdl","lsdli",
    "lsx","lslx","lsdx","lsdlx","map_l","mv","mvi","mkdir","mkdiri",
    "not_paths","rm","rmi","rm_r","rm_ri","rmdir","rmdiri","update_l",
    "setfacl","setfacli","setfacl_list","setfacl_listi",
    "setfacl_r","setfacl_ri","setfattr","setfattri",
    "setfattr_list","setfattr_listi","setfattr_r","setfattr_ri",
    "set_filter","set_filter_r","show_stream","show_stream_r",
    ""
 };

 Xorriso_alloc_meM(cmd_data, char, cmd_data_size);

 *count= 0;
 if(argc<=0)
   {ret= -1; goto ex;}
 ret= Xorriso_normalize_command(xorriso, argv[0], -1,
                                cmd_data, cmd_data_size, &cmd, 0);
 if(ret<0)
   goto ex;
 was_dashed= (ret>0);
 if(cmd[0]=='#' || cmd[0]==0 || strcmp(cmd, xorriso->list_delimiter) == 0) {
   /* ignore: comment line , empty option , orphaned list delimiter */
   {ret= 1; goto ex;}
 }
 for(i=0; arg0_commands[i][0]!=0; i++)
   if(strcmp(arg0_commands[i], cmd)==0)
     {ret= 1; goto ex;}
 *count= 1;
 for(i=0; arg1_commands[i][0]!=0; i++)
   if(strcmp(arg1_commands[i], cmd)==0)
     {ret= 1; goto ex;}
 *count= 2;
 for(i=0; arg2_commands[i][0]!=0; i++)
   if(strcmp(arg2_commands[i], cmd)==0)
     {ret= 1; goto ex;}
 *count= 3;
 for(i=0; arg3_commands[i][0]!=0; i++)
   if(strcmp(arg3_commands[i], cmd)==0)
     {ret= 1; goto ex;}
 *count= 4;
 for(i=0; arg4_commands[i][0]!=0; i++)
   if(strcmp(arg4_commands[i], cmd)==0)
     {ret= 1; goto ex;}
 *count= 0;
 for(i=0; argn_commands[i][0]!=0; i++)
   if(strcmp(argn_commands[i], cmd)==0) {
     ret= Xorriso_end_idx(xorriso, argc, argv, 1, 1);
     if(ret<1)
       goto ex;
     *count= ret-1;
     {ret= 1; goto ex;}
   }

 if(!(flag&1)) {
   sprintf(xorriso->info_text, "Unknown option : '%s'", argv[0]);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
 }

 ret= 2 + !was_dashed;
ex:
 Xorriso_free_meM(cmd_data);
 return(ret);
}


/* @param flag bit0= list sorting order rather than looking for argv[idx]
*/
int Xorriso_cmd_sorting_rank(struct XorrisO *xorriso,
                        int argc, char **argv, int idx, int flag)
{
 int ret, i, cmd_data_size= 2 * SfileadrL;
 char *cmd, *cmd_data= NULL;
 static char *commands[]= {

   "* Execution order of program arguments with option -x:",
   "x",

   "* Support for frontend programs via stdin and stdout (1):",
   "prog", "prog_help",

   "* Exception processing:",
   "abort_on", "return_with", "report_about", "signal_handling",
   "error_behavior",

   "* Scripting, dialog and program control features (1):",
   "no_rc", "help", "version", "list_extras", "list_arg_sorting",
   "temp_mem_limit", "backslash_codes",
   "errfile_log", "session_log", "scsi_log",
   "options_from_file", "list_delimiter",
   "print", "print_info", "print_mark", "prompt", "sleep",

   "* Drive and media related inquiry actions (1):",
   "devices", "device_links",
   "mount_opts", "mount_cmd", "session_string",

   "* Influencing the behavior of image loading:",
   "load", "displacement", "drive_class", "assert_volid", "in_charset",
   "auto_charset", "hardlinks", "acl", "xattr", "md5", "for_backup",
   "disk_dev_ino", "rom_toc_scan", "calm_drive", "ban_stdio_write",
   "early_stdio_test", "data_cache_size",

   "* Character sets:",
   "charset", "local_charset",

   "* Aquiring source and target drive:",
   "dev", "indev", "outdev",

   "* Drive and media related inquiry actions (2):",
   "list_profiles", "list_formats", "list_speeds",
   "toc", "toc_of", "pvd_info",

   "* Settings for file insertion:",
   "file_size_limit", "not_mgt", "not_paths", "not_leaf", "not_list",
   "quoted_not_list", "follow", "pathspecs", "overwrite", "split_size",

   "* Navigation in ISO image and disk filesystem (1):",
   "cd", "cdx", "pwd", "pwdx",

   "* Inserting files into ISO image:",
   "disk_pattern", "add_plainly",
   "mkdir", "lns", "add", "path_list", "quoted_path_list",
   "map", "map_single", "map_l", "update", "update_r", "update_l",
   "cut_out", "cpr", 
   "clone", "cp_clone",

   "* Navigation in ISO image and disk filesystem (2):",
   "ls", "lsd", "lsl", "lsdl", "lsx", "lsdx", "lslx", "lsdlx",
   "getfacl", "getfacl_r", "getfattr", "getfattr_r", "du", "dus",
   "dux", "dusx", "findx",
   "compare", "compare_r", "compare_l", "show_stream", "show_stream_r",

   "* File manipulations:",
   "iso_rr_pattern",
   "rm", "rm_r", "rmdir", "move", "mv",
   "chown", "chown_r", "chgrp", "chgrp_r", "chmod", "chmod_r", "setfacl",
   "setfacl_r", "setfacl_list", "setfattr", "setfattr_r", "setfattr_list",
   "alter_date", "alter_date_r", "hide",

   "* Filters for data file content:",
   "external_filter", "unregister_filter", "close_filter_list",
   "set_filter", "set_filter_r", 

   "* Tree traversal command -find:",
   "find",

   "* osirrox ISO-to-disk restore options:",
   "osirrox", "extract", "extract_single", "extract_l", "extract_cut",
   "cpx", "cpax", "cp_rx", "cp_rax", "paste_in",
   "mount",

   "* Settings for result writing:",
   "rockridge", "joliet", "hfsplus","compliance", "rr_reloc_dir",
   "volid", "volset_id", "publisher",
   "application_id", "system_id", "volume_date", "copyright_file",
   "abstract_file", "biblio_file", "preparer_id", "out_charset",
   "read_mkisofsrc",
   "uid", "gid", "zisofs", "speed", "stream_recording", "dvd_obs",
   "stdio_sync", "dummy", "fs", "close", "padding", "write_type",
   "grow_blindly", "pacifier", "scdbackup_tag",

   "* Bootable ISO images:",
   "boot_image", "append_partition",

   "* Jigdo Template Extraction:",
   "jigdo",

   "* Command compatibility emulations:",
   "as",

   "* Scripting, dialog and program control features (2):",
   "history", "status_history_max", "status",

   "* Drive and media related inquiry actions (3):",
   "print_size", "tell_media_space",

   "* Writing the result, drive control:",
   "format", "blank", "close_damaged",
   "rollback", "changes_pending", "commit", "commit_eject",
   "eject",

   "* Evaluation of readability and recovery:",
   "check_media_defaults", "check_media", "check_md5", "check_md5_r",

   "* Support for frontend programs via stdin and stdout (2):",
   "pkt_output", "logfile", "mark", "msg_op",

   "* Dialog mode control:",
   "dialog", "page", "use_readline", "reassure",

   "* Support for frontend programs via stdin and stdout (3):",
   "launch_frontend",

   "* Scripting, dialog and program control features (3):",
   "rollback_end", "end",

   ""
 };

 if(flag & 1) {
   for(i= 0; commands[i][0] !=0; i++) {
     if(commands[i][0] == '*')
       sprintf(xorriso->result_line, "#%s\n", commands[i] + 1);
     else
       sprintf(xorriso->result_line, "-%s\n", commands[i]);
     Xorriso_result(xorriso, 0);
   }
   ret= 1; goto ex;
 }
 if(argc <= 0)
   {ret= -1; goto ex;}

 Xorriso_alloc_meM(cmd_data, char, cmd_data_size);
 ret= Xorriso_normalize_command(xorriso, argv[idx], -1,
                                cmd_data, cmd_data_size, &cmd, 0);
 if(ret < 0)
   goto ex;

 if(cmd[0] == '#' || cmd[0] == 0 ||
    strcmp(cmd, xorriso->list_delimiter) == 0) {
   /* Move to end: comment line , empty option , orphaned list delimiter */
   ret= 0x7fffffff; goto ex;
 }
 for(i= 0; commands[i][0] !=0; i++) {
   if(commands[i][0] == '*') /* headline in command list */
 continue;
   if(strcmp(commands[i], cmd) != 0)
 continue;
   ret= i + 1; goto ex;
 }

 ret= 1;
ex:
 Xorriso_free_meM(cmd_data);
 return(ret);
}


int Xorriso__cmp_cmd_rank(const void *a, const void *b)
{
 int ra, rb;

 ra= ((int *) a)[1];
 rb= ((int *) b)[1];
 if(ra < rb)
   return(-1);
 if(ra > rb)
   return(1);
 ra= ((int *) a)[2];
 rb= ((int *) b)[2];
 if(ra < rb)
   return(-1);
 if(ra > rb)
   return(1);
 return(0);
}


/* @param flag bit0= print command sequence rather than executing it
               bit1= these are command line arguments
                     (for xorriso->argument_emulation)
*/
int Xorriso_exec_args_sorted(struct XorrisO *xorriso,
                             int argc, char **argv, int *idx, int flag)
{
 int cmd_count= 0, ret, i, arg_count, *idx_rank= NULL, cmd_idx;

 /* Count commands and allocate index-rank array */
 for(i= *idx; i < argc; i++) {
   ret= Xorriso_count_args(xorriso, argc - i, argv + i, &arg_count, 1);
   if(ret <= 0)
     goto ex;
   if(ret != 1)
 continue;
   cmd_count++;
   i+= arg_count;
 }
 if(cmd_count <= 0)
   {ret= 1; goto ex;}
 Xorriso_alloc_meM(idx_rank, int, 3 * cmd_count);

 /* Fill index-rank array and sort */
 cmd_count= 0;
 for(i= *idx; i < argc; i++) {
   ret= Xorriso_count_args(xorriso, argc - i, argv + i, &arg_count, 1);
   if(ret <= 0)
     goto ex;
   if(ret != 1)
 continue;
   idx_rank[3 * cmd_count]= i;
   ret= Xorriso_cmd_sorting_rank(xorriso, argc, argv, i, 0);
   if(ret < 0)
     goto ex;
   idx_rank[3 * cmd_count + 1]= ret;
   idx_rank[3 * cmd_count + 2]= cmd_count;
   cmd_count++;
   i+= arg_count;
 }
 qsort(idx_rank, cmd_count, 3 * sizeof(int), Xorriso__cmp_cmd_rank);

 /* Execute or print indice from index-rank array */
 if(flag & 1) {
   sprintf(xorriso->result_line,
           "Automatically determined command sequence:\n");
   Xorriso_result(xorriso, 0);
   xorriso->result_line[0]= 0;
 }
 for(i= 0; i < cmd_count; i++) {
   cmd_idx= idx_rank[3 * i];
   if(flag & 1) {
     if(strlen(xorriso->result_line) + 1 + strlen(argv[cmd_idx]) > 78) {
       strcat(xorriso->result_line, "\n");
       Xorriso_result(xorriso, 0);
       xorriso->result_line[0]= 0;
     }
     sprintf(xorriso->result_line + strlen(xorriso->result_line),
             " %s", argv[cmd_idx]);
   } else {
     ret= Xorriso_interpreter(xorriso, argc, argv, &cmd_idx, 4 | (flag & 2));
     if(ret <= 0 || ret == 3)
       goto ex;
   }
 }
 if(flag & 1) {
   if(strlen(xorriso->result_line) > 0) {
     strcat(xorriso->result_line, "\n");
     Xorriso_result(xorriso, 0);
   }
 } else
   *idx= argc;
 ret= 1;
ex:
 Xorriso_free_meM(idx_rank); 
 return(ret);
}


/* @param flag bit0= recursion
               bit1= these are command line arguments
                     (for xorriso->argument_emulation)
               bit2= Only execute the one command argv[*idx] and advance 
                     *idx to the next command if sucessful. Then return.
*/
int Xorriso_interpreter(struct XorrisO *xorriso,
                        int argc, char **argv, int *idx, int flag)
/*
return:
 <=0 error , 1 = success , 2 = problem event ignored , 3 = end program run
*/
{
 int ret, was_dashed, end_ret, num1, num2, cmd_data_size= 2 * SfileadrL;
 int mem_idx, arg_count, i;
 char *cmd, *original_cmd, *cmd_data= NULL, *arg1, *arg2, *arg3;
 
 Xorriso_alloc_meM(cmd_data, char, cmd_data_size);

 if(xorriso==NULL)
   {ret= 0; goto ex;}
 if(xorriso->is_dialog) {
   xorriso->result_line_counter= xorriso->result_page_counter= 0;
   if(xorriso->result_page_length<0)
     xorriso->result_page_length= -xorriso->result_page_length;
 }

next_command:;
 if(flag&2) {
   ret= 1;
   if(xorriso->argument_emulation==1)
     ret= Xorriso_as_genisofs(xorriso, argc, argv, idx, 0);
   else if(xorriso->argument_emulation==2)
     ret= Xorriso_as_cdrskin(xorriso, argc, argv, idx, 0);
   if(xorriso->argument_emulation>0) {
     xorriso->argument_emulation= 0;
     if(ret<=0)
       goto eval_any_problems;
     if((*idx)>=argc)
       {ret= 1; goto ex;}
   }
   if((xorriso->arrange_args || (flag & 8)) && !(flag & (4 | 16))) {
     ret= Xorriso_exec_args_sorted(xorriso, argc, argv, idx, 0);
     goto ex;
   } 
 }

 ret= Xorriso_count_args(xorriso, argc - *idx, argv + *idx, &arg_count, 1);
 if((ret == 1 || ret == 2) &&
    strcmp(argv[*idx], xorriso->list_delimiter) != 0) {
   sprintf(xorriso->info_text, "Command:    %s", argv[*idx]);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
   for(i= 1; i <= arg_count && *idx + i < argc; i++) {
     sprintf(xorriso->info_text, "Parameter:     %s", argv[*idx + i]);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
   }
   if(*idx + arg_count >= argc) {
     sprintf(xorriso->info_text, "Missing arguments: %d",
             *idx + arg_count + 1 - argc);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
   }
 }

 xorriso->prepended_wd= 0;
 xorriso->request_to_abort= xorriso->request_not_to_ask= 0;
 Xorriso_set_problem_status(xorriso, "", 0);
 if((*idx)<argc)
   original_cmd= cmd= argv[*idx];
 else
   original_cmd= cmd= "";
 if(xorriso->add_plainly==3 && cmd[0] && !xorriso->is_dialog) {
   (*idx)++;
   goto add_plain_argument;
 }
 was_dashed= 0;
 ret= Xorriso_normalize_command(xorriso, original_cmd, -1,
                                cmd_data, cmd_data_size, &cmd, 0);
 if(ret<0)
   goto eval_any_problems;
 was_dashed= ret;

 (*idx)++;
 if((*idx)<argc)
   arg1= argv[(*idx)];
 else
   arg1= "";
 if((*idx)+1<argc)
   arg2= argv[(*idx)+1];
 else
   arg2= "";
 if((*idx) + 2 < argc)
   arg3= argv[(*idx) + 2];
 else
   arg3= "";

 ret= 1;
 if(cmd[0]=='#' || cmd[0]==0) {
   /* ignore comment line and empty option */; 

 } else if(strcmp(cmd,"abort_on")==0) {
   (*idx)++;
   ret= Xorriso_option_abort_on(xorriso, arg1, 0);

 } else if(strcmp(cmd,"abstract_file")==0) { 
   (*idx)++;
   Xorriso_option_abstract_file(xorriso, arg1, 0);

 } else if(strcmp(cmd,"acl")==0) {
   (*idx)++;
   ret= Xorriso_option_acl(xorriso, arg1, 0);

 } else if(strcmp(cmd,"add")==0) {
   ret= Xorriso_option_add(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd,"add_plainly")==0) {
   (*idx)++;
   ret= Xorriso_option_add_plainly(xorriso, arg1, 0);

 } else if(strcmp(cmd,"alter_date")==0 || strcmp(cmd,"alter_date_r")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_alter_date(xorriso, arg1, arg2, argc, argv, idx,
                                  strlen(cmd)>10);

 } else if(strcmp(cmd,"append_partition")==0) {
   (*idx)+= 3;
   ret= Xorriso_option_append_partition(xorriso, arg1, arg2, arg3, 0);

 } else if(strcmp(cmd,"application_id")==0) {
   (*idx)++;
   ret= Xorriso_option_application_id(xorriso, arg1, 0);

 } else if(strcmp(cmd,"as")==0) {
   ret= Xorriso_option_as(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd,"assert_volid")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_assert_volid(xorriso, arg1, arg2, 0);

 } else if(strcmp(cmd,"auto_charset")==0) {
   (*idx)++;
   ret= Xorriso_option_auto_charset(xorriso, arg1, 0);

 } else if(strcmp(cmd,"backslash_codes")==0) {
   (*idx)++;
   ret= Xorriso_option_backslash_codes(xorriso, arg1, 0);

 } else if(strcmp(cmd,"ban_stdio_write")==0) {
   ret= Xorriso_option_ban_stdio_write(xorriso, 0);

 } else if(strcmp(cmd,"biblio_file")==0) { 
   (*idx)++;
   Xorriso_option_biblio_file(xorriso, arg1, 0);

 } else if(strcmp(cmd,"blank")==0) {
   (*idx)++;
   ret= Xorriso_option_blank(xorriso, arg1, 0);

 } else if(strcmp(cmd,"boot_image")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_boot_image(xorriso, arg1, arg2, 0);

 } else if(strcmp(cmd,"calm_drive")==0) {
   (*idx)++;
   ret= Xorriso_option_calm_drive(xorriso, arg1, 0);

 } else if(strcmp(cmd,"cd")==0 || strcmp(cmd,"cdi")==0) {
   (*idx)++;
   ret= Xorriso_option_cdi(xorriso, arg1, 0);

 } else if(strcmp(cmd,"cdx")==0) {
   (*idx)++;
   ret= Xorriso_option_cdx(xorriso, arg1, 0);

 } else if(strcmp(cmd, "changes_pending")==0) {
   (*idx)++;
   ret= Xorriso_option_changes_pending(xorriso, arg1, 0);

 } else if(strcmp(cmd,"charset")==0) {
   (*idx)++;
   ret= Xorriso_option_charset(xorriso, arg1, 3);

 } else if(strcmp(cmd,"check_md5")==0) {
   ret= Xorriso_option_check_md5(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd,"check_md5_r")==0) {
   ret= Xorriso_option_check_md5(xorriso, argc, argv, idx, 8);

 } else if(strcmp(cmd,"check_media")==0) {
   ret= Xorriso_option_check_media(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd,"check_media_defaults")==0) {
   ret= Xorriso_option_check_media_defaults(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd,"chgrp")==0 || strcmp(cmd,"chgrpi")==0) {
   (*idx)+= 1;
   ret= Xorriso_option_chgrpi(xorriso, arg1, argc, argv, idx, 0);   

 } else if(strcmp(cmd,"chgrp_r")==0 || strcmp(cmd,"chgrp_ri")==0) {
   (*idx)+= 1;
   ret= Xorriso_option_chgrpi(xorriso, arg1, argc, argv, idx, 1);   

 } else if(strcmp(cmd,"chmod")==0 || strcmp(cmd,"chmodi")==0) {
   (*idx)+= 1;
   ret= Xorriso_option_chmodi(xorriso, arg1, argc, argv, idx, 0);   

 } else if(strcmp(cmd,"chmod_r")==0 || strcmp(cmd,"chmod_ri")==0) {
   (*idx)+= 1;
   ret= Xorriso_option_chmodi(xorriso, arg1, argc, argv, idx, 1);   

 } else if(strcmp(cmd,"chown_r")==0 || strcmp(cmd,"chown_ri")==0) {
   (*idx)+= 1;
   ret= Xorriso_option_chowni(xorriso, arg1, argc, argv, idx, 1);   

 } else if(strcmp(cmd,"chown")==0 || strcmp(cmd,"chowni")==0) {
   (*idx)+= 1;
   ret= Xorriso_option_chowni(xorriso, arg1, argc, argv, idx, 0);   

 } else if(strcmp(cmd,"clone")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_clone(xorriso, arg1, arg2, 1);

 } else if(strcmp(cmd,"close")==0) {
   (*idx)++;
   ret= Xorriso_option_close(xorriso, arg1, 0);

 } else if(strcmp(cmd,"close_damaged")==0) {
   (*idx)++;
   ret= Xorriso_option_close_damaged(xorriso, arg1, 0);

 } else if(strcmp(cmd,"close_filter_list")==0) {
   ret= Xorriso_option_close_filter_list(xorriso, 0);

 } else if(strcmp(cmd,"commit")==0) {
   ret= Xorriso_option_commit(xorriso, 0);

 } else if(strcmp(cmd,"commit_eject")==0) {
   (*idx)++;
   ret= Xorriso_option_commit_eject(xorriso, arg1, 0);

 } else if(strcmp(cmd,"compare")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_compare(xorriso, arg1, arg2, 1);

 } else if(strcmp(cmd,"compare_l")==0) {
   ret= Xorriso_option_map_l(xorriso, argc, argv, idx, 1<<8);

 } else if(strcmp(cmd,"compare_r")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_compare(xorriso, arg1, arg2, 1|8);

 } else if(strcmp(cmd,"compliance")==0) {
   (*idx)++;
   Xorriso_option_compliance(xorriso, arg1, 0);

 } else if(strcmp(cmd,"copyright_file")==0) { 
   (*idx)++;
   Xorriso_option_copyright_file(xorriso, arg1, 0);

 } else if(strcmp(cmd,"cp_clone") == 0) {
   ret= Xorriso_option_cp_clone(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd,"cp_rx")==0 || strcmp(cmd,"cp_rax")==0) {
   ret= Xorriso_option_cpx(xorriso, argc, argv, idx,
                           1|((strcmp(cmd,"cp_rax")==0)<<1));

 } else if(strcmp(cmd,"cpr")==0 || strcmp(cmd,"cpri")==0) {
   ret= Xorriso_option_cpri(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd,"cpx")==0 || strcmp(cmd,"cpax")==0) {
   ret= Xorriso_option_cpx(xorriso, argc, argv, idx,
                           (strcmp(cmd,"cpax")==0)<<1);

 } else if(strcmp(cmd,"cut_out")==0) {
   (*idx)+= 4;
   if((*idx)>argc) {
     sprintf(xorriso->info_text,
            "-cut_out: Not enough arguments. Needed are: disk_path start count so_rr_path");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0;
   } else
     ret= Xorriso_option_cut_out(xorriso, arg1, arg2,
                                 argv[(*idx)-2], argv[(*idx)-1], 0);

 } else if(strcmp(cmd,"data_cache_size")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_data_cache_size(xorriso, arg1, arg2, 0);

 } else if(strcmp(cmd,"dev")==0) {
   (*idx)++;
   ret= Xorriso_option_dev(xorriso, arg1, 3);

 } else if(strcmp(cmd,"device_links")==0) {
   ret= Xorriso_option_devices(xorriso, 1);

 } else if(strcmp(cmd,"devices")==0) {
   ret= Xorriso_option_devices(xorriso, 0);

 } else if(strcmp(cmd,"dialog")==0) {
   (*idx)++;
   ret= Xorriso_option_dialog(xorriso, arg1, 0);

 } else if(strcmp(cmd,"disk_dev_ino")==0) {
   (*idx)++;
   ret= Xorriso_option_disk_dev_ino(xorriso, arg1, 0);

 } else if(strcmp(cmd,"displacement")==0) {
   (*idx)++;
   ret= Xorriso_option_displacement(xorriso, arg1, 0);

 } else if(strcmp(cmd,"disk_pattern")==0) {
   (*idx)++;
   ret= Xorriso_option_disk_pattern(xorriso, arg1, 0);

 } else if(strcmp(cmd,"drive_class")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_drive_class(xorriso, arg1, arg2, 0);

 } else if(strcmp(cmd,"du")==0   || strcmp(cmd,"dui")==0 ||
           strcmp(cmd,"dus")==0 || strcmp(cmd,"dusi")==0) {
   ret= Xorriso_option_lsi(xorriso, argc, argv, idx, (cmd[2]!='s')|4);

 } else if(strcmp(cmd,"dummy")==0) {
   (*idx)++;
   ret= Xorriso_option_dummy(xorriso, arg1, 0);

 } else if(strcmp(cmd,"dvd_obs")==0) {
   (*idx)++;
   ret= Xorriso_option_dvd_obs(xorriso, arg1, 0);

 } else if(strcmp(cmd,"dux")==0 || strcmp(cmd,"dusx")==0) {
   ret= Xorriso_option_lsx(xorriso, argc, argv, idx, (cmd[2]!='s')|4);

 } else if(strcmp(cmd,"early_stdio_test")==0) {
   (*idx)++;
   ret= Xorriso_option_early_stdio_test(xorriso, arg1, 0);

 } else if(strcmp(cmd,"eject")==0) {
   (*idx)++;
   ret= Xorriso_option_eject(xorriso, arg1, 0);

 } else if(strcmp(cmd,"end")==0) {
   end_ret= Xorriso_option_end(xorriso, 0);
   ret= Xorriso_eval_problem_status(xorriso, ret, 0);
   if(ret<0)
     goto ex;
   if(end_ret!=2)
     {ret= 3; goto ex;}

 } else if(strcmp(cmd,"errfile_log")==0) {
   (*idx)+= 2;
    ret= Xorriso_option_errfile_log(xorriso, arg1, arg2, 0);

 } else if(strcmp(cmd,"error_behavior")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_error_behavior(xorriso, arg1, arg2, 0);

 } else if(strcmp(cmd,"external_filter")==0) {
   ret= Xorriso_option_external_filter(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd,"extract")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_extract(xorriso, arg1, arg2, 0);

 } else if(strcmp(cmd,"extract_cut")==0) {
   (*idx)+= 4;
   if((*idx)>argc) {
     sprintf(xorriso->info_text,
            "-extract_cut: Not enough arguments. Needed are: disk_path start count so_rr_path");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0;
   } else
     ret= Xorriso_option_extract_cut(xorriso, arg1, arg2,
                                     argv[(*idx)-2], argv[(*idx)-1], 0);

 } else if(strcmp(cmd,"extract_l")==0) {
   ret= Xorriso_option_map_l(xorriso, argc, argv, idx, 3<<8);

 } else if(strcmp(cmd,"extract_single")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_extract(xorriso, arg1, arg2, 32);

 } else if(strcmp(cmd,"file_size_limit")==0) {
   ret= Xorriso_option_file_size_limit(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd,"find")==0 || strcmp(cmd,"findi")==0) {
   ret= Xorriso_option_find(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd,"findx")==0) {
   ret= Xorriso_option_find(xorriso, argc, argv, idx, 1);

 } else if(strcmp(cmd,"follow")==0) {
   (*idx)++;
   ret= Xorriso_option_follow(xorriso, arg1, 0);

 } else if(strcmp(cmd,"for_backup")==0) {
   Xorriso_option_hardlinks(xorriso, "on", 0);
   Xorriso_option_acl(xorriso, "on", 0);
   Xorriso_option_xattr(xorriso, "on", 0);
   Xorriso_option_md5(xorriso, "on", 0);
   ret= 1;

 } else if(strcmp(cmd,"format")==0) {
   (*idx)++;
   ret= Xorriso_option_blank(xorriso, arg1, 1);

 } else if(strcmp(cmd,"fs")==0) {
   (*idx)++;
   ret= Xorriso_option_fs(xorriso, arg1, 0);

 } else if(strcmp(cmd,"getfacl")==0 || strcmp(cmd,"getfacli")==0) {
   ret= Xorriso_option_getfacli(xorriso, argc, argv, idx, 0);   

 } else if(strcmp(cmd,"getfacl_r")==0 || strcmp(cmd,"getfacl_ri")==0) {
   ret= Xorriso_option_getfacli(xorriso, argc, argv, idx, 1);   

 } else if(strcmp(cmd,"getfattr")==0 || strcmp(cmd,"getfattri")==0) {
   ret= Xorriso_option_getfacli(xorriso, argc, argv, idx, 2);   

 } else if(strcmp(cmd,"getfattr_r")==0 || strcmp(cmd,"getfattr_ri")==0) {
   ret= Xorriso_option_getfacli(xorriso, argc, argv, idx, 1 | 2);   

 } else if(strcmp(cmd,"gid")==0) {
   (*idx)++;
   ret= Xorriso_option_gid(xorriso,arg1,0);

 } else if(strcmp(cmd,"grow_blindly")==0) {
   (*idx)++;
   ret= Xorriso_option_grow_blindly(xorriso,arg1,0);

 } else if(strcmp(cmd,"hardlinks")==0) {
   (*idx)++;
   ret= Xorriso_option_hardlinks(xorriso, arg1, 0);

 } else if(strcmp(cmd,"hfsplus")==0) {
   (*idx)++;
   ret= Xorriso_option_hfsplus(xorriso, arg1, 0);

 } else if(strcmp(cmd,"help")==0) {
   Xorriso_option_help(xorriso,0);

 } else if(strcmp(cmd,"hide")==0) {
   (*idx)+= 1;
   ret= Xorriso_option_hide(xorriso, arg1, argc, argv, idx, 1);   

 } else if(strcmp(cmd,"history")==0) {
   /* add to readline history */
   (*idx)++;
   ret= Xorriso_option_history(xorriso, arg1, 0);

 } else if(strcmp(cmd,"indev")==0) {
   (*idx)++;
   ret= Xorriso_option_dev(xorriso, arg1, 1);

 } else if(strcmp(cmd,"in_charset")==0) {
   (*idx)++;
   ret= Xorriso_option_charset(xorriso, arg1, 1);

 } else if(strcmp(cmd,"iso_rr_pattern")==0) {
   (*idx)++;
   ret= Xorriso_option_iso_rr_pattern(xorriso, arg1, 0);

 } else if(strcmp(cmd,"jigdo")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_jigdo(xorriso, arg1, arg2, 0);

 } else if(strcmp(cmd,"joliet")==0) {
   (*idx)++;
   ret= Xorriso_option_joliet(xorriso, arg1, 0);

 } else if(strcmp(cmd, "launch_frontend") == 0) {
   ret= Xorriso_option_launch_frontend(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd, "list_arg_sorting") == 0) {
   ret= Xorriso_option_list_arg_sorting(xorriso, 0);

 } else if(strcmp(cmd, "list_delimiter") == 0) {
   (*idx)++;
   ret= Xorriso_option_list_delimiter(xorriso, arg1, 0);

 } else if(strcmp(cmd, "list_extras") == 0) {
   (*idx)++;
   ret= Xorriso_option_list_extras(xorriso, arg1, 0);

 } else if(strcmp(cmd,"list_formats")==0) {
   ret= Xorriso_option_list_formats(xorriso, 0);

 } else if(strcmp(cmd,"list_profiles")==0) {
   (*idx)++;
   ret= Xorriso_option_list_profiles(xorriso, arg1, 0);

 } else if(strcmp(cmd,"list_speeds")==0) {
   ret= Xorriso_option_list_speeds(xorriso, 0);

 } else if(strcmp(cmd, "lns") == 0 || strcmp(cmd, "lnsi") == 0) {
   (*idx)+= 2;
   ret= Xorriso_option_lnsi(xorriso, arg1, arg2, 0);

 } else if(strcmp(cmd,"load")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_load(xorriso, arg1, arg2, 0);
   
 } else if(strcmp(cmd,"local_charset")==0) {
   (*idx)++;
   ret= Xorriso_option_charset(xorriso, arg1, 4);

 } else if(strcmp(cmd,"logfile")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_logfile(xorriso, arg1, arg2, 0);
   
 } else if(strcmp(cmd,"ls")==0 || strcmp(cmd,"lsi")==0 ||
           strcmp(cmd,"lsl")==0 || strcmp(cmd,"lsli")==0) {
   ret= Xorriso_option_lsi(xorriso, argc, argv, idx, (cmd[2]=='l'));

 } else if(strcmp(cmd,"lsd")==0 || strcmp(cmd,"lsdi")==0 ||
           strcmp(cmd,"lsdl")==0 || strcmp(cmd,"lsdli")==0) {
   ret= Xorriso_option_lsi(xorriso, argc, argv, idx, (cmd[3]=='l')|8);

 } else if(strcmp(cmd,"lsdx")==0 || strcmp(cmd,"lsdlx")==0) {
   ret= Xorriso_option_lsx(xorriso, argc, argv, idx, (cmd[3]=='l')|8);

 } else if(strcmp(cmd,"lsx")==0 || strcmp(cmd,"lslx")==0) {
   ret= Xorriso_option_lsx(xorriso, argc, argv, idx, (cmd[2]=='l'));

 } else if(strcmp(cmd,"map")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_map(xorriso, arg1, arg2, 0);

 } else if(strcmp(cmd,"map_l")==0) {
   ret= Xorriso_option_map_l(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd,"map_single")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_map(xorriso, arg1, arg2, 32);

 } else if(strcmp(cmd,"mark")==0) {
   (*idx)++;
   ret= Xorriso_option_mark(xorriso, arg1, 0);

 } else if(strcmp(cmd, "md5")==0) {
   (*idx)++;
   ret= Xorriso_option_md5(xorriso, arg1, 0);

 } else if(strcmp(cmd, "mount") == 0 || strcmp(cmd, "mount_cmd") == 0) {
   (*idx)+= 4;
   if((*idx)>argc) {
     sprintf(xorriso->info_text,
         "-%s: Not enough arguments. Needed are: device entity id command",
         cmd);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0;
   } else
     ret= Xorriso_option_mount(xorriso, arg1, arg2,
                               argv[(*idx)-2], argv[(*idx)-1],
                               (strcmp(cmd, "mount_cmd") == 0));

 } else if(strcmp(cmd, "mount_opts")==0) {
   (*idx)++;
   ret= Xorriso_option_mount_opts(xorriso, arg1, 0);

 } else if(strcmp(cmd, "move")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_move(xorriso, arg1, arg2, 0);

 } else if(strcmp(cmd,"msg_op") == 0) {
   (*idx)+= 2;
   ret= Xorriso_option_msg_op(xorriso, arg1, arg2, 0);

 } else if(strcmp(cmd,"mv")==0 || strcmp(cmd,"mvi")==0) {
   ret= Xorriso_option_mvi(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd,"mkdir")==0 || strcmp(cmd,"mkdiri")==0) {
   ret= Xorriso_option_mkdiri(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd,"no_rc")==0) {
   ret= Xorriso_option_no_rc(xorriso, 0);

 } else if(strcmp(cmd,"not_leaf")==0) {
   (*idx)++;
   ret= Xorriso_option_not_leaf(xorriso, arg1, 0);

 } else if(strcmp(cmd,"not_list")==0) {
   (*idx)++;
   ret= Xorriso_option_not_list(xorriso, arg1, 0);

 } else if(strcmp(cmd,"not_mgt")==0) {
   (*idx)++;
   ret= Xorriso_option_not_mgt(xorriso, arg1, 0);

 } else if(strcmp(cmd,"not_paths")==0) {
   ret= Xorriso_option_not_paths(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd,"options_from_file")==0) {
   (*idx)++;
   ret= Xorriso_option_options_from_file(xorriso,arg1,0);
   if(ret==3)
     goto ex;

 } else if(strcmp(cmd,"osirrox")==0) {
   (*idx)++;
   ret= Xorriso_option_osirrox(xorriso,arg1,0);

 } else if(strcmp(cmd,"outdev")==0) {
   (*idx)++;
   ret= Xorriso_option_dev(xorriso, arg1, 2);

 } else if(strcmp(cmd,"out_charset")==0) {
   (*idx)++;
   ret= Xorriso_option_charset(xorriso, arg1, 2);

 } else if(strcmp(cmd,"overwrite")==0) {
   (*idx)++;
   ret= Xorriso_option_overwrite(xorriso,arg1,0);

 } else if(strcmp(cmd,"pacifier")==0) {
   (*idx)++;
   ret= Xorriso_option_pacifier(xorriso, arg1, 0);

 } else if(strcmp(cmd,"padding")==0) {
   (*idx)++;
   ret= Xorriso_option_padding(xorriso, arg1, 0);

 } else if(strcmp(cmd,"page")==0) {
   (*idx)+= 2;
   num1= num2= 0;
   sscanf(arg1,"%d",&num1);
   sscanf(arg2,"%d",&num2);
   if(num1<0)
     num1= 0;
   if(arg1[0]==0)
     num1= 16;
   if(num2<=0)
     num2= 80;
   ret= Xorriso_option_page(xorriso, num1, num2, 0);

 } else if(strcmp(cmd,"paste_in")==0) {
   (*idx)+= 4;
   if((*idx)>argc) {
     sprintf(xorriso->info_text,
            "-paste_in: Not enough arguments. Needed are: disk_path start count so_rr_path");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0;
   } else
     ret= Xorriso_option_paste_in(xorriso, arg1, arg2,
                                 argv[(*idx)-2], argv[(*idx)-1], 0);

 } else if(strcmp(cmd,"path-list")==0 || strcmp(cmd,"path_list")==0) {
   (*idx)++;
   ret= Xorriso_option_path_list(xorriso, arg1, 0);

 } else if(strcmp(cmd,"pathspecs")==0) {
   (*idx)++;
   ret= Xorriso_option_pathspecs(xorriso, arg1, 0);

 } else if(strcmp(cmd,"pkt_output")==0) {
   (*idx)++;
   ret=  Xorriso_option_pkt_output(xorriso, arg1, 0);

 } else if(strcmp(cmd,"preparer_id")==0) {
   (*idx)++;
   ret= Xorriso_option_preparer_id(xorriso, arg1, 0);

 } else if(strcmp(cmd,"print")==0) {
   (*idx)++;
   ret= Xorriso_option_print(xorriso, arg1, 0);

 } else if(strcmp(cmd,"print_info")==0) {
   (*idx)++;
   ret= Xorriso_option_print(xorriso, arg1, 1);

 } else if(strcmp(cmd,"print_mark")==0) {
   (*idx)++;
   ret= Xorriso_option_print(xorriso, arg1, 2);

 } else if(strcmp(cmd,"print_size")==0) {
   Xorriso_option_print_size(xorriso, 0);

 } else if(strcmp(cmd,"prompt")==0) {
   (*idx)++;
   ret= Xorriso_option_prompt(xorriso, arg1, 0);

 } else if(strcmp(cmd,"prog")==0) {
   (*idx)++;
   ret= Xorriso_option_prog(xorriso, arg1, 0);

 } else if(strcmp(cmd,"publisher")==0) { 
   (*idx)++;
   Xorriso_option_publisher(xorriso, arg1, 0);

 } else if(strcmp(cmd,"pvd_info")==0) { 
   Xorriso_option_pvd_info(xorriso, 0);

 } else if(strcmp(cmd,"pwd")==0 || strcmp(cmd,"pwdi")==0) {
   Xorriso_option_pwdi(xorriso, 0);

 } else if(strcmp(cmd,"pwdx")==0) { 
   Xorriso_option_pwdx(xorriso, 0);

 } else if(strcmp(cmd,"quoted_not_list")==0) {
   (*idx)++;
   ret= Xorriso_option_not_list(xorriso, arg1, 1);

 } else if(strcmp(cmd,"quoted_path_list")==0) {
   (*idx)++;
   ret= Xorriso_option_path_list(xorriso, arg1, 1);

 } else if(strcmp(cmd,"read_mkisofsrc")==0) {
   ret= Xorriso_option_read_mkisofsrc(xorriso, 0);

 } else if(strcmp(cmd,"reassure")==0) {
   (*idx)++;
   ret= Xorriso_option_reassure(xorriso, arg1, 0);

 } else if(strcmp(cmd,"report_about")==0) {
   (*idx)++;
   ret= Xorriso_option_report_about(xorriso, arg1, 0);

 } else if(strcmp(cmd,"return_with")==0) {
   (*idx)+= 2;
   num2= 0;
   sscanf(arg2,"%d",&num2);
   ret= Xorriso_option_return_with(xorriso, arg1, num2, 0);

 } else if(strcmp(cmd,"rm")==0 || strcmp(cmd,"rmi")==0) {
   ret= Xorriso_option_rmi(xorriso, argc, argv, idx, 0);

 } else if(strcmp(cmd,"rm_r")==0 || strcmp(cmd,"rm_ri")==0) {
   ret= Xorriso_option_rmi(xorriso, argc, argv, idx, 1);

 } else if(strcmp(cmd,"rmdir")==0 || strcmp(cmd,"rmdiri")==0) {
   ret= Xorriso_option_rmi(xorriso, argc, argv, idx, 2);

 } else if(strcmp(cmd, "rockridge") == 0) {
   (*idx)++;
   ret= Xorriso_option_rockridge(xorriso, arg1, 0);

 } else if(strcmp(cmd,"rollback")==0) {
   ret= Xorriso_option_rollback(xorriso, 0);

 } else if(strcmp(cmd,"rollback_end")==0) {
   end_ret= Xorriso_option_end(xorriso, 1);
   ret= Xorriso_eval_problem_status(xorriso, ret, 0);
   if(ret<0)
     goto ex;
   if(end_ret!=2)
     {ret= 3; goto ex;}

 } else if(strcmp(cmd,"rom_toc_scan")==0) {
   (*idx)++;
   Xorriso_option_rom_toc_scan(xorriso, arg1, 0);

 } else if(strcmp(cmd,"rr_reloc_dir")==0) {
   (*idx)++;
   Xorriso_option_rr_reloc_dir(xorriso, arg1, 0);

 } else if(strcmp(cmd,"scdbackup_tag")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_scdbackup_tag(xorriso, arg1, arg2, 0);
 
 } else if(strcmp(cmd, "scsi_log") == 0) {
   (*idx)++;
   ret= Xorriso_option_scsi_log(xorriso, arg1, 0);

 } else if(strcmp(cmd,"session_log")==0) {
   (*idx)++;
   ret= Xorriso_option_session_log(xorriso, arg1, 0);
 
 } else if(strcmp(cmd, "session_string") == 0) {
   (*idx)+= 4;
   if((*idx)>argc) {
     sprintf(xorriso->info_text,
         "-%s: Not enough arguments. Needed are: device entity id command",
         cmd);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0;
   } else
     ret= Xorriso_option_mount(xorriso, arg1, arg2,
                               argv[(*idx)-2], argv[(*idx)-1], 2);

 } else if(strcmp(cmd,"setfacl")==0 || strcmp(cmd,"setfacli")==0) {
   (*idx)+= 1;
   ret= Xorriso_option_setfacli(xorriso, arg1, argc, argv, idx, 0);   

 } else if(strcmp(cmd,"setfacl_list")==0 || strcmp(cmd,"setfacl_listi")==0) {
   (*idx)+= 1;
   ret= Xorriso_option_setfacl_listi(xorriso, arg1, 0);   

 } else if(strcmp(cmd,"setfacl_r")==0 || strcmp(cmd,"setfacl_ri")==0) {
   (*idx)+= 1;
   ret= Xorriso_option_setfacli(xorriso, arg1, argc, argv, idx, 1);   

 } else if(strcmp(cmd,"setfattr")==0 || strcmp(cmd,"setfattri")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_setfattri(xorriso, arg1, arg2, argc, argv, idx, 0);   

 } else if(strcmp(cmd,"setfattr_list")==0 || strcmp(cmd,"setfattr_listi")==0) {
   (*idx)+= 1;
   ret= Xorriso_option_setfattr_listi(xorriso, arg1, 0);   

 } else if(strcmp(cmd,"setfattr_r")==0 || strcmp(cmd,"setfattr_ri")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_setfattri(xorriso, arg1, arg2, argc, argv, idx, 1);   

 } else if(strcmp(cmd,"set_filter")==0 || strcmp(cmd,"set_filter_r")==0) {
   (*idx)+= 1;
   ret= Xorriso_option_set_filter(xorriso, arg1, argc, argv, idx,
                                  strcmp(cmd,"set_filter_r")==0);

 } else if(strcmp(cmd,"show_stream")==0 || strcmp(cmd,"show_stream_r")==0) {
   ret= Xorriso_option_set_filter(xorriso, "", argc, argv, idx,
                                  (strcmp(cmd,"show_stream_r")==0) | 2 | 4);

 } else if(strcmp(cmd,"signal_handling")==0) {
   (*idx)++;
   ret= Xorriso_option_signal_handling(xorriso, arg1, 0);

 } else if(strcmp(cmd,"sleep")==0) {
   (*idx)++;
   ret= Xorriso_option_sleep(xorriso, arg1, 0);

 } else if(strcmp(cmd,"speed")==0) {
   (*idx)++;
   ret= Xorriso_option_speed(xorriso, arg1, 0);

 } else if(strcmp(cmd,"split_size")==0) {
   (*idx)++;
   ret= Xorriso_option_split_size(xorriso, arg1, 0);

 } else if(strcmp(cmd,"status")==0) {
   (*idx)++;
   ret= Xorriso_option_status(xorriso, arg1, 0);

 } else if(strcmp(cmd,"status_history_max")==0) {
   (*idx)++;
   sscanf(arg1,"%d",&num1);
   ret= Xorriso_option_status_history_max(xorriso, num1, 0);

 } else if(strcmp(cmd,"stdio_sync")==0) {
   (*idx)++;
   ret= Xorriso_option_stdio_sync(xorriso, arg1, 0);

 } else if(strcmp(cmd,"stream_recording")==0) {
   (*idx)++;
   ret= Xorriso_option_stream_recording(xorriso, arg1, 0);

 } else if(strcmp(cmd,"system_id")==0) {
   (*idx)++;
   ret= Xorriso_option_system_id(xorriso, arg1, 0);

 } else if(strcmp(cmd,"tell_media_space")==0) {
   Xorriso_option_tell_media_space(xorriso, 0);

 } else if(strcmp(cmd,"temp_mem_limit")==0) {
   (*idx)++;
   ret= Xorriso_option_temp_mem_limit(xorriso, arg1, 0);

 } else if(strcmp(cmd,"test")==0) { /* This option does not exist. */
   /* install temporary test code here */;

if (0) {
/* Test setup for for Xorriso_push_outlists() et.al. */
/* Test setup for Xorriso_parse_line */
   int stack_handle = -1, line_count= 0;
   struct Xorriso_lsT *result_list, *info_list;
   int Xorriso_process_msg_lists(struct XorrisO *xorriso,
                                     struct Xorriso_lsT *result_list,
                                     struct Xorriso_lsT *info_list,
                                     int *line_count, int flag);

   (*idx)++;
   if(strcmp(arg1, "push") == 0) {
     ret= Xorriso_push_outlists(xorriso, &stack_handle, 3);
     fprintf(stderr, "xorriso -test: push = %d, handle = %d\n",
             ret, stack_handle);
   } else if(strcmp(arg1, "pull") == 0) {
     ret= Xorriso_pull_outlists(xorriso, -1, &result_list, &info_list, 0);
     fprintf(stderr, "xorriso -test: pull = %d\n", ret);
     if(ret > 0) {
       ret= Xorriso_process_msg_lists(xorriso, result_list, info_list,
                                      &line_count, 0);
       fprintf(stderr,
          "xorriso -test: Xorriso_process_msg_lists() = %d, line_count = %d\n",
          ret, line_count);
     }
   } else if(strcmp(arg1, "fetch") == 0) {
     ret= Xorriso_fetch_outlists(xorriso, -1, &result_list, &info_list, 0);
     fprintf(stderr, "xorriso -test: fetch = %d\n", ret);
     if(ret > 0) {
       ret= Xorriso_process_msg_lists(xorriso, result_list, info_list,
                                      &line_count, 0);
       fprintf(stderr,
          "xorriso -test: Xorriso_process_msg_lists() = %d, line_count = %d\n",
          ret, line_count);
     }
   } else if(strcmp(arg1, "peek") == 0) {
     ret= Xorriso_peek_outlists(xorriso, -1, 0, 0);
     fprintf(stderr, "xorriso -test: peek = %d\n", ret);
   } else if(strcmp(arg1, "sleep_peek") == 0) {
     usleep(1000000);
     ret= Xorriso_peek_outlists(xorriso, -1, 0, 0);
     fprintf(stderr, "xorriso -test: sleep_peek = %d\n", ret);
   } else if(strcmp(arg1, "peek_loop") == 0) {
     ret= Xorriso_peek_outlists(xorriso, -1, 3, 4);
     fprintf(stderr, "xorriso -test: peek_loop = %d\n", ret);
   } else if(strcmp(arg1, "start") == 0) {
     ret= Xorriso_start_msg_watcher(xorriso, NULL, NULL, NULL, NULL, 0);
     fprintf(stderr, "xorriso -test: Xorriso_start_msg_watcher() = %d\n", ret);
   } else if(strcmp(arg1, "stop") == 0) {
     ret= Xorriso_stop_msg_watcher(xorriso, 0);
     fprintf(stderr, "xorriso -test: Xorriso_stop_msg_watcher() = %d\n", ret);

   } else if(strcmp(arg1, "help") == 0) {
     fprintf(stderr, "-test [mode] [arguments]\n");
     fprintf(stderr, "   push\n");
     fprintf(stderr, "     perform Xorriso_push_outlists()\n");
     fprintf(stderr, "   pull\n");
     fprintf(stderr, "     perform Xorriso_pull_outlists() and show messages\n");
     fprintf(stderr, "   fetch\n");
     fprintf(stderr, "     perform Xorriso_fetch_outlists() and show\n");
     fprintf(stderr, "   peek\n");
     fprintf(stderr, "     perform Xorriso_peek_outlists()\n");
     fprintf(stderr, "   sleep_peek\n");
     fprintf(stderr, "     sleep 1 s and perform Xorriso_peek_outlists()\n");
     fprintf(stderr, "   peek_loop\n");
     fprintf(stderr, "     wait for up to 3s in Xorriso_peek_outlists()\n");
     fprintf(stderr, "     for return value 0 or -1\n");
     fprintf(stderr, "   start\n");
     fprintf(stderr, "     perform Xorriso_start_msg_watcher()\n");
     fprintf(stderr, "   stop\n");
     fprintf(stderr, "     perform Xorriso_stop_msg_watcher()\n");
   } else {
     fprintf(stderr, "xorriso -test: unknwon mode: %s\n", arg1);
   }
   ret= 0;
}

 } else if(strcmp(cmd,"toc")==0) {
   Xorriso_option_toc(xorriso, 0);

 } else if(strcmp(cmd,"toc_of")==0) {
   (*idx)++;
   Xorriso_option_toc_of(xorriso, arg1, 0);

 } else if(strcmp(cmd,"uid")==0) {
   (*idx)++;
   ret= Xorriso_option_uid(xorriso,arg1,0);

 } else if(strcmp(cmd,"unregister_filter")==0) {
   (*idx)++;
   ret= Xorriso_option_unregister_filter(xorriso, arg1, 0);

 } else if(strcmp(cmd,"update")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_update(xorriso, arg1, arg2, 1);

 } else if(strcmp(cmd,"update_l")==0) {
   ret= Xorriso_option_map_l(xorriso, argc, argv, idx, 2<<8);

 } else if(strcmp(cmd,"update_r")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_update(xorriso, arg1, arg2, 1|8);

 } else if(strcmp(cmd,"use_readline")==0) {
   (*idx)++;
   ret= Xorriso_option_use_readline(xorriso, arg1, 0);

 } else if(strcmp(cmd,"version")==0){
   ret= Xorriso_option_version(xorriso, 0);

 } else if(strcmp(cmd,"volset_id")==0) {
   (*idx)++;
   ret= Xorriso_option_volset_id(xorriso, arg1, 0);

 } else if(strcmp(cmd,"volid")==0) {
   (*idx)++;
   ret= Xorriso_option_volid(xorriso,arg1,0);

 } else if(strcmp(cmd,"volume_date")==0) {
   (*idx)+= 2;
   ret= Xorriso_option_volume_date(xorriso, arg1, arg2, 0);

 } else if(strcmp(cmd,"write_type")==0) {
   (*idx)++;
   ret= Xorriso_option_write_type(xorriso, arg1, 0);

 } else if(strcmp(cmd, "x") == 0) {
   /* only in effect in Xorriso_prescan_args() */;

 } else if(strcmp(cmd,"xattr")==0) {
   (*idx)++;
   ret= Xorriso_option_xattr(xorriso, arg1, 0);

 } else if(strcmp(cmd,"zisofs")==0) {
   (*idx)++;
   ret= Xorriso_option_zisofs(xorriso, arg1, 0);

 } else if(strcmp(cmd, xorriso->list_delimiter)==0){
   /* tis ok */;

 } else if(was_dashed) {
   if(xorriso->add_plainly>1)
     goto add_plain_argument;
unknown_option:;
   sprintf(xorriso->info_text, "Not a known command:  '%s'\n",
           original_cmd);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto eval_any_problems;}

 } else {
   if(xorriso->add_plainly<=0)
     goto unknown_option;
add_plain_argument:;
   mem_idx= *idx;
   (*idx)--;
   ret= Xorriso_option_add(xorriso, (*idx)+1, argv, idx, 0);
   (*idx)= mem_idx;

 }

eval_any_problems:
 ret= Xorriso_eval_problem_status(xorriso, ret, 0);
 if(ret<0)
   goto ex;

 if(*idx < argc && !(flag & 4))
   goto next_command;

ex:;
 fflush(stdout);
 Xorriso_free_meM(cmd_data);
 return(ret);
}


int Xorriso_parse_line(struct XorrisO *xorriso, char *line,
                       char *prefix, char *separators, int max_words,
                       int *argc, char ***argv, int flag)
{
 int ret, bsl_mode;
 char *to_parse, *progname= "";

 if(xorriso == NULL && (flag & (32 | 64))) {
   ret= -2; goto ex;
 }

 *argc= 0;
 *argv= NULL;

 to_parse= line;
 if((flag & 1) || xorriso == NULL)
   bsl_mode= (flag >> 1) & 3;
 else
   bsl_mode= xorriso->bsl_interpretation & 3;
 if(prefix[0]) {
   if(strncmp(line, prefix, strlen(prefix)) == 0) {
     to_parse= line + strlen(prefix);
   } else {
     ret= 2; goto ex;
   }
 }

 if(xorriso != NULL)
   progname= xorriso->progname;
 ret= Sfile_sep_make_argv(progname, to_parse, separators,
                          max_words, argc, argv,
                          (!(flag & 32)) | 4 | (bsl_mode << 5));
 if(ret < 0) {
   if(xorriso != NULL)
     Xorriso_msgs_submit(xorriso, 0,
        "Severe lack of resources during command line parsing", 0, "FATAL", 0);
   ret= -1; goto ex;
 }
 if(ret == 0) {
   if((flag & 64) && xorriso != NULL) {
     sprintf(xorriso->info_text, "Incomplete quotation in %s line: %s",
             (flag & 32) ? "command" : "parsed", to_parse);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   }
   goto ex;
 }
 ret= 1;
ex:;
 if(ret <= 0)
   Sfile_sep_make_argv("", "", "", 0, argc, argv, 2); /* release memory */
 return(ret);
}


void Xorriso__dispose_words(int *argc, char ***argv)
{
 Sfile_make_argv("", "", argc, argv, 2); /* release memory */
}


int Xorriso_execute_option(struct XorrisO *xorriso, char *line, int flag)
/*
 bit0-bit15 are forwarded to Xorriso_interpreter
 
 bit16= no pageing of info lines
 bit17= print === bar even if xorriso->found<0
*/
{
 int ret,argc= 0, idx= 1;
 char **argv= NULL;
 double tdiff;
 struct timeval tv;
 struct timezone tz;

 gettimeofday(&tv,&tz);
 Xorriso_reset_counters(xorriso,0);
 xorriso->idle_time= 0.0;
 tdiff= tv.tv_sec+(1.e-6*(double) tv.tv_usec);

 ret= Xorriso_parse_line(xorriso, line, "", "", 0, &argc, &argv, 32 | 64);
 if(ret <= 0)
   goto ex;

 if(argc<2)
   {ret= 1; goto ex;}
 if(argv[1][0]=='#')
   {ret= 1; goto ex;}

 ret= Xorriso_interpreter(xorriso, argc, argv, &idx, flag&0xffff);
 if(ret<0)
   goto ex;
 gettimeofday(&tv,&tz);
 tdiff= tv.tv_sec+(1.e-6*(double) tv.tv_usec)-tdiff-xorriso->idle_time;
 if(tdiff<0.001)
   tdiff= 0.001;
 if(xorriso->error_count>0) {
   sprintf(xorriso->info_text,
           "----------------------------- %7.f errors encountered\n",
           xorriso->error_count);
   Xorriso_info(xorriso,!(flag&(1<<16)));
 }

 /* ??? >>> print elapsed time  tdiff ? */;

 if((flag&(1<<17)) && !xorriso->bar_is_fresh) {
   sprintf(xorriso->info_text,"============================\n");
   Xorriso_info(xorriso,0);
   xorriso->bar_is_fresh= 1;
 }
 Xorriso_reset_counters(xorriso,0);
ex:;
 Sfile_make_argv("", "", &argc, &argv, 2); /* release memory */
 return(ret);
}


int Xorriso_dialog(struct XorrisO *xorriso, int flag)
{
 int ret, line_size= 2 * SfileadrL;
 char *line= NULL;

 Xorriso_alloc_meM(line, char, line_size);

 if(!xorriso->dialog)
   {ret= 1; goto ex;}
 if(xorriso->abort_on_is_default)
   Xorriso_option_abort_on(xorriso, "NEVER", 0);
 xorriso->is_dialog= 1;
 while(1) {
   if(xorriso->pending_option[0]!=0) {
     Xorriso_mark(xorriso,0);
     strcpy(line,xorriso->pending_option);
     xorriso->pending_option[0]= 0;
   } else {
     if(!xorriso->bar_is_fresh) {
       sprintf(xorriso->info_text,"============================\n");
       Xorriso_info(xorriso,0);
       xorriso->bar_is_fresh= 1;
     }
     sprintf(xorriso->info_text,"enter option and arguments :\n");
     Xorriso_info(xorriso,0);
     Xorriso_mark(xorriso,0);
     ret= Xorriso_dialog_input(xorriso,line, line_size, 4);
     if(ret<=0)
 break;
   }
   sprintf(xorriso->info_text,
           "==============================================================\n");
   Xorriso_info(xorriso,0);

   ret= Xorriso_execute_option(xorriso,line,1<<17);
   if(ret<0)
     goto ex;
   if(ret==3)
     goto ex;
   xorriso->did_something_useful= 1;
   xorriso->no_volset_present= 0; /* Re-enable "No ISO image present." */
 }
 ret= 1;
ex:;
 xorriso->is_dialog= 0;
 Xorriso_free_meM(line);
 return(ret);
}


int Xorriso_prescan_args(struct XorrisO *xorriso, int argc, char **argv,
                         int flag)
/*
 bit0= do not interpret argv[1]
 bit1= complain about inknown arguments
*/
/*
 return:
  <0  error
   0  end program
   1  ok, go on
*/
{
 int i, ret, was_dashed, num2, arg_count;
 int advice, mem_add_plainly, error_seen= 0;
 int was_report_about= 0, was_abort_on= 0, was_return_with= 0;
 int was_signal_handling= 0, was_scsi_log= 0, cmd_data_size= 5 * SfileadrL;
 char *cmd, *original_cmd, *cmd_data= NULL, *arg1, *arg2;
 char mem_list_delimiter[81];

 strcpy(mem_list_delimiter, xorriso->list_delimiter);
 mem_add_plainly= xorriso->add_plainly;

 Xorriso_alloc_meM(cmd_data, char, cmd_data_size);

 for(i=1+(flag&1);i<argc;i++) {
   original_cmd= cmd= argv[i];
   was_dashed= 0;

   was_dashed= Xorriso_normalize_command(xorriso, original_cmd, i,
                                         cmd_data, cmd_data_size, &cmd, 0);
   if(was_dashed<0)
     {ret= -1; goto ex;}

   arg1= "";
   if(i+1<argc)
     arg1= argv[i+1];
   arg2= "";
   if(i+2<argc)
     arg2= argv[i+2];
   if(i>1)
     xorriso->did_something_useful= 1;
   if(i==1 && argc==2) {
     if(strcmp(cmd,"prog_help")==0) {
       i++;
       Xorriso_option_prog_help(xorriso,arg1,0);
       xorriso->did_something_useful= 1;
       {ret= 0; goto ex;}
     } else if(strcmp(cmd,"help")==0) {
       if(xorriso->argument_emulation == 1) {
         Xorriso_genisofs_help(xorriso, 0);
       } else if(xorriso->argument_emulation == 2) {
         Xorriso_cdrskin_help(xorriso, 0);
       } else {
         Xorriso_option_help(xorriso,0);
       }
       xorriso->did_something_useful= 1;
       {ret= 0; goto ex;}
     }
   } else if(i==1 && strcmp(cmd,"no_rc")==0) {
     ret= Xorriso_option_no_rc(xorriso, 0);
     if(ret<=0)
       error_seen= 1;
     {ret= 1; goto ex;}
   } else if(xorriso->argument_emulation == 1) { /* mkisofs emulation */
     if(xorriso->dev_fd_1 < 0)
       goto protect_stdout;
     {ret= 1; goto ex;}

   } else if(xorriso->argument_emulation == 2) { /* cdrecord emulation */
     if(xorriso->dev_fd_1 < 0)
       if(Xorriso_cdrskin_uses_stdout(xorriso, argc - 1 - (flag & 1),
                                      argv + 1 + (flag & 1), 0))
         goto protect_stdout;
     {ret= 1; goto ex;}

   } else if((strcmp(cmd,"dev")==0 || strcmp(cmd,"outdev")==0 ||
                                      strcmp(cmd,"indev")==0) &&
             (strcmp(arg1,"stdio:/dev/fd/1")==0 || strcmp(arg1,"-")==0) &&
             xorriso->dev_fd_1<0) {
     /* Detach fd 1 from externally perceived stdout and attach it to stderr.
        Keep dev_fd_1 connected to external stdout. dev_fd_1 is to be used when
        "stdio:/dev/fd/1" is interpreted as drive address.
     */
protect_stdout:;
     ret= Xorriso_protect_stdout(xorriso, 0);
     if(ret == 1) {
       sprintf(xorriso->info_text,
             "Encountered  -  or  stdio:/dev/fd/1  as possible write target.");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
       sprintf(xorriso->info_text,
             "Redirecting nearly all text message output to stderr.");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
       sprintf(xorriso->info_text, "Disabling use of libreadline.");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
     }
     if(xorriso->argument_emulation >= 1 && xorriso->argument_emulation <=2)
       {ret= 1; goto ex;}

   } else if(strcmp(cmd,"abort_on")==0 && was_dashed == 1) {
     i++;
     if(!was_abort_on)
       Xorriso_option_abort_on(xorriso, arg1, 0);
     was_abort_on= 1;

   } else if(strcmp(cmd,"report_about")==0 && was_dashed == 1) {
     i++;
     if(!was_report_about)
       Xorriso_option_report_about(xorriso, arg1, 0);
     was_report_about= 1;

   } else if(strcmp(cmd,"return_with")==0 && was_dashed == 1) {
     i+= 2;
     num2= 0;
     sscanf(arg2,"%d",&num2);
     if(!was_return_with)
       Xorriso_option_return_with(xorriso, arg1, num2, 0);
     was_return_with= 1;

   } else if(strcmp(cmd,"as")==0 && was_dashed == 1) {
     ret= Xorriso_count_args(xorriso, argc - i, argv + i, &arg_count, 1);
     if(ret == 1) {
       i+= arg_count;

       if((strcmp(arg1, "cdrecord")==0 || strcmp(arg1, "wodim")==0 ||
           strcmp(arg1, "cdrskin")==0 || strcmp(arg1, "xorrecord")==0) &&
          xorriso->dev_fd_1 < 0)
         if(Xorriso_cdrskin_uses_stdout(xorriso, arg_count - 1,
                                        argv + i - arg_count + 2, 0))
           goto protect_stdout;
     }
     if(was_dashed == 1) {
       if((strcmp(arg1, "mkisofs")==0 || strcmp(arg1, "genisoimage")==0 ||
           strcmp(arg1, "genisofs")==0 || strcmp(arg1, "xorrisofs")==0) &&
          xorriso->dev_fd_1 < 0)
         goto protect_stdout;
     }

   } else if(strcmp(cmd, "list_delimiter") == 0) {
     /* Needed for interpreting other args. Gets reset after prescan. */
     i++;
     ret= Xorriso_option_list_delimiter(xorriso, arg1, 0);
     if(ret <= 0)
       error_seen= 1;

   } else if(strcmp(cmd, "add_plainly") == 0) {
     i++;
     ret= Xorriso_option_add_plainly(xorriso, arg1, 0);
     if(ret <= 0)
       error_seen= 1;
     if(xorriso->add_plainly == 3) {
       /* All further arguments count as pathspecs */
       {ret= 1; goto ex;}
     }
   } else if(strcmp(cmd, "scsi_log") == 0 && was_dashed == 1) {
     i++;
     if(!was_scsi_log)
        Xorriso_option_scsi_log(xorriso, arg1, 0);
     was_scsi_log= 1;

   } else if(strcmp(cmd, "signal_handling") == 0 && was_dashed == 1) {
     i++;
     if(!was_signal_handling)
       Xorriso_option_signal_handling(xorriso, arg1, 1); /* no install */
     was_signal_handling= 1;

   } else if(strcmp(original_cmd, "-x") == 0) {
     xorriso->arrange_args= 1;

   } else {
     ret= Xorriso_count_args(xorriso, argc - i, argv + i, &arg_count, 1);
     if(ret == 1) {
       i+= arg_count;
     } else if((flag & 2) && ((was_dashed && xorriso->add_plainly <= 1) ||
                              xorriso->add_plainly <= 0)) {
       sprintf(xorriso->info_text, "Not a known command:  '%s'\n",
               original_cmd);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       error_seen= 1;
     }
   }
 }
 ret= 1;
ex:;
 strcpy(xorriso->list_delimiter, mem_list_delimiter);
 xorriso->add_plainly= mem_add_plainly;
 Xorriso_free_meM(cmd_data);
 if(error_seen && ret > 0) {
   advice= Xorriso_eval_problem_status(xorriso, 0, 0);
   if(advice < 0)
     ret= -1;
 }
 return(ret);
}


int Xorriso_read_as_mkisofsrc(struct XorrisO *xorriso, char *path, int flag)
{
 int ret, linecount= 0;
 FILE *fp= NULL;
 char *sret, *line= NULL, *cpt, *wpt;

 Xorriso_alloc_meM(line, char, SfileadrL);

 ret= Xorriso_afile_fopen(xorriso, path, "rb", &fp, 1 | 2);
 if(ret <= 0)
   {ret= 0; goto ex;}
 while(1) {
   sret= Sfile_fgets_n(line, SfileadrL - 1, fp, 0);
   if(sret == NULL) {
     if(ferror(fp))
       {ret= 0; goto ex;}
 break;
   }
   linecount++;
   
   /* Interpret line */
   if(line[0] == 0 || line[0] == '#')
 continue;
   cpt= strchr(line, '=');
   if(cpt == NULL) {

     /* >>> ??? complain ? abort reading ? */;

 continue;
   }
   *cpt= 0;
   /* Names are not case sensitive */
   for(wpt= line; wpt < cpt; wpt++)
     if(*wpt >= 'a' && *wpt <= 'z')
       *wpt= toupper(*wpt);
   /* Remove trailing whitespace from name */
   for(wpt= cpt - 1; wpt >= line ; wpt--)
     if(*wpt == ' ' || *wpt == '\t')
       *wpt= 0;
     else
   break;
   /* Remove trailing whitespace from value */
   for(wpt= cpt + 1 + strlen(cpt + 1) - 1; wpt >= cpt; wpt--)
      if(*wpt == ' ' || *wpt == '\t')
       *wpt= 0;
     else
   break;
   /* Remove leading whitespace from value */
   for(cpt++; *cpt == ' ' || *cpt == '\t'; cpt++);
   
   if(strcmp(line, "APPI") == 0) {
     ret= Xorriso_option_application_id(xorriso, cpt, 0);
   } else if(strcmp(line, "COPY") == 0) {
     ret= Xorriso_option_copyright_file(xorriso, cpt, 0);
   } else if(strcmp(line, "ABST") == 0) {
     ret= Xorriso_option_abstract_file(xorriso, cpt, 0);
   } else if(strcmp(line, "BIBL") == 0) {
     ret= Xorriso_option_biblio_file(xorriso, cpt, 0);
   } else if(strcmp(line, "PREP") == 0) {
     /* Not planned to be implemented. Preparer is xorriso. */
     ret= 1;
   } else if(strcmp(line, "PUBL") == 0) {
     ret= Xorriso_option_publisher(xorriso, cpt, 0);
   } else if(strcmp(line, "SYSI") == 0) {
     ret= Xorriso_option_system_id(xorriso, cpt, 0);
   } else if(strcmp(line, "VOLI") == 0) {
     ret= Xorriso_option_volid(xorriso, cpt, 1);
   } else if(strcmp(line, "VOLS") == 0) {
     ret= Xorriso_option_volset_id(xorriso, cpt, 0);
   } else if(strcmp(line, "HFS_TYPE") == 0) {
     /* Not planned to be implemented */
     ret= 1;
   } else if(strcmp(line, "HFS_CREATOR") == 0) {
     /* Not planned to be implemented */
     ret= 1;
   } else {

     /* >>> ??? complain ? abort reading ? */;

   }
 }
 xorriso->mkisofsrc_done= 1;
 ret= 1;
ex:
 if(fp != NULL)
   fclose(fp);
 Xorriso_free_meM(line);
 return(ret);
}


/* ./.mkisofsrc , getenv("MKISOFSRC") ,
   $HOME/.mkisofsrc , $(basename $0)/.mkisofsrc
 */
int Xorriso_read_mkisofsrc(struct XorrisO *xorriso, int flag)
{
 char *path= NULL, *cpt;
 int ret;

 Xorriso_alloc_meM(path, char, SfileadrL);

 ret= Xorriso_read_as_mkisofsrc(xorriso, "./.mkisofsrc", 0);
 if(ret > 0)
   goto ex;
 cpt= getenv("MKISOFSRC");
 if(cpt != NULL) {
   strncpy(path, cpt, SfileadrL - 1);
   path[SfileadrL - 1]= 0;
   ret= Xorriso_read_as_mkisofsrc(xorriso, path, 0);
   if(ret > 0)
     goto ex;
 }
 cpt= getenv("HOME");
 if(cpt != NULL) {
   strncpy(path, cpt, SfileadrL - 1 - 11);
   path[SfileadrL - 1 - 11]= 0;
   strcat(path, "/.mkisofsrc");
   ret= Xorriso_read_as_mkisofsrc(xorriso, path, 0);
   if(ret > 0)
     goto ex;
 }
 strcpy(path, xorriso->progname);
 cpt= strrchr(path, '/');
 if(cpt != NULL) {
   strcpy(cpt + 1, ".mkisofsrc");
   ret= Xorriso_read_as_mkisofsrc(xorriso, path, 0);
   if(ret > 0)
     goto ex;
 }
 /* no .mkisofsrc file found */
 ret= 2;
ex:;
 Xorriso_free_meM(path);
 return(ret);
}


int Xorriso_read_rc(struct XorrisO *xorriso, int flag)
{
 int ret,i,was_failure= 0,fret;

 if(xorriso->no_rc)
   return(1);
 i= xorriso->rc_filename_count-1;
 Sfile_home_adr_s(".xorrisorc", xorriso->rc_filenames[i],
                  sizeof(xorriso->rc_filenames[i]),0);
 for(i=0;i<xorriso->rc_filename_count;i++) {
   ret= Sfile_type(xorriso->rc_filenames[i],1|8);
   if(ret!=1)
 continue;
   ret= Xorriso_option_options_from_file(xorriso,xorriso->rc_filenames[i],0);
   if(ret>1)
     return(ret);
   if(ret==1)
 continue; /* regular bottom of loop */
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1);
   if(fret>=0)
 continue;
   return(ret);
 }
 if(xorriso->argument_emulation == 1 && !xorriso->mkisofsrc_done) {
   ret= Xorriso_read_mkisofsrc(xorriso, 0);
   if(ret <= 0)
     was_failure= 1;
 }
 return(!was_failure);
}


int Xorriso_make_return_value(struct XorrisO *xorriso, int flag)
{
 int exit_value= 0;

 if(xorriso->eternal_problem_status >= xorriso->return_with_severity)
   exit_value= xorriso->return_with_value;
 if(exit_value) {
   sprintf(xorriso->info_text,
          "-return_with %s %d triggered by problem severity %s",
          xorriso->return_with_text, exit_value,
          xorriso->eternal_problem_status_text);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
 }
 return(exit_value);
}


int Xorriso_program_arg_bsl(struct XorrisO *xorriso, int argc, char ***argv,
                            int flag)
{
 int ret;

 if(!(xorriso->bsl_interpretation & 16))
   return(1);
 ret= Sfile_argv_bsl(argc, argv, 0);
 return(ret);
}


/* @param flag bit0= prepend wd only if name does not begin by '/'
               bit1= normalize image path
               bit2= prepend wd (automatically done if wd[0]!=0)
               bit3= (with bit1) this is an address in the disk world
*/
int Xorriso_make_abs_adr(struct XorrisO *xorriso, char *wd, char *name,
                             char adr[], int flag)
{
 char *norm_adr= NULL;
 int ret;

 Xorriso_alloc_meM(norm_adr, char, SfileadrL);

 if((wd[0]!=0 || (flag&4)) && !((flag&1) && name[0]=='/')) {
   if(strlen(wd)+1>=SfileadrL)
     goto much_too_long;
   strcpy(adr, wd);
   if(name[0])
     if(Sfile_add_to_path(adr, name, 0)<=0) {
much_too_long:;
       Xorriso_much_too_long(xorriso, (int) (strlen(adr)+strlen(name)+1), 2);
       {ret= 0; goto ex;}
     }
 } else {
   if(strlen(name)+1>=SfileadrL)
     goto much_too_long;
   strcpy(adr, name);
 }
 if(flag&2) {
   ret= Xorriso_normalize_img_path(xorriso, "", adr, norm_adr,
                                   1|2|((flag&8)>>1));
   if(ret<=0)
     goto ex;
   if(norm_adr[0]==0)
     strcpy(norm_adr, "/");
   strcpy(adr, norm_adr);
 }
 ret= 1;
ex:;
 Xorriso_free_meM(norm_adr);
 return(ret);
}


/* @param flag bit0= do not complain in case of error, but set info_text */
int Xorriso_convert_datestring(struct XorrisO *xorriso, char *cmd,
                               char *time_type, char *timestring,
                               int *t_type, time_t *t, int flag)
{
 int ret;

 if(strcmp(time_type, "a")==0)
   (*t_type)|= 1;
 else if(strcmp(time_type, "m")==0)
   (*t_type)|= 4;
 else if(strcmp(time_type, "b")==0)
   (*t_type)|= 5;
 else {
   sprintf(xorriso->info_text, "%s: Unrecognized type '%s'", cmd, time_type);
   if(!(flag & 1))
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 ret= Decode_timestring(timestring, t, 0);
 if(ret<=0) {
   sprintf(xorriso->info_text, "%s: Cannot decode timestring '%s'", cmd,
           timestring);
   if(!(flag & 1))
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 sprintf(xorriso->info_text, "Understanding timestring '%s' as:  %s",
         timestring, ctime(t));
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
 return(1);
}


/* @param flag bit1= do not report memory usage as DEBUG
*/ 
int Xorriso_check_temp_mem_limit(struct XorrisO *xorriso, off_t mem, int flag)
{
 char mem_text[80], limit_text[80];
 
 Sfile_scale((double) mem, mem_text,5,1e4,0);
 if(!(flag&2)) {
   sprintf(xorriso->info_text,
           "Temporary memory needed for result sorting : %s", mem_text);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
 }
 if(mem > xorriso->temp_mem_limit) {
   Sfile_scale((double) xorriso->temp_mem_limit,limit_text,5,1e4,1);
   sprintf(xorriso->info_text,
       "Cannot sort. List of matching files exceeds -temp_mem_limit (%s > %s)",
           mem_text, limit_text);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
   return(0);
 }
 return(1);
}


/*
  @param flag bit0= use env_path to find the desired program
return:
 <=0 : error
 1   : done
*/
int Xorriso_execv(struct XorrisO *xorriso, char *cmd, char *env_path,
                  int *status, int flag)
{
 int ret, argc= 0;
 char **argv= NULL, *pathlist= NULL, *cpt, *npt, *prog= NULL;
 pid_t child_pid;
 struct stat stbuf;

 Xorriso_alloc_meM(prog, char, 5 * SfileadrL);

 wait3(NULL,WNOHANG,NULL); /* just to remove any old dead child */

 ret= Sfile_make_argv("", cmd, &argc, &argv, 1|4|128);
 if(ret <= 0)
   goto ex;
 if(argc < 1)
   {ret= 0; goto ex;}

 strcpy(prog, argv[0]);
 if((flag & 1) && strchr(argv[0], '/') == NULL) {
     if(env_path == NULL)
       env_path= "/bin:/sbin";
     else if(env_path[0] == 0)
       env_path= "/bin:/sbin";
     if(Sregex_string(&pathlist, env_path, 0) <= 0)
       {ret= -1; goto ex;}
     for(cpt= npt= pathlist; npt != NULL; cpt= npt + 1) {
       npt= strchr(cpt, ':');
       if(npt != NULL)
         *npt= 0;
       if(strlen(cpt) + strlen(argv[0]) + 1 >= SfileadrL)
         {ret= -1; goto ex;}
       sprintf(prog, "%s/%s", cpt, argv[0]);
       ret= stat(prog, &stbuf);
       if(ret != -1)
     break;
       prog[0]= 0;
     }
     if(prog[0] == 0) {
       sprintf(xorriso->info_text, "Cannot find external program ");
       Text_shellsafe(argv[0], xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
     }
   }

 child_pid= fork();
 if(child_pid==-1) 
   return(-1);

 if(child_pid==0) {
                     /* this is the child process */

   sprintf(xorriso->info_text, "Executing external program ");
   Text_shellsafe(prog, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);

   Xorriso_destroy(&xorriso, 0); /* reduce memory foot print */

   execv(prog, argv); /* should never come back */
   fprintf(stderr,"--- execution of shell command failed:\n");
   fprintf(stderr,"    %s\n",cmd);
   exit(127);
 }


         /* this is the original process waiting for child to exit */
 do {
   /* try to read and print the reply */;
   ret= waitpid(child_pid,status,WNOHANG);
   if(ret==-1) {
     if(errno!=EINTR)
       ret= 0; goto ex;
   } else if(ret==0) {

#ifdef NIX

     /* >>> An interrupt key would be nice. */

     if((flag&4)) {
       ret= Asqueue_event_is_pending(agent->queue,0,0);
       if(ret>0) {
         Asagent_stderr(agent,"--- shell command interrupted",1);
         kill(child_pid,SIGTERM);
         ret= 2; goto ex;
       }
     }
#endif /* NIX */

 continue;
   } else {
 break;
   }
 } while(1);
 ret= 1;
ex:
 Sfile_make_argv("", "", &argc, &argv, 2);
 Sregex_string(&pathlist, NULL, 0);
 Xorriso_free_meM(prog);
 return(ret);
}


/* @param flag bit0= path is a command parameter
*/
int Xorriso_path_is_excluded(struct XorrisO *xorriso, char *path, int flag)
{
 int ret;

 if(!(xorriso->disk_excl_mode&1)) /* exclusion is off */
   return(0);
 if((flag&1) && !(xorriso->disk_excl_mode&2)) /* params are exempted */
   return(0);
 ret= Exclusions_match(xorriso->disk_exclusions, path,
                       !!(xorriso->disk_excl_mode&4));
 if(ret<0) {
   sprintf(xorriso->info_text,
           "Error during disk file exclusion decision");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
 }
 if(ret>0 && (flag&1)) {
   sprintf(xorriso->info_text, "Disk path parameter excluded by %s : ",
          (ret==1 ? "-not_paths" : "-not_leaf"));
   Text_shellsafe(path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
 }
 return(ret);
}


int Xorriso_path_is_hidden(struct XorrisO *xorriso, char *path, int flag)
{
 int ret, hide_attrs= 0;

 ret= Exclusions_match(xorriso->iso_rr_hidings, path, 0);
 if(ret < 0) {
failure:;
   sprintf(xorriso->info_text, "Error during disk file hiding decision");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   return(-1);
 }
 if(ret > 0)
   hide_attrs|= 1;
 ret= Exclusions_match(xorriso->joliet_hidings, path, 0);
 if(ret < 0)
   goto failure;
 if(ret > 0)
   hide_attrs|= 2;
 ret= Exclusions_match(xorriso->hfsplus_hidings, path, 0);
 if(ret < 0)
   goto failure;
 if(ret > 0)
   hide_attrs|= 4;
 return(hide_attrs);
}


/* Normalize ACL and sort apart "access" ACL from "default" ACL.
 */
int Xorriso_normalize_acl_text(struct XorrisO *xorriso, char *in_text,
                     char **access_acl_text, char **default_acl_text, int flag)
{
 int ret, access_count= 0, default_count= 0, pass, is_default, line_len;
 int was_error= 0, line_count= 0, perms;
 char *acl_text= NULL, *cpt, *npt, *access_wpt= NULL, *default_wpt= NULL; 
 char *dpt= NULL, *ddpt= NULL, **wpt, *ppt;

 if(in_text[0] == 0 || strcmp(in_text, "clear") == 0 ||
    strcmp(in_text, "--remove-all") == 0) {
   *access_acl_text= *default_acl_text= NULL;
   return(1);
 } else if (strcmp(in_text, "--remove-default") == 0) {

   /* >>> protect Access-ACL and delete Default-ACL */;

   /* <<< */
   return(0);
   
 }

 acl_text= strdup(in_text);
 if(acl_text == NULL) {
   Xorriso_no_malloc_memory(xorriso, NULL, 0);
   {ret= -1; goto ex;}
 }

 /* From comma to newline */
 for(cpt= strchr(acl_text, ','); cpt != NULL; cpt= strchr(cpt + 1, ','))
   *cpt= '\n';

 /* Normalize to long text form
    and sort apart "access" ACL from "default" ACL */;
 for(pass= 0; pass < 2; pass++) {
   line_count= 0;
   for(cpt= acl_text; cpt != NULL; cpt= npt) {
     line_count++;
     npt= strchr(cpt, '\n');
     if(npt != NULL)
       npt++;
     if(*cpt == '#' || *cpt == '\n' || *cpt == 0)
   continue;

     is_default= 0;
     wpt= &access_wpt;
     if(*cpt == 'd') {
       is_default= 1;
       if(pass == 1)
         wpt= &default_wpt;
       cpt= strchr(cpt, ':');
       if(cpt == NULL) {
         was_error= line_count;
   continue;
       }
       cpt++;
     } 

     line_len= 0;
     dpt= strchr(cpt, ':');
     if(dpt != NULL)
       ddpt= strchr(dpt + 1, ':');
     if(dpt == NULL || ddpt == NULL) {
       was_error= line_count;
   continue;
     }
     if(*cpt == 'u') {
       if(pass == 0) {
         line_len+= 5;
         line_len+= ddpt - dpt;
       } else {
         strcpy(*wpt, "user:");
         strncpy(*wpt + 5, dpt + 1, ddpt - dpt);
         (*wpt)+= 5 + (ddpt - dpt);
       }
     } else if(*cpt == 'g') {
       if(pass == 0) {
         line_len+= 6 + (ddpt - dpt);
       } else {
         strcpy(*wpt, "group:");
         strncpy(*wpt + 6, dpt + 1, ddpt - dpt);
         (*wpt)+= 6 + (ddpt - dpt);
       }
     } else if(*cpt == 'o') {
       if(pass == 0) {
         if(ddpt - dpt > 1) {
           was_error= line_count;
   continue;
         }
         line_len+= 6 + (ddpt - dpt);
       } else {
         strcpy(*wpt, "other:");
         strncpy(*wpt + 6, dpt + 1, ddpt - dpt);
         (*wpt)+= 6 + (ddpt - dpt);
       }
     } else if(*cpt == 'm') {
       if(pass == 0) {
         if(ddpt - dpt > 1) {
           was_error= line_count;
   continue;
         }
         line_len+= 5 + (ddpt - dpt);
       } else {
         strcpy(*wpt, "mask:");
         strncpy(*wpt + 5, dpt + 1, ddpt - dpt);
         (*wpt)+= 5 + (ddpt - dpt);
       }

     } else {
       /* Unknown tag type */
       was_error= line_count;
   continue;
     }

     /* Examine permissions at ddpt + 1 */;
     perms= 0;
     for(ppt= ddpt + 1; *ppt != 0 && *ppt != '\n'; ppt++) {
       if(*ppt == 'r')
         perms|= 4;
       else if(*ppt == 'w')
         perms|= 2;
       else if(*ppt == 'x')
         perms|= 1;
       else if(*ppt == '-' || *ppt == ' ' || *ppt == '\t')
         ;
       else if(*ppt == '#')
     break;
       else {
         was_error= line_count;
     break;
       }
     }
     if(pass == 0) {
       line_len+= 4;
     } else {
       sprintf(*wpt, "%c%c%c\n",
          perms & 4 ? 'r' : '-', perms & 2 ? 'w' : '-', perms & 1 ? 'x' : '-');
       (*wpt)+= 4;
     }

     if(pass == 0) {
       if(is_default)
         default_count+= line_len;
       else
         access_count+= line_len;
     }
   }

   if(pass == 0) {
     *access_acl_text= calloc(access_count + 1, 1);
     *default_acl_text= calloc(default_count + 1, 1);
     if(*access_acl_text == NULL || *default_acl_text == NULL) {
       Xorriso_no_malloc_memory(xorriso, access_acl_text, 0);
       {ret= -1; goto ex;}
     }
     access_wpt= *access_acl_text;
     default_wpt= *default_acl_text;
   } else {
     *access_wpt= 0;
     *default_wpt= 0;
   }
 }

 ret= 1;
ex:;
 if(acl_text != NULL)
   free(acl_text);
 if(was_error) {
   sprintf(xorriso->info_text,
           "Malformed ACL entries encountered. Last one in line number %d.",
           was_error);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(ret);
}

