
/*

 aaip-os-freebsd.c
 Arbitrary Attribute Interchange Protocol , system adapter for getting and
 setting of ACLs and xattr.

 To be included by aaip_0_2.c

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
#include <sys/stat.h>
#include <errno.h>

#ifdef Libisofs_with_aaip_acL
#include <sys/acl.h>
#endif

#ifdef Libisofs_with_freebsd_extattR
#include <sys/extattr.h>
#endif

/* <<< Use old ACL adapter code that is unable to deal with extattr */
/* # define Libisofs_old_freebsd_acl_adapteR */


/* ------------------------------ Inquiry --------------------------------- */

/* See also API iso_local_attr_support().
   @param flag
        Bitfield for control purposes
             bit0= inquire availability of ACL
             bit1= inquire availability of xattr
             bit2 - bit7= Reserved for future types.
                          It is permissibile to set them to 1 already now.
             bit8 and higher: reserved, submit 0
   @return
        Bitfield corresponding to flag. If bits are set, th
             bit0= ACL adapter is enabled
             bit1= xattr adapter is enabled
             bit2 - bit7= Reserved for future types.
             bit8 and higher: reserved, do not interpret these
*/
int aaip_local_attr_support(int flag)
{
 int ret= 0;

#ifdef Libisofs_with_aaip_acL
 if(flag & 1)
   ret|= 1;
#endif
#ifdef Libisofs_with_freebsd_extattR
 if(flag & 2)
   ret|= 2;
#endif

 return(ret);
}


/* ------------------------------ Getters --------------------------------- */

/* Obtain the ACL of the given file in long text form.
   @param path          Path to the file
   @param text          Will hold the result. This is a managed object which
                        finally has to be freed by a call to this function
                        with bit15 of flag.
   @param flag          Bitfield for control purposes
                        (bit0=  obtain default ACL rather than access ACL)
                        bit4=  set *text = NULL and return 2
                               if the ACL matches st_mode permissions.
                        bit5=  in case of symbolic link: inquire link target
                        bit15= free text and return 1
   @return              > 0 ok
                          0 ACL support not enabled at compile time
                            or filesystem does not support ACL
                         -1 failure of system ACL service (see errno)
                         -2 attempt to inquire ACL of a symbolic
                            link without bit4 or bit5
*/
int aaip_get_acl_text(char *path, char **text, int flag)
{
#ifdef Libisofs_with_aaip_acL
 acl_t acl= NULL;
#endif
 struct stat stbuf;
 int ret;

 if(flag & (1 << 15)) {
   if(*text != NULL)
#ifdef Libisofs_with_aaip_acL
     acl_free(*text);
#else
     free(*text);
#endif
   *text= NULL;
   return(1);
 }

 *text= NULL;
 if(flag & 32)
   ret= stat(path, &stbuf);
 else
   ret= lstat(path, &stbuf);
 if(ret == -1)
   return(-1);
 if((stbuf.st_mode & S_IFMT) == S_IFLNK) {
   if(flag & 16)
     return(2);
   return(-2);
 }

 /* Note: no ACL_TYPE_DEFAULT in FreeBSD  */
 if(flag & 1)
   return(0);

#ifdef Libisofs_with_aaip_acL

 acl= acl_get_file(path, ACL_TYPE_ACCESS);

 if(acl == NULL) {
   if(errno == EOPNOTSUPP) {
     /* filesystem does not support ACL */
     if(flag & 16)
       return(2);

     /* >>> ??? fake ACL from POSIX permissions ? */;

     return(0);
   }
   return(-1);
 }
 *text= acl_to_text(acl, NULL);
 acl_free(acl);

#else /* Libisofs_with_aaip_acL */

 /* ??? >>> Fake ACL */;

 return(0);

#endif /* ! Libisofs_with_aaip_acL */

 if(*text == NULL)
   return(-1);
 if(flag & 16) {
   ret = aaip_cleanout_st_mode(*text, &(stbuf.st_mode), 2);
   if(!(ret & (7 | 64)))
     (*text)[0]= 0;
   if((*text)[0] == 0 || strcmp(*text, "\n") == 0) {
#ifdef Libisofs_with_aaip_acL
     acl_free(*text);
#else
     free(*text);
#endif
     *text= NULL;
     return(2);
   }
 }
 return(1);
}


