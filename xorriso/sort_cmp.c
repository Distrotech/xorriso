
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2011 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains functions which sort and compare tree nodes.
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


#include "base_obj.h"
#include "lib_mgt.h"
#include "sort_cmp.h"
#include "iso_tree.h"
#include "iso_manip.h"


int Xorriso__findi_sorted_ino_cmp(const void *p1, const void *p2)
{
 int ret;
 IsoNode *n1, *n2;

 n1= *((IsoNode **) p1);
 n2= *((IsoNode **) p2);

 ret= Xorriso__node_lba_cmp(&n1, &n2);
 if(ret)
   return (ret > 0 ? 1 : -1);
 ret= iso_node_cmp_ino(n1, n2, 0);
 return(ret);
}


/* Not suitable for qsort() but for cross-array comparisons.
   p1 and p2 are actually IsoNode *p1, IsoNode *p2
*/
int Xorriso__hln_cmp(const void *p1, const void *p2)
{
 int ret;

 ret= Xorriso__findi_sorted_ino_cmp(&p1, &p2);
 if(ret)
   return (ret > 0 ? 1 : -1);
 if(p1 != p2)
   return(p1 < p2 ? -1 : 1);
 return(0);
}


/* 
   p1 and p2 are actually IsoNode **p1, IsoNode **p2
*/
int Xorriso__findi_sorted_cmp(const void *p1, const void *p2)
{
 int ret;

 ret= Xorriso__findi_sorted_ino_cmp(p1, p2);
 if(ret)
   return (ret > 0 ? 1 : -1);
 if(p1 != p2)
   return(p1 < p2 ? -1 : 1);
 return(0);
}


int Xorriso_sort_node_array(struct XorrisO *xorriso, int flag)
{
 if(xorriso->node_counter <= 0)
   return(0);
 qsort(xorriso->node_array, xorriso->node_counter, sizeof(IsoNode *),
       Xorriso__findi_sorted_cmp);
 return(1);
}


int Xorriso__search_node(void *node_array[], int n,
                         int (*cmp)(const void *p1, const void *p2),
                         void *node, int *idx, int flag)
{
 int ret, l, r, p, pos;

 if(n == 0)
   return(0);
 l= 0;
 r= n + 1;
 while(1) {
   p= (r - l) / 2;
   if(p == 0)
 break;
   p+= l;

   /* NULL elements may indicate invalid nodes. Their first valid right neigbor
      will serve as proxy. If none exists, then the test pushes leftwards.
    */
   for(pos= p - 1; pos < n; pos++)
     if(node_array[pos] != NULL)
   break;
   if(pos < n)
     ret= (*cmp)(&(node_array[pos]), &node);
   else
     ret= 1;

   if(ret < 0)
     l= p;
   else if(ret > 0)
     r= p;
   else {
     *idx= pos;
     return(1);
   }
 }
 return(0);
}


int Xorriso_search_in_hln_array(struct XorrisO *xorriso,
                                 void *node, int *idx, int flag)
{
 int ret;

 if(xorriso->hln_array == NULL || xorriso->hln_count <= 0)
   return(0);
 ret= Xorriso__search_node(xorriso->hln_array, xorriso->hln_count, 
                           Xorriso__findi_sorted_ino_cmp, node, idx, 0);
 return ret;
}


