
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2012 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the miscellaneous helper functions of xorriso.
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
#include <sys/utsname.h>


#include "sfile.h"
#include "misc_funct.h"


/* --------------------------------- misc --------------------------------- */


int Strcmp(const void *pt1, const void *pt2)
{
 return(strcmp(*((char **) pt1), *((char **) pt2)));
}


int Sort_argv(int argc, char **argv, int flag)
{
 if(argc<=0)
   return(2);
 qsort(argv,(size_t) argc,sizeof(char *),Strcmp);
 return(1);
}


static int Text_to_argv(char *text, int *argc, char ***argv, int flag)
{
 char *npt, *cpt;
 int pass;
 
 *argv= NULL;
 *argc= 0;
 for(pass= 0; pass < 2; pass++) {
   if(pass) {
     if(*argc == 0)
       return(1);
     (*argv)= calloc(*argc, sizeof(char *));
     if(*argv == NULL) {
       *argc= 0;
       return(-1);
     }
     *argc= 0;
   }
   for(npt= cpt= text; npt != NULL; cpt= npt + 1) {
     npt= strchr(cpt, '\n');
     if(pass) {
       if(npt != NULL)
         *npt= 0;
       (*argv)[*argc]= cpt;
     }
     (*argc)++;
   }
 }
 return(1);
}


static int Count_diffs(int argc1, char **argv1, int argc2, char **argv2,
                       int flag)
{
 int count= 0, i1= 0, i2= 0, cmp, end_corr= 0;

 Sort_argv(argc1, argv1, 0);
 Sort_argv(argc2, argv2, 0);

 while(1) {
   if(i1 >= argc1) {
     count+= argc2 - i2 - end_corr;
 break;
   }
   if(i2 >= argc2) {
     count+= argc1 - i1 - end_corr;
 break;
   }
   cmp= strcmp(argv1[i1], argv2[i2]);
   if(cmp == 0) {
     end_corr= 0;
     i1++;
     i2++;
   } else if(cmp > 0) {
     count++;
     end_corr= 1;
     i2++;
     if(i2 < argc2 && i1 < argc1 - 1)
       if(strcmp(argv1[i1 + 1], argv2[i2]) == 0) {
         i1++;
         end_corr= 0;
       }
   } else {
     count++;
     end_corr= 1;
     i1++;
     if(i1 < argc1 && i2 < argc2 - 1)
       if(strcmp(argv2[i2 + 1], argv1[i1]) == 0) {
         i2++;
         end_corr= 0;
       }
   }
 }
 return(count);
}


/*
   @flag        bit0= do not initialize *diff_count
   @return  <0 error , 0 = mismatch , 1 = match
*/
int Compare_text_lines(char *text1, char *text2, int *diff_count, int flag)
{
 int ret, argc1= 0, argc2= 0;
 char **argv1= NULL, **argv2= NULL, *copy1= NULL, *copy2= NULL;

 if(!(flag & 1))
   *diff_count= 0;
 if(text1 == NULL && text2 == NULL)
   return(1);
 if(text1 != NULL) {
   copy1= strdup(text1);
   if(copy1 == NULL)
     {ret= -1; goto ex;}
   ret= Text_to_argv(copy1, &argc1, &argv1, 0);
   if(ret <= 0)
     {ret= -1; goto ex;}
 }
 if(text2 != NULL) {
   copy2= strdup(text2);
   if(copy2 == NULL)
     {ret= -1; goto ex;}
   ret= Text_to_argv(copy2, &argc2, &argv2, 0);
   if(ret <= 0)
     {ret= -1; goto ex;}
 }
 ret= Count_diffs(argc1, argv1, argc2, argv2, 1);
 if(ret < 0)
   goto ex;
 *diff_count+= ret;
 ret= (*diff_count == 0);
ex:;
 if(argv1 != NULL)
   free(argv1);
 if(argv2 != NULL)
   free(argv2);
 if(copy1 != NULL)
   free(copy1);
 if(copy2 != NULL)
   free(copy2);
 return ret;
}


/** Convert a text into a number of type double and multiply it by unit code
    [kmgtpe] (2^10 to 2^60) or [s] (2048). (Also accepts capital letters.)
    @param text Input like "42", "2k", "3.14m" or "-1g"
    @param flag Bitfield for control purposes:
                bit0= return -1 rathern than 0 on failure
    @return The derived double value
*/
double Scanf_io_size(char *text, int flag)
/*
 bit0= default value -1 rather than 0
*/
{
 int c;
 double ret= 0.0;

 if(flag&1)
   ret= -1.0;
 if(text[0]==0)
   return(ret);
 sscanf(text,"%lf",&ret);
 c= text[strlen(text)-1];
 if(c=='k' || c=='K') ret*= 1024.0;
 if(c=='m' || c=='M') ret*= 1024.0*1024.0;
 if(c=='g' || c=='G') ret*= 1024.0*1024.0*1024.0;
 if(c=='t' || c=='T') ret*= 1024.0*1024.0*1024.0*1024.0;
 if(c=='p' || c=='P') ret*= 1024.0*1024.0*1024.0*1024.0*1024.0;
 if(c=='e' || c=='E') ret*= 1024.0*1024.0*1024.0*1024.0*1024.0*1024.0;
 if(c=='s' || c=='S') ret*= 2048.0;
 return(ret);
}


