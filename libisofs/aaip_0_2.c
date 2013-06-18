
/*

 Arbitrary Attribute Interchange Protocol , AAIP versions 0.2 , 1.0 , 2.0.
 Implementation of encoding and decoding xattr and ACL.

 See libisofs/aaip_0_2.h
     http://libburnia-project.org/wiki/AAIP

 Copyright (c) 2009 - 2011 Thomas Schmitt, libburnia project, GPLv2+

*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>

#include "libisofs.h"
#include "util.h"

/*
#define Aaip_encode_debuG 1
*/

#include "aaip_0_2.h"

#define Aaip_EXEC        1
#define Aaip_WRITE       2
#define Aaip_READ        4

#define Aaip_TRANSLATE       0
#define Aaip_ACL_USER_OBJ    1
#define Aaip_ACL_USER        2 
#define Aaip_ACL_GROUP_OBJ   3
#define Aaip_ACL_GROUP       4
#define Aaip_ACL_MASK        5
#define Aaip_ACL_OTHER       6
#define Aaip_SWITCH_MARK     8
#define Aaip_ACL_USER_N     10
#define Aaip_ACL_GROUP_N    12
#define Aaip_FUTURE_VERSION 15

#define Aaip_with_short_namespaceS yes
#define Aaip_max_named_spacE    0x06
#define Aaip_min_named_spacE    0x02
#define Aaip_maxdef_namespacE   0x1f

#define Aaip_namespace_literaL   0x01
#define Aaip_namespace_systeM    0x02
#define Aaip_namespace_useR      0x03
#define Aaip_namespace_isofS     0x04
#define Aaip_namespace_trusteD   0x05
#define Aaip_namespace_securitY  0x06

static char Aaip_namespace_textS[][16]= {"", "", "system.", "user.", "isofs.",
                                         "trusted.", "security."};

/* maximum expansion:  "security." */
#define Aaip_max_name_expansioN  9

/* --------------------------------- Encoder ---------------------------- */


static int aaip_encode_pair(char *name, size_t attr_length, char *attr,
                            unsigned int *num_recs, size_t *comp_size,
                            unsigned char *result, size_t result_fill,
                            int flag);


/* Convert an array of Arbitrary Attributes into a series of AAIP fields.
   @param num_attrs     Number of attributes
   @param names         Array of pointers to 0 terminated name strings
   @param value_lengths Array of byte lengths for each value
   @param values        Array of pointers to the value bytes
   @param result_len    Number of bytes in the resulting SUSP field string
   @param result        *result will point to the start of the result string.
                        This is malloc() memory which needs to be freed when
                        no longer needed 
   @param flag          Bitfield for control purposes
                        bit0= set CONTINUE bit of last AAIP field to 1
   @return              >0 is the number of SUSP fields generated,
                        0 means error 
*/
size_t aaip_encode(size_t num_attrs, char **names,
                   size_t *value_lengths, char **values, 
                   size_t *result_len, unsigned char **result, int flag)
{
 size_t mem_size= 0, comp_size, ret;
 unsigned int number_of_fields, i, num_recs;

 /* Predict memory needs, number of SUSP fields and component records */
 *result_len= 0;
 for(i= 0; i < num_attrs; i++) {
   ret= aaip_encode_pair(names[i], value_lengths[i], values[i],
                         &num_recs, &comp_size, NULL, (size_t) 0, 1);
   if(ret <= 0)
     return(ret);
   mem_size+= comp_size;
 }
 number_of_fields= mem_size / 250 + !!(mem_size % 250);
 mem_size+= number_of_fields * 5;

#ifdef Aaip_encode_debuG
 *result= (unsigned char *) calloc(1, mem_size + 1024000);
                                          /* generous honeypot for overflows */
#else
 *result= (unsigned char *) calloc(1, mem_size);
#endif

 if(*result == NULL)
   return 0;

 /* Encode pairs into result */
 for(i= 0; i < num_attrs; i++) {
   ret= aaip_encode_pair(names[i], value_lengths[i], values[i],
                         &num_recs, &comp_size, *result, *result_len, 0);
   if(ret <= 0)
     return(ret);
   (*result_len)+= comp_size;
 }

 /* write the field headers */
 for(i= 0; i < number_of_fields; i++) {
   (*result)[i * 255 + 0]= 'A';
   (*result)[i * 255 + 1]= 'L';
   if(i < number_of_fields - 1 || (mem_size % 255) == 0)
     (*result)[i * 255 + 2]= 255;
   else 
     (*result)[i * 255 + 2]= mem_size % 255;
   (*result)[i * 255 + 3]= 1;
   (*result)[i * 255 + 4]= (flag & 1) || (i < number_of_fields - 1);
 }
 (*result_len)+= number_of_fields * 5;

#ifdef Aaip_encode_debuG
 if(*result_len != mem_size) {
   fprintf(stderr, "aaip_encode(): MEMORY MISMATCH BY %d BYTES\n",
           (int) (mem_size - *result_len));
 } else {
   unsigned char *hpt;
   hpt= malloc(*result_len);
   if(hpt != NULL) {
     memcpy(hpt, *result, *result_len);
     free(*result);
     *result= hpt;
   }
 }
 ret= 0;
 for(i= 0; i < *result_len; i+= ((unsigned char *) (*result))[i + 2])
   ret++;
 if(ret != (int) number_of_fields) {
   fprintf(stderr, "aaip_encode(): WRONG NUMBER OF FIELDS %d <> %d\n",
           (int) number_of_fields, ret);
 }
#endif /* Aaip_encode_debuG */

 return(number_of_fields);
}


static void aaip_encode_byte(unsigned char *result, size_t *result_fill,
                            unsigned char value)
{
 result[(*result_fill / 250) * 255 + 5 + (*result_fill % 250)]= value;
 (*result_fill)++;
}


static int aaip_encode_comp(unsigned char *result, size_t *result_fill,
                            int prefix, char *data, size_t l, int flag)
{
 size_t todo;
 char *rpt, *comp_start;

 if(l == 0 && prefix <= 0) {
   aaip_encode_byte(result, result_fill, 0);
   aaip_encode_byte(result, result_fill, 0);
   return(1);
 }
 for(rpt= data; rpt - data < (ssize_t) l;) {
   todo= l - (rpt - data) + (prefix > 0);
   aaip_encode_byte(result, result_fill, (todo > 255));
   if(todo > 255)
     todo= 255;
   aaip_encode_byte(result, result_fill, todo);
   if(prefix > 0) {
     aaip_encode_byte(result, result_fill, prefix);
     todo--;
     prefix= 0;
   }
   for(comp_start= rpt; rpt - comp_start < (ssize_t) todo; rpt++)
     aaip_encode_byte(result, result_fill, *((unsigned char *) rpt));
 }
 return(1);
}


/* Write the component records for name and attr. Skip the positions of
   AAIP field headers.
   @param flag          bit0= only count but do not produce result
*/
static int aaip_encode_pair(char *name, size_t attr_length, char *attr,
                            unsigned int *num_recs, size_t *comp_size,
                            unsigned char *result, size_t result_fill,
                            int flag)
{
 size_t l;
 int i, prefix= 0;

#ifdef Aaip_with_short_namespaceS

 /* translate name into eventual short form */
 for(i= Aaip_min_named_spacE; i <= Aaip_max_named_spacE; i++)
   if(strncmp(name, Aaip_namespace_textS[i], strlen(Aaip_namespace_textS[i]))
      == 0) {
     name+= strlen(Aaip_namespace_textS[i]);
     prefix= i;
   }
 /* Eventually prepend escape marker for strange names */
 if(prefix <= 0 && name[0] > 0 && name[0] <= Aaip_maxdef_namespacE)
   prefix= Aaip_namespace_literaL;

#endif /* Aaip_with_short_namespaceS */

 l= strlen(name) + (prefix > 0);
 *num_recs= l / 255 + (!!(l % 255)) + (l == 0) +
            attr_length / 255 + (!!(attr_length % 255)) + (attr_length == 0);
 *comp_size= l + attr_length + 2 * *num_recs;

 if(flag & 1)
   return(1);

 aaip_encode_comp(result, &result_fill, prefix, name, l - (prefix > 0), 0);
 aaip_encode_comp(result, &result_fill, 0, attr, attr_length, 0);
 return(1);
}


/* ----------- Encoder for ACLs ----------- */

static ssize_t aaip_encode_acl_text(char *acl_text, mode_t st_mode,
                          size_t result_size, unsigned char *result, int flag);


/* Convert an ACL text as of acl_to_text(3) into the value of an Arbitrary
   Attribute. According to AAIP this value is to be stored together with
   an empty name.
   @param acl_text      The ACL in long text form
   @param st_mode       The stat(2) permission bits to be used with flag bit3
   @param result_len    Number of bytes in the resulting value
   @param result        *result will point to the start of the result string.
                        This is malloc() memory which needs to be freed when
                        no longer needed 
   @param flag          Bitfield for control purposes
                        bit0= count only
                        bit1= use numeric qualifiers rather than names
                        bit2= this is a default ACL, prepend SWITCH_MARK
                        bit3= check for completeness of list and eventually
                              fill up with entries deduced from st_mode
   @return              >0 means ok
                        <=0 means error 
                        -1= out of memory
                        -2= program error with prediction of result size
                        -3= error with conversion of name to uid or gid 
     ISO_AAIP_ACL_MULT_OBJ= multiple entries of user::, group::, other::
*/
int aaip_encode_acl(char *acl_text, mode_t st_mode,
                    size_t *result_len, unsigned char **result, int flag)
{
 ssize_t bytes;

 *result= NULL;
 *result_len= 0;
 bytes= aaip_encode_acl_text(acl_text, st_mode,
                             (size_t) 0, NULL, 1 | (flag & (2 | 4 | 8)));
 if(bytes < -2)
   return(bytes);
 if(bytes < 0)
   return((int) bytes - 1);
 if(flag & 1) {
   *result_len= bytes;
   return(1);
 }
 *result= calloc(bytes + 1, 1);
 if(*result == NULL)
   return(-1);
 (*result)[bytes]= 0;
 *result_len= bytes;
 bytes= aaip_encode_acl_text(acl_text, st_mode, *result_len, *result,
                             (flag & (2 | 4 | 8)));
 if(bytes < -2)
   return(bytes);
 if(bytes < 0)
   return((int) bytes - 1);
 if((size_t) bytes != *result_len) {
   *result_len= 0;
   return(-2);
 }
 return(1);
}


