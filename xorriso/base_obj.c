


/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2012 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains functions which are needed to read data
   from ISO image.
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
#include <pthread.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#endif

#ifdef Xorriso_standalonE

#ifdef Xorriso_with_libjtE
#include "../libjte/libjte.h"
#endif

#else

#ifdef Xorriso_with_libjtE
#include <libjte/libjte.h>
#endif

#endif /* ! Xorriso_standalonE */

#include "xorriso.h"
#include "xorriso_private.h"

#include "base_obj.h"
#include "lib_mgt.h"



/* See Xorriso__preset_signal_behavior() */
static int Xorriso_signal_behavioR= 1;


void Xorriso__version(int *major, int *minor, int *micro)
{
 *major= Xorriso_header_version_majoR;
 *minor= Xorriso_header_version_minoR;
 *micro= Xorriso_header_version_micrO;
}


int Xorriso__is_compatible(int major, int minor, int micro, int flag)
{
 int own_major, own_minor, own_micro;

 Xorriso__version(&own_major, &own_minor, &own_micro);
 return(own_major > major ||
        (own_major == major && (own_minor > minor ||
         (own_minor == minor && own_micro >= micro))));
}


char *Xorriso__get_patch_level_text(int flag)
{
 return(Xorriso_program_patch_leveL);
}


/** The list of startup file names */
#define Xorriso_rc_nuM 4

static char Xorriso_sys_rc_nameS[Xorriso_rc_nuM][80]= {
 "/etc/default/xorriso",
 "/etc/opt/xorriso/rc",
 "/etc/xorriso/xorriso.conf",
 "placeholder for $HOME/.xorrisorc"
};