int Decode_date_input_format(struct tm *erg, char *text, int flag)
/* MMDDhhmm[[CC]YY][.ss]] */
{
 int i,l,year;
 time_t current_time;
 struct tm *now;

 current_time= time(0);
 now= localtime(&current_time);
 for(i= 0; i < (int) sizeof(struct tm); i++)
   ((char *) erg)[i]= ((char *) now)[i];

 l= strlen(text);
 for(i=0;i<l;i++)
   if(text[i]<'0'||text[i]>'9')
     break;
 if(i!=8 && i!=10 && i!=12)
   return(0);
 if(text[i]==0)
   goto decode;
 if(text[i]!='.' || l!=15)
   return(0);
 i++;
 if(text[i]<'0'||text[i]>'9')
   return(0);  
 i++;
 if(text[i]<'0'||text[i]>'9')
   return(0);

decode:; 
 /* MMDDhhmm[[CC]YY][.ss]] */
 i= 0;
 erg->tm_mon=  10*(text[0]-'0')+text[1]-'0'-1;
 erg->tm_mday= 10*(text[2]-'0')+text[3]-'0';
 erg->tm_hour= 10*(text[4]-'0')+text[5]-'0';
 erg->tm_min=  10*(text[6]-'0')+text[7]-'0';
 erg->tm_sec= 0;
 if(l==8)
   return(1);
 if(l>10){
   year= 1000*(text[8]-'0')+100*(text[9]-'0')+10*(text[10]-'0')+(text[11]-'0');
 }else{
   year= 1900+10*(text[8]-'0')+(text[9]-'0');
   if(year<1970)
     year+= 100;
 }
 erg->tm_year= year-1900;
 if(l<=12)
   return(1);
 erg->tm_sec=  10*(text[13]-'0')+text[14]-'0';
 return(1);
}


