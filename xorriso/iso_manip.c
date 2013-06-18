
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2013 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains functions which manipulate the libisofs tree model.
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

#include "lib_mgt.h"
#include "iso_img.h"
#include "iso_tree.h"
#include "iso_manip.h"
#include "sort_cmp.h"
#include "parse_exec.h"



/* @param flag bit0= give directory x-permission where is r-permission
               bit1= do not transfer ACL or xattr
               bit2= record dev,inode (only if enabled by xorriso)
               bit5= transfer ACL or xattr from eventual link target
*/
int Xorriso_transfer_properties(struct XorrisO *xorriso, struct stat *stbuf,
                               char *disk_path,  IsoNode *node, int flag)
{
 mode_t mode;
 int ret= 1;
 size_t num_attrs= 0, *value_lengths= NULL;
 char **names= NULL, **values= NULL;

 mode= stbuf->st_mode;

 if((!(flag & 2)) && !(xorriso->do_aaip & 1))
   /* Will drop ACL. Update mode S_IRWXG by eventual group:: ACL entry */
   iso_local_get_perms_wo_acl(disk_path, &mode, flag & 32);

 if((flag&1) && S_ISDIR(mode)) {
   if(mode&S_IRUSR)
     mode|= S_IXUSR;
   if(mode&S_IRGRP)
     mode|= S_IXGRP;
   if(mode&S_IROTH)
     mode|= S_IXOTH;
 }
 iso_node_set_permissions(node, mode & 07777);
 iso_node_set_uid(node, stbuf->st_uid);
 iso_node_set_gid(node, stbuf->st_gid);
 iso_node_set_atime(node, stbuf->st_atime);
 iso_node_set_mtime(node, stbuf->st_mtime);
 iso_node_set_ctime(node, stbuf->st_ctime);

 if((xorriso->do_aaip & 5) && !(flag & 2)) {
   ret= iso_local_get_attrs(disk_path, &num_attrs, &names, &value_lengths,
                            &values, ((xorriso->do_aaip & 1) && !(flag & 2))
                                     | ((!(xorriso->do_aaip & 4)) << 2)
                                     | (flag & 32));
   if(ret < 0) { 
     Xorriso_process_msg_queues(xorriso,0);
     Xorriso_report_iso_error(xorriso, disk_path, ret,
                              "Error when obtaining local ACL and xattr", 0,
                              "FAILURE", 1 | 2);
     ret= 0; goto ex;
   }

   /* Preserve namespace isofs, but not ACL or system xattr */
   ret= iso_node_set_attrs(node, num_attrs, names, value_lengths, values,
                           1 | 8 | 16);
   if(ret < 0) {
     Xorriso_process_msg_queues(xorriso,0);
     Xorriso_report_iso_error(xorriso, "", ret,
                              "Error when setting ACL and xattr to image node",
                              0, "FAILURE", 1);
     ret= 0; goto ex;
   }
 }

 if((flag & 4) && ((xorriso->do_aaip & 16) || !(xorriso->ino_behavior & 2))) {
   ret= Xorriso_record_dev_inode(xorriso, disk_path, (dev_t) 0, (ino_t) 0,
                                 (void *) node, "", flag & 32);
   if(ret <= 0)
     goto ex;
 }

 ret= 1;
ex:;
 Xorriso_process_msg_queues(xorriso,0);
 iso_local_get_attrs(disk_path, &num_attrs, &names, &value_lengths,
                      &values, 1 << 15); /* free memory */
 return(ret);
}


int Xorriso_graft_split(struct XorrisO *xorriso, IsoImage *volume,
                        IsoDir *dir, char *disk_path, char *img_name,
                        char *nominal_source, char *nominal_target,
                        off_t size, IsoNode **node, int flag)
{
 int ret;
 IsoDir *new_dir= NULL;
 IsoNode *part_node;
 int partno, total_parts;
 off_t offset;
 char *part_name= NULL;

 Xorriso_alloc_meM(part_name, char, SfileadrL);

 ret= iso_tree_add_new_dir(dir, img_name, &new_dir);
 if(ret<0)
   goto ex;
 *node= (IsoNode *) new_dir;
 if(xorriso->update_flags & 1) {
   ret= Xorriso_mark_update_merge(xorriso, img_name, node, 1);
   if(ret <= 0)
     {ret= 0; goto ex;}
 }
 total_parts= size / xorriso->split_size;
 if(size % xorriso->split_size)
   total_parts++;
 for(partno= 1; partno<=total_parts; partno++) {
   offset = xorriso->split_size * (off_t) (partno-1);
   Splitpart__compose(part_name, partno, total_parts, offset,
                      xorriso->split_size, size, 0);
   ret= Xorriso_tree_graft_node(xorriso, volume,
                                new_dir, disk_path, part_name,
                                nominal_source, nominal_target,
                                offset, xorriso->split_size,
                                &part_node, 8);
   if(ret<=0)
     goto ex;
 }
 sprintf(xorriso->info_text, "Split into %d parts: ", total_parts);
 Text_shellsafe(nominal_target, xorriso->info_text, 1);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
 ret= 1;
ex:;
 Xorriso_free_meM(part_name);
 return(ret);
}


/* 
   @param flag bit0= ISO_NODE_NAME_NOT_UNIQUE exception mode:
                     Do not issue message. Return existing node into *node.
               bit3= cut_out_node: offset and size are valid
               bit8= hide in iso_rr
               bit9= hide in joliet
              bit10= hide in hfsplus
*/
int Xorriso_tree_graft_node(struct XorrisO *xorriso, IsoImage *volume,
                            IsoDir *dir, char *disk_path, char *img_name,
                            char *nominal_source, char *nominal_target,
                            off_t offset, off_t cut_size,
                            IsoNode **node, int flag)
{
 int ret, stbuf_valid= 0;
 struct stat stbuf;
 char *namept;
 off_t size= 0;

 if(lstat(disk_path, &stbuf) != -1) {
   stbuf_valid= 1;
   if(S_ISREG(stbuf.st_mode))
     size= stbuf.st_size;
 }
 if(flag&8)  {
   if(cut_size > xorriso->file_size_limit && xorriso->file_size_limit > 0) {
     sprintf(xorriso->info_text,
             "File piece exceeds size limit of %.f bytes: %.f from ",
             (double) xorriso->file_size_limit, (double) cut_size);
     Text_shellsafe(disk_path, xorriso->info_text, 1);
     strcat(xorriso->info_text, "\n");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   }
   ret= iso_tree_add_new_cut_out_node(volume, dir, img_name, disk_path,
                                      offset, cut_size, node);
   if(ret<0)
     goto ex;
 } else {
   if(xorriso->split_size > 0 && size > xorriso->split_size) {
     ret= Xorriso_graft_split(xorriso, volume, dir, disk_path, img_name,
                              nominal_source, nominal_target, size,
                              node, 0);
     if(ret<=0)
       goto ex;
   } else if(size > xorriso->file_size_limit && xorriso->file_size_limit > 0) {
     sprintf(xorriso->info_text,
             "File exceeds size limit of %.f bytes: ",
             (double) xorriso->file_size_limit);
     Text_shellsafe(disk_path, xorriso->info_text, 1);
     strcat(xorriso->info_text, "\n");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     return(0);
   } else {
     ret= iso_tree_add_new_node(volume, dir, img_name, disk_path, node);
     if(ret<0)
       goto ex;
   }
 }
 if(flag & (256 | 512 | 1024)) {
   ret= Xorriso_set_hidden(xorriso, (void *) *node, "",  (flag >> 8) & 7, 0);
   if(ret <= 0)
     goto ex;
 }
 if(stbuf_valid && ((xorriso->do_aaip & 16) || !(xorriso->ino_behavior & 2))) {
   ret= Xorriso_record_dev_inode(xorriso, disk_path,
                            stbuf.st_dev, stbuf.st_ino, (void *) *node, "", 1);
   if(ret <= 0)
     goto ex;
 }
 if(xorriso->update_flags & 1) {
   ret= Xorriso_mark_update_merge(xorriso, img_name, *node, 1);
   if(ret <= 0)
     goto ex;
 }

ex:;
 if(ret<0) {
   if(ret == (int) ISO_NODE_NAME_NOT_UNIQUE && (flag & 1)) {
     iso_dir_get_node(dir, img_name, node);
   } else {
     Xorriso_process_msg_queues(xorriso,0);
     if(ret == (int) ISO_RR_NAME_TOO_LONG ||
        ret == (int) ISO_RR_NAME_RESERVED ||
        ret == (int) ISO_RR_PATH_TOO_LONG)
       namept= nominal_target;
     else
       namept= nominal_source;
     Xorriso_report_iso_error(xorriso, namept, ret,
                              "Cannot add node to tree", 0, "FAILURE", 1|2);
   }
   return(ret);
 }
 if(LIBISO_ISREG(*node))
   xorriso->pacifier_byte_count+= iso_file_get_size((IsoFile *) *node);
 return(1);
}


/*
   @param boss_iter Opaque handle to be forwarded to actions in ISO image
                    Set to NULL if calling this function without having
                    a boss iterator objetc.
   @param node      Pointer to pointer to existing node,
                    *node is set to NULL, if the node gets removed.
   @param flag bit0= source is directory
               bit4= return 3 on rejection by exclusion or user
               bit6= do not delete eventually existing node from di_array
               bit7= no special handling of split file directories
   @return     1= no action was needed, 2= target removed,
               3= rejected with bit4, <=0 means error
*/
int Xoriso_handle_collision(struct XorrisO *xorriso, void *boss_iter,
                            IsoNode **node, char *img_path,
                            char *full_img_path, char *disk_path,
                            char *show_path, int flag)
{
 int ret, target_is_dir, target_is_split, source_is_dir;

 source_is_dir= flag & 1;
 target_is_dir= LIBISO_ISDIR(*node);

 target_is_split= 0;
 if(target_is_dir && !(flag & 128))
   target_is_split= Xorriso_is_split(xorriso, "", (void *) *node, 1 | 2);

 if(!((target_is_dir && !target_is_split) && source_is_dir)) {
   Xorriso_process_msg_queues(xorriso, 0);

   /* handle overwrite situation */;
   if(xorriso->do_overwrite == 1 ||
      (xorriso->do_overwrite == 2 && !(target_is_dir && !target_is_split))) {
     ret= Xorriso_rmi(xorriso, boss_iter, (off_t) 0, img_path,
                      1 | 8 | (flag & 64));
     if(ret <= 0)
       return(ret);
     if(ret == 3) {
       sprintf(xorriso->info_text, "User revoked adding of: ");
       Text_shellsafe(show_path, xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
       return(3 * !!(flag & 16));
     }
     *node= NULL;
     return(2);
   }

   if (disk_path[0])
     Xorriso_msgs_submit(xorriso, 0, disk_path, 0, "ERRFILE", 0);
   if(strcmp(full_img_path, img_path) == 0)
     sprintf(xorriso->info_text,
         "While grafting '%s' : file object exists and may not be overwritten",
         img_path);
   else
     sprintf(xorriso->info_text,
            "While grafting '%s' : '%s' exists and may not be overwritten",
            full_img_path, img_path);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 return(1);
}


/* @param flag bit0= recursion is active
               bit1= do not report added files
               bit6= do not delete eventually existing node from di_array
               bit7= no special handling of split file directories
               bit8= hide in iso_rr
               bit9= hide in joliet
*/
int Xorriso_add_tree(struct XorrisO *xorriso, IsoDir *dir,
                     char *img_dir_path, char *disk_dir_path,
                     struct LinkiteM *link_stack, int flag)
{
 IsoImage *volume;
 IsoNode *node;
 int ret, source_is_dir, source_is_link, fret, was_failure= 0;
 int do_not_dive, hide_attrs;
 struct DirseQ *dirseq= NULL;
 char *name, *img_name, *srcpt, *stbuf_src= "";
 struct stat stbuf, hstbuf;
 dev_t dir_dev;
 struct LinkiteM *own_link_stack;
 char *sfe= NULL, *sfe2= NULL;
 char *disk_path= NULL, *img_path= NULL, *link_target= NULL;

#define Xorriso_add_handle_collisioN 1
#define Xorriso_optimistic_add_treE 1

#ifndef Xorriso_optimistic_add_treE
#ifndef Xorriso_add_handle_collisioN
 int target_is_split= 0, target_is_dir;
#endif
#endif

 /* Avoiding large local memory objects in order to save stack space */
 sfe= malloc(5*SfileadrL);
 sfe2= malloc(5*SfileadrL);
 disk_path= malloc(2*SfileadrL);
 img_path= malloc(2*SfileadrL);
 link_target= calloc(SfileadrL, 1);
 if(sfe==NULL || sfe2==NULL || disk_path==NULL || img_path==NULL ||
    link_target==NULL) {
   Xorriso_no_malloc_memory(xorriso, &sfe, 0);
   {ret= -1; goto ex;}
 }

 own_link_stack= link_stack;

 ret= Xorriso_get_volume(xorriso, &volume, 0);
 if(ret<=0)
   goto ex;

 stbuf_src= disk_dir_path;
 if(lstat(disk_dir_path, &stbuf)==-1)
   goto cannot_open_dir;
 dir_dev= stbuf.st_dev;
 if(S_ISLNK(stbuf.st_mode)) {
   if(!(xorriso->do_follow_links || (xorriso->do_follow_param && !(flag&1))))
     {ret= 2; goto ex;}
   stbuf_src= disk_dir_path;
   if(stat(disk_dir_path, &stbuf)==-1)
     goto cannot_open_dir;
   if(dir_dev != stbuf.st_dev &&
      !(xorriso->do_follow_mount || (xorriso->do_follow_param && !(flag&1))))
     {ret= 2; goto ex;}
 }
 ret= Dirseq_new(&dirseq, disk_dir_path, 1);
 if(ret<0) {
   sprintf(xorriso->info_text,"Failed to create source filesystem iterator");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   {ret= -1; goto ex;}
 }
 if(ret==0) {
cannot_open_dir:;
   Xorriso_msgs_submit(xorriso, 0, disk_dir_path, 0, "ERRFILE", 0);
   sprintf(xorriso->info_text,"Cannot open as source directory: %s",
           Text_shellsafe(disk_dir_path, sfe, 0));
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }

 if(Sfile_str(disk_path, disk_dir_path,0)<=0)
   {ret= -1; goto ex;}
 if(disk_path[0]==0 || disk_path[strlen(disk_path)-1]!='/')
   strcat(disk_path,"/");
 name= disk_path+strlen(disk_path);
 if(Sfile_str(img_path, img_dir_path, 0)<=0)
   {ret= -1; goto ex;}
 if(img_path[0] == 0)
   strcat(img_path, "/");
 else if(img_path[strlen(img_path) - 1] != '/')
   strcat(img_path, "/");
 img_name= img_path+strlen(img_path);

 while(1) { /* loop over directory content */
   stbuf_src= "";
   Linkitem_reset_stack(&own_link_stack, link_stack, 0);
   srcpt= disk_path;
   Xorriso_process_msg_queues(xorriso,0);
   ret= Dirseq_next_adr(dirseq,name,0); /* name is a pointer into disk_path */
   if(ret==0)
 break;
   if(ret<0) {
     sprintf(xorriso->info_text,"Failed to obtain next directory entry");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
     {ret= -1; goto ex;}
   }

   /* Compare exclusions against disk_path resp. name */
   ret= Xorriso_path_is_excluded(xorriso, disk_path, 0); /* (is never param) */
   if(ret<0)
     {ret= -1; goto ex;}
   if(ret>0)
 continue;
   /* Check for mkisofs-style hidings */
   hide_attrs= (flag >> 8) & 3;
   if(hide_attrs != 3) {
     ret= Xorriso_path_is_hidden(xorriso, disk_path, 0);
     if(ret<0)
       return(ret);
     if(ret>=0)
       hide_attrs|= ret;
   }

   strcpy(img_name, name);
   if(Xorriso_much_too_long(xorriso, strlen(img_path), 0)<=0)
     {ret= 0; goto was_problem;}
   if(Xorriso_much_too_long(xorriso, strlen(srcpt), 0)<=0)
     {ret= 0; goto was_problem;}
   stbuf_src= srcpt;
   if(lstat(srcpt, &stbuf)==-1) {
cannot_lstat:;
     Xorriso_msgs_submit(xorriso, 0, srcpt, 0, "ERRFILE", 0);
     sprintf(xorriso->info_text,
             "Cannot determine attributes of source file %s",
             Text_shellsafe(srcpt, sfe, 0));
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
     ret= 0; goto was_problem;
   }
   source_is_dir= 0;
   source_is_link= S_ISLNK(stbuf.st_mode);
   if(xorriso->do_follow_links && source_is_link) {
     /* Xorriso_hop_link checks for wide link loops */
     ret= Xorriso_hop_link(xorriso, srcpt, &own_link_stack, &hstbuf, 0);
     if(ret<0)
       goto was_problem;
     if(ret==1) {
       ret= Xorriso_resolve_link(xorriso, srcpt, link_target, 0);
       if(ret<=0)
         goto was_problem;
       srcpt= link_target;
       stbuf_src= srcpt;
       if(lstat(srcpt, &stbuf)==-1)
         goto cannot_lstat;
     } else {
       if(Xorriso_eval_problem_status(xorriso, 0, 1|2)<0)
         {ret= 0; goto was_problem;} 
       ret= Xorriso_resolve_link(xorriso, srcpt, link_target, 1);
       if(ret<=0)
         goto was_problem;
     }
   } else if (S_ISLNK(stbuf.st_mode)) {
     ret= Xorriso_resolve_link(xorriso, srcpt, link_target, 1);
     if(ret<=0)
       goto was_problem;
   }
   do_not_dive= 0;
   if(S_ISDIR(stbuf.st_mode)) {
     source_is_dir= 1;
     if(dir_dev != stbuf.st_dev && !xorriso->do_follow_mount)
       do_not_dive= 1;
   }

#ifdef Xorriso_optimistic_add_treE

   ret= Xorriso_tree_graft_node(xorriso, volume, dir, srcpt, img_name,
                                "", img_path, (off_t) 0, (off_t) 0,
                                &node, 1 | (hide_attrs << 8));
   if(ret == (int) ISO_NODE_NAME_NOT_UNIQUE) {
     ret= Xoriso_handle_collision(xorriso, NULL, &node, img_path, img_path,
                                  srcpt, img_path,
                                  (!!source_is_dir) | (flag & (64 | 128)));
     if(ret <= 0)
       goto was_problem;
     if(node == NULL) {
       ret= Xorriso_tree_graft_node(xorriso, volume, dir, srcpt, img_name,
                                    "", img_path, (off_t) 0, (off_t) 0,
                                    &node, (hide_attrs << 8));
       if(ret <= 0)
         node= NULL;
     }
   }

#else /* Xorriso_optimistic_add_treE */

   /* does a node exist with this name ? */
   node= NULL;
   if(dir != NULL) {
     ret= iso_dir_get_node(dir, img_name, &node);
   } else {
     ret= Xorriso_node_from_path(xorriso, volume, img_path, &node, 1);
   }
   if(ret>0) {
     target_is_dir= LIBISO_ISDIR(node);
     target_is_split= 0;
     if(target_is_dir && !(flag & 128))
       target_is_split= Xorriso_is_split(xorriso, "", (void *) node, 1 | 2);

     if(!((target_is_dir && !target_is_split) && source_is_dir)) {
       Xorriso_process_msg_queues(xorriso,0);

       /* handle overwrite situation */;
       if(xorriso->do_overwrite==1 ||
          (xorriso->do_overwrite==2 && !(target_is_dir && !target_is_split))) {
         ret= Xorriso_rmi(xorriso, NULL, (off_t) 0, img_path,
                          1 | 8 | (flag & 64));
         if(ret<=0)
           goto was_problem;
         if(ret==3) {
           sprintf(xorriso->info_text, "User revoked adding of: %s",
                   Text_shellsafe(img_path, sfe, 0));
           Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
           ret= 0; goto was_problem;
         }
         node= NULL;
       } else {
         Xorriso_msgs_submit(xorriso, 0, srcpt, 0, "ERRFILE", 0);
         sprintf(xorriso->info_text,
     "While grafting %s : file object exists and may not be overwritten by %s",
             Text_shellsafe(img_path,sfe,0), Text_shellsafe(stbuf_src,sfe2,0));
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         ret= 0; goto was_problem;
       }
     }
   }

   if(node==NULL) {
     ret= Xorriso_tree_graft_node(xorriso, volume, dir, srcpt, img_name,
                                  "", img_path, (off_t) 0, (off_t) 0,
                                  &node, (hide_attrs << 8));
   }

#endif /* Xorriso_optimistic_add_treE */

   if(node==NULL) {
     Xorriso_process_msg_queues(xorriso,0);
     Xorriso_msgs_submit(xorriso, 0, stbuf_src, 0, "ERRFILE", 0);
     sprintf(xorriso->info_text, "Grafting failed:  %s = %s",
             Text_shellsafe(img_path,sfe,0), Text_shellsafe(stbuf_src,sfe2,0));
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto was_problem;
   }

   xorriso->pacifier_count++;
   if((xorriso->pacifier_count%100)==0)
     Xorriso_pacifier_callback(xorriso, "files added", xorriso->pacifier_count,
                               xorriso->pacifier_total, "", 0);

   Xorriso_set_change_pending(xorriso, 0);
   if(source_is_dir) {
     if(do_not_dive) {
       sprintf(xorriso->info_text, "Did not follow mount point : %s",
               Text_shellsafe(disk_path, sfe, 0));
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
     } else {
       ret= Xorriso_add_tree(xorriso, (IsoDir *) node,
                             img_path, disk_path, own_link_stack,
                             1 | (flag & (2 | 64 | 128)));
     }
     if(ret<=0)
       goto was_problem;
   }

 continue; /* regular bottom of loop */
was_problem:;
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret<0)
     goto ex;
 }

 ret= 1;
ex:
 if(sfe!=NULL)
   free(sfe);
 if(sfe2!=NULL)
   free(sfe2);
 if(disk_path!=NULL)
   free(disk_path);
 if(img_path!=NULL)
   free(img_path);
 if(link_target!=NULL)
   free(link_target);
 Xorriso_process_msg_queues(xorriso,0);
 Linkitem_reset_stack(&own_link_stack, link_stack, 0);
 Dirseq_destroy(&dirseq, 0);
 if(ret<=0)
   return(ret);
 return(!was_failure);
}


/* @param flag bit0= cut_out mode : base on leaf parent directory
               bit1= do not check and perform hidings
*/
int Xorriso_copy_implicit_properties(struct XorrisO *xorriso, IsoDir *dir,
           char *full_img_path, char *img_path, char *full_disk_path, int flag)
{
 int ret, nfic, nic, nfdc, d, i;
 char *nfi= NULL, *ni= NULL, *nfd= NULL, *cpt;
 struct stat stbuf;

 Xorriso_alloc_meM(nfi, char, SfileadrL);
 Xorriso_alloc_meM(ni, char, SfileadrL);
 Xorriso_alloc_meM(nfd, char, SfileadrL);

 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, full_img_path, nfi,
                                 1|2);
 if(ret<=0)
   goto ex;
 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, img_path, ni, 1|2);
 if(ret<=0)
   goto ex;
 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdx, full_disk_path, nfd,
                                 1|2|4);
 if(ret<=0)
   goto ex;
 nfic= Sfile_count_components(nfi, 0);
 nic= Sfile_count_components(ni, 0);
 nfdc= Sfile_count_components(nfd, 0);
 d= nfic-(flag&1)-nic;
 if(d<0)
   {ret= -1; goto ex;}
 if(d>nfdc)
   {ret= 0; goto ex;}
 for(i= 0; i<d; i++) {
   cpt= strrchr(nfd, '/');
   if(cpt==NULL)
     {ret= -1; goto ex;} /* should not happen */
   *cpt= 0;
 }
 if(nfd[0]==0)
   strcpy(nfd, "/");
 if(stat(nfd, &stbuf)==-1)
   {ret= 0; goto ex;}
 Xorriso_transfer_properties(xorriso, &stbuf, nfd, (IsoNode *) dir,
                             ((flag&1) && d==0) | 4 | 32);
 sprintf(xorriso->info_text, "Copied properties for ");
 Text_shellsafe(ni, xorriso->info_text, 1);
 sprintf(xorriso->info_text+strlen(xorriso->info_text), " from ");
 Text_shellsafe(nfd, xorriso->info_text, 1);
 if(!((flag&1) && d==0))
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);

 if(!(flag & 2)) {
   /* Check for mkisofs-style hidings */
   ret= Xorriso_path_is_hidden(xorriso, nfd, 0);
   if(ret<0)
     goto ex;
   if(ret>=0) {
     /* Hide dir */
     ret= Xorriso_set_hidden(xorriso, (void *) dir, "", ret, 0);
     if(ret <= 0)
       goto ex;
   }
 }
 ret= 1;