int Xorriso_new(struct XorrisO ** xorriso,char *progname, int flag)
{
 int i, ret;
 struct XorrisO *m;
 char *leafname= NULL;

 leafname= TSOB_FELD(char, SfileadrL);
 if(leafname == NULL)
   return(-1); 
 *xorriso= m= TSOB_FELD(struct XorrisO,1);
 if(m==NULL) {
   free(leafname);
   return(-1);
 }
 m->libs_are_started= 0;
 strncpy(m->progname,progname,sizeof(m->progname)-1);
 m->progname[sizeof(m->progname)-1]= 0;
 if(getcwd(m->initial_wdx,sizeof(m->initial_wdx)-1)==NULL)
   m->initial_wdx[0]= 0;
 m->no_rc= 0;
 m->argument_emulation= 0;

 m->rc_filename_count= Xorriso_rc_nuM;
 for(i=0;i<m->rc_filename_count-1;i++)
   strcpy(m->rc_filenames[i],Xorriso_sys_rc_nameS[i]);
 m->rc_filenames[m->rc_filename_count-1][0]= 0;
 m->arrange_args= 0;
 m->mkisofsrc_done= 0;

 m->wdi[0]= 0;
 strcpy(m->wdx, m->initial_wdx);
 m->did_something_useful= 0;
 m->add_plainly= 0;
 m->split_size= 0;
 strcpy(m->list_delimiter, "--");
 m->ino_behavior= 1 | 2 | 4 | 32;      /* off:no_lsl_count */
 m->iso_level= 3;
 m->iso_level_is_default= 1;
 m->do_joliet= 0;
 m->do_hfsplus= 0;
 m->do_fat= 0;
 m->do_rockridge= 1;
 m->do_iso1999= 0;
 m->do_aaip= 0;
 m->do_md5= 0;
 m->no_emul_toc= 0;
 m->do_old_empty= 0;
 m->scdbackup_tag_name[0]= 0;
 m->scdbackup_tag_time[0]= 0;
 m->scdbackup_tag_written[0]= 0;
 m->scdbackup_tag_listname[0]= 0;
 m->relax_compliance= 0;
 m->allow_dir_id_ext_dflt= 1;
 m->rr_reloc_dir[0]= 0;
 m->rr_reloc_flags= 1;
 m->untranslated_name_len= 0;
 m->do_follow_pattern= 1;
 m->do_follow_param= 0;
 m->do_follow_links= 0;
 m->follow_link_limit= 100;
 m->do_follow_mount= 1;
 m->do_global_uid= 0;
 m->global_uid= 0;
 strcpy(m->volid, "ISOIMAGE");
 m->volid_default= 1;
 m->loaded_volid[0]= 0;
 m->assert_volid[0]= 0;
 m->assert_volid_sev[0]= 0;
 m->preparer_id[0]= 0;
 m->publisher[0]= 0;
 m->application_id[0]= 0;
 m->system_id[0]= 0;
 m->volset_id[0]= 0;
 m->copyright_file[0]= 0;
 m->biblio_file[0]= 0;
 m->abstract_file[0]= 0;
 m->session_logfile[0]= 0;
 m->session_lba= -1;
 m->session_blocks= 0;
 m->do_global_gid= 0;
 m->global_gid= 0;
 m->do_global_mode= 0;
 m->global_dir_mode= 0555;
 m->global_file_mode= 0444;
 m->do_tao= 0;
 m->filters= NULL;
 m->filter_list_closed= 0;
 m->zlib_level_default= m->zlib_level= 6;
 m->zisofs_block_size= m->zisofs_block_size_default= (1 << 15);
 m->zisofs_by_magic= 0;
 m->do_overwrite= 2;
 m->do_reassure= 0;
 m->drive_blacklist= NULL;
 m->drive_greylist= NULL;
 m->drive_whitelist= NULL;
 m->toc_emulation_flag= 0;
 m->image_start_mode= 0;
 m->image_start_value[0]= 0;
 m->displacement= 0;
 m->displacement_sign= 0;
 m->drives_exclusive= 1;
 m->early_stdio_test= 0;
 m->cache_num_tiles= 0;
 m->cache_tile_blocks= 0;
 m->cache_default= 1 | 2;
 m->do_calm_drive= 1;
 m->indev[0]= 0;
 m->in_drive_handle= NULL;
 m->in_volset_handle= NULL;
 m->in_charset= NULL;
 m->isofs_st_out= time(0) - 1;
 m->indev_is_exclusive= 1;
 m->indev_off_adr[0]= 0;
 m->isofs_st_in= 0;
 m->volset_change_pending= 0;
 m->no_volset_present= 0;
 m->in_sector_map= NULL;
 m->check_media_default= NULL;
 m->check_media_bad_limit= Xorriso_read_quality_invaliD;
 m->outdev[0]= 0;
 m->out_drive_handle= NULL;
 m->out_charset= NULL;
 m->dev_fd_1= -1;
 m->outdev_is_exclusive= 1;
 m->outdev_off_adr[0]= 0;
 m->grow_blindly_msc2= -1;
 m->ban_stdio_write= 0;
 m->do_dummy= 0;
 m->do_close= 0;
 m->speed= 0;
 m->fs= 4*512; /* 4 MiB */
 m->padding= 300*1024;
 m->do_padding_by_libisofs= 0;
 m->alignment= 0;
 m->do_stream_recording= 0;
 m->dvd_obs= 0;
 m->stdio_sync= 0;
 m->keep_boot_image= 0;
 m->boot_image_cat_path[0]= 0;
 m->boot_image_cat_hidden= 0;
 m->boot_count= 0;
 m->boot_platform_id= 0x00; /* El Torito Boot Catalog Platform ID: 0 = 80x86 */
 m->patch_isolinux_image= 0;
 m->boot_image_bin_path[0]= 0;
 m->boot_image_bin_form[0]= 0;
 m->boot_image_emul= 0;
 m->boot_emul_default= 1;
 m->boot_image_load_size= 4 * 512;     /* hearsay out of libisofs/demo/iso.c */
 memset(m->boot_id_string, 0, sizeof(m->boot_id_string));
 memset(m->boot_selection_crit, 0, sizeof(m->boot_selection_crit));

#ifdef Xorriso_with_isohybriD
 m->boot_image_isohybrid= 1;
#else
 m->boot_image_isohybrid= 0;
#endif

 m->boot_efi_default= 0;
 m->system_area_disk_path[0]= 0;
 m->system_area_options= 0;
 m->patch_system_area= 0;
 m->partition_offset= 0;
 m->partition_secs_per_head= 0;
 m->partition_heads_per_cyl= 0;
 m->prep_partition[0]= 0;
 m->efi_boot_partition[0]= 0;
 for(i= 0; i < Xorriso_max_appended_partitionS; i++) {
   m->appended_partitions[i]= NULL;
   m->appended_part_types[i]= 0;
 }
 m->ascii_disc_label[0]= 0;
 m->grub2_sparc_core[0]= 0;
 memset(m->hfsp_serial_number, 0, 8);
 m->hfsp_block_size= 0;
 m->apm_block_size= 0;
 m->vol_creation_time= 0;
 m->vol_modification_time= 0;
 m->vol_expiration_time= 0;
 m->vol_effective_time= 0;
 m->vol_uuid[0]= 0;

#ifdef Xorriso_with_libjtE
 m->libjte_handle= NULL;
#endif

 m->jigdo_params= NULL;
 m->jigdo_values= NULL;
 m->libjte_params_given= 0;
 m->loaded_boot_bin_lba= 0;
 m->loaded_boot_cat_path[0]= 0;
 m->allow_graft_points= 0;
 m->allow_restore= 0;
 m->do_concat_split= 1;
 m->do_auto_chmod= 0;
 m->do_restore_sort_lba= 0;
 m->do_strict_acl= 0;
 m->dialog= 0;
 m->bsl_interpretation= 0;
 m->search_mode= 0;
 m->structured_search= 1;
 m->do_iso_rr_pattern= 1;
 m->do_disk_pattern= 2;
 m->temp_mem_limit= 16*1024*1024;
 m->file_size_limit= Xorriso_default_file_size_limiT;
 m->disk_exclusions= NULL;
 m->iso_rr_hidings= NULL;
 m->joliet_hidings= NULL;
 m->hfsplus_hidings= NULL;
 m->disk_excl_mode= 1;
 m->use_stdin= 0;
 m->result_page_length= 0;
 m->result_page_width= 80;
 m->mark_text[0]= 0;
 m->packet_output= 0;
 for(i=0; i<4; i++) {
   m->logfile[i][0]= 0;
   m->logfile_fp[i]= NULL;
 }
 m->pktlog_fp= NULL;
 m->stderr_fp= NULL;
 for(i= 0; i < Xorriso_max_outlist_stacK; i++) {
   m->result_msglists[i]= NULL;
   m->info_msglists[i]= NULL;
   m->msglist_flags[i]= 0;
 }
 m->lib_msg_queue_lock_ini= 0;
 m->result_msglists_lock_ini= 0;
 m->write_to_channel_lock_ini= 0;
 m->msg_watcher_lock_ini= 0;
 m->msg_watcher_state= 0;
 m->msgw_result_handler= NULL;
 m->msgw_result_handle= NULL;
 m->msgw_info_handler= NULL;
 m->msgw_info_handle= NULL;
 m->msgw_stack_handle= -1;
 m->msgw_msg_pending= 0;
 m->msgw_fetch_lock_ini= 0;
 m->msg_sieve= NULL;
 m->msg_sieve_disabled= 0;
 m->msglist_stackfill= 0;
 m->status_history_max= Xorriso_status_history_maX;
 m->scsi_log= 0;
 strcpy(m->report_about_text, "UPDATE");
 Xorriso__text_to_sev(m->report_about_text, &m->report_about_severity, 0);
 m->library_msg_direct_print= 0;
 strcpy(m->abort_on_text,"FAILURE");
 Xorriso__text_to_sev(m->abort_on_text, &m->abort_on_severity, 0);
 m->abort_on_is_default= 1;
 m->problem_status= 0;
 m->problem_status_lock_ini= 0;
 m->problem_status_text[0]= 0;
 m->errfile_log[0]= 0;
 m->errfile_mode= 0;
 m->errfile_fp= NULL;
 
 m->img_read_error_mode= 1;       /* abort faulty image reading with FAILURE */
 m->extract_error_mode= 1;          /* keep extracted files after read error */
 strcpy(m->return_with_text, "SORRY");
 Xorriso__text_to_sev(m->return_with_text, &m->return_with_severity, 0);
 m->return_with_value= 32;
 m->eternal_problem_status= 0;
 m->eternal_problem_status_text[0]= 0;
 m->re= NULL;
 /* >>> ??? how to initialize m->match[0] ? */
 m->re_constants= NULL;
 m->re_count= 0;
 m->re_fill= 0;
 m->reg_expr[0]= 0;
 m->run_state= 0;
 m->is_dialog= 0;
 m->bar_is_fresh= 0;
 m->pending_option[0]= 0;
 m->request_to_abort= 0;
 m->request_not_to_ask= 0;
 m->idle_time= 0.0;
 m->re_failed_at= -1;
 m->prepended_wd= 0;
 m->insert_count= 0;
 m->insert_bytes= 0;
 m->error_count= 0;
 m->launch_frontend_banned= 0;
 m->pacifier_style= 0;
 m->pacifier_interval= 1.0;
 m->pacifier_count= 0;
 m->pacifier_prev_count= 0;
 m->pacifier_total= 0;
 m->pacifier_byte_count= 0;
 m->pacifier_fifo= NULL;
 m->start_time= 0.0;
 m->last_update_time= 0.0;
 m->find_compare_result= 1;
 m->find_check_md5_result= 0;
 m->last_abort_file_time= 0.0;

 m->node_counter= 0;
 m->node_array_size= 0;
 m->node_array= NULL;
 m->node_disk_prefixes= NULL;
 m->node_img_prefixes= NULL;

 m->hln_count= 0;
 m->hln_array= NULL; 
 m->hln_targets= NULL;
 m->hln_change_pending= 0;
 m->di_do_widen= NULL;
 m->di_disk_paths= NULL;
 m->di_iso_paths= NULL;

 m->node_targets_availmem= 0;

 m->di_count= 0;
 m->di_array= NULL;

 m->perm_stack= NULL;

 m->update_flags= 0;

 m->result_line[0]= 0;
 m->result_line_counter= 0;
 m->result_page_counter= 0;
 m->result_open_line_len= 0;

 m->info_text[0]= 0;

 ret= Sfile_leafname(progname, leafname, 0);
 if(ret<=0)
   goto failure;
 if(strcmp(leafname, "osirrox")==0) {
   m->allow_restore= 1;
   m->drives_exclusive= 0;
 } else if(strcmp(leafname, "xorrisofs")==0 || strcmp(leafname, "genisofs")==0 ||
        strcmp(leafname, "mkisofs")==0 || strcmp(leafname, "genisoimage")==0) {
   m->argument_emulation= 1;
   m->pacifier_style= 1;
   Xorriso_protect_stdout(*xorriso, 0);
 } else if(strcmp(leafname, "xorrecord")==0 || strcmp(leafname, "wodim")==0 ||
           strcmp(leafname, "cdrecord")==0 || strcmp(leafname, "cdrskin")==0) {
   m->argument_emulation= 2;
   m->pacifier_style= 2;
 }
 ret= Exclusions_new(&(m->disk_exclusions), 0);
 if(ret<=0)
   goto failure;
 ret= Exclusions_new(&(m->iso_rr_hidings), 0);
 if(ret<=0)
   goto failure;
 ret= Exclusions_new(&(m->joliet_hidings), 0);
 if(ret<=0)
   goto failure;
 ret= Exclusions_new(&(m->hfsplus_hidings), 0);
 if(ret<=0)
   goto failure;
 Xorriso_relax_compliance(m, "default", 0);
 ret= Xorriso_lst_new(&(m->drive_greylist), "/dev", m->drive_greylist, 1);
 if(ret <= 0)
   goto failure;
 Xorriso_preparer_string(m, m->preparer_id, 1); /* avoids library calls */
 ret= pthread_mutex_init(&(m->lib_msg_queue_lock), NULL);
 if(ret != 0)
   goto failure;
 m->lib_msg_queue_lock_ini= 1;
 ret= pthread_mutex_init(&(m->result_msglists_lock), NULL);
 if(ret != 0)
   goto failure;
 m->result_msglists_lock_ini= 1;
 ret= pthread_mutex_init(&(m->write_to_channel_lock), NULL);
 if(ret != 0)
   goto failure;
 m->result_msglists_lock_ini= 1;
 ret= pthread_mutex_init(&(m->problem_status_lock), NULL);
 if(ret != 0)
   goto failure;
 m->problem_status_lock_ini= 1;
 ret= pthread_mutex_init(&(m->msg_watcher_lock), NULL);
 if(ret != 0)
   goto failure;
 m->msg_watcher_lock_ini= 1;
 ret= pthread_mutex_init(&(m->msgw_fetch_lock), NULL);
 if(ret != 0)
   goto failure;
 m->msgw_fetch_lock_ini= 1;

 if(leafname != NULL)
   free(leafname);
 return(1);
failure:;
 Xorriso_destroy(xorriso, 0);
 if(leafname != NULL)
   free(leafname);
 return(-1);
}


