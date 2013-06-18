
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2012 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of cellaneous helper functions of xorriso.
*/


#ifndef Xorriso_pvt_misc_includeD
#define Xorriso_pvt_misc_includeD yes

#include <regex.h>


char *Text_shellsafe(char *in_text, char *out_text, int flag);

int Sort_argv(int argc, char **argv, int flag);

/* @param flag bit0= single letters */
char *Ftypetxt(mode_t st_mode, int flag);

/* @param flag bit0=with year and seconds
               bit1=timestamp format YYYY.MM.DD.hhmmss
*/
char *Ftimetxt(time_t t, char timetext[40], int flag);

int System_uname(char **sysname, char **release, char **version, 
                 char **machine, int flag);

/** Convert a text into a number of type double and multiply it by unit code
    [kmgtpe] (2^10 to 2^60) or [s] (2048). (Also accepts capital letters.)
    @param text Input like "42", "2k", "3.14m" or "-1g"
    @param flag Bitfield for control purposes:
                bit0= return -1 rathern than 0 on failure
    @return The derived double value
*/
double Scanf_io_size(char *text, int flag);

/*
   @flag        bit0= do not initialize *diff_count
   @return  <0 error , 0 = mismatch , 1 = match
*/
int Compare_text_lines(char *text1, char *text2, int *diff_count, int flag);

time_t Decode_timestring(char *code, time_t *date, int flag);

int Decode_ecma119_format(struct tm *erg, char *text, int flag);

int Wait_for_input(int fd, int microsec, int flag);

int Fileliste__target_source_limit(char *line, char sep, char **limit_pt,
                                    int flag);

int Fileliste__escape_source_path(char *line, int size, int flag);

int Hex_to_bin(char *hex,
              int bin_size, int *bin_count, unsigned char *bin_data, int flag);


/* bit0= append (text!=NULL) */
int Sregex_string(char **handle, char *text, int flag);

/* @param flag bit0= only test expression whether compilable
*/
int Sregex_match(char *pattern, char *text, int flag);

/*
  vars[][0] points to the variable names, vars[][1] to their contents.
  start marks the begin of variable names. It must be non-empty. esc before
  start disables this meaning. start and esc may be equal but else they must
  have disjoint character sets.
  end marks the end of a variable name. It may be empty but if non-empty it
  must not appear in vars[][0].
  @param flag bit0= Substitute unknown variables by empty text
                    (else copy start,name,end unaltered to result).
                    Parameter end must be non-empty for that.
*/
int Sregex_resolve_var(char *form, char *vars[][2], int num_vars,
                       char *start, char *end, char *esc,
                       char *result, int result_size, int flag);

/* reg_expr should be twice as large as bourne_expr ( + 2 to be exact) */
/* return: 2= bourne_expr is surely a constant */
int Xorriso__bourne_to_reg(char bourne_expr[], char reg_expr[], int flag);


int Xorriso__hide_mode(char *mode, int flag);

char *Xorriso__hide_mode_text(int hide_mode, int flag);

/* @return 0=truncated, 1=ok
*/
int Xorriso__to_upper(char *in, char *out, int out_size, int flag);

#endif /* ! Xorriso_pvt_misc_includeD */

