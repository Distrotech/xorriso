
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2011 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains functions which operate on data filter objects.
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

#include "lib_mgt.h"
#include "iso_tree.h"

/*

#include "xorriso.h"
#include "xorriso_private.h"
#include "xorrisoburn.h"

#include "iso_img.h"
#include "iso_manip.h"
#include "sort_cmp.h"

*/


struct Xorriso_extF {

 int flag; /* unused yet */

 IsoExternalFilterCommand *cmd;

};

int Xorriso_extf_destroy(struct XorrisO *xorriso, struct Xorriso_extF **filter,
                         int flag);


/* @param flag see struct Xorriso_extF.flag */
int Xorriso_extf_new(struct XorrisO *xorriso, struct Xorriso_extF **filter,
                     char *path, int argc, char **argv, int behavior,
                     char *suffix, char *name, int flag)
{
 int i;
 struct Xorriso_extF *o= NULL;
 IsoExternalFilterCommand *cmd;

 *filter= o= calloc(sizeof(struct Xorriso_extF), 1);
 if(o == NULL)
   goto failure;
 o->flag= flag;
 o->cmd= NULL;
 o->cmd= cmd= calloc(sizeof(IsoExternalFilterCommand), 1);
 if(cmd == NULL)
   goto failure;
 cmd->version= 0;
 cmd->refcount= 0;
 cmd->name= NULL;
 cmd->path= NULL;
 cmd->argv= NULL;
 cmd->argc= argc + 1;
 cmd->behavior= behavior;
 cmd->suffix= NULL;
 cmd->suffix= strdup(suffix);
 if(cmd->suffix == NULL)
   goto failure;

 cmd->path= strdup(path);
 if(cmd->path == NULL)
   goto failure;
 cmd->argv= calloc(sizeof(char *), argc + 2);
 if(cmd->argv == NULL)
   goto failure;
 for(i= 0; i < argc + 2; i++)
   cmd->argv[i]= NULL;
 cmd->argv[0]= strdup(path);
 if(cmd->argv[0] == NULL)
   goto failure;
 for(i= 0; i < argc; i++) {
   cmd->argv[i + 1]= strdup(argv[i]);
   if(cmd->argv[i] == NULL)
     goto failure;
 }
 cmd->name= strdup(name);
 if(cmd->name == NULL)
   goto failure;
 return(1);
failure:;
 Xorriso_extf_destroy(xorriso, filter, 0);
 return(-1);
}


int Xorriso_extf_destroy(struct XorrisO *xorriso, struct Xorriso_extF **filter,
                         int flag)
{
 int i;
 IsoExternalFilterCommand *cmd;

 if(*filter == NULL)
   return(0);
 cmd= (*filter)->cmd;
 if(cmd != NULL) {
   if(cmd->refcount > 0)
     return(0);
   if(cmd->path != NULL)
     free(cmd->path);
   if(cmd->suffix != NULL)
     free(cmd->suffix);
   if(cmd->argv != NULL) {
     for(i= 0; i < cmd->argc; i++)
       if(cmd->argv[i] != NULL)
         free(cmd->argv[i]);
     free((char *) cmd->argv);
   }
   if(cmd->name != NULL)
     free(cmd->name);
   free((char *) cmd);
 }
 free((char *) *filter);
 *filter= NULL;
 return(1);
}


int Xorriso_lookup_extf(struct XorrisO *xorriso, char *name,
                        struct Xorriso_lsT **found_lst, int flag)
{
 struct Xorriso_extF *filter;
 struct Xorriso_lsT *lst;

 for(lst= xorriso->filters; lst != NULL; lst= Xorriso_lst_get_next(lst, 0)) {
   filter= (struct Xorriso_extF *) Xorriso_lst_get_text(lst, 0);
   if(strcmp(filter->cmd->name, name) == 0) {
     *found_lst= lst;
     return(1);
   }
 }
 return(0);
}