static double aaip_numeric_id(char *name, int flag)
{
 double num;
 char *cpt;

 for(cpt= name; *cpt != 0; cpt++)
   if(*cpt < '0' || *cpt >'9')
 break;
 if(*cpt != 0)
   return(-1);
 sscanf(name, "%lf", &num);
 return(num);
}


static int aaip_make_aaip_perms(int r, int w, int x)
{
 int perms;

 perms= 0;
 if(r)
   perms|= Aaip_READ;
 if(w)
   perms|= Aaip_WRITE;
 if(x)
   perms|= Aaip_EXEC;
 return(perms);
}


/*
   @param result_size   Number of bytes to store result
   @param result        Pointer to the start of the result string.
   @param flag          Bitfield for control purposes
                        bit0= count only, do not really produce bytes
                        bit1= use numeric qualifiers
                        bit2= this is a default ACL, prepend SWITCH_MARK 1
                        bit3= check for completeness of list and eventually
                              fill up with entries deduced from st_mode
   @return              >=0 number of bytes produced resp. counted
                        <0 means error 
                        -1: result size overflow
                        -2: conversion errror with user name or group name
     ISO_AAIP_ACL_MULT_OBJ: multiple entries of user::, group::, other::
*/
static ssize_t aaip_encode_acl_text(char *acl_text, mode_t st_mode,
                           size_t result_size, unsigned char *result, int flag)
{
 char *rpt, *npt, *cpt;
 int qualifier= 0, perms, type, i, qualifier_len= 0, num_recs, needed= 0, ret;
 unsigned int has_u= 0, has_g= 0, has_o= 0, has_m= 0, is_trivial= 1;
 uid_t uid, huid;
 gid_t gid, hgid;
 ssize_t count= 0;
 struct passwd *pwd;
 struct group *grp;
 char *name = NULL;
 int name_size= 1024;
 double num;

 LIBISO_ALLOC_MEM(name, char, name_size);
 if(flag & 4) {
   /* set SWITCH_MARK to indicate a default ACL */;
   if(!(flag & 1)) {
     if((size_t) count >= result_size)
       {ret= -1; goto ex;}
     result[count]= (Aaip_SWITCH_MARK << 4) | Aaip_EXEC;
   }
   count++;
 }

 for(rpt= acl_text; *rpt != 0; rpt= npt) {
   npt= strchr(rpt, '\n');
   if(npt == 0)
     npt= rpt + strlen(rpt);
   else
     npt++;
   if(*rpt == '#')
 continue;
   cpt= strchr(rpt, ':');
   if(cpt == NULL)
 continue;
   cpt= strchr(cpt + 1, ':');
   if(cpt == NULL)
 continue;
   qualifier= 0;
   if(strncmp(rpt, "user:", 5) == 0) {
     if(cpt - rpt == 5) {
       type= Aaip_ACL_USER_OBJ;
       if (has_u) {

         /* >>> Duplicate u:: entry. */;
         /* >>> ??? If it matches the previous one: ignore */

         return((int) ISO_AAIP_ACL_MULT_OBJ);
       }
       has_u++;
     } else {
       if(cpt - (rpt + 5) >= name_size)
 continue;
       is_trivial= 0;
       strncpy(name, rpt + 5, cpt - (rpt + 5)); 
       name[cpt - (rpt + 5)]= 0;
       if(flag & 2) {
         type= Aaip_ACL_USER_N;
         pwd= getpwnam(name);
         if(pwd == NULL) {
           num= aaip_numeric_id(name, 0);
           if(num <= 0) {
             /* ACL_USER is not part of AAIP 2.0 */
             {ret= -2; goto ex;}
           }
           uid= huid= num;
         } else
           uid= huid= pwd->pw_uid;
         /* Convert uid into Qualifier Record */
         for(i= 0; huid != 0; i++)
           huid= huid >> 8;
         qualifier_len= i;
         if(qualifier_len <= 0)
           qualifier_len= 1;
         for(i= 0; i < qualifier_len ; i++)
           name[i]= uid >> (8 * (qualifier_len - i - 1));
       } else {
         type= Aaip_ACL_USER;
         qualifier_len= strlen(name);
         if(qualifier_len <= 0)
           qualifier_len= 1;
       }
       qualifier= 1;
     }
   } else if(strncmp(rpt, "group:", 6) == 0) {
     if(cpt - rpt == 6) {
       type= Aaip_ACL_GROUP_OBJ;
       if (has_g) {

         /* >>> Duplicate g:: entry. */;
         /* >>> ??? If it matches the previous one: ignore */

         return((int) ISO_AAIP_ACL_MULT_OBJ);
       }
       has_g++;
     } else {
       if(cpt - (rpt + 6) >= name_size)
 continue;
       is_trivial= 0;
       strncpy(name, rpt + 6, cpt - (rpt + 6)); 
       name[cpt - (rpt + 6)]= 0;
       if(flag & 2) {
         type= Aaip_ACL_GROUP_N;
         grp= getgrnam(name);
         if(grp == NULL) {
           num= aaip_numeric_id(name, 0);
           if(num <= 0) {
             /* ACL_GROUP is not part of AAIP 2.0 */
             {ret= -2; goto ex;}
           }
           gid= hgid= num;
         } else
           gid= hgid= grp->gr_gid;
         /* Convert gid into Qualifier Record */
         for(i= 0; hgid != 0; i++)
           hgid= hgid >> 8;
         qualifier_len= i;
         if(qualifier_len <= 0)
           qualifier_len= 1;
         for(i= 0; i < qualifier_len ; i++)
           name[i]= gid >> (8 * (qualifier_len - i - 1));
       } else {
         type= Aaip_ACL_GROUP;
         qualifier_len= strlen(name);
         if(qualifier_len <= 0)
           qualifier_len= 1;
       }
       qualifier= 1;
     }
   } else if(strncmp(rpt, "other:", 6) == 0) {
     type= Aaip_ACL_OTHER;
     if (has_o) {

       /* >>> Duplicate o:: entry. */;
       /* >>> ??? If it matches the previous one: ignore */

       return((int) ISO_AAIP_ACL_MULT_OBJ);
     }
     has_o++;
   } else if(strncmp(rpt, "mask:", 5) == 0) {
     type= Aaip_ACL_MASK;
     has_m++;
   } else
 continue;

   if(npt - cpt < 4)
 continue;
   perms= aaip_make_aaip_perms(cpt[1] == 'r', cpt[2] == 'w', cpt[3] == 'x');

   if(!(flag & 1)) {
     if((size_t) count >= result_size)
       {ret= -1; goto ex;}
     result[count]= perms | ((!!qualifier) << 3) | (type << 4);
   }
   count++;

   if(qualifier) {
     num_recs= (qualifier_len / 127) + !!(qualifier_len % 127);
     if(!(flag & 1)) {
       if((size_t) (count + 1) > result_size)
         {ret= -1; goto ex;}
       for(i= 0; i < num_recs; i++) {
         if(i < num_recs - 1)
           result[count++]= 255;
         else {
           result[count++]= (qualifier_len % 127);
           if(result[count - 1] == 0)
             result[count - 1]= 127;
         }
         if((size_t) (count + (result[count - 1] & 127)) > result_size)
           {ret= -1; goto ex;}
         memcpy(result + count, name + i * 127, result[count - 1] & 127);
         count+= result[count - 1] & 127;
       }
     } else
       count+= qualifier_len + num_recs;
   }
 }
 if (flag & 8) {
   /* add eventually missing mandatory ACL entries */
   needed= (!has_u) + (!has_g) + (!has_o) + !(is_trivial || has_m);
   if(flag & 1)
     count+= needed;
   else {
     if((size_t) (count + needed) > result_size)
       {ret= -1; goto ex;}
   }
 }
 if ((flag & 8) && needed > 0 && !(flag & 1)) {
   if(!has_u) {
     perms= aaip_make_aaip_perms(st_mode & S_IRUSR, st_mode & S_IWUSR,
                                 st_mode * S_IXUSR);
     result[count++]= perms | (Aaip_ACL_USER_OBJ << 4);
   }
   if(!has_g) {
     perms= aaip_make_aaip_perms(st_mode & S_IRGRP, st_mode & S_IWGRP,
                                 st_mode * S_IXGRP);
     result[count++]= perms | (Aaip_ACL_GROUP_OBJ << 4);
   }
   if(!has_o) {
     perms= aaip_make_aaip_perms(st_mode & S_IROTH, st_mode & S_IWOTH,
                                 st_mode * S_IXOTH);
     result[count++]= perms | (Aaip_ACL_OTHER << 4);
   }
   if(!(is_trivial | has_m)) {
     perms= aaip_make_aaip_perms(st_mode & S_IRGRP, st_mode & S_IWGRP,
                                 st_mode * S_IXGRP);
     result[count++]= perms | (Aaip_ACL_MASK << 4);
   }
 }
 ret= count;
ex:;
 LIBISO_FREE_MEM(name);
 return(ret);
}