int Xorriso__get_di(IsoNode *node, dev_t *dev, ino_t *ino, int flag)
{
 int ret, i, i_end, imgid, error_code;
 size_t value_length= 0;
 char *value= NULL, *msg= NULL, severity[80];
 unsigned char *vpt;
 static char *name= "isofs.di";

#ifdef NIX
 /* <<< */
 Xorriso_get_di_counteR++;
#endif /* NIX */

 msg= TSOB_FELD(char, ISO_MSGS_MESSAGE_LEN);
 if(msg == NULL)
   {ret= -1; goto ex;}
 *dev= 0;
 *ino= 0;
 ret= iso_node_lookup_attr(node, name, &value_length, &value, 0);
 if(ret <= 0) {
   /* Drop any pending messages because there is no xorriso to take them */
   iso_obtain_msgs("NEVER", &error_code, &imgid, msg, severity);
   goto ex;
 }
 vpt= (unsigned char *) value;
 for(i= 1; i <= vpt[0] && i < (int) value_length; i++)
   *dev= ((*dev) << 8) | vpt[i];
 i_end= i + vpt[i] + 1;
 for(i++; i < i_end && i < (int) value_length; i++)
   *ino= ((*ino) << 8) | vpt[i];
 free(value);
 ret= 1;
ex:;
 if(msg != NULL)
   free(msg);
 return(ret);
}


int Xorriso__di_ino_cmp(const void *p1, const void *p2)
{  
 int ret; 
 IsoNode *n1, *n2;
 dev_t d1, d2;
 ino_t i1, i2;
 
 n1= *((IsoNode **) p1);
 n2= *((IsoNode **) p2);

 ret= Xorriso__get_di(n1, &d1, &i1, 0);
 if(ret <= 0)
   {d1= 0; i1= 0;}
 ret= Xorriso__get_di(n2, &d2, &i2, 0);
 if(ret <= 0)
   {d2= 0; i2= 0;}

 if(d1 < d2)
   return(-1);
 if(d1 > d2)
   return(1);
 if(i1 < i2)
   return(-1);
 if(i1 > i2)
   return(1);
 if(d1 == 0 && i1 == 0 && n1 != n2)
   return(n1 < n2 ? -1 : 1);
 return(0);
} 


int Xorriso__di_cmp(const void *p1, const void *p2)
{  
 int ret; 
 IsoNode *n1, *n2;

 ret= Xorriso__di_ino_cmp(p1, p2);
 if(ret)
   return(ret);
 n1= *((IsoNode **) p1);
 n2= *((IsoNode **) p2);
 if(n1 != n2)
   return(n1 < n2 ? -1 : 1);
 return(0);
} 


int Xorriso__sort_di(void *node_array[], int count, int flag)
{
 if(count <= 0)
   return(0);
 qsort(node_array, count, sizeof(IsoNode *), Xorriso__di_cmp);
 return(1);
}


int Xorriso_invalidate_di_item(struct XorrisO *xorriso, IsoNode *node,
                               int flag)
{
 int ret, idx;

 if(xorriso->di_array == NULL)
   return(1);
 ret= Xorriso__search_node(xorriso->di_array, xorriso->di_count,
                           Xorriso__di_cmp, node, &idx, 0);
 if(ret <= 0)
   return(ret == 0);
 if(xorriso->di_array[idx] != NULL)
   iso_node_unref(xorriso->di_array[idx]);
 xorriso->di_array[idx]= NULL;
 return(1);
}


/* @param flag bit0= return 1 even if matching nodes were found but node is
                     not among them
               bit1= use Xorriso__di_cmp() rather than Xorriso__di_ino_cmp()
*/
int Xorriso_search_di_range(struct XorrisO *xorriso, IsoNode *node,
                            int *idx, int *low, int *high, int flag)
{
 int ret, i, found;
 int (*cmp)(const void *p1, const void *p2)= Xorriso__di_ino_cmp;

 if(flag & 2)
   cmp= Xorriso__di_cmp;

 *high= *low= *idx= -1;                            
 ret= Xorriso__search_node(xorriso->di_array, xorriso->di_count,
                           cmp, node, &found, 0);
 if(ret <= 0)
   return(0);
 *low= *high= found;
 for(i= found + 1; i < xorriso->di_count; i++)
   if(xorriso->di_array[i] != NULL) {
     if((*cmp)(&node, &(xorriso->di_array[i])) != 0)
 break;
     *high= i;
   }
 for(i= found - 1; i >= 0; i--)
   if(xorriso->di_array[i] != NULL) {
     if((*cmp)(&node, &(xorriso->di_array[i])) != 0)
 break;
     *low= i;
   }
 for(i= *low; i <= *high; i++)
   if(xorriso->di_array[i] == node) {
     *idx= i;
 break;
   }
 return(*idx >= 0 || (flag & 1));
}


