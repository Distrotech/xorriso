
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2012 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains declarations of functions around files and strings.
*/


#ifndef Xorriso_pvt_sfile_includeD
#define Xorriso_pvt_sfile_includeD yes

#define TSOB_FELD(typ, count) (typ *) calloc(1, (count)*sizeof(typ));
#define Smem_malloC malloc
#define Smem_freE free

#define Xorriso_alloc_meM(pt, typ, count) { \
  pt= (typ *) calloc(1, (count) * sizeof(typ)); \
  if(pt == NULL) { \
    Xorriso_no_malloc_memory(xorriso, NULL, 0); \
    ret= -1; \
    goto ex; \
  } }

#define Xorriso_free_meM(pt) { \
  if(pt != NULL) \
    free((char *) pt); \
  }


#define SfileadrL 4096


int Sfile_str(char target[SfileadrL], char *source, int flag); 
 
double Sfile_microtime(int flag);
  
int Sfile_add_to_path(char path[SfileadrL], char *addon, int flag);
  
int Sfile_scale(double value, char *result, int siz, double thresh, int flag);
  
int Sfile_destroy_argv(int *argc, char ***argv, int flag);

int Sfile_count_char(char *text, char to_count);

 
/*
 bit0= do not ignore trailing slash
 bit1= do not ignore empty components (other than the empty root name)
*/
int Sfile_count_components(char *path, int flag);

/*
 @param flag
 bit0= return -1 if file is missing
 bit1= return a hardlink with siblings as type 5
 bit2= evaluate eventual link target rather than the link object itself
 bit3= return a socket or a char device as types 7 or 8 rather than 0
 @return
  0=unknown
  1=regular
  2=directory
  3=symbolic link
  4=named pipe
  5=multiple hardlink (with bit1)
  6=block device
  7=socket (with bit3)
  8=character device (with bit3)
*/
int Sfile_type(char *filename, int flag);

/* @param flag bit0= only encode inside quotes
               bit1= encode < 32 outside quotes except 7, 8, 9, 10, 12, 13
               bit2= encode in any case above 126
               bit3= encode in any case shellsafe:
                     <=42 , 59, 60, 62, 63, 92, 94, 96, >=123
*/
int Sfile_bsl_encoder(char **result, char *text, size_t text_len, int flag);

int Sfile_argv_bsl(int argc, char ***argv, int flag);

/*
 bit0= read progname as first argument from line
 bit1= just release argument list argv and return
 bit2= abort with return(0) if incomplete quotes are found
 bit3= eventually prepend missing '-' to first argument read from line
 bit4= like bit2 but only check quote completeness, do not allocate memory
 bit5+6= interpretation of backslashes:
       0= no interpretation, leave unchanged
       1= only inside double quotes
       2= outside single quotes
       3= everywhere
 bit7= append a NULL element to argv
*/
int Sfile_make_argv(char *progname, char *line, int *argc, char ***argv,
                    int flag);
int Sfile_sep_make_argv(char *progname, char *line, char *separators,
                        int max_argc, int *argc, char ***argv, int flag);

/* YYMMDD[.hhmm[ss]] */
int Sfile_decode_datestr(struct tm *reply, char *text, int flag);

int Sfile_off_t_text(char text[80], off_t num, int flag);

int Sfile_leafname(char *path, char leafname[SfileadrL], int flag);

/* @param flag bit0= do not clip of carriage return at line end
*/
char *Sfile_fgets_n(char *line, int maxl, FILE *fp, int flag);

/*
 bit0=with hours+minutes
 bit1=with seconds

 bit8= local time rather than UTC
*/
char *Sfile_datestr(time_t tim, short int flag);

/* Converts backslash codes into single characters:
    \a BEL 7 , \b BS 8 , \e ESC 27 , \f FF 12 , \n LF 10 , \r CR 13 ,
    \t  HT 9 , \v VT 11 , \\ \ 92
    \[0-9][0-9][0-9] octal code , \x[0-9a-f][0-9a-f] hex code ,
    \cX control-x (ascii(X)-64)
   @param upto  maximum number of characters to examine for backslash.
                The scope of a backslash (0 to 3 characters) is not affected.
   @param eaten returns the difference in length between input and output
   @param flag bit0= only determine *eaten, do not convert
               bit1= allow to convert \000 to binary 0
*/
int Sfile_bsl_interpreter(char *text, int upto, int *eaten, int flag);

int Sfile_prepend_path(char *prefix, char path[SfileadrL], int flag);

int Sfile_home_adr_s(char *filename, char *fileadr, int fa_size, int flag);


#endif /* ! Xorriso_pvt_sfile_includeD */