int aaip_encode_both_acl(char *a_acl_text, char *d_acl_text, mode_t st_mode,
                         size_t *result_len, unsigned char **result, int flag)
{
 int ret;
 size_t a_acl_len= 0, d_acl_len= 0, acl_len= 0;
 unsigned char *a_acl= NULL, *d_acl= NULL, *acl= NULL;

 if(a_acl_text != NULL) {
   ret= aaip_encode_acl(a_acl_text, st_mode, &a_acl_len, &a_acl, flag & 11);
   if(ret <= 0)
     goto ex;
 }
 if(d_acl_text != NULL) {
   ret= aaip_encode_acl(d_acl_text, (mode_t) 0, &d_acl_len, &d_acl,
                        (flag & 3) | 4);
   if(ret <= 0)
     goto ex;
 }
 if(a_acl == NULL || a_acl_len == 0) {
   acl= d_acl;
   d_acl= NULL;
   acl_len= d_acl_len;
 } else if (d_acl == NULL || d_acl_len == 0) {
   acl= a_acl; 
   a_acl= NULL;
   acl_len= a_acl_len;
 } else {
   acl= calloc(a_acl_len + d_acl_len, 1);
   if(acl == NULL)
     {ret = -1; goto ex;}
   memcpy(acl, a_acl, a_acl_len);
   memcpy(acl + a_acl_len, d_acl, d_acl_len);
   acl_len= a_acl_len + d_acl_len;
 }
 *result= acl;
 *result_len= acl_len;
 ret= 1;
ex:;
 if(a_acl != NULL)
   free(a_acl);
 if(d_acl != NULL)
   free(d_acl);
 return(ret);
}


/* GNU/Linux man 5 acl says:
     The permissions defined by ACLs are a superset of the permissions speci-
     fied by the file permission bits. The permissions defined for the file
     owner correspond to the permissions of the ACL_USER_OBJ entry.  The per-
     missions defined for the file group correspond to the permissions of the
     ACL_GROUP_OBJ entry, if the ACL has no ACL_MASK entry. If the ACL has an
     ACL_MASK entry, then the permissions defined for the file group corre-
     spond to the permissions of the ACL_MASK entry. The permissions defined
     for the other class correspond to the permissions of the ACL_OTHER_OBJ
     entry.

     Modification of the file permission bits results in the modification of
     the permissions in the associated ACL entries. Modification of the per-
     missions in the ACL entries results in the modification of the file per-
     mission bits.

*/
/* Analyze occurence of ACL tag types in long text form. If not disabled by
   parameter flag remove the entries of type "user::" , "group::" , "other::" ,
   or "other:" from an ACL in long text form if they match the bits in st_mode
   as described by man 2 stat and man 5 acl.
   @param acl_text   The text to be analyzed and eventually shortened.
   @param st_mode    The component of struct stat which tells POSIX permission
                     bits and eventually shall take equivalent bits as read
                     from the ACL. The caller should submit a pointer
                     to the st_mode variable which holds permissions as
                     indicated by stat(2) resp. ECMA-119 and RRIP data.
   @param flag       bit0= do not remove entries, only determine return value
                     bit1= like bit0 but return immediately if a non-st_mode
                           ACL entry is found
                     bit2= update *st_mode by acl_text
                           ("user::" -> S_IRWXU, "mask::"|"group::" -> S_IRWXG,
                            "other::" -> S_IRWXO)
                     bit3= update acl_text by *st_mode (same mapping as bit 2
                           but with reversed transfer direction)
                     bit4= map "group::" <-> S_IRWXG in any case.
                           I.e. ignore "mask::".
   @return           <0  failure
                     >=0 tells in its bits which tag types were found.
                         The first three tell which types deviate from the
                         corresponding st_mode settings:
                         bit0= "other::" overrides S_IRWXO
                         bit1= "group::" overrides S_IRWXG (no "mask::" found)
                         bit2= "user::"  overrides S_IRWXU
                         The second three tell which types comply with st_mode:
                         bit3= "other::" matches S_IRWXO
                         bit4= "group::" matches S_IRWXG (no "mask::" found)
                         bit5= "user::"  matches S_IRWXU
                         Given the nature of ACLs nearly all combinations are
                         possible although some would come from invalid ACLs.
                         bit6= other ACL tag types are present. Particularly:
                               bit7= "user:...:" is present
                               bit8= "group:...:" is present
                               bit9= "mask::" is present
*/
int aaip_cleanout_st_mode(char *acl_text, mode_t *in_st_mode, int flag)
{
 char *rpt, *wpt, *npt, *cpt;
 mode_t m, list_mode, st_mode;
 int tag_types= 0, has_mask= 0, do_cleanout = 0;

 list_mode= st_mode= *in_st_mode;
 do_cleanout = !(flag & 15);

 has_mask= strncmp(acl_text, "mask:", 5) == 0 ||
           strstr(acl_text, "\nmask:") != NULL;
 if(has_mask && (flag & 2))
   return(64 | 512);

 for(npt= wpt= rpt= acl_text; *npt != 0; rpt= npt + 1) {
   npt= strchr(rpt, '\n');
   if(npt == NULL)
     npt= rpt + strlen(rpt);
   if(strncmp(rpt, "user:", 5) == 0) {
     if(rpt[5] == ':' && npt - rpt == 9) {
       cpt= rpt + 6;
       m= 0;
       if(cpt[0] == 'r')
         m|= S_IRUSR;
       if(cpt[1] == 'w')
         m|= S_IWUSR;
       if(cpt[2] == 'x')
         m|= S_IXUSR;
       list_mode= (list_mode & ~S_IRWXU) | m;
       if((st_mode & S_IRWXU) == (m & S_IRWXU)) {
         tag_types|= 32;
 continue;
       }
       if(flag & 8) {
         cpt[0]= st_mode & S_IRUSR ? 'r' : '-';
         cpt[1]= st_mode & S_IWUSR ? 'w' : '-';
         cpt[2]= st_mode & S_IXUSR ? 'x' : '-';
       }
       tag_types|= 4;
     } else {
       tag_types|= 64 | 128;
     }
   } else if(strncmp(rpt, "group:", 6) == 0) {
     if(rpt[6] == ':' && npt - rpt == 10 && ((flag & 16) || !has_mask)) {
                                  /* oddly: mask overrides group in st_mode */
       cpt= rpt + 7;
       m= 0;
       if(cpt[0] == 'r')
         m|= S_IRGRP;
       if(cpt[1] == 'w')
         m|= S_IWGRP;
       if(cpt[2] == 'x')
         m|= S_IXGRP;
       list_mode= (list_mode & ~S_IRWXG) | m;
       if((st_mode & S_IRWXG) == (m & S_IRWXG)) {
         tag_types|= 16;
 continue;
       }
       if(flag & 8) {
         cpt[0]= st_mode & S_IRGRP ? 'r' : '-';
         cpt[1]= st_mode & S_IWGRP ? 'w' : '-';
         cpt[2]= st_mode & S_IXGRP ? 'x' : '-';
       }
       tag_types|= 2;
     } else {
       if(rpt[6] == ':' && npt - rpt == 10)
         tag_types|= 1024;
       else
         tag_types|= 64 | 256;
     }
   } else if(strncmp(rpt, "other::", 7) == 0 && npt - rpt == 10) {
     cpt= rpt + 7;
others_st_mode:;
     m= 0;
     if(cpt[0] == 'r')
       m|= S_IROTH;
     if(cpt[1] == 'w')
       m|= S_IWOTH;
     if(cpt[2] == 'x')
       m|= S_IXOTH;
     list_mode= (list_mode & ~S_IRWXO) | m;
     if((st_mode & S_IRWXO) == (m & S_IRWXO)) {
       tag_types|= 8;
 continue;
     }
     if(flag & 8) {
       cpt[0]= st_mode & S_IROTH ? 'r' : '-';
       cpt[1]= st_mode & S_IWOTH ? 'w' : '-';
       cpt[2]= st_mode & S_IXOTH ? 'x' : '-';
     }
     tag_types|= 1;
   } else if(strncmp(rpt, "other:", 6) == 0 && npt - rpt == 9) {
     cpt= rpt + 7;
     goto others_st_mode;
   } else if(strncmp(rpt, "mask::", 6) == 0 && npt - rpt == 9) {
     cpt= rpt + 6;
mask_st_mode:;
     tag_types|= 64 | 512;
     if(!(flag & 16)) {
       /* oddly: mask overrides group in st_mode */
       m= 0;
       if(cpt[0] == 'r')
         m|= S_IRGRP;
       if(cpt[1] == 'w')
         m|= S_IWGRP;
       if(cpt[2] == 'x')
         m|= S_IXGRP;
       list_mode= (list_mode & ~S_IRWXG) | m;
       if(flag & 8) {
         cpt[0]= st_mode & S_IRGRP ? 'r' : '-';
         cpt[1]= st_mode & S_IWGRP ? 'w' : '-';
         cpt[2]= st_mode & S_IXGRP ? 'x' : '-';
       }
     }
   } else if(strncmp(rpt, "mask:", 5) == 0 && npt - rpt == 8) {
     cpt= rpt + 5;
     goto mask_st_mode;
   } else if(*rpt != 0) {
     tag_types|= 64;
   }
   if (flag & 2)
     goto ex;
   if(wpt == rpt) {
     wpt= npt + 1;
 continue;
   }
   if(do_cleanout)
     memmove(wpt, rpt, 1 + npt - rpt);
   wpt+= 1 + npt - rpt;
 }
 if(do_cleanout) {
   if(wpt == acl_text)
     *wpt= 0;
   else if(*(wpt - 1) != 0)
     *wpt= 0;
 }
ex:;
 if(flag & 4)
   *in_st_mode= list_mode;
 return(tag_types);
}