ex:
 Xorriso_free_meM(nfi);
 Xorriso_free_meM(ni);
 Xorriso_free_meM(nfd);
 return(ret);
}


/* @param bit0= copy link target properties rather than link properties
          bit1= give directory x-permission where is r-permission
          bit2= record dev,inode (only if enabled by xorriso)
*/
int Xorriso_copy_properties(struct XorrisO *xorriso,
                            char *disk_path, char *img_path, int flag)
{
 int ret;
 IsoNode *node;
 struct stat stbuf;

 ret= Xorriso_get_node_by_path(xorriso, img_path, NULL, &node, 0);
 if(ret<=0)
   return(ret);
 if(flag & 1) {
   if(stat(disk_path, &stbuf)==-1)
     return(0);
 } else {
   if(lstat(disk_path, &stbuf)==-1)
     return(0);
 }
 Xorriso_transfer_properties(xorriso, &stbuf, disk_path, node,
                           ((flag & 2) >> 1) | ((flag & 1) << 5) | (flag & 4));
 Xorriso_set_change_pending(xorriso, 0);
 return(1);
}


int Xorriso_add_symlink(struct XorrisO *xorriso, IsoDir *parent,
                        char *link_target, char *leaf_name,
                        char *nominal_path, int flag)
{
 int ret= 0;
 IsoSymlink *link= NULL;

 ret= iso_tree_add_new_symlink(parent, leaf_name, link_target, &link);
 Xorriso_process_msg_queues(xorriso,0);
 if(ret < 0) {
   Xorriso_report_iso_error(xorriso, nominal_path, ret,
                            "Cannot create symbolic link", 0, "FATAL", 1);
   ret= 0;
 }
 return(ret);
}