int Xorriso__node_lba_cmp(const void *node1, const void *node2)
{
 int ret;
 int lba1= 0, lba2= 0;

 ret= Xorriso__file_start_lba(*((IsoNode **) node1), &lba1, 0);
 if(ret!=1) 
   lba1= 0;
 ret= Xorriso__file_start_lba(*((IsoNode **) node2), &lba2, 0);
 if(ret!=1)
   lba2= 0;
 return(lba1-lba2);  
}


int Xorriso__node_name_cmp(const void *node1, const void *node2)
{
 char *name1, *name2;

 name1= (char *) iso_node_get_name(*((IsoNode **) node1));
 name2= (char *) iso_node_get_name(*((IsoNode **) node2));
 return(strcmp(name1,name2));
}


/* @param flag bit0= only accept directory nodes
               bit1= do not report memory usage as DEBUG
               bit2= do not apply search pattern but accept any node
*/
int Xorriso_sorted_node_array(struct XorrisO *xorriso, 
                              IsoDir *dir_node,
                              int *nodec, IsoNode ***node_array,
                              off_t boss_mem, int flag)
{
 int i, ret, failed_at;
 char *npt;
 IsoDirIter *iter= NULL;
 IsoNode *node;
 off_t mem;

 mem= ((*nodec)+1)*sizeof(IsoNode *);
 ret= Xorriso_check_temp_mem_limit(xorriso, mem+boss_mem, flag&2);
 if(ret<=0)
   return(ret);

 *node_array= calloc(sizeof(IsoNode *), (*nodec)+1);
 if(*node_array==NULL) {
   sprintf(xorriso->info_text,
           "Cannot allocate memory for %d directory entries", *nodec); 
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FATAL", 0);
   return(-1);
 }

 ret= iso_dir_get_children(dir_node, &iter);
 if(ret<0) {
   Xorriso_cannot_create_iter(xorriso, ret, 0);
   return(-1);
 }
 
 for(i= 0; iso_dir_iter_next(iter, &node) == 1 && i<*nodec; ) {
   npt= (char *) iso_node_get_name(node);
   if(!(flag&4)) {
     ret= Xorriso_regexec(xorriso, npt, &failed_at, 0);
     if(ret)
 continue; /* no match */
   }
   if(flag&1)
     if(!LIBISO_ISDIR(node))
 continue;
   (*node_array)[i++]= node;
 }
 iso_dir_iter_free(iter);
 *nodec= i;
 if(*nodec<=0)
   return(1);
 qsort(*node_array, *nodec, sizeof(IsoNode *), Xorriso__node_name_cmp);
 return(1);
}


