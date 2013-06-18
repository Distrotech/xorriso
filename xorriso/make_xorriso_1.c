
/*
  ( cd xorriso ; cc -g -Wall -o make_xorriso_1 make_xorriso_1.c )
*/
/*
   Specialized converter from xorriso/xorriso.texi to xorriso/xorriso.1
   resp. from xorriso/xorrisofs.texi to xorriso/xorrisofs.1

   The conversion rules are described at the beginning of xorriso/xorriso.texi

   Copyright 2010 - 2011 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.
*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>


/* The conversion state
*/
struct Mx1 {

 char prog[4096];

 int count_in;
 int count_out;

 int skipping;          /* <0 stacked skipping , 0= no , >0 counting down */
 
};

int Mx1_substitute(struct Mx1 *m, char line_in[256], char line_out[256],
                   int raw, int upto, int flag);



int Mx1_init(struct Mx1 *m, char *prog, int flag)
{
 strncpy(m->prog, prog, sizeof(m->prog) - 1);
 m->prog[sizeof(m->prog) - 1]= 0;
 m->count_in= 0;
 m->count_out= 0;
 m->skipping= 0;
 return(1);
}


int Mx1_report_error(struct Mx1 *m, char *text, int flag)
{
 fprintf(stderr, "%s : line %d : %s\n", m->prog, m->count_in, text);
 return(1);
}


int Mx1__get_word(char *line, char word[256], char **remainder, int flag)
{
 char *cpt, *start;
 int l;

 word[0]= 0;
 *remainder= NULL;
 for(cpt= line; *cpt != 0 && isspace(*cpt); cpt++);
 if(*cpt == 0)
   return(0);
 start= cpt;
 for(cpt= line; *cpt != 0 && ! isspace(*cpt); cpt++);
 l= cpt - start;
 if(l > 0)
   strncpy(word, start, l);
 word[l]= 0;
 *remainder= cpt;
 return(1);
}


int Mx1_is_wrap(struct Mx1 *m, char wraps[][20], char *start, char **found,
                int flag)
{
 int i;

 for(i= 0; wraps[i][0] != 0; i++)
   if(strncmp(start, wraps[i], strlen(wraps[i])) == 0)
 break;
 if(wraps[i][0] != 0) {
   if(found != NULL)
      *found= wraps[i];
   return(1);
 }
 return(0);
}


int Mx1_is_bold_wrap(struct Mx1 *m, char *start, char **found, int flag)
{
 int ret;
 static char bold_wraps[][20]= {
   "@b{", "@dfn{", "@emph{", "@strong{", "@command{",
   "" };
 
 ret= Mx1_is_wrap(m, bold_wraps, start, found, 0);
 return(ret);
}


int Mx1_is_normal_wrap(struct Mx1 *m, char *start, char **found, int flag)
{
 int ret;
 static char normal_wraps[][20]= {
   "@var{", "@code{", "@i{", "@abbr{", "@file{", "@option{", "@samp{", "@r{",
    "" };
 
 ret= Mx1_is_wrap(m, normal_wraps, start, found, 0);
 return(ret);
}


int Mx1_is_ignored_wrap(struct Mx1 *m, char *start, char **found, int flag)
{
 int ret;
 static char ignored_wraps[][20]= {
   "@ref{", "@xref{",
    "" };
 
 ret= Mx1_is_wrap(m, ignored_wraps, start, found, 0);
 return(ret);
}


int Mx1_is_any_wrap(struct Mx1 *m, char *start, char **found, int flag)
{
 int ret;

 ret= Mx1_is_bold_wrap(m, start, found, 0);
 if(ret > 0)
   return(1);
 ret= Mx1_is_normal_wrap(m, start, found, 0);
 if(ret > 0)
   return(2);
 ret= Mx1_is_ignored_wrap(m, start, found, 0);
 if(ret > 0)
   return(3);
 return(0);
}


