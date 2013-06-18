
/*

 Arbitrary Attribute Interchange Protocol , AAIP versions 0.2 and 1.0.
 Implementation for encoding and decoding xattr and ACL.

 See http://libburnia-project.org/wiki/AAIP
 or  doc/susp_aaip_2_0.txt

 test/aaip_0_2.h - Public declarations

 Copyright (c) 2009 Thomas Schmitt, libburnia project, GPLv2+

*/

#ifndef Aaip_h_is_includeD
#define Aaip_h_is_includeD yes


/* --------------------------------- Encoder ---------------------------- */

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
                   size_t *result_len, unsigned char **result, int flag);


/* ------ ACL representation ------ */

/* Convert an ACL from long text form into the value of an Arbitrary
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
                    size_t *result_len, unsigned char **result, int flag);


/* Convert an "access" and "default" ACL from long text form into the value
   of an Arbitrary Attribute. According to AAIP this value is to be stored
   together with an empty name.
   @param a_acl_text    The "access" ACL in long text form.
                        Submit NULL if there is no such ACL to be encoded.
   @param d_acl_text    The "default" ACL in long text form.
                        Submit NULL if there is no such ACL to be encoded.
   @param st_mode       The stat(2) permission bits to be used with flag bit3
   @param result_len    Number of bytes in the resulting value
   @param result        *result will point to the start of the result string.
                        This is malloc() memory which needs to be freed when
                        no longer needed
   @param flag          Bitfield for control purposes
                        bit0= count only
                        bit1= use numeric qualifiers rather than names
                        bit3= check for completeness of list and eventually
                              fill up with entries deduced from st_mode
   @return              >0 means ok
                        <=0 means error, see aaip_encode_acl
*/
int aaip_encode_both_acl(char *a_acl_text, char *d_acl_text, mode_t st_mode,
                         size_t *result_len, unsigned char **result, int flag);


/* Analyze occurence of ACL tag types in long text form. If not disabled by
   parameter flag remove the entries of type "user::" , "group::" , "other::" ,
   or "other:" from an ACL in long text form if they match the bits in st_mode
   as described by man 2 stat and man 5 acl.
   @param acl_text   The text to be analyzed and eventually shortened.
   @param st_mode    The component of struct stat which tells permission
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
                         bit10= "group::" found and "mask::" exists
                         
*/
int aaip_cleanout_st_mode(char *acl_text, mode_t *st_mode, int flag);


/* Append entries of type "user::" , "group::" , "other::" representing the
   permission bits in st_mode if those tag types are not present in the ACL
   text. Append "mask::" if missing although "user:...:" or "group:...:"
   is present. Eventually set it to S_IRWXG bits of st_mode.
   @param acl_text   The text to be made longer. It must offer 43 bytes more
                     storage space than its length when it is submitted.
   @param st_mode    The component of struct stat which shall provide the
                     permission information.
   @param flag       Unused yet. Submit 0.
   @return           <0 failure
*/
int aaip_add_acl_st_mode(char *acl_text, mode_t st_mode, int flag);


/* ------ OS interface ------ */

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
int aaip_local_attr_support(int flag);


/* Obtain the ACL of the given file in long text form.
   @param path          Path to the file
   @param text          Will hold the result. This is a managed object which
                        finally has to be freed by a call to this function
                        with bit15 of flag.
   @param flag          Bitfield for control purposes
                        bit0=  obtain default ACL rather than access ACL
                        bit4=  set *text = NULL and return 2
                               if the ACL matches st_mode permissions.
                        bit15= free text and return 1
   @return               1 ok
                         2 only st_mode permissions exist and bit 4 is set
                         0 ACL support not enabled at compile time
                        -1 failure of system ACL service (see errno)
*/
int aaip_get_acl_text(char *path, char **text, int flag);


/* Obtain the Extended Attributes and/or the ACLs of the given file in a form
   that is ready for aaip_encode(). The returned data objects finally have
   to be freed by a call with flag bit 15.
   @param path          Path to the file
   @param num_attrs     Will return the number of name-value pairs
   @param names         Will return an array of pointers to 0-terminated names
   @param value_lengths Will return an arry with the lenghts of values
   @param values        Will return an array of pointers to 8-bit values
   @param flag          Bitfield for control purposes
                        bit0=  obtain ACLs (access and eventually default) via
                               system ACL API and encode 
                        bit1=  use numeric ACL qualifiers rather than names
                        bit2=  do not obtain attributes other than ACLs
                        bit3=  do not ignore eventual non-user attributes.
                               I.e. those with a name which does not begin
                               by "user."
                        bit4=  do not return trivial ACL that matches st_mode
                        bit15= free memory of names, value_lengths, values
   @return              >0  ok
                        <=0 error
*/
int aaip_get_attr_list(char *path, size_t *num_attrs, char ***names,
                       size_t **value_lengths, char ***values, int flag);


/* --------------------------------- Decoder ---------------------------- */

/*
   The AAIP decoder offers several levels of abstraction of which the
   lower two avoid the use of dynamic memory. It provides a stateful decoding
   context with a small buffer which delivers results to caller provided
   memory locations.

   The lowest level is the stream-like Component Level Interface. It allows
   to decode very many very long attributes.
 
   Next is the Pair Level Interface which delivers to fixly sized storage for
   name and value. It allows to decode very many attributes. 

   The List Level Interface uses dynamic memory allocation to provide arrays
   of names, values and value lengths. It is intended for moderately sized
   attribute lists but may also be used as alternative to Pair Level.
*/