/* Important: acl_text must provide 42 bytes more than its current length !
*/
int aaip_add_acl_st_mode(char *acl_text, mode_t st_mode, int flag)
{
 char *wpt;
 int tag_types= 0;

 tag_types = aaip_cleanout_st_mode(acl_text, &st_mode, 1);
 if(!(tag_types & (4 | 32))) {
   wpt= acl_text + strlen(acl_text);
   sprintf(wpt, "user::%c%c%c\n",
           st_mode & S_IRUSR ? 'r' : '-',
           st_mode & S_IWUSR ? 'w' : '-',
           st_mode & S_IXUSR ? 'x' : '-');
 }
 if(!(tag_types & (2 | 16 | 1024))) {
   wpt= acl_text + strlen(acl_text);
   sprintf(wpt, "group::%c%c%c\n",
         st_mode & S_IRGRP ? 'r' : '-',
         st_mode & S_IWGRP ? 'w' : '-',
         st_mode & S_IXGRP ? 'x' : '-');
 }
 if(!(tag_types & (1 | 8))) {
   wpt= acl_text + strlen(acl_text);
   sprintf(wpt, "other::%c%c%c\n",
         st_mode & S_IROTH ? 'r' : '-',
         st_mode & S_IWOTH ? 'w' : '-',
         st_mode & S_IXOTH ? 'x' : '-');
 }
 if((tag_types & (128 | 256)) && !(tag_types & 512)) {
   wpt= acl_text + strlen(acl_text);
   sprintf(wpt, "mask::%c%c%c\n",
         st_mode & S_IRGRP ? 'r' : '-',
         st_mode & S_IWGRP ? 'w' : '-',
         st_mode & S_IXGRP ? 'x' : '-');
 }
 return(1); 
}


/* --------------------------------- Decoder ---------------------------- */

/* --- private --- */

/* Not less than 2 * 2048 */
#define Aaip_buffer_sizE 4096

/* Enough for one full component record and three empty ones which might get
   added in case of unclean end of attribute list.
*/
#define Aaip_buffer_reservE (257 + 3 * 2)


struct aaip_state {

  /* AAIP field status */
  int aa_head_missing; /* number of bytes needed to complete field header */
  int aa_missing;     /* number of bytes needed to complete current field */
  int aa_ends;      /* 0= still fields expected, 1= last field being processed,
                       2= all fields processed, 3= all is delivered */

  /* Buffer for component records */
  int recs_invalid;                          /* number of components to skip */
  unsigned char recs[Aaip_buffer_sizE + Aaip_buffer_reservE];
  size_t recs_fill;
  unsigned char *recs_start;
  int rec_head_missing;     /* number of bytes needed to complete rec header */
  int rec_missing;         /* number of bytes needed to complete current rec */
  int rec_ends;

  /* Counter for completed data */
  unsigned int num_recs;
  size_t ready_bytes;

  /* Counter and meaning for completed components */
  unsigned int num_components;
  size_t end_of_components; /* start index of eventual incomplete component */
  int first_is_name;

  /* Last return value of aaip_decode_pair() */
  int pair_status;
  unsigned int pairs_skipped;

  /* status of aaip_decode_attrs() */
  size_t list_mem_used;
  size_t list_size;
  size_t list_num_attrs;
  char   **list_names;
  size_t *list_value_lengths;
  char   **list_values;
  char *name_buf;
  size_t name_buf_size;
  size_t name_buf_fill;
  char *value_buf;
  size_t value_buf_size;
  size_t value_buf_fill;
  int list_pending_pair;
};


/* ------- functions ------ */


size_t aaip_count_bytes(unsigned char *data, int flag)
{
 int done = 0;
 unsigned char *aapt;

 for(aapt= data; !done; aapt += aapt[2])
   done = !(aapt[4] & 1);
 return((size_t) (aapt - data));
}


size_t aaip_sizeof_aaip_state(void)
{
 return((size_t) sizeof(struct aaip_state));
}


int aaip_init_aaip_state(struct aaip_state *aaip, int flag)
{
 aaip->aa_head_missing= 5;
 aaip->aa_missing= 0;

 aaip->recs_invalid= 0;
 memset(aaip->recs, 0, Aaip_buffer_sizE + Aaip_buffer_reservE);
 aaip->recs_fill= 0;
 aaip->recs_start= aaip->recs;
 aaip->rec_head_missing= 2;
 aaip->rec_missing= 0;
 aaip->rec_ends= 0;

 aaip->num_recs= 0;
 aaip->ready_bytes= 0;

 aaip->num_components= 0;
 aaip->end_of_components= 0;
 aaip->first_is_name= 1;

 aaip->pair_status= 2;
 aaip->pairs_skipped= 0;

 aaip->list_mem_used= 0;
 aaip->list_size= 0;
 aaip->list_num_attrs= 0;
 aaip->list_names= NULL;
 aaip->list_value_lengths= NULL;
 aaip->list_values= NULL;
 aaip->name_buf= NULL;
 aaip->name_buf_size= 0;
 aaip->name_buf_fill= 0;
 aaip->value_buf= NULL;
 aaip->value_buf_size= 0;
 aaip->value_buf_fill= 0;
 aaip->list_pending_pair= 0;
 return(1);
}

/*
*/
#define Aaip_with_ring_buffeR yes

#ifdef Aaip_with_ring_buffeR

/* Compute the one or two byte intervals in the ring buffer which form a
   given byte interval in the virtual shift fifo.
   @param idx           The byte start index in the virtual shift fifo.
   @param todo          Number of bytes to cover
   @param start_pt      Will return the start address of the first interval
   @param at_start_pt   Will return the size of the first interval
   @param at_recs       Will return the size of the second interval which
                        always starts at aaip->recs
   @param flag          Bitfield for control purposes
   @return              1= next start_pt is *start_pt + *at_start_pt
                        2= next start_pt is aaip->recs + *at_recs
*/
static int aaip_ring_adr(struct aaip_state *aaip, size_t idx, size_t todo,
                         unsigned char **start_pt, size_t *at_start_pt,
                         size_t *at_recs, int flag)
{
 size_t ahead;

 ahead= Aaip_buffer_sizE + Aaip_buffer_reservE
        - (aaip->recs_start - aaip->recs);
 if(idx < ahead)
   *start_pt= (aaip->recs_start + idx);
 else
   *start_pt= aaip->recs + (idx - ahead);
 ahead= Aaip_buffer_sizE + Aaip_buffer_reservE - (*start_pt - aaip->recs);
 if(todo >= ahead) {
   *at_start_pt= ahead;
   *at_recs= todo - ahead;
   return(2);
 }
 *at_start_pt= todo;
 *at_recs= 0;
 return(1);
}


/* 
   @param flag          Bitfield for control purposes
                        bit0= count as ready_bytes
*/
static int aaip_push_to_recs(struct aaip_state *aaip, unsigned char *data,
                             size_t todo, int flag)
{
 unsigned char *start_pt;
 size_t at_start_pt, at_recs;

 aaip_ring_adr(aaip, aaip->recs_fill, todo,
               &start_pt, &at_start_pt, &at_recs, 0);
 if(at_start_pt > 0)
   memcpy(start_pt,  data, at_start_pt);
 if(at_recs > 0)
   memcpy(aaip->recs, data + at_start_pt, at_recs);
 aaip->recs_fill+= todo;
 if(flag &  1)
   aaip->ready_bytes+= todo;
 return(1);
}


static int aaip_read_from_recs(struct aaip_state *aaip, size_t idx,
                               unsigned char *data, size_t num_data, int flag)
{
 unsigned char *start_pt;
 size_t at_start_pt, at_recs;

 aaip_ring_adr(aaip, idx, num_data,
               &start_pt, &at_start_pt, &at_recs, 0);
 if(at_start_pt > 0)
   memcpy(data, start_pt, at_start_pt);
 if(at_recs > 0)
   memcpy(data + at_start_pt, aaip->recs, at_recs);
 return(1);
}


static int aaip_set_buffer_byte(struct aaip_state *aaip, size_t idx,
                                unsigned char data, int flag)
{
 unsigned char *start_pt;
 size_t at_start_pt, at_recs;

 aaip_ring_adr(aaip, idx, 1,
               &start_pt, &at_start_pt, &at_recs, 0);
 *start_pt= data;
 return(1);
}


static int aaip_get_buffer_byte(struct aaip_state *aaip, size_t idx, int flag)
{
 unsigned char *start_pt;
 size_t at_start_pt, at_recs;

 aaip_ring_adr(aaip, idx, 1,
               &start_pt, &at_start_pt, &at_recs, 0);
 return((int) *start_pt);
}


static int aaip_shift_recs(struct aaip_state *aaip, size_t todo, int flag)
{
 int ret;
 unsigned char *start_pt;
 size_t at_start_pt, at_recs;

 if(todo < aaip->recs_fill) {
   ret= aaip_ring_adr(aaip, 0, todo, &start_pt, &at_start_pt, &at_recs, 0);
   if(ret == 1)
     aaip->recs_start= start_pt + todo;
   else
     aaip->recs_start= aaip->recs + at_recs;
 } else {
   aaip->recs_start= aaip->recs;
 }
 aaip->recs_fill-= todo;
 if(aaip->end_of_components >= todo)
   aaip->end_of_components-= todo;
 else
   aaip->end_of_components= 0;
 return(1);
}


#else /* Aaip_with_ring_buffeR */


/* 
   @param flag          Bitfield for control purposes
                        bit0= count as ready_bytes
*/
static int aaip_push_to_recs(struct aaip_state *aaip, unsigned char *data,
                             size_t todo, int flag)
{
 memcpy(aaip->recs + aaip->recs_fill, data, todo);
 aaip->recs_fill+= todo;
 if(flag &  1)
   aaip->ready_bytes+= todo;
 return(1);
}