/* @param flag bit0= recursion
               bit1= drop content of brackets
*/
int Mx1_rewrap(struct Mx1 *m, char **read_pt, char **write_pt,
                char *write_base, char *envelope,
                char *front, char *back, int flag)
{
 char *rpt, *wpt, *ept, content[256], msg[256];
 int l, ret;

 rpt= *read_pt;
 wpt= *write_pt;

 ept= strchr(rpt, '}');
 if(ept == NULL) {
   sprintf(msg, "No closing bracket found for '%s'", envelope);
   Mx1_report_error(m, msg, 0);
   return(-1);
 }
 /* Mapped {...} content is subject to the rules except {...} mapping. */
 l= ept - rpt;
 if(flag & 2)
   l= 0;
 if(l > 0) {
   ret= Mx1_substitute(m, rpt, content, 0, l, 1);
   if(ret <= 0)
     return(ret);
   l= strlen(content);
 }
 if((wpt - write_base) + l + strlen(front) + strlen(back) > 255) {
   Mx1_report_error(m, "Line length overflow while text substitution", 0);
   return(-1);
 }
 strcpy(wpt, front);
 wpt+= strlen(front);
 if(l > 0)
   strncpy(wpt, content, l);
 wpt+= l;
 strcpy(wpt, back);
 wpt+= strlen(back);

 (*read_pt)+= ept - rpt;
 (*write_pt)= wpt;
 return(1);
}


/* @param flag bit0= recursion
*/
int Mx1_substitute(struct Mx1 *m, char line_in[256], char line_out[256],
                   int raw, int upto, int flag)
{
 char *rpt, *wpt, *found;
 int ret, typ= 0;

 wpt= line_out;
 for(rpt= line_in; rpt - line_in < upto && *rpt != 0; rpt++) {
   if(rpt - line_in < raw) {
     *(wpt++)= *rpt;
 continue;
   } 
   if(*rpt == '@') {
     typ= 0;
     if(!(flag & 1))
       typ= Mx1_is_any_wrap(m, rpt, &found, 0);
     if(typ == 1) {
       /* @b{...}, @command{...}, @dfn{...}, @emph{...}, @strong{...}
            get mapped to \fB...\fR .
       */
       rpt+= strlen(found);
       ret= Mx1_rewrap(m, &rpt, &wpt, line_out,
                        found , "\\fB", "\\fR", flag & 1);
       if(ret <= 0)
         return(ret);

     } else if(typ == 2) {
       /*  @abbr{...}, @code{...}, @file{...}, @i{...}, @option{...}, @r{...},
             @ref{...}, @samp{...},@var{...}, get mapped to ... .
       */
       rpt+= strlen(found);
       ret= Mx1_rewrap(m, &rpt, &wpt, line_out, found, "", "", flag & 1);
       if(ret <= 0)
         return(ret);

     } else if(typ == 3) {
       /* @ref{...}, @xref{...} get mapped to empty text.
       */
       rpt+= strlen(found);
       ret= Mx1_rewrap(m, &rpt, &wpt, line_out, found , "", "",
                       (flag & 1) | 2);
       if(ret <= 0)
         return(ret);

     } else if(strncmp(rpt, "@email{", 7) == 0 && !(flag & 1)) {
       /* @email{...} gets mapped to <...> . */
       rpt+= 7;
       ret= Mx1_rewrap(m, &rpt, &wpt, line_out, "@email{", "<", ">", 0);
       if(ret <= 0)
         return(ret);

     } else if(strncmp(rpt, "@minus{}", 8) == 0) {
       /* @minus{} will become "-". */
       if((wpt - line_out) + 1 > 255)
         goto overflow;
       *(wpt++)= '-';
       rpt+= 7;

     } else if(strncmp(rpt, "@@", 2) == 0 ||
               strncmp(rpt, "@{", 2) == 0 ||
               strncmp(rpt, "@}", 2) == 0) {
       /* @@ , @{, @} will get stripped of their first @. */
       if((wpt - line_out) + 1 > 255)
         goto overflow;
       *(wpt++)= *(rpt + 1);
       rpt++;

     } else {
       if((wpt - line_out) + 1 > 255)
         goto overflow;
       *(wpt++)= *(rpt);

     }

   } else if(*rpt == '\\') {
     /* "\" becomes "\\" */
     if((wpt - line_out) + 2 > 255)
       goto overflow;
     *(wpt++)= '\\';
     *(wpt++)= '\\';

   } else if((wpt - line_out) + 1 > 255) {
overflow:;
     Mx1_report_error(m, "Line length overflow while text substitution", 0);
     return(-1);
   } else
     *(wpt++)= *rpt;

 }
 *wpt= 0;
 return(1);
}