int Xorriso_remake_hln_array(struct XorrisO *xorriso, int flag)
{  
 int ret, addon_nodes= 0, i, old_count, old_pt, new_pt;
 IsoNode **old_nodes;
 char **old_targets;

 /* Count hln_targets of which the node has been deleted meanwhile */
 for(i= 0; i < xorriso->hln_count; i++) {
   if(xorriso->hln_targets[i] == NULL)
 continue;
   if(Xorriso_node_is_valid(xorriso, xorriso->hln_array[i], 0))
 continue;
   addon_nodes++;
 }
 ret= Xorriso_all_node_array(xorriso, addon_nodes, 0);
 if(ret <= 0)
   goto ex;
 if(addon_nodes > 0) {
   /* Transfer delete nodes with hln_target to node array */
   for(i= 0; i < xorriso->hln_count; i++) {
     if(xorriso->hln_targets[i] == NULL)
   continue;
     if(Xorriso_node_is_valid(xorriso, xorriso->hln_array[i], 0))
   continue;
     if(xorriso->node_counter < xorriso->node_array_size) {
       xorriso->node_array[xorriso->node_counter++]= xorriso->hln_array[i];
       iso_node_ref(xorriso->node_array[xorriso->node_counter - 1]);
     }
   }
 }

 Xorriso_sort_node_array(xorriso, 0);
 old_nodes= (IsoNode **) xorriso->hln_array;
 old_targets= (char **) xorriso->hln_targets;
 old_count= xorriso->hln_count;
 xorriso->hln_array= 0;
 xorriso->hln_targets= NULL;

 /* Transfer node_array to di_array without unrefering nodes */
 xorriso->hln_count= xorriso->node_counter;
 xorriso->hln_array= xorriso->node_array;
 xorriso->node_counter= 0;
 xorriso->node_array_size= 0;
 xorriso->node_array= NULL;

 /* Allocate hln_targets */
 ret= Xorriso_new_hln_array(xorriso, xorriso->temp_mem_limit, 1);
 if(ret<=0)
   goto ex;
 xorriso->node_targets_availmem= xorriso->temp_mem_limit;
 if(old_targets != NULL) {
   /* Transfer targets from old target array */;
   new_pt= old_pt= 0;
   while(new_pt < xorriso->hln_count && old_pt < old_count) {
     ret= Xorriso__hln_cmp(xorriso->hln_array[new_pt], old_nodes[old_pt]);
     if(ret < 0) {
       new_pt++;
     } else if(ret > 0) {
       old_pt++;
     } else {
       xorriso->hln_targets[new_pt]= old_targets[old_pt];
       if(old_targets[old_pt] != NULL)
         xorriso->temp_mem_limit-= strlen(old_targets[old_pt]) + 1;
       old_targets[old_pt]= NULL;
       new_pt++;
       old_pt++;
     }
   }
   for(old_pt= 0; old_pt < old_count; old_pt++)
     if(old_targets[old_pt] != NULL) /* (should not happen) */
       free(old_targets[old_pt]);
   free((char *) old_targets);
 }
 if(old_nodes != NULL) {
   for(old_pt= 0; old_pt < old_count; old_pt++)
     if(old_nodes[old_pt] != NULL)
       iso_node_unref(old_nodes[old_pt]);
   free((char *) old_nodes);
 }
 xorriso->hln_change_pending= 0;
 ret= 1;
ex:;
 return(ret);
}


/* @param flag bit0= overwrite existing hln_array (else return 2)
*/
int Xorriso_make_hln_array(struct XorrisO *xorriso, int flag)
{  
 int ret;

 if(xorriso->hln_array != NULL && !(flag & 1)) {
   /* If no fresh image manipulations occured: keep old array */
   if(!xorriso->hln_change_pending)
     return(2);
   ret= Xorriso_remake_hln_array(xorriso, 0);
   return(ret);
 }
 Xorriso_destroy_hln_array(xorriso, 0);

 ret= Xorriso_all_node_array(xorriso, 0, 0);
 if(ret <= 0)
   goto ex;
 Xorriso_sort_node_array(xorriso, 0);

 /* Transfer node_array to di_array without unrefering nodes */
 xorriso->hln_count= xorriso->node_counter;
 xorriso->hln_array= xorriso->node_array;
 xorriso->node_counter= 0;
 xorriso->node_array_size= 0;
 xorriso->node_array= NULL;

 /* Allocate hln_targets */
 ret= Xorriso_new_hln_array(xorriso, xorriso->temp_mem_limit, 1);
 if(ret<=0) {
   Xorriso_destroy_hln_array(xorriso, 0);
   goto ex;
 }
 xorriso->node_targets_availmem= xorriso->temp_mem_limit;
 xorriso->hln_change_pending= 0;
 ret= 1;
ex:;
 return(ret);
}