static int aaip_read_from_recs(struct aaip_state *aaip, size_t idx,
                               unsigned char *data, size_t num_data, int flag)
{
 memcpy(data, aaip->recs + idx, num_data);
 return(1);
}


static int aaip_set_buffer_byte(struct aaip_state *aaip, size_t idx,
                                unsigned char data, int flag)
{
 aaip->recs[idx]= data;
 return(1);
}


static int aaip_get_buffer_byte(struct aaip_state *aaip, size_t idx, int flag)
{
 return((int) aaip->recs[idx]);
}


static int aaip_shift_recs(struct aaip_state *aaip, size_t todo, int flag)
{
 if(todo < aaip->recs_fill)
   memmove(aaip->recs, aaip->recs + todo, aaip->recs_fill - todo);
 aaip->recs_fill-= todo;

 if(aaip->end_of_components >= todo)
   aaip->end_of_components-= todo;
 else
   aaip->end_of_components= 0;
 return(1);
}


#endif /* ! Aaip_with_ring_buffeR */
 

static int aaip_consume_rec_head(struct aaip_state *aaip,
                              unsigned char **data, size_t *num_data, int flag)
{
 size_t todo;

 todo= *num_data;
 if(todo > (size_t) aaip->aa_missing)
   todo= aaip->aa_missing;
 if(todo >= (size_t) aaip->rec_head_missing)
   todo= aaip->rec_head_missing;
 if(!aaip->recs_invalid)
   aaip_push_to_recs(aaip, *data, todo, 0);
 aaip->rec_head_missing-= todo;
 if(aaip->rec_head_missing == 0) {
   aaip->rec_missing= aaip_get_buffer_byte(aaip, aaip->recs_fill - 1, 0); 
   aaip->rec_ends= !(aaip_get_buffer_byte(aaip, aaip->recs_fill - 2, 0) & 1);
 }
 aaip->aa_missing-= todo;
 (*num_data)-= todo;
 (*data)+= todo;
 return(1);
}


static int aaip_consume_rec_data(struct aaip_state *aaip,
                              unsigned char **data, size_t *num_data, int flag)
{
 size_t todo;
 
 todo= *num_data;
 if(todo > (size_t) aaip->aa_missing)
   todo= aaip->aa_missing;
 if(todo > (size_t) aaip->rec_missing)
   todo= aaip->rec_missing;
 if(!aaip->recs_invalid)
   aaip_push_to_recs(aaip, *data, todo, 1);
 aaip->rec_missing-= todo;
 aaip->aa_missing-= todo;
 (*num_data)-= todo;
 (*data)+= todo;
 if(aaip->rec_missing <= 0) {
   if(aaip->recs_invalid > 0) {
     if(aaip->rec_ends)
       aaip->recs_invalid--;
   } else {
     aaip->num_recs++;
     if(aaip->rec_ends) {
       aaip->num_components++;
       aaip->end_of_components= aaip->recs_fill;
     }
   }
   aaip->rec_head_missing= 2;
 }
 return(0);
}


static int aaip_consume_aa_head(struct aaip_state *aaip,
                              unsigned char **data, size_t *num_data, int flag)
{
 size_t todo;
 unsigned char aa_head[5];

 todo= *num_data;
 if(todo >= (size_t) aaip->aa_head_missing)
   todo= aaip->aa_head_missing;
 aaip_push_to_recs(aaip, *data, todo, 0);
 aaip->aa_head_missing-= todo;
 if(aaip->aa_head_missing == 0) {
   aaip_read_from_recs(aaip, aaip->recs_fill - 5, aa_head, 5, 0);
   if(aa_head[0] != 'A' || (aa_head[1] != 'L' && aa_head[1] != 'A') ||
      aa_head[3] != 1)
     return(-1);
   aaip->aa_missing= aa_head[2];
   aaip->aa_ends= !(aa_head[4] & 1);
   aaip->recs_fill-= 5; /* AAIP field heads do not get delivered */
   if(aaip->aa_missing >= 5)
     aaip->aa_missing-= 5;
   else
     aaip->aa_missing= 0;
 }
 (*num_data)-= todo;
 (*data)+= todo;
 return(1);
}


static int aaip_consume_aa_data(struct aaip_state *aaip,
                              unsigned char **data, size_t *num_data, int flag)
{
 size_t i;
 static unsigned char zero_char[2]= {0, 0};

 while(*num_data > 0 && aaip->aa_missing > 0) {
   if(aaip->rec_head_missing > 0) {
     aaip_consume_rec_head(aaip, data, num_data, 0);
     if(*num_data == 0 || aaip->aa_missing <= 0)
       return(1);
   }
   aaip_consume_rec_data(aaip, data, num_data, 0);
 }
 if(aaip->aa_missing <= 0) {
   if(aaip->aa_ends) {
     /* Check for incomplete pair and eventually make emergency closure */
     if(aaip->rec_head_missing != 2) {         /* incomplete record detected */
       if(aaip->rec_head_missing) {
         /* fake 0 length record */
         aaip_set_buffer_byte(aaip, aaip->recs_fill - 1, (unsigned char) 0, 0);
         aaip_push_to_recs(aaip, zero_char, 1, 0);
       } else {
         /* fill in missing btes */
         for(i= 0; (int) i < aaip->rec_missing; i++)
           aaip_push_to_recs(aaip, zero_char, 1, 1);
       }
       aaip->rec_head_missing= 2;
       aaip->rec_missing= 0;
       aaip->num_recs++;
       if(aaip->rec_ends) {
         aaip->num_components++;
         aaip->end_of_components= aaip->recs_fill;
       }
     }
     if(aaip->end_of_components != aaip->recs_fill &&
        aaip->end_of_components != 0) {
                                            /* incomplete component detected */
       /* add empty end record */
       aaip_push_to_recs(aaip, zero_char, 2, 0);
       aaip->num_recs++;
       aaip->num_components++;
       aaip->end_of_components= aaip->recs_fill;
     }
     if(!(aaip->first_is_name ^ (aaip->num_components % 2))) {
                                               /* value component is missing */
       /* add dummy component */
       aaip_push_to_recs(aaip, zero_char, 2, 0);
       aaip->num_recs++;
       aaip->num_components++;
       aaip->end_of_components= aaip->recs_fill;
     }
     aaip->aa_ends= 2;
   } else
     aaip->aa_head_missing= 5;
 }
 return(0);
}


/* Submit small data chunk for decoding.
   The return value will tell whether data are pending for being fetched.
   @param aaip          The AAIP decoder context
   @param data          Not more than 2048 bytes input for the decoder
   @parm  num_data      Number of bytes in data
                        0 inquires the buffer status avoiding replies <= 0
   @param ready_bytes   Number of decoded bytes ready for delivery
   @param flag          Bitfield for control purposes
   @return             -1= non-AAIP field detected
                           *ready_bytes gives number of consumed bytes in data
                        0= cannot accept data because buffer full
                        1= no component record complete, submit more data
                        2= component record complete, may be delivered
                        3= component complete, may be delivered
                        4= no component available, no more data expected, done
*/
int aaip_submit_data(struct aaip_state *aaip,
                     unsigned char *data, size_t num_data,
                     size_t *ready_bytes, int flag)
{
 int ret;
 unsigned char *in_data;

 if(aaip->aa_ends == 3)
   return(4);
 in_data= data;
 if(num_data == 0)
   goto ex;
 if(aaip->recs_fill + num_data > Aaip_buffer_sizE)
   return(0);
 
 while(num_data > 0) {
   if(aaip->aa_head_missing > 0) {
     ret= aaip_consume_aa_head(aaip, &data, &num_data, 0);
     if(ret < 0) {
       *ready_bytes= data - in_data;
       return(-1);
     }
     if(num_data == 0 || aaip->aa_missing <= 0)
       goto ex;
   }
   aaip_consume_aa_data(aaip, &data, &num_data, 0);
   if(aaip->aa_missing)
 break;
 }
ex:;
 *ready_bytes= aaip->ready_bytes;
 if(aaip->num_components > 0)
   return(3);
 if(aaip->num_recs > 0)
   return(2);
 if(aaip->aa_ends && aaip->aa_head_missing == 0 && aaip->aa_missing == 0)
   aaip->aa_ends= 2;
 if(aaip->aa_ends == 2 && aaip->num_recs == 0)
   aaip->aa_ends= 3;
 if(aaip->aa_ends == 3)
   return(4);
 return(1);
}


/* Fetch the available part of current component.
   The return value will tell whether it belongs to name or to value and
   whether that name or value is completed now.
   @param aaip          The AAIP decoder context
   @param result        Has to point to storage for the component data
   @param result_size   Gives the amount of provided result storage
   @param num_result    Will tell the number of fetched result bytes
   @param flag          Bitfield for control purposes
                        bit0= discard data rather than copying to result
   @return -2 = insufficient result_size
           -1 = no data ready for delivery
            0 = result holds the final part of a name
            1 = result holds an intermediate part of a name
            2 = result holds the final part of a value
            3 = result holds an intermediate part of a value
*/
int aaip_fetch_data(struct aaip_state *aaip,
                    char *result, size_t result_size, size_t *num_result,
                    int flag)
{
 int ret= -1, complete= 0, payload;
 unsigned int i, num_bytes= 0, h;

 if(aaip->num_recs == 0)
   return(-1);

 /* Copy data until end of buffer or end of component */
 h= 0;
 for(i= 0; i < aaip->num_recs && !complete; i++) {
   payload= aaip_get_buffer_byte(aaip, h + 1, 0);
   if(!(flag & 1)) {
     if(num_bytes + payload > result_size)
       return(-2);
     aaip_read_from_recs(aaip, h + 2, (unsigned char *) (result + num_bytes),
                         payload, 0);
     *num_result= num_bytes + payload;
   }
   num_bytes+= payload;
   if(!(aaip_get_buffer_byte(aaip, h, 0) & 1))
     complete= 1;
   h+= payload + 2;
 }
 aaip->ready_bytes-= num_bytes;
 aaip->num_recs-= i;

 /* Shift buffer */
 aaip_shift_recs(aaip, h, 0);

 /* Compute reply */
 ret= 2 * !aaip->first_is_name;
 if(complete) {
   aaip->first_is_name= !aaip->first_is_name;
   if(aaip->num_components > 0)
     aaip->num_components--;
 } else
   ret|= 1;

 return(ret);
}