/*
  @return 1= line_out is valid, 0= do not put out line_out, -1 = error
*/
int Mx1_convert(struct Mx1 *m, char line_in[256], char line_out[256], int flag)
{
 int l, num, keep= 0, ret, raw, i, backslash_count;
 char word[256], buf[256], *remainder, *wpt;

 m->count_in++;
 l= strlen(line_in);

 if(m->skipping > 0) {
   m->skipping--;
   return(0);
 }

 /* The first line gets discarded. */
 if(m->count_in == 1)
   return(0);

 /* Line start "@c man " will become "", the remainder is put out unaltered. */
 if(strncmp(line_in, "@c man ", 7) == 0) {
   strcpy(line_out, line_in + 7);
   m->count_out++;
   return(1);
 }

 /* Lines "@*" will be converted to ".br" */
 if(strcmp(line_in, "@*") == 0) {
   strcpy(line_out, ".br");
   m->count_out++;
   return(1);
 }

 /* @c man-ignore-lines N  will discard N following lines.
    "@c man-ignore-lines begin" discards all following lines
    up to "@c man-ignore-lines end".
 */
 if(strncmp(line_in, "@c man-ignore-lines ", 20) == 0) {
   if(strcmp(line_in + 20, "begin") == 0) {
     m->skipping--;
     return(0);
   } else if(strcmp(line_in + 20, "end") == 0) {
     if(m->skipping < 0)
       m->skipping++;
     return(0);
   } else if(m->skipping == 0) {
     num= 0;
     sscanf(line_in + 20, "%d", &num);
     if(num > 0) {
       m->skipping= num;
       return(0);
     }
   }
   Mx1_report_error(m, "Inappropriate use of '@c man-ignore-lines'", 0);
   return(-1);
 }

 /* Line blocks of "@menu" "@end menu" will be discarded. */
 if(strcmp(line_in, "@menu") == 0) {
   m->skipping--;
   return(0);
 }
 if(strcmp(line_in, "@end menu") == 0) {
   if(m->skipping < 0)
      m->skipping++;
   return(0);
 }
 if(m->skipping)
   return(0);

 /* "@item -word words" becomes "\fB\-word\fR words". */
 /* "@item word words" becomes "\fBword\fR words". */
 if(strncmp(line_in, "@item ", 6) == 0) {
   ret= Mx1__get_word(line_in + 6, word, &remainder, 0);
   if(ret <= 0) {
     Mx1_report_error(m, "Found no word after @item", 0);
     return(0);
   }
   strcpy(buf, "\\fB");
   if(word[0] == '-') {
     if(l >= 255) {
       Mx1_report_error(m, "Line length overflow while converting @item", 0);
       return(-1);
     }
     strcat(buf, "\\");
   }

   /* Substitute option text */
   raw= strlen(buf);
   strcat(buf, word);
   ret= Mx1_substitute(m, buf, line_out, raw, strlen(buf), 0);
   if(ret <= 0)
     return(-1);
   strcpy(buf, line_out);

   strcat(buf, "\\fR");
   raw= strlen(buf);
   strcat(buf, remainder);

   /* Substitute arguments text */
   ret= Mx1_substitute(m, buf, line_out, raw, strlen(buf), 0);
   if(ret <= 0)
     return(-1);
   m->count_out++;
   return(1);
 }

 /* @strong{... } gets mapped to \fB...\fR . */
 /* @command{... } gets mapped to \fB...\fR . */
 /* @minus{} will become "-". */
 /* Mapped {...} content is subject to the rules except {...} mapping. */
 /* @@ , @{, @} will get stripped of their first @. */
 /* "\" becomes "\\" */

 if(line_in[0] != '@' ||
    Mx1_is_any_wrap(m, line_in, NULL, 0) > 0 ||
    strncmp(line_in, "@minus{}", 8) == 0 ||
    strncmp(line_in, "@@", 2) == 0 ||
    strncmp(line_in, "@{", 2) == 0 ||
    strncmp(line_in, "@}", 2) == 0 ) {
   keep= 1;
   ret= Mx1_substitute(m, line_in, line_out, 0, strlen(line_in), 0);
   if(ret <= 0)
     return(-1);
 }

 /* Other lines which begin by "@" will be discarded. */
 if(! keep) {
   if(line_in[0] == '@')
     return(0);
   strcpy(line_out, line_in);
 }

 /* "-" which are not preceded by an uneven number of "\"  will get
    prepended one "\".
 */
 l= strlen(line_out);
 backslash_count= 0;
 wpt= buf;
 for(i= 0; i < l; i++) {
   if(line_out[i] == '\\')
     backslash_count++;
   else if(line_out[i] == '-') {
     if(backslash_count % 2 == 0)
       *(wpt++)= '\\';
     backslash_count= 0;
   } else
     backslash_count= 0;
   *(wpt++)= line_out[i];
 }
 *wpt= 0;
 strcpy(line_out, buf);
 m->count_out++;
 return(1);
}