/* @param flag bit0= overwrite existing di_array (else return 2)
               bit1= make di_array despite xorriso->ino_behavior bit 3
*/
int Xorriso_make_di_array(struct XorrisO *xorriso, int flag)
{  
 int ret, bytes; 

#ifdef NIX
 /* <<< */
 unsigned long old_gdic;
 old_gdic= Xorriso_get_di_counteR;
#endif /* NIX */

 if((xorriso->ino_behavior & 8 ) && !(flag & 2))
   return(2);
 if(xorriso->di_array != NULL && !(flag & 1))
   return(2);
 Xorriso_finish_hl_update(xorriso, 0);

 ret= Xorriso_all_node_array(xorriso, 0, 0);
 if(ret <= 0)
   goto ex;
 bytes= xorriso->node_array_size / 8 + 1;
 xorriso->di_do_widen= calloc(bytes, 1);
 if(xorriso->di_do_widen == NULL) {
   Xorriso_no_malloc_memory(xorriso, NULL, 0);
   ret= -1; goto ex;
 }

 /* Transfer node_array to di_array without unrefering nodes */
 xorriso->di_count= xorriso->node_counter;
 xorriso->di_array= xorriso->node_array;
 xorriso->node_counter= 0;
 xorriso->node_array_size= 0;
 xorriso->node_array= NULL;

 Xorriso__sort_di((void *) xorriso->di_array, xorriso->di_count, 0);

 ret= 1;
ex:;

#ifdef NIX
/* <<< */
 fprintf(stderr, "xorriso_DEBUG: sort_count= %lu\n",
         Xorriso_get_di_counteR - old_gdic);
#endif /* NIX */

 return(ret);
}


/*
   @param flag  bit0= iso_rr_path is freshly added and up to date
                bit1= do not mark as changed content (implied by bit0 too)
                bit2= -follow: this is not a command parameter
   @return -1= severe error
            0= not applicable for hard links
            1= go on with processing
            2= iso_rr_path is fully updated
 */