/* @param boss_iter Opaque handle to be forwarded to actions in ISO image
                    Set to NULL if calling this function from outside ISO world
   @param flag bit0= mkdir: graft in as empty directory, not as copy from disk
               bit1= do not report added files
               bit2= -follow, -not_*: this is not a command parameter
               bit3= use offset and cut_size for cut_out_node
               bit4= return 3 on rejection by exclusion or user
               bit5= if directory then do not add sub tree
               bit6= do not delete eventually existing node from di_array
               bit7= no special handling of split file directories
               bit8= hide in iso_rr
               bit9= hide in joliet
              bit10= ln -s: graft in as symbolic link.
                            Link target is handed over in parameter disk_path.
   @return <=0 = error , 1 = added simple node , 2 = added directory ,
                         3 = rejected 
*/
int Xorriso_graft_in(struct XorrisO *xorriso, void *boss_iter, 
                     char *disk_path, char *img_path,
                     off_t offset, off_t cut_size, int flag)
{
 IsoImage *volume;
 char *path= NULL, *apt, *npt, *cpt;
 char *disk_path_pt, *resolved_disk_path= NULL;
 IsoDir *dir= NULL, *hdir;
 IsoNode *node;
 int done= 0, is_dir= 0, l, ret, source_is_dir, resolve_link= 0;
 int hide_attrs;
 struct stat stbuf;

#define Xorriso_graft_handle_collisioN 1
#define Xorriso_optimistic_graft_iN 1

#ifndef Xorriso_optimistic_graft_iN
#ifndef Xorriso_graft_handle_collisioN
 int target_is_split, target_is_dir;
#endif
#endif

 Xorriso_alloc_meM(path, char, SfileadrL);
 Xorriso_alloc_meM(resolved_disk_path, char, SfileadrL);

 hide_attrs= (flag >> 8) & 3;
 if (disk_path == NULL && !(flag & 1)) {
   Xorriso_msgs_submit(xorriso, 0,
         "Program error: Xorriso_graft_in(): disk_path == NULL && !(flag & 1)",
         0, "ABORT", 0);
   {ret= -1; goto ex;}
 }
 if (disk_path == NULL) {
   disk_path= "";
 } else {
   ret= Xorriso_path_is_excluded(xorriso, disk_path, !(flag&4));
   if(ret<0)
     goto ex;
   if(ret>0)
     {ret= 3*!!(flag&16); goto ex;}

   /* Check for mkisofs-style hidings */
   if(hide_attrs != 3) {
     ret= Xorriso_path_is_hidden(xorriso, disk_path, 0);
     if(ret<0)
       goto ex;
     if(ret>=0)
       hide_attrs|= ret;
   }
 }

 for(cpt= img_path; 1; cpt++) {
   cpt= strstr(cpt,"/.");
   if(cpt==NULL)
 break;
   if(cpt[2]=='.') {
     if(cpt[3]=='/' || cpt[3]==0)
 break;
   } else if(cpt[2]=='/' || cpt[2]==0)
 break;
 }
 if(cpt!=NULL) {
   if(disk_path[0])
     Xorriso_msgs_submit(xorriso, 0, disk_path, 0, "ERRFILE", 0);
   sprintf(xorriso->info_text,
           "Unsupported relative addressing in iso_rr_path ");
   Text_shellsafe(img_path, xorriso->info_text, 1);
   if(disk_path[0]) {
     strcat(xorriso->info_text, " (disk: ");
     Text_shellsafe(disk_path, xorriso->info_text, 1);
     strcat(xorriso->info_text, ")");
   }
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
   {ret= 0; goto ex;}
 }

 ret= Xorriso_get_volume(xorriso, &volume, 0);
 if(ret<=0)
   goto ex;

 strncpy(path, img_path, SfileadrL - 1);
 path[SfileadrL - 1]= 0;
 apt= npt= path;

 if(!(flag & (1 | 1024))) {
   ret= lstat(disk_path, &stbuf);
   if(ret!=-1) {
     if(S_ISDIR(stbuf.st_mode))
       is_dir= 1;
     else if((stbuf.st_mode&S_IFMT)==S_IFLNK &&
             (xorriso->do_follow_links ||
              (xorriso->do_follow_param && !(flag&4)))) {
       resolve_link= 1;
       ret= stat(disk_path, &stbuf);
       if(ret!=-1) {
         if(S_ISDIR(stbuf.st_mode))
           is_dir= 1;
       }
     }
   }
   if(ret == -1) {
     Xorriso_process_msg_queues(xorriso,0);
     Xorriso_msgs_submit(xorriso, 0, disk_path, 0, "ERRFILE", 0);
     sprintf(xorriso->info_text,
            "Cannot determine attributes of source file ");
     Text_shellsafe(disk_path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
   if(S_ISDIR(stbuf.st_mode)) {
     is_dir= 1;
   } else {
     l= strlen(img_path);
     if(l>0)
       if(img_path[l-1]=='/')
         l= 0;
     if(l==0) {
       Xorriso_msgs_submit(xorriso, 0, disk_path, 0, "ERRFILE", 0);
       sprintf(xorriso->info_text, "Source ");
       Text_shellsafe(disk_path, xorriso->info_text, 1);
       strcat(xorriso->info_text, " is not a directory. Target ");
       Text_shellsafe(img_path, xorriso->info_text, 1);
       strcat(xorriso->info_text, " would be.");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       {ret= 0; goto ex;}
     }
   }
 }

 dir= iso_image_get_root(volume);
 if(dir==NULL) {
   Xorriso_process_msg_queues(xorriso,0);
   sprintf(xorriso->info_text,
           "While grafting '%s' : no root node available", img_path);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   {ret= 0; goto ex;}
 }
 for(npt= apt; !done; apt= npt+1) {
   npt= strchr(apt, '/');
   if(npt==NULL) {
     npt= apt+strlen(apt);
     done= 1;
   } else
     *npt= 0;
   if(*apt==0) {
     *apt= '/';
     apt++;
     if(done)
       goto attach_source;
 continue;
   }
   source_is_dir= (is_dir || (flag&1) || !done);

#ifdef Xorriso_optimistic_graft_iN

   /* Directories of the source path are likely to exist already as directory
      in the image.
      That will cause two lookups with optimistic, and only one with
      pessimistic.
      So optimism will pay off only with the leaf. I.e. if(done).
    */
   if(source_is_dir) { /* eventually create directory */
     ret= iso_dir_get_node(dir, apt, &node);
     if(ret > 0) {
       ret= Xoriso_handle_collision(xorriso, boss_iter, &node, path,
                                    img_path, disk_path,
                                    disk_path[0] ? disk_path : img_path,
                               (!!source_is_dir) | (flag & (16 | 64 | 128)));
       if(ret <= 0 || ret == 3)
         goto ex;
       if(ret == 1 && node != NULL)
         dir= (IsoDir *) node;
     } else
       node= NULL;
     if(node == NULL) {
       ret= iso_tree_add_new_dir(dir, apt, &hdir);
       if(ret < 0) {
         Xorriso_process_msg_queues(xorriso,0);
         if(disk_path[0])
           Xorriso_msgs_submit(xorriso, 0, disk_path, 0, "ERRFILE", 0);
         Xorriso_report_iso_error(xorriso, img_path, ret,
                                  "Cannot create directory", 0, "FAILURE", 1);
         sprintf(xorriso->info_text,
               "While grafting '%s' : could not insert '%s'", img_path, path);
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         {ret= 0; goto ex;}
       }
       if(xorriso->update_flags & 1) {
         ret= Xorriso_mark_update_merge(xorriso, path, (IsoNode *) hdir, 1);
         if(ret <= 0)
           {ret= 0; goto ex;}
       }

       dir= hdir;
       Xorriso_set_change_pending(xorriso, 0);
       iso_node_set_ctime((IsoNode *) dir, time(NULL));
       iso_node_set_uid((IsoNode *) dir, geteuid());
       iso_node_set_gid((IsoNode *) dir, getegid());

       if(disk_path[0] && !done) {
         /* This not only copies disk directory properties
            but also sets eventual hide_attrs */
         Xorriso_copy_implicit_properties(xorriso, dir, img_path, path,
                                          disk_path, !!(flag&8));
       }
     }
   }

   if(done) {
attach_source:;
     if(flag&1) {
       /* directory node was created above */;

     } else if(flag & 1024) {
       ret= Xorriso_add_symlink(xorriso, dir, disk_path, apt, img_path, 0);
       if(ret <= 0)
         goto ex;
       Xorriso_set_change_pending(xorriso, 0);

     } else if(is_dir) {
       Xorriso_transfer_properties(xorriso, &stbuf, disk_path,
                                   (IsoNode *) dir, 4 | 32);
       if(!(flag&32)) {
         ret= Xorriso_add_tree(xorriso, dir, img_path, disk_path, NULL,
                               flag & (2 | 64 | 128));
         if(ret<=0)
           goto ex;
       }
     } else {
       if(resolve_link) {
         ret= Xorriso_resolve_link(xorriso, disk_path, resolved_disk_path, 0);
         if(ret<=0)
           goto ex;
         disk_path_pt= resolved_disk_path;
       } else
         disk_path_pt= disk_path;

       ret= Xorriso_tree_graft_node(xorriso, volume, dir, disk_path_pt, apt,
                                    disk_path, img_path, offset, cut_size,
                                    &node, 1 | (flag & 8) | (hide_attrs << 8));
       if(ret == (int) ISO_NODE_NAME_NOT_UNIQUE) {
         ret= Xoriso_handle_collision(xorriso, boss_iter, &node, img_path,
                                      img_path, disk_path,
                                      disk_path[0] ? disk_path : img_path,
                                      (flag & (16 | 64 | 128)));
         if(ret <= 0 || ret == 3)
           goto ex;
         ret= Xorriso_tree_graft_node(xorriso, volume, dir, disk_path_pt, apt,
                                      disk_path, img_path, offset, cut_size,
                                      &node, (flag & 8) | (hide_attrs << 8));
       }
       if(ret<=0) {
         sprintf(xorriso->info_text, "Grafting failed:  ");
         Text_shellsafe(img_path, xorriso->info_text, 1);
         strcat(xorriso->info_text, " = ");
         Text_shellsafe(disk_path, xorriso->info_text, 1);
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         {ret= 0; goto ex;}
       }
       Xorriso_set_change_pending(xorriso, 0);
       iso_node_set_name(node, apt);

       xorriso->pacifier_count++;
       if(xorriso->pacifier_count%100 && !(flag&2))
         Xorriso_pacifier_callback(xorriso, "files added",
                                   xorriso->pacifier_count,
                                   xorriso->pacifier_total, "", 0);
     }
   } else
     *npt= '/';

#else /* Xorriso_optimistic_graft_iN */

   node= NULL;
   ret= iso_dir_get_node(dir, apt, &node);
   if(ret>0) {

#ifdef Xorriso_graft_handle_collisioN

     ret= Xoriso_handle_collision(xorriso, boss_iter, &node, path, img_path,
                                  disk_path,
                                  disk_path[0] ? disk_path : img_path,
                                 (!!source_is_dir) | (flag & (16 | 64 | 128)));
     if(ret <= 0 || ret == 3)
       goto ex;
     if(ret == 2)
       goto handle_path_node;

#else /* Xorriso_graft_handle_collisioN */

     target_is_dir= LIBISO_ISDIR(node);

     target_is_split= 0;
     if(target_is_dir && !(flag & 128))
       target_is_split= Xorriso_is_split(xorriso, "", (void *) node, 1 | 2);

     if(!((target_is_dir && !target_is_split) && source_is_dir)) {
       Xorriso_process_msg_queues(xorriso,0);

       /* handle overwrite situation */;
       if(xorriso->do_overwrite==1 ||
          (xorriso->do_overwrite==2 && !(target_is_dir && !target_is_split))) {
         ret= Xorriso_rmi(xorriso, boss_iter, (off_t) 0, path,
                          1 | 8 | (flag & 64));
         if(ret<=0)
           goto ex;
         if(ret==3) {
           sprintf(xorriso->info_text, "User revoked adding of: ");
           if(disk_path[0])
             Text_shellsafe(disk_path, xorriso->info_text, 1);
           else
             Text_shellsafe(img_path, xorriso->info_text, 1);
           Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
           {ret= 3*!!(flag&16); goto ex;}
         }
         node= NULL;
         goto handle_path_node;
       }

       if (disk_path[0])
         Xorriso_msgs_submit(xorriso, 0, disk_path, 0, "ERRFILE", 0);
       sprintf(xorriso->info_text,
              "While grafting '%s' : '%s' exists and may not be overwritten",
              img_path, path);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       {ret= 0; goto ex;}
     }

#endif /* ! Xorriso_graft_handle_collisioN */

     dir= (IsoDir *) node;
   }

handle_path_node:;
   if(node==NULL && source_is_dir) { /* make a directory */
     ret= iso_tree_add_new_dir(dir, apt, &hdir);
     if(ret<0) {
       Xorriso_process_msg_queues(xorriso,0);
       if(disk_path[0])
         Xorriso_msgs_submit(xorriso, 0, disk_path, 0, "ERRFILE", 0);
       Xorriso_report_iso_error(xorriso, img_path, ret,
                                "Cannot create directory", 0, "FAILURE", 1);
       sprintf(xorriso->info_text,
               "While grafting '%s' : could not insert '%s'", img_path, path);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       {ret= 0; goto ex;}
     }
     if(xorriso->update_flags & 1) {
       ret= Xorriso_mark_update_merge(xorriso, path, (IsoNode *) hdir, 1);
       if(ret <= 0)
         {ret= 0; goto ex;}
     }

     dir= hdir;
     Xorriso_set_change_pending(xorriso, 0);
     iso_node_set_ctime((IsoNode *) dir, time(NULL));
     iso_node_set_uid((IsoNode *) dir, geteuid());
     iso_node_set_gid((IsoNode *) dir, getegid());

     if(disk_path[0] && !done)
       /* This not only copies disk directory properties
          but also sets eventual hide_attrs */
       Xorriso_copy_implicit_properties(xorriso, dir, img_path, path, disk_path,
                                       !!(flag&8));
   }
   if(done) {
attach_source:;
     if(flag&1) {
       /* directory node was created above */;

     } else if(flag & 1024) {
       ret= Xorriso_add_symlink(xorriso, dir, disk_path, apt, img_path, 0);
       if(ret <= 0)
         goto ex;

     } else if(is_dir) {
       Xorriso_transfer_properties(xorriso, &stbuf, disk_path,
                                   (IsoNode *) dir, 4 | 32);
       if(!(flag&32)) {
         ret= Xorriso_add_tree(xorriso, dir, img_path, disk_path, NULL,
                               flag & (2 | 64 | 128));
         if(ret<=0)
           goto ex;
       }
     } else {
       if(resolve_link) {
         ret= Xorriso_resolve_link(xorriso, disk_path, resolved_disk_path, 0);
         if(ret<=0)
           goto ex;
         disk_path_pt= resolved_disk_path;
       } else
         disk_path_pt= disk_path;

       ret= Xorriso_tree_graft_node(xorriso, volume, dir, disk_path_pt, apt,
                                    disk_path, img_path, offset, cut_size,
                                    &node, (flag&8) | (hide_attrs << 8));
       if(ret<=0) {
         sprintf(xorriso->info_text, "Grafting failed:  ");
         Text_shellsafe(img_path, xorriso->info_text, 1);
         strcat(xorriso->info_text, " = ");
         Text_shellsafe(disk_path, xorriso->info_text, 1);
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         {ret= 0; goto ex;}
       }
       Xorriso_set_change_pending(xorriso, 0);
       iso_node_set_name(node, apt);

       xorriso->pacifier_count++;
       if(xorriso->pacifier_count%100 && !(flag&2))
         Xorriso_pacifier_callback(xorriso, "files added",
                                   xorriso->pacifier_count,
                                   xorriso->pacifier_total, "", 0);
     }
   } else
     *npt= '/';

#endif /* ! Xorriso_optimistic_graft_iN */

 }

 Xorriso_process_msg_queues(xorriso,0);
 ret= 1+!!is_dir;
ex:;
 Xorriso_free_meM(path);
 Xorriso_free_meM(resolved_disk_path);
 return(ret);
}


/* @param flag bit0= -follow: disk_path is not a command parameter
*/
int Xorriso_cut_out(struct XorrisO *xorriso, char *disk_path,
                off_t startbyte, off_t bytecount, char *iso_rr_path, int flag)
{
 int ret;
 char *eff_source= NULL, *eff_dest= NULL;
 struct stat stbuf;

 Xorriso_alloc_meM(eff_source, char, SfileadrL);
 Xorriso_alloc_meM(eff_dest, char, SfileadrL);

 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdx, disk_path, eff_source,
                                 2|4);
 if(ret<=0)
   goto ex;
 ret= Xorriso_path_is_excluded(xorriso, disk_path, !(flag&1));
 if(ret!=0)
   {ret= 0; goto ex;}

 if(lstat(eff_source, &stbuf)==-1) {
   Xorriso_msgs_submit(xorriso, 0, eff_source, 0, "ERRFILE", 0);
   sprintf(xorriso->info_text, "-cut_out: Cannot determine type of ");
   Text_shellsafe(eff_source, xorriso->info_text, 1); 
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
   {ret= 0; goto ex;}
 }

 if((stbuf.st_mode&S_IFMT) == S_IFLNK) {
   if(!(xorriso->do_follow_links || (xorriso->do_follow_param && !(flag&1))))
     goto unsupported_type;
   if(stat(eff_source, &stbuf)==-1) {
     Xorriso_msgs_submit(xorriso, 0, eff_source, 0, "ERRFILE", 0);
     sprintf(xorriso->info_text,
             "-cut_out: Cannot determine link target type of ");
     Text_shellsafe(eff_source, xorriso->info_text, 1); 
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE",0);
     {ret= 0; goto ex;}
   }
 }
 if(S_ISREG(stbuf.st_mode)) {
   if(stbuf.st_size<startbyte) {
     Xorriso_msgs_submit(xorriso, 0, eff_source, 0, "ERRFILE", 0);
     sprintf(xorriso->info_text,
             "-cut_out: Byte offset %.f larger than file size %.f",
             (double) startbyte, (double) stbuf.st_size); 
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "SORRY", 0);
     {ret= 0; goto ex;}
   }
 } else {
unsupported_type:;
   Xorriso_msgs_submit(xorriso, 0, eff_source, 0, "ERRFILE", 0);
   sprintf(xorriso->info_text, "-cut_out: Unsupported file type (%s) with ",
           Ftypetxt(stbuf.st_mode, 0));
   Text_shellsafe(eff_source, xorriso->info_text, 1); 
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
   {ret= 0; goto ex;}
 }

 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, iso_rr_path, eff_dest,
                                 2);
 if(ret<=0)
   goto ex;

 ret= Xorriso_graft_in(xorriso, NULL, eff_source, eff_dest,
                       startbyte, bytecount, 8);
ex:;
 Xorriso_free_meM(eff_source);
 Xorriso_free_meM(eff_dest);
 return(ret);
}


/* @param flag bit0= do not produce info message on success
               bit1= do not raise protest if directory already exists
   @return 1=success,
           0=was already directory, -1=was other type, -2=other error
*/
int Xorriso_mkdir(struct XorrisO *xorriso, char *path, int flag)
{
 int ret;
 char *eff_path= NULL;

 Xorriso_alloc_meM(eff_path, char, SfileadrL);

 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, path, eff_path, 1);
 if(ret<0)
   {ret= -2; goto ex;}
 if(ret>0) {
   if(ret == 2 && (flag & 2))
     {ret= 0; goto ex;}
   sprintf(xorriso->info_text,"-mkdir: Address already existing ");
   Text_shellsafe(eff_path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0,
                       (ret==2 ? "WARNING" : "FAILURE"), 0);
   {ret= -1 + (ret == 2); goto ex;}
 }
 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, path, eff_path, 2);
 if(ret<0)
   {ret= -2; goto ex;}
 ret= Xorriso_graft_in(xorriso, NULL, NULL, eff_path, (off_t) 0, (off_t) 0, 1);
 if(ret<=0)
   {ret= -2; goto ex;}
 if(!(flag&1)) {
   sprintf(xorriso->info_text, "Created directory in ISO image: ");
   Text_shellsafe(eff_path, xorriso->info_text, 1);
   strcat(xorriso->info_text, "\n");
   Xorriso_info(xorriso, 0);
 }
 ret= 1;
ex:;
 Xorriso_free_meM(eff_path);
 return(ret);
}


/* @param boss_iter  If not NULL then this is an iterator suitable for
               iso_dir_iter_remove()  which is then to be used instead
               of iso_node_remove().
   @param flag bit0= remove whole sub tree: rm -r
               bit1= remove empty directory: rmdir  
               bit2= recursion: do not reassure in mode 2 "tree"
               bit3= this is for overwriting and not for plain removal
               bit4= count deleted files in xorriso->pacifier_count
               bit5= with bit0 only remove directory content, not the directory
               bit6= do not delete eventually existing node from di_array
   @return   <=0 = error
               1 = removed simple node 
               2 = removed directory or tree
               3 = did not remove on user revocation
*/
int Xorriso_rmi(struct XorrisO *xorriso, void *boss_iter, off_t boss_mem,
                char *path, int flag)
{
 int ret, is_dir= 0, pl, not_removed= 0, fret;
 IsoNode *victim_node, *node;
 IsoDir *boss_node, *root_dir;
 IsoDirIter *iter= NULL;
 IsoImage *volume;
 char *sub_name, *name;
 char *sfe= NULL, *sub_path= NULL;
 off_t mem;
 IsoNode **node_array= NULL;
 int node_count= 0, node_idx;

 /* Avoiding large local memory objects in order to save stack space */
 sfe= malloc(5*SfileadrL);
 sub_path= malloc(2*SfileadrL);
 if(sfe==NULL || sub_path==NULL) {
   Xorriso_no_malloc_memory(xorriso, &sfe, 0);
   {ret= -1; goto ex;}
 }

#ifndef Libisofs_iso_dir_iter_sufficienT
 /* Ticket 127: A80301 - A80302
    I do not not deem IsoDirIter safe for node list manipulations.
    The parameter boss_iter once was intended to allow such but
    has now been downgraded to a mere check for eventual programming bugs.
 */
 if(boss_iter!=NULL) {
   sprintf(xorriso->info_text,
       "Program error: Xorriso_rmi() was requested to delete iterated node %s",
       Text_shellsafe(path, sfe, 0));
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   ret= -1; goto ex;
 }
#endif /* Libisofs_iso_dir_iter_sufficienT */

 ret= Xorriso_get_volume(xorriso, &volume, 0);
 if(ret<=0)
   goto ex;
 
 if(Xorriso_much_too_long(xorriso, strlen(path), 0)<=0)
   {ret= 0; goto ex;}
 ret= Xorriso_node_from_path(xorriso, volume, path, &victim_node, 0);
 if(ret<=0)
   goto ex;
 root_dir= iso_image_get_root(volume);
 if(((void *) root_dir) == ((void *) victim_node) && !(flag & 1)) {
   sprintf(xorriso->info_text, "May not delete root directory");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }

 if(LIBISO_ISDIR(victim_node))
   is_dir= 1;
 if(!is_dir) {
   if(flag&2) { /* rmdir */
     sprintf(xorriso->info_text, "%s in loaded ISO image is not a directory",
             Text_shellsafe(path, sfe, 0));
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }
 } else {
   if(flag&1) { /* rm -r */
     if((xorriso->do_reassure==1 && !xorriso->request_not_to_ask) ||
        (flag&32) || ((void *) root_dir) == ((void *) victim_node)) {
       /* Iterate over subordinates and delete them */
       mem= boss_mem;

       ret= Xorriso_findi_iter(xorriso, (IsoDir *) victim_node, &mem,
                         &iter, &node_array, &node_count, &node_idx,
                         &node, 1|2);
       if(ret<=0) {
cannot_create_iter:;
         Xorriso_cannot_create_iter(xorriso, ret, 0);
         ret= -1; goto ex;
       }
       pl= strlen(path);
       strcpy(sub_path, path);
       if(pl==0 || sub_path[pl-1]!='/') {
         sub_path[pl++]= '/';
         sub_path[pl]= 0;
       }
       sub_name= sub_path+pl;
       while(1) { 
         ret= Xorriso_findi_iter(xorriso, (IsoDir *) victim_node, &mem, &iter,
                                &node_array, &node_count, &node_idx, &node, 0);
         if(ret<0)
           goto ex;
         if(ret==0 || xorriso->request_to_abort)
       break;
         name= (char *) iso_node_get_name(node);
         if(Xorriso_much_too_long(xorriso, pl+1+strlen(name), 0)<=0)
           {ret= 0; goto rm_r_problem_handler;}
         strcpy(sub_name, name);
         ret= Xorriso_rmi(xorriso, iter, mem, sub_path,
                          (flag & ( 1 | 2 | 8 | 16 | 64)) | 4);
         if(ret==3 || ret<=0 || xorriso->request_to_abort) {
rm_r_problem_handler:;
           not_removed= 1;
           fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
           if(fret<0)
             goto dir_not_removed;
         }
       }
       if(flag&32)
         {ret= 2; goto ex;}

       if(not_removed) {
dir_not_removed:;
         sprintf(xorriso->info_text, "Directory not removed: %s",
                 Text_shellsafe(path, sfe, 0));
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
         if(ret>0)
           ret= 3;
         goto ex;
       }
     }
   } else {
     if(!(flag&2)) { /* not rmdir */
       sprintf(xorriso->info_text, "%s in loaded ISO image is a directory",
               Text_shellsafe(path, sfe, 0));
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
     }

     ret= iso_dir_get_children((IsoDir *) victim_node, &iter);
     Xorriso_process_msg_queues(xorriso,0);
     if(ret<0)
       goto cannot_create_iter;
     if(ret>0) {
       if(iso_dir_iter_next(iter, &node) == 1) {
         sprintf(xorriso->info_text,
                 "Directory not empty on attempt to delete: %s",
                 Text_shellsafe(path, sfe, 0));
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         ret= 0; goto ex;
       }
     }
   }
 }

 if(((void *) root_dir) == ((void *) victim_node))
   {ret= 2; goto ex;}

 if(xorriso->request_to_abort)
   {ret= 3; goto ex;}
 boss_node= iso_node_get_parent(victim_node);
 Xorriso_process_msg_queues(xorriso,0);
 if(boss_node==NULL) {
   sprintf(xorriso->info_text,
           "Cannot find parent node of %s in loaded ISO image",
           Text_shellsafe(path, sfe, 0));
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   ret= 0; goto ex;
 }

 while((xorriso->do_reassure==1 || (xorriso->do_reassure==2 && !(flag&4)))
       && !xorriso->request_not_to_ask) {
   /* ls -ld */
   Xorriso_ls_filev(xorriso, xorriso->wdi, 1, &path, (off_t) 0, 1|2|8);
   if(is_dir) /* du -s */
     Xorriso_ls_filev(xorriso, xorriso->wdi, 1, &path, (off_t) 0, 2|4);
   if(flag&8)
     sprintf(xorriso->info_text,
  "File exists. Remove ?  n= keep old, y= remove, x= abort, @= stop asking\n");
   else
     sprintf(xorriso->info_text,
  "Remove above file ?  n= keep it, y= remove it, x= abort, @= stop asking\n");
   Xorriso_info(xorriso, 4);
   ret= Xorriso_request_confirmation(xorriso, 1|2|4|16);
   if(ret<=0)
     goto ex;
   if(xorriso->request_to_abort) {
     sprintf(xorriso->info_text,
             "Removal operation aborted by user before file: %s",
             Text_shellsafe(path, sfe, 0));
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
     ret= 3; goto ex;
   }
   if(ret==3)
 continue;
   if(ret==6) /* yes */
 break;
   if(ret==4) { /* yes, do not ask again */
     xorriso->request_not_to_ask= 1;
 break;
   }
   if(ret==1) { /* no */
     sprintf(xorriso->info_text, "Kept in existing state: %s",
             Text_shellsafe(path, sfe, 0));
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
     ret= 3; goto ex;
   }
 }

 if(!(flag & 64))
   Xorriso_invalidate_di_item(xorriso, victim_node, 0);

#ifdef Libisofs_iso_dir_iter_sufficienT

 if(boss_iter!=NULL) {
   ret= iso_dir_iter_remove((IsoDirIter *) boss_iter);
   if(ret<0)
     ret= -1;
 } else
   ret= iso_node_remove(victim_node);

#else /* ! Libisofs_iso_dir_iter_sufficienT */

 ret= iso_node_remove(victim_node);

#endif /* Libisofs_iso_dir_iter_sufficienT */

 Xorriso_process_msg_queues(xorriso,0);
 if(ret<0) {
   Xorriso_report_iso_error(xorriso, path, ret, "Cannot remove node", 0,
                            "FATAL", 1);
   sprintf(xorriso->info_text,
           "Internal failure to remove %s from loaded ISO image",
           Text_shellsafe(path, sfe, 0));
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   ret= -1; goto ex;
 }

 if(flag&16)
   xorriso->pacifier_count++;
 Xorriso_set_change_pending(xorriso, 0);
 ret= 1+!!is_dir;
ex:;
 if(sfe!=NULL)
   free(sfe);
 if(sub_path!=NULL)
   free(sub_path);
 Xorriso_findi_iter(xorriso, (IsoDir *) victim_node, &mem, &iter,
                    &node_array, &node_count, &node_idx, &node, (1<<31));
 return(ret);
} 


