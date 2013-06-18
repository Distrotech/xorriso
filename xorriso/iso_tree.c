
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2012 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains functions which access nodes of the
   libisofs tree model.
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

#include <pwd.h>
#include <grp.h>


#include "xorriso.h"
#include "xorriso_private.h"
#include "xorrisoburn.h"

#include "lib_mgt.h"
#include "iso_img.h"
#include "iso_tree.h"
#include "iso_manip.h"
#include "sort_cmp.h"


/* @param eff_path  returns resulting effective path.
                    Must provide at least SfileadrL bytes of storage.
   @param flag bit0= do not produce problem events (unless faulty path format)
               bit1= work purely literally, do not use libisofs
               bit2= (with bit1) this is an address in the disk world
               bit3= return root directory as "/" and not as ""
               bit4= (with bit2) determine type of disk file eff_path
                     and return 0 if not existing
               bit5= (with bit3) this is not a parameter
               bit6= insist in having an ISO image, even with bits1+2
   @return -1 = faulty path format, 0 = not found ,
            1 = found simple node , 2 = found directory
*/
int Xorriso_normalize_img_path(struct XorrisO *xorriso, char *wd,
                               char *img_path, char eff_path[], int flag)
{
 int ret, is_dir= 0, done= 0;
 IsoImage *volume;
 IsoDir *dir= NULL;
 IsoNode *node= NULL;
 char *path= NULL, *apt, *npt, *cpt;

 Xorriso_alloc_meM(path, char, SfileadrL);

 if((flag&64) || !(flag&2)) {
   ret= Xorriso_get_volume(xorriso, &volume, 0);
   if(ret<=0)
     goto ex;
 }

 eff_path[0]= 0;
 if(img_path[0]==0) {
   if(flag&8)
     strcpy(eff_path, "/");
   {ret= 2; goto ex;} /* root directory */
 }

 apt= npt= path;
 if(img_path[0]!='/') {
   strcpy(path, wd);
   ret= Sfile_add_to_path(path, img_path, 0);
   if(ret<=0)
     goto much_too_long;
 } else
   if(Sfile_str(path, img_path, 0)<=0)
     {ret= -1; goto ex;}

 if(path[0]!='/') {
   sprintf(xorriso->info_text,
        "Internal error: Unresolvable relative addressing in iso_rr_path '%s'",
        img_path);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FATAL", 0);
   {ret= -1; goto ex;}
 } else if(path[1]==0) {
   if(flag&8)
     strcpy(eff_path, "/");
   {ret= 2; goto ex;} /* root directory */
 }

 if(apt[0] == '.')
   if(apt[1] == 0 || apt[1] == '/')
     is_dir= 1;
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
 break;
 continue;
   }
   if(strcmp(apt,".")==0)
 continue;
   if(strcmp(apt,"..")==0) {
     if(!(flag&2)) {
       node= (IsoNode *) dir;
       if(node==NULL) {
bonked_root:;
         sprintf(xorriso->info_text,
                 "Relative addressing in path exceeds root directory: ");
         Text_shellsafe(img_path, xorriso->info_text, 1);
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         {ret= -1; goto ex;}
       }
       dir= iso_node_get_parent(node);
     }
     /* truncate eff_path */;
     cpt= strrchr(eff_path, '/');
     if(cpt==NULL) /* ??? if not flag&2 then this is a bug */
       goto bonked_root;
     *cpt= 0;
     is_dir= 1;
 continue;
   }
   is_dir= 0;
   ret= Sfile_add_to_path(eff_path, apt, 0);
   if(ret<=0) {
much_too_long:;
     sprintf(xorriso->info_text, "Effective path gets much too long (%d)",
             (int) (strlen(eff_path)+strlen(apt)+1));
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= -1; goto ex;}
   }
   if(!(flag&2)) {
     dir= (IsoDir *) node;
     ret= Xorriso_node_from_path(xorriso, volume, eff_path, &node, flag&1);
     if(ret<=0) 
       {ret= 0; goto ex;}
     if(dir==NULL) /* could be false with  "/dir/.." */
       dir= iso_node_get_parent(node);
     is_dir= LIBISO_ISDIR(node);
   }
 }
 if(flag&16) {
   ret= Sfile_type(eff_path,
    1|(4*(xorriso->do_follow_links || (xorriso->do_follow_param && !(flag&32)))
                  ));
   if(ret<0)
     {ret= 0; goto ex;}
   if(ret==2)
     is_dir= 1;
   else
     is_dir= 0;
 }
 ret= 1+!!is_dir;
ex:;
 Xorriso_free_meM(path);
 return(ret);
}


int Xorriso_get_node_by_path(struct XorrisO *xorriso,
                             char *in_path, char *eff_path,
                             IsoNode **node, int flag)
{
 int ret;
 char *path= NULL;
 IsoImage *volume;

 Xorriso_alloc_meM(path, char, SfileadrL);

 ret= Xorriso_normalize_img_path(xorriso, xorriso->wdi, in_path, path, 0);
 if(ret<=0)
   goto ex;
 if(eff_path!=NULL)
   strcpy(eff_path, path);
 ret= Xorriso_get_volume(xorriso, &volume, 0);
 if(ret<=0)
   goto ex;
 ret= Xorriso_node_from_path(xorriso, volume, path, node, 0);
 if(ret<=0)
   {ret= 0; goto ex;}
 ret= 1;
ex:;
 Xorriso_free_meM(path);
 return(ret);
}


/* @param flag
*/
int Xorriso_node_get_dev(struct XorrisO *xorriso, IsoNode *node,
                         char *path, dev_t *dev, int flag)
{
 *dev= iso_special_get_dev((IsoSpecial *) node);
 return(1);
}


/* @param flag bit0= *node is already valid
               bit1= add extra block for size estimation
               bit2= complain loudely if path is missing in image
               bit3= stbuf is to be used without eventual ACL
               bit4= try to obtain a better st_nlink count if hardlinks
                     are enabled
*/
int Xorriso_fake_stbuf(struct XorrisO *xorriso, char *path, struct stat *stbuf,
                       IsoNode **node, int flag)
{
 int ret, min_hl, max_hl, node_idx, i;
 IsoImage *volume;
 IsoBoot *bootcat;
 uint32_t lba;
 char *catcontent = NULL;
 off_t catsize;
 dev_t dev_number;

 memset((char *) stbuf, 0, sizeof(struct stat));
 if(!(flag&1)) {
   ret= Xorriso_get_volume(xorriso, &volume, 0);
   if(ret<=0)
     return(-1);
   ret= Xorriso_node_from_path(xorriso, volume, path, node, !(flag&4));
   if(ret<=0)
     *node= NULL;
 }
 if(*node==NULL)
   return(0);

 /* >>> stbuf->st_dev */
 /* >>> stbuf->st_ino */

 if(flag & 8)
   stbuf->st_mode= iso_node_get_perms_wo_acl(*node) & 07777;
 else
   stbuf->st_mode= iso_node_get_permissions(*node) & 07777;
 if(LIBISO_ISDIR(*node))
   stbuf->st_mode|= S_IFDIR;
 else if(LIBISO_ISREG(*node))
   stbuf->st_mode|= S_IFREG;
 else if(LIBISO_ISLNK(*node))
   stbuf->st_mode|= S_IFLNK;
 else if(LIBISO_ISCHR(*node)) {
   stbuf->st_mode|= S_IFCHR;
   Xorriso_node_get_dev(xorriso, *node, path, &(stbuf->st_rdev), 0);
 } else if(LIBISO_ISBLK(*node)) {
   stbuf->st_mode|= S_IFBLK;
   /* ts B11124:
      Debian mips and mipsel have sizeof(stbuf.st_rdev) == 4
      whereas sizeof(dev_t) is 8.
      This workaround assumes that the returned numbers fit into 4 bytes.
      The may stem from the local filesystem or from the ISO image.
      At least there will be no memory corruption but only an integer rollover.
   */ 
   Xorriso_node_get_dev(xorriso, *node, path, &dev_number, 0);
   stbuf->st_rdev= dev_number;
 } else if(LIBISO_ISFIFO(*node))
   stbuf->st_mode|= S_IFIFO;
 else if(LIBISO_ISSOCK(*node))
   stbuf->st_mode|= S_IFSOCK;
 else if(LIBISO_ISBOOT(*node))
   stbuf->st_mode|= Xorriso_IFBOOT;

 /* >>> With directories this should be : number of subdirs + 2 */
 /* >>> ??? How to obtain RR hardlink number for other types ? */
 /* This may get overriden farther down */
 stbuf->st_nlink= 1;

 stbuf->st_uid= iso_node_get_uid(*node);
 stbuf->st_gid= iso_node_get_gid(*node);

 if(LIBISO_ISREG(*node))
   stbuf->st_size= iso_file_get_size((IsoFile *) *node)+ (2048 * !!(flag&2));
 else if(LIBISO_ISBOOT(*node)) {
   ret= Xorriso_get_volume(xorriso, &volume, 0);
   if(ret <= 0)
     return(-1);
   ret= iso_image_get_bootcat(volume, &bootcat, &lba, &catcontent, &catsize);
   if(catcontent != NULL)
     free(catcontent);
   if(ret < 0) {
     Xorriso_process_msg_queues(xorriso,0);
     return(-1);
   }
   stbuf->st_size= catsize;
 } else
   stbuf->st_size= 0;

 stbuf->st_blksize= 2048;
 stbuf->st_blocks= stbuf->st_size / (off_t) 2048;
 if(stbuf->st_blocks * (off_t) 2048 != stbuf->st_size)
   stbuf->st_blocks++; 

 stbuf->st_atime= iso_node_get_atime(*node);
 stbuf->st_mtime= iso_node_get_mtime(*node);
 stbuf->st_ctime= iso_node_get_ctime(*node);

 if(LIBISO_ISDIR(*node) || (xorriso->ino_behavior & 1) || (!(flag & 16)) ||
    xorriso->hln_array == NULL)
   return(1);

 /* Try to obtain a better link count */
 ret= Xorriso_search_hardlinks(xorriso, *node, &node_idx, &min_hl, &max_hl, 0);
 if(ret < 0)
   return(ret);
 if(ret > 0 && node_idx >= 0) {
   for(i= min_hl; i <= max_hl; i++) {
     if(i == node_idx)
   continue;
     /* Check whether node is still valid */
     if(iso_node_get_parent(xorriso->hln_array[i]) != NULL)
       stbuf->st_nlink++;
   }
 }
 return(1);
}