int main(int argc, char **argv)
{
 int ret, l, as_filter= 0, i;
 char line_in[256], line_out[256], *got;
 static char name_in[1024]= {"xorriso/xorriso.texi"};
 static char name_out[1024]= {"xorriso/xorriso.1"};
 struct Mx1 m;
 FILE *fp_in= stdin, *fp_out= stdout;
 
 Mx1_init(&m, argv[0], 0);

 if(argc < 2) {
usage:;
   fprintf(stderr, "usage:  %s -auto|-filter [-xorrisofs]\n", argv[0]);
   fprintf(stderr, "  -auto       xorriso/xorriso.texi -> xorriso/xorriso.1\n");
   fprintf(stderr, "  -filter     stdin                -> stdout\n");
   fprintf(stderr, "  -xorrisofs  process xorriso/xorrisofs.texi\n");
   fprintf(stderr, "  -xorrecord  process xorriso/xorrecord.texi\n");
   exit(2);
 }
 for(i= 1; i < argc; i++) {
   if(strcmp(argv[i], "-filter") == 0) {
     as_filter= 1;
   } else if(strcmp(argv[i], "-auto") == 0) {
     as_filter= 0;
   } else if(strcmp(argv[i], "-xorrisofs") == 0) {
     strcpy(name_in, "xorriso/xorrisofs.texi");
     strcpy(name_out, "xorriso/xorrisofs.1");
   } else if(strcmp(argv[i], "-xorrecord") == 0) {
     strcpy(name_in, "xorriso/xorrecord.texi");
     strcpy(name_out, "xorriso/xorrecord.1");
   } else {
     fprintf(stderr, "%s : unknown option %s\n", argv[0], argv[i]);
     goto usage;
   }
 }

 if(!as_filter) {
   fp_in= fopen(name_in, "r");
   if(fp_in == NULL) {
     fprintf(stderr, "%s : failed to fopen( %s ,r) : %d %s\n",
             argv[0], name_in, errno, strerror(errno));
     exit(3);
   }
   fp_out= fopen(name_out, "w");
   if(fp_out == NULL) {
     fprintf(stderr, "%s : failed to fopen( %s ,w) : %d %s\n",
             argv[0], name_out, errno, strerror(errno));
     exit(4);
   }
 }
 while(1) {
   got= fgets(line_in, sizeof(line_in), fp_in);
   if(got == NULL)
 break;
   l= strlen(line_in);
   while(l > 0) {
     if(line_in[l - 1] == '\r' || line_in[l - 1] == '\n') {
       line_in[l - 1] = 0;
       l--;
     } else
   break;
   }
   ret= Mx1_convert(&m, line_in, line_out, 0);
   if(ret < 0)
     exit(1);
   if(ret == 0)
 continue;
   fprintf(fp_out, "%s\n", line_out);
 }
 exit(0);
}