#ifndef Libisofs_old_freebsd_acl_adapteR

#ifdef Libisofs_with_freebsd_extattR

/*
   @param flag          Bitfield for control purposes
                        bit5=  in case of symbolic link: inquire link target
*/
static int aaip_extattr_make_list(char *path, int attrnamespace,
                                  char **list, ssize_t *list_size, int flag)
{
 *list= NULL;
 *list_size= 0;

 /* man 2 extattr_list_file:
    If data is NULL in a call to extattr_get_file() and extattr_list_file()
    then the size of defined extended attribute data will be returned,
 */
 if(flag & 32) /* follow link */
   *list_size= extattr_list_file(path, attrnamespace, NULL, (size_t) 0);
 else
   *list_size= extattr_list_link(path, attrnamespace, NULL, (size_t) 0);
 if(*list_size == -1)
   return(0);
 if(*list_size == 0)
   return(2);
 *list= calloc(*list_size, 1);
 if(*list == NULL)
   return(-1);
 if(flag & 32)
   *list_size= extattr_list_file(path, attrnamespace, *list,
                                 (size_t) *list_size);
 else
   *list_size= extattr_list_link(path, attrnamespace, *list,
                                 (size_t) *list_size);
 if(*list_size == -1)
   return(0);
 return(1);
}


/*
   @param flag          Bitfield for control purposes
                        bit0= preserve existing namelist content
                        bit1= ignore names with NUL rather than returning error
*/
static int aaip_extattr_make_namelist(char *path, char *attrnamespace,
                                      char *list, ssize_t list_size,
                                      char **namelist, ssize_t *namelist_size,
                                      ssize_t *num_names, int flag)
{
 int i, j, len, new_bytes= 0, space_len;
 char *new_list= NULL, *wpt;

 if(!(flag & 1)) {
   *namelist= NULL;
   *namelist_size= 0;
   *num_names= 0;
 }
 if(list_size <= 0)
   return(1);
 space_len= strlen(attrnamespace);
 for(i= 0; i < list_size; i+= len + 1) {
   len= *((unsigned char *) (list + i));
   if(len == 0)
     return ISO_AAIP_BAD_ATTR_NAME; /* empty name is reserved for ACL */
   for(j= 0; j < len; j++)
     if(list[i + 1 + j] == 0) {
       if(flag & 2)
 continue;
       return ISO_AAIP_BAD_ATTR_NAME; /* names may not contain 0-bytes */
     }
   new_bytes+= space_len + 1 + len + 1;
 }
 if((flag & 1) && *namelist_size > 0)
   new_bytes+= *namelist_size;
 new_list= calloc(new_bytes, 1);
 if(new_list == NULL)
   return(ISO_OUT_OF_MEM);
 wpt= new_list;
 if((flag & 1) && *namelist_size > 0) {
   memcpy(new_list, *namelist, *namelist_size);
   wpt= new_list + *namelist_size;
 }
 for(i= 0; i < list_size; i+= len + 1) {
   len= *((unsigned char *) (list + i));
   if(flag & 2) {
     for(j= 0; j < len; j++)
       if(list[i + j] == 0)
 continue;
   }
   memcpy(wpt, attrnamespace, space_len);
   wpt[space_len]= '.';
   wpt+= space_len + 1;
   memcpy(wpt, list + i + 1, len);
   wpt+= len;
   *(wpt++)= 0;
   (*num_names)++;
 }
 if((flag & 1) && *namelist != NULL)
   free(*namelist);
 *namelist= new_list;
 *namelist_size= new_bytes;
 return(1);
}

#endif /* Libisofs_with_freebsd_extattR */