int Xorriso_overwrite_dest(struct XorrisO *xorriso, void *boss_iter,
                           char *eff_dest, int dest_ret, char *activity,
                           int flag)
{
 int ret;

 if(dest_ret==2 && xorriso->do_overwrite!=1) {
   sprintf(xorriso->info_text, "%s: May not overwrite directory: ", activity);
   Text_shellsafe(eff_dest, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 } else if (dest_ret==1 && !xorriso->do_overwrite) {
   sprintf(xorriso->info_text, "%s: May not overwite: ", activity);
   Text_shellsafe(eff_dest, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 } else if(dest_ret>0) {
   ret= Xorriso_rmi(xorriso, boss_iter, (off_t) 0, eff_dest, 1|8);
   if(ret<=0)
     return(0);
   if(ret==3) {
     sprintf(xorriso->info_text, "%s: User revoked removal of: ", activity);
     Text_shellsafe(eff_dest, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
     return(0);
   }
 }
 return(1);
}


/* @param boss_iter Opaque handle to be forwarded to actions in ISO image
                    Set to NULL if calling this function from outside ISO world
*/
int Xorriso_rename(struct XorrisO *xorriso, void *boss_iter,
                   char *origin, char *dest, int flag)
{
 int ret, ol, dest_ret;
 char *eff_dest= NULL, *dir_adr= NULL, *cpt;
 char *leafname, *eff_origin= NULL, *old_leafname;
 IsoImage *volume;
 IsoDir *origin_dir, *dest_dir;
 IsoNode *node, *iso_node;

 Xorriso_alloc_meM(eff_dest, char, SfileadrL);
 Xorriso_alloc_meM(dir_adr, char, SfileadrL);
 Xorriso_alloc_meM(eff_origin, char, SfileadrL);

#ifndef Libisofs_iso_dir_iter_sufficienT
 /* Ticket 127: A80301 - A80302
    I do not not deem IsoDirIter safe for node list manipulations.
    The parameter boss_iter once was intended to allow such but
    has now been downgraded to a mere check for eventual programming bugs.
 */
 if(boss_iter!=NULL) {
   sprintf(xorriso->info_text,
     "Program error: Xorriso_rename() was requested to delete iterated node ");
   Text_shellsafe(origin, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   {ret= -1; goto ex;}
 }
#endif /* Libisofs_iso_dir_iter_sufficienT */

 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, origin, eff_origin, 0);
 if(ret<=0)
   goto ex;
 dest_ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, dest, eff_dest,1);
 if(dest_ret<0)
   {ret= dest_ret; goto ex;}
 if(dest_ret==0) { /* obtain eff_dest address despite it does not exist */
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, dest, eff_dest, 2);
   if(ret<=0)
     goto ex;
 }

 /* Prevent that destination is a subordinate of origin
    (that would be a black hole plopping out of the universe) */
 ol= strlen(eff_origin);
 if(ol==0) {
   sprintf(xorriso->info_text, "May not rename root directory");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 } else if(strcmp(eff_origin, eff_dest)==0) {
   sprintf(xorriso->info_text, "Ignored attempt to rename ");
   Text_shellsafe(eff_origin, xorriso->info_text, 1);
   strcat(xorriso->info_text, " to itself");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
   {ret= 0; goto ex;}
 } else if(strncmp(eff_origin, eff_dest, ol)==0 &&
           (eff_dest[ol]==0 || eff_dest[ol]=='/')) {
   sprintf(xorriso->info_text, "May not rename ");
   Text_shellsafe(eff_origin, xorriso->info_text, 1);
   strcat(xorriso->info_text, " to its own sub address ");
   Text_shellsafe(eff_dest, xorriso->info_text, 1 | 2);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 }

 /* Check whether destination exists and may be not overwriteable */
 ret= Xorriso_overwrite_dest(xorriso, boss_iter,
                             eff_dest, dest_ret, "Renaming", 0);
 if(ret <= 0)
   goto ex;

 /* Ensure existence of destination directory */
 strcpy(dir_adr, eff_dest);
 cpt= strrchr(dir_adr, '/');
 if(cpt==NULL)
   cpt= dir_adr+strlen(dir_adr);
 *cpt= 0;
 if(dir_adr[0]!=0) {
   ret= Xorriso_graft_in(xorriso, boss_iter, NULL, dir_adr,
                         (off_t) 0, (off_t) 0, 1);
   if(ret<=0)
     goto ex;
 }

 /* Move node */
 ret= Xorriso_get_volume(xorriso, &volume, 0);
 if(ret<=0)
   goto ex;
 Xorriso_node_from_path(xorriso, volume, dir_adr, &iso_node, 0);
 dest_dir= (IsoDir *) iso_node;
 strcpy(dir_adr, eff_origin);
 cpt= strrchr(dir_adr, '/');
 if(cpt==NULL)
   cpt= dir_adr+strlen(dir_adr);
 *cpt= 0;
 Xorriso_node_from_path(xorriso, volume, dir_adr, &iso_node, 0);
 origin_dir= (IsoDir *) iso_node;
 Xorriso_node_from_path(xorriso, volume, eff_origin, &node, 0);
 if(dest_dir==NULL || origin_dir==NULL || node==NULL) {
   Xorriso_process_msg_queues(xorriso,0);
   sprintf(xorriso->info_text,
           "Internal error on rename: confirmed node turns out as NULL");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   {ret= -1; goto ex;}
 }
 ret= iso_node_take(node);
 if(ret<0) {
   Xorriso_process_msg_queues(xorriso,0);
   Xorriso_report_iso_error(xorriso, eff_dest, 0, "Cannot take", 0, "FATAL",1);
   sprintf(xorriso->info_text,
           "Internal error on rename: failed to take node");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   {ret= -1; goto ex;}
 }
 leafname= strrchr(eff_dest, '/');
 if(leafname==NULL)
   leafname= eff_dest;
 else
   leafname++;
 
 old_leafname= (char *) iso_node_get_name(node);
 if(strcmp(leafname, old_leafname)!=0)
   ret= iso_node_set_name(node, leafname);
 else
   ret= 1;
 if(ret<0) {
   Xorriso_process_msg_queues(xorriso,0);
   Xorriso_report_iso_error(xorriso, eff_dest, 0, "Cannot set name", 0,
                            "FAILURE", 1);
   {ret= -1; goto ex;}
 }
 Xorriso_process_msg_queues(xorriso,0);
 ret= iso_dir_add_node(dest_dir, node, 0);
 if(ret<0) {
   Xorriso_process_msg_queues(xorriso,0);
   Xorriso_report_iso_error(xorriso, eff_dest, 0, "Cannot add", 0, "FATAL", 1);
   sprintf(xorriso->info_text,
           "Internal error on rename: failed to insert node");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   {ret= -1; goto ex;}
 }
 Xorriso_set_change_pending(xorriso, 0);
 ret= 1;
ex:;
 Xorriso_free_meM(eff_dest);
 Xorriso_free_meM(dir_adr);
 Xorriso_free_meM(eff_origin);
 return(ret);
}


int Xorriso_cannot_clone(struct XorrisO *xorriso, char *eff_origin,
                         char *eff_dest, int iso_error, int flag)
{
 Xorriso_report_iso_error(xorriso, eff_dest, iso_error, "Cannot clone",
                          0, "FAILURE", 1);
 sprintf(xorriso->info_text, "Failed to clone ");
 Text_shellsafe(eff_origin, xorriso->info_text, 1);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
 return(0);
}


/* @param flag bit0= for iso_tree_clone() : merge directories
               bit1= do not issue NOTE message
*/
int Xorriso_clone_tree(struct XorrisO *xorriso, void *boss_iter,
                       char *origin, char *dest, int flag)
{
 int ret, dest_ret, l;
 char *eff_dest= NULL, *eff_origin= NULL, *dir_adr= NULL;
 char *leafname;
 IsoImage *volume;
 IsoDir *new_parent;
 IsoNode *origin_node, *dir_node, *new_node;

 Xorriso_alloc_meM(eff_dest, char, SfileadrL);
 Xorriso_alloc_meM(eff_origin, char, SfileadrL);
 Xorriso_alloc_meM(dir_adr, char, SfileadrL);

 ret= Xorriso_get_volume(xorriso, &volume, 0);
 if(ret <= 0)
   goto ex;

 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, origin, eff_origin, 0);
 if(ret<=0)
   goto ex;
 ret= Xorriso_node_from_path(xorriso, volume, eff_origin, &origin_node, 0);
 if(ret <= 0)
   goto ex;

 dest_ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, dest, eff_dest,1);
 if(dest_ret<0)
   {ret= dest_ret; goto ex;}
 if(dest_ret > 0) {
   if(eff_dest[0] == 0)
     strcpy(eff_dest, "/");
   sprintf(xorriso->info_text,
           "Cloning: Copy address already exists: ");
   Text_shellsafe(eff_dest, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   {ret= 0; goto ex;}
 } else {
   /* obtain eff_dest address despite it does not exist */
   ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, dest, eff_dest, 2);
   if(ret<=0)
     goto ex;
 }

 /* Obtain parent path and leaf name */
 strcpy(dir_adr, eff_dest);
 for(l= strlen(dir_adr); l > 0; ) {
   if(dir_adr[l - 1] == '/')
     dir_adr[--l]= 0;
   else
 break;
 }
 leafname= strrchr(dir_adr, '/');
 if(leafname == NULL) {
   leafname= dir_adr;
   if (leafname[0] == 0) {
     Xorriso_msgs_submit(xorriso, 0, "Empty file name as clone destination",
                         0, "FAILURE", 0);
     {ret= 0; goto ex;}
   }
 } else {
   *leafname= 0;
   leafname++;
   if(dir_adr[0] != 0) {
     /* Ensure existence of destination directory */
     ret= Xorriso_graft_in(xorriso, boss_iter, NULL, dir_adr,
                           (off_t) 0, (off_t) 0, 1);
     if(ret <= 0)
       goto ex;
   }
 }

 ret= Xorriso_node_from_path(xorriso, volume, dir_adr, &dir_node, 0);
 if(ret <= 0)
   goto ex;
 new_parent= (IsoDir *) dir_node;

 ret = iso_tree_clone(origin_node, new_parent, leafname, &new_node, flag & 1);
 Xorriso_process_msg_queues(xorriso,0);
 if(ret < 0) {
   Xorriso_cannot_clone(xorriso, eff_origin, eff_dest, ret, 0);
   {ret= 0; goto ex;}
 }
 Xorriso_set_change_pending(xorriso, 0);
 if(!(flag & 2)) {
   strcpy(xorriso->info_text, "Cloned in ISO image: ");
   Text_shellsafe(eff_origin, xorriso->info_text, 1);
   strcat(xorriso->info_text, " to ");
   Text_shellsafe(eff_dest, xorriso->info_text, 1 | 2);
   strcat(xorriso->info_text, "\n");
   Xorriso_info(xorriso, 0);
 }
 ret= 1;
ex:;
 Xorriso_free_meM(eff_dest);
 Xorriso_free_meM(eff_origin);
 Xorriso_free_meM(dir_adr);
 return(ret);
}


int Xorriso_clone_under(struct XorrisO *xorriso, char *origin, char *dest,
                        int flag)
{
 int ret, pass;
 char *eff_dest= NULL, *eff_origin= NULL, *namept;
 IsoDir *origin_dir, *dest_dir;
 IsoDirIter *iter= NULL;
 IsoNode *origin_node, *new_node;
 IsoImage *volume;

 Xorriso_alloc_meM(eff_dest, char, SfileadrL);
 Xorriso_alloc_meM(eff_origin, char, SfileadrL);

 ret= Xorriso_get_volume(xorriso, &volume, 0);
 if(ret <= 0)
   goto ex;
 ret= Xorriso_dir_from_path(xorriso, "Copy source", origin, &origin_dir, 0);
 if(ret <= 0)
   goto ex;
 ret= Xorriso_dir_from_path(xorriso, "Copy destination", dest, &dest_dir, 0);
 if(ret <= 0)
   goto ex;

 for(pass= 0; pass < 2; pass++) {
   ret= iso_dir_get_children(origin_dir, &iter);
   if(ret < 0) {
     Xorriso_cannot_create_iter(xorriso, ret, 0);
     {ret= -1; goto ex;}
   }
   Xorriso_process_msg_queues(xorriso,0);

   while(iso_dir_iter_next(iter, &origin_node) == 1) {
     namept= (char *) iso_node_get_name(origin_node);
     sprintf(eff_origin, "%s/%s", origin, namept);
     sprintf(eff_dest, "%s/%s", dest, namept);
     if(pass == 0) {
       ret= Xorriso_node_from_path(xorriso, volume, eff_dest, &new_node, 1);
       if(ret < 0)
         goto ex;
       if(ret > 0) {
         sprintf(xorriso->info_text, "Cloning: Copy address already exists: ");
         Text_shellsafe(eff_dest, xorriso->info_text, 1);
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         ret= 0; goto ex;
       }
     } else {
       ret = iso_tree_clone(origin_node, dest_dir, namept, &new_node, 1);
       Xorriso_process_msg_queues(xorriso,0);
       if(ret < 0) {
         Xorriso_cannot_clone(xorriso, eff_origin, eff_dest, ret, 0);
         ret= 0; goto ex;
       }
     }
   }   
   iso_dir_iter_free(iter);
   iter= NULL;
 }
 Xorriso_set_change_pending(xorriso, 0);
 ret= 1;
ex:;
 if(iter != NULL)
   iso_dir_iter_free(iter);
 Xorriso_free_meM(eff_dest);
 Xorriso_free_meM(eff_origin);
 Xorriso_process_msg_queues(xorriso,0);
 return(ret);
}


int Xorriso_set_st_mode(struct XorrisO *xorriso, char *in_path,
                        mode_t mode_and, mode_t mode_or, int flag)
{
 mode_t mode= 0;
 int ret;
 IsoNode *node;
 char *path= NULL;

 Xorriso_alloc_meM(path, char, SfileadrL);
 ret= Xorriso_get_node_by_path(xorriso, in_path, path, &node, 0);
 if(ret<=0)
   goto ex;
 mode= iso_node_get_permissions(node);
 mode= (mode & mode_and) | mode_or;
 iso_node_set_permissions(node, mode);
 iso_node_set_ctime(node, time(NULL));
 sprintf(xorriso->info_text,"Permissions now: %-5.5o  ",
         (unsigned int) (mode & 0xffff));
 Text_shellsafe(path, xorriso->info_text, 1);
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
 Xorriso_set_change_pending(xorriso, 0);
 Xorriso_process_msg_queues(xorriso,0);
 ret= 1;
ex:;
 Xorriso_free_meM(path);
 return(ret);
}


int Xorriso_set_uid(struct XorrisO *xorriso, char *in_path, uid_t uid,
                    int flag)
{
 int ret;
 IsoNode *node;

 ret= Xorriso_get_node_by_path(xorriso, in_path, NULL, &node, 0);
 if(ret<=0)
   return(ret);
 iso_node_set_uid(node, uid);
 iso_node_set_ctime(node, time(NULL));
 Xorriso_set_change_pending(xorriso, 0);
 Xorriso_process_msg_queues(xorriso,0);
 return(1);
}