/* @param flag >>>  bit0= follow links (i.e. stat() rather than lstat()
               bit1= do not return -2 on severe errors
               bit2= complain loudely if path is missing in image
*/
int Xorriso_iso_lstat(struct XorrisO *xorriso, char *path, struct stat *stbuf,
                      int flag)
{
 int ret;
 IsoNode *node;

 if(flag&1) {

   /* >>> follow link in ISO image */;

 }

 ret= Xorriso_fake_stbuf(xorriso, path, stbuf, &node, flag&4);
 if(ret>0)
   return(0);
 if(ret<0 && !(flag&2))
   return(-2);
 return(-1);
}


int Xorriso_node_is_valid(struct XorrisO *xorriso, IsoNode *in_node, int flag)
{
 IsoNode *node, *parent;

 for(node= in_node; 1; node= parent) {
   parent= (IsoNode *) iso_node_get_parent(node);
   if(parent == node)
 break;
   if(parent == NULL)
     return(0); /* Node is not in the tree (any more) */
 }
 return(1);
}


int Xorriso_path_from_node(struct XorrisO *xorriso, IsoNode *in_node,
                           char path[SfileadrL], int flag)
{
 int ret, i, comp_count= 0;
 IsoNode *node, *parent, **components= NULL;
 char *wpt, *npt;

 for(node= in_node; 1; node= parent) {
   parent= (IsoNode *) iso_node_get_parent(node);
   if(parent == node)
 break;
   if(parent == NULL)
     return(0); /* Node is not in the tree (any more) */
   comp_count++;
 }
 if(comp_count == 0) {
   strcpy(path, "/");
   return(1);
 }
 components= calloc(comp_count, sizeof(IsoNode *));
 if(components == NULL) {
   Xorriso_no_malloc_memory(xorriso, NULL, 0);
   ret= -1; goto ex;
 }
 i= comp_count;
 for(node= in_node; 1; node= parent) {
   parent= (IsoNode *) iso_node_get_parent(node);
   if(parent == node)
 break;
   components[--i]= node;
 }

 wpt= path;
 for(i= 0; i < comp_count; i++) {
   npt= (char *) iso_node_get_name(components[i]);
   if((wpt - path) + strlen(npt) + 1 >= SfileadrL) {

     /* >>> path is getting much too long */;

     ret= -1; goto ex;
   }
   *(wpt++)= '/';
   strcpy(wpt, npt);
   wpt+= strlen(npt);
   *wpt= 0;
 }
 ret= 1;
ex:;
 if(components != NULL)
   free(components);
 return(ret);
}


/* <<< The lookup from node pointer will be done by Xorriso_path_from_node()
       (Currently it runs a full tree traversal)
       Parameter node and flag bit0 will vanish then
*/
/* @param flag bit0= use lba rather than node pointer
*/
int Xorriso_path_from_lba(struct XorrisO *xorriso, IsoNode *node, int lba,
                          char path[SfileadrL], int flag)
{
 int ret;
 struct FindjoB *job= NULL;
 struct stat dir_stbuf;
 char *found_path;

 path[0]= 0;
 if((flag & 1) && lba <= 0)
   return(0);

 ret= Findjob_new(&job, "/", 0);
 if(ret <= 0) {
   Xorriso_no_findjob(xorriso, "path_from_node", 0);
   return(ret);
 }
 if(flag & 1)
   Findjob_set_lba_range(job, lba, 1, 0);
 else
   Findjob_set_wanted_node(job, (void *) node, 0);
 Findjob_set_action_found_path(job, 0);
 ret= Xorriso_findi(xorriso, job, NULL,  (off_t) 0,
                    NULL, "/", &dir_stbuf, 0, 0);
 if(ret > 0) {
   ret= 1;
   Findjob_get_found_path(job, &found_path, 0);
   if(found_path == NULL)
     ret= 0;
   else if(Sfile_str(path, found_path, 0) <= 0)
     ret= -1;
 }
 Findjob_destroy(&job, 0);
 return(ret);
}


/* @param flag bit0= in_node is valid, do not resolve iso_adr
               bit2= recognize and parse split parts despite
                     xorrio->split_size <= 0
*/
int Xorriso_identify_split(struct XorrisO *xorriso, char *iso_adr,
                           void *in_node,
                           struct SplitparT **parts, int *count,
                           struct stat *total_stbuf,  int flag)
{
 int ret, i, incomplete= 0, overlapping= 0;
 int partno, total_parts, first_total_parts= -1;
 off_t offset, bytes, total_bytes, first_total_bytes= -1, first_bytes= -1;
 off_t size, covered;

 IsoImage *volume;
 IsoDir *dir_node;
 IsoDirIter *iter= NULL;
 IsoNode *node;
 char *name;
 struct stat stbuf, first_stbuf;

 *count= 0;
 *parts= NULL;

 if(xorriso->split_size <= 0 && !(flag & 4))
   return(0);

 if(flag&1) {
   node= (IsoNode *) in_node;
 } else {
   ret= Xorriso_get_volume(xorriso, &volume, 0);
   if(ret<=0)
     return(-1);
   ret= Xorriso_node_from_path(xorriso, volume, iso_adr, &node, 1);
   if(ret<=0)
     return(-1);
 }
 if(!LIBISO_ISDIR(node))
   return(0);
 dir_node= (IsoDir *) node;

 ret= iso_dir_get_children(dir_node, &iter);
 if(ret<0) {
cannot_iter:;
   Xorriso_cannot_create_iter(xorriso, ret, 0);
   return(-1);
 }

 for(i= 0; iso_dir_iter_next(iter, &node) == 1; i++) {
   name= (char *) iso_node_get_name(node);
   ret= Splitpart__parse(name, &partno, &total_parts,
                         &offset, &bytes, &total_bytes, 0);
   if(ret<=0)
     {ret= 0; goto ex;}
   if(i==0) {
     first_total_parts= total_parts;
     first_bytes= bytes;
     first_total_bytes= total_bytes;
     Xorriso_fake_stbuf(xorriso, "", &first_stbuf, &node, 1);
     size= first_stbuf.st_size;
   } else {
     if(first_total_parts!=total_parts || first_total_bytes!=total_bytes ||
        (first_bytes!=bytes && partno!=total_parts))
       {ret= 0; goto ex;}
     Xorriso_fake_stbuf(xorriso, "", &stbuf, &node, 1);
     if(first_stbuf.st_mode != stbuf.st_mode ||
        first_stbuf.st_uid != stbuf.st_uid ||
        first_stbuf.st_gid != stbuf.st_gid ||
        first_stbuf.st_mtime != stbuf.st_mtime ||
        first_stbuf.st_ctime != stbuf.st_ctime)
       {ret= 0; goto ex;} 
     size= stbuf.st_size;
   }
   /* check for plausible size */
   if(!((partno != total_parts && size == bytes) ||
        (partno == total_parts && size <= bytes)))
     {ret= 0; goto ex;} 
   if(offset != first_bytes * (off_t) (partno - 1))
     {ret= 0; goto ex;}
   (*count)++;
 }
 if(*count <= 0 || *count != first_total_parts)
   {ret= 0; goto ex;}

 ret= Splitparts_new(parts, (*count)+1, 0); /* (have one end marker item) */
 if(ret<=0)
   return(ret);

 iso_dir_iter_free(iter);
 ret= iso_dir_get_children(dir_node, &iter);
 if(ret<0)
   goto cannot_iter;
 for(i= 0; i<*count; i++) {
   ret= iso_dir_iter_next(iter, &node);
   if(ret!=1)
 break;
   name= (char *) iso_node_get_name(node);
   ret= Splitpart__parse(name, &partno, &total_parts,
                         &offset, &bytes, &total_bytes, 0);
   if(ret<=0)
     {ret= 0; goto ex;}
   ret= Splitparts_set(*parts, i, name, partno, total_parts, offset, bytes,
                       total_bytes, 0);
   if(ret<=0)
     goto ex;
 }

 Splitparts_sort(*parts, *count, 0);

 covered= 0;
 for(i= 0; i<*count; i++) {
   Splitparts_get(*parts, i, &name, &partno, &total_parts, &offset, &bytes,
                  &total_bytes, 0);
   if(offset>covered)
     incomplete= 1;
   else if(offset<covered)
     overlapping= 1;
   if(offset+bytes > covered)
     covered= offset+bytes;
 }
 if(total_bytes>covered)
   incomplete= 1;
 memcpy(total_stbuf, &first_stbuf, sizeof(struct stat));
 total_stbuf->st_size= total_bytes;
 ret= !(overlapping || incomplete);
ex:;
 if(iter!=NULL)
   iso_dir_iter_free(iter);
 return(ret);
}


/* @param flag bit0= node is valid, do not resolve path
               bit1= insist in complete collection of part files
*/
int Xorriso_is_split(struct XorrisO *xorriso, char *path, void *node,
                     int flag)
{
 struct SplitparT *split_parts= NULL;
 int split_count= 0, ret;
 struct stat stbuf;

 ret= Xorriso_identify_split(xorriso, path, node, &split_parts,
                             &split_count, &stbuf, flag & 3);
 if(split_parts!=NULL)
   Splitparts_destroy(&split_parts, split_count, 0);
 return(ret>0);
}