int Xorriso_hardlink_update(struct XorrisO *xorriso, int *compare_result,
                            char *disk_path, char *iso_rr_path, int flag)
{
 int ret, hret, idx, low, high, i, do_overwrite= 0, did_fake_di= 0;
 int follow_links, old_idx= -1;
 IsoNode *node;
 struct stat stbuf;
 dev_t old_dev;
 ino_t old_ino;

 if(xorriso->di_array == NULL)
   return(1);
 follow_links= xorriso->do_follow_links ||
               (xorriso->do_follow_param && !(flag & 4));
 ret= Xorriso_node_from_path(xorriso, NULL, iso_rr_path, &node, 0);
 if(ret <= 0)
   return(ret);
 if(LIBISO_ISDIR(node))
   return(1);

 /* Handle eventual hardlink split : */
 /* This is achieved by setting the content change bit. Reason:
    The node needs to be removed from di_array because its di is
    not matching its array index any more. So it becomes invisible for
    the join check of eventual later hardlink siblings. Therefore
    it must be updated now, even if it has currently no siblings
    which it leaves or which it joins.
 */
 if(!(flag & (1 | 2)))
   do_overwrite= 1;

 Xorriso__get_di(node, &old_dev, &old_ino, 0);
 ret= Xorriso__search_node(xorriso->di_array, xorriso->di_count,
                           Xorriso__di_cmp, node, &idx, 0);
 if(ret < 0)
   {ret= 0; goto ex;}
 if(ret > 0)
   old_idx= idx;

 /* Handle eventual hardlink joining : */

 if(follow_links)
   ret= stat(disk_path, &stbuf);
 else
   ret= lstat(disk_path, &stbuf);
 if(ret==-1)
   {ret= 0; goto ex;}

 /* Are there new dev-ino-siblings in the image ? */
 /* Fake isofs.di */
 if(!(flag & 1)) {
   ret= Xorriso_record_dev_inode(xorriso, disk_path, stbuf.st_dev,
                                 stbuf.st_ino, node, iso_rr_path, 1);
   if(ret <= 0)
     {ret= -1; goto ex;}
   did_fake_di= 1;
   /* temporarily remove node from di_array so it does not disturb 
       search by its fake di info */;
   if(old_idx >= 0)
     xorriso->di_array[old_idx]= NULL;
 }
 ret= Xorriso_search_di_range(xorriso, node, &idx, &low, &high, 1);
 if(did_fake_di) {
   /* Revoke fake of isofs.di */
   hret= Xorriso_record_dev_inode(xorriso, disk_path, old_dev, old_ino,
                                  node, iso_rr_path, 1);
   if(hret <= 0)
     {ret= -1; goto ex;}
   if(old_idx >= 0)
     xorriso->di_array[old_idx]= node;
 }
 if(ret == 0)
   {ret= 1; goto ex;}
 if(ret < 0)
   {ret= 0; goto ex;}


#ifdef Xorriso_hardlink_update_debuG
 /* <<< */
 if(low < high || idx < 0) {
   fprintf(stderr,
   "xorriso_DEBUG: old_idx= %d , low= %d , high= %d , iso= '%s' , disk='%s'\n",
   old_idx, low, high, iso_rr_path, disk_path);
   fprintf(stderr,
         "xorriso_DEBUG: old_dev= %lu , old_ino= %lu ,  dev= %lu , ino= %lu\n",
         (unsigned long) old_dev, (unsigned long) old_ino,
         (unsigned long) stbuf.st_dev, (unsigned long) stbuf.st_ino);

   if(idx >= 0 && idx != old_idx)
     fprintf(stderr, "xorriso_DEBUG: idx= %d , old_idx = %d\n", idx, old_idx);
 }
#endif /* Xorriso_hardlink_update_debuG */

 /* Overwrite all valid siblings : */
 for(i= low; i <= high; i++) {
   if(i == idx || xorriso->di_array[i] == NULL)
 continue;

#ifdef Xorriso_hardlink_update_debuG
 /* <<< */
{
 ino_t ino;
 dev_t dev;

 Xorriso__get_di(xorriso->di_array[i], &dev, &ino, 0);
 fprintf(stderr, "xorriso_DEBUG: iso_sibling= '%s' , dev= %lu , ino= %lu\n",
         node_path, (unsigned long) dev, (unsigned long) ino);
}
#endif /* Xorriso_hardlink_update_debuG */

   xorriso->di_do_widen[i / 8]|= 1 << (i % 8);
 }

 ret= 1;
ex:;
 if(do_overwrite)
   *compare_result|= (1<<15);
 if(old_idx >= 0 && (*compare_result & (3 << 21))) {
   /* The old di info is obsolete */
   if(xorriso->di_array[old_idx] != NULL)
     iso_node_unref(xorriso->di_array[old_idx]);
   xorriso->di_array[old_idx]= NULL;
 }
 return(ret);
}


/* @param flag bit0= do not destroy di_array
*/
int Xorriso_finish_hl_update(struct XorrisO *xorriso, int flag)
{
 int ret, zero= 0;
 char *argv[4];
 struct Xorriso_lsT *disk_lst, *iso_lst;

 if(xorriso->di_array == NULL)
   {ret= 1; goto ex;}
 disk_lst= xorriso->di_disk_paths;
 iso_lst= xorriso->di_iso_paths;
 while(disk_lst != NULL && iso_lst != NULL) {
   argv[0]= Xorriso_lst_get_text(iso_lst, 0);
   argv[1]= "-exec";
   argv[2]= "widen_hardlinks";
   argv[3]= Xorriso_lst_get_text(disk_lst, 0);
   zero= 0;
   ret= Xorriso_option_find(xorriso, 4, argv, &zero, 0); /* -findi */
   if(ret < 0)
     goto ex;
   disk_lst= Xorriso_lst_get_next(disk_lst, 0);
   iso_lst= Xorriso_lst_get_next(iso_lst, 0);
 }
 ret= 1;
ex:;
 if(!(flag & 1))
   Xorriso_destroy_di_array(xorriso, 0);
 return(ret);
}