int Xorriso_destroy_re(struct XorrisO *m, int flag)
{
 int i;

 if(m->re!=NULL) {
   for(i=0;i<m->re_fill;i++) {
     if(m->re_constants!=NULL)
       if(m->re_constants[i]!=NULL)
   continue; /* ,->re[i] was never subject to regcomp() */
     regfree(&(m->re[i]));
   }
   free((char *) m->re);
   m->re= NULL;
 }

 if(m->re_constants!=NULL) {
   for(i=0;i<m->re_fill;i++)
     if(m->re_constants[i]!=NULL)
       free(m->re_constants[i]);
   free((char *) m->re_constants);
   m->re_constants= NULL;
 }
 m->re_count= 0;
 m->re_fill= 0;
 return(1);
}


/* @param flag bit0= global shutdown of libraries */
int Xorriso_destroy(struct XorrisO **xorriso, int flag)
{
 struct XorrisO *m;
 int i;

 m= *xorriso;
 if(m==NULL)
   return(0);

 /* Give up drives and image to unref all connected xorriso objects */
 Xorriso_give_up_drive(m, 3);

 if(m->in_charset!=NULL)
   free(m->in_charset);
 if(m->out_charset!=NULL)
   free(m->out_charset);
 Xorriso_destroy_re(m,0);
 Exclusions_destroy(&(m->disk_exclusions), 0);
 Exclusions_destroy(&(m->iso_rr_hidings), 0);
 Exclusions_destroy(&(m->joliet_hidings), 0);
 Exclusions_destroy(&(m->hfsplus_hidings), 0);
 Xorriso_destroy_all_extf(m, 0);
 Xorriso_lst_destroy_all(&(m->drive_blacklist), 0);
 Xorriso_lst_destroy_all(&(m->drive_greylist), 0);
 Xorriso_lst_destroy_all(&(m->drive_whitelist), 0);
 Xorriso_destroy_node_array(m, 0);
 Xorriso_destroy_hln_array(m, 0);
 Xorriso_destroy_di_array(m, 0);

#ifdef Xorriso_with_libjtE
 if(m->libjte_handle)
   libjte_destroy(&(m->libjte_handle));
#endif

 Xorriso_lst_destroy_all(&(m->jigdo_params), 0);
 Xorriso_lst_destroy_all(&(m->jigdo_values), 0);
 for(i= 0; i < Xorriso_max_appended_partitionS; i++)
   if(m->appended_partitions[i] != NULL)
     free(m->appended_partitions[i]);

 Xorriso_detach_libraries(m, flag&1);

 if(m->lib_msg_queue_lock_ini)
   pthread_mutex_destroy(&(m->lib_msg_queue_lock));
 if(m->result_msglists_lock_ini)
   pthread_mutex_destroy(&(m->result_msglists_lock));
 if(m->write_to_channel_lock_ini)
   pthread_mutex_destroy(&(m->write_to_channel_lock));
 if(m->problem_status_lock_ini)
   pthread_mutex_destroy(&(m->problem_status_lock));
 if(m->msg_watcher_lock_ini)
   pthread_mutex_destroy(&(m->msg_watcher_lock));
 if(m->msgw_fetch_lock_ini)
   pthread_mutex_destroy(&(m->msgw_fetch_lock));
 Xorriso_sieve_dispose(m, 0);
 
 free((char *) m);
 *xorriso= NULL;
 return(1);
}