/* Skip the current component and eventually the following value component.
   This has to be called if fetching of a component shall be aborted
   but the next component resp. pair shall be fetchable again.
   aaip_submit_data() will not indicate readiness for fetching until all
   bytes of the skipped components are submitted. Those bytes get discarded.
   @param aaip          The AAIP decoder context
   @param flag          Bitfield for control purposes
                        bit0= do not skip value if current component is name
   @return              <=0 error , 1= now in skip state, 2= not in skip state
*/
int aaip_skip_component(struct aaip_state *aaip, int flag)
{
 int to_skip= 1;

 if(aaip->first_is_name && !(flag & 1))
   to_skip= 2;
 if(aaip->recs_invalid) {
   aaip->recs_invalid+= to_skip;
   return(1);
 }

 if(aaip->num_components) {
   /* null-fetch */
   aaip_fetch_data(aaip, NULL, (size_t) 0, NULL, 1);
   to_skip--;
 }
 if(aaip->num_components && to_skip) {
   /* null-fetch */
   aaip_fetch_data(aaip, NULL, (size_t) 0, NULL, 1);
   to_skip--;
 }
 if(to_skip) {
   aaip->recs_fill= 0;
   aaip->num_recs= 0;
   aaip->ready_bytes= 0;
 }
 aaip->recs_invalid= to_skip;
 if(aaip->aa_ends == 2 && aaip->num_recs == 0)
   aaip->aa_ends= 3;
 return(1 + (aaip->num_recs > 0));
}


/* -------------------------  Pair Level Interface  ------------------------ */

/*
   @param flag          Bitfield for control purposes
                        bit0= do not skip oversized component but return -2
   @return  see aaip_decode_pair
*/
static int aaip_advance_pair(struct aaip_state *aaip,
                            char *name, size_t name_size, size_t *name_fill,
                            char *value, size_t value_size, size_t *value_fill,
                            int flag)
{
 int ret;
 char *wpt;
 size_t size, num;

retry:;
 if(aaip->first_is_name) {
   wpt= name + *name_fill;
   size= name_size - *name_fill;
 } else {
   wpt= value + *value_fill;
   size= value_size - *value_fill;
 }
 ret= aaip_fetch_data(aaip, wpt, size, &num, 0);
 if(ret == -2) {                                 /* insufficient result size */
   if(flag & 1)
     return(-2);
   ret= aaip_skip_component(aaip, 0);
   *name_fill= *value_fill= 0;
   aaip->pairs_skipped++;
   if(ret == 2) /* Skip performed, valid data pending */
     goto retry;
 } else if(ret == -1) {       /* No data ready for delivery : may not happen */
   return(-1);
 } else if(ret == 0) {              /* result holds the final part of a name */
   (*name_fill)+= num;
   /* peek for value data */
   ret= aaip_submit_data(aaip, NULL, (size_t) 0, &num, 0);
   if(ret == 2 || ret == 3) {
     /* fetch value data */;
     ret= aaip_advance_pair(aaip, name, name_size, name_fill,
                            value, value_size, value_fill, flag);
     return ret;
   } else if(ret == 4)
     return(5);
 } else if(ret == 1) {        /* result holds an intermediate part of a name */
   (*name_fill)+= num;
 } else if(ret == 2) {             /* result holds the final part of a value */
   (*value_fill)+= num;
   if(aaip->num_components >= 2)
     return(3);
   if(aaip->aa_ends == 2 && aaip->num_recs == 0)
     aaip->aa_ends= 3;
   if(aaip->aa_ends == 3)
     return(4);
   return(2);
 } else if(ret == 3) {
   /* result holds an intermediate part of a value */;
   (*value_fill)+= num;
 } else {
   return(-1); /* unknown reply from aaip_fetch_data() */
 }
 return(1);
}


/* Accept raw input data and collect a pair of name and value.
   The return value will indicate whether the pair is complete, whether more
   pairs are complete or whether more data are desired. No input data will be
   accepted as long as complete pairs are pending. The end of the attribute
   list will be indicated.
   @param aaip          The AAIP decoder context
   @param data          The raw data to decode
   @param num_data      Number of data bytes provided
   @param consumed      Returns the number of consumed data bytes 
   @param name          Buffer to build the name string
   @param name_size     Maximum number of bytes in name
   @param name_fill     Holds the current buffer fill of name
   @param value         Buffer to build the value string
   @param value_size    Maximum number of bytes in value
   @param value_fill    Holds the current buffer fill of value
   @param flag          Bitfield for control purposes
                        bit0= do not skip oversized pair but return -2
   @return <0 error
           -3 buffer full (program error)
           -2 insufficient result_size (only with flag bit0)
           -1 non-AAIP field detected
            0 data not accepted, first fetch pending pairs with num_data == 0
            1 name and value are not valid yet, submit more data
            2 name and value are valid, submit more data
            3 name and value are valid, pairs pending, fetch with num_data == 0
            4 name and value are valid, no more data expected
            5 name and value are not valid, no more data expected
*/
int aaip_decode_pair(struct aaip_state *aaip,
                     unsigned char *data, size_t num_data, size_t *consumed,
                     char *name, size_t name_size, size_t *name_fill,
                     char *value, size_t value_size, size_t *value_fill,
                     int flag)
{
 int ret;
 size_t ready_bytes= 0;

#ifdef Aaip_with_short_namespaceS
 char prefix[Aaip_max_name_expansioN + 1];
 size_t nl, pl;
#endif

 *consumed= 0;
 if((aaip->pair_status < 0 && aaip->pair_status != -2) ||
     aaip->pair_status == 4 ||
    aaip->pair_status == 5) { /* dead ends */
   ret= aaip->pair_status;
   goto ex;
 } else if(aaip->pair_status == 2 || aaip->pair_status == 3) {
   if(aaip->pair_status == 3 && num_data > 0)
     {ret= 0; goto ex;}
   /* Start a new pair */
   if(!aaip->first_is_name) /* Eventually skip orphaned value */
     aaip_fetch_data(aaip, NULL, (size_t) 0, NULL, 1);
   *name_fill= *value_fill= 0;
 }

 if(num_data > 0) {
   ret= aaip_submit_data(aaip, data, num_data, &ready_bytes, 0);
 } else {
   ret= 1;
   if(aaip->num_components)
     ret= 3;
   else if(aaip->num_recs)
     ret= 2;
 }
 if(ret < 0) { /* non-AAIP field detected */
   *consumed= ready_bytes;
   {ret= -1; goto ex;}
 } else if(ret == 0) { /* buffer overflow */;
   /* should not happen with correct usage */ 
   {ret= -3; goto ex;}
 } else if(ret == 1) { /* no component record complete */
   goto ex;
 } else if(ret == 2) { /* component record complete, may be delivered */
   ;
 } else if(ret == 3) { /* component complete, may be delivered */
   ;
 } else if(ret == 4) { /* no component available, no more data expected */
   {ret= 5; goto ex;}
 } else 
   {ret= -1; goto ex;} /* unknown reply from aaip_submit_data() */

 *consumed= num_data;
 ret= aaip_advance_pair(aaip, name, name_size - Aaip_max_name_expansioN,
                        name_fill, value, value_size, value_fill, flag & 1);
 if(aaip->aa_ends == 3) {
   if(ret >= 2 && ret <= 4)
     ret= 4;
   else
     ret= 5;
 }
ex:;

#ifdef Aaip_with_short_namespaceS

 if(ret >= 2 && ret <= 4 && *name_fill > 0) {
   /* Translate name from eventual short form */
   nl= *name_fill;
   if(name[0] > 0  && name[0] <= Aaip_maxdef_namespacE) {
     prefix[0]= 0;
     if(name[0] == Aaip_namespace_literaL) {
       if(nl > 1) {
         /* Remove first character of name */
         memmove(name, name + 1, nl - 1);
         (*name_fill)--; 
       }
     } else if(name[0] == Aaip_namespace_systeM ||
               name[0] == Aaip_namespace_useR ||
               name[0] == Aaip_namespace_isofS ||
               name[0] == Aaip_namespace_trusteD ||
               name[0] == Aaip_namespace_securitY
              ) {
       strcpy(prefix, Aaip_namespace_textS[(int) name[0]]);
       pl= strlen(prefix);
       memmove(name + pl, name + 1, nl - 1);
       memcpy(name, prefix, pl);
       *name_fill= pl + nl - 1;
     }
   }
 }

#endif /* Aaip_with_short_namespaceS */

 aaip->pair_status= ret;
 return(ret);
}


unsigned int aaip_get_pairs_skipped(struct aaip_state *aaip, int flag)
{
 return(aaip->pairs_skipped);
}


/* -------------------------  List Level Interface  ------------------------ */


#define Aaip_initial_name_leN 256
#define Aaip_initial_value_leN 256
#define Aaip_initial_list_sizE  2
#define Aaip_list_enlargeR     1.5