/* Operations on complete AAIP field strings which need no decoder context.
   These function expect to get submitted a complete chain of AAIP fields.
*/

/* Determine the size of the AAIP string by interpreting the SUSP structure.
   @param data          An arbitrary number of bytes beginning with the
                        complete chain of AAIP fields. Trailing trash is
                        ignored.
   @param flag          Unused yet. Submit 0.
   @return              The number of bytes of the AAIP field chain.
*/
size_t aaip_count_bytes(unsigned char *data, int flag);


/* The AAIP decoder context.
*/
struct aaip_state;


/* Obtain the size in bytes of an aaip_state object.
*/
size_t aaip_sizeof_aaip_state(void);


/* Initialize a AAIP decoder context.
   This has to be done before the first AAIP field of a node is processed.
   The caller has to provide the storage of the struct aaip_state.
   @param aaip          The AAIP decoder context to be initialized
   @param flag          Bitfield for control purposes
                        submit 0
   @return <=0 error , >0 ok
*/
int aaip_init_aaip_state(struct aaip_state *aaip, int flag);


/* -------------------------  Component Level Interface  ------------------- */
/* 
   Provides support for unlimited component size but demands the caller
   to have a growing storage facility resp. to do own oversize handling.

   This interface expects moderatly sized input pieces and will hand out
   moderately sized result pieces. The number of transactions is virtually
   unlimited. 
*/

/* Submit small data chunk for decoding.
   The return value will tell whether data are pending for being fetched.
   @param aaip          The AAIP decoder context
   @param data          Not more than 2048 bytes input for the decoder
   @param num_data      Number of bytes in data
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
                     size_t *ready_bytes, int flag);


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
                    int flag);


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
int aaip_skip_component(struct aaip_state *aaip, int flag);


/* -------------------------  Pair Level Interface  ------------------------ */
/*
   Provides support for names and values of limited size. The limits are
   given by the caller who has to provide the storage for name and value.

   This interface expects moderatly sized input pieces.
   The number of input transcations is virtually unlimited.
   The number of pair transactions after aaip_init() should be limited
   to 4 billion.
*/


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
   @param flag          Bitfield for control purposes - submit 0 for now
   @return <0 error
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
                     int flag);


/* Inquire the number of pairs which were skipped because being oversized.
   @param aaip          The AAIP decoder context
   @param flag          Bitfield for control purposes - submit 0 for now
   @return The number of pairs skipped since aaip_init()
*/
unsigned int aaip_get_pairs_skipped(struct aaip_state *aaip, int flag);


/* -------------------------  List Level Interface  ------------------------ */
/*
   Provides support for names and values of limited size. The limits are
   given for total memory consumption and for number of attributes.

   Iterated decoding is supported as long as no single attribute exceeds
   the memory limit.
*/

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
                        bit0=  this is the first call for a file object
                        bit15= end decoding :
                               Free handle and its intermediate list memory.
   @return <=0 error
             1 not complete yet, submit more data
             2 arrays are complete, call aaip_get_decoded_attrs()
             3 limit exceeded, not complete yet, call with bit15 and give up
             4 limit exceeded, call aaip_get_decoded_attrs() and try again
*/
int aaip_decode_attrs(struct aaip_state **handle,
                      size_t memory_limit, size_t num_attr_limit,
                      unsigned char *data, size_t num_data, size_t *consumed, 
                      int flag);


/* Obtain the resulting attributes when aaip_decode_attrs() indicates to
   be done or to have the maximum possible amount of result ready.
   The returned data objects get detached from handle making it ready for
   the next round of decoding with possibly a different input source. The
   returned data objects finally have to be freed by a call with flag bit 15.
   @param handle        The decoding context created by aaip_decode_attrs()
   @param num_attrs     Will return the number of name-value pairs
   @param names         Will return an array of pointers to 0-terminated names
   @param value_lengths Will return an arry with the lenghts of values
   @param values        Will return an array of pointers to 8-bit values
   @param flag          Bitfield for control purposes
                        bit15= free memory of names, value_lengths, values
*/
int aaip_get_decoded_attrs(struct aaip_state **handle, size_t *num_attrs,
                         char ***names, size_t **value_lengths, char ***values,
                         int flag);


/* ------ ACL representation ------ */

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
                    size_t *acl_text_fill, int flag);


/* ------ OS interface ------ */

/* Set the ACL of the given file to a given list in long text form.
   @param path          Path to the file
   @param text          The input text (0 terminated, ACL long text form)
   @param flag          Bitfield for control purposes
                        bit0=  set default ACL rather than access ACL
   @return              >0 ok
                         0 ACL support not enabled at compile time
                        -1 failure of system ACL service (see errno)
*/
int aaip_set_acl_text(char *path, char *text, int flag);


/* Bring the given attributes and/or ACLs into effect with the given file.
   @param path          Path to the file
   @param num_attrs     Number of attributes
   @param names         Array of pointers to 0 terminated name strings
   @param value_lengths Array of byte lengths for each attribute payload
   @param values        Array of pointers to the attribute payload bytes
   @param flag          Bitfield for control purposes
                        bit0= decode and set ACLs
                        bit1= first clear all existing attributes of the file
                        bit2= do not set attributes other than ACLs
                        bit3= do not ignore eventual non-user attributes.
                              I.e. those with a name which does not begin
                              by "user."
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
                       size_t *value_lengths, char **values, int flag);

#endif /* ! Aaip_h_is_includeD */