/* Obtain the Extended Attributes and/or the ACLs of the given file in a form
   that is ready for aaip_encode().
   @param path          Path to the file
   @param num_attrs     Will return the number of name-value pairs
   @param names         Will return an array of pointers to 0-terminated names
   @param value_lengths Will return an arry with the lenghts of values
   @param values        Will return an array of pointers to 8-bit values
   @param flag          Bitfield for control purposes
                        bit0=  obtain ACL (access and eventually default)
                        bit1=  use numeric ACL qualifiers rather than names
                        bit2=  do not obtain attributes other than ACL
                        bit3=  do not ignore eventual non-user attributes
                               I.e. those with a name which does not begin
                               by "user."
                        bit4=  do not return trivial ACL that matches st_mode
                        bit5=  in case of symbolic link: inquire link target
                        bit15= free memory of names, value_lengths, values
   @return              >0  ok
                        <=0 error
                        -1= out of memory
                        -2= program error with prediction of result size
                        -3= error with conversion of name to uid or gid
*/
int aaip_get_attr_list(char *path, size_t *num_attrs, char ***names,
                       size_t **value_lengths, char ***values, int flag)
{
 int ret;
 ssize_t i, num_names= 0, acl_names= 0;

#ifdef Libisofs_with_aaip_acL
 unsigned char *a_acl= NULL;
 char *a_acl_text= NULL;
 size_t a_acl_len= 0;
#endif
#ifdef Libisofs_with_freebsd_extattR
 char *list= NULL, *user_list= NULL, *sys_list= NULL, *namept;
 ssize_t value_ret, retry= 0, list_size= 0, user_list_size= 0;
 ssize_t sys_list_size= 0;
 int attrnamespace;
#endif

 if(flag & (1 << 15)) { /* Free memory */
   {ret= 1; goto ex;}
 }

 *num_attrs= 0;
 *names= NULL;
 *value_lengths= NULL;
 *values= NULL;

 /* Set up arrays */

#ifdef Libisofs_with_freebsd_extattR

 if(!(flag & 4)) { /* Get extattr names */

   /* Linux  : Names are encoded as name NUL
      FreeBSD: Names are encoded as length_byte:chars (no NUL)
      AAIP demands names not to contain NUL bytes. 
   */

   /* Obtain lists of names
      Must be done separately for namespaces. See man 9 extattr :
      EXTATTR_NAMESPACE_USER , EXTATTR_NAMESPACE_SYSTEM
      Must then be marked by "user." and "system." for libisofs use.
   */
   ret= aaip_extattr_make_list(path, EXTATTR_NAMESPACE_USER,
                               &user_list, &user_list_size, flag & 32);
   if(ret <= 0)
     {ret= -1; goto ex;}
   if(flag & 8) {
     ret= aaip_extattr_make_list(path, EXTATTR_NAMESPACE_SYSTEM,
                                 &sys_list, &sys_list_size, flag & 32);
     if(ret <= 0)
       {ret= -1; goto ex;}
   }

   /* Check for NUL in names, convert into a linuxish list of namespace.name */
   ret= aaip_extattr_make_namelist(path, "user", user_list, user_list_size,
                                   &list, &list_size, &num_names, 0);
   if(ret <= 0)
     goto ex;
   ret= aaip_extattr_make_namelist(path, "system", sys_list, sys_list_size,
                                   &list, &list_size, &num_names, 1);
   if(ret <= 0)
     goto ex;
 }

#endif /* Libisofs_with_freebsd_extattR */

#ifdef Libisofs_with_aaip_acL
 if(flag & 1) {
   num_names++;
   acl_names= 1;
 }
#endif
 
 if(num_names == 0)
   {ret= 1; goto ex;}
 (*names)= calloc(num_names, sizeof(char *));
 (*value_lengths)= calloc(num_names, sizeof(size_t));
 (*values)= calloc(num_names, sizeof(char *));
 if(*names == NULL || *value_lengths == NULL || *values == NULL)
   {ret= -1; goto ex;}

 for(i= 0; i < num_names; i++) {
   (*names)[i]= NULL;
   (*values)[i]= NULL;
   (*value_lengths)[i]= 0;
 }

#ifdef Libisofs_with_freebsd_extattR

 if(!(flag & 4)) { /* Get xattr values */
   for(i= 0; i < list_size && (size_t) num_names - acl_names > *num_attrs;
       i+= strlen(list + i) + 1) {
     if(!(flag & 8))
       if(strncmp(list + i, "user.", 5))
   continue;
     (*names)[(*num_attrs)++]= strdup(list + i);
     if((*names)[(*num_attrs) - 1] == NULL)
       {ret= -1; goto ex;}
   }
 
   for(i= 0; (size_t) i < *num_attrs; i++) {
     if(strncmp((*names)[i], "user.", 5) == 0) {
       attrnamespace= EXTATTR_NAMESPACE_USER;
       namept= (*names)[i] + 5;
     } else {
       if(!(flag & 8))
   continue;
       attrnamespace= EXTATTR_NAMESPACE_SYSTEM;
       namept= (*names)[i] + 7;
     }
     /* Predict length of value */
     if(flag & 32) /* follow link */
       value_ret= extattr_get_file(path, attrnamespace, namept,
                                   NULL, (size_t) 0);
     else
       value_ret= extattr_get_link(path, attrnamespace, namept,
                                   NULL, (size_t) 0);
     if(value_ret == -1)
 continue;

     (*values)[i]= calloc(value_ret + 1, 1);
     if((*values)[i] == NULL)
       {ret= -1; goto ex;}

     /* Obtain value */
     if(flag & 32) /* follow link */
       value_ret= extattr_get_file(path, attrnamespace, namept,
                                   (*values)[i], (size_t) value_ret);
     else
       value_ret= extattr_get_link(path, attrnamespace, namept,
                                   (*values)[i], (size_t) value_ret);
     if(value_ret == -1) { /* there could be a race condition */

       if(retry++ > 5)
         {ret= -1; goto ex;}
       i--;
 continue;
     }
     (*value_lengths)[i]= value_ret;
     retry= 0;
   }
 }

#endif /* Libisofs_with_freebsd_extattR */

#ifdef Libisofs_with_aaip_acL

 if(flag & 1) { /* Obtain ACL */
  /* access-ACL */
   aaip_get_acl_text(path, &a_acl_text, flag & (16 | 32));
   if(a_acl_text == NULL)
     {ret= 1; goto ex;} /* empty ACL / only st_mode info was found in ACL */
   ret= aaip_encode_acl(a_acl_text, (mode_t) 0, &a_acl_len, &a_acl, flag & 2);
   if(ret <= 0)
     goto ex;

   /* Note: There are no default-ACL in FreeBSD */

   /* Set as attribute with empty name */;
   (*names)[*num_attrs]= strdup("");
   if((*names)[*num_attrs] == NULL)
     {ret= -1; goto ex;}
   (*values)[*num_attrs]= (char *) a_acl;
   a_acl= NULL;
   (*value_lengths)[*num_attrs]= a_acl_len;
   (*num_attrs)++;
 }

#endif /* Libisofs_with_aaip_acL */

 ret= 1;
ex:;
#ifdef Libisofs_with_aaip_acL
 if(a_acl != NULL)
   free(a_acl);
 if(a_acl_text != NULL)
   aaip_get_acl_text("", &a_acl_text, 1 << 15); /* free */
#endif
#ifdef Libisofs_with_freebsd_extattR
 if(list != NULL)
   free(list);
 if(user_list != NULL)
   free(user_list);
 if(sys_list != NULL)
   free(sys_list);
#endif

 if(ret <= 0 || (flag & (1 << 15))) {
   if(*names != NULL) {
     for(i= 0; (size_t) i < *num_attrs; i++)
       free((*names)[i]);
     free(*names);
   }
   *names= NULL;
   if(*value_lengths != NULL)
     free(*value_lengths);
   *value_lengths= NULL;
   if(*values != NULL) {
     for(i= 0; (size_t) i < *num_attrs; i++)
       free((*values)[i]);
     free(*values);
   }
   *values= NULL;
   *num_attrs= 0;
 }
 return(ret);
}