int Xorriso_destroy_node_array(struct XorrisO *xorriso, int flag)
{
 int i;

 if(xorriso->node_array != NULL) {
   for(i= 0; i < xorriso->node_counter; i++)
     iso_node_unref((IsoNode *) xorriso->node_array[i]);
   free(xorriso->node_array);
 }
 xorriso->node_array= NULL;
 xorriso->node_counter= xorriso->node_array_size= 0;
 Xorriso_lst_destroy_all(&(xorriso->node_disk_prefixes), 0);
 Xorriso_lst_destroy_all(&(xorriso->node_img_prefixes), 0);
 return(1);
}


/* @param flag bit0= do not destroy hln_array but only hln_targets
*/
int Xorriso_destroy_hln_array(struct XorrisO *xorriso, int flag)
{
 int i;

 
 if(xorriso->hln_array != NULL && !(flag & 1)) {
   for(i= 0; i < xorriso->hln_count; i++)
     iso_node_unref((IsoNode *) xorriso->hln_array[i]);
   free(xorriso->hln_array);
   xorriso->hln_array= NULL;
   xorriso->hln_count= 0;
 }
 if(xorriso->hln_targets != NULL) {
   for(i= 0; i < xorriso->hln_count; i++)
     if(xorriso->hln_targets[i] != NULL)
       free(xorriso->hln_targets[i]);
   free(xorriso->hln_targets);
   xorriso->hln_targets= NULL;
 }
 xorriso->node_targets_availmem= 0;
 return(1);
}


