
/*

 aaip-os-dummy.c

 Idle placeholder for:
 Arbitrary Attribute Interchange Protocol , system adapter for getting and
 setting of ACLs and xattr.

 See aaip-os-linux.c for a real implementation of this interface.

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
 return(0);
}


/* ------------------------------ Getters --------------------------------- */

/* Obtain the ACL of the given file in long text form.
   @return          0 ACL support not enabled at compile time
*/
int aaip_get_acl_text(char *path, char **text, int flag)
{
 return(0);
}


/* Obtain the Extended Attributes and/or the ACLs of the given file in a form
   that is ready for aaip_encode().
   @return              1   ok
*/
int aaip_get_attr_list(char *path, size_t *num_attrs, char ***names,
                       size_t **value_lengths, char ***values, int flag)
{
 *num_attrs= 0;
 *names= NULL;
 *value_lengths= NULL;
 *values= NULL;
 return(1);
}


/* ------------------------------ Setters --------------------------------- */


/* Set the ACL of the given file to a given list in long text form.
   @return              0 ACL support not enabled at compile time
*/
int aaip_set_acl_text(char *path, char *text, int flag)
{
 return(0);
}


/* Bring the given attributes and/or ACLs into effect with the given file.
   @param flag          Bitfield for control purposes
                        bit0= decode and set ACLs
                        bit1= first clear all existing attributes of the file
                        bit2= do not set attributes other than ACLs
   @return              1 success (there was nothing to do)
                       -6 support of xattr not enabled at compile time
                       -7 support of ACL not enabled at compile time
*/
int aaip_set_attr_list(char *path, size_t num_attrs, char **names,
                       size_t *value_lengths, char **values, int flag)
{
 size_t i;

 for(i= 0; i < num_attrs; i++) {
   if(names[i] == NULL || values[i] == NULL)
 continue;
   if(names[i][0] == 0) { /* ACLs */
     if(flag & 1)
       return(-7);
 continue;
   }
   /* Extended Attribute */
   if(flag & 4)
 continue;
   if(!(flag & 8))
     if(strncmp(names[i], "user.", 5))
 continue;
   return(-6);
 }
 if(flag & 2)
   return(-6);
 return(1);
}