int Xorriso_destroy_all_extf(struct XorrisO *xorriso, int flag)
{
 struct Xorriso_extF *filter;
 struct Xorriso_lsT *lst, *next_lst;

 for(lst= xorriso->filters; lst != NULL; lst= next_lst) {
   filter= (struct Xorriso_extF *) Xorriso_lst_get_text(lst, 0);
   Xorriso_lst_detach_text(lst, 0);
   next_lst= Xorriso_lst_get_next(lst, 0);
   Xorriso_lst_destroy(&lst, 0);
   Xorriso_extf_destroy(xorriso, &filter, 0);
 }
 xorriso->filters= NULL;
 return(1);
}


/*
 @param flag   bit0= return 2 if renaming is not possible by libisofs
                     (always: if demanded strip suffix is missing
                              or if suffix makes name length > 255)
               bit1= strip suffix rather than appending it
*/
int Xorriso_rename_suffix(struct XorrisO *xorriso, IsoNode *node, char *suffix,
                          char *show_path, char new_name[], int flag)
{
 int ret, lo= 0, ls= 0, strip_suffix;
 char *old_name= NULL, *show_name;

 strip_suffix= !!(flag & 2);
 
 old_name= strdup((char *) iso_node_get_name(node));
 show_name= old_name;
 if(show_path != NULL)
   if(show_path[0] != 0)
     show_name= show_path;
 lo= strlen(old_name);
 ls= strlen(suffix);
 if(strip_suffix) {
   if(lo <= ls) {
     /* refuse gracefully */
     ret= 2; goto ex;
   }
   if(strcmp(old_name + lo - ls, suffix) != 0) {
     ret= 2; goto ex;
   }
   if(lo >= SfileadrL)
     goto cannot_remove_suffix;
   strcpy(new_name, old_name);
   new_name[lo - ls]= 0;
   ret = iso_node_set_name(node, new_name);
   if (ret < 0) {
     Xorriso_process_msg_queues(xorriso,0);
     if (!(flag & 1))
       Xorriso_report_iso_error(xorriso, "", ret,
                            "Error when renaming ISO node", 0, "FAILURE", 1);
cannot_remove_suffix:;
     strcpy(xorriso->info_text, "-set_filter: Cannot remove suffix from ");
     Text_shellsafe(show_name, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0,
                         (flag & 1) ? "WARNING" : "FAILURE", 0);
     ret= 2 * (flag & 1); goto ex;
   }
 } else {
   /* check whether suffix already present */
   if(lo >= ls)
     if(strcmp(old_name + lo - ls, suffix) == 0) {
       /* refuse gracefully */
       ret= 2; goto ex;
     }
   if(lo + ls > 255) {
cannot_append_suffix:;
     strcpy(xorriso->info_text, "-set_filter: Cannot append suffix to ");
     Text_shellsafe(show_name, xorriso->info_text, 1);
     strcat(xorriso->info_text, ". Left unfiltered.");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0,
                         (flag & 1) ? "WARNING" : "FAILURE", 0);
     ret= 2 * (flag & 1); goto ex;
   } 
   sprintf(new_name, "%s%s", old_name, suffix);
   ret = iso_node_set_name(node, new_name);
   if (ret < 0) {
     Xorriso_process_msg_queues(xorriso,0);
     if (!(flag & 1))
       Xorriso_report_iso_error(xorriso, "", ret,
                              "Error when renaming ISO node", 0, "FAILURE", 1);
     goto cannot_append_suffix;
   }
 }

 ret= 1;
ex:;
 if(old_name != NULL)
   free(old_name);
 Xorriso_process_msg_queues(xorriso,0);
 return(ret);
}


/*
 @param flag   bit0= return 2 if renaming is not possible
               bit1= print pacifier messages
 */