#else /* ! Libisofs_old_freebsd_acl_adapteR */

/* Obtain the Extended Attributes and/or the ACLs of the given file in a form
   that is ready for aaip_encode().

   Note: There are no Extended Attributes in FreeBSD. So only ACL will be
         obtained.

   @param path          Path to the file
   @param num_attrs     Will return the number of name-value pairs
   @param names         Will return an array of pointers to 0-terminated names
   @param value_lengths Will return an arry with the lenghts of values
   @param values        Will return an array of pointers to 8-bit values
   @param flag          Bitfield for control purposes
                        bit0=  obtain ACL (access and eventually default)
                        bit1=  use numeric ACL qualifiers rather than names
                        bit2=  do not encode attributes other than ACL
                        bit3=  do not ignore eventual non-user attributes
                               I.e. those which are not from name space
                               EXTATTR_NAMESPACE_USER
                        bit4=  do not return trivial ACL that matches st_mode
                        bit15= free memory of names, value_lengths, values
   @return              >0  ok
                        <=0 error
                        -1= out of memory
                        -2= program error with prediction of result size
                        -3= error with conversion of name to uid or gid
*/
int aaip_get_attr_list(char *path, size_t *num_attrs, char ***names,
                       size_t **value_lengths, char ***values, int flag)
{
 int ret;
 ssize_t i, num_names;

#ifdef Libisofs_with_aaip_acL
 size_t a_acl_len= 0;
 unsigned char *a_acl= NULL;
 char *acl_text= NULL;
#endif

 if(flag & (1 << 15)) { /* Free memory */
   {ret= 1; goto ex;}
 }

 *num_attrs= 0;
 *names= NULL;
 *value_lengths= NULL;
 *values= NULL;

 num_names= 0;
 if(flag & 1)
   num_names++;
 if(num_names == 0)
   {ret= 1; goto ex;}
 (*names)= calloc(num_names, sizeof(char *));
 (*value_lengths)= calloc(num_names, sizeof(size_t));
 (*values)= calloc(num_names, sizeof(char *));
 if(*names == NULL || *value_lengths == NULL || *values == NULL)
   {ret= -1; goto ex;}

 for(i= 0; i < num_names; i++) {
   (*names)[i]= NULL;
   (*values)[i]= NULL;
   (*value_lengths)[i]= 0;
 }

#ifdef Libisofs_with_aaip_acL

 if(flag & 1) { /* Obtain ACL */
   /* access-ACL */
   aaip_get_acl_text(path, &acl_text, flag & (16 | 32));
   if(acl_text == NULL)
     {ret= 1; goto ex;} /* empty ACL / only st_mode info was found in ACL */
   ret= aaip_encode_acl(acl_text, (mode_t) 0, &a_acl_len, &a_acl, flag & 2);
   if(ret <= 0)
     goto ex;
   aaip_get_acl_text("", &acl_text, 1 << 15); /* free */
   
   /* Note: There are no default-ACL in FreeBSD */

   /* Set as attribute with empty name */;
   (*names)[*num_attrs]= strdup("");
   if((*names)[*num_attrs] == NULL)
     {ret= -1; goto ex;}
   (*values)[*num_attrs]= (char *) a_acl;
   a_acl= NULL;
   (*value_lengths)[*num_attrs]= a_acl_len;
   (*num_attrs)++;
 }

#endif /* ! Libisofs_with_aaip_acL */

 ret= 1;
ex:;
#ifdef Libisofs_with_aaip_acL
 if(a_acl != NULL)
   free(a_acl);
 if(acl_text != NULL)
   aaip_get_acl_text("", &acl_text, 1 << 15); /* free */
#endif /* Libisofs_with_aaip_acL */

 if(ret <= 0 || (flag & (1 << 15))) {
   if(*names != NULL) {
     for(i= 0; i < (ssize_t) *num_attrs; i++)
       free((*names)[i]);
     free(*names);
   }
   *names= NULL;
   if(*value_lengths != NULL)
     free(*value_lengths);
   *value_lengths= NULL;
   if(*values != NULL) {
     for(i= 0; i < (ssize_t) *num_attrs; i++)
       free((*values)[i]);
     free(*values);
   }
   *values= NULL;
   *num_attrs= 0;
 }
 return(ret);
}