int Xorriso_destroy_di_array(struct XorrisO *xorriso, int flag)
{
 int i;

 if(xorriso->di_array != NULL) {
   for(i= 0; i < xorriso->di_count; i++)
     if(xorriso->di_array[i] != NULL)
       iso_node_unref((IsoNode *) xorriso->di_array[i]);
   free(xorriso->di_array);
   xorriso->di_array= NULL;
 }
 if(xorriso->di_do_widen != NULL) {
   free(xorriso->di_do_widen);
   xorriso->di_do_widen= NULL;
 }
 Xorriso_lst_destroy_all(&(xorriso->di_disk_paths), 0);
 Xorriso_lst_destroy_all(&(xorriso->di_iso_paths), 0);
 xorriso->di_count= 0;

#ifdef NIX
 /* <<< */
 fprintf(stderr, "xorriso_DEBUG: get_di_count= %lu\n",
         Xorriso_get_di_counteR);
#endif /* NIX */

 return(1);
}


int Xorriso_new_node_array(struct XorrisO *xorriso, off_t mem_limit,
                           int addon_nodes, int flag)
{
 int i;

 if(xorriso->node_counter <= 0)
   return(1);

 xorriso->node_array= calloc(xorriso->node_counter + addon_nodes,
                             sizeof(IsoNode *));
 if(xorriso->node_array == NULL) {
   Xorriso_no_malloc_memory(xorriso, NULL, 0);
   return(-1);
 }
 for(i= 0; i < xorriso->node_counter + addon_nodes; i++)
   xorriso->node_array[i]= NULL;
 xorriso->node_array_size= xorriso->node_counter + addon_nodes;
 xorriso->node_counter= 0;
 return(1);
}