int Xorriso_set_filter(struct XorrisO *xorriso, void *in_node,
                       char *path, char *filter_name, int flag)
{
 int ret, strip_suffix= 0, strip_filter= 0, filter_ret= 0;
 int explicit_suffix= 0, internal_filter= 0;
 IsoNode *node;
 IsoFile *file;
 struct Xorriso_lsT *found_lst;
 struct Xorriso_extF *found_filter;
 IsoExternalFilterCommand *cmd = NULL;
 char *old_name= NULL, *new_name= NULL, *suffix= "";
 IsoStream *stream;

 Xorriso_alloc_meM(new_name, char, SfileadrL);
 new_name[0]= 0;

 node= (IsoNode *) in_node;
 if(node == NULL) {
   ret= Xorriso_get_node_by_path(xorriso, path, NULL, &node, 0);
   if(ret <= 0)
     goto ex;
 }
 if(!LIBISO_ISREG(node)) {
   strcpy(xorriso->info_text, "-set_filter: Not a regular data file node ");
   Text_shellsafe(path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   ret= 0; goto ex;
 }
 file= (IsoFile *) node;

 if(strncmp(filter_name, "--remove-all-filters", 20) == 0) {
   strip_filter= 1;
   strip_suffix= 1;
   if(strlen(filter_name) > 21) {
     strip_suffix= (filter_name[20] != '+');
     suffix= filter_name + 21;
     explicit_suffix= 1;
   }
 } else if(strcmp(filter_name, "--zisofs") == 0) {
   internal_filter= 1;
 } else if(strcmp(filter_name, "--zisofs-decode") == 0) {
   internal_filter= 2;
 } else if(strcmp(filter_name, "--gzip") == 0) {
   internal_filter= 3;
   suffix= ".gz";
   strip_suffix= 0;
   explicit_suffix= 1;
 } else if(strcmp(filter_name, "--gunzip") == 0 ||
           strcmp(filter_name, "--gzip-decode") == 0) {
   internal_filter= 4;
   suffix= ".gz";
   strip_suffix= 1;
   explicit_suffix= 1;
 } else {
   ret= Xorriso_lookup_extf(xorriso, filter_name, &found_lst, 0);
   if(ret < 0)
     goto ex;
   if(ret == 0) {
     strcpy(xorriso->info_text, "-set_filter: Not a registered filter name ");
     Text_shellsafe(filter_name, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }
   found_filter= (struct Xorriso_extF *) Xorriso_lst_get_text(found_lst, 0);
   cmd= found_filter->cmd;
   suffix= cmd->suffix;
   strip_suffix= cmd->behavior & 8;
 }

 if(suffix[0]) {

   /* >>> would need full iso_rr_path of node for showing */;

   old_name= strdup((char *) iso_node_get_name(node));
   ret= Xorriso_rename_suffix(xorriso, node, suffix, path, new_name,
                               (flag & 1) | (strip_suffix ? 2 : 0));
   if(ret <= 0 || ret == 2)
     goto ex;
 }

 if(strip_filter) {
   while(1) {
     if(!explicit_suffix) {
       stream= iso_file_get_stream(file);

       if(strncmp(stream->class->type, "gzip", 4) == 0) {
         suffix= ".gz";
         strip_suffix= 1;
       } else if(strncmp(stream->class->type, "pizg", 4) == 0) {
         suffix= ".gz";
         strip_suffix= 0;
       } else {
         ret= iso_stream_get_external_filter(stream, &cmd, 0);
         if(ret > 0) {
           suffix= cmd->suffix;
           strip_suffix= !(cmd->behavior & 8);
         }
       }
       if(suffix[0]) {

         /* >>> would need the current renaming state of path */;

         ret= Xorriso_rename_suffix(xorriso, node, suffix, NULL, new_name,
                                    (flag & 1) | (strip_suffix << 1));
         if(ret <= 0 || ret == 2)
           goto ex;
       }
     }
     ret= iso_file_remove_filter(file, 0);
     if(ret != 1)
   break;
   }
   filter_ret= 1;
 } else if (internal_filter == 1 || internal_filter == 2) {
   filter_ret = iso_file_add_zisofs_filter(file, 1 | (internal_filter & 2));
   if(filter_ret < 0) {
     Xorriso_process_msg_queues(xorriso,0);
     if(!(internal_filter == 2 && filter_ret == (int) ISO_ZISOFS_WRONG_INPUT))
       Xorriso_report_iso_error(xorriso, "", filter_ret,
                    "Error when setting filter to ISO node", 0, "FAILURE", 1);
   }
 } else if (internal_filter == 3 || internal_filter == 4) {
   filter_ret = iso_file_add_gzip_filter(file,
                                         1 | ((internal_filter == 4) << 1));
   if(filter_ret < 0) {
     Xorriso_process_msg_queues(xorriso,0);
     Xorriso_report_iso_error(xorriso, "", filter_ret,
                    "Error when setting filter to ISO node", 0, "FAILURE", 1);
   }
 } else {

#ifndef Xorriso_allow_extf_suiD
   /* This is a final safety precaution before iso_file_add_external_filter()
      performs fork() and executes the alleged filter program.
   */
   if(getuid() != geteuid()) {
     sprintf(xorriso->info_text,
          "-set_filter: UID and EUID differ. Will not run external programs.");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
     ret= 0; goto ex;
   }
#endif /* ! Xorriso_allow_extf_suiD */

   filter_ret = iso_file_add_external_filter(file, cmd, 0);
   if(filter_ret < 0) {
     Xorriso_process_msg_queues(xorriso,0);
     Xorriso_report_iso_error(xorriso, "", filter_ret,
                    "Error when setting filter to ISO node", 0, "FAILURE", 1);
   }
 }
 if(filter_ret != 1 && new_name[0] && old_name != NULL) {
   ret = iso_node_set_name(node, old_name);
   if (ret < 0) {
     Xorriso_process_msg_queues(xorriso,0);
     if (!(flag & 1))
       Xorriso_report_iso_error(xorriso, "", ret,
                              "Error when renaming ISO node", 0, "FAILURE", 1);
   }
 }
 if(flag & 2) {
   xorriso->pacifier_count++;
   Xorriso_pacifier_callback(xorriso, "file filters processed",
                      xorriso->pacifier_count, xorriso->pacifier_total, "", 0);
 }
 if(filter_ret < 0) {
   ret= 0; goto ex;
 }

 ret= filter_ret;
ex:;
 if(old_name != NULL)
   free(old_name);
 Xorriso_free_meM(new_name);
 Xorriso_process_msg_queues(xorriso,0);
 return(ret);
}


/* @param flag bit0= delete filter with the given name
*/
int Xorriso_external_filter(struct XorrisO *xorriso,
                            char *name, char *options, char *path, 
                            int argc, char **argv, int flag)
{
 int ret, delete= 0, behavior= 0, extf_flag= 0, is_banned= 0;
 char *what, *what_next, *suffix= "";
 struct Xorriso_lsT *lst;
 struct Xorriso_extF *found_filter, *new_filter= NULL;

#ifndef Xorriso_allow_external_filterS
 /* To be controlled by: configure --enable-external-filters */

 sprintf(xorriso->info_text, "%s : Banned at compile time.",
         flag & 1 ? "-unregister_filter" : "-external_filter");
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
 sprintf(xorriso->info_text,
"This may be changed at compile time by ./configure option --enable-external-filters");
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
 is_banned= 1;
 
#endif /* ! Xorriso_allow_external_filterS */

#ifndef Xorriso_allow_extf_suiD
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
#endif /* ! Xorriso_allow_extf_suiD */

 if(is_banned)
   return(0);

 if(xorriso->filter_list_closed) {
   sprintf(xorriso->info_text,
           "%s : Banned by previous command -close_filter_list",
           flag & 1 ? "-unregister_filter" : "-external_filter");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 } 
 if((!(flag & 1)) && path[0] != '/') {
   sprintf(xorriso->info_text,
           "-external_filter : Given command path does not begin by '/' : ");
   Text_shellsafe(path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }


 delete= flag & 1;
 ret= Xorriso_lookup_extf(xorriso, name, &lst, 0);
 if(ret < 0)
   return(ret);
 if(ret > 0) {
   if(delete) {
     found_filter= (struct Xorriso_extF *) Xorriso_lst_get_text(lst, 0);
     if(found_filter->cmd->refcount > 0) {
       sprintf(xorriso->info_text,
 "-external_filter: Cannot remove filter because it is in use by %.f nodes : ",
              (double) found_filter->cmd->refcount);
       Text_shellsafe(name, xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
     }
     Xorriso_lst_detach_text(lst, 0);
     if(xorriso->filters == lst)
       xorriso->filters= Xorriso_lst_get_next(lst, 0);
     Xorriso_lst_destroy(&lst, 0);
     Xorriso_extf_destroy(xorriso, &found_filter, 0);
     ret= 1; goto ex;
   }
   strcpy(xorriso->info_text,
          "-external_filter: filter with given name already existing: ");
   Text_shellsafe(name, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   ret= 0; goto ex;
 }
 if(delete) {
   strcpy(xorriso->info_text,
          "-external_filter: filter with given name does not exist: ");
   Text_shellsafe(name, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   ret= 0; goto ex;
 }

 for(what= options; what!=NULL; what= what_next) {
   what_next= strchr(what, ':');
   if(what_next!=NULL) {
     *what_next= 0;
     what_next++;
   }
   if(strncmp(what, "default", 7) == 0) {
     suffix= "";
     behavior= 0;
   } else if(strncmp(what, "suffix=", 7) == 0) {
     suffix= what + 7;
   } else if(strcmp(what, "remove_suffix") == 0) {
     behavior|= 8;
   } else if(strcmp(what, "if_nonempty") == 0) {
     behavior|= 1;
   } else if(strcmp(what, "if_reduction") == 0) {
     behavior|= 2;
   } else if(strcmp(what, "if_block_reduction") == 0) {
     behavior|= 4;
   } else if(strncmp(what, "used=", 5) == 0) {
     ; /* this is informational output from -status */
   } else if(what[0]) {
     strcpy(xorriso->info_text,
            "-external_filter: unknown option ");
     Text_shellsafe(what, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }
 }

 ret= Xorriso_extf_new(xorriso, &new_filter, path, argc, argv, behavior,
                       suffix, name, extf_flag);
 if(ret <= 0) {
could_not_create:;
   strcpy(xorriso->info_text,
          "-external_filter: Could not create filter object");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   goto ex;
 }
 ret= Xorriso_lst_append_binary(&(xorriso->filters), (char *) new_filter,0, 4);
 if(ret <= 0)
   goto could_not_create;
 ret= 1;
ex:;
 if(ret <= 0) {
   if(new_filter != NULL)
     Xorriso_extf_destroy(xorriso, &new_filter, 0);
 }
 return(ret);
}


int Xorriso_status_extf(struct XorrisO *xorriso, char *filter, FILE *fp,
                        int flag)
/*
 bit1= do only report to fp
*/ 
{
 int i, maxl= 4 * SfileadrL;
 struct Xorriso_extF *extf;
 struct Xorriso_lsT *lst;
 char *line;

 line= xorriso->result_line;
 for(lst= xorriso->filters; lst != NULL; lst= Xorriso_lst_get_next(lst, 0)) {
   extf= (struct Xorriso_extF *) Xorriso_lst_get_text(lst, 0);
   
   strcpy(xorriso->result_line, "-external_filter ");
   Text_shellsafe(extf->cmd->name, line, 1);
   if((int) strlen(line) > maxl)
 continue;
   strcat(line, " ");
   if(extf->cmd->suffix[0]) {
     strcat(line, "suffix=");
     Text_shellsafe(extf->cmd->suffix, line, 1);
     if((int) strlen(line) > maxl)
 continue;
     strcat(line, ":");
   }
   if(extf->cmd->behavior & 8)
     strcat(line, "remove_suffix:");
   if(extf->cmd->behavior & 1)
     strcat(line, "if_nonempty:");
   if(extf->cmd->behavior & 2)
     strcat(line, "if_reduction:");
   if(extf->cmd->behavior & 4)
     strcat(line, "if_block_reduction:");
   sprintf(line + strlen(line), "used=%.f ", (double) extf->cmd->refcount);
   if((int) strlen(line) > maxl)
 continue;
   Text_shellsafe(extf->cmd->path, line, 1);
   if((int) strlen(line) > maxl)
 continue;
   for(i= 1; i < extf->cmd->argc; i++) {
     strcat(line, " ");
     Text_shellsafe(extf->cmd->argv[i], line, 1);
     if((int) strlen(line) > maxl)
   break;
   }
   if(i < extf->cmd->argc)
 continue;
   strcat(line, " --\n");
   Xorriso_status_result(xorriso, filter, fp, flag&2);
 }
 if(xorriso->filter_list_closed) {
   strcpy(line, "-close_filter_list\n");
   Xorriso_status_result(xorriso, filter, fp, flag&2);
 }
 return(1);
}


int Xorriso_set_zisofs_params(struct XorrisO *xorriso, int flag)
{
 int ret;
 struct iso_zisofs_ctrl ctrl;

 ctrl.version= 0;
 ctrl.compression_level= xorriso->zlib_level;
 if(xorriso->zisofs_block_size == (1 << 16))
   ctrl.block_size_log2= 16;
 else if(xorriso->zisofs_block_size == (1 << 17))
   ctrl.block_size_log2= 17;
 else
   ctrl.block_size_log2= 15;
 ret= iso_zisofs_set_params(&ctrl, 0);
 Xorriso_process_msg_queues(xorriso,0);
 if(ret < 0) {
   Xorriso_report_iso_error(xorriso, "", ret,
                      "Error when setting zisofs parameters", 0, "FAILURE", 1);
   return(0);
 }
 return(1);
}


int Xorriso_status_zisofs(struct XorrisO *xorriso, char *filter, FILE *fp,
                          int flag)
/*
 bit0= do only report non-default settings
 bit1= do only report to fp
*/ 
{
 off_t ziso_count= 0, osiz_count= 0;
 off_t gzip_count= 0, gunzip_count= 0;

 iso_zisofs_get_refcounts(&ziso_count, &osiz_count, 0);
 iso_gzip_get_refcounts(&gzip_count, &gunzip_count, 0);
 if((flag & 1) && xorriso->zlib_level == xorriso->zlib_level_default &&
    xorriso->zisofs_block_size == xorriso->zisofs_block_size_default &&
    xorriso->zisofs_by_magic == 0 &&
    ziso_count == 0 && osiz_count == 0 &&
    gzip_count == 0 && gunzip_count == 0) {
   if(filter == NULL)
     return(2);
   if(filter[0] == 0)
     return 2;
 }
 sprintf(xorriso->result_line,
     "-zisofs level=%d:block_size=%dk:by_magic=%s:ziso_used=%.f:osiz_used=%.f",
     xorriso->zlib_level, xorriso->zisofs_block_size / 1024,
     xorriso->zisofs_by_magic ? "on" : "off",
     (double) ziso_count, (double) osiz_count);
 sprintf(xorriso->result_line + strlen(xorriso->result_line),
         ":gzip_used=%.f:gunzip_used=%.f\n",
         (double) gzip_count, (double) gunzip_count);
 Xorriso_status_result(xorriso, filter, fp, flag&2);
 return(1);
}