int Decode_date_weekday(char *text, int flag)
{
 int i;
 static char days[][4]= {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", ""};

 for(i= 0; days[i][0]!=0; i++)
   if(strncmp(text,days[i],3)==0)
     return(i);
 if((strlen(text)==3 || (strlen(text)==4 && text[3]==',')) &&
    isalpha(text[0]) && isalpha(text[1]) && isalpha(text[2]))
   return(7);
 return(-1);
}


int Decode_date_month(char *text, int flag)
{
 int i;
 static char months[][4]= {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", ""};

 for(i= 0; months[i][0]!=0; i++)
   if(strncmp(text,months[i],3)==0)
     return(i);
 return(-1);
}


/* @return -1=not a number, -2=not a day , 1 to 31 day of month */
int Decode_date_mday(char *text, int flag)
{
 int ret, i;

 for(i= 0; text[i]!=0; i++)
   if(!isdigit(text[i]))
     return(-1);
 if(strlen(text)>2 || text[0]==0)
   return(-2);
 sscanf(text, "%d", &ret);
 if(ret<=0 || ret>31)
   return(-2);
 return(ret); 
}

int Decode_date_hms(char *text, struct tm *erg, int flag)
{
 int i, hour= -1, minute= -1, second= 0;

 for(i= 0; i<9; i+= 3) {
   if(i==6&&text[i]==0)
 break;
   if(!isdigit(text[i]))
     return(-1);
   if(!isdigit(text[i+1]))
     return(-1);
   if(text[i+2]!=':' && !(text[i+2]==0 && i>=3))
     return(-1);
   if(i==0)
     sscanf(text+i,"%d",&hour);
   else if(i==3)
     sscanf(text+i,"%d",&minute);
   else
     sscanf(text+i,"%d",&second);
 }
 if(hour<0 || hour>23 || minute<0 || minute>59 || second>59)
   return(-1);
 erg->tm_hour= hour;
 erg->tm_min= minute;
 erg->tm_sec= second;
 return(1);
}


/* @return -1=not a number, -2=not a year , >=0 years AD */
int Decode_date_year(char *text, int flag)
{
 int ret, i;

 for(i= 0; text[i]!=0; i++)
   if(!isdigit(text[i]))
     return(-1);
 if(strlen(text)!=4)
   return(-2);
 sscanf(text, "%d", &ret);
 if(ret<0 || ret>3000)
   return(-2);
 return(ret); 
}


int Decode_date_timezone(char *text, struct tm *erg, int flag)
{
 int i;
 static char tzs[][5]= {"GMT", "CET", "CEST", "0000", ""};

 for(i= 0; tzs[i][0]!=0; i++)
   if(strcmp(text,tzs[i])==0) {

     /* ??? >>> what to do with timezone info ? Add to ->tm_hour ? */

     return(1);
   }
 if(text[0]=='+' || text[0]=='-') {
   for(i= 1; text[i]!=0; i++)
     if(!isdigit(text[i]))
       return(-1);
   if(i!=5)
     return(-1);

   /* ??? >>> what to do with timezone info ? Add to ->tm_hour ? */

   return(1);
 } else {
   for(i= 0; text[i]!=0; i++)
     if(text[i]<'A' || text[i]>'Z')
       return(-1);
   if(i!=3 && i!=4)
     return(-1);
   return(2);
 }
}


int Decode_date_output_format(struct tm *erg, char *text, int flag)
/* Thu Nov  8 09:07:50 CET 2007 */
/* Sat, 03 Nov 2007 08:58:30 +0100 */
/* Nov  7 23:24 */
{
 int ret, i, argc= 0, seen_year= 0, seen_month= 0, seen_day= 0, seen_time= 0;
 char **argv= NULL;
 struct tm *now;
 time_t timep;

 memset(erg, 0, sizeof(*erg));
 erg->tm_isdst= -1;
 ret= Sfile_make_argv("xorriso", text, &argc, &argv, 0);
 if(ret<=0)
   goto ex;
 for(i= 1; i<argc; i++) {
   if(!seen_month) {
     ret= Decode_date_month(argv[i], 0);
     if(ret>=0) {
       seen_month= 1;
       erg->tm_mon= ret;
 continue;
     }
   }
   if(!seen_day) {
     ret= Decode_date_mday(argv[i], 0);
     if(ret>0) {
       seen_day= 1;
       erg->tm_mday= ret;
 continue;
     }
     if(ret==-2) /* first pure number must be day of month */
       {ret= 0; goto ex;}
   }
   if(!seen_time) {
     ret= Decode_date_hms(argv[i], erg, 0);
     if(ret>0) {
       seen_time= 1;
 continue;
     }
   }
   if(!seen_year) {
     ret= Decode_date_year(argv[i], 0);
     if(ret>0) {
       erg->tm_year= ret-1900;
       seen_year= 1;
 continue;
     }
   }

   /* ignorants have to stay at the end of the loop */

   ret= Decode_date_timezone(argv[i], erg, 0);
   if(ret>=0)
 continue;
   ret= Decode_date_weekday(argv[i], 0);
   if(ret>=0)
 continue; /* ignore weekdays */

   {ret= 0; goto ex;} /* unrecognizable component */
 }

 if(!(seen_day && seen_month))
  {ret= 0; goto ex;}
 if(!seen_year) { /* then use this year */
   timep= time(NULL);
   now= localtime(&timep);
   erg->tm_year= now->tm_year;
 }   
 ret= 1;
ex:
 Sfile_make_argv("", "", &argc, &argv, 2); /* release storage */
 return(ret);
}


int Decode_ecma119_format(struct tm *erg, char *text, int flag)
/* YYYYMMDDhhmmsscc[LOC] */
/* 2010040711405800 */
{
 int i, l, num, utc= 1;

 memset(erg, 0, sizeof(*erg));
 erg->tm_isdst= -1;
 l= strlen(text);
 if(l == 19) {
   if(strcmp(text + 16, "LOC") != 0)
     return(0);
   utc= 0;
   l= 16;
 }
 if(l != 16)
   return(0);
 for(i= 0; i < l; i++)
   if(text[i] < '0' || text[i] > '9')
     return(0);
 num= 0;
 for(i= 0; i < 4; i++)
   num= num * 10 + text[i] - '0';
 if(num < 1970 || num > 3000)
   return(0);
 erg->tm_year = num - 1900;
 erg->tm_mon=  10*(text[4]-'0')+text[5]-'0'-1;
 if(erg->tm_mon > 12)
   return(0);
 erg->tm_mday= 10*(text[6]-'0')+text[7]-'0';
 if(erg->tm_mday > 31)
   return(0);
 erg->tm_hour= 10*(text[8]-'0')+text[9]-'0';
 if(erg->tm_hour > 23)
   return(0);
 erg->tm_min=  10*(text[10]-'0')+text[11]-'0';
 if(erg->tm_min > 59)
   return(0);
 erg->tm_sec= 10*(text[12]-'0')+text[13]-'0';
 if(erg->tm_sec > 59)
   return(0);
 return(1 + !utc);
}


int Decode_xorriso_timestamp(struct tm *erg, char *code, int flag)
   /* 2007.11.07.225624 */
{
 char buf[20];
 int year,month,day,hour= 0,minute= 0,second= 0, i, l, mem;

 memset(erg, 0, sizeof(*erg));
 erg->tm_isdst= -1;

 l= strlen(code);
 if(l>17 || l<10)
   return(0);
 strcpy(buf, code);
 for(i= 0; buf[i]!=0 && i<4; i++)
   if(!isdigit(buf[i]))
     return(0);
 if(buf[4]!='.')
   return(0);
 buf[4]= 0;
 sscanf(buf, "%d", &year);
 if(year<1900 || year>3000)
   return(0);
 if(!(isdigit(buf[5]) && isdigit(buf[6]) && buf[7]=='.'))
   return(0);
 buf[7]= 0;
 sscanf(buf+5, "%d", &month);
 if(month<1 || month>12)
   return(0);
 if(!(isdigit(buf[8]) && isdigit(buf[9]) && (buf[10]=='.' || buf[10]==0)))
   return(0);
 buf[10]= 0;
 sscanf(buf+8, "%d", &day);
 if(day<1 || day>31)
   return(0);
 if(l==10)
   goto done;
 if(!(isdigit(buf[11]) && isdigit(buf[12]) &&
      (isdigit(buf[13]) || buf[13]==0)))
   return(0);
 mem= buf[13];
 buf[13]= 0;
 sscanf(buf+11, "%d", &hour);
 buf[13]= mem;
 if(hour<0 || hour>23)
   return(0);
 if(l==13)
   goto done;
 if(!(isdigit(buf[13]) && isdigit(buf[14]) &&
      (isdigit(buf[15]) || buf[15]==0)))
   return(0);
 mem= buf[15];
 buf[15]= 0;
 sscanf(buf+13, "%d", &minute);
 buf[15]= mem;
 if(minute<0 || minute>59)
   return(0);
 if(l==15)
   goto done;
 if(!(isdigit(buf[15]) && isdigit(buf[16]) && buf[17]==0))
   return(0);
 sscanf(buf+15, "%d", &second);
 if(second<0 || second>59)
   return(0);

done:;
 erg->tm_year= year-1900;
 erg->tm_mon= month-1;
 erg->tm_mday= day;
 erg->tm_hour= hour;
 erg->tm_min= minute;
 erg->tm_sec= second;
 return(1);
}


time_t Decode_timestring(char *code, time_t *date, int flag)
{
 char scale_chr;
 double value,seconds;
 struct tm result_tm;
 int seconds_valid= 0, ret;

 *date= 0;
 if(code[0]=='-' || code[0]=='+' || code[0]=='=' || code[0]=='@'){
   if(code[1]==0)
     return(0);
   if(!isdigit(code[1]))
     return(0);
   value= -1;
   if(code[0]=='=' || code[0]=='@') {
     seconds= 0;
     sscanf(code+1,"%lf",&value);
   } else {
     seconds= time(NULL);
     sscanf(code,"%lf",&value);
   }
   scale_chr= code[strlen(code)-1];
   if(isalpha(scale_chr))
     scale_chr= tolower(scale_chr);
   if     (scale_chr=='s') seconds+= 1.0*value;
   else if(scale_chr=='h') seconds+= 3600.0*value;
   else if(scale_chr=='d') seconds+= 86400.0*value;
   else if(scale_chr=='w') seconds+= 86400.0*7.0*value;
   else if(scale_chr=='m') seconds+= 86400.0*31.0*value;
   else if(scale_chr=='y') seconds+= 86400.0*(365.25*value+1.0);
   else                    seconds+= 1.0*value;
   seconds_valid= 1;
   goto completed;
 } else if(Sfile_decode_datestr(&result_tm,code,0)>0) {
   /* YYMMDD[.hhmm[ss]] */
   result_tm.tm_isdst= -1;
   seconds= mktime(&result_tm);
   seconds_valid= 1;
   goto completed;
 } else if(Decode_date_input_format(&result_tm,code,0)>0) {
   /* MMDDhhmm[[CC]YY][.ss]] */
   result_tm.tm_isdst= -1;
   seconds= mktime(&result_tm);
   seconds_valid= 1;
   goto completed;
 } else if(Decode_xorriso_timestamp(&result_tm, code, 0)>0) {
   /* 2007.11.07.225624 */
   seconds= mktime(&result_tm);
   seconds_valid= 1;
   goto completed;
 } else if(Decode_date_output_format(&result_tm, code, 0)>0) {
   /* Thu Nov  8 09:07:50 CET 2007 */;
   /* Sat, 03 Nov 2007 08:58:30 +0100 */;
   /* Nov  7 23:24 */;
   seconds= mktime(&result_tm);
   seconds_valid= 1;
   goto completed;
 } else if((ret= Decode_ecma119_format(&result_tm, code, 0)) > 0) {
   /* YYYYMMDDhhmmsscc[UTC] */
   /* 2010040711405800UTC */
   seconds= mktime(&result_tm);
   if(ret == 1) {

#ifdef HAVE_TM_GMTOFF
     seconds+= result_tm.tm_gmtoff;
#else
     if(result_tm.tm_isdst < 0)
       result_tm.tm_isdst = 0;
     seconds-= timezone - result_tm.tm_isdst * 3600;
#endif

   }
   seconds_valid= 1;
   goto completed;
 }
 return(0);
completed:;
 if(!seconds_valid)
   return(0);
 *date= seconds;
 return(1);
}


/* @param flag bit0=with year and seconds
               bit1-3= form
                       0= ls -l format
                       1= timestamp format YYYY.MM.DD.hhmmss
                       2= Wdy Mon Day hh:mm:ss Year
                       3= Mon Day hh:mm:ss Year
                       4= YYMMDD.hhmmss
*/
char *Ftimetxt(time_t t, char timetext[40], int flag)
{
 char *rpt;
 struct tm tms, *tmpt;
 static char months[12][4]= { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
 static char days[7][4]= {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
 int form;

 form= (flag>>1)&7;
 tmpt= localtime_r(&t, &tms);
 rpt= timetext;
 rpt[0]= 0;
 if(tmpt==0)
   sprintf(rpt+strlen(rpt), "%12.f", (double) t);
 else if (form==1)
   sprintf(rpt+strlen(rpt), "%4.4d.%2.2d.%2.2d.%2.2d%2.2d%2.2d",
           1900+tms.tm_year, tms.tm_mon+1, tms.tm_mday,
           tms.tm_hour, tms.tm_min, tms.tm_sec);
 else if (form==2)
   sprintf(rpt+strlen(rpt), "%s %s %2.2d %2.2d:%2.2d:%2.2d %4.4d",
           days[tms.tm_wday], months[tms.tm_mon], tms.tm_mday,
           tms.tm_hour, tms.tm_min, tms.tm_sec, 1900+tms.tm_year);
 else if (form==3)
   sprintf(rpt+strlen(rpt), "%s %2.2d %2.2d:%2.2d:%2.2d %4.4d",
           months[tms.tm_mon], tms.tm_mday,
           tms.tm_hour, tms.tm_min, tms.tm_sec, 1900+tms.tm_year);
 else if (form == 4) {
   if(tms.tm_year>99)
     sprintf(rpt+strlen(rpt), "%c", 'A' + (tms.tm_year - 100) / 10);
   else
     sprintf(rpt+strlen(rpt), "%c", '0' + tms.tm_year / 10);
   sprintf(rpt+strlen(rpt), "%1.1d%2.2d%2.2d.%2.2d%2.2d%2.2d",
           tms.tm_year % 10, tms.tm_mon + 1, tms.tm_mday,
           tms.tm_hour, tms.tm_min, tms.tm_sec);
 } else if (flag&1)
   sprintf(rpt+strlen(rpt), "%2d %3s %4.4d %2.2d:%2.2d:%2.2d",
           tms.tm_mday, months[tms.tm_mon], 1900+tms.tm_year,
           tms.tm_hour, tms.tm_min, tms.tm_sec);
 else if(time(NULL)-t < 180*86400 && time(NULL)-t >= 0)
   sprintf(rpt+strlen(rpt), "%3s %2d %2.2d:%2.2d",
           months[tms.tm_mon], tms.tm_mday, tms.tm_hour, tms.tm_min);
 else
   sprintf(rpt+strlen(rpt), "%3s %2d  %4.4d",
           months[tms.tm_mon], tms.tm_mday, 1900+tms.tm_year);
 return(timetext);
}


/* @param flag bit0= single letters */
char *Ftypetxt(mode_t st_mode, int flag)
{
 if(flag&1)
   goto single_letters;
 if(S_ISDIR(st_mode))
   return("directory");
 else if(S_ISREG(st_mode))
   return("regular_file");
 else if(S_ISLNK(st_mode))
   return("symbolic_link");
 else if(S_ISBLK(st_mode))
   return("block_device");
 else if(S_ISCHR(st_mode))
   return("char_device");
 else if(S_ISFIFO(st_mode))
   return("name_pipe");
 else if(S_ISSOCK(st_mode))
   return("unix_socket");
 return("unknown");
single_letters:;
 if(S_ISDIR(st_mode))
   return("d");
 else if(S_ISREG(st_mode))
   return("-");
 else if(S_ISLNK(st_mode))
   return("l");
 else if(S_ISBLK(st_mode))
   return("b");
 else if(S_ISCHR(st_mode))
   return("c");
 else if(S_ISFIFO(st_mode))
   return("p");
 else if(S_ISSOCK(st_mode))
   return("s");
 return("?");
}


int Wait_for_input(int fd, int microsec, int flag)
{
 struct timeval wt;
 fd_set rds,wts,exs;
 int ready;

 FD_ZERO(&rds);
 FD_ZERO(&wts);
 FD_ZERO(&exs);
 FD_SET(fd,&rds);
 FD_SET(fd,&exs);
 wt.tv_sec=  microsec/1000000;
 wt.tv_usec= microsec%1000000;
 ready= select(fd+1,&rds,&wts,&exs,&wt);
 if(ready<=0)
   return(0);
 if(FD_ISSET(fd,&exs))
   return(-1);
 if(FD_ISSET(fd,&rds))
   return(1);
 return(0);
}


int System_uname(char **sysname, char **release, char **version,
                 char **machine, int flag)
{
 int ret;
 static struct utsname uts;
 static int initialized= 0;
 
 if(initialized == 0) {
   ret= uname(&uts);
   if(ret != 0)
     initialized = -1;
 }
 if(initialized == -1)
   return(0);
 if(sysname != NULL)
   *sysname= uts.sysname;
 if(release != NULL)
   *release= uts.release;
 if(version != NULL)
   *version= uts.version;
 if(machine != NULL)
   *machine= uts.machine;
 return(1);
}

/* ------------------------------------------------------------------------ */


#ifndef Xorriso_sregex_externaL

#ifndef Smem_malloC
#define Smem_malloC malloc
#endif
#ifndef Smem_freE
#define Smem_freE free
#endif


int Sregex_string_cut(char **handle, char *text, int len, int flag)
/*
 bit0= append (text!=NULL)
*/
{
 int l=0;
 char *old_handle;

 if((flag&1)&&*handle!=NULL)
   l+= strlen(*handle);
 old_handle= *handle;
 if(text!=NULL) {
   l+= len;
   *handle= TSOB_FELD(char,l+1);
   if(*handle==NULL) {
     *handle= old_handle;
     return(0);
   }
   if((flag&1) && old_handle!=NULL)
     strcpy(*handle,old_handle);
   else
     (*handle)[0]= 0;
   if(len>0)
     strncat(*handle,text,len);
 } else {
   *handle= NULL;
 }
 if(old_handle!=NULL)
   Smem_freE(old_handle);
 return(1);
}


int Sregex_string(char **handle, char *text, int flag)
/*
 bit0= append (text!=NULL)
*/
{
 int ret,l=0;

 if(text!=NULL)
   l= strlen(text);
 
/* #define Sregex_looking_for_contenT 1 */
#ifdef Sregex_looking_for_contenT
 /* a debugging point if a certain text content has to be caught */
 if(text!=NULL)
   if(strcmp(text,"clear")==0)
     ret= 0;
#endif

 ret= Sregex_string_cut(handle,text,l,flag&1);
 return(ret);
}


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
                       char *result, int result_size, int flag)
{
 int l_e, l_s, l_esc, i, start_equals_esc;
 char *rpt, *wpt, *spt, *npt, *ept;

 if(start[0] == 0) /* It is not allowed to have no start marker */
   return(-1);
 l_s= strlen(start);
 l_e= strlen(end);
 l_esc= strlen(esc);
 start_equals_esc= !strcmp(start, esc);
 rpt= form;
 wpt= result;
 wpt[0]= 0;
 while(1) {

   /* look for start mark */
   spt= strstr(rpt, start);
   if(spt == NULL) {
     if((wpt - result) + (int) strlen(rpt) >= result_size)
       return(0);
     strcpy(wpt, rpt);
     wpt+= strlen(wpt);
 break;
   }

   /* copy cleartext part up to next variable */
   if((wpt - result) + (spt - rpt) >= result_size)
     return(0);
   strncpy(wpt, rpt, spt - rpt);
   wpt+= spt - rpt;
   *wpt= 0;
   rpt= spt;
   npt= spt + l_s;

   /* handle eventual escape */
   if(start_equals_esc) {
     if(strncmp(spt + l_s, esc, l_esc) == 0) {
       /* copy esc and start */
       if((wpt - result) + l_esc + l_s >= result_size)
         return(0);
       strncpy(wpt, spt, l_esc + l_s);
       wpt+= l_esc + l_s;
       rpt+= l_esc + l_s;
       *wpt= 0;
 continue;
     }
   } else {
     /* escape would be already copied */
     if(l_esc > 0 && spt - form >= l_esc) {
       if(strncmp(spt - l_esc, esc, l_esc) == 0) {
         /* copy start */
         if((wpt - result) + l_s >= result_size)
           return(0);
         strncpy(wpt, spt, l_s);
         wpt+= l_s;
         rpt+= l_s;
       *wpt= 0;
 continue;
       }
     }
   }

   /* Memorize eventual end mark for default handling */;
   ept= NULL;
   if(l_e > 0)
     ept= strstr(npt, end);

   /* Look for defined variable name */
   for(i = 0; i < num_vars; i++) {
     if(strncmp(npt, vars[i][0], strlen(vars[i][0])) == 0
        && (l_e == 0 || strncmp(npt + strlen(vars[i][0]), end, l_e) == 0))
   break;
   }
   if(i < num_vars) {
     /* substitute found variable */
     if((wpt - result) + (int) strlen(vars[i][1]) >= result_size)
       return(0);
     strcpy(wpt, vars[i][1]);
     rpt= npt + strlen(vars[i][0]) + l_e;
   } else if((flag & 1) && ept != NULL) {
     /* skip up to end mark */
     rpt= ept + l_e;
   } else if(ept != NULL) {
     /* copy start,name,end */
     if((wpt - result) + (ept - rpt) + l_e >= result_size)
       return(0);
     strncpy(wpt, rpt, (ept - rpt) + l_e);
     rpt= ept + l_e;
   } else {
     /* copy start marker only */
     if((wpt - result) + l_s >= result_size)
       return(0);
     strncpy(wpt, rpt, l_s);
     rpt= rpt + l_s;
   }
   wpt+= strlen(wpt);
   *wpt= 0;
 }
 return(1);
}


/* @param flag bit0= only test expression whether compilable
*/
int Sregex_match(char *pattern, char *text, int flag)
{
 int ret;
 char *re_text= NULL;
 regex_t re;
 regmatch_t match[1];

 re_text= TSOB_FELD(char, 2 * SfileadrL);
 if(re_text == NULL)
   {ret= -1; goto ex;}

 Xorriso__bourne_to_reg(pattern, re_text, 0);
 ret= regcomp(&re, re_text, 0);
 if(ret != 0) 
   {ret= -1; goto ex;}
 if(flag & 1) {
   regfree(&re);
   {ret= 1; goto ex;}
 }
 ret= regexec(&re, text, 1, match, 0);
 regfree(&re);
 if(ret != 0)
   {ret= 0; goto ex;}
 ret= 1;
ex:;
 if(re_text != NULL)
   free(re_text);
 return(ret);
}


#endif /*  Xorriso_sregex_externaL */



/* @param flag bit0= append to out_text rather than overwrite it
               bit1= length limit is 10 * SfileadrL rather than 5 *
*/
char *Text_shellsafe(char *in_text, char *out_text, int flag)
{
 int l,i,w=0, limit= 5 * SfileadrL;

 if(flag&1)
   w= strlen(out_text);
 if(flag & 2)
   limit= 10 * SfileadrL;
 /* enclose everything by hard quotes */
 l= strlen(in_text);
 out_text[w++]= '\'';
 for(i=0;i<l;i++){
   if(in_text[i]=='\''){
     if(w + 7 > limit)
       goto overflow;
     /* escape hard quote within the text */
     out_text[w++]= '\'';
     out_text[w++]= '"';
     out_text[w++]= '\'';
     out_text[w++]= '"';
     out_text[w++]= '\'';
   } else {
     if(w + 3 > limit) {
overflow:;
       strncpy(out_text, "'xorriso: TEXT MUCH TOO LONG ...   ",33);
 break;
     }
     out_text[w++]= in_text[i];
   }
 }
 out_text[w++]= '\'';
 out_text[w++]= 0;
 return(out_text);
}


int Hex_to_bin(char *hex,
               int bin_size, int *bin_count, unsigned char *bin_data, int flag)
{
 int i, l, acc;

 l= strlen(hex);
 if(l % 2 || l == 0)
   return(-1); /* malformed */
 *bin_count= 0;
 for(i= 0; i < l; i+= 2) {
   if(hex[i] >= '0' && hex[i] <= '9')
     acc= (hex[i] - '0') << 4;
   else if(hex[i] >= 'A' && hex[i] <= 'F')
     acc= (hex[i] - 'A' + 10) << 4;
   else if(hex[i] >= 'a' && hex[i] <= 'f')
     acc= (hex[i] - 'a' + 10) << 4;
   else
     return(-1);
   if(hex[i + 1] >= '0' && hex[i + 1] <= '9')
     acc|= (hex[i + 1] - '0');
   else if(hex[i + 1] >= 'A' && hex[i + 1] <= 'F')
     acc|= (hex[i + 1] - 'A' + 10);
   else if(hex[i + 1] >= 'a' && hex[i + 1] <= 'f')
     acc|= (hex[i + 1] - 'a' + 10);
   else
     return(-1);
   if(*bin_count >= bin_size)
     return(0); /* overflow */
   bin_data[*bin_count]= acc;
   (*bin_count)++;
 }
 return(1);
}


#ifndef Xorriso_fileliste_externaL 

/* ??? ts A71006 : Is this compatible with mkisofs pathspecs ?
                   I dimly remember so */

int Fileliste__target_source_limit(char *line, char sep, char **limit_pt,
                                    int flag)
{
 char *npt;

 for(npt= line;*npt!=0;npt++) {
   if(*npt=='\\') {
     if(*(npt+1)!=0)
       npt++;
 continue;
   }
   if(*npt=='=')
 break;
 }
 if(*npt==0)
   npt= NULL;
 (*limit_pt)= npt;
 return(npt!=NULL);
}


int Fileliste__escape_source_path(char *line, int size, int flag)
{
 int l, count= 0, i;
 char *wpt;

 l= strlen(line);
 for(i= 0; i < l; i++)
   if(line[i] == '=' || line[i] == '\\')
     count++;
 if(l + count >= size)
   return(0);

 wpt= line + l + count;
 for(i= l; i >= 0; i--) {
   *(wpt--)= line[i];
   if(line[i] == '=' || line[i] == '\\')
     *(wpt--)= '\\';
 }
 return(1);
}


int Xorriso__bourne_to_reg(char bourne_expr[], char reg_expr[], int flag)
/* reg_expr should be twice as large as bourne_expr ( + 2 to be exact) */
/* return: 2= bourne_expr is surely a constant */
{
 char *wpt,*lpt;
 int backslash= 0,is_constant= 1,in_square_brackets= 0;
 int first_in_square_brackets=0;

 wpt= reg_expr;
 lpt= bourne_expr;

 *(wpt++)= '^';

 while(*lpt!=0){
  if(first_in_square_brackets>0)
    first_in_square_brackets--;
  if(!backslash){
    switch(*lpt){
    case '?':
      *(wpt++)= '.';
      is_constant= 0;
    break;case '*':
      *(wpt++)= '.';
      *(wpt++)= '*';
      is_constant= 0;
    break;case '.':
      *(wpt++)= '\\';
      *(wpt++)= '.';
    break;case '+':
      *(wpt++)= '\\';
      *(wpt++)= '+';
    break;case '[':
      *(wpt++)= *lpt;
      first_in_square_brackets= 2;
      in_square_brackets= 1;
      is_constant= 0;
    break;case ']':
      *(wpt++)= *lpt;
      in_square_brackets= 0;
    break;case '!':
      if(first_in_square_brackets)
        *(wpt++)= '^';
      else if(in_square_brackets)
        *(wpt++)= '!';
      else {
        *(wpt++)= '\\';
        *(wpt++)= '!';
      }
    break;case '^':
      if(in_square_brackets)
        *(wpt++)= '^';
      else
        *(wpt++)= '\\';
        *(wpt++)= '^';
    break;case '$':
      *(wpt++)= '\\';
      *(wpt++)= '$';
    break;case '\\':
      backslash= 1;
      *(wpt++)= '\\';
      is_constant= 0;
    break;default:
      *(wpt++)= *lpt;
    }
  } else {
    backslash= 0;
    *(wpt++)= *lpt;
  }
  lpt++;
 }
 *(wpt++)= '$';
 *wpt= 0;
 return(1+(is_constant>0));
}


#endif /* ! Xorriso_fileliste_externaL */


int Xorriso__hide_mode(char *mode, int flag)
{
 char *npt, *cpt;
 int l, value= 0;

 npt= cpt= mode;
 for(; npt!=NULL; cpt= npt+1) {
   npt= strchr(cpt,':');
   if(npt==NULL)
     l= strlen(cpt);
   else 
     l= npt-cpt;
   if(l == 0)
 continue;
   if(l == 2 && strncmp(cpt, "on", l) == 0)
     value= 1 | 2 | 4;
   else if(l == 6 && strncmp(cpt, "iso_rr", l) == 0)
     value |= 1;
   else if(l == 6 && strncmp(cpt, "joliet", l) == 0)
     value |= 2;
   else if(l == 7 && strncmp(cpt, "hfsplus", l) == 0)
     value |= 4;
   else if(l == 3 && strncmp(cpt, "off", l) == 0)
     value= 0;
   else
     return(-1);
 }
 return(value);
}


char *Xorriso__hide_mode_text(int hide_mode, int flag)
{
 char *acc= NULL;

 acc = calloc(1, 80);
 if(acc == NULL)
   return(NULL);
 acc[0]= 0;
 if(hide_mode == 0) {
   strcat(acc, "off:");
 } else if(hide_mode == 7) {
   strcat(acc, "on:");
 } else {
   if(hide_mode & 1)
     strcat(acc, "iso_rr:");
   if(hide_mode & 2)
     strcat(acc, "joliet:");
   if(hide_mode & 4)
     strcat(acc, "hfsplus:");
 }
 if(acc[0])
   acc[strlen(acc) - 1]= 0; /* cut off last colon */
 return acc;
}


/* @return 0=truncated, 1=ok
*/
int Xorriso__to_upper(char *in, char *out, int out_size, int flag)
{
 int i;

 for(i= 0; i < out_size - 1 && in[i] != 0; i++)
   if(isalpha(in[i]))
     out[i]= toupper(in[i]);
   else
     out[i]= in[i];
 out[i]= 0;
 return(in[i] == 0);
}