#endif /* Libisofs_old_freebsd_acl_adapteR */


/* ------------------------------ Setters --------------------------------- */


/* Set the ACL of the given file to a given list in long text form.
   @param path          Path to the file
   @param text          The input text (0 terminated, ACL long text form)
   @param flag          Bitfield for control purposes
                        bit0=  set default ACL rather than access ACL
                        bit5=  in case of symbolic link: manipulate link target
                        bit6= tolerate inappropriate presence or absence of
                              directory default ACL
   @return              > 0 ok
                         0 no suitable ACL manipulation adapter available
                        -1  failure of system ACL service (see errno)
                        -2 attempt to manipulate ACL of a symbolic link
                           without bit5 resp. with no suitable link target
*/
int aaip_set_acl_text(char *path, char *text, int flag)
{

#ifdef Libisofs_with_aaip_acL

 int ret;
 acl_t acl= NULL;
 struct stat stbuf;

 if(flag & 32)
   ret= stat(path, &stbuf);
 else
   ret= lstat(path, &stbuf);
 if(ret == -1)
   return(-1);
 if((stbuf.st_mode & S_IFMT) == S_IFLNK)
   return(-2);

 acl= acl_from_text(text);
 if(acl == NULL) {
   ret= -1; goto ex;
 }

 /* Note: no ACL_TYPE_DEFAULT in FreeBSD */
 if(flag & 1)
   {ret= 0; goto ex;}

 ret= acl_set_file(path, ACL_TYPE_ACCESS, acl);

 if(ret == -1)
   goto ex;
 ret= 1;
ex:
 if(acl != NULL)
   acl_free(acl);
 return(ret);

#else /* Libisofs_with_aaip_acL */

 return(0);

#endif /* ! Libisofs_with_aaip_acL */

}