int Xorriso_set_gid(struct XorrisO *xorriso, char *in_path, gid_t gid,
                    int flag)
{
 int ret;
 IsoNode *node;

 ret= Xorriso_get_node_by_path(xorriso, in_path, NULL, &node, 0);
 if(ret<=0)
   return(ret);
 iso_node_set_gid(node, gid);
 iso_node_set_ctime(node, time(NULL));
 Xorriso_set_change_pending(xorriso, 0);
 Xorriso_process_msg_queues(xorriso,0);
 return(1);
}


/* @parm flag  bit0= atime, bit1= ctime, bit2= mtime, bit8=no auto ctime */
int Xorriso_set_time(struct XorrisO *xorriso, char *in_path, time_t t,
                    int flag)
{
 int ret;
 IsoNode *node;

 ret= Xorriso_get_node_by_path(xorriso, in_path, NULL, &node, 0);
 if(ret<=0)
   return(ret);
 if(flag&1)
   iso_node_set_atime(node, t);
 if(flag&2)
   iso_node_set_ctime(node, t);
 if(flag&4)
   iso_node_set_mtime(node, t);
 if(!(flag&(2|256)))
   iso_node_set_ctime(node, time(NULL));
 Xorriso_set_change_pending(xorriso, 0);
 Xorriso_process_msg_queues(xorriso,0);
 return(1);
}


/*
  Apply the effect of mkisofs -r to a single node
*/
int Xorriso_mkisofs_lower_r(struct XorrisO *xorriso, IsoNode *node, int flag)
{
 mode_t perms;

 perms= iso_node_get_permissions(node);
 iso_node_set_uid(node, (uid_t) 0);
 iso_node_set_gid(node, (gid_t) 0);
 perms|= S_IRUSR | S_IRGRP | S_IROTH;
 perms&= ~(S_IWUSR | S_IWGRP | S_IWOTH);
 if(perms & (S_IXUSR | S_IXGRP | S_IXOTH))
   perms|= (S_IXUSR | S_IXGRP | S_IXOTH);
 perms&= ~(S_ISUID | S_ISGID | S_ISVTX);
 iso_node_set_permissions(node, perms);
 return(1);
}