/*
   @param flag          Bitfield for control purposes
                        bit0=  do not update *buf_size
*/
static int aaip_enlarge_buf(struct aaip_state *aaip, size_t memory_limit,
                      size_t item_size, char **buf, size_t *buf_size, int flag)
{
 size_t new_size;
 char *new_buf;

 new_size= *buf_size * Aaip_list_enlargeR;
 if(aaip->list_mem_used + (new_size - *buf_size) * item_size >= memory_limit)
   return(3);
 aaip->list_mem_used+= (new_size - *buf_size) * item_size;
 new_buf= realloc(*buf, new_size * item_size);
 if(new_buf == NULL)
   return(-1);
 *buf= new_buf;
 if(!(flag & 1))
   *buf_size= new_size;
 return(1);
}


/* Accept raw input data and collect arrays of name pointers, value lengths
   and value pointers. A handle object will emerge which finally has to be
   be freed by a call with bit 15.
   @param handle        The decoding context.
                        It will be created by this call with flag bit 0 or if
                        *handle == NULL. This handle has to be the same as long
                        as decoding goes on and finally has to be freed by a
                        call with bit15.
   @param memory_limit  Maximum number of bytes to allocate
   @param num_attr_limit  Maximum number of name-value pairs to allocate
   @param data          The raw data to decode
   @param num_data      Number of data bytes provided
   @param consumed      Returns the number of consumed data bytes
   @param flag          Bitfield for control purposes
                        bit0=  this is the first call with the given handle
                               (also in effect if *handle is NULL)
                        bit15= end decoding :
                               Free handle and its intermediate list memory.
   @return <=0 error
            -4 interpretation stalled, no valid result
            -3 program error, unexpected reply from lower layers
            -2 non-AAIP-field detected, arrays are complete,
               call aaip_get_decoded_attrs()
            -1 out of memory
             1 not complete yet, submit more data
             2 arrays are complete, call aaip_get_decoded_attrs()
             3 limit exceeded, not complete yet,
               enlarge memory_limit or call with bit15 and give up
             4 limit exceeded, call aaip_get_decoded_attrs() and try again
*/
int aaip_decode_attrs(struct aaip_state **handle,
                      size_t memory_limit, size_t num_attr_limit,
                      unsigned char *data, size_t num_data, size_t *consumed, 
                      int flag)
{
 int ret;
 struct aaip_state *aaip;
 size_t h_num, *h_lengths, i, new_mem, pair_consumed= 0;
 char **h_names, **h_values, *hpt;

 *consumed= 0;
 if(flag & (1 << 15)) {
   if(*handle == NULL)
     return(0);
   ret= aaip_get_decoded_attrs(handle, &h_num, &h_names, &h_lengths, &h_values,
                               0);
   if(ret > 0)
     aaip_get_decoded_attrs(handle, &h_num, &h_names, &h_lengths, &h_values,
                            1 << 15);
   if((*handle)->name_buf != NULL)
     free((*handle)->name_buf);
   if((*handle)->value_buf != NULL)
     free((*handle)->value_buf);
   free((char *) *handle);
   *handle= NULL;
   return(1);
 }

 aaip= *handle;
 if(aaip == NULL || (flag & 1)) {
   aaip= *handle= calloc(1, sizeof(struct aaip_state));
   if(*handle == NULL)
     return(-1);
   aaip_init_aaip_state(*handle, 0);
 }
 if(aaip->list_names == NULL || aaip->list_values == NULL ||
    aaip->list_value_lengths == NULL) {
   /* Initialize arrays */
   aaip->list_size= Aaip_initial_list_sizE;
   if(num_attr_limit > 0  && num_attr_limit < aaip->list_size)
     aaip->list_size= num_attr_limit;
   new_mem= aaip->list_size * (2*sizeof(char *) + sizeof(size_t)) +
            Aaip_initial_name_leN + Aaip_initial_value_leN;
   if(aaip->list_mem_used + new_mem >= memory_limit)
     return(3);
   aaip->list_mem_used+= new_mem;
   aaip->list_names= calloc(sizeof(char *), aaip->list_size);
   aaip->list_value_lengths= calloc(sizeof(size_t), aaip->list_size);
   aaip->list_values= calloc(sizeof(char *), aaip->list_size);
   if(aaip->list_names == NULL || aaip->list_value_lengths == NULL ||
      aaip->list_values == NULL)
     return(-1);
   for(i= 0; i < aaip->list_size; i++) {
     aaip->list_names[i]= NULL;
     aaip->list_value_lengths[i]= 0;
     aaip->list_values[i]= NULL;
   }
 }
 if(aaip->name_buf == NULL || aaip->value_buf == NULL) {
   new_mem= Aaip_initial_name_leN + Aaip_initial_value_leN;
   if(aaip->list_mem_used >= memory_limit)
     return(3);
   aaip->list_mem_used+= new_mem;
   aaip->name_buf= calloc(sizeof(char *), Aaip_initial_name_leN);
   aaip->value_buf= calloc(sizeof(char *), Aaip_initial_value_leN);
   if(aaip->name_buf == NULL || aaip->value_buf == NULL)
     return(-1);
   aaip->name_buf_size= Aaip_initial_name_leN;
   aaip->value_buf_size= Aaip_initial_name_leN;
 }

 while(1) {
   if(aaip->list_pending_pair > 0) {
    /* the buffer holds a complete pair from a previous memory limit refusal */
     ret= aaip->list_pending_pair;
     aaip->list_pending_pair= 0;
   } else {
     ret= aaip_decode_pair(aaip, data, num_data, &pair_consumed,
                  aaip->name_buf, aaip->name_buf_size, &aaip->name_buf_fill,
                  aaip->value_buf, aaip->value_buf_size, &aaip->value_buf_fill,
                  1);
     *consumed+= pair_consumed;
   }
   if(ret == -2) { /* insufficient result_size */
     if(aaip->first_is_name)
       ret= aaip_enlarge_buf(aaip, memory_limit, (size_t) 1, &(aaip->name_buf),
                             &(aaip->name_buf_size), 0);
     else
       ret= aaip_enlarge_buf(aaip, memory_limit, (size_t) 1,
                             &(aaip->value_buf), &(aaip->value_buf_size), 0);
     if(ret != 1)
       return(ret);

   } else if(ret == -1) { /* non-AAIP field detected */
     if(pair_consumed <= 0)
       return(-4); /* interpretation did not advance */

   } else if(ret < 0) { /* other error */
     return(-3);

   } else if(ret == 0) { /* first fetch pending pairs with num_data == 0 */
     /* should not happen, fetch more pairs */;

   } else if(ret == 1) {
                       /* name and value are not valid yet, submit more data */
     return(1);

   } else if(ret == 2 || ret == 3 || ret == 4) {
                               /* name and value are valid, submit more data */
        /* name and value are valid, pairs pending, fetch with num_data == 0 */
                          /* name and value are valid, no more data expected */
     aaip->list_pending_pair= ret;

     if(aaip->list_num_attrs >= aaip->list_size) {
       hpt= (char *) aaip->list_names;
       ret= aaip_enlarge_buf(aaip, memory_limit, sizeof(char *),
                             &hpt, &(aaip->list_size), 1);
       if(ret != 1)
         return(ret);
       aaip->list_names= (char **) hpt;
       hpt= (char *) aaip->list_values;
       ret= aaip_enlarge_buf(aaip, memory_limit, sizeof(char *),
                             &hpt, &(aaip->list_size), 1);
       if(ret != 1)
         return(ret);
       aaip->list_values= (char **) hpt;
       hpt= (char *) aaip->list_value_lengths;
       ret= aaip_enlarge_buf(aaip, memory_limit, sizeof(size_t),
                             &hpt, &(aaip->list_size), 0);
       if(ret != 1)
         return(ret);
       aaip->list_value_lengths= (size_t *) hpt;
     }

     /* Allocate name and value in list */;
     if(aaip->list_mem_used + aaip->name_buf_fill + aaip->value_buf_fill + 2
        > memory_limit) {
       return(3);
     }
     aaip->list_mem_used+= aaip->name_buf_fill + aaip->value_buf_fill + 2;
     i= aaip->list_num_attrs;
     aaip->list_names[i]= calloc(aaip->name_buf_fill + 1, 1);
     aaip->list_values[i]= calloc(aaip->value_buf_fill + 1, 1);
     memcpy(aaip->list_names[i], aaip->name_buf, aaip->name_buf_fill);
     aaip->list_names[i][aaip->name_buf_fill]= 0;
     memcpy(aaip->list_values[i], aaip->value_buf, aaip->value_buf_fill);
     aaip->list_values[i][aaip->value_buf_fill]= 0;
     aaip->list_value_lengths[i]= aaip->value_buf_fill;
     aaip->list_num_attrs++;
     aaip->name_buf_fill= aaip->value_buf_fill= 0;

     ret= aaip->list_pending_pair;
     aaip->list_pending_pair= 0;

     if(ret == 2) 
        return(1);
     if(ret == 4) 
 break;

   } else if(ret == 5)
 break;
   else
     return(-2);

   num_data= 0; /* consume pending pairs */
 }
 aaip->list_pending_pair= 5;
 return(2);
}