#ifndef Libisofs_old_freebsd_acl_adapteR

#ifdef Libisofs_with_freebsd_extattR

/*
   @param flag          Bitfield for control purposes
                        bit5=  in case of symbolic link: manipulate link target
*/
static int aaip_extattr_delete_names(char *path, int attrnamespace,
                                     char *list, ssize_t list_size, int flag)
{
 int len;
 char name[256];
 ssize_t value_ret, i;

 for(i= 0; i < list_size; i+= len + 1) {
   len= *((unsigned char *) (list + i));
   if(len > 0)
     strncpy(name, list + i + 1, len);
   name[len]= 0;
   if(flag & 32)
     value_ret= extattr_delete_file(path, attrnamespace, name);
   else
     value_ret= extattr_delete_file(path, attrnamespace, name);
   if(value_ret == -1)
     return(0);
 }
 return(1);
}

#endif /* Libisofs_with_freebsd_extattR */


/* Bring the given attributes and/or ACLs into effect with the given file.
   @param flag          Bitfield for control purposes
                        bit0= decode and set ACLs
                        bit1= first clear all existing attributes of the file
                        bit2= do not set attributes other than ACLs
                        bit3= do not ignore eventual non-user attributes.
                              I.e. those with a name which does not begin
                              by "user."
                        bit5= in case of symbolic link: manipulate link target
                        bit6= tolerate inappropriate presence or absence of
                              directory default ACL
   @return              1 success
                       -1 error memory allocation
                       -2 error with decoding of ACL
                       -3 error with setting ACL
                       -4 error with setting attribute
                       -5 error with deleting attributes
                       -6 support of xattr not enabled at compile time
                       -7 support of ACL not enabled at compile time
                       -8 unsupported xattr namespace
    ISO_AAIP_ACL_MULT_OBJ multiple entries of user::, group::, other::
*/
int aaip_set_attr_list(char *path, size_t num_attrs, char **names,
                       size_t *value_lengths, char **values, int flag)
{
 int ret, has_default_acl= 0;
 size_t i, consumed, acl_text_fill, acl_idx= 0;
 char *acl_text= NULL;
#ifdef Libisofs_with_freebsd_extattR
 char *user_list= NULL, *sys_list= NULL, *namept;
 ssize_t user_list_size= 0, sys_list_size= 0;
 int attrnamespace;
#endif

#ifdef Libisofs_with_freebsd_extattR

 if(flag & 2) { /* Delete all file attributes */
   ret= aaip_extattr_make_list(path, EXTATTR_NAMESPACE_USER,
                               &user_list, &user_list_size, flag & 32);
   if(ret <= 0)
     {ret= -1; goto ex;}
   ret= aaip_extattr_delete_names(path, EXTATTR_NAMESPACE_USER,
                                  user_list, user_list_size, flag & 32);
   if(ret <= 0)
     {ret= -5; goto ex;}
   if(flag & 8) { 
     ret= aaip_extattr_make_list(path, EXTATTR_NAMESPACE_SYSTEM,
                                 &sys_list, &sys_list_size, flag & 32);
     if(ret <= 0)
       {ret= -5; goto ex;}
     ret= aaip_extattr_delete_names(path, EXTATTR_NAMESPACE_SYSTEM,
                                  sys_list, sys_list_size, flag & 32);
     if(ret <= 0)
       {ret= -5; goto ex;}
   }
 }

#endif /* Libisofs_with_freebsd_extattR */

 for(i= 0; i < num_attrs; i++) {
   if(names[i] == NULL || values[i] == NULL)
 continue;
   if(names[i][0] == 0) { /* ACLs */
     if(flag & 1)
       acl_idx= i + 1;
 continue;
   }
   /* Extended Attribute */
   if(flag & 4)
 continue;

#ifdef Libisofs_with_freebsd_extattR

   if(strncmp(names[i], "user.", 5) == 0) {
     attrnamespace= EXTATTR_NAMESPACE_USER;
     namept= names[i] + 5;
   } else if(strncmp(names[i], "isofs.", 6) == 0 || !(flag & 8)) {
 continue;
   } else if(strncmp(names[i], "system.", 7) == 0) {
     attrnamespace= EXTATTR_NAMESPACE_SYSTEM;
     namept= names[i] + 7;
   } else {
     {ret= -8; goto ex;}
   }
   if(flag & 32)
     ret= extattr_set_file(path, attrnamespace, namept,
                           values[i], value_lengths[i]);
   else
     ret= extattr_set_link(path, attrnamespace, namept,
                           values[i], value_lengths[i]);
   if(ret == -1)
     {ret= -4; goto ex;}

#else

   if(strncmp(names[i], "user.", 5) == 0)
     ;
   else if(strncmp(names[i], "isofs.", 6) == 0 || !(flag & 8))
 continue;
   {ret= -6; goto ex;}

#endif /* Libisofs_with_freebsd_extattR */

 }

 /* Decode ACLs */
 if(acl_idx == 0)
   {ret= 1; goto ex;}
 i= acl_idx - 1; 

 ret= aaip_decode_acl((unsigned char *) values[i], value_lengths[i],
                      &consumed, NULL, 0, &acl_text_fill, 1);
 if(ret < -3)
   goto ex;
 if(ret <= 0)
   {ret= -2; goto ex;}
 acl_text= calloc(acl_text_fill, 1);
 if(acl_text == NULL) 
   {ret= -1; goto ex;}
 ret= aaip_decode_acl((unsigned char *) values[i], value_lengths[i],
                      &consumed, acl_text, acl_text_fill, &acl_text_fill, 0);
 if(ret < -3)
   goto ex;
 if(ret <= 0)
   {ret= -2; goto ex;} 
 has_default_acl= (ret == 2);

#ifdef Libisofs_with_aaip_acL
 ret= aaip_set_acl_text(path, acl_text, flag & (32 | 64));
 if(ret <= 0)
   {ret= -3; goto ex;}
#else
 {ret= -7; goto ex;}
#endif

 if(has_default_acl && !(flag & 64))
   {ret= -3; goto ex;}

 ret= 1;
ex:;
 if(acl_text != NULL)
   free(acl_text);
#ifdef Libisofs_with_freebsd_extattR
 if(user_list != NULL)
   free(user_list);
 if(sys_list != NULL)
   free(sys_list);
#endif /* Libisofs_with_freebsd_extattR */
 return(ret);
}