/* @param node      Opaque handle to IsoNode which is to be manipulated
                    instead of path if it is not NULL.
   @param path      is used as address if node is NULL.
   @param access_text  "access" ACL in long text form
   @param default_text "default" ACL in long text form
   @param flag      bit0= do not warn of root directory if not capable of AAIP
   @return          >0 success , <=0 failure
*/
int Xorriso_setfacl(struct XorrisO *xorriso, void *in_node, char *path,
                    char *access_text, char *default_text, int flag)
{
 int ret;
 IsoNode *node;

 node= (IsoNode *) in_node;
 if(node == NULL) {
   ret= Xorriso_get_node_by_path(xorriso, path, NULL, &node, 0);
   if(ret<=0)
     goto ex;
 }
 ret= iso_node_set_acl_text(node, access_text, default_text, 0);
 if(ret <= 0) {
   Xorriso_report_iso_error(xorriso, "", ret,
                            "Error when setting ACL to image node",
                            0, "FAILURE", 1);
   if(path != NULL && path[0] != 0) {
     strcpy(xorriso->info_text, "Error with setting ACL of ");
     Text_shellsafe(path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   }
   ret= 0; goto ex;
 }
 Xorriso_set_change_pending(xorriso, 0);
 ret= 1;
ex:;
 Xorriso_process_msg_queues(xorriso,0);
 return(ret);
}


/* @param in_node       Opaque handle to IsoNode which is to be manipulated
                        instead of path if it is not NULL.
   @param path          is used as address if node is NULL.
   @param num_attrs     Number of attributes
   @param names         Array of pointers to 0 terminated name strings
   @param value_lengths Array of byte lengths for each attribute payload
   @param values        Array of pointers to the attribute payload bytes
   @param flag          bit0= Do not maintain eventual existing ACL of the node
                        bit1= Do not clear the existing attribute list
                        bit2= Delete the attributes with the given names
                        bit3= Allow non-user attributes.
                        bit4= do not warn of root if incapable of AAIP
   @return              >0 success , <=0 failure
*/
int Xorriso_setfattr(struct XorrisO *xorriso, void *in_node, char *path,
                     size_t num_attrs, char **names,
                     size_t *value_lengths, char **values, int flag)
{
 int ret;
 IsoNode *node;

 node= (IsoNode *) in_node;
 if(node == NULL) {
   ret= Xorriso_get_node_by_path(xorriso, path, NULL, &node, 0);
   if(ret<=0)
     goto ex;
 }
 ret= iso_node_set_attrs(node, num_attrs, names, value_lengths, values,
                         flag & (1 | 2 | 4 | 8));
 Xorriso_process_msg_queues(xorriso,0);
 if(ret <= 0) {
   Xorriso_report_iso_error(xorriso, "", ret,
                            "Error when setting ACL and xattr to image node",
                            0, "FAILURE", 1);
   if(path != NULL && path[0] != 0) {
     strcpy(xorriso->info_text, "Error with setting xattr of ");
     Text_shellsafe(path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   }
   ret= 0; goto ex;
 }
 Xorriso_set_change_pending(xorriso, 0);
 ret= 1;
ex:;
 Xorriso_process_msg_queues(xorriso,0);
 return(ret);
}


/*
   @param flag bit0= use parameters dev,ino rather than disk_path
               bit1= compare attribute rather than setting it
                     return: 0=dev,ino match, 1=mismatch, 2=no node attribute
                            -1=error
               bit5= if not bit0:
                     transfer dev,inode from eventual link target
               bit7= omit dev check mit bit1
*/
int Xorriso_record_dev_inode(struct XorrisO *xorriso, char *disk_path,
                             dev_t dev, ino_t ino,
                             void *in_node, char *iso_path, int flag)
{
 size_t l, di_l= 0;
 int i, ret;
 dev_t hdev;
 ino_t hino;
 char buf[66], *bufpt, *wpt, *di= NULL;
 static char *name= "isofs.di";
 struct stat stbuf;

 if(!(flag & 1)) {
   if(flag & 32) {
     if(stat(disk_path, &stbuf) == -1)
       return(-1);
   } else {
     if(lstat(disk_path, &stbuf) == -1)
       return(-1);
   }
   dev= stbuf.st_dev;
   ino= stbuf.st_ino;
 }
   
 wpt= buf;
 hdev= dev;
 for(i= 0; hdev != 0; i++)
   hdev= hdev >> 8;
 l= i;
 *(wpt++)= l;
 for(i= 0; i < (int) l; i++)
   *(wpt++)= dev >> (8 * (l - i - 1));
 hino= ino;
 for(i= 0; hino != 0; i++)
   hino= hino >> 8;
 l= i;
 *(wpt++)= l;
 for(i= 0; i < (int) l; i++)
   *(wpt++)= ino >> (8 * (l - i - 1));
 l= wpt - buf; 
 bufpt= buf;

 if(flag & 2) {
   /* Compare node attribute with bufpt,l */
   ret= Xorriso_get_attr_value(xorriso, in_node, iso_path,
                               "isofs.di", &di_l, &di, 0);
   if(ret < 0)
     goto ex;
   if(ret == 0)
     {ret= 2; goto ex;}
   if(flag & 128) {
     if(di_l <= 0)
       {ret= 1; goto ex;}
     hino= 0;
     for(i= di[0] + 2; i < (int) di_l && i - di[0] - 2 < di[(int) di[0] + 1];
         i++)
       hino= (hino << 8) | ((unsigned char *) di)[i];
     if(hino != ino)
       {ret= 1; goto ex;} 
   } else {
     if(l != di_l)
       {ret= 1; goto ex;}
     for(i= 0; i < (int) l; i++)
       if(di[i] != buf[i])
         {ret= 1; goto ex;} 
   }
   ret= 0;
 } else {
   ret= Xorriso_setfattr(xorriso, in_node, iso_path,
                         (size_t) 1, &name, &l, &bufpt, 2 | 8);
 }
ex:;
 if(di != NULL)
   free(di);
 return(ret);
}


/* @return  see Xorriso_update_interpreter()
*/
int Xorriso_widen_hardlink(struct XorrisO *xorriso, void * boss_iter,
                           IsoNode *node,
                           char *abs_path, char *iso_prefix, char *disk_prefix,
                           int flag)
{
 int ret= 0, idx, low, high, i, do_widen= 0, compare_result= 0;
 char *disk_path;

 Xorriso_alloc_meM(disk_path, char, SfileadrL);

 /* Lookup all di_array instances of node */
 if(LIBISO_ISDIR(node))
   {ret= 3; goto ex;}
 ret= Xorriso_search_di_range(xorriso, node, &idx, &low, &high, 2);
 if(ret <= 0)
   {ret= 3; goto ex;}
 /* Check and reset di_do_widen bits */
 for(i= low; i <= high; i++) {
   if(node != xorriso->di_array[i]) /* might be NULL */
 continue;
   if(xorriso->di_do_widen[i / 8] & (1 << (i % 8)))
     do_widen= 1;
   xorriso->di_do_widen[i / 8]&= ~(1 << (i % 8));
 }
 if(idx < 0 || !do_widen)
   {ret= 3; goto ex;}

 ret= Xorriso_pfx_disk_path(xorriso, abs_path, iso_prefix, disk_prefix,
                            disk_path, 0);
 if(ret <= 0)
   goto ex;
 ret= Sfile_type(disk_path, 1);
 if(ret < 0)
   {ret= 3; goto ex;} /* does not exist on disk */

 /* >>> compare_result bit17 = is_split */;

 ret= Xorriso_update_interpreter(xorriso, boss_iter, NULL,
                                 compare_result, disk_path, abs_path, 1);
 if(ret <= 0)
   goto ex;
ex:;
 Xorriso_free_meM(disk_path);
 return(ret);
}


int Xorriso_set_hidden(struct XorrisO *xorriso, void *in_node, char *path,
                       int hide_state, int flag)
{
 int ret, hide_attrs= 0;
 IsoNode *node;

 node= (IsoNode *) in_node;
 if(node == NULL) {
   ret= Xorriso_get_node_by_path(xorriso, path, NULL, &node, 0);
   if(ret<=0)
     return(ret);
 }
 if(hide_state) {
   hide_attrs|= LIBISO_HIDE_BUT_WRITE;
   if(hide_state & 1)
     hide_attrs|= LIBISO_HIDE_ON_RR;
   if(hide_state & 2)
     hide_attrs|= LIBISO_HIDE_ON_JOLIET;
   if(hide_state & 4)
     hide_attrs|= LIBISO_HIDE_ON_HFSPLUS;
 }
 iso_node_set_hidden(node, hide_attrs);
 return(1);
}


/* @param flag bit0= increase only upper estimation
*/
int Xorriso_estimate_file_size(struct XorrisO *xorriso, struct FindjoB *job,
                       char *basename, mode_t st_mode, off_t st_size, int flag)
{
 off_t upper, lower, size;

  lower = 3 * strlen(basename) + 34; /* >>> + minimum RR ? */
  upper = 3 * strlen(basename) + 2048;
  if(S_ISREG(st_mode)) {
    size= ((st_size + (off_t) 2047) / (off_t) 2048) * (off_t) 2048;
    lower+= size;
    upper+= size;
  } else if(S_ISDIR(st_mode)) {
    upper+= 4096;
  }
  job->estim_upper_size+= upper;
  if(!(flag & 1))
    job->estim_lower_size+= lower;
  return(1);
}


int Xorriso_cannot_create_iter(struct XorrisO *xorriso, int iso_error,int flag)
{
 Xorriso_process_msg_queues(xorriso,0);
 Xorriso_report_iso_error(xorriso, "", iso_error, "Cannot create iter", 0,
                          "FATAL", 1);
 sprintf(xorriso->info_text, "Cannot create IsoDirIter object");
 Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
 return(1);
}


/* The caller shall make no assumptions about the meaning of iter, node_array,
   node_count, node_idx ! They are just opaque handles for which the caller
   provides the memory of proper type.
   @param flag bit0= initialize iteration
               bit1= action needs full freedom of object manipulation
               bit2= action needs LBA sorted iteration
               bit31= end iteration (mandatory !)
*/
int Xorriso_findi_iter(struct XorrisO *xorriso, IsoDir *dir_node, off_t *mem,
                       IsoDirIter **iter, 
                       IsoNode ***node_array, int *node_count, int *node_idx,
                       IsoNode **iterated_node, int flag)
{
 int ret, i;
 IsoNode *node;
 off_t new_mem= 0;
 char mem_text[80], limit_text[80];

 if(flag&1) {
   *node_array= NULL;
   *node_count= -1;
   *node_idx= 0;
   *iter= NULL;
   ret= iso_dir_get_children(dir_node, iter);
   if(ret<0) {
cannot_iter:;
     Xorriso_cannot_create_iter(xorriso, ret, 0);
     return(-1);
   }
   if((flag&2)|(flag&4)) {
     /* copy list of nodes and prepare soft iterator */
     *node_count= 0;
     while(iso_dir_iter_next(*iter, &node) == 1)
       (*node_count)++;
     iso_dir_iter_free(*iter);
     *iter= NULL;

     new_mem= ((*node_count)+1) * sizeof(IsoNode *);
     if(new_mem > xorriso->temp_mem_limit) {
       Sfile_scale((double) new_mem, mem_text, 5,1e4, 0);
       Sfile_scale((double) xorriso->temp_mem_limit, limit_text, 5,1e4, 0);
       sprintf(xorriso->info_text,
           "Stacked directory snapshots exceed -temp_mem_limit (%s > %s)",
           mem_text, limit_text);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       *node_count= -1;
       return(-1);
     }
     (*node_array)= (IsoNode **) calloc((*node_count)+1, sizeof(IsoNode *));
     if(*node_array == NULL) {
       sprintf(xorriso->info_text,
               "Could not allocate inode list of %.f bytes",
               ((double) (*node_count)+1) * (double) sizeof(IsoNode *));
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
       *node_count= -1;
       return(-1);
     }
     *mem= new_mem;
     ret= iso_dir_get_children(dir_node, iter);
     if(ret<0)
       goto cannot_iter;
     while(iso_dir_iter_next(*iter, &node) == 1 && *node_idx < *node_count) {
       (*node_array)[*node_idx]= node;
       iso_node_ref(node);
       (*node_idx)++;
     }
     iso_dir_iter_free(*iter);
     *iter= NULL;
     *node_count= *node_idx;
     *node_idx= 0;
     if((flag&4) && *node_count>1)
       qsort(*node_array, *node_count, sizeof(IsoNode *),
             Xorriso__node_lba_cmp);
   }
 }

 if(flag&(1<<31)) {
   if(*node_count>=0 && *node_array!=NULL) {
     for(i= 0; i<*node_count; i++)
       iso_node_unref((*node_array)[i]);
     free(*node_array);
     *node_array= NULL;
     *node_count= -1;
     *node_idx= 0;
   } else {
     if(*iter!=NULL)
       iso_dir_iter_free(*iter);
     *iter= NULL;
   }
 }

 if(flag&(1|(1<<31))) 
   return(1);
 if(*node_count>=0) {
   /* return next node_array element */
   if(*node_idx>=*node_count)
     return(0);
   *iterated_node= (*node_array)[*node_idx];
   (*node_idx)++;
 } else {
   ret= iso_dir_iter_next(*iter, iterated_node);
   return(ret == 1);
 }
 return(1);
}


/* @param flag bit0= not a command parameter (directory iteration or recursion)
               bit1= do not count deleted files with rm and rm_r
   @return <=0 error,
             1=ok
             2=ok, node has been deleted,
             3=ok, do not dive into directory (e.g. because it is a split file)
             4=ok, end findjob gracefully
*/
int Xorriso_findi_action(struct XorrisO *xorriso, struct FindjoB *job,
                         IsoDirIter *boss_iter, off_t boss_mem,
                         char *abs_path, char *show_path, 
                         IsoNode *node, int depth, int flag)
{
 int ret= 0, type, action= 0, hflag, deleted= 0, no_dive= 0, i, bless_idx;
 int unbless= 0;
 uid_t user= 0;
 gid_t group= 0;
 time_t date= 0;
 mode_t mode_or= 0, mode_and= ~1;
 char *target, *text_2, *iso_prefix, md5[16], *basename, bless_code[17];
 struct FindjoB *subjob;
 struct stat dir_stbuf, stbuf;
 void *xinfo;
 struct iso_hfsplus_xinfo_data *hfsplus_xinfo;
 size_t value_length;
 char *value;

 action= Findjob_get_action_parms(job, &target, &text_2, &user, &group,
                                &mode_and, &mode_or, &type, &date, &subjob, 0);
 if(action<0)
   action= 0;
 job->match_count++;

 hflag= 16*!(flag&2);
 ret= 1;
 if(action==1) { /* rm (including rmdir) */
   ret= Xorriso_fake_stbuf(xorriso, abs_path, &dir_stbuf, &node, 1);
   if(ret>0) {
     if(S_ISDIR(dir_stbuf.st_mode))
       hflag= 2;
     ret= Xorriso_rmi(xorriso, boss_iter, boss_mem, abs_path, hflag);
     deleted= 1;
   }
 } else if(action==2) { /* rm_r */
   ret= Xorriso_rmi(xorriso, boss_iter, boss_mem, abs_path, 1|hflag);
   deleted= 1;
 } else if(action==3) {

   /* >>> mv target */;

 } else if(action==4) { /* chown */
   ret= Xorriso_set_uid(xorriso, abs_path, user, 0);
 } else if(action==5) { /* chgrp */
   ret= Xorriso_set_gid(xorriso, abs_path, group, 0);
 } else if(action==6) { /* chmod */
   ret= Xorriso_set_st_mode(xorriso, abs_path, mode_and, mode_or, 0);
 } else if(action==7) { /* alter_date */
   ret= Xorriso_set_time(xorriso, abs_path, date, type&7);
 } else if(action==8) { /* lsdl */
   ret= Xorriso_ls_filev(xorriso, "", 1, &abs_path, (off_t) 0, 1|2|8);
 } else if(action>=9 && action<=13) { /* actions which have own findjobs */
   Findjob_set_start_path(subjob, abs_path, 0);
   ret= Xorriso_findi(xorriso, subjob, boss_iter, boss_mem, NULL,
                      abs_path, &dir_stbuf, depth, 1);
 } else if(action==14 || action==17 || action == 41) {
                                          /* compare , update , update_merge */
   Findjob_get_start_path(job, &iso_prefix, 0);
   ret= Xorriso_find_compare(xorriso, (void *) boss_iter, (void *) node,
                         abs_path, iso_prefix, target,
                         (action == 17 || action == 41)
                         | ((flag&1)<<1) | ((action == 41) << 2));
   if(ret==2)
     deleted= 1;
   if(ret==3)
     no_dive= 1;
   if(ret>=0)
     ret= 1;
 } else if(action==16 || action==18) { /* not_in_iso , add_missing */
   ;
 } else if(action == 21) { /* report_damage */
   ret= Xorriso_report_damage(xorriso, show_path, node, 0);
 } else if(action == 22) {
   ret= Xorriso_report_lba(xorriso, show_path, node, 0);
 } else if(action == 23) { /* internal: memorize path of last matching node */
   ret= Findjob_set_found_path(job, show_path, 0);
 } else if(action == 24) {
   ret= Xorriso_getfacl(xorriso, (void *) node, show_path, NULL, 0);
 } else if(action == 25) {
   if(target == NULL || target[0] || text_2 == NULL || text_2[0])
     ret= Xorriso_setfacl(xorriso, (void *) node, show_path, target, text_2,0);
 } else if(action == 26) {
   ret= Xorriso_getfattr(xorriso, (void *) node, show_path, NULL, 0);
 } else if(action == 27) {
   ret= Xorriso_path_setfattr(xorriso, (void *) node, show_path,
                              target, strlen(text_2), text_2, 0);
 } else if(action == 28) { /* set_filter */
   ret= Xorriso_set_filter(xorriso, (void *) node, show_path, target, 1 | 2);
 } else if(action == 29) { /* show_stream */
   ret= Xorriso_show_stream(xorriso, (void *) node, show_path, 1 | 2);
 } else if(action == 30) { /* internal: count */
   xorriso->node_counter++;
 } else if(action == 31) { /* internal: register */
   if(xorriso->node_counter < xorriso->node_array_size) {
     xorriso->node_array[xorriso->node_counter++]= (void *) node;
     iso_node_ref(node); /* In case node gets deleted from tree during
                            the lifetime of xorriso->node_array */
   }
 } else if(action == 32) { /* internal: widen_hardlinks disk_equiv */
   Findjob_get_start_path(job, &iso_prefix, 0);
   ret= Xorriso_widen_hardlink(xorriso, (void *) boss_iter, node, abs_path,
                               iso_prefix, target, 0);
   if(ret==2)
     deleted= 1;
 } else if(action == 33) { /* get_any_xattr */
   ret= Xorriso_getfattr(xorriso, (void *) node, show_path, NULL, 8);
 } else if(action == 34) { /* get_md5 */
   ret= Xorriso_get_md5(xorriso, (void *) node, show_path, md5, 0);
   if(ret >= 0)
     ret= 1;
 } else if(action == 35) { /* check_md5 */
   ret= Xorriso_check_md5(xorriso, (void *) node, show_path, 2);
   if(ret == 0)
     xorriso->find_check_md5_result|= 1;
   else if(ret < 0)
     xorriso->find_check_md5_result|= 2;
   else if(ret == 1)
     xorriso->find_check_md5_result|= 8;
   else if(ret == 2)
     xorriso->find_check_md5_result|= 4;
   if(ret >= 0)
     ret= 1;
 } else if(action == 36) { /* make_md5 */
   ret= Xorriso_make_md5(xorriso, (void *) node, show_path, 0);
   if(ret >= 0)
     ret= 1;
 } else if(action == 37) { /* mkisofs_r */
   ret= Xorriso_mkisofs_lower_r(xorriso, node, 0);
 } else if(action == 38) { /* sort_weight */
   iso_node_set_sort_weight(node, type);
 } else if(action == 39) { /* hide */
   Xorriso_set_hidden(xorriso, node, NULL, type, 0);
 } else if(action == 40) { /* estimate_size */
   basename= strrchr(abs_path, '/');
   if(basename != NULL)
     basename++;
   else
     basename= abs_path;
   ret= Xorriso_fake_stbuf(xorriso, "", &stbuf, &node, 1);
   if(ret > 0)
     ret= Xorriso_estimate_file_size(xorriso, job, basename, stbuf.st_mode,
                                     stbuf.st_size, 0);
 } else if(action == 42) { /* rm_merge */
   ret= Xorriso_mark_update_merge(xorriso, show_path, node, 2 | 4);
   if(ret == 2) {
     ret= Xorriso_rmi(xorriso, boss_iter, boss_mem, abs_path, 1|hflag);
     sprintf(xorriso->info_text, "Deleted ");
     Text_shellsafe(show_path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "UPDATE", 0);
     deleted= 1;
   }
 } else if(action == 43) { /* clear_merge */
   ret= Xorriso_mark_update_merge(xorriso, show_path, node, 2 | 4);
 } else if(action == 44) { /* list_extattr */
   ret= Xorriso_list_extattr(xorriso, (void *) node, show_path, show_path,
                             target, 0);
 } else if(action == 45) { /* set_hfs_crtp */
   ret= Xorriso_hfsplus_file_creator_type(xorriso, show_path, (void *) node, 
                                          target, text_2, 0);

 } else if(action == 46) { /* get_hfs_crtp */
   ret= iso_node_get_xinfo(node, iso_hfsplus_xinfo_func, &xinfo);
   if(ret < 0) {
     Xorriso_process_msg_queues(xorriso, 0);
     ret= 0;
   } else if(ret == 1) {
     hfsplus_xinfo= (struct iso_hfsplus_xinfo_data *) xinfo;
     for(i= 0; i < 4; i++)
       xorriso->result_line[i]= hfsplus_xinfo->creator_code[i];
     xorriso->result_line[4]= ' ';
     for(i= 0; i < 4; i++)
       xorriso->result_line[5 + i]= hfsplus_xinfo->type_code[i];
     xorriso->result_line[9]= ' ';
     xorriso->result_line[10]= 0;
     Text_shellsafe(show_path, xorriso->result_line, 1);
     strcat(xorriso->result_line, "\n");
     Xorriso_result(xorriso, 0);
   }
   ret= 1;
 } else if(action == 47) { /* set_hfs_bless */
   if(strcmp(target, "none") == 0 ||
      strcmp(target, "n") == 0 || strcmp(target, "N") == 0) {
     ret= Xorriso_get_blessing(xorriso, node, &bless_idx, bless_code, 0);
     if(ret < 0)
       return(ret);
     if(ret == 0)
       return(1);
     unbless= 1;
   }
   ret= Xorriso_hfsplus_bless(xorriso, show_path, (void *) node, target, 0);
   /* If successful, end -find run gracefully */
   if(ret > 0) {
     if(unbless) {
       sprintf(xorriso->info_text, "HFS blessing '%s' revoked from ",
                                   bless_code);
     } else {
       sprintf(xorriso->info_text, "HFS blessing '%s' issued to ", target);
     }
     Text_shellsafe(show_path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
   }
   if(!unbless)
     return(4);
 } else if(action == 48) { /* get_hfs_bless */
   ret= Xorriso_get_blessing(xorriso, node, &bless_idx, bless_code, 0);
   if (ret > 0) {
     sprintf(xorriso->result_line, "%-16.16s ", bless_code);
     Text_shellsafe(show_path, xorriso->result_line, 1);
     strcat(xorriso->result_line, "\n");
     Xorriso_result(xorriso, 0);
   } else if(ret == 0)
     ret= 1;
 } else if(action == 49) {
   /* internal: update creator, type, and blessings from persistent isofs.* */
   ret= Xorriso_get_attr_value(xorriso, node, show_path, "isofs.hx",
                               &value_length, &value, 0);
   if(ret < 0) 
     return(ret);
   if(ret > 0 && value_length >= 10) {
     ret= Xorriso_hfsplus_file_creator_type(xorriso, show_path, (void *) node,
                                            value + 2, value + 6, 4);
     free(value);
     if(ret <= 0) 
       return(ret);
   }
   ret= Xorriso_get_attr_value(xorriso, node, show_path, "isofs.hb",
                               &value_length, &value, 0);
   if(ret < 0) 
     return(ret);
   if(ret > 0 && value_length >= 1) {
     bless_code[0]= value[0];
     bless_code[1]= 0;
     ret= Xorriso_hfsplus_bless(xorriso, show_path, (void *) node,
                                bless_code, 0);
     if(ret <= 0)
       return(ret);
   }
   ret= 1;
   
 } else { /* includes : 15 in_iso */
   Text_shellsafe(show_path, xorriso->result_line, 0);
   strcat(xorriso->result_line, "\n");
   Xorriso_result(xorriso, 0);
   ret= 1;
 }
 if(ret<=0)
   return(ret);
 if(deleted)
   return(2);
 if(no_dive)
   return(3);
 return(1);
}


/* flag bit0= perform -disk_path rather than -disk_name
*/
int Exprtest_match_disk_name(struct XorrisO *xorriso, struct ExprtesT *ftest,
                             IsoNode *node, int flag)

{
 int ret;
 char *disk_path= NULL, *npt;
 regmatch_t name_match;
 void *arg2;

 Xorriso_alloc_meM(disk_path, char, SfileadrL);

 ret= Xorriso_retrieve_disk_path(xorriso, node, disk_path, 0);
 if(ret <= 0)
   {ret= 0; goto ex;}
 if(flag & 1) {
   if(strcmp(disk_path, ftest->arg1) == 0)
     {ret= 1; goto ex;}
   {ret= 0; goto ex;}
 }
 arg2= ftest->arg2;
 npt= strrchr(disk_path, '/');
 if(npt != NULL)
   npt++;
 else
   npt= disk_path;
 ret= ! regexec(arg2, npt, 1, &name_match, 0);
ex:;
 Xorriso_free_meM(disk_path);
 return(ret);
}


int Exprtest_match(struct XorrisO *xorriso, struct ExprtesT *ftest,
                   void *node_pt, char *name, char *path,
                   struct stat *boss_stbuf, struct stat *stbuf, int flag)
/*
return:
 <0 = error
  0 = does not match
  1 = does match
  2 = immediate decision : does not match
  3 = immediate decision : does match
*/
{
 int value=0, ret, start_lba, end_lba, bless_idx;
 int lba_count, *file_end_lbas= NULL, *file_start_lbas= NULL, i, mask;
 void *arg1, *arg2;
 char ft, *decision, md5[16], bless_code[17];
 regmatch_t name_match;
 off_t damage_start, damage_end, size;
 void *xinfo_dummy;
 IsoNode *node;
 IsoStream *stream;
 struct iso_hfsplus_xinfo_data *hfsplus_xinfo;

 if(ftest == NULL)
   return(1);

 node= (IsoNode *) node_pt;
 arg1= ftest->arg1;
 arg2= ftest->arg2;

 if(node == NULL) {
   if(ftest->test_type > 2 && ftest->test_type != 4) {
     value= 0;
     goto ex;
   }
 }

 switch(ftest->test_type) {
 case 0: /* -false */
   value= 0;

 break; case 1: /* -name *arg1 (regex in *arg2) */
   ret= regexec(arg2, name, 1, &name_match, 0);
   value= !ret;

 break; case 2: /* -type *arg1 */
   value= 1;
   ft= *((char *) arg1);
   if(ft!=0) {
     if(S_ISBLK(stbuf->st_mode)) {
       if(ft!='b')
         value= 0;
     } else if(S_ISCHR(stbuf->st_mode)) {
       if(ft!='c')
         value= 0;
     } else if(S_ISDIR(stbuf->st_mode)) {
       if(ft=='m') {
         if(node != NULL)
           value= 0;
         else if(boss_stbuf==NULL)
           value= 0;
         else if(boss_stbuf->st_dev == stbuf->st_dev)
           value= 0;
       } else if(ft!='d')
         value= 0;
     } else if(S_ISFIFO(stbuf->st_mode)) {
       if(ft!='p')
         value= 0;
     } else if(S_ISREG(stbuf->st_mode)) {
       if(ft!='f' && ft!='-')
         value= 0;
     } else if(((stbuf->st_mode)&S_IFMT)==S_IFLNK) {
       if(ft!='l')
         value= 0;
     } else if(((stbuf->st_mode)&S_IFMT)==S_IFSOCK) {
       if(ft!='s')
         value= 0;
     } else if((flag & 1) && ((stbuf->st_mode) & S_IFMT) == Xorriso_IFBOOT) {
       if(ft!='e' || node == NULL)
         value= 0;
     } else {
       if(ft!='X')
         value= 0;
     }
   }

 break; case 3: /* -damaged */;
   value= Xorriso_file_eval_damage(xorriso, node, &damage_start, &damage_end,
                                   0);
   if(value > 0)
     value= 1;

 break; case 4: /* -lba_range *arg1 *arg2 */
   value= 1;
   start_lba= *((int *) ftest->arg1);
   end_lba= *((int *) ftest->arg2);
   if(node == NULL) {
     value= !(start_lba >= 0);
     goto ex;
   }
   ret= Xorriso__start_end_lbas(node, &lba_count,
                                &file_start_lbas, &file_end_lbas, &size, 0);
   if(ret <= 0) {
     if(ret < 0)
       Xorriso_process_msg_queues(xorriso, 0);
     if(start_lba >= 0)
       value= 0;
   } else {
     for(i= 0; i < lba_count; i++) {
       if(start_lba >= 0) {
         if(file_end_lbas[i] < start_lba || file_start_lbas[i] > end_lba)
           value= 0;
       } else {
         if(file_end_lbas[i] >= -start_lba && file_start_lbas[i] <= -end_lba)
           value= 0;
       }
     }
   }

 break; case 5: /* -has_acl */
   ret = Xorriso_getfacl(xorriso, (void *) node, "", NULL, 2);
   if(ret <= 0) {
     value= -1;
     Xorriso_process_msg_queues(xorriso, 0);
     goto ex;
   }
   value= (ret == 1);

 break; case 6: /* -has_xattr */
        case 14: /* -has_any_xattr */
   ret = Xorriso_getfattr(xorriso, (void *) node, "", NULL,
                          64 | (8 * (ftest->test_type == 14)));
   if(ret < 0) {
     value= -1;
     Xorriso_process_msg_queues(xorriso, 0);
     goto ex;
   }
   value= (ret > 0);

 break; case 7: /* -has_aaip */
   ret= iso_node_get_xinfo(node, aaip_xinfo_func, &xinfo_dummy);
   if(ret < 0) {
     value= -1;
     Xorriso_process_msg_queues(xorriso, 0);
     goto ex;
   }
   value= (ret > 0);

 break; case 8: /* -has_filter */
   value= 0;
   if(LIBISO_ISREG(node)) {
     stream= iso_file_get_stream((IsoFile *) node);
     if(iso_stream_get_input_stream(stream, 0) != NULL)
       value= 1;
   }

 break; case 9: /* -wanted_node arg1 (for internal use) */
   value= (((IsoNode *) arg1) == node);

 break; case 10: /* -pending_data */
   value= 1;
   if(!LIBISO_ISREG(node)) {
     value= 0;
   } else {
     ret= Xorriso__file_start_lba(node, &start_lba, 0);
     if(ret > 0 && start_lba >= 0)
       value= 0;
   }

 break; case 11: /* -decision */
   value= 2;
   decision= (char *) arg1;
   if(strcmp(decision, "yes") == 0 || strcmp(decision, "true") == 0)
     value= 3;

 break; case 12: /* -prune */
   value= 1;
   ftest->boss->prune= 1;

 break; case 13: /* -wholename *arg1 (regex in *arg2) */
   ret= regexec(arg2, path, 1, &name_match, 0);
   value= !ret;

 break; case 15: /* -has_md5 */
   ret= Xorriso_get_md5(xorriso, node, path, md5, 1);
   value= (ret > 0);

 break; case 16: /* -disk_name *arg1 (regex in *arg2) */
   value= !! Exprtest_match_disk_name(xorriso, ftest, node, 0);

 break; case 17: /* -hidden int *arg1 */
   value= 1;
   ret= iso_node_get_hidden(node);
   mask= *((int *) arg1) & 3;
   if((!!(mask & 1)) ^ (!!(ret & LIBISO_HIDE_ON_RR)))
     value= 0;
   if((!!(mask & 2)) ^ (!!(ret & LIBISO_HIDE_ON_JOLIET)))
     value= 0;
   if((!!(mask & 3)) ^ (!!(ret & LIBISO_HIDE_ON_HFSPLUS)))
     value= 0;
   
 break; case 18: /* -has_hfs_crtp char *creator char *type */
   ret= iso_node_get_xinfo(node, iso_hfsplus_xinfo_func, &xinfo_dummy);
   value= 0;
   if(ret < 0) {
     Xorriso_process_msg_queues(xorriso, 0);
     ret= 0;
   } else if(ret == 1) {
     hfsplus_xinfo= (struct iso_hfsplus_xinfo_data *) xinfo_dummy;
     if((strlen(arg1) == 1 ||
         (strncmp(arg1, (char *) hfsplus_xinfo->creator_code, 4) == 0 &&
          strlen(arg1) == 4)) &&
        (strlen(arg2) == 1 || 
         (strncmp(arg2, (char *) hfsplus_xinfo->type_code, 4) == 0 &&
          strlen(arg2) == 4)))
       value= 1;
   }

 break; case 19: /* -has_hfs_bless int bless_index */
   value= 0;
   ret= Xorriso_get_blessing(xorriso, node, &bless_idx, bless_code, 0);
   if (ret > 0) {
     if(*((int *) arg1) == (int) ISO_HFSPLUS_BLESS_MAX ||
        *((int *) arg1) == bless_idx)
       value= 1;
   }

 break; case 20: /* -disk_path */
   value= !! Exprtest_match_disk_name(xorriso, ftest, node, 1);

 break; default:

   /* >>> complain about unknown test type */;

   value= -1;

 }

ex:;
 if(ftest->invert && value<=1 && value>=0)
   value= !value;
 if(file_start_lbas != NULL)
   free((char *) file_start_lbas);
 if(file_end_lbas != NULL)
   free((char *) file_end_lbas);
 return(value);
}


/* @return <0 = error , 0 = no match , 1 = match */
int Xorriso_findi_test(struct XorrisO *xorriso, struct FindjoB *job,
                       IsoNode *node, char *name, char *path,
                       struct stat *boss_stbuf, struct stat *stbuf,
                       int depth, int flag)
{
 int ret;

 job->prune= 0;
 ret= Findjob_test_2(xorriso, job, node, name, path, boss_stbuf, stbuf, 1);
 if(ret <= 0)
   return(ret);
 return(1);
}


int Xorriso_findi_headline(struct XorrisO *xorriso, struct FindjoB *job,
                           int flag)
{
 int action;

 action= Findjob_get_action(job, 0);
 if(action == 21) {                                         /* report_damage */
   sprintf(xorriso->result_line, "Report layout: %8s , %8s , %8s , %s\n",
           "at byte", "Range", "Filesize", "ISO image path");
   Xorriso_result(xorriso, 0);
 } else if(action == 22) {                                     /* report_lba */
   sprintf(xorriso->result_line,
           "Report layout: %2s , %8s , %8s , %8s , %s\n",
           "xt", "Startlba", "Blocks", "Filesize", "ISO image path");
   Xorriso_result(xorriso, 0);
 }
 return(1);
}


/* @param flag bit0= recursion
               bit1= do not count deleted files with rm and rm_r
               bit2= do not dive into split file directories
                     (implicitly given with actions 14=compare and 17=update)
   @return <=0 error, 1= ok , 2= dir node and path has been deleted
                      4= end gracefully
*/
int Xorriso_findi(struct XorrisO *xorriso, struct FindjoB *job,
                  void *boss_iter, off_t boss_mem,
                  void *dir_node_generic, char *dir_path,
                  struct stat *dir_stbuf, int depth, int flag)
{
 int ret, action= 0, hflag, deleted= 0, no_dive= 0;
 IsoDirIter *iter= NULL;
 IsoDir *dir_node= NULL;
 IsoNode *node, *iso_node;
 IsoImage *volume= NULL;
 struct stat stbuf;
 char *name;
 off_t mem;
 IsoNode **node_array= NULL;
 int node_count, node_idx;
 char *path= NULL, *abs_path= NULL;

 if(xorriso->request_to_abort)
   {ret= 0; goto ex;}

 path= malloc(SfileadrL);
 abs_path= malloc(SfileadrL);
 if(path==NULL || abs_path==NULL) {
   Xorriso_no_malloc_memory(xorriso, &path, 0);
   {ret= -1; goto ex;}
 }

 action= Findjob_get_action(job, 0);
 if(action<0)
   action= 0;
 if(!(flag & 1))
   Xorriso_findi_headline(xorriso, job, 0);

 dir_node= (IsoDir *) dir_node_generic;
 if(dir_node==NULL) {
   ret= Xorriso_get_volume(xorriso, &volume, 0);
   if(ret<=0)
     {ret= -1; goto ex;}
   ret= Xorriso_make_abs_adr(xorriso, xorriso->wdi, dir_path, path, 1|2|4);
   if(ret<=0)
     goto ex;
   ret= Xorriso_node_from_path(xorriso, volume, path, &iso_node, 0);
   dir_node= (IsoDir *) iso_node;
   if(ret<=0)
     {ret= 0; goto ex;}
   ret= Xorriso_fake_stbuf(xorriso, "", dir_stbuf, &iso_node, 1);
   if(ret<=0)
     goto ex;

   name= strrchr(dir_path, '/');
   if(name==NULL)
     name= dir_path;
   else
     name++;

   ret= Xorriso_findi_test(xorriso, job, iso_node, name, path, NULL, dir_stbuf,
                           depth, 0);
   if(ret<0)
     goto ex;
   if(job->prune)
     no_dive= 1;
   if(ret>0) {
     iso_node_ref(iso_node); /* protect from real disposal */
     ret= Xorriso_findi_action(xorriso, job,
                               (IsoDirIter *) boss_iter, boss_mem,
                               path, dir_path, iso_node, depth,
                               flag&(1|2));
     deleted= (iso_node_get_parent(iso_node) == NULL); /* still in tree ? */
     iso_node_unref(iso_node); /* eventually do real disposal */
     if(xorriso->request_to_abort)
       {ret= 0; goto ex;}
     if(ret == 4)
       goto ex;
     if(ret<=0)
       goto ex;
     if(ret==2 || deleted) {
       /* re-determine dir_node in case it has a new persona */ 
       ret= Xorriso_node_from_path(xorriso, volume, path, &iso_node, 1);
       if(ret==0) {
         deleted= 1;
         {ret= 2; goto ex;}
       }
       if(ret<0)
         {ret= 0; goto ex;}
       dir_node= (IsoDir *) iso_node;
       ret= Xorriso_fake_stbuf(xorriso, "", dir_stbuf, &iso_node, 1);
       if(ret<=0)
         goto ex;
     }
     if(ret==3)
       no_dive= 1;
   }
 }
 if(no_dive || !LIBISO_ISDIR((IsoNode *) dir_node))
   {ret= 1; goto ex;}
 if(action == 14 || action == 17 || (flag & 4))
   if(Xorriso_is_split(xorriso, dir_path, (IsoNode *) dir_node, 1)>0)
     {ret= 1; goto ex;}

 mem= boss_mem;
 hflag= 1;
 if(action==1 || action==2 || action==3 || action==17 || action == 28 ||
    action == 32 || action == 41 || action == 42)
   hflag|= 2; /* need freedom to manipulate image */
 if(action==14 || action==17 || action == 28 || action == 35 || action == 36 ||
    action == 41)
   hflag|= 4; /* need LBA sorted iteration for good data reading performance */
 ret= Xorriso_findi_iter(xorriso, dir_node, &mem,
                         &iter, &node_array, &node_count, &node_idx,
                         &node, hflag);
 if(ret<=0)
   goto ex;
 while(1) {
   ret= Xorriso_findi_iter(xorriso, dir_node, &mem, &iter,
                           &node_array, &node_count, &node_idx, &node, 0);
   if(ret<0)
     goto ex;
   if(ret==0)
 break;
   name= (char *) iso_node_get_name(node);
   ret= Xorriso_make_abs_adr(xorriso, dir_path, name, path, 4);
   if(ret<=0)
     goto ex;
   ret= Xorriso_fake_stbuf(xorriso, "", &stbuf, &node, 1);
   if(ret<0)
     goto ex;
   if(ret==0)
 continue;

/* ??? This seems to be redundant with the single test above
   ??? Should i dive in unconditionally and leave out test and action here ?
   ??? Then do above test unconditionally ?
   --- Seems that the current configuration represents the special
       handling of the find start path with mount points. Dangerous to change.
*/

   ret= Xorriso_findi_test(xorriso, job, node, name, path, dir_stbuf, &stbuf,
                           depth, 0);
   if(ret<0)
     goto ex;
   if(job->prune)
     no_dive= 1;
   if(ret>0) {
     ret= Xorriso_make_abs_adr(xorriso, xorriso->wdi, path, abs_path, 1|2|4);
     if(ret<=0)
       goto ex;
     ret= Xorriso_findi_action(xorriso, job, iter, mem,
                               abs_path, path, node, depth, 1|(flag&2));
     if(xorriso->request_to_abort)
       {ret= 0; goto ex;}
     if(ret == 4)
       goto ex;
     if(ret==2) { /* node has been deleted */
       /* re-determine node in case it has a new persona */ 
       if(volume==NULL) {
         ret= Xorriso_get_volume(xorriso, &volume, 0);
         if(ret<=0)
           {ret= -1; goto ex;}
       }
       ret= Xorriso_node_from_path(xorriso, volume, abs_path, &node, 1);
       if(ret==0)
 continue;
       if(ret<0)
         {ret= 0; goto ex;}
       ret= Xorriso_fake_stbuf(xorriso, "", &stbuf, &node, 1);
       if(ret<0)
         goto ex;
       if(ret==0)
 continue;
     }
     no_dive= (ret==3);
     if(ret<=0) {
       if(Xorriso_eval_problem_status(xorriso, ret, 1|2)<0)
         goto ex;
     }
   }

   if(S_ISDIR(stbuf.st_mode) && !no_dive) {
     ret= Xorriso_findi(xorriso, job, (void *) iter, mem,
                        (void *) node, path, &stbuf, depth+1, flag|1);
     if(ret<0)
       goto ex;
     if(xorriso->request_to_abort)
       {ret= 0; goto ex;}
     if(ret == 4)
       goto ex;
   }
 }

 ret= 1;
ex:;
 if(path!=NULL)
   free(path);
 if(abs_path!=NULL)
   free(abs_path);
 Xorriso_process_msg_queues(xorriso,0);

 Xorriso_findi_iter(xorriso, dir_node, &mem, &iter, &node_array, &node_count,
                    &node_idx, &node, (1<<31));
 if(ret<=0)
   return(ret);
 if(deleted)
   return(2);
 return(1);
}


/* @param flag bit0= do not dive into trees
               bit1= do not perform job->action on resulting node array
               bit2= do not free node_array after all actions are done
*/
int Xorriso_findi_sorted(struct XorrisO *xorriso, struct FindjoB *job,
                         off_t boss_mem, int filec, char **filev, int flag)
{
 int i, ret, find_flag= 0;
 struct FindjoB array_job, *proxy_job= NULL, *hindmost= NULL, *hmboss= NULL;
 struct stat dir_stbuf;
 IsoNode *node;
 char *abs_path= NULL;
 off_t mem_needed= 0;

 Xorriso_alloc_meM(abs_path, char, SfileadrL);

 array_job.start_path= NULL;

 if(job->action == 14 || job->action == 17)
   find_flag|= 4;
 if(job->action>=9 && job->action<=13) { /* actions which have own findjobs */
   /* array_job replaces the hindmost job in the chain */
   for(hindmost= job; hindmost->subjob != NULL; hindmost= hindmost->subjob)
     hmboss= hindmost;
   if(hmboss == NULL)
     {ret= -1; goto ex;}
   memcpy(&array_job, hindmost, sizeof(struct FindjoB));
   hmboss->subjob= &array_job;
   proxy_job= job;
 } else {
   memcpy(&array_job, job, sizeof(struct FindjoB));
   proxy_job= &array_job;
   hindmost= job;
 }
 array_job.start_path= NULL; /* is owned by the original, not by array_job */

 /* Count matching nodes */
 Xorriso_destroy_node_array(xorriso, 0);
 array_job.action= 30; /* internal: count */
 for(i= 0; i < filec; i++) {
   if(flag & 1) {
     xorriso->node_counter++;
 continue;
   }
   ret= Findjob_set_start_path(proxy_job, filev[i], 0);
   if(ret <= 0)
     goto ex;
   ret= Xorriso_findi(xorriso, proxy_job, NULL, boss_mem, NULL,
                      filev[i],  &dir_stbuf, 0, find_flag);
   if(ret <= 0)
     goto ex;
 }
 if(xorriso->node_counter <= 0)
   {ret= 1; goto ex;}

 mem_needed= boss_mem + xorriso->node_counter * sizeof(IsoNode *);
 if(!(flag &1)) {
   ret= Xorriso_check_temp_mem_limit(xorriso, mem_needed, 0);
   if(ret <= 0) {
     /* Memory curbed : Perform unsorted find jobs */
     if(hmboss != NULL)
       hmboss->subjob= hindmost;
     for(i= 0; i < filec; i++) {
       ret= Findjob_set_start_path(job, filev[i], 0);
       if(ret <= 0)
         goto ex;
       ret= Xorriso_findi(xorriso, job, NULL, boss_mem, NULL,
                          filev[i], &dir_stbuf, 0, find_flag);
       if(ret <= 0)
         if(Xorriso_eval_problem_status(xorriso, ret, 1|2)<0)
           goto ex;
     }
     {ret= 1; goto ex;}
   }
 }

 /* Copy matching nodes into allocated array */
 ret= Xorriso_new_node_array(xorriso, xorriso->temp_mem_limit, 0, 0);
 if(ret <= 0)
   goto ex;
 array_job.action= 31; /* internal: register */
 xorriso->node_counter= 0;
 for(i= 0; i < filec; i++) {
   if(flag & 1) {
     ret= Xorriso_get_node_by_path(xorriso, filev[i], NULL, &node, 0);
     if(ret <= 0)
       goto ex;
     if(xorriso->node_counter < xorriso->node_array_size) {
       xorriso->node_array[xorriso->node_counter++]= (void *) node;
       iso_node_ref(node);
     }
 continue;
   }
   ret= Findjob_set_start_path(proxy_job, filev[i], 0);
   if(ret <= 0)
     goto ex;
   ret= Xorriso_findi(xorriso, proxy_job, NULL, mem_needed, NULL,
                      filev[i], &dir_stbuf, 0, find_flag);
   if(ret <= 0)
     goto ex;
 }

 Xorriso_sort_node_array(xorriso, 0);
 if(flag & 2)
   {ret= 1; goto ex;}

 /* Perform job->action on xorriso->node_array */

 /* Headlines of actions report_damage , report_lba */;
 Xorriso_findi_headline(xorriso, job, 0);

 for(i= 0; i < xorriso->node_counter; i++) {
   node= xorriso->node_array[i];
   ret= Xorriso_path_from_node(xorriso, node, abs_path, 0);
   if(ret < 0)
     goto ex;
   if(ret == 0)
 continue; /* node is deleted from tree meanwhile */

   ret= Xorriso_findi_action(xorriso, hindmost, NULL, (off_t) 0,
                             abs_path, abs_path, node, 0, 1);
   if(ret <= 0 || xorriso->request_to_abort)
     if(Xorriso_eval_problem_status(xorriso, ret, 1|2)<0)
       goto ex;
   if(ret == 4) /* end gracefully */
 break;
 }

 ret= 1;
ex:;
 if(!(flag & (2 | 4)))
   Xorriso_destroy_node_array(xorriso, 0);
 if(hmboss != NULL)
   hmboss->subjob= hindmost;
 if(array_job.start_path != NULL)
   free(array_job.start_path);
 Xorriso_free_meM(abs_path);
 return(ret);
}


int Xorriso_all_node_array(struct XorrisO *xorriso, int addon_nodes, int flag)
{  
 int ret;
 struct FindjoB *job= NULL;
 struct stat dir_stbuf;

 ret= Findjob_new(&job, "/", 0);
 if(ret<=0) {
   Xorriso_no_findjob(xorriso, "xorriso", 0);
   {ret= -1; goto ex;}
 }
 Findjob_set_action_target(job, 30, NULL, 0);
 Xorriso_destroy_node_array(xorriso, 0);
 ret= Xorriso_findi(xorriso, job, NULL, (off_t) 0, NULL, "/",
                    &dir_stbuf, 0, 0);
 if(ret <= 0)
   goto ex;
 ret= Xorriso_new_node_array(xorriso, xorriso->temp_mem_limit, addon_nodes, 0);
 if(ret <= 0)
   goto ex;
 Findjob_set_action_target(job, 31, NULL, 0);
 ret= Xorriso_findi(xorriso, job, NULL, (off_t) 0, NULL, "/",
                    &dir_stbuf, 0, 0);
 if(ret <= 0)
   goto ex;
 ret= 1;
ex:;
 Findjob_destroy(&job, 0);
 return(ret);
}


int Xorriso_perform_acl_from_list(struct XorrisO *xorriso, char *file_path,
                                  char *uid, char *gid, char *acl, int flag)
{
 int ret, zero= 0;
 uid_t uid_number;
 gid_t gid_number;

 /* Set group and owner */
 if(gid[0]) {
   ret= Xorriso_convert_gidstring(xorriso, gid, &gid_number, 0);
   if(ret<=0)
     return(ret);
   ret= Xorriso_set_gid(xorriso, file_path, gid_number, 0);
   if(ret<=0)
     return(ret);
 }
 if(uid[0]) {
   ret= Xorriso_convert_uidstring(xorriso, uid, &uid_number, 0);
   if(ret<=0)
     return(ret);
   ret= Xorriso_set_uid(xorriso, file_path, uid_number, 0);
   if(ret<=0)
     return(ret);
 }
 ret= Xorriso_option_setfacli(xorriso, acl, 1, &file_path, &zero, 0);
 if(ret <= 0)
   return(ret);
 return(1);
}


/*
  @param flag   bit0= do not perform setfattr but only check input
*/
int Xorriso_path_setfattr(struct XorrisO *xorriso, void *in_node, char *path,
                        char *name, size_t value_length, char *value, int flag)
{
 int ret, hflag;
 size_t num_attrs= 1;
 char *name_pt;

 hflag= 2;
 name_pt= name;
 if(name[0] == 0) {
   sprintf(xorriso->info_text,
           "-setfattr: Empty attribute name is not allowed");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   return(0);
 } else if(strcmp(name, "--remove-all") == 0) {
   if(value[0]) {
     sprintf(xorriso->info_text,
             "-setfattr: Value is not empty with pseudo name --remove-all");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
     return(0);
   }
   num_attrs= 0;
   hflag= 0;
 } else if(name[0] == '-') {
   name_pt++;
   hflag|= 4;
 } else if(name[0] == '=' || name[0] == '+') {
   name_pt++;
 }
 if(flag & 1)
   return(1);
 ret= Xorriso_setfattr(xorriso, in_node, path,
                       num_attrs, &name_pt, &value_length, &value, hflag);
 return(ret);
}


/* Warning: The text content of lst gets mangled by 0s and unescaping.
*/
int Xorriso_perform_attr_from_list(struct XorrisO *xorriso, char *path,
                      struct Xorriso_lsT *lst_start, int flag)
{
 int ret, eaten;
 char *valuept, *ept, *line, **names= NULL, **values= NULL;
 size_t num_attr= 0, *value_lengths= NULL, v_len;
 struct Xorriso_lsT *lst;

 for(lst= lst_start; lst != NULL; lst= Xorriso_lst_get_next(lst, 0))
   num_attr++;
 if(num_attr == 0) {
   ret= Xorriso_setfattr(xorriso, NULL, path, num_attr, NULL, NULL, NULL, 0);
   goto ex;
 }

 names= calloc(num_attr, sizeof(char *));
 if(names == NULL) {
   Xorriso_no_malloc_memory(xorriso, NULL, 0);
   ret= -1; goto ex;
 }
 value_lengths= calloc(num_attr, sizeof(size_t));
 if(value_lengths== NULL) {
   Xorriso_no_malloc_memory(xorriso, NULL, 0);
   ret= -1; goto ex;
 }
 values= calloc(num_attr, sizeof(char *));
 if(values== NULL) {
   Xorriso_no_malloc_memory(xorriso, NULL, 0);
   ret= -1; goto ex;
 }

 num_attr= 0;
 for(lst= lst_start; lst != NULL; lst= Xorriso_lst_get_next(lst, 0)) {
   line= Xorriso_lst_get_text(lst, 0);
   ept= strchr(line, '=');
   if(ept == NULL)
 continue;
   /* Split into name and content */;
   *ept= 0;
   valuept= ept + 1;

   /* Strip quotes from value */
   v_len= strlen(valuept);
   if(v_len < 2 || *valuept != '"' || *(valuept + v_len - 1) != '"')
 continue;
   *valuept= 0;
   *(valuept + v_len - 1)= 0;
   valuept++;
   v_len-= 2;

   /* Unescape backslashes , values eventually with 0-bytes */
   ret= Sfile_bsl_interpreter(line, strlen(line), &eaten, 0);
   if(ret <= 0)
 continue;
   ret= Sfile_bsl_interpreter(valuept, (int) v_len, &eaten, 2);
   if(ret <= 0)
 continue;

   names[num_attr]= line;
   values[num_attr]= valuept;
   value_lengths[num_attr]= v_len - eaten;
   num_attr++;
 }
 ret= Xorriso_setfattr(xorriso, NULL, path, num_attr, names,
                       value_lengths, values, 0);
ex:;
 if(names != NULL)
   free(names);
 if(value_lengths != NULL)
   free(value_lengths);
 if(values != NULL)
   free(values);
 return(ret);
}


int Xorriso__mark_update_xinfo(void *data, int flag)
{
 /* data is an int disguised as pointer. It does not point to memory. */
 return(1);
}


int Xorriso__mark_update_cloner(void *old_data, void **new_data, int flag)
{
 *new_data= NULL;
 if(flag)
   return(ISO_XINFO_NO_CLONE);
 if(old_data == NULL)
   return(0);
 /* data is an int disguised as pointer. It does not point to memory. */
 *new_data= old_data;
 return(0);
}


/* @param flag bit0= found on disk
               bit1= inquire visit-found status:
                     1=not visited, 2=not found, 3=found
               bit2= with bit1: delete xinfo before returning status
*/
int Xorriso_mark_update_merge(struct XorrisO *xorriso, char *path,
                              void *in_node, int flag)
{
 int ret;
 void *xipt= NULL;
 IsoNode *node;

 if(in_node == NULL) {
   ret= Xorriso_node_from_path(xorriso, NULL, path, &node, 0);
   if(ret <= 0)
     return(ret);
 } else
   node= (IsoNode *) in_node;
 ret= iso_node_get_xinfo(node, Xorriso__mark_update_xinfo, &xipt);
 if(ret < 0) {
   Xorriso_process_msg_queues(xorriso,0);
   Xorriso_report_iso_error(xorriso, "", ret,
                            "Error when looking for update_merge xinfo",
                            0, "FAILURE", 1);
   return(0);
 }
 if(flag & 2) { /* Inquire status and optionally delete xinfo */
   if(ret == 0)
     return(1);
   if(flag & 4) {
     ret= iso_node_remove_xinfo(node, Xorriso__mark_update_xinfo);
     if(ret < 0) {
       Xorriso_process_msg_queues(xorriso,0);
       Xorriso_report_iso_error(xorriso, "", ret,
                                "Error when removing update_merge xinfo",
                                0, "FAILURE", 1);
       return(0);
     }
   }
   if(((char *) &xipt)[0])
     return(3);
   return(2);
 }
 /* xipt is a byte value disguised as void pointer */
 if(ret == 1) {
   if(((char *) &xipt)[0])
     return(1);
   if(!(flag & 1))
     return(1);
 } else
   ((char *) &xipt)[0]= 0;
 if(flag & 1)
   ((char *) &xipt)[0]= 1;
 ret= iso_node_remove_xinfo(node, Xorriso__mark_update_xinfo);
 if(ret < 0)
   goto set_error;
 ret= iso_node_add_xinfo(node, Xorriso__mark_update_xinfo, xipt);
 if(ret <= 0) {
set_error:;
   Xorriso_process_msg_queues(xorriso,0);
   Xorriso_report_iso_error(xorriso, "", ret,
                            "Error when trying to set update_merge xinfo",
                            0, "FAILURE", 1);
   return(0);
 }
 return(1);
}


/* flag bit0= in case of error talk of "overwrite" rather than "remove"
*/
static int Xorriso_remove_hfsplus_crtp(struct XorrisO *xorriso, IsoNode *node,
                                       char *path, int flag)
{
 int ret;
 char *msg, buf[10], *bufpt;
 size_t l;
 static char *name= "isofs.hx";

 ret= iso_node_remove_xinfo(node, iso_hfsplus_xinfo_func);
 Xorriso_process_msg_queues(xorriso, 0);
 if(ret < 0) {
   if(flag & 1)
     msg= "Cannot overwrite HFS+ creator and type of ISO node";
   else
     msg= "Cannot remove HFS+ creator and type of ISO node";
   Xorriso_report_iso_error(xorriso, path, ret, msg, 0, "FAILURE", 1);
   return(0);
 }
 /* Delete isofs.hx attribute */
 bufpt= buf;

 /* >>> ??? check whether there is isofs.hx attached ? */;

 ret= Xorriso_setfattr(xorriso, node, path,
                       (size_t) 1, &name, &l, &bufpt, 4 | 8);
 return(ret);
}


static int Xorriso_set_hfsplus_crtp(struct XorrisO *xorriso, IsoNode *node,
                                    char *path, char *creator, char *hfs_type,
                                    int flag)
{
 struct iso_hfsplus_xinfo_data *hfs_data= NULL;
 char buf[10], *bufpt;
 size_t l;
 int ret;
 static char *name= "isofs.hx";

 /* Register as non-persistent xinfo */
 hfs_data= iso_hfsplus_xinfo_new(0);
 if(hfs_data == NULL) {
   Xorriso_no_malloc_memory(xorriso, NULL, 0);
   return(-1);
 }
 memcpy(hfs_data->creator_code, creator, 4);
 memcpy(hfs_data->type_code, hfs_type, 4);
 ret= iso_node_add_xinfo(node, iso_hfsplus_xinfo_func, (void *) hfs_data);
 Xorriso_process_msg_queues(xorriso, 0);
 if(ret < 0) {
   Xorriso_report_iso_error(xorriso, path, ret,
          "Cannot attach HFS+ creator and type to ISO node", 0, "FAILURE", 1);
   goto failure;
 } else if(ret == 0) {
   strcat(xorriso->info_text,
 "Programm error: iso_node_add_xinfo refuses to attach HFS+ creator and type");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   goto failure;
 }

 /* Register as persistent attribute isofs.hx */
 bufpt= buf;
 l= 10;
 buf[0]= 1;
 buf[1]= 0;
 memcpy(buf + 2, creator, 4);
 memcpy(buf + 6, hfs_type, 4);
 ret= Xorriso_setfattr(xorriso, node, path,
                       (size_t) 1, &name, &l, &bufpt, 2 | 8);
 if(ret <= 0)
   goto failure;
 Xorriso_set_change_pending(xorriso, 0);
 return(1);

failure:
 if(hfs_data != NULL)
   iso_hfsplus_xinfo_func(hfs_data, 1);
 return(0);
}


/* @param flag bit0= only check creator and hfs_type for compliance.
               bit1= with bit0: check for search rather than for setting
               bit2= copy 2 times 4 bytes without any check
*/
int Xorriso_hfsplus_file_creator_type(struct XorrisO *xorriso, char *path,
                                      void *in_node, 
                                      char *creator, char *hfs_type, int flag)
{
 int ret;
 IsoNode *node;

 if(in_node == NULL && !(flag * 1)) {
   ret= Xorriso_node_from_path(xorriso, NULL, path, &node, 0);
   if(ret <= 0)
     return(ret);
 } else
   node= (IsoNode *) in_node;
 if(flag & 4) {
   ;
 } else if((creator[0] == 0 && hfs_type[0] == 0) ||
    strcmp(creator, "--delete") == 0) {
   if(flag & 2) {
     strcpy(xorriso->info_text,
           "Attempt to use HFS+ file pseudo-creator '--delete' for searching");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     strcpy(xorriso->info_text,
             "Suitable are strings of length 4 or length 1");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "HINT", 0);
     return(0);
   }
   if(flag & 1)
     return(1);
   ret= Xorriso_remove_hfsplus_crtp(xorriso, node, path, 0);
   if(ret < 0)
     return(ret);
   return(1);
 } else if((strlen(creator) != 4 && !(strlen(creator) == 1 &&
            (flag & 3) == 3)) ||
           (strlen(hfs_type) != 4 && !(strlen(hfs_type) == 1 &&
            (flag & 3) == 3))) {
   if(flag & 2) {
     strcpy(xorriso->info_text,
      "HFS+ file creator code or type code for searching are not exactly 1 or 4 characters long");
   } else {
     strcpy(xorriso->info_text,
      "HFS+ file creator code or type code are not exactly 4 characters long");
   }
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 if(flag & 1)
   return(1);

 ret= Xorriso_remove_hfsplus_crtp(xorriso, node, path, 1);
 if(ret <= 0)
   return(ret);
 ret= Xorriso_set_hfsplus_crtp(xorriso, node, path, creator, hfs_type, 0);
 if(ret <= 0)
   return(ret);
 return(1);
}


/*
  @param node
       If node is NULL and path is empty, then the blessing will be
       revoked from any node which bears it.
  @param flag
       Bitfield for control purposes.
         bit0= Revoke blessing if node != NULL bears it.
         bit1= Revoke any blessing of the node, regardless of parameter
               blessing. If node is NULL, then revoke all blessings.
         bit2= Only check parameter blessing.
               Return blessing index + 1 instead of issueing the blessing.
         bit3= With bit2:
               Allow blessing "any" and map to index ISO_HFSPLUS_BLESS_MAX.
               Elsewise, blessing "none" is mapped to ISO_HFSPLUS_BLESS_MAX.
*/
int Xorriso_hfsplus_bless(struct XorrisO *xorriso, char *path,
                          void *in_node, char *blessing, int flag)
{
 int ret, bless_max;
 IsoNode *node, **blessed_nodes;
 IsoImage *volume= NULL;
 enum IsoHfsplusBlessings bless_code = ISO_HFSPLUS_BLESS_MAX; /* = invalid */
 char *hb = "";
 size_t l= 0;
 static char *name= "isofs.hb";

 if(strcmp(blessing, "ppc_bootdir") == 0 ||
           strcmp(blessing, "p") == 0 || strcmp(blessing, "P") == 0) {
   bless_code= ISO_HFSPLUS_BLESS_PPC_BOOTDIR;
   hb= "p";
 } else if(strcmp(blessing, "intel_bootfile") == 0 ||
           strcmp(blessing, "i") == 0 || strcmp(blessing, "I") == 0) {
   bless_code= ISO_HFSPLUS_BLESS_INTEL_BOOTFILE;
   hb= "i";
 } else if(strcmp(blessing, "show_folder") == 0 ||
           strcmp(blessing, "s") == 0 || strcmp(blessing, "S") == 0) {
   bless_code= ISO_HFSPLUS_BLESS_SHOWFOLDER;
   hb= "s";
 } else if(strcmp(blessing, "os9_folder") == 0 ||
           strcmp(blessing, "9") == 0) {
   bless_code= ISO_HFSPLUS_BLESS_OS9_FOLDER;
   hb= "9";
 } else if(strcmp(blessing, "osx_folder") == 0 ||
         strcmp(blessing, "x") == 0 || strcmp(blessing, "X") == 0) {
   bless_code= ISO_HFSPLUS_BLESS_OSX_FOLDER;
   hb= "x";
 } else if((!(flag & 8)) && (strcmp(blessing, "none") == 0 ||
          strcmp(blessing, "n") == 0 || strcmp(blessing, "N") == 0)) {
   bless_code= ISO_HFSPLUS_BLESS_MAX;
   flag |= 2;
 } else if((flag & 8) && (flag & 4) &&
         (strcmp(blessing, "any") == 0 ||
          strcmp(blessing, "a") == 0 || strcmp(blessing, "A") == 0)) {
   bless_code= ISO_HFSPLUS_BLESS_MAX;
 } else {
   sprintf(xorriso->info_text, "Unknown blessing type ");
   Text_shellsafe(blessing, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 if(flag & 4)
   return(1 + bless_code);

 if(in_node == NULL && path[0]) {
   ret= Xorriso_node_from_path(xorriso, NULL, path, &node, 0);
   if(ret <= 0)
     return(ret);
 } else
   node= (IsoNode *) in_node;
 ret= Xorriso_get_volume(xorriso, &volume, 0);
 if(ret <= 0)
   return(ret);

 if(!(flag & 2)) {
   /* Remove persistent bless mark from current bearer */
   ret= iso_image_hfsplus_get_blessed(volume, &blessed_nodes, &bless_max, 0);
   Xorriso_process_msg_queues(xorriso, 0);
   if(ret < 0) {
     Xorriso_report_iso_error(xorriso, "", ret,
                              "Error when trying to bless a file",
                              0, "FAILURE", 1);
     return(0);
   }
   if((int) bless_code < bless_max) {
     if(blessed_nodes[(int) bless_code] != NULL) {
       ret= Xorriso_setfattr(xorriso, blessed_nodes[(int) bless_code], "",
                             (size_t) 1, &name, &l, &hb, 4 | 8);
       if(ret <= 0)
         return(ret);
     }
   }
 }

 /* Bless node */
 ret= iso_image_hfsplus_bless(volume, bless_code, node, flag & 3);
 Xorriso_process_msg_queues(xorriso, 0);
 if(ret == 0 && path[0]) {
   if((flag & 3)) {
     sprintf(xorriso->info_text,
             "Attempt to revoke blessing of unblessed file");
   } else {
     sprintf(xorriso->info_text,
             "Multiple blessing to same file or inappropriate file type");
   }
   if(path[0]) {
     strcat(xorriso->info_text, ": ");
     Text_shellsafe(path, xorriso->info_text, 1);
   }
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 } else if (ret < 0) {
   Xorriso_report_iso_error(xorriso, "", ret,
                            "Error when trying to bless a file",
                            0, "FAILURE", 1);
   return(0);
 }

 /* Attach persistent AAIP bless mark to node */
 if(!(flag & 3)) {
   l= 1;
   ret= Xorriso_setfattr(xorriso, node, path, (size_t) 1, &name, &l, &hb,
                         2 | 8);
   if(ret <= 0)
     return(ret);
 }

 Xorriso_set_change_pending(xorriso, 0);
 return(1);
}


int Xorriso_get_blessing(struct XorrisO *xorriso, IsoNode *node,
                         int *bless_idx, char bless_code[17], int flag)
{
 IsoNode **blessed_nodes;
 int bless_max, ret, i;

 if(xorriso->in_volset_handle == NULL)
   return(0);

 ret= iso_image_hfsplus_get_blessed((IsoImage *) xorriso->in_volset_handle,
                                     &blessed_nodes, &bless_max, 0);
 Xorriso_process_msg_queues(xorriso, 0);
 if(ret < 0) {
   Xorriso_report_iso_error(xorriso, "", ret,
                            "Error when trying to inquire HFS+ file blessings",
                            0, "FAILURE", 1);
   return(-1);
 }
 for(i= 0; i < bless_max; i++) {
   if(blessed_nodes[i] == node) {
     switch (i) {
     case ISO_HFSPLUS_BLESS_PPC_BOOTDIR:
       strcpy(bless_code, "ppc_bootdir");
     break; case ISO_HFSPLUS_BLESS_INTEL_BOOTFILE:
       strcpy(bless_code, "intel_bootfile");
     break; case ISO_HFSPLUS_BLESS_SHOWFOLDER:
       strcpy(bless_code, "show_folder");
     break; case ISO_HFSPLUS_BLESS_OS9_FOLDER:
       strcpy(bless_code, "os9_folder");
     break; case ISO_HFSPLUS_BLESS_OSX_FOLDER:
       strcpy(bless_code, "osx_folder");
     break; default:
       strcpy(bless_code, "unknown_blessing");
     }
     *bless_idx= i;
     return(1);
   }
 }
 return(0);
}


