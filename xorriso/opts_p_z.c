
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2013 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the implementation of options -p* to -z* as mentioned
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


#include "xorriso.h"
#include "xorriso_private.h"
#include "xorrisoburn.h"


/* Option -pacifier */
int Xorriso_option_pacifier(struct XorrisO *xorriso, char *style, int flag)
{
 if(strcmp(style, "xorriso")==0 || strcmp(style, "default")==0)
   xorriso->pacifier_style= 0;
 else if(strcmp(style, "mkisofs")==0 || strcmp(style, "genisofs")==0 ||
         strcmp(style, "genisoimage")==0 || strcmp(style, "xorrisofs")==0)
   xorriso->pacifier_style= 1;
 else if(strcmp(style, "cdrecord")==0 || strcmp(style, "cdrskin")==0 ||
         strcmp(style, "wodim")==0 || strcmp(style, "xorrecord")==0)
   xorriso->pacifier_style= 2;
 else {
   sprintf(xorriso->info_text, "-pacifier: unknown behavior code '%s'", style);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 return(1);
}


/* Option -padding */
int Xorriso_option_padding(struct XorrisO *xorriso, char *size, int flag)
{
 double num;

 if(strcmp(size, "included") == 0) {
   xorriso->do_padding_by_libisofs= 1;
   return(1);
 } else if(strcmp(size, "excluded") == 0 || strcmp(size, "appended") == 0) {
   xorriso->do_padding_by_libisofs= 0;
   return(1);
 } else if(size[0] < '0' || size[0] > '9') {
   sprintf(xorriso->info_text, "-padding: unrecognized non-numerical mode ");
   Text_shellsafe(size, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 num= Scanf_io_size(size, 0);
 if(num < 0 || num > 1024.0 * 1024.0 * 1024.0) {
   sprintf(xorriso->info_text, "-padding: wrong size %.f (allowed: %.f - %.f)",
           num, 0.0, 1024.0 * 1024.0 * 1024.0);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 xorriso->padding= num;
 if(xorriso->padding/2048 != num/2048.0)
   xorriso->padding++;
 return(1);
}


/* Option -page */
int Xorriso_option_page(struct XorrisO *xorriso, int len, int width, int flag)
{
 if(len<0 || width<=0) {
   sprintf(xorriso->info_text,
             "Improper numeric value of arguments of -page:  %d  %d",
             len, width);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 xorriso->result_page_length= len;
 xorriso->result_page_width= width;
 return(1);
}


/* Option -paste_in */
int Xorriso_option_paste_in(struct XorrisO *xorriso, char *iso_rr_path,
                           char *disk_path, char *start, char *count, int flag)
{
 int ret;
 double num;
 off_t startbyte, bytecount;
 
 num= Scanf_io_size(start, 0);
 if(num<0 || num > 1.0e18) { /* 10^18 = 10^3 ^ 6 < 2^10 ^ 6 = 2^60 */
   sprintf(xorriso->info_text,
        "-paste_in: startbyte address negative or much too large (%s)", start);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 startbyte= num;
 num= Scanf_io_size(count, 0);
 if(num<=0 || num > 1.0e18) {
   sprintf(xorriso->info_text,
         "-paste_in : bytecount zero, negative or much too large (%s)", count);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 bytecount= num;
 sprintf(xorriso->info_text, "-paste_in from %s to %s, byte %.f to %.f",
         disk_path, iso_rr_path,
         (double) startbyte, (double) (startbyte+bytecount));
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);

 ret= Xorriso_paste_in(xorriso, disk_path, startbyte, bytecount,
                       iso_rr_path, 0); 
 return(ret);
}


/* Option -path_list , -quoted_path_list */
/* @param flag bit0= -quoted_path_list */
int Xorriso_option_path_list(struct XorrisO *xorriso, char *adr, int flag)
{
 int ret,linecount= 0, insertcount= 0, null= 0, was_failure= 0, fret= 0;
 int was_ferror= 0, argc= 0, i;
 FILE *fp= NULL;
 char **argv= NULL;

 Xorriso_pacifier_reset(xorriso, 0);
 if(adr[0]==0) {
   sprintf(xorriso->info_text,"Empty file name given with %s",
           flag & 1 ? "-quoted_path_list" : "-path_list");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
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
     null= 0;
     ret= Xorriso_option_add(xorriso, 1, argv + i, &null, 1|2);
     if(ret<=0 || xorriso->request_to_abort)
       goto problem_handler;
     insertcount++;

   continue; /* regular bottom of loop */
problem_handler:;
     was_failure= 1;
     fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
     if(fret>=0)
   continue;
     if(ret > 0)
       ret= 0;
     goto ex;
   }
 }
 ret= 1;
ex:;
 if(flag & 1)
   Xorriso_read_lines(xorriso, fp, &linecount, &argc, &argv, 2);

 if(fp != NULL && fp != stdin)
   fclose(fp);
 Xorriso_pacifier_callback(xorriso, "files added", xorriso->pacifier_count,
                           xorriso->pacifier_total, "", 1);
 if(ret<=0) {
   sprintf(xorriso->info_text, "Aborted reading of file ");
   Text_shellsafe(adr, xorriso->info_text, 1);
   sprintf(xorriso->info_text + strlen(xorriso->info_text),
           " in line number %d", linecount);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0,
                       (fret==-2 ? "NOTE" : "FAILURE"), 0);
 } else
   ret= !was_ferror;
 sprintf(xorriso->info_text, "Added %d items from file ", insertcount);
 Text_shellsafe(adr, xorriso->info_text, 1);
 strcat(xorriso->info_text, "\n");
 Xorriso_info(xorriso,0);
 if(ret<=0)
   return(ret);
 return(!was_failure);
}


/* Option -pathspecs */
int Xorriso_option_pathspecs(struct XorrisO *xorriso, char *mode, int flag)
{
 if(strcmp(mode, "off")==0)
   xorriso->allow_graft_points= 0;
 else if(strcmp(mode, "on")==0)
   xorriso->allow_graft_points= 1;
 else {
   sprintf(xorriso->info_text, "-pathspecs: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(1);
}


/* Option -pkt_output */
int Xorriso_option_pkt_output(struct XorrisO *xorriso, char *mode, int flag)
{
 if(strcmp(mode,"off")==0)
   xorriso->packet_output= 0;
 else
   xorriso->packet_output= 1;
 return(1);
}


/* Option -preparer_id */
int Xorriso_option_preparer_id(struct XorrisO *xorriso, char *name, int flag)
{
 if(Xorriso_check_name_len(xorriso, name,
                           (int) sizeof(xorriso->preparer_id),
                           "-preparer_id", 0) <= 0)
   return(0);
 if(strcmp(name, "@xorriso@") == 0)
   Xorriso_preparer_string(xorriso, xorriso->preparer_id, 0);
 else
   strcpy(xorriso->preparer_id, name);
 Xorriso_set_change_pending(xorriso, 1);
 return(1);
}


/* Options -print , -print_info , -print_mark */
/* @param flag bit0-1= channel: 0=result, 1=info, 2=mark */
int Xorriso_option_print(struct XorrisO *xorriso, char *text, int flag)
{
 int maxl, l, mode;

 l= strlen(text);
 mode= flag & 3;
 if(mode == 1)
   maxl= sizeof(xorriso->info_text);
 else if(mode == 2)
   maxl= sizeof(xorriso->mark_text);
 else
   maxl= sizeof(xorriso->result_line);
 if(l >= maxl) {
   sprintf(xorriso->info_text, "Output text too long for -print%s(%d > %d)",
           mode == 1 ? "_info" : mode == 2 ? "_mark" : "", l, maxl);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
   return(0);
 }
 if(mode == 1) {
   sprintf(xorriso->info_text,"%s\n", text);
   Xorriso_info(xorriso,0);
 } else if(mode == 2) {
   strcpy(xorriso->info_text, xorriso->mark_text);
   strcpy(xorriso->mark_text, text);
   Xorriso_mark(xorriso,0);
   strcpy(xorriso->mark_text, xorriso->info_text);
 } else {
   sprintf(xorriso->result_line,"%s\n",text);
   Xorriso_result(xorriso,1);
 }
 return(1);
}


/* Option -print_size
   @param flag bit0= report in mkisofs compatible form on real stdout
                     (resp. on result channel if xorriso->packet_output)
*/
int Xorriso_option_print_size(struct XorrisO *xorriso, int flag)
{
 int ret, fd;

 if(!Xorriso_change_is_pending(xorriso, 0)) {
   sprintf(xorriso->info_text,"-print_size: No image modifications pending");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
   if(!(flag & 1)) {
     sprintf(xorriso->result_line,"Image size   : 0s\n");
     Xorriso_result(xorriso,0);
   }
   return(2);
 }
 ret= Xorriso_write_session(xorriso, 1);
 if(ret<=0) {
   sprintf(xorriso->info_text,"-print_size: Failed to set up virtual -commit");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 if(flag&1) {
   sprintf(xorriso->result_line,"%d\n", ret);
   if(xorriso->packet_output) {
     Xorriso_result(xorriso,0);
   } else {
     fd= xorriso->dev_fd_1;
     if(fd<0)
       fd= 1;
     ret= write(fd, xorriso->result_line, strlen(xorriso->result_line));
     /* (result of write intentionally ignored) */
     fsync(fd);
   }
 } else {
   sprintf(xorriso->result_line,"Image size   : %ds\n", ret);
   Xorriso_result(xorriso,0);
 }
 return(1);
}


/* Option -prog */
int Xorriso_option_prog(struct XorrisO *xorriso, char *name, int flag)
{
 if(strlen(name)>=sizeof(xorriso->progname)) {
   sprintf(xorriso->info_text,
           "Name too long with option -prog (%d > %d)",
           (int) strlen(name), (int) sizeof(xorriso->progname)-1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 if(Sfile_str(xorriso->progname,name,0)<=0)
   return(-1);
 return(1);
}


/* Option -prog_help */
int Xorriso_option_prog_help(struct XorrisO *xorriso, char *name, int flag)
{
 int ret;

 ret= Xorriso_option_prog(xorriso, name, 0);
 if(ret<=0)
   return(ret);
 ret= Xorriso_option_help(xorriso, 0);
 return(ret);
}


/* Option -prompt */
int Xorriso_option_prompt(struct XorrisO *xorriso, char *text, int flag)
{
 int ret;
 char line[80];

 strncpy(xorriso->result_line,text,sizeof(xorriso->result_line)-1);
 xorriso->result_line[sizeof(xorriso->result_line)-1]= 0;
 Xorriso_result(xorriso,0);
 ret= Xorriso_dialog_input(xorriso, line, sizeof(line),1);
 return(ret);
}


/* Option -publisher */
int Xorriso_option_publisher(struct XorrisO *xorriso, char *name, int flag)
{
  if(Xorriso_check_name_len(xorriso, name, (int) sizeof(xorriso->publisher),
                            "-publisher", 0) <= 0)
    return(0);
 strcpy(xorriso->publisher,name);
 Xorriso_set_change_pending(xorriso, 1);
 return(1);
}


/* Option -pvd_info */
int Xorriso_option_pvd_info(struct XorrisO *xorriso, int flag)
{
  return(Xorriso_pvd_info(xorriso, 0));
}


/* Option -pwd alias -pwdi */
int Xorriso_option_pwdi(struct XorrisO *xorriso, int flag)
{
 sprintf(xorriso->info_text,"current working directory in ISO image:\n");
 Xorriso_info(xorriso,0);
 Text_shellsafe(xorriso->wdi, xorriso->result_line, 0);
 strcat(xorriso->result_line, "/\n");
 Xorriso_result(xorriso,0);
 return(1);
}


/* Option -pwdx */
int Xorriso_option_pwdx(struct XorrisO *xorriso, int flag)
{
 sprintf(xorriso->info_text,"current working directory on hard disk:\n");
 Xorriso_info(xorriso,0);
 sprintf(xorriso->result_line,"%s/\n",xorriso->wdx);
 Xorriso_result(xorriso,0);
 return(1);
}


int Xorriso_option_read_mkisofsrc(struct XorrisO *xorriso, int flag)
{
 int ret;

 ret= Xorriso_read_mkisofsrc(xorriso, 0);
 return(ret);
}


/* Option -reassure "on"|"tree"|"off" */
int Xorriso_option_reassure(struct XorrisO *xorriso, char *mode, int flag)
{
 if(strcmp(mode, "off")==0)
   xorriso->do_reassure= 0;
 else if(strcmp(mode, "on")==0)
   xorriso->do_reassure= 1;
 else if(strcmp(mode, "tree")==0)
   xorriso->do_reassure= 2;
 else {
   sprintf(xorriso->info_text, "-reassure: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 return(1);
}


/* Option -report_about */
int Xorriso_option_report_about(struct XorrisO *xorriso, char *in_severity,
                                int flag)
{
 int ret, sev;
 char severity[20], *official;

 Xorriso__to_upper(in_severity, severity, (int) sizeof(severity), 0);
 ret= Xorriso__text_to_sev(severity, &sev, 0);
 if(ret<=0) {
   sprintf(xorriso->info_text, "-report_about: Not a known severity name : ");
   Text_shellsafe(in_severity, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
   return(ret);
 }
 ret= Xorriso__sev_to_text(sev, &official, 0);
 if(ret <= 0)
   official= severity;
 if(Sfile_str(xorriso->report_about_text, official, 0) <= 0)
   return(-1);
 xorriso->report_about_severity= sev;
 return(1);
}


/* Option -return_with */
int Xorriso_option_return_with(struct XorrisO *xorriso, char *in_severity,
                               int exit_value, int flag)
{
 int ret, sev;
 char severity[20], *official;

 Xorriso__to_upper(in_severity, severity, (int) sizeof(severity), 0);
 ret= Xorriso__text_to_sev(severity, &sev, 0);
 if(ret<=0) {
   sprintf(xorriso->info_text,
           "-return_with: Not a known severity name : ");
   Text_shellsafe(in_severity, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(ret);
 }
 ret= Xorriso__sev_to_text(sev, &official, 0);
 if(ret <= 0)
   official= severity;
 if(exit_value && (exit_value < 32 || exit_value > 63)) {
   sprintf(xorriso->info_text,
           "-return_with: Not an allowed exit_value. Use 0, or 32 to 63.");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 if(Sfile_str(xorriso->return_with_text, official, 0) <= 0)
   return(-1);
 xorriso->return_with_severity= sev;
 xorriso->return_with_value= exit_value;
 return(1);
}


/* Options -rm alias -rmi , -rm_r alias -rm_ri , -rmdir alias -rmdiri */
/* @param flag bit0=recursive , bit1= remove empty directory: rmdir */
int Xorriso_option_rmi(struct XorrisO *xorriso, int argc, char **argv,
                       int *idx, int flag)
{
 int i, ret, end_idx, was_failure= 0, fret;
 char *path= NULL, *eff_path= NULL;
 int optc= 0;
 char **optv= NULL;

 ret= Xorriso_opt_args(xorriso, "-rm*i",
                       argc, argv, *idx, &end_idx, &optc, &optv, 0);
 if(ret<=0)
   goto ex; 
 Xorriso_alloc_meM(path, char, SfileadrL);
 Xorriso_alloc_meM(eff_path, char, SfileadrL);

 for(i= 0; i<optc; i++) {
   if(Sfile_str(path,optv[i],0)<=0)
     {ret= -1; goto problem_handler;}
   if(path[0]!='/') {
     ret= Sfile_prepend_path(xorriso->wdi, path, 0);
     if(ret<=0)
       goto problem_handler;
   }
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, path, eff_path, 1);
   if(ret<0)
     goto problem_handler;
   if(ret==0) {
     sprintf(xorriso->info_text, "Cannot find path ");
     Text_shellsafe(path, xorriso->info_text, 1);
     strcat(xorriso->info_text, " in loaded ISO image for removal");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
     goto problem_handler;
   }
   strcpy(path, eff_path);

   ret= Xorriso_rmi(xorriso, NULL, (off_t) 0, path, flag&(1|2));
   if(ret<=0 || xorriso->request_to_abort)
     goto problem_handler;
   if(ret<3) {
     sprintf(xorriso->info_text, "Removed from ISO image: %s '%s'\n",
             ((flag&2) ? "directory" : (ret>1 ? "subtree" : "file")), path);
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
 ret= 1;
ex:; 
 (*idx)= end_idx;
 Xorriso_free_meM(path);
 Xorriso_free_meM(eff_path);
 Xorriso_opt_args(xorriso, "-rm*i",
                  argc, argv, *idx, &end_idx, &optc, &optv, 256);
 if(ret<=0)
   return(ret);
 return(!was_failure);
}


/* Option -rockridge "on"|"off" */
int Xorriso_option_rockridge(struct XorrisO *xorriso, char *mode, int flag)
{
 if(strcmp(mode, "off")==0)
   xorriso->do_rockridge= 0;
 else if(strcmp(mode, "on")==0)
   xorriso->do_rockridge= 1;
 else {
   sprintf(xorriso->info_text, "-rockridge: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(1);
}


/* Option -rollback */
/* @param flag bit0= do not -reassure
   @return <=0 error , 1 success, 2 revoked by -reassure
*/
int Xorriso_option_rollback(struct XorrisO *xorriso, int flag)
{
 int ret;
 char *indev= NULL, *which_will;

 Xorriso_alloc_meM(indev, char, SfileadrL);
 if(Xorriso_change_is_pending(xorriso, 0))
   which_will= "revoke the pending image changes";
 else
   which_will= "reload the image";
 if(!(flag&1)) {
   ret= Xorriso_reassure(xorriso, "-rollback", which_will, 0);
   if(ret<=0)
     {ret= 2; goto ex;}
 }

 if(Sfile_str(indev, xorriso->indev, 0)<=0)
   {ret= -1; goto ex;}
 xorriso->volset_change_pending= 0;
 ret= Xorriso_give_up_drive(xorriso, 1|8);
 if(ret<=0)
   goto ex;
 xorriso->image_start_mode&= ~(1<<31); /* reactivate eventual -load address */
 ret= Xorriso_option_dev(xorriso, indev, 1|4);
ex:;
 Xorriso_free_meM(indev);
 return(ret);
}


/* Option -rom_toc_scan */
int Xorriso_option_rom_toc_scan(struct XorrisO *xorriso, char *mode, int flag)
{
 int l;
 char *cpt, *npt;

 npt= cpt= mode;
 for(cpt= mode; npt != NULL; cpt= npt + 1) {
   npt= strchr(cpt,':');
   if(npt==NULL)
     l= strlen(cpt);
   else
     l= npt-cpt;
   if(l==0)
 continue;
   if(strncmp(cpt, "off", l) == 0)
     xorriso->toc_emulation_flag&= ~5;
   else if(strncmp(cpt, "on", l) == 0)
     xorriso->toc_emulation_flag= (xorriso->toc_emulation_flag & ~4) | 1;
   else if(strncmp(cpt, "force", l) == 0)
     xorriso->toc_emulation_flag|= 5;
   else if(strncmp(cpt, "emul_off", l) == 0)
     xorriso->toc_emulation_flag|= 2;
   else if(strncmp(cpt, "emul_on", l) == 0)
     xorriso->toc_emulation_flag&= ~2;
   else if(strncmp(cpt, "emul_wide", l) == 0)
     xorriso->toc_emulation_flag|= 8;
   else if(strncmp(cpt, "emul_narrow", l) == 0)
     xorriso->toc_emulation_flag&= ~8;
   else {
     sprintf(xorriso->info_text, "-rom_toc_scan: unknown mode in '%s'", mode);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
 }
 return(1);
}


/* Command -rr_reloc_dir */
int Xorriso_option_rr_reloc_dir(struct XorrisO *xorriso, char *name, int flag)
{
 if(strlen(name) > 255) {
   sprintf(xorriso->info_text,
           "Name too long with -rr_reloc_dir. Max. 255 bytes allowed.");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 if(strchr(name, '/') != NULL) {
   sprintf(xorriso->info_text,
           "Name given with -rr_reloc_dir contains '/' character");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 strcpy(xorriso->rr_reloc_dir, name);
 return(1);
}


/* Option -scdbackup_tag list_path record_name */
int Xorriso_option_scdbackup_tag(struct XorrisO *xorriso, char *listname,
                                 char *recname, int flag)
{
 if(strlen(recname) > 80) {
   sprintf(xorriso->info_text,
           "Unsuitable record name given with -scdbackup_tag");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 strcpy(xorriso->scdbackup_tag_name, recname);
 xorriso->scdbackup_tag_time[0]= 0;
 if(Sfile_str(xorriso->scdbackup_tag_listname, listname, 0) <= 0)
   return(-1);
 return(1);
}


/* Option -scsi_log */
int Xorriso_option_scsi_log(struct XorrisO *xorriso, char *mode, int flag)
{
 if(strcmp(mode, "on") == 0)
   xorriso->scsi_log= 1;
 else if(strcmp(mode, "off") == 0)
   xorriso->scsi_log= 0;
 else {
   sprintf(xorriso->info_text, "-scsi_log: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 Xorriso_scsi_log(xorriso, !!xorriso->scsi_log);
 return(1);
}


/* Option -session_log */
int Xorriso_option_session_log(struct XorrisO *xorriso, char *path, int flag)
{
 if(Sfile_str(xorriso->session_logfile, path, 0)<=0)
   return(-1);
 return(1);
}


/* Option -setfacl_list alias -setfacl_listi */
int Xorriso_option_setfacl_listi(struct XorrisO *xorriso, char *path, int flag)
{
 int ret, eaten, line_size;
 size_t buf_size= 0, buf_add= 64 * 1024, l, linecount= 0;
 char *line= NULL, *buf= NULL, *wpt, *new_buf, limit_text[80];
 char *file_path= NULL, *uid= NULL, *gid= NULL;
 FILE *fp= NULL;

 line_size= SfileadrL * 4;
 Xorriso_alloc_meM(line, char, line_size);
 Xorriso_alloc_meM(file_path, char, SfileadrL);
 Xorriso_alloc_meM(uid, char, 161);
 Xorriso_alloc_meM(gid, char, 161);

 Xorriso_pacifier_reset(xorriso, 0);
 if(path[0]==0) {
   sprintf(xorriso->info_text, "Empty file name given with -setfacl_list");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 ret= Xorriso_afile_fopen(xorriso, path, "rb", &fp, 0);
 if(ret <= 0)
   {ret= 0; goto ex;}

 buf_size= buf_add;
 buf= calloc(buf_size, 1);
 if(buf == NULL)
   goto out_of_mem;
 wpt= buf;
 *wpt= 0;
 uid[0]= gid[0]= 0;

 while(1) {
   if(Sfile_fgets_n(line, line_size, fp, 0) == NULL)
 break;
   linecount++;
   if(strncmp(line, "# file: ", 8) ==0) {
      if(wpt != buf && file_path[0]) {
        /* Commit previous list */
        ret= Xorriso_perform_acl_from_list(xorriso, file_path,
                                           uid, gid, buf, 0);
        if(ret<=0)
          goto ex;
        wpt= buf;
        *wpt= 0;
        file_path[0]= uid[0]= gid[0]= 0;
      }
      /* Unescape line and register as file path */
      Sfile_bsl_interpreter(line + 8, strlen(line + 8), &eaten, 0);
      if(strlen(line + 8) >= SfileadrL) {
        sprintf(xorriso->info_text, "-setfacl_list: Oversized file path");
        Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
        ret= 0; goto ex;
      }
      strcpy(file_path, line + 8);
 continue;
   } else if(strncmp(line, "# owner: ", 9) == 0) {
      if(strlen(line + 9) > 160) {
        sprintf(xorriso->info_text, "-setfacl_list: Oversized owner id");
        Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
        ret= 0; goto ex;
      }
      strcpy(uid, line + 9);
 continue;
   } else if(strncmp(line, "# group: ", 9) == 0) {
      if(strlen(line + 9) > 160) {
        sprintf(xorriso->info_text, "-setfacl_list: Oversized group id");
        Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
        ret= 0; goto ex;
      }
      strcpy(gid, line + 9);
 continue;
   } else if(line[0] == '#' || line[0] == 0) {
 continue;
   } else if(strcmp(line, "@") == 0) {
     Xorriso_msgs_submit(xorriso, 0,
                "-setfacl_list input ended by '@'", 0, "NOTE", 0);
 break;
   } else if(strcmp(line, "@@@") == 0) {
     Xorriso_msgs_submit(xorriso, 0,
                "-setfacl_list aborted by input line '@@@'", 0, "WARNING", 0);
     ret= 0; goto ex;
   }

   /* Register ACL entry */
   l= strlen(line);
   if(wpt + l + 2 - buf > (int) buf_size) {
     if((int) (buf_size + buf_add) > xorriso->temp_mem_limit) {
       Sfile_scale((double) xorriso->temp_mem_limit, limit_text,5,1e4,1);
       sprintf(xorriso->info_text,
      "-setfacl_list: List entry for a single file exceeds -temp_mem_limit %s",
              limit_text);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
     }
     buf_size+= buf_add;
     new_buf= realloc(buf, buf_size);
     if(new_buf == NULL)
       goto out_of_mem;
     buf= new_buf;
   }
   memcpy(wpt, line, l);
   *(wpt + l)= '\n';
   wpt+= l + 1;
   *wpt= 0;
 }
 if(wpt != buf && file_path[0]) {
   /* Commit last list */
   ret= Xorriso_perform_acl_from_list(xorriso, file_path, uid, gid, buf, 0);
   if(ret<=0)
     goto ex;
 } else {
   sprintf(xorriso->info_text, "-setfacl_list: Unexpected end of file ");
   Text_shellsafe(path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
 }
 ret= 1;
ex:;
 if(buf != NULL)
   free(buf);
 if(fp != NULL && fp != stdin)
   fclose(fp);
 if(ret <= 0) {
   sprintf(xorriso->info_text, "-setfacl_list ");
   Text_shellsafe(path, xorriso->info_text, 1);
   sprintf(xorriso->info_text + strlen(xorriso->info_text),
           " aborted in line %.f\n", (double) linecount);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
 }
 Xorriso_free_meM(line);
 Xorriso_free_meM(file_path);
 Xorriso_free_meM(uid);
 Xorriso_free_meM(gid);
 return(ret);
out_of_mem:;
 Xorriso_no_malloc_memory(xorriso, &buf, 0);
 ret= -1;
 goto ex;
}


/* Options -setfacl alias -setfacli, -setfacl_r alias -setfacl_ri */
/* @param flag   bit0=recursive -setfacl_r
*/
int Xorriso_option_setfacli(struct XorrisO *xorriso, char *acl_text,
                            int argc, char **argv, int *idx, int flag)
{
 int i, ret, was_failure= 0, end_idx, fret;
 int optc= 0;
 char **optv= NULL, *access_acl_text= NULL, *default_acl_text= NULL;
 struct FindjoB *job= NULL;
 struct stat dir_stbuf;

 ret= Xorriso_opt_args(xorriso, "-setfacl", argc, argv, *idx, &end_idx, &optc,
                       &optv, 0);
 if(ret <= 0)
   goto ex;

 ret= Xorriso_normalize_acl_text(xorriso, acl_text,
                                 &access_acl_text, &default_acl_text, 0);
 if(access_acl_text != NULL && default_acl_text != NULL) {
   sprintf(xorriso->info_text, "Access-ACL :\n%s", access_acl_text);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
   sprintf(xorriso->info_text, "Default-ACL :\n%s", default_acl_text);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
 } else if(access_acl_text == NULL && default_acl_text == NULL) {
   sprintf(xorriso->info_text, "Will delete Access-ACL and Default-ACL");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
 }
 if(ret <= 0)
   goto ex;

 for(i= 0; i<optc; i++) {
   if(flag&1) {
     ret= Findjob_new(&job, optv[i], 0);
     if(ret<=0) {
       Xorriso_no_findjob(xorriso, "-setfacl_r", 0);
       {ret= -1; goto ex;}
     }
     Findjob_set_action_text_2(job, 25, access_acl_text, default_acl_text, 0);
     ret= Xorriso_findi(xorriso, job, NULL, (off_t) 0,
                        NULL, optv[i], &dir_stbuf, 0, 0);
     Findjob_destroy(&job, 0);
   } else {
     ret= 1;
     if(access_acl_text == NULL || access_acl_text[0] ||
        default_acl_text == NULL || default_acl_text[0])
       ret= Xorriso_setfacl(xorriso, NULL, optv[i],
                            access_acl_text, default_acl_text, 0);
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
 Xorriso_opt_args(xorriso, "-setfacl", argc, argv, *idx, &end_idx,
                  &optc, &optv, 256);
 Findjob_destroy(&job, 0);
 if(access_acl_text != NULL)
   free(access_acl_text);
 if(default_acl_text != NULL)
   free(default_acl_text);
 if(ret<=0)
   return(ret);
 return(!was_failure);
}


/* Options -setfattr alias -setfattri, -setfattr_r alias -setfattr_ri */
/* @param flag   bit0=recursive -setfattr_r
*/
int Xorriso_option_setfattri(struct XorrisO *xorriso, char *name, char *value,
                            int argc, char **argv, int *idx, int flag)
{
 int i, ret, was_failure= 0, end_idx, fret;
 int optc= 0;
 char **optv= NULL;
 struct FindjoB *job= NULL;
 struct stat dir_stbuf;

 ret= Xorriso_opt_args(xorriso, "-setfattr", argc, argv, *idx, &end_idx, &optc,
                       &optv, 0);
 if(ret <= 0)
   goto ex;

 /* check input */
 ret= Xorriso_path_setfattr(xorriso, NULL, "", name, strlen(value), value, 1);
 if(ret <= 0)
    goto ex;
 
 for(i= 0; i<optc; i++) {
   if(flag&1) {
     ret= Findjob_new(&job, optv[i], 0);
     if(ret<=0) {
       Xorriso_no_findjob(xorriso, "-setfattr_r", 0);
       {ret= -1; goto ex;}
     }
     Findjob_set_action_text_2(job, 27, name, value, 0);
     ret= Xorriso_findi(xorriso, job, NULL, (off_t) 0,
                        NULL, optv[i], &dir_stbuf, 0, 0);
     Findjob_destroy(&job, 0);
   } else {
     ret= 1;
     ret= Xorriso_path_setfattr(xorriso, NULL, optv[i],
                                name, strlen(value), value, 0);
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
 Xorriso_opt_args(xorriso, "-setfattr", argc, argv, *idx, &end_idx,
                  &optc, &optv, 256);
 Findjob_destroy(&job, 0);
 if(ret<=0)
   return(ret);
 return(!was_failure);
}


/* Option -setfattr_list alias -setfattr_listi */
int Xorriso_option_setfattr_listi(struct XorrisO *xorriso, char *path,
                                  int flag)
{
 int ret, eaten, line_size= SfileadrL * 4;
 size_t linecount= 0, mem_used= 0, num_attr= 0, v_len;
 char *line= NULL, limit_text[80], *ept, *valuept;
 char *file_path= NULL;
 FILE *fp= NULL;
 struct Xorriso_lsT *lst_curr= NULL, *lst_start= NULL;

 Xorriso_alloc_meM(line, char, line_size);
 Xorriso_alloc_meM(file_path, char, SfileadrL);

 Xorriso_pacifier_reset(xorriso, 0);
 if(path[0]==0) {
   sprintf(xorriso->info_text, "Empty file name given with -setfattr_list");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }
 ret= Xorriso_afile_fopen(xorriso, path, "rb", &fp, 0);
 if(ret <= 0)
   {ret= 0; goto ex;}

 while(1) {
   if(Sfile_fgets_n(line, line_size, fp, 0) == NULL)
 break;
   linecount++;
   if(strncmp(line, "# file: ", 8) ==0) {
      if(num_attr > 0 && file_path[0]) {
        /* Commit previous list */
        ret= Xorriso_perform_attr_from_list(xorriso, file_path, lst_start, 0);
        if(ret<=0)
          goto ex;
        num_attr= 0;
        file_path[0]= 0;
        Xorriso_lst_destroy_all(&lst_start, 0);
        lst_curr= NULL;
      }
      /* Unescape line and register as file path */
      Sfile_bsl_interpreter(line + 8, strlen(line + 8), &eaten, 0);
      if(strlen(line + 8) >= SfileadrL) {
        sprintf(xorriso->info_text, "-setfattr_list: Oversized file path");
        Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
        ret= 0; goto ex;
      }
      strcpy(file_path, line + 8);
 continue;
   } else if(line[0] == '#' || line[0] == 0) {
 continue;
   } else if(strcmp(line, "@") == 0) {
     Xorriso_msgs_submit(xorriso, 0,
                "-setfattr_list input ended by '@'", 0, "NOTE", 0);
 break;
   } else if(strcmp(line, "@@@") == 0) {
     Xorriso_msgs_submit(xorriso, 0,
                "-setfattr_list aborted by input line '@@@'", 0, "WARNING", 0);
     ret= 1; goto ex;
   }
   mem_used+= strlen(line) + 1;
   if(mem_used > (size_t) xorriso->temp_mem_limit) {
     Sfile_scale((double) xorriso->temp_mem_limit, limit_text,5,1e4,1);
     sprintf(xorriso->info_text,
     "-setfattr_list: List entry for a single file exceeds -temp_mem_limit %s",
              limit_text);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }

   /* Register attr pair */

   ept= strchr(line, '=');
   if(ept == NULL) {
     sprintf(xorriso->info_text, "-setfattr_list: ");
     Text_shellsafe(path, xorriso->info_text, 1);
     sprintf(xorriso->info_text + strlen(xorriso->info_text),
             " : Line %.f : No separator '=' found",
             (double) linecount);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
 continue;
   }
   valuept= ept + 1;
   v_len= strlen(valuept);
   for(ept= valuept + v_len - 1; ept > valuept; ept--)
     if(isspace(*ept))
       *ept= 0;
     else
   break;
   v_len= strlen(valuept);
   if(v_len < 2 || *valuept != '"' || *(valuept + v_len -1) != '"') {
     sprintf(xorriso->info_text, "-setfattr_list: ");
     Text_shellsafe(path, xorriso->info_text, 1);
     sprintf(xorriso->info_text + strlen(xorriso->info_text),
             " : Line %.f : Value not enclosed in quotes",
             (double) linecount);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);

 continue;
   }

   ret= Xorriso_lst_new(&lst_curr, line, lst_curr, 0);
   if(ret <= 0)
     goto out_of_mem;
   if(lst_start == NULL)
     lst_start= lst_curr;
   num_attr++;
 }

 if(file_path[0]) {
   /* Commit last list */
   ret= Xorriso_perform_attr_from_list(xorriso, file_path, lst_start, 0);
   if(ret<=0)
     goto ex;
 } else {
   sprintf(xorriso->info_text, "-setfattr_list: Unexpected end of file ");
   Text_shellsafe(path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
 }
 ret= 1;
ex:;
 if(fp != NULL && fp != stdin)
   fclose(fp);
 Xorriso_lst_destroy_all(&lst_start, 0);
 Xorriso_free_meM(line);
 Xorriso_free_meM(file_path);
 if(ret <= 0) {
   sprintf(xorriso->info_text, "-setfattr_list ");
   Text_shellsafe(path, xorriso->info_text, 1);
   sprintf(xorriso->info_text + strlen(xorriso->info_text),
           " aborted in line %.f\n", (double) linecount);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
 }
 return(ret);
out_of_mem:;
 Xorriso_no_malloc_memory(xorriso, NULL, 0);
 ret= -1;
 goto ex;
}


/* Options -set_filter , -set_filter_r , -show_stream , -show_stream_r */
/* @param flag   bit0=recursive -set_filter_r
                 bit1= do not reset pacifier, no final pacifier message
                 bit2= -show_stream rather than -set_filter
*/
int Xorriso_option_set_filter(struct XorrisO *xorriso, char *name,
                              int argc, char **argv, int *idx, int flag)
{
 int i, ret, was_failure= 0, end_idx, fret;
 int optc= 0;
 char **optv= NULL;
 struct FindjoB *job= NULL;
 struct stat dir_stbuf;
 char *cmd= "-set_filter";

 switch(flag & 5) {
 case 0: cmd= "-set_filter";
 break; case 1: cmd= "-set_filter_r";
 break; case 4: cmd= "-show_stream";
 break; case 5: cmd= "-show_stream_r";
 }

 ret= Xorriso_opt_args(xorriso, cmd,
                       argc, argv, *idx, &end_idx, &optc, &optv, 0);
 if(ret <= 0)
   goto ex;
 if(!(flag&2))
   Xorriso_pacifier_reset(xorriso, 0);

 for(i= 0; i<optc; i++) {
   if(flag&1) {
     ret= Findjob_new(&job, optv[i], 0);
     if(ret<=0) {
       Xorriso_no_findjob(xorriso, cmd, 0);
       {ret= -1; goto ex;}
     }
     Findjob_set_action_target(job, ((flag & 4) ? 29 : 28), name, 0);
     Findjob_set_file_type(job, 'f', 0);
     ret= Xorriso_findi(xorriso, job, NULL, (off_t) 0,
                        NULL, optv[i], &dir_stbuf, 0, 0);
     Findjob_destroy(&job, 0);
   } else {
     ret= 1;
     if(flag & 4)
       ret= Xorriso_show_stream(xorriso, NULL, optv[i], 0);
     else
       ret= Xorriso_set_filter(xorriso, NULL, optv[i], name, 0);
   }
   if(ret>0 && !xorriso->request_to_abort)
 continue; /* regular bottom of loop */
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret>=0)
 continue;
   ret= 0; goto ex;
 }
 if(!(flag&2))
   Xorriso_pacifier_callback(xorriso, "file filters processed",
                             xorriso->pacifier_count, 0, "", 1);
 ret= 1;
ex:;
 (*idx)= end_idx;
 Xorriso_opt_args(xorriso, cmd, argc, argv, *idx, &end_idx,
                  &optc, &optv, 256);
 Findjob_destroy(&job, 0);
 if(ret<=0)
   return(ret);
 return(!was_failure);
}


/* Option -signal_handling */
/* @param flag bit0= prescan mode: do not yet install the eventual handler
                     else: when switching from other modes to "off":
                           activate mode "sig_dfl"
*/
int Xorriso_option_signal_handling(struct XorrisO *xorriso, char *mode,
                                   int flag)
{
 int ret, behavior;

 if (strcmp(mode, "off") == 0) {
   behavior= Xorriso__get_signal_behavior(0);
   if(flag & 1) {
     behavior= 0;
   } else if(behavior != 0) {
     sprintf(xorriso->info_text,
    "Signal handling mode \"off\" comes too late. Defaulted to \"sig_dfl\"\n");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
     behavior= 2;
   }
 } else if(strcmp(mode, "libburn") == 0 || strcmp(mode, "on") == 0) {
   behavior= 1;
 } else if (strcmp(mode, "sig_dfl") == 0) {
   behavior= 2;
 } else if (strcmp(mode, "sig_ign") == 0) {
   behavior= 3;
 } else {
   sprintf(xorriso->info_text, "-signal_handling: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   Xorriso_msgs_submit(xorriso, 0,
           "Use one of: \"off\",\"on\",\"sig_dfl\",\"sig_ign\"", 0, "HINT", 0);
   return(0);
 }
 Xorriso__preset_signal_behavior(behavior, 0);
 if(flag & 1)
   return(1);
 ret= Xorriso_set_signal_handling(xorriso, 0);
 return(ret);
}


/* Option -sleep */
int Xorriso_option_sleep(struct XorrisO *xorriso, char *duration, int flag)
{
 double dur= 0.0, start_time, end_time, todo, granularity= 0.01;
 unsigned long usleep_time; 

 sscanf(duration, "%lf", &dur);
 start_time= Sfile_microtime(0);
 end_time= start_time + dur;
 Ftimetxt(time(NULL), xorriso->info_text, 6);
 sprintf(xorriso->info_text + strlen(xorriso->info_text),
         " : Will sleep for %f seconds", dur);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "UPDATE", 0);
 while(1) {
   todo= end_time - Sfile_microtime(0);
   if(todo <= 0)
     usleep_time= 0;
   else if(todo > granularity)
     usleep_time= granularity * 1.0e6;
   else
     usleep_time= todo * 1.0e6;
   if(usleep_time == 0)
 break;
   usleep(usleep_time);
 }
 sprintf(xorriso->info_text, "Slept for %f seconds",
         Sfile_microtime(0) - start_time);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
 return(1);
}


/* Option -speed */
int Xorriso_option_speed(struct XorrisO *xorriso, char *speed, int flag)
{
 int is_cd= 1, unit_found= 0, ret, profile_number;
 double num;
 char *cpt, profile_name[80];

 if(speed[0]==0 || strcmp(speed, "any")==0) {
   xorriso->speed= 0; /* full speed */
   return(1);
 }

 sscanf(speed,"%lf",&num);
 for(cpt= speed+strlen(speed)-1; cpt>=speed; cpt--)
   if(isdigit(*cpt) || *cpt=='.')
 break;
 cpt++;

 if(*cpt=='k' || *cpt=='K') {
   /* is merchand kilobyte, stays merchand kilobyte */
   unit_found= 1;
 } else if(*cpt=='m' || *cpt=='M') {
   num*= 1000;
   unit_found= 1;
 } else if(*cpt=='x' || *cpt=='X')
   cpt++;

 if (unit_found) {
   ;
 } else if(*cpt=='c' || *cpt=='C') {
cd_speed:;
   num*= 176.4;
 } else if(*cpt=='d' || *cpt=='D') {
dvd_speed:;
   num*= 1385;
 } else if(*cpt=='b' || *cpt=='B') {
bd_speed:;
   num*= 4495.625;
 } else {
   ret= Xorriso_get_profile(xorriso, &profile_number, profile_name, 2);
   is_cd= (ret==2);
   if(is_cd)
     goto cd_speed;
   else if (ret == 3)
     goto bd_speed;
   else
     goto dvd_speed;
 }

 if(num> 2.0e9) {
   sprintf(xorriso->info_text,
           "-speed: Value too large or not recognizable: '%s'", speed);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 xorriso->speed= num;
 if(xorriso->speed<num)
   xorriso->speed++;
 return(1);
}


/* Option -split_size */
int Xorriso_option_split_size(struct XorrisO *xorriso, char *size, int flag)
{
 double num;

 num= Scanf_io_size(size, 0);
 if(num > xorriso->file_size_limit && xorriso->file_size_limit > 0) {
   sprintf(xorriso->info_text, "-split_size: too large %.f (allowed: %.f)",
           num, (double) xorriso->file_size_limit);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 } else if(num < 0)
   num= 0.0;
 xorriso->split_size= num;
 return(1);
}


/* Option -status */
int Xorriso_option_status(struct XorrisO *xorriso, char *mode, int flag)
{
 if(strcmp(mode,"short")==0)
   Xorriso_status(xorriso,NULL,NULL,1);
 else if(strcmp(mode,"long")==0)
   Xorriso_status(xorriso,NULL,NULL,0);
 else if(strcmp(mode,"long_history")==0)
   Xorriso_status(xorriso,NULL,NULL,8);
 else if(mode[0]=='-')
   Xorriso_status(xorriso,mode,NULL,8);
 else
   Xorriso_status(xorriso,NULL,NULL,1);
 return(1);
}


/* Option -status_history_max */
int Xorriso_option_status_history_max(struct XorrisO *xorriso, int num,
                                      int flag)
{
 if(num>=0 && num<1000000)
   xorriso->status_history_max= num;
 return(1);
}


/* Option -stdio_sync "on"|"off"|size */
int Xorriso_option_stdio_sync(struct XorrisO *xorriso, char *rythm, int flag)
{
 double num;

 if(strcmp(rythm, "default") == 0 || strcmp(rythm, "on") == 0)
   num= 0;
 if(strcmp(rythm, "off") == 0)
   num= -1;
 else
   num = Scanf_io_size(rythm, 0);
 if(num > 0)
   num /= 2048;
 if(num != -1 && num != 0 && (num < 32 || num > 512 * 1024)) {
     sprintf(xorriso->info_text,
             "-stdio_sync : Bad size. Acceptable are -1, 0, 32k ... 1g");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
     return(0);
 } else
   xorriso->stdio_sync= num;
 return(1);
}


/* Option -stream_recording */
int Xorriso_option_stream_recording(struct XorrisO *xorriso, char *mode,
                                    int flag)
{
 double num;

 if(strcmp(mode,"on")==0 || mode[0]==0)
   xorriso->do_stream_recording= 32;
 else if(strcmp(mode,"full")==0)
   xorriso->do_stream_recording= 1;
 else if(strcmp(mode,"data")==0)
   xorriso->do_stream_recording= 2;
 else if(mode[0] >= '0' && mode[0] <= '9') {
   num= Scanf_io_size(mode, 0);
   num/= 2048.0;
   if(num >= 16 && num <= 0x7FFFFFFF)
     xorriso->do_stream_recording= num;
   else
     xorriso->do_stream_recording= 0;
 } else
   xorriso->do_stream_recording= 0;
 return(1);
}


/* Option -system_id */
int Xorriso_option_system_id(struct XorrisO *xorriso, char *name, int flag)
{
  if(Xorriso_check_name_len(xorriso, name, (int) sizeof(xorriso->system_id),
                            "-system_id", 0) <= 0)
    return(0);
 strcpy(xorriso->system_id, name);
 Xorriso_set_change_pending(xorriso, 1);
 return(1);
}


/* Option -tell_media_space */
int Xorriso_option_tell_media_space(struct XorrisO *xorriso, int flag)
{
 int ret, free_space= 0, media_space= 0;

 ret= Xorriso_tell_media_space(xorriso, &media_space, &free_space, 0);
 if(ret<=0) {
   sprintf(xorriso->info_text, "Cannot -tell_media_space");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 if(free_space<0) {
   sprintf(xorriso->info_text,
           "Pending image size larger than free space on medium");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
 }
 sprintf(xorriso->result_line, "Media space  : %ds\n", media_space);
 Xorriso_result(xorriso, 0);
 sprintf(xorriso->result_line, "After commit : %ds\n", free_space);
 Xorriso_result(xorriso, 0);
 return(1);
}


/* Option -temp_mem_limit */
int Xorriso_option_temp_mem_limit(struct XorrisO *xorriso, char *size,
                                  int flag)
{
 double num;

 num= Scanf_io_size(size, 0);
 if(num < 64.0 * 1024.0 || num > 1024.0 * 1024.0 * 1024.0) {
   sprintf(xorriso->info_text,
           "-temp_mem_limit: wrong size %.f (allowed: %.f - %.f)",
           num, 64.0 * 1024.0, 1024.0 * 1024.0 * 1024.0);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 }
 xorriso->temp_mem_limit= num;
 return(1);
}


/* Option -toc */
/* @param flag   bit0= short report form as with -dev, no table-of-content
*/
int Xorriso_option_toc(struct XorrisO *xorriso, int flag)
{
 int ret, in_ret= 1000;

 if(strcmp(xorriso->indev,xorriso->outdev)==0)
   ret= Xorriso_toc(xorriso, 0);
 else {
   if(xorriso->indev[0]!=0)
     in_ret= Xorriso_toc(xorriso, 0);
   if(xorriso->indev[0]!=0 && xorriso->outdev[0]!=0) {
     strcpy(xorriso->result_line, "-------------: ---------------------------------------------------------------\n");
     Xorriso_result(xorriso,0);
   }
   ret= 1;
   if(xorriso->outdev[0]!=0)
     ret= Xorriso_toc(xorriso, 2 | (flag & 1));
   if(in_ret<ret)
     ret= in_ret;
 }
 return(ret);
}


/* Option -toc_of */
int Xorriso_option_toc_of(struct XorrisO *xorriso, char *which, int flag)
{
 int ret= 0, toc_flag= 0;

 if(strstr(which, ":short") != NULL)
   toc_flag|= 1;
 if(strncmp(which, "in", 2) == 0) {
   if(xorriso->indev[0] == 0) {
     Xorriso_msgs_submit(xorriso, 0, "-toc_of 'in' : No input drive aquired",
                         0, "NOTE", 0);
     return(2);
   }
   ret= Xorriso_toc(xorriso, toc_flag | 0);
 } else if(strncmp(which, "out", 3) == 0) {
   if(xorriso->outdev[0] == 0) {
     Xorriso_msgs_submit(xorriso, 0, "-toc_of 'out' : No output drive aquired",
                         0, "NOTE", 0);
     return(2);
   }
   ret= Xorriso_toc(xorriso, toc_flag | 2);
 } else if(strncmp(which, "all", 3) == 0) {
   if(xorriso->indev[0] == 0 && xorriso->outdev[0] == 0) {
     Xorriso_msgs_submit(xorriso, 0, "-toc_of 'all' : No drive aquired",
                         0, "NOTE", 0);
     return(2);
   }
   ret= Xorriso_option_toc(xorriso, toc_flag | 0);
 } else {
   sprintf(xorriso->info_text, "-toc_of: Unknown drive code ");
   Text_shellsafe(which, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   ret= 0;
 }
 return(ret);
}


/* Option -uid */
int Xorriso_option_uid(struct XorrisO *xorriso, char *uid, int flag)
{
 int ret;

 xorriso->do_global_uid= 0;
 if(uid[0]==0 || strcmp(uid,"-")==0)
   return(1);
 ret= Xorriso_convert_uidstring(xorriso, uid, &(xorriso->global_uid), 0);
 if(ret>0) 
   xorriso->do_global_uid= 1;
 return(ret);
}


/* Option -unregister_filter */
int Xorriso_option_unregister_filter(struct XorrisO *xorriso, char *name,
                                     int flag)
{
 int ret;

 ret= Xorriso_external_filter(xorriso, name, "", "", 0, NULL, 1);
 return(ret);
}


/* Options -update and -update_r
   @param flag bit0= issue start and summary message
               bit1= do not reset pacifier, no final pacifier message
               bit2= do not issue pacifier messages at all
               bit3= recursive: -update_r
               bit4= do not establish and dispose xorriso->di_array
               bit5= do not delete files which are not found under
                     disk_path, but rather mark visited files and mark
                     files which were found.
*/
int Xorriso_option_update(struct XorrisO *xorriso, char *disk_path,
                          char *iso_path, int flag)
{
 int ret, mem_pci, zero= 0, result, uret, follow_links, do_register= 0;
 int not_in_iso= 0, not_on_disk= 0;
 double mem_lut= 0.0, start_time;
 char *ipth, *argv[6];
 char *eff_origin= NULL, *eff_dest= NULL;
 struct stat stbuf;

 Xorriso_alloc_meM(eff_origin, char, SfileadrL);
 Xorriso_alloc_meM(eff_dest, char, SfileadrL);

 start_time= Sfile_microtime(0);

 ipth= iso_path;
 if(ipth[0]==0)
   ipth= disk_path;
 if(disk_path[0]==0) {
   sprintf(xorriso->info_text, "-update: Empty disk_path given");
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

 if(!(flag&2)) {
   Xorriso_pacifier_reset(xorriso, 0);
   mem_lut= xorriso->last_update_time;
 }
 mem_pci= xorriso->pacifier_interval;
 xorriso->pacifier_interval= 5.0;

 if(flag&1) {
   sprintf(xorriso->info_text, "Updating ");
   Text_shellsafe(eff_origin, xorriso->info_text, 1);
   strcat(xorriso->info_text, " to ");
   Text_shellsafe(eff_dest, xorriso->info_text, 1 | 2);
   strcat(xorriso->info_text, "\n");
   Xorriso_info(xorriso,0);
 }
 if(xorriso->disk_excl_mode&8)
   ret= Xorriso_path_is_excluded(xorriso, eff_origin, 1);
 else
   ret= 0;
 if(ret!=0)
   goto report_outcome;

 if(!(xorriso->ino_behavior & 2)) {
   if(!(xorriso->di_array != NULL || (flag & 16))) {
     /* Create all-image node array sorted by isofs.di */
     ret= Xorriso_make_di_array(xorriso, 0);
     if(ret <= 0)
       goto ex;
   }
   if(xorriso->di_array != NULL) {
     do_register= 1;
     if(!(flag & 8)) {
       /* If directory with -update : do not register di_*_paths */
       ret= lstat(eff_origin, &stbuf);
       if(ret != -1)
         if(S_ISDIR(stbuf.st_mode))
           do_register= 0;
     }
   }
 }

 if(flag&8) {
   xorriso->find_compare_result= 1;
   ret= Xorriso_iso_lstat(xorriso, eff_dest, &stbuf, 0);
   if(ret >= 0) {
     argv[0]= eff_dest;
     argv[1]= "-exec";
     if(flag & 32)
       argv[2]= "update_merge";
     else
       argv[2]= "update";
     argv[3]= eff_origin;
     zero= 0;
     ret= Xorriso_option_find(xorriso, 4, argv, &zero,
                      2 | (8 * !((xorriso->do_aaip & 96) == 96))); /* -findi */
   } else if(ret==-2) { /* severe error (e.g. lack of image) */
     ret= -1;
     goto report_outcome;
   } else {
     not_in_iso= 1;
     ret= 1;
   }
   if(ret>0) {
     ret= lstat(eff_origin, &stbuf);
     if(ret != -1) {
       argv[0]= eff_origin;
       argv[1]= "-exec";
       argv[2]= "add_missing";
       argv[3]= eff_dest;
       zero= 0;
       ret= Xorriso_option_find(xorriso, 4, argv, &zero, 1|2); /* -findx */
       if(ret>0 && (!xorriso->do_follow_mount) && !(flag & 32)) {

         /* >>> ??? what about mount points with (flag & 32) ?
                empty_iso_dir shall delete those which already existed
                and are freshly excluded. (E.g. by mounting at a non-empty
                directory, or by new follow rules.)
                This deletion does not match the idea of merging.
                For determining the foreign files in a directory which is
                target of a mount point, one would have to enter that mount
                point directory. Somewhat contrary to do-not-follow.
         */

         argv[0]= eff_origin;
         argv[1]= "-type";
         argv[2]= "m";
         argv[3]= "-exec";
         argv[4]= "empty_iso_dir";
         argv[5]= eff_dest;
         zero= 0;
         ret= Xorriso_option_find(xorriso, 6, argv, &zero, 1|2); /* -findx */
       }
       if(ret>0)
         ret= xorriso->find_compare_result;
       else
         ret= -1;
     } else {
       ret= xorriso->find_compare_result;
       not_on_disk= 1;
     }
   } else
     ret= -1;
   if(not_on_disk && not_in_iso) {
     sprintf(xorriso->info_text, "Missing on disk and in ISO: disk_path ");
     Text_shellsafe(disk_path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 1);
     ret= -1;
   }
 } else {
   if(flag & 32)
     xorriso->update_flags|= 1; /* Enter update_merge mode for node adding */
   /* compare ctime too, no filename reporting, eventually silent */
   follow_links= (xorriso->do_follow_links || xorriso->do_follow_param) <<28;
   ret= Xorriso_compare_2_files(xorriso, eff_origin, eff_dest, "", &result,
                                2 | follow_links | ((flag&4)<<27) | (3<<30));
   if(ret == 0 || (ret > 0 && (flag & 32))) {
     if(ret > 0)
       result= 0;
     uret= Xorriso_update_interpreter(xorriso, NULL, NULL, result, eff_origin,
                                      eff_dest, (!!(flag & 32)) << 1);
     if(uret<=0)
       ret= -1;
     if(uret==3)
       ret= -1;
   }
 }
 xorriso->pacifier_interval= mem_pci;
 if(mem_lut!=xorriso->last_update_time && !(flag & (2 | 4)))
   Xorriso_pacifier_callback(xorriso, "content bytes read",
                             xorriso->pacifier_count, 0, "", 1 | 8 | 32);
report_outcome:;
 if(ret>0) {
   sprintf(xorriso->info_text,
           "No file object needed update.");
   do_register= 0;
 } else if(ret==0) {
   sprintf(xorriso->info_text, "Differences detected and updated.");
 } else {
   sprintf(xorriso->info_text,
           "Not ok. Comparison or update failed due to error.");
   do_register= 0;
 }

 if(do_register) {
   ret= Xorriso_iso_lstat(xorriso, eff_dest, &stbuf, 0);
   if(ret < 0)
     do_register= 0;
 }
 if(do_register) {
   ret= Xorriso_lst_new(&(xorriso->di_disk_paths), eff_origin,
                        xorriso->di_disk_paths, 1);
   if(ret <= 0)
     goto ex;
   ret= Xorriso_lst_new(&(xorriso->di_iso_paths), eff_dest,
                        xorriso->di_iso_paths, 1);
   if(ret <= 0)
     goto ex;
 }
 sprintf(xorriso->info_text+strlen(xorriso->info_text),
         " (runtime %.1f s)\n", Sfile_microtime(0)-start_time);
 if(flag&1)
   Xorriso_info(xorriso,0);

ex:;
 Xorriso_free_meM(eff_origin);
 Xorriso_free_meM(eff_dest);
 if(ret < 0)
   return(ret);
 return(1);
}


/* Option -use_readline */
int Xorriso_option_use_readline(struct XorrisO *xorriso, char *mode, int flag)
{
 if(strcmp(mode,"off")==0)
   xorriso->use_stdin= 1;
 else
   xorriso->use_stdin= 0;
 return(1);
}


/* Option -version */
int Xorriso_option_version(struct XorrisO *xorriso, int flag)
{
 sprintf(xorriso->result_line, "%sxorriso %d.%d.%d%s\n",
#ifdef Xorriso_GNU_xorrisO
         "GNU ",
#else
         "",
#endif /* ! Xorriso_GNU_xorrisO */
         Xorriso_header_version_majoR, Xorriso_header_version_minoR,
         Xorriso_header_version_micrO, Xorriso_program_patch_leveL);
 Xorriso_result(xorriso, 0);
 sprintf(xorriso->result_line,
"ISO 9660 Rock Ridge filesystem manipulator and CD/DVD/BD burn program\n");
 sprintf(xorriso->result_line+strlen(xorriso->result_line),
"Copyright (C) 2013, Thomas Schmitt <scdbackup@gmx.net>, libburnia project.\n");
 Xorriso_result(xorriso, 0);
 sprintf(xorriso->result_line,
         "xorriso version   :  %d.%d.%d%s\n",
         Xorriso_header_version_majoR, Xorriso_header_version_minoR,
         Xorriso_header_version_micrO, Xorriso_program_patch_leveL);
 Xorriso_result(xorriso, 0);
 sprintf(xorriso->result_line, "Version timestamp :  %s\n",Xorriso_timestamP);
 Xorriso_result(xorriso, 0);
 sprintf(xorriso->result_line,
         "Build timestamp   :  %s\n",Xorriso_build_timestamP);
 Xorriso_result(xorriso, 0);
 Xorriso_report_lib_versions(xorriso, 0);

#ifdef Xorriso_GNU_xorrisO
 sprintf(xorriso->result_line,
"License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n");
 Xorriso_result(xorriso, 0);
 sprintf(xorriso->result_line,
"This is free software: you are free to change and redistribute it.\n");
 Xorriso_result(xorriso, 0);
#else
 sprintf(xorriso->result_line, "Provided under GNU GPL version 2 or later.\n");
 Xorriso_result(xorriso, 0);
#endif /* ! Xorriso_GNU_xorrisO */

 sprintf(xorriso->result_line,
"There is NO WARRANTY, to the extent permitted by law.\n");
 Xorriso_result(xorriso, 0);
 return(1);
}


/* Option -volid */
/* @param flag bit0= do not warn of problematic volid
*/
int Xorriso_option_volid(struct XorrisO *xorriso, char *volid, int flag)
{
 int warn_shell= 0, warn_ecma= 0, i, ret;
 static char shell_chars[]= {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-+=:.,~@"};
 static char ecma_chars[]= {"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"};

 for(i=0; volid[i]!=0; i++) {
   if(strchr(shell_chars, volid[i])==NULL)
     warn_shell= 1;
   if(strchr(ecma_chars, volid[i])==NULL)
     warn_ecma= 1;
 }
 if(i>32) {
   sprintf(xorriso->info_text, "-volid: Text too long (%d > 32)", i);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 if(warn_shell && !(flag & 1)) {
   sprintf(xorriso->info_text,
           "-volid text problematic as automatic mount point name");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
 }
 if(xorriso->do_joliet && strlen(volid)>16 && !(flag & 1)) {
   sprintf(xorriso->info_text,
           "-volid text is too long for Joliet (%d > 16)",(int) strlen(volid));
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
 }
 if(warn_ecma && !(flag & 1)) {
   sprintf(xorriso->info_text,
          "-volid text does not comply to ISO 9660 / ECMA 119 rules");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
 }
 strcpy(xorriso->volid, volid);
 ret= Xorriso_set_volid(xorriso, volid, 0);
 if(ret<=0)
   return(ret);
 xorriso->volid_default= (strcmp(xorriso->volid, "ISOIMAGE")==0 ||
                          xorriso->volid[0]==0);
 return(1);
}


/* Option -volset_id */
int Xorriso_option_volset_id(struct XorrisO *xorriso, char *name, int flag)
{
  if(Xorriso_check_name_len(xorriso, name, (int) sizeof(xorriso->volset_id),
                            "-volset_id", 0) <= 0)
    return(0);
 strcpy(xorriso->volset_id, name);
 Xorriso_set_change_pending(xorriso, 1);
 return(1);
}


/* Option -volume_date */
int Xorriso_option_volume_date(struct XorrisO *xorriso,
                               char *time_type, char *timestring, int flag)
{
 int ret, t_type= 0;
 time_t t;
 struct tm erg;

 if(timestring[0] == 0 || strcmp(timestring, "default") == 0 ||
    strcmp(timestring, "overridden") == 0 ){
   t= 0;
 } else if(strcmp(time_type, "uuid") == 0) {
   t= time(NULL); /* Just to have some that is not 0 */
 } else {
   ret= Xorriso_convert_datestring(xorriso, "-volume_date",
                                   "m", timestring, &t_type, &t, 0);
   if(ret<=0)
     goto ex;
 }
 if(strcmp(time_type, "uuid") == 0) {
   if(t == 0) {
     xorriso->vol_uuid[0]= 0;
     ret= 1; goto ex;
   }
   ret= Decode_ecma119_format(&erg, timestring, 0);
   if(ret <= 0) {
     sprintf(xorriso->info_text, "-volume_date uuid : Not an ECMA-119 time string. (16 decimal digits, range 1970... to 2999...)");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }
   strcpy(xorriso->vol_uuid, timestring);
   if(erg.tm_year < 138) {
     sprintf(xorriso->info_text,
             "Understanding ECMA-119 timestring '%s' as:  %s",
             timestring, asctime(&erg));
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
   }

 } else if(strcmp(time_type, "c") == 0) {
   xorriso->vol_creation_time= t;
 } else if(strcmp(time_type, "m") == 0) {
   xorriso->vol_modification_time= t;
 } else if(strcmp(time_type, "x") == 0) {
   xorriso->vol_expiration_time= t;
 } else if(strcmp(time_type, "f") == 0) {
   xorriso->vol_effective_time= t;
 } else {

   /* >>> unknown time type */;

   ret= 0; goto ex;
 }
 ret= 1;
ex:;
 return(ret);
}


/* Command -write_type */
int Xorriso_option_write_type(struct XorrisO *xorriso, char *mode, int flag)
{
 if(strcmp(mode, "auto") == 0)
   xorriso->do_tao = 0;
 else if(strcmp(mode, "tao") == 0 || strcmp(mode, "TAO") == 0) 
   xorriso->do_tao = 1;
 else if(strcmp(mode, "sao") == 0 || strcmp(mode, "SAO") == 0 ||
         strcmp(mode, "dao") == 0 || strcmp(mode, "DAO") == 0 ||
         strcmp(mode, "sao/dao") == 0 || strcmp(mode, "SAO/DAO") == 0 ) 
   xorriso->do_tao = -1;
 else {
   sprintf(xorriso->info_text, "-write_type: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(1);
}


/* Option -xattr "on"|"off" */
int Xorriso_option_xattr(struct XorrisO *xorriso, char *mode, int flag)
{
 int ret;

 if(strcmp(mode, "off")==0)
   xorriso->do_aaip&= ~12;
 else if(strcmp(mode, "on")==0)
   xorriso->do_aaip|= (4 | 8);
 else {
   sprintf(xorriso->info_text, "-xattr: unknown mode '%s'", mode);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 ret= Xorriso_set_ignore_aclea(xorriso, 0);
 if(ret <= 0)
   return(ret);
 return(1);
}


/* Option -zisofs */
int Xorriso_option_zisofs(struct XorrisO *xorriso, char *mode, int flag)
{
 int was_level, was_blocksize, ret, l;
 double num;
 char *cpt, *npt, text[16];

 was_level= xorriso->zlib_level;
 was_blocksize= xorriso->zisofs_block_size;
 npt= cpt= mode;
 for(cpt= mode; npt!=NULL; cpt= npt+1) {
   npt= strchr(cpt,':');
   if(npt==NULL)
     l= strlen(cpt);
   else
     l= npt-cpt;
   if(l==0)
     goto unknown_mode;

   if(strncmp(cpt, "level=", 6) == 0) {
     sscanf(cpt + 6, "%lf", &num);
     if(num < 0 || num > 9) {
       sprintf(xorriso->info_text,
               "-zisofs: Wrong zlib compression level '%s' (allowed 0...9)",
               cpt + 6);
       goto sorry_ex;
     }
     xorriso->zlib_level= num;

   } else if(strncmp(cpt, "ziso_used=", 10) == 0 ||
             strncmp(cpt, "osiz_used=", 10) == 0) {
     /* (ignored info from -status) */;

   } else if(strncmp(cpt, "block_size=", 11)==0) {
     num= 0.0;
     if(l > 11 && l < 27) {
       strncpy(text, cpt + 11, l - 11);
       text[l - 11]= 0;
       num= Scanf_io_size(text, 0);
     }
     if (num != (1 << 15) && num != (1 << 16) && num != (1 << 17)) {
       sprintf(xorriso->info_text,
               "-zisofs: Unsupported block size (allowed 32k, 64k, 128k)");
       goto sorry_ex;
     }
     xorriso->zisofs_block_size= num;

   } else if(strncmp(cpt, "by_magic=", 8)==0) {
     if(strncmp(cpt + 9, "on", l - 9) == 0)
       xorriso->zisofs_by_magic= 1;
     else
       xorriso->zisofs_by_magic= 0;

   } else if(strncmp(cpt, "default", l)==0) {
     xorriso->zlib_level= xorriso->zlib_level_default;
     xorriso->zisofs_block_size= xorriso->zisofs_block_size_default;
     xorriso->zisofs_by_magic= 0;

   } else {
unknown_mode:;
     if(l<SfileadrL)
       sprintf(xorriso->info_text, "-zisofs: unknown mode '%s'", cpt);
     else
       sprintf(xorriso->info_text, "-zisofs: oversized mode parameter (%d)",l);
sorry_ex:
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     xorriso->zlib_level= was_level;
     xorriso->zisofs_block_size= was_blocksize;
     return(0);
   }
 }
 ret= Xorriso_set_zisofs_params(xorriso, 0);
 return(ret);
}