/* Obtain the resulting attributes when aaip_decode_attrs() indicates to
   be done or to have the maximum possible amount of result ready.
   The returned data objects finally have to be freed by a call with flag
   bit 15.
   @param handle        The decoding context created by aaip_decode_attrs()
   @param num_attrs     Will return the number of name-value pairs
   @param names         Will return an array of pointers to 0-terminated names
   @param value_lengths Will return an arry with the lenghts of values
   @param values        Will return an array of pointers to 8-bit values
   @param flag          Bitfield for control purposes
                        bit15= free memory of names, value_lengths, values
   @return              <0 error
                        0  no attribute list ready 
                        1  ok
*/
int aaip_get_decoded_attrs(struct aaip_state **handle, size_t *num_attrs,
                         char ***names, size_t **value_lengths, char ***values,
                         int flag)
{
 size_t i;
 struct aaip_state *aaip;

 aaip= *((struct aaip_state **) handle);
 if(flag & (1 << 15)) {
   if(*names != NULL) {
     for(i= 0; i < *num_attrs; i++) {
       if((*names)[i] != NULL)
         free((*names)[i]);
       (*names)[i]= NULL;
     }
     free(*names);
     *names= NULL;
   }
   if(*values != NULL) {
     for(i= 0; i < *num_attrs; i++) {
       if((*values)[i] != NULL)
         free((*values)[i]);
       (*values)[i]= NULL;
     }
     free(*values);
     *values= NULL;
   }
   if(*value_lengths != NULL)
     free(*value_lengths);
   *value_lengths= NULL;
   *num_attrs= 0;
   return(1);
 }

 /* Check whether decoding is finished yet */
 if(aaip->list_pending_pair != 5)
   return(0);

 *num_attrs= aaip->list_num_attrs;
 *names= aaip->list_names;
 *value_lengths= aaip->list_value_lengths;
 *values= aaip->list_values;

 /* Now the memory is owned by the caller */
 aaip->list_num_attrs= 0;
 aaip->list_names= NULL;
 aaip->list_value_lengths= NULL;
 aaip->list_values= NULL;
 aaip->list_size= 0;
 aaip->list_pending_pair= 0;
 return(1);
}


/* ------ Decoder for ACLs ------ */


static int aaip_write_acl_line(char **result, size_t *result_size,
                               char *tag_type, char *qualifier,
                               char *permissions, int flag)
{
 size_t needed, tag_len, perm_len, qualifier_len;

 tag_len= strlen(tag_type);
 qualifier_len= strlen(qualifier);
 perm_len= strlen(permissions);
 needed= tag_len + qualifier_len + perm_len + 3;
 if((flag & 1)) {
   (*result_size)+= needed;
   return(1);
 }
 if(needed + 1 > *result_size) /* +1 : want to append a trailing 0 */
   return(-1);
 memcpy((*result), tag_type, tag_len);
 (*result)[tag_len]= ':';
 memcpy((*result) + tag_len + 1, qualifier, qualifier_len);
 (*result)[tag_len + 1 + qualifier_len]= ':';
 memcpy((*result) + tag_len + 1 + qualifier_len + 1, permissions, perm_len);
 (*result)[tag_len + 1 + qualifier_len + 1 + perm_len]= '\n';
 (*result)[tag_len + 1 + qualifier_len + 1 + perm_len + 1] = 0;
 (*result)+= needed;
 (*result_size)-= needed;
 return(1);
}


static int aaip_read_qualifier(unsigned char *data, size_t num_data,
                               char *name, size_t name_size, size_t *name_fill,
                               int flag)
{
 int is_done= 0;
 size_t rec_len= 0;
 unsigned char *rpt;

 *name_fill= 0;
 for(rpt= data; !is_done; rpt+= rec_len) {
   rec_len= (*rpt) & 127;
   is_done= !((*rpt) & 128);
   if(*name_fill + rec_len >= name_size ||
      (size_t) (rpt + 1 + rec_len - data) > num_data)
     return(-1);
   memcpy(name + *name_fill, rpt + 1, rec_len);
   rpt+= 1 + rec_len;
   (*name_fill)+= rec_len;
   name[*name_fill]= 0;
 }
 return(1);
}


/* Convert an AAIP ACL attribute value into the long text form of ACL.
   @param data          The raw data to decode
   @param num_data      Number of data bytes provided
   @param consumed      Returns the number of consumed data bytes
   @param acl_text      Will be filled with ACL long text form
   @param acl_text_size Maximum number of bytes to be written to acl_text
   @param acl_text_fill Will return the number of bytes in acl_text
   @param flag          Bitfield for control purposes
                        bit0= count only, do not really produce bytes:
                               acl_text will not be touched,
                               acl_text_size will be ignored,
                               *acl_text_fill will return the counted number
                                              plus 1 for a trailing zero.
                        bit1= expected is a default ACL (see return value 2)
   @return              1 success
                        2 success, begin of default/access ACL encountered,
                          submit data + *consumed for access/default ACL
                       -1 error with reading of qualifier
                       -2 error with writing of ACL text line
                       -3 version mismatch
                       -4 unknown tag type encountered
*/
int aaip_decode_acl(unsigned char *data, size_t num_data, size_t *consumed,
                    char *acl_text, size_t acl_text_size,
                    size_t *acl_text_fill, int flag)
{
 unsigned char *rpt;
 char perm_text[4], *wpt, *name= NULL;
 int type, qualifier= 0, perm, ret, cnt, name_size= 1024;
 size_t w_size, name_fill= 0, i;
 uid_t uid;
 gid_t gid;
 struct passwd *pwd;
 struct group *grp;

 LIBISO_ALLOC_MEM(name, char, name_size);
 cnt= flag & 1;
 *consumed= 0;
 wpt= acl_text;
 w_size= acl_text_size;
 *acl_text_fill= 0;
 for(rpt= data; (size_t) (rpt - data) < num_data; ) {
   perm= *rpt;
   strcpy(perm_text, "---");
   if(perm & Aaip_READ)
     perm_text[0]= 'r';
   if(perm & Aaip_WRITE)
     perm_text[1]= 'w';
   if(perm & Aaip_EXEC)
     perm_text[2]= 'x';
     
   type= (*rpt) >> 4;
   if(type == Aaip_FUTURE_VERSION) /* indicate to caller: version mismatch */
     {ret = -3; goto ex;}

   qualifier= !!((*rpt) & 8);
   if(qualifier) {
     ret= aaip_read_qualifier(rpt + 1, num_data - (rpt + 1 - data),
                              name, name_size, &name_fill, 0);
     if(ret <= 0)
       {ret = -1; goto ex;}
   }

   /* Advance read pointer */
   (*consumed)+= 1 + (qualifier ? name_fill + 1 : 0);
   rpt+= 1 + (qualifier ? name_fill + 1 : 0);

   ret= 1;
   if(type == Aaip_TRANSLATE) {
     /* rightfully ignored yet */;
 continue;
   } else if(type == Aaip_ACL_USER_OBJ) {
     /* user::rwx */
     ret= aaip_write_acl_line(&wpt, &w_size, "user", "", perm_text, cnt);
   } else if(type == Aaip_ACL_USER) {
     /* user:<username>:rwx */;
     ret= aaip_write_acl_line(&wpt, &w_size, "user", name, perm_text, cnt);
   } else if(type == Aaip_ACL_GROUP_OBJ) {
     /* user::rwx */
     ret= aaip_write_acl_line(&wpt, &w_size, "group", "", perm_text, cnt);
   } else if(type == Aaip_ACL_GROUP) {
     /* group:<groupname>:rwx */;
     ret= aaip_write_acl_line(&wpt, &w_size, "group", name, perm_text, cnt);
   } else if(type == Aaip_ACL_MASK) {
     /* mask::rwx */
     ret= aaip_write_acl_line(&wpt, &w_size, "mask", "", perm_text, cnt);
   } else if(type == Aaip_ACL_OTHER) {
     /* other::rwx */
     ret= aaip_write_acl_line(&wpt, &w_size, "other", "", perm_text, cnt);
   } else if(type == Aaip_SWITCH_MARK) {
     /* Indicate to caller: end of desired ACL type access/default */
     if((perm & Aaip_EXEC) ^ (!!(flag & 2)))
       {ret= 2; goto ex;}
   } else if(type == Aaip_ACL_USER_N) {
     /* determine username from uid */
     uid= 0;
     for(i= 0; i < name_fill; i++)
       uid= (uid << 8) | ((unsigned char *) name)[i];
     pwd= getpwuid(uid);
     if(pwd == NULL)
       sprintf(name, "%.f", (double) uid);
     else if(strlen(pwd->pw_name) >= (size_t) name_size)
       sprintf(name, "%.f", (double) uid);
     else
       strcpy(name, pwd->pw_name);
     /* user:<username>:rwx */;
     ret= aaip_write_acl_line(&wpt, &w_size, "user", name, perm_text, cnt);
   } else if(type == Aaip_ACL_GROUP_N) {
     /* determine username from gid */;
     gid= 0;
     for(i= 0; i < name_fill; i++)
       gid= (gid << 8) | ((unsigned char *) name)[i];
     grp= getgrgid(gid);
     if(grp == NULL)
       sprintf(name, "%.f", (double) gid);
     else if(strlen(grp->gr_name) >= (size_t) name_size)
       sprintf(name, "%.f", (double) gid);
     else
       strcpy(name, grp->gr_name);
     /* user:<username>:rwx */;
     ret= aaip_write_acl_line(&wpt, &w_size, "group", name, perm_text, cnt);
   } else {
     /* indicate to caller: unknown type */
     {ret = -4; goto ex;}
   }
   if(ret <= 0)
     {ret = -2; goto ex;}
 }
 ret= 1;
ex:;
 *acl_text_fill= w_size;
 if(flag & 1)
   (*acl_text_fill)++;
 LIBISO_FREE_MEM(name);
 return(ret);
}


/* ----------------------- Adapter for operating systems ----------------- */


#ifdef __FreeBSD__

#include "aaip-os-freebsd.c"

#else
#ifdef __linux

#include "aaip-os-linux.c"

/* August 2011: aaip-os-linux.c would also work for GNU/Hurd : ifdef __GNU__
   Libraries and headers are present on Debian GNU/Hurd but there is no
   ACL or xattr support in the filesystems yet.
   Further, llistxattr() produces ENOSYS "Function not implemented".
   So it makes few sense to enable it here.
*/

#else

#include "aaip-os-dummy.c"

#endif /* ! __linux */
#endif /* ! __FreeBSD__ */