/* @param flag bit0= do not allocate hln_array but only hln_targets
*/
int Xorriso_new_hln_array(struct XorrisO *xorriso, off_t mem_limit, int flag)
{
 int i;

 Xorriso_destroy_hln_array(xorriso, flag & 1);
 if(xorriso->hln_count <= 0)
   return(1);

 if(!(flag & 1)) {
   xorriso->hln_array= calloc(xorriso->hln_count, sizeof(char *));
   if(xorriso->hln_array == NULL) {
     Xorriso_no_malloc_memory(xorriso, NULL, 0);
     return(-1);
   }
   for(i= 0; i < xorriso->hln_count; i++)
     xorriso->hln_array[i]= NULL;
 }

 xorriso->hln_targets= calloc(xorriso->hln_count, sizeof(char *));
 if(xorriso->hln_targets == NULL) {
   if(!(flag & 1)) {
     free(xorriso->hln_array);
     xorriso->hln_array= NULL;
   }
   Xorriso_no_malloc_memory(xorriso, NULL, 0);
   return(-1);
 }
 for(i= 0; i < xorriso->hln_count; i++)
   xorriso->hln_targets[i]= NULL;
 xorriso->node_targets_availmem= mem_limit
                                 - xorriso->hln_count * sizeof(void *)
                                 - xorriso->hln_count * sizeof(char *);
 if(xorriso->node_targets_availmem < 0)
   xorriso->node_targets_availmem= 0;
 return(1);
}


int Xorriso__preset_signal_behavior(int behavior, int flag)
{
 if(behavior < 0 || behavior > 3)
   return(0);
 Xorriso_signal_behavioR= behavior;
 return(1);
}


int Xorriso__get_signal_behavior(int flag)
{
 return(Xorriso_signal_behavioR);
}