/* @param node      Opaque handle to IsoNode which is to be inquired instead of                     path if it is not NULL.
   @param path      is used as address if node is NULL.
   @param acl_text  if acl_text is not NULL, then *acl_text will be set to the
                    ACL text (without comments) of the file object. In this
                    case it finally has to be freed by the caller.
   @param flag      bit0= do not report to result but only retrieve ACL text
                    bit1= check for existence of true ACL (not fabricated),
                          do not allocate and set acl_text but return 1 or 2
                    bit2-3: what ALC to retrieve:
                          0= "access" and "default", mark "default:"
                          1= "access" only
                          2= "default" only, do not mark "default:"
                    bit4= get "access" ACL only if not trivial
   @return          2 ok, no ACL available, eventual *acl_text will be NULL
                    1 ok, ACL available, eventual *acl_text stems from malloc()
                  <=0 error
*/
int Xorriso_getfacl(struct XorrisO *xorriso, void *in_node, char *path,
                    char **acl_text, int flag)
{
 int ret, d_ret, result_len= 0, pass, what;
 IsoNode *node;
 char *text= NULL, *d_text= NULL, *cpt, *npt;
 uid_t uid;
 gid_t gid;
 struct passwd *pwd;
 struct group *grp;

 what= (flag >> 2) & 3;
 if(acl_text != NULL)
   *acl_text= NULL;

 node= (IsoNode *) in_node;
 if(node == NULL) {
   ret= Xorriso_get_node_by_path(xorriso, path, NULL, &node, 0);
   if(ret<=0)
     goto ex;
 }
 ret= iso_node_get_acl_text(node, &text, &d_text, flag & 16);
 d_ret= (d_text != NULL);
 if(ret < 0 || d_ret < 0) {
   if(path != NULL && path[0] != 0) {
     strcpy(xorriso->info_text, "Error with obtaining ACL of ");
     Text_shellsafe(path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   }
   ret= 0; goto ex;
 }
 if(flag & 2) {
   ret= 1 + (ret != 1 && d_ret == 0);
   goto ex;
 }
 if((ret == 0 || ret == 2) && d_ret == 0) {
   if(flag & 1) {
     ret= 1 + (ret == 0);
     goto ex;
   }
   strcpy(xorriso->info_text, "No ACL associated with ");
   Text_shellsafe(path, xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
   if(ret == 0)
     {ret= 2; goto ex;}
 }

 if(!(flag & 1)) {
   ret= Xorriso_getfname(xorriso, path, 0);
   if(ret <= 0)
     goto ex;
   uid= iso_node_get_uid(node);
   pwd= getpwuid(uid);
   if(pwd == NULL)
     sprintf(xorriso->result_line, "# owner: %.f\n", (double) uid);
   else
     sprintf(xorriso->result_line, "# owner: %s\n", pwd->pw_name);
   Xorriso_result(xorriso, 0);
   gid= iso_node_get_gid(node);
   grp= getgrgid(gid);
   if(grp == NULL)
     sprintf(xorriso->result_line, "# group: %.f\n", (double) gid);
   else
     sprintf(xorriso->result_line, "# group: %s\n", grp->gr_name);
   Xorriso_result(xorriso, 0);
 }

 for(pass= 0; pass < 1 + (acl_text != NULL && !(flag & 2)); pass++) {
   if(pass) {
     *acl_text= calloc(result_len + 1, 1);
     if(*acl_text == NULL) {
       Xorriso_no_malloc_memory(xorriso, NULL, 0);
       ret= -1; goto ex;
     }
   }
   if(text != NULL && what <= 1) {
     for(npt= cpt= text; npt != NULL; cpt= npt + 1) {
       npt= strchr(cpt, '\n');
       if(npt != NULL)
         *npt= 0;
       if(*cpt == 0) {
         if(d_text != NULL || pass) {
           if(npt != NULL)
             *npt= '\n';
     continue;
         }
       } else
         result_len+= strlen(cpt) + 1;
       if(pass) {
         sprintf(*acl_text + strlen(*acl_text), "%s\n", cpt);
       } else if(!(flag & 1)) {
         Sfile_str(xorriso->result_line, cpt, 0);
         strcat(xorriso->result_line, "\n");
         Xorriso_result(xorriso, 0);
       }
       if(npt != NULL)
         *npt= '\n';
     }
   }
   if(d_text != NULL && (what == 0 || what == 2)) {
     for(npt= cpt= d_text; npt != NULL; cpt= npt + 1) {
       npt= strchr(cpt, '\n');
       if(npt != NULL)
         *npt= 0;
       if(*cpt != 0) {
         if(pass) {
           if(what == 0)
             sprintf(*acl_text + strlen(*acl_text), "default:%s\n", cpt);
           else
             sprintf(*acl_text + strlen(*acl_text), "%s\n", cpt);
         } else {
           xorriso->result_line[0]= 0;
           if(what == 0)
             Sfile_str(xorriso->result_line, "default:", 0);
           Sfile_str(xorriso->result_line, cpt, 1);
           result_len+= strlen(cpt) + 9;
         }
       } else
         xorriso->result_line[0]= 0;
       if(pass== 0 && !(flag & 1)) {
         strcat(xorriso->result_line, "\n");
         Xorriso_result(xorriso, 0);
       }
       if(npt != NULL)
         *npt= '\n';
     }
   }
 }
 ret= 1;
ex:;
 iso_node_get_acl_text(node, &text, &d_text, 1 << 15);
 return(ret);
}


/*
   @param flag      bit0= do not report to result but only retrieve attr text
                    bit1= path is disk_path
                    bit3= do not ignore eventual non-user attributes.
                    bit5= in case of symbolic link on disk: inquire link target
                    bit6= check for existence of xattr, return 0 or 1
                          (depends also on bit3)
*/
int Xorriso_getfattr(struct XorrisO *xorriso, void *in_node, char *path,
                     char **attr_text, int flag)
{
 int ret= 1, i, bsl_mem, result_len= 0, pass;
 size_t num_attrs= 0, *value_lengths= NULL;
 char **names= NULL, **values= NULL, *bsl;

 if(attr_text != NULL)
   *attr_text= NULL;
 ret= Xorriso_get_attrs(xorriso, in_node, path, &num_attrs, &names,
                        &value_lengths, &values, flag & (2 | 8 | 32));
 if(ret <= 0)
   goto ex;
 if(flag & 64) {
   ret= (num_attrs > 0);
   goto ex;
 }
 if(num_attrs == 0)
   {ret= 2; goto ex;}

 if(!(flag & 1)) {
   ret= Xorriso_getfname(xorriso, path, 0);
   if(ret <= 0)
     goto ex;
 }
 for(pass= 0; pass < 1 + (attr_text != NULL); pass++) {
   if(pass) {
     *attr_text= calloc(result_len + 1, 1);
     if(*attr_text == NULL) {
       Xorriso_no_malloc_memory(xorriso, NULL, 0);
       ret= -1; goto ex;
     }
   }
   for(i= 0; i < (int) num_attrs; i++) {
     if(strlen(names[i]) + value_lengths[i] >= SfileadrL) {
       sprintf(xorriso->result_line, "# oversized: name %d , value %d bytes\n",
               (int) strlen(names[i]), (int) value_lengths[i]);
     } else {
       ret= Sfile_bsl_encoder(&bsl, names[i], strlen(names[i]), 8);
       if(ret <= 0) 
         {ret= -1; goto ex;}
       strcpy(xorriso->result_line, bsl);
       free(bsl);
       ret= Sfile_bsl_encoder(&bsl, values[i], value_lengths[i], 8);
       if(ret <= 0) 
         {ret= -1; goto ex;}
       sprintf(xorriso->result_line + strlen(xorriso->result_line),
               "=\"%s\"\n", bsl);
       free(bsl);
     }
     /* temporarily disable -backslash_codes with result output */
     result_len+= strlen(xorriso->result_line);
     if(pass) {
       strcat(*attr_text, xorriso->result_line);
     } else if(!(flag & 1)) {
       bsl_mem= xorriso->bsl_interpretation;
       xorriso->bsl_interpretation= 0;
       Xorriso_result(xorriso, 0);
       xorriso->bsl_interpretation= bsl_mem;
     }
   }
 }
 if(!(flag & 1)) {
   strcpy(xorriso->result_line, "\n");
   Xorriso_result(xorriso, 0);
 }
 ret= 1;
ex:;
 Xorriso_get_attrs(xorriso, in_node, path, &num_attrs, &names,
                   &value_lengths, &values, 1 << 15);
 return(ret);
}


/*
   @param flag      bit0= with mode "e" : Use echo -e encoding but
                          do not put out commands and quotation marks.
                          Rather apply double backslash.
*/
int Xorriso_append_extattr_comp(struct XorrisO *xorriso,
                                char *comp, size_t comp_len,
                                char *mode, int flag)
{
 int ret;
 char *line, *wpt, *bsl = NULL;
 unsigned char *upt, *uval;

 line= xorriso->result_line;
 uval= (unsigned char *) comp;

 if(*mode == 'q') {
   Text_shellsafe(comp, line, 1);
 } else if(*mode == 'e' || mode[0] == 0) {
   for(upt= uval; (size_t) (upt - uval) < comp_len; upt++)
     if(*upt <= 037 || *upt >= 0177)
   break;
  if((size_t) (upt - uval) < comp_len || (flag & 1)) {
     /* Use "$(echo -e '\0xyz')" */;
     if(!(flag & 1))
       strcat(line, "\"$(echo -e '");
     wpt= line + strlen(line);
     for(upt= uval; (size_t) (upt - uval) < comp_len; upt++) {
       if(*upt <= 037 || *upt >= 0177 || *upt == '\\' || *upt == '\'') {
         if(flag & 1)
           *(wpt++)= '\\';
         sprintf((char *) wpt, "\\0%-3.3o", *upt);
         wpt+= strlen(wpt);
       } else {
         *(wpt++)= *upt;
       }
     }
     *wpt= 0;
     if(!(flag & 1))
       strcpy(wpt, "')\"");
   } else {
     Text_shellsafe(comp, line, 1);
   }
 } else if(*mode == 'b') { 
   ret= Sfile_bsl_encoder(&bsl, comp, comp_len, 8);
   if(ret <= 0) 
     {ret= -1; goto ex;}
   strcat(line, bsl);
   free(bsl);
   bsl= NULL;
 } else if(*mode == 'r') {
   strcat(line, comp);
 }
 ret= 1;
ex:;
 if(bsl != NULL)
   free(bsl);
 return(ret);
}


/*
   @param flag      bit1= path is disk_path
                    bit3= do not ignore eventual non-user attributes.
                    bit5= in case of symbolic link on disk: inquire link target
*/
int Xorriso_list_extattr(struct XorrisO *xorriso, void *in_node, char *path,
                         char *show_path, char *mode, int flag)
{
 int ret= 1, i, bsl_mem;
 size_t num_attrs= 0, *value_lengths= NULL;
 char **names= NULL, **values= NULL, *cpt, *space_pt, *name_pt, *path_pt;
 char *line;
 unsigned char *upt, *uval;

 line= xorriso->result_line;
 ret= Xorriso_get_attrs(xorriso, in_node, path, &num_attrs, &names,
                        &value_lengths, &values, flag & (2 | 8 | 32));
 if(ret <= 0)
   goto ex;
 if(flag & 64) {
   ret= (num_attrs > 0);
   goto ex;
 }
 if(num_attrs == 0)
   {ret= 2; goto ex;}

 strcpy(line, "n=");
 path_pt= show_path + (show_path[0] == '/');
 if(path_pt[0] == 0)
   path_pt= ".";
 ret= Xorriso_append_extattr_comp(xorriso, path_pt, strlen(path_pt), mode, 0);
 if(ret <= 0)
   goto ex;
 strcat(line, "\n");
 Xorriso_result(xorriso, 0);
 for(i= 0; i < (int) num_attrs; i++) {
   line[0]= 0;
   uval= (unsigned char *) values[i];

   if(strlen(names[i]) + value_lengths[i] >= SfileadrL) {
     sprintf(line,
        "echo 'OMITTED: Oversized: name %d bytes, value %d bytes in file '\"$n\" >&2\n",
        (int) strlen(names[i]), (int) value_lengths[i]);
     Xorriso_result(xorriso, 0);
 continue;
   }

   /* Form:  $c space name value $n */

   /* Split namespace from name */
   cpt= strchr(names[i], '.');
   if(cpt == NULL) {
     space_pt= "user";
     name_pt= names[i];
   } else {
     *cpt= 0;
     space_pt= names[i];
     name_pt= cpt + 1;
   }

   /* FreeBSD setextattr cannot set 0-bytes */
   for(upt= uval; (size_t) (upt - uval) < value_lengths[i]; upt++)
     if(*upt == 0
       )
   break;
   if((size_t) (upt - uval) < value_lengths[i]) { 
     strcpy(line, "echo 'OMITTED: Value contains 0-bytes : space \"'\"");
     Xorriso_append_extattr_comp(xorriso, space_pt, strlen(space_pt), "e", 1);
     if(ret <= 0)
       goto ex;
     strcat(line, "\"'\" , name \"'\"");
     Xorriso_append_extattr_comp(xorriso, name_pt, strlen(name_pt), "e", 1);
     if(ret <= 0)
       goto ex;
     strcat(line, "\"'\" in file '\"");
     Xorriso_append_extattr_comp(xorriso, path_pt, strlen(path_pt), "e", 1);
     strcat(line, "\" >&2\n");

     /* temporarily disable -backslash_codes with result output */
     bsl_mem= xorriso->bsl_interpretation;
     xorriso->bsl_interpretation= 0;
     Xorriso_result(xorriso, 0);
     xorriso->bsl_interpretation= bsl_mem;
     strcpy(line, "# ");
   }

   strcat(line, "$c ");
   ret= Xorriso_append_extattr_comp(xorriso, space_pt, strlen(space_pt),
                                    mode, 0);
   if(ret <= 0)
     goto ex;
   strcat(line, " ");
   ret= Xorriso_append_extattr_comp(xorriso,name_pt, strlen(name_pt), mode, 0);
   if(ret <= 0)
     goto ex;
   strcat(line, " ");
   ret= Xorriso_append_extattr_comp(xorriso, values[i], value_lengths[i],
                                    mode, 0);
   if(ret <= 0)
     goto ex;
   strcat(line, " \"$n\"\n");

   /* temporarily disable -backslash_codes with result output */
   bsl_mem= xorriso->bsl_interpretation;
   xorriso->bsl_interpretation= 0;
   Xorriso_result(xorriso, 0);
   xorriso->bsl_interpretation= bsl_mem;
 }
 strcpy(line, "\n");
 Xorriso_result(xorriso, 0);
 ret= 1;
ex:;
 Xorriso_get_attrs(xorriso, in_node, path, &num_attrs, &names,
                   &value_lengths, &values, 1 << 15);
 return(ret);
}


/*
 @param flag
       Bitfield for control purposes
            bit0=  get default ACL rather than access ACL
            bit4=  set *text = NULL and return 2
                   if the ACL matches st_mode permissions.
            bit5=  in case of symbolic link: inquire link target
            bit15= free text and return 1
 @return
       1 ok
       2 ok, trivial ACL found while bit4 is set, *text is NULL
       0 no ACL manipulation adapter available / ACL not supported by fs
      -1 failure of system ACL service (see errno)
      -2 attempt to inquire ACL of a symbolic link without bit4 or bit5
         resp. with no suitable link target
*/
int Xorriso_local_getfacl(struct XorrisO *xorriso, char *disk_path,
                          char **text, int flag)
{
 int ret, skip= 0, colons= 0, countdown= 0;
 char *acl= NULL, *cpt, *wpt;

 if(flag & (1 << 15)) {
   if(*text != NULL)
     free(*text);
   *text= NULL;
   return(1);
 }
 *text= NULL;
 ret= iso_local_get_acl_text(disk_path, &acl, flag & (1 | 16 | 32));
 Xorriso_process_msg_queues(xorriso,0);
 if(ret < 0 || ret == 2)
   return(ret);
 if(acl == NULL)
   return(0);
 *text= strdup(acl);
 iso_local_get_acl_text(disk_path, &acl, 1 << 15);
 if(*text == NULL) {
   Xorriso_no_malloc_memory(xorriso, NULL, 0);
   return(-1);
 }

 /* Garbage collection about trailing remarks after 3 permission chars */
 wpt= *text;
 for(cpt= *text; *cpt; cpt++) {
   if(skip) {
     if(*cpt == '\n')
       skip= 0;
     else
 continue;
   }
   if(*cpt == ':' && !countdown) {
     colons++;
     if(colons == 2) {
       countdown= 4;
       colons= 0;
     }
   }
   if(countdown > 0) {
     countdown--;
     if(countdown == 0)
       skip= 1;
   }
   *wpt= *cpt;
   wpt++;
 }
 *wpt= 0;

 return(1);
}


/*
   @param flag
               bit1= path is disk_path
               bit3= do not ignore eventual non-user attributes.
               bit5= in case of symbolic link on disk: inquire link target
              bit15= free memory
*/
int Xorriso_get_attrs(struct XorrisO *xorriso, void *in_node, char *path,
                      size_t *num_attrs, char ***names,
                      size_t **value_lengths, char ***values, int flag)
{
 int ret, i, widx;
 IsoNode *node;

 if(flag & (1 << 15)) {
   if(flag & 2) {
     iso_local_get_attrs(NULL, num_attrs, names, value_lengths, values,
                         1 << 15);
   } else {
     iso_node_get_attrs(NULL, num_attrs, names, value_lengths, values,
                        1 << 15);
   }
   return(1);
 }

 *num_attrs= 0;
 if(flag & 2) {
   ret= iso_local_get_attrs(path, num_attrs, names, value_lengths, values,
                            flag & (8 | 32));
   if(ret < 0) {
     strcpy(xorriso->info_text, "Error with reading xattr of disk file ");
     Text_shellsafe(path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno, "FAILURE", 0);
   }
 } else {
   node= (IsoNode *) in_node;
   if(node == NULL) {
     ret= Xorriso_get_node_by_path(xorriso, path, NULL, &node, 0);
     if(ret<=0)
       goto ex;
   }
   ret= iso_node_get_attrs(node, num_attrs, names, value_lengths, values,
                           0);
   if(ret < 0) {
     Xorriso_report_iso_error(xorriso, "", ret,
                    "Error when obtaining xattr of ISO node", 0, "FAILURE", 1);
     goto ex;
   }

   if(!(flag & 8)) {
     /* Filter away any non-userspace xattr */;
     widx= 0;
     for(i= 0; i < (int) *num_attrs; i++) {
       if(strncmp((*names)[i], "user.", 5) != 0) {
         free((*names)[i]);
         (*names)[i]= NULL;
         if((*values)[i] != NULL) {
           free((*values)[i]);
           (*values)[i]= NULL;
         }
       } else {
         if(widx != i) {
           (*names)[widx]= (*names)[i];
           (*value_lengths)[widx]= (*value_lengths)[i];
           (*values)[widx]= (*values)[i];
           (*names)[i]= NULL;
           (*value_lengths)[i]= 0;
           (*values)[i]= NULL;
         }
         widx++;
       }
     }
     *num_attrs= widx;
   }
 }
 ret= 1;
ex:;
 Xorriso_process_msg_queues(xorriso,0);
 return(ret);
}


int Xorriso_get_attr_value(struct XorrisO *xorriso, void *in_node, char *path,
                      char *name, size_t *value_length, char **value, int flag)
{
 int ret;
 size_t num_attrs= 0, *value_lengths= NULL, i;
 char **names = NULL, **values= NULL;

 *value= NULL;
 *value_length= 0;
 ret= Xorriso_get_attrs(xorriso, in_node, path, &num_attrs, &names,
                        &value_lengths, &values, 8);
 if(ret <= 0)
   goto ex;

 for(i= 0; i < num_attrs; i++) {
   if(strcmp(name, names[i]) != 0)
 continue;
   *value= calloc(value_lengths[i] + 1, 1);
   if(*value == NULL)
     {ret= -1; goto ex;}
   memcpy(*value, values[i], value_lengths[i]);
   (*value)[value_lengths[i]]= 0;
   *value_length= value_lengths[i];
   ret= 1; goto ex;
 }
 ret= 0;
ex:
 Xorriso_get_attrs(xorriso, in_node, path, &num_attrs, &names,
                        &value_lengths, &values, 1 << 15);
 return(ret);
}


int Xorriso_stream_type(struct XorrisO *xorriso, IsoNode *node,
                        IsoStream *stream, char type_text[], int flag)
{
 int ret, lba;
 char text[5];

 strncpy(text, stream->class->type, 4);
 text[4]= 0;
 if(strcmp(text, "fsrc") == 0) {
   ret= Xorriso__file_start_lba(node, &lba, 0);
   if(ret > 0 && lba > 0)
     strcpy(type_text, "image");
   else
     strcpy(type_text, "disk");
 } else if(strcmp(text, "ziso") == 0) {
   strcpy(type_text, "--zisofs");
 } else if(strcmp(text, "osiz") == 0) {
   strcpy(type_text, "--zisofs-decode");
 } else if(strcmp(text, "gzip") == 0) {
   strcpy(type_text, "--gzip");
 } else if(strcmp(text, "pizg") == 0) {
   strcpy(type_text, "--gunzip");
 } else if(strcmp(text, "cout") == 0 || strcmp(text, "boot") == 0 ||
           strcmp(text, "user") == 0 || strcmp(text, "extf") == 0) {
   strcpy(type_text, text);
 } else {
   Text_shellsafe(text, type_text, 0);
 }
 return(1);
} 


/*
   @param flag      bit0= do not report to result but only retrieve md5 text
   @return          1= ok, 0= no md5 available, <0= other error
*/
int Xorriso_get_md5(struct XorrisO *xorriso, void *in_node, char *path,
                    char md5[16], int flag)
{
 int ret= 1, i;
 char *wpt;
 IsoImage *image;
 IsoNode *node;

 ret= Xorriso_get_volume(xorriso, &image, 0);
 if(ret <= 0)
   goto ex;
 node= (IsoNode *) in_node;
 if(node == NULL) {
   ret= Xorriso_get_node_by_path(xorriso, path, NULL, &node, 0);
   if(ret<=0)
     goto ex;
 }
 if(!LIBISO_ISREG(node))
   return(0);
 ret= iso_file_get_md5(image, (IsoFile *) node, md5, 0);
 Xorriso_process_msg_queues(xorriso,0);
 if(ret <= 0)
   goto ex;
 if(flag & 1)
   {ret= 1; goto ex;}

 wpt= xorriso->result_line;
 for(i= 0; i < 16; i++) {
   sprintf(wpt, "%2.2x", ((unsigned char *) md5)[i]);
   wpt+= 2;
 }
 strcpy(wpt, "  ");
 wpt+= 2;
 Xorriso_getfname(xorriso, path, 1 | 2);
 ret= 1;
ex:;
 return(ret);
}


int Xorriso_make_md5(struct XorrisO *xorriso, void *in_node, char *path,
                     int flag)
{
 int ret;
 off_t size;
 IsoNode *node;
 
 node= (IsoNode *) in_node;
 if(node == NULL) {
   ret= Xorriso_get_node_by_path(xorriso, path, NULL, &node, 0);
   if(ret <= 0)
     return(ret);
 }
 if(!LIBISO_ISREG(node))
   return(0);
 ret= iso_file_make_md5((IsoFile *) node, 0);
 size= iso_file_get_size((IsoFile *) node);
 xorriso->pacifier_count+= size;
 xorriso->pacifier_byte_count+= size;
 Xorriso_pacifier_callback(xorriso, "content bytes read",
                           xorriso->pacifier_count, 0, "", 8);
 Xorriso_process_msg_queues(xorriso, 0);
 if(ret < 0) {
   Xorriso_report_iso_error(xorriso, "", ret,
                            "Error when computing MD5", 0, "FAILURE", 1);
   return(0);
 }
 Xorriso_set_change_pending(xorriso, 1);
 return(1);
}


/* @param flag bit0= do not only sum up sizes but also print subdirs
*/
int Xorriso_show_du_subs(struct XorrisO *xorriso, IsoDir *dir_node,
                      char *abs_path, char *rel_path, off_t *size,
                      off_t boss_mem, int flag)
{
 int i, ret, no_sort= 0, filec= 0, l;
 IsoDirIter *iter= NULL;
 IsoNode *node, **node_array= NULL;
 char *name;
 off_t sub_size, report_size, mem= 0;
 char *path= NULL, *show_path= NULL, *sfe= NULL;

 sfe= malloc(5*SfileadrL);
 path= malloc(SfileadrL);
 show_path= malloc(SfileadrL);
 if(path==NULL || show_path==NULL || sfe==NULL) {
   Xorriso_no_malloc_memory(xorriso, &sfe, 0);
   {ret= -1; goto ex;}
 }

 *size= 0;
 ret= iso_dir_get_children(dir_node, &iter);
 if(ret<0) {
cannot_create_iter:;
   Xorriso_cannot_create_iter(xorriso, ret, 0);
   {ret= -1; goto ex;}
 }
 for(i= 0; iso_dir_iter_next(iter, &node) == 1; ) {
   sub_size= 0;
   name= (char *) iso_node_get_name(node);
   strcpy(show_path, rel_path);
   if(Sfile_add_to_path(show_path, name, 0)<=0)
       goto much_too_long;
   if(LIBISO_ISDIR(node)) {
     strcpy(path, abs_path);
     if(Sfile_add_to_path(path, name, 0)<=0) {
much_too_long:;
       Xorriso_much_too_long(xorriso, strlen(path)+strlen(name)+1, 2);
       {ret= -1; goto ex;}
     }
     filec++;
     l= strlen(rel_path)+1;
     mem+= l;
     if(l % sizeof(char *))
       mem+= sizeof(char *)-(l % sizeof(char *));
     if(flag&1) /* diving and counting is done further below */
 continue;
     ret= Xorriso_show_du_subs(xorriso, (IsoDir *) node,
                               path, show_path, &sub_size, boss_mem, 0);
     if(ret<0)
       goto ex;
     if(ret==0)
 continue;
   }

   if(LIBISO_ISREG(node)) {
     sub_size+= iso_file_get_size((IsoFile *) node)+2048;
/*
     sub_size+= iso_file_get_size((IsoFile *) node)+strlen(name)+1;
*/
   }

   if(sub_size>0)
     (*size)+= sub_size;
   Xorriso_process_msg_queues(xorriso,0);
 }

 if(filec<=0 || !(flag&1))
   {ret= 1; goto ex;}

 /* Reset iteration */
 iso_dir_iter_free(iter);
 iter= NULL;
 Xorriso_process_msg_queues(xorriso,0);

 ret= Xorriso_sorted_node_array(xorriso, dir_node, &filec, &node_array,
                                boss_mem, 1|2|4);
 if(ret<0)
   goto ex;
 if(ret==0) {
   no_sort= 1;
   ret= iso_dir_get_children(dir_node, &iter);
   if(ret<0)
     goto cannot_create_iter;
 }

 for(i= 0; (no_sort || i<filec) && !(xorriso->request_to_abort); i++) {
   if(no_sort) {
     ret= iso_dir_iter_next(iter, &node);
     if(ret!=1)
 break;
     if(!LIBISO_ISDIR(node))
 continue;
   } else
     node= node_array[i];

   sub_size= 0;
   name= (char *) iso_node_get_name(node);
   strcpy(show_path, rel_path);
   if(Sfile_add_to_path(show_path, name, 0)<=0)
     goto much_too_long;
   strcpy(path, abs_path);
   if(Sfile_add_to_path(path, name, 0)<=0)
     goto much_too_long;
   ret= Xorriso_show_du_subs(xorriso, (IsoDir *) node,
                             path, show_path, &sub_size, boss_mem+mem, flag&1);
   if(ret<0)
     goto ex;

   if(LIBISO_ISREG(node)) {
     sub_size+= iso_file_get_size((IsoFile *) node)+2048;
/*
     sub_size+= iso_tree_node_get_size((IsoFile *) node)+strlen(name)+1;
*/
   }
   if(sub_size>0)
     (*size)+= sub_size;
   report_size= sub_size/1024;
   if(report_size*1024<sub_size)
      report_size++;
   sprintf(xorriso->result_line, "%7.f ",(double) (report_size));
   sprintf(xorriso->result_line+strlen(xorriso->result_line), "%s\n",
           Text_shellsafe(show_path, sfe, 0));
   Xorriso_result(xorriso, 0);
 }
 ret= 1;
ex:;
 if(sfe!=NULL)
   free(sfe);
 if(path!=NULL)
   free(path);
 if(show_path!=NULL)
   free(show_path);
 if(iter!=NULL)
   iso_dir_iter_free(iter);
 if(node_array!=NULL)
   free((char *) node_array);
 Xorriso_process_msg_queues(xorriso,0);
 return(ret);
}


int Xorriso_sorted_dir_i(struct XorrisO *xorriso, IsoDir *dir_node,
                         int *filec, char ***filev, off_t boss_mem, int flag)
{
 int i,j,ret;
 IsoDirIter *iter= NULL;
 IsoNode *node;
 char *name;
 off_t mem;

 (*filec)= 0;
 (*filev)= NULL;

 ret= iso_dir_get_children(dir_node, &iter);
 if(ret<0) {
cannot_iter:;
   Xorriso_cannot_create_iter(xorriso, ret, 0);
   {ret= -1; goto ex;}
 }
 mem= 0;
 for(i= 0; iso_dir_iter_next(iter, &node) == 1; ) {
   name= (char *) iso_node_get_name(node);
   mem+= sizeof(char *)+strlen(name)+8;
   (*filec)++;
 }
 iso_dir_iter_free(iter);
 iter= NULL;
 if(*filec==0)
   {ret= 1; goto ex;}

 ret= Xorriso_check_temp_mem_limit(xorriso, mem+boss_mem, 2);
 if(ret<=0)
   goto ex;
 (*filev)= (char **) calloc(*filec, sizeof(char *));
 if(*filev==NULL)
   {ret= -1; goto ex; }
 ret= iso_dir_get_children(dir_node, &iter);
 if(ret<0) 
   goto cannot_iter;
 for(i= 0; i<*filec; i++) {
   ret= iso_dir_iter_next(iter, &node);
   if(ret!=1)
 break;
   name= (char *) iso_node_get_name(node);
   (*filev)[i]= strdup(name);
   if((*filev)[i]==NULL) {
     for(j= 0; j<i; j++)
       if((*filev)[j]!=NULL)
         free((*filev)[j]);
     free((char *) (*filev));
     ret= -1; goto ex;
   }
 }
 Sort_argv(*filec, *filev, 0);
 ret= 1;
ex:;
 if(iter!=NULL)
   iso_dir_iter_free(iter);
 return(ret);
}


int Xorriso_node_eff_hidden(struct XorrisO *xorriso, IsoNode *node, int flag)
{
 int hidden_state= 0, ret;
 IsoNode *current, *parent;

 current= node;
 for(current= node; hidden_state != 7;) {
   ret= iso_node_get_hidden(current);
   if(ret & LIBISO_HIDE_ON_RR)
     hidden_state|= 1;
   if(ret & LIBISO_HIDE_ON_JOLIET)
     hidden_state|= 2;
   if(ret & LIBISO_HIDE_ON_HFSPLUS)
     hidden_state|= 4;
   parent= (IsoNode *) iso_node_get_parent(current);
   if(parent == current)
 break;
   current= parent;
 }
 return(hidden_state);
}


/* @param flag bit0= long format
               bit1= do not print count of nodes
               bit2= du format
               bit3= print directories as themselves (ls -d)
*/
int Xorriso_ls_filev(struct XorrisO *xorriso, char *wd,
                     int filec, char **filev, off_t boss_mem, int flag)
{
 int i, ret, was_error= 0, dfilec= 0, pass, passes, hidden_state= 0;
 IsoNode *node;
 IsoImage *volume;
 char *path= NULL, *link_target= NULL, *rpt, **dfilev= NULL;
 char *a_text= NULL, *d_text= NULL;
 off_t size;
 struct stat stbuf;

 Xorriso_alloc_meM(path, char, SfileadrL);
 Xorriso_alloc_meM(link_target, char, SfileadrL);

 rpt= xorriso->result_line;

 ret= Xorriso_get_volume(xorriso, &volume, 0);
 if(ret<=0)
   goto ex;

 Sort_argv(filec, filev, 0);

 /* Count valid nodes, warn of invalid ones */
 for(i= 0; i<filec; i++) {
   ret= Xorriso_make_abs_adr(xorriso, wd, filev[i], path, 1|2|4);
   if(ret<=0) {
     was_error++;
 continue;
   }
   ret= Xorriso_node_from_path(xorriso, volume, path, &node, 1);
   if(ret<=0) {
     sprintf(xorriso->info_text, "Not found in ISO image: ");
     Text_shellsafe(path, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
     was_error++;
 continue;
   }
 }

 if((flag&8) && !(flag&(2|4))) {
   sprintf(xorriso->info_text, "Valid ISO nodes found: %d\n", filec-was_error);
   Xorriso_info(xorriso,1);
   if(filec-was_error<=0)
     {ret= !was_error; goto ex;}
 }

 passes= 1+!(flag&(4|8));
 for(pass= 0; pass<passes; pass++)
 for(i= 0; i<filec && !(xorriso->request_to_abort); i++) {
   rpt[0]= 0;
   ret= Xorriso_make_abs_adr(xorriso, wd, filev[i], path, 1|2|4);
   if(ret<=0)
 continue;
   ret= Xorriso_fake_stbuf(xorriso, path, &stbuf, &node, ((flag&4) >> 1) | 16);
   if(ret<=0)
 continue;
   if(LIBISO_ISDIR(node)  && !(flag&(4|8))) {
     if(pass==0)
 continue;
     if(filec>1) {
       strcpy(xorriso->result_line, "\n");
       Xorriso_result(xorriso,0);
       Text_shellsafe(filev[i], xorriso->result_line, 0);
       strcat(xorriso->result_line, ":\n");
       Xorriso_result(xorriso,0);
     }
     ret= Xorriso_sorted_dir_i(xorriso, 
                               (IsoDir *) node, &dfilec, &dfilev, boss_mem, 0);
     if(ret<=0) {

       /* >>> libisofs iterator loop and single item Xorriso_lsx_filev() */;

     } else {
       if(flag&1) {
         sprintf(xorriso->result_line, "total %d\n", dfilec);
         Xorriso_result(xorriso,0);
       }
       Xorriso_ls_filev(xorriso, path,
                        dfilec, dfilev, boss_mem, (flag&1)|2|8);
     }
     if(dfilec>0)
       Sfile_destroy_argv(&dfilec, &dfilev, 0);
 continue;
   } else
     if(pass>0)
 continue;
   link_target[0]= 0;
   if((flag&5)==1) { /* -ls_l */
     iso_node_get_acl_text(node, &a_text, &d_text, 16);
     hidden_state= Xorriso_node_eff_hidden(xorriso, node, 0);
     ret= Xorriso_format_ls_l(xorriso, &stbuf,
                              1 | ((a_text != NULL || d_text != NULL) << 1) |
                              (hidden_state << 2));
     iso_node_get_acl_text(node, &a_text, &d_text, 1 << 15);
     if(ret<=0)
 continue;
     if(LIBISO_ISLNK(node)) {
       if(Sfile_str(link_target, (char *) iso_symlink_get_dest(
                                                   (IsoSymlink *) node), 0)<=0)
         link_target[0]= 0;
     }
   } else if(flag&4) { /* -du or -dus */
     size= stbuf.st_size;
     if(S_ISDIR(stbuf.st_mode)) {
       ret= Xorriso_show_du_subs(xorriso, (IsoDir *) node,
                                 path, filev[i], &size, boss_mem, flag&1);
       if(ret<0)
         {ret= -1; goto ex;}
       if(ret==0)
 continue;
     }
     sprintf(rpt, "%7.f ",(double) (size/1024));
   }
   if(link_target[0] && (flag&5)==1) {
     Text_shellsafe(filev[i], xorriso->result_line, 1),
     strcat(xorriso->result_line, " -> ");
     Text_shellsafe(link_target, xorriso->result_line, 1 | 2);
   } else {
     Text_shellsafe(filev[i], xorriso->result_line, 1);
   }
   strcat(xorriso->result_line, "\n");
   Xorriso_result(xorriso, 0);
 }
 ret= !was_error;
ex:;
 Xorriso_free_meM(path);
 Xorriso_free_meM(link_target);
 return(ret);
}


/* This function needs less buffer memory than Xorriso_ls_filev() but cannot
   perform structured pattern matching as done by Xorriso_expand_pattern()
   for subsequent Xorriso_ls_filev().
   @param flag bit0= long format
               bit1= only check for directory existence
               bit2= do not apply search pattern but accept any file
               bit3= just count nodes and return number
*/
int Xorriso_ls(struct XorrisO *xorriso, int flag)
{
 int ret, i, filec= 0, failed_at, no_sort= 0;
 IsoNode *node, **node_array= NULL;
 IsoDir *dir_node;
 IsoDirIter *iter= NULL;
 char *link_target= NULL, *npt, *rpt;
 struct stat stbuf;

 Xorriso_alloc_meM(link_target, char, SfileadrL);

 rpt= xorriso->result_line;

 ret= Xorriso_dir_from_path(xorriso, "Working directory", xorriso->wdi,
                            &dir_node, 0);
 if(ret <= 0)
   goto ex;
 ret= iso_dir_get_children(dir_node, &iter);
 if(ret<0) {
cannot_create_iter:;
   Xorriso_cannot_create_iter(xorriso, ret, 0);
   {ret= -1; goto ex;}
 }
 Xorriso_process_msg_queues(xorriso,0);

 for(i= 0; iso_dir_iter_next(iter, &node) == 1; ) {
   npt= (char *) iso_node_get_name(node);
   if(!(flag&4)) {
     ret= Xorriso_regexec(xorriso, npt, &failed_at, 0);
     if(ret)
 continue; /* no match */
   }
   filec++;
 }
 /* Reset iteration */
 iso_dir_iter_free(iter);
 iter= NULL;
 Xorriso_process_msg_queues(xorriso,0);
 if(flag&8)
   {ret= filec; goto ex;}
 sprintf(xorriso->info_text, "Valid ISO nodes found: %d\n", filec);
 Xorriso_info(xorriso,1);

 ret= Xorriso_sorted_node_array(xorriso, dir_node, &filec, &node_array, 0,
                                flag&4);
 if(ret<0)
   goto ex;
 if(ret==0) {
   no_sort= 1;
   ret= iso_dir_get_children(dir_node, &iter);
   if(ret<0)
     goto cannot_create_iter;
 }

 for(i= 0; i<filec && !(xorriso->request_to_abort); i++) {
   if(no_sort) {
     ret= iso_dir_iter_next(iter, &node);
     if(ret!=1)
 break;
     npt= (char *) iso_node_get_name(node);
     if(!(flag&4)) {
       ret= Xorriso_regexec(xorriso, npt, &failed_at, 0);
       if(ret)
 continue; /* no match */
     }
   } else
     node= node_array[i];

   npt= (char *) iso_node_get_name(node);
   link_target[0]= 0;
   if(LIBISO_ISLNK(node)) {
     if(Sfile_str(link_target, (char *) iso_symlink_get_dest(
                                                   (IsoSymlink *) node), 0)<=0)
       link_target[0]= 0;
   }
   rpt[0]= 0;
   if(flag&1) {
     ret= Xorriso_fake_stbuf(xorriso, "", &stbuf, &node, 1);
     if(ret<=0)
 continue;
     ret= Xorriso_format_ls_l(xorriso, &stbuf, 1);
     if(ret<=0)
 continue;
   }
   if(link_target[0] && (flag&1)) {
     Text_shellsafe(npt, xorriso->result_line, 1); 
     strcat(xorriso->result_line, " -> ");
     Text_shellsafe(link_target, xorriso->result_line, 1 | 2);
   } else {
     Text_shellsafe(npt, xorriso->result_line, 1);
   }
   strcat(xorriso->result_line, "\n");
   Xorriso_result(xorriso, 0);
 }

 ret= 1;
ex:;
 if(iter!=NULL)
   iso_dir_iter_free(iter);
 Xorriso_process_msg_queues(xorriso,0);
 if(node_array!=NULL)
   free((char *) node_array);
 Xorriso_free_meM(link_target);
 return(ret);
}


/* @param flag bit0= count results rather than storing them
               bit1= this is a recursion
               bit2= prepend wd (automatically done if wd[0]!=0)
*/
int Xorriso_obtain_pattern_files_i(
       struct XorrisO *xorriso, char *wd, IsoDir *dir,
       int *filec, char **filev, int count_limit, off_t *mem,
       int *dive_count, int flag)
{
 int ret, failed_at;
 IsoDirIter *iter= NULL;
 IsoNode *node;
 char *name;
 char *adr= NULL;

 adr= malloc(SfileadrL);
 if(adr==NULL) {
   Xorriso_no_malloc_memory(xorriso, &adr, 0);
   {ret= -1; goto ex;}
 }

 if(!(flag&2))
   *dive_count= 0;
 else
   (*dive_count)++;
 ret= Xorriso_check_for_root_pattern(xorriso, filec, filev, count_limit,
                                     mem, (flag&1)|2);
 if(ret!=2)
   goto ex;

 ret= iso_dir_get_children(dir, &iter);
 if(ret<0) {
   Xorriso_cannot_create_iter(xorriso, ret, 0);
   {ret= -1; goto ex;}
 }
 while(iso_dir_iter_next(iter, &node) == 1) {
   name= (char *) iso_node_get_name(node);
   ret= Xorriso_make_abs_adr(xorriso, wd, name, adr, flag&4);
   if(ret<=0)
     goto ex;
   ret= Xorriso_regexec(xorriso, adr, &failed_at, 1);
   if(ret) { /* no match */
     if(failed_at <= *dive_count) /* no hope for a match */
 continue;

     if(!LIBISO_ISDIR(node)) {

       /* >>> How to deal with softlinks ? */

 continue;
     }
     /* dive deeper */
     ret= Xorriso_obtain_pattern_files_i(
                          xorriso, adr, (IsoDir *) node,
                          filec, filev, count_limit, mem, dive_count, flag|2);
     if(ret<=0)
       goto ex;
   } else {
     ret= Xorriso_register_matched_adr(xorriso, adr, count_limit,
                                       filec, filev, mem, (flag&1)|2);
     if(ret<=0)
       goto ex;
   }
 } 
 ret= 1;
ex:;
 if(adr!=NULL)
   free(adr);
 if(flag&2)
   (*dive_count)--;
 if(iter != NULL)
   iso_dir_iter_free(iter);
 return(ret);
}


/* @param flag bit0= a match count !=1 is a FAILURE event
               bit1= with bit0 tolerate 0 matches if pattern is a constant
*/
int Xorriso_expand_pattern(struct XorrisO *xorriso,
                           int num_patterns, char **patterns, int extra_filec,
                           int *filec, char ***filev, off_t *mem, int flag)
{
 int ret, count= 0, abs_adr= 0, i, was_count, was_filec;
 int nonconst_mismatches= 0, dive_count= 0;
 IsoImage *volume;
 IsoDir *dir= NULL, *root_dir;
 IsoNode *iso_node;

 *filec= 0;
 *filev= NULL;

 xorriso->search_mode= 3;
 xorriso->structured_search= 1;

 ret= Xorriso_get_volume(xorriso, &volume, 0);
 if(ret<=0)
   return(ret);
 root_dir= iso_image_get_root(volume);
 if(root_dir==NULL) {
   Xorriso_process_msg_queues(xorriso,0);
   sprintf(xorriso->info_text,
           "While expanding pattern : Cannot obtain root node of ISO image");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   ret= -1; goto ex;
 }

 for(i= 0; i<num_patterns; i++) {
   ret=  Xorriso_prepare_expansion_pattern(xorriso, patterns[i], 0);
   if(ret<=0)
     return(ret);
   if(ret==2)
     abs_adr= 4;
   if(patterns[i][0]=='/' || abs_adr) {
     dir= root_dir;
     abs_adr= 4;
   } else {
     /* This is done so late to allow the following:
        It is not an error if xorriso->wdi does not exist yet, but one may
        not use it as base for relative address searches.
     */
     ret= Xorriso_node_from_path(xorriso, volume, xorriso->wdi, &iso_node, 1);
     dir= (IsoDir *) iso_node;
     if(ret<=0) {
       Xorriso_process_msg_queues(xorriso,0);
       sprintf(xorriso->info_text, "While expanding pattern ");
       Text_shellsafe(patterns[i], xorriso->info_text, 1);
       strcat(xorriso->info_text,
              " : Working directory does not exist in ISO image");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
     }
     if(!LIBISO_ISDIR((IsoNode *) dir)) {
       sprintf(xorriso->info_text,
           "Working directory path does not lead to a directory in ISO image");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
     }
   }

   /* count the matches */
   was_count= count;
   ret= Xorriso_obtain_pattern_files_i(xorriso, "", dir, &count, NULL, 0,
                                       mem, &dive_count, 1 | abs_adr);
   if(ret<=0)
     goto ex;
   if(was_count==count && strcmp(patterns[i],"*")!=0 && (flag&3)!=1) {
     count++;
     Xorriso_eval_nonmatch(xorriso, patterns[i], &nonconst_mismatches, mem, 0);
   }
 }

 ret= Xorriso_check_matchcount(xorriso, count, nonconst_mismatches,
                               num_patterns, patterns, (flag&1)|2);
 if(ret<=0)
   goto ex;
 count+= extra_filec;
 mem+= extra_filec*sizeof(char *);
 if(count<=0)
   {ret= 0; goto ex;}
 ret= Xorriso_alloc_pattern_mem(xorriso, *mem, count, filev, 0);
 if(ret<=0)
   goto ex;
 /* now store addresses */
 for(i= 0; i<num_patterns; i++) {
   ret=  Xorriso_prepare_expansion_pattern(xorriso, patterns[i], 0);
   if(ret<=0)
     return(ret);
   if(ret==2)
     abs_adr= 4;

   was_filec= *filec;
   ret= Xorriso_obtain_pattern_files_i(xorriso, "", dir, filec, *filev, count,
                                       mem, &dive_count, abs_adr);
   if(ret<=0)
     goto ex;
   if(was_filec == *filec && strcmp(patterns[i],"*")!=0) {
     (*filev)[*filec]= strdup(patterns[i]);
     if((*filev)[*filec]==NULL) {
       (*mem)= strlen(patterns[i])+1;
       Xorriso_no_pattern_memory(xorriso, *mem, 0);
       ret= -1; goto ex;
     }
     (*filec)++;
   } 
 }
 ret= 1;
ex:;
 if(ret<=0) {
   if(filev!=NULL)
     Sfile_destroy_argv(&count, filev, 0);
   *filec= 0;
 }
 return(ret);
}


int Xorriso__start_end_lbas(IsoNode *node,
                            int *lba_count, int **start_lbas, int **end_lbas,
                            off_t *size, int flag)
{
 int section_count= 0, ret, i;
 struct iso_file_section *sections= NULL;

 *lba_count= 0;
 *start_lbas= *end_lbas= NULL;
 *size= 0;
 if(!LIBISO_ISREG(node))
   return(0);
 *size= iso_file_get_size((IsoFile *) node);
 ret= iso_file_get_old_image_sections((IsoFile *) node, &section_count,
                                      &sections, 0);
 if(ret < 0)
   {ret= -1; goto ex;}
 if(ret != 1 || section_count <= 0)
   {ret= 0; goto ex;}
 *start_lbas= calloc(section_count, sizeof(int));
 *end_lbas= calloc(section_count, sizeof(int));
 if(*start_lbas == NULL || *end_lbas == NULL)
   {ret= -1; goto ex;}
 for(i= 0; i < section_count; i++) {
   (*start_lbas)[i]= sections[i].block;
   (*end_lbas)[i]= sections[i].block + sections[i].size / 2048 - 1;
   if(sections[i].size % 2048)
     (*end_lbas)[i]++;
 }
 *lba_count= section_count;
 ret= 1;
ex:;
 if(sections != NULL)
   free((char *) sections);
 if(ret <= 0) {
   if((*start_lbas) != NULL)
     free((char *) *start_lbas);
   if((*end_lbas) != NULL)
     free((char *) *end_lbas);
   *start_lbas= *end_lbas= NULL;
   *lba_count= 0;
 }
 return(ret);
}


int Xorriso__file_start_lba(IsoNode *node,
                           int *lba, int flag)
{
 int *start_lbas= NULL, *end_lbas= NULL, lba_count= 0, i, ret;
 off_t size;

 *lba= -1;
 ret= Xorriso__start_end_lbas(node, &lba_count, &start_lbas, &end_lbas,
                              &size, 0);
 if(ret <= 0)
   return(ret);
 for(i= 0; i < lba_count; i++) {
   if(*lba < 0 || start_lbas[i] < *lba)
     *lba= start_lbas[i];
 }
 if(start_lbas != NULL)
   free((char *) start_lbas);
 if(end_lbas != NULL)
   free((char *) end_lbas);
 if(*lba < 0)
   return(0);
 return(1);
}


/* flag bit0= examine sub directories rather than data files
*/
int Xorriso_dir_disk_path(struct XorrisO *xorriso, IsoNode *dir_node,
                          char disk_path[SfileadrL], int flag)
{
 int ret;
 char *npt;
 IsoNode *node;
 IsoDir *dir;
 IsoDirIter *iter= NULL;

 dir= (IsoDir *) dir_node;
 ret= iso_dir_get_children(dir, &iter);
 if(ret<0) {
   Xorriso_cannot_create_iter(xorriso, ret, 0);
   {ret= -1; goto ex;}
 }
 while(1) {
   ret= iso_dir_iter_next(iter, &node);
   if(ret < 0) {
     Xorriso_report_iso_error(xorriso, "", ret,
               "Error when iterating over directory", 0, "FAILURE", 1);
     ret= -1; goto ex;
   }
   if(ret == 0)
 break;
     
   if(LIBISO_ISDIR(node) && (flag & 1)) {
     ret= Xorriso_dir_disk_path(xorriso, node, disk_path, flag);
     if(ret < 0)
       goto ex;
     if(ret == 0)
 continue;
   } else if(LIBISO_ISREG(node) && !(flag & 1)) {
     ret= Xorriso_retrieve_disk_path(xorriso, node, disk_path, 0);
     if(ret < 0)
       goto ex;
     if(ret == 0)
 continue;
   } else
 continue;
   /* Use its parent dir as answer */
   npt= strrchr(disk_path, '/');
   if(npt == NULL || npt == disk_path)
     strcpy(disk_path, "/");
   else
     *npt= 0;
   ret= 1; goto ex;
 }
 if(!(flag & 1))
   ret= Xorriso_dir_disk_path(xorriso, dir_node, disk_path, 1);
 else
   ret= 0;
ex:
 if(iter != NULL)
   iso_dir_iter_free(iter);
 return(ret);
}


int Xorriso_retrieve_disk_path(struct XorrisO *xorriso, IsoNode *node,
                               char disk_path[SfileadrL], int flag)
{
 IsoFile *file;
 IsoStream *stream= NULL, *input_stream;
 char type_text[80], *source_path = NULL;
 int ret;

 if(LIBISO_ISDIR(node)) {
   ret= Xorriso_dir_disk_path(xorriso, node, disk_path, 0);
   return(ret);
 }

 if(!LIBISO_ISREG(node)) 
   return(0);

 /* Obtain most fundamental input stream */
 file= (IsoFile *) node;
 input_stream= iso_file_get_stream(file);
 if(input_stream == NULL)
   return(0);
 while(1) {
   stream= input_stream;
   input_stream= iso_stream_get_input_stream(stream, 0);
   if(input_stream == NULL)
 break;
 }

 /* Obtain disk path if applicable */
 type_text[0]= 0;
 Xorriso_stream_type(xorriso, node, stream, type_text, 0);
 if(strcmp(type_text, "disk") != 0 && strcmp(type_text, "cout") != 0)
   return(0); /* among othersi rejected: "image" */
 source_path= iso_stream_get_source_path(stream, 0);
 if(source_path == NULL)
   return(0);
 if(strlen(source_path) >= SfileadrL) {
   free(source_path);
   return(0);
 }
 strcpy(disk_path, source_path);
 free(source_path);
 return(1);
}


int Xorriso_show_stream(struct XorrisO *xorriso, void *in_node,
                        char *path, int flag)
{
 int ret;
 IsoNode *node;
 IsoFile *file;
 IsoStream *stream= NULL, *input_stream;
 IsoExternalFilterCommand *cmd;
 char type_text[16], *source_path= NULL;

 node= (IsoNode *) in_node;
 if(node == NULL) {
   ret= Xorriso_get_node_by_path(xorriso, path, NULL, &node, 0);
   if(ret <= 0)
     goto ex;
 }
 if(!LIBISO_ISREG(node)) 
   {ret= 2; goto ex;}
 file= (IsoFile *) node;
 input_stream= iso_file_get_stream(file);
 Text_shellsafe(path, xorriso->result_line, 0);
 while(1) {
   stream= input_stream;
   input_stream= iso_stream_get_input_stream(stream, 0);
   if(input_stream == NULL)
 break;
   strcat(xorriso->result_line, " < ");
   Xorriso_stream_type(xorriso, node, stream, type_text, 0);
   strcat(xorriso->result_line, type_text);
   ret= iso_stream_get_external_filter(stream, &cmd, 0);
   if(ret < 0) {
     Xorriso_process_msg_queues(xorriso,0);
     Xorriso_report_iso_error(xorriso, "", ret,
               "Error when inquiring filter command of node", 0, "FAILURE", 1);
     ret= 0; goto ex;
   }
   if(ret > 0) {
     strcat(xorriso->result_line, ":");
     Text_shellsafe(cmd->name, xorriso->result_line, 1);
   }
   if(strlen(xorriso->result_line) > SfileadrL) {
     Xorriso_result(xorriso, 0);
     xorriso->result_line[0]= 0;
   }
 }
 strcat(xorriso->result_line, " < ");
 Xorriso_stream_type(xorriso, node, stream, type_text, 0);
 strcat(xorriso->result_line, type_text);

 source_path= iso_stream_get_source_path(stream, 0);
 if(source_path != NULL) {
   strcat(xorriso->result_line, ":");
   Text_shellsafe(source_path, xorriso->result_line, 1);
 }
 
 strcat(xorriso->result_line, "\n");
 Xorriso_result(xorriso, 0);
 ret= 1;
ex:;
 if(source_path != NULL)
   free(source_path);
 return(ret); 
}


/* @param damage_start Returns first damaged byte address
   @param damage_end   Returns first byte address after last damaged byte
   @return <0 error, 0=undamaged , 1=damaged
*/
int Xorriso_file_eval_damage(struct XorrisO *xorriso, IsoNode *node,
                             off_t *damage_start, off_t *damage_end,
                             int flag)
{
 int *start_lbas= NULL, *end_lbas= NULL, lba_count= 0, sect;
 int i, sectors, sector_size, ret;
 off_t sect_base= 0, size= 0, byte;
 struct SectorbitmaP *map;

 *damage_start= *damage_end= -1;
 map= xorriso->in_sector_map;
 if(map == NULL)
   return(0);
 Sectorbitmap_get_layout(map, &sectors, &sector_size, 0);
 sector_size/= 2048;

 ret= Xorriso__start_end_lbas(node, &lba_count, &start_lbas, &end_lbas,
                              &size, 0);
 if(ret <= 0) {
   Xorriso_process_msg_queues(xorriso, 0);
   return(ret);
 }
 for(sect= 0; sect < lba_count; sect++) {
   for(i= start_lbas[sect]; i <= end_lbas[sect]; i+= sector_size) {
     if(Sectorbitmap_is_set(map, i / sector_size, 0) == 0) {
       byte= ((off_t) 2048) * ((off_t) (i - start_lbas[sect])) + sect_base;
       if(*damage_start < 0 || byte < *damage_start)
         *damage_start= byte;
       if(byte + (off_t) 2048 > *damage_end)
         *damage_end= byte + (off_t) 2048;
     }
   }
   sect_base+= ((off_t) 2048) *
               ((off_t) (end_lbas[sect] - start_lbas[sect] + 1));
 }
 if(*damage_end > size)
   *damage_end= size;
 if(start_lbas != NULL)
   free((char *) start_lbas);
 if(end_lbas != NULL)
   free((char *) end_lbas);
 if(*damage_start < 0)
   return(0);
 return(1);
}


int Xorriso_report_lba(struct XorrisO *xorriso, char *show_path,
                       IsoNode *node, int flag)
{
 int ret, *start_lbas= NULL, *end_lbas= NULL, lba_count, i;
 off_t size;

 ret= Xorriso__start_end_lbas(node, &lba_count, &start_lbas, &end_lbas,
                              &size, 0);
 if(ret < 0) {
   Xorriso_process_msg_queues(xorriso, 0);
   {ret= -1; goto ex;}
 }
 if(ret == 0)
   {ret= 1; goto ex;} /* it is ok to ignore other types */
 for(i= 0; i < lba_count; i++) {
   sprintf(xorriso->result_line,
           "File data lba: %2d , %8d , %8d , %8.f , ",
           i, start_lbas[i], end_lbas[i] + 1 - start_lbas[i], (double) size);
   Text_shellsafe(show_path, xorriso->result_line, 1);
   strcat(xorriso->result_line, "\n");
   Xorriso_result(xorriso, 0);
 }
 ret= 1;
ex:;
 if(start_lbas != NULL)
   free((char *) start_lbas);
 if(end_lbas != NULL)
   free((char *) end_lbas);
 return(ret);
}


int Xorriso_report_damage(struct XorrisO *xorriso, char *show_path,
                          IsoNode *node, int flag)
{
 int ret;
 off_t size= 0, damage_start, damage_end;

 ret= Xorriso_file_eval_damage(xorriso, node, &damage_start, &damage_end, 0);
 if(ret < 0)
   return(0);

 if(LIBISO_ISREG(node))
   size= iso_file_get_size((IsoFile *) node);
 if(ret > 0) {
   sprintf(xorriso->result_line, "File damaged : %8.f , %8.f , %8.f , ",
           (double) damage_start, (double) (damage_end - damage_start) ,
           (double) size);
 } else {
   sprintf(xorriso->result_line, "File seems ok: %8.f , %8.f , %8.f , ",
           -1.0, -1.0, (double) size);
 }
 Text_shellsafe(show_path, xorriso->result_line, 1);
 strcat(xorriso->result_line, "\n");
 Xorriso_result(xorriso, 0);
 return(1);
}


/* @param flag bit0= do not accept hln_targets[i] != NULL as *node_idx
               bit1= use *node_idx as found index rather than searching it
               bit2= with bit1: use xorriso->node_array rather than hln_array
*/
int Xorriso_search_hardlinks(struct XorrisO *xorriso, IsoNode *node,
                             int *node_idx, int *min_hl, int *max_hl, int flag)
{
 int idx, ret, i, node_count;
 void *np, **node_array;

 node_array= xorriso->hln_array;
 node_count= xorriso->hln_count;
 *min_hl= *max_hl= -1;
 np= node;
 if(flag & 2) {
   idx= *node_idx;
   if(flag & 4) {
     node_array= xorriso->node_array;
     node_count= xorriso->node_counter;
   }
 } else {
   *node_idx= -1;
   ret= Xorriso_search_in_hln_array(xorriso, np, &idx, 0);
   if(ret <= 0)
     return(ret);
 }
 for(i= idx - 1; i >= 0 ; i--)
   if(Xorriso__findi_sorted_ino_cmp(&(node_array[i]), &np) != 0)
 break;
 *min_hl= i + 1;
 for(i= idx + 1; i < node_count; i++) 
   if(Xorriso__findi_sorted_ino_cmp(&(node_array[i]), &np) != 0)
 break;
 *max_hl= i - 1;

 /* Search for *node_idx */
 if(flag & 2)
   return(1);
 for(i= *min_hl; i <= *max_hl; i++)
   if(node_array[i] == np) {
     if((flag & 1) && xorriso->hln_targets != NULL && !(flag & 4))
       if(xorriso->hln_targets[i] != NULL)
 continue;
     *node_idx= i;
 break;
   }
 return(1);
}


/* @param flag bit0=do not complain about non existent node */
int Xorriso_node_from_path(struct XorrisO *xorriso, IsoImage *volume,
                           char *path, IsoNode **node, int flag)
{
 int ret;
 char *path_pt;

 path_pt= path;
 if(path[0]==0)
   path_pt= "/";
 if(volume == NULL) {
   ret= Xorriso_get_volume(xorriso, &volume, 0);
   if(ret <= 0)
     return(ret);
 }
 *node= NULL;
 ret= iso_tree_path_to_node(volume, path_pt, node);
 Xorriso_process_msg_queues(xorriso,0);
 if(ret<=0 || (*node)==NULL) {
   if(!(flag&1)) {
     sprintf(xorriso->info_text, "Cannot find path ");
     Text_shellsafe(path_pt, xorriso->info_text, 1);
     strcat(xorriso->info_text, " in loaded ISO image");
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   }
   return(0);
 }
 return(1);
}


int Xorriso_dir_from_path(struct XorrisO *xorriso, char *purpose,
                          char *path, IsoDir **dir_node, int flag)
{
 IsoImage *volume;
 IsoNode *node;
 int ret, is_dir= 0;

 ret= Xorriso_get_volume(xorriso, &volume, 0);
 if(ret<=0)
   return(ret);

 ret= Xorriso_node_from_path(xorriso, volume, path, &node, 0);
 if(ret<=0)
   goto wdi_is_not_a_dir;
 if(LIBISO_ISDIR(node))
   is_dir= 1;
 if(!is_dir) {
wdi_is_not_a_dir:;
   sprintf(xorriso->info_text,
           "%s path does not lead to a directory in ISO image", purpose); 
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   return(0);
 }
 *dir_node= (IsoDir *) node;
 return(1);
}


/*
  @param flag bit0= do not remove leading slash
              bit1= append flatly to result_line and put out
*/
int Xorriso_getfname(struct XorrisO *xorriso, char *path, int flag)
{
 int ret, path_offset= 0, bsl_mem;
 char *bsl_path= NULL;

 if(path[0] == '/' && !(flag & 1))
   path_offset= 1;

 /* backslash escaped path rather than shellsafe path */
 ret= Sfile_bsl_encoder(&bsl_path, path + path_offset,
                        strlen(path + path_offset), 8);
 if(ret <= 0)
   return(-1);
 if(flag & 2) {
   sprintf(xorriso->result_line + strlen(xorriso->result_line),
           "%s\n", bsl_path[0] ? bsl_path : ".");
 } else {
   sprintf(xorriso->result_line, "# file: %s\n", bsl_path[0] ? bsl_path : ".");
 }
 free(bsl_path);
 bsl_path= NULL;
 /* temporarily disable -backslash_codes with result output */
 bsl_mem= xorriso->bsl_interpretation;
 xorriso->bsl_interpretation= 0;
 Xorriso_result(xorriso, 0);
 xorriso->bsl_interpretation= bsl_mem;
 return(1);
}


int Xorriso_is_plain_image_file(struct XorrisO *xorriso, void *in_node,
                                char *path, int flag)
{
 int ret, lba;
 IsoStream *stream;
 IsoNode *node;

 node= (IsoNode *) in_node;
 if(node == NULL) {
   ret= Xorriso_get_node_by_path(xorriso, path, NULL, &node, 0);
   if(ret<=0)
     return(ret);
 }

 ret= Xorriso__file_start_lba(node, &lba, 0);
 if(ret > 0) { /* Stream source is from loaded image */
   stream= iso_file_get_stream((IsoFile *) node);
   if(stream != NULL)
     if(iso_stream_get_input_stream(stream, 0) == NULL)
       return(1);
 }
 return(0);
}