#else /* ! Libisofs_old_freebsd_acl_adapteR */


/* Bring the given attributes and/or ACLs into effect with the given file.

   Note: There are no Extended Attributes in FreeBSD. So only ACL get set.

   @param flag          Bitfield for control purposes
                        bit0= decode and set ACLs
                      ( bit1= first clear all existing attributes of the file )
                      ( bit2= do not set attributes other than ACLs )
                      ( bit3= do not ignore eventual non-user attributes.
                              I.e. those with a name which does not begin
                              by "user." )
   @return              1 success
                       -1 error memory allocation
                       -2 error with decoding of ACL
                       -3 error with setting ACL
                     ( -4 error with setting attribute )
                     ( -5 error with deleting attribute )
                       -6 support of xattr not enabled at compile time
                       -7 support of ACL not enabled at compile time
*/
int aaip_set_attr_list(char *path, size_t num_attrs, char **names,
                       size_t *value_lengths, char **values, int flag)
{
 int ret, has_default_acl= 0, was_xattr= 0;
 size_t i, consumed, acl_text_fill;
 char *acl_text= NULL, *list= NULL;

 for(i= 0; i < num_attrs; i++) {
   if(names[i] == NULL || values[i] == NULL)
 continue;
   if(names[i][0] == 0) { /* Decode ACLs */
     /* access ACL */
     if(!(flag & 1))
 continue;
     ret= aaip_decode_acl((unsigned char *) values[i], value_lengths[i],
                          &consumed, NULL, 0, &acl_text_fill, 1);
     if(ret <= 0)
       {ret= -2; goto ex;}
     acl_text= calloc(acl_text_fill, 1);
     if(acl_text == NULL)
       {ret= -1; goto ex;}
     ret= aaip_decode_acl((unsigned char *) values[i], value_lengths[i],
                       &consumed, acl_text, acl_text_fill, &acl_text_fill, 0);
     if(ret <= 0)
       {ret= -2; goto ex;}
     has_default_acl= (ret == 2);

#ifdef Libisofs_with_aaip_acL
     ret= aaip_set_acl_text(path, acl_text, flag & 32);
     if(ret <= 0)
       {ret= -3; goto ex;}
#else
     {ret= -7; goto ex;}
#endif
                                                            /* "default" ACL */
     if(has_default_acl) {
       free(acl_text);
       acl_text= NULL;
       ret= aaip_decode_acl((unsigned char *) (values[i] + consumed),
                            value_lengths[i] - consumed, &consumed,
                            NULL, 0, &acl_text_fill, 1);
       if(ret <= 0)
         {ret= -2; goto ex;}
       acl_text= calloc(acl_text_fill, 1);
       if(acl_text == NULL)
         {ret= -1; goto ex;}
       ret= aaip_decode_acl((unsigned char *) (values[i] + consumed),
                            value_lengths[i] - consumed, &consumed,
                            acl_text, acl_text_fill, &acl_text_fill, 0);
       if(ret <= 0)
         {ret= -2; goto ex;}
       ret= aaip_set_acl_text(path, acl_text, 1 | (flag & 32));
       if(ret <= 0)
         {ret= -3; goto ex;}
     }
   } else {
     if(flag & 4)
 continue;
     if(!(flag & 8))
       if(strncmp(names[i], "user.", 5))
 continue;
     was_xattr= 1;
   }
 }
 ret= 1;
 if(was_xattr)
   ret= -6;
ex:;
 if(acl_text != NULL)
   free(acl_text);
 if(list != NULL)
   free(list);
 return(ret);
}

#endif /* Libisofs_old_freebsd_acl_adapteR */

