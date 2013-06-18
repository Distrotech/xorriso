
/* xorriso - creates, loads, manipulates and burns ISO 9660 filesystem images.

   Copyright 2007-2013 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the implementation of emulators for mkisofs and cdrecord.
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
#include <fcntl.h>
#include <errno.h>

#include "xorriso.h"
#include "xorriso_private.h"
#include "xorrisoburn.h"


int Xorriso_cdrskin_uses_stdout(struct XorrisO *xorriso, int argc, char **argv,
                                int flag)
{
 int i;

 for(i= 0; i < argc; i++) {
   if(strcmp(argv[i], "dev=-") == 0 ||
      strcmp(argv[i], "dev=stdio:/dev/fd/1") == 0 ||
      strcmp(argv[i], "-dev=-") == 0 ||
      strcmp(argv[i], "-dev=stdio:/dev/fd/1") == 0)
     return(1);
 }
 return(0);
}


int Xorriso_cdrskin_help(struct XorrisO *xorriso, int flag)
{
 static char helptext[][80]= {
"Usage: xorriso -as cdrecord [options|source_addresses]",
"Note: This is not cdrecord. See xorriso -help, xorriso -version, man xorrecord",
"Options:",
"\t-version\tprint version information and exit emulation",
"\t--devices\tprint list of available MMC drives and exit emulation",
"\tdev=target\tpseudo-SCSI target to use as CD-Recorder",
"\t-v\t\tincrement verbose level by one",
"\t-V\t\tincrement SCSI command transport verbose level by one",
"\t-checkdrive\tcheck if a driver for the drive is present",
"\t-inq\t\tdo an inquiry for the drive",
"\tspeed=#\t\tset speed of drive",
"\tblank=type\tblank a CD-RW disc (see blank=help)",
"\tfs=#\t\tSet fifo size to # (0 to disable, default is 4 MB)",
"\t-eject\t\teject the disk after doing the work",
"\t-dummy\t\tdo everything with laser turned off",
"\t-msinfo\t\tretrieve multi-session info for mkisofs >= 1.10",
"\t-toc\t\tretrieve and print TOC/PMA data",
"\t-atip\t\tretrieve media state, print \"Is *erasable\"",
"\t-multi\t\tgenerate a TOC that allows multi session",
"\t-waiti\t\twait until input is available before opening SCSI",
"\t-tao\t\tWrite disk in TAO mode.",
"\t-dao\t\tWrite disk in SAO mode.",
"\t-sao\t\tWrite disk in SAO mode.",
"\ttsize=#\t\tannounces exact size of source data",
"\tpadsize=#\tAmount of padding",
"\t-data\t\tSubsequent tracks are CD-ROM data mode 1 (default)",
"\t-isosize\tUse iso9660 file system size for next data track",
"\t-pad\t\tpadsize=30k",
"\t-nopad\t\tDo not pad",
"\t--grow_overwriteable_iso\temulate multi-session on DVD+RW, BD-RE",
"\twrite_start_address=#\t\twrite to byte address on DVD+RW, BD-RE",
"\tstream_recording=on|number\ttry to get full speed on DVD-RAM, BD",
"\tdvd_obs=default|32k|64k\t\tbytes per DVD/BD write operation",
"\tstdio_sync=on|off|number\twhether to fsync output to \"stdio:\"",
"\t--no_rc\t\tDo not execute xorriso startup files",
"\t-help\t\tprint this text to stderr and exit emulation",
"Actually this is the integrated ISO RockRidge filesystem manipulator xorriso",
"lending its libburn capabilities to a very limited cdrecord emulation. Only",
"a single data track can be burnt to blank, appendable or overwriteable media.",
"A much more elaborate cdrecord emulator is cdrskin from the same project.",
"@End_of_helptexT@"
};
 int i;

 for(i= 0; strcmp(helptext[i], "@End_of_helptexT@")!=0; i++) {
   sprintf(xorriso->info_text, "%s\n", helptext[i]);
   Xorriso_info(xorriso,0);
 }
 return(1);
}


/* micro version of cdrskin */
int Xorriso_cdrskin(struct XorrisO *xorriso, char *whom, int argc, char **argv,
                    int flag)
{
 int ret, i, k, mem_do_close, aq_ret, eject_ret, msc1, msc2, hflag;
 int do_atip= 0, do_checkdrive= 0, do_eject= 0, do_scanbus= 0;
 int do_toc= 0, do_verbous= 0, do_version= 0, do_help= 0, do_waiti= 0;
 int do_multi= 0, do_msinfo= 0, do_grow= 0, do_isosize= 0, do_xa1= 0;
 double write_start_address= -1.0, tsize= -1.0;
 char *track_source= NULL, *dev_adr= NULL, *cpt;
 char mem_report_about_text[80], *report_about= "SORRY", blank_mode[80];
 char speed[80], *argpt;

 /* cdrecord 2.01 options which are not scheduled for implementation, yet */
 static char ignored_partial_options[][41]= {
   "timeout=", "debug=", "kdebug=", "kd=", "driver=", "ts=",
   "pregap=", "defpregap=", "mcn=", "isrc=", "index=", "textfile=",
   "pktsize=", "cuefile=",
   "gracetime=", "minbuf=",

   "assert_write_lba=", "fifo_start_at=", "dev_translation=",
   "drive_scsi_dev_family=", "fallback_program=", "modesty_on_drive=",
   "tao_to_sao_tsize=", 
   
   "direct_write_amount=", "msifile=",

   ""
 };
 static char ignored_full_options[][41]= {
   "-d", "-silent", "-s", "-setdropts", "-prcap",
   "-reset", "-abort", "-overburn", "-ignsize", "-useinfo",
   "-fix", "-nofix",
   "-raw", "-raw96p", "-raw16",
   "-clone", "-text",
   "-cdi", "-preemp", "-nopreemp", "-copy", "-nocopy",
   "-scms", "-shorttrack", "-noshorttrack", "-packet", "-noclose",
   "-media-info", "-minfo",
   "-load", "-lock", "-raw96r", "-swab",
   "-force", "-format",

   "--adjust_speed_to_drive", "--allow_emulated_drives", "--allow_setuid",
   "--allow_untested_media", "--any_track", "--demand_a_drive", 
   "--fifo_disable", "--fifo_start_empty", "--fill_up_media",
   "--list_ignored_options", "--no_rc", "--no_convert_fs_adr",
   "--prodvd_cli_compatible", "--single_track", 
   "--tell_media_space",

   ""
 };

static char blank_help[][80]= {
"Blanking options:",
"\tall\t\tblank the entire disk",
"\tdisc\t\tblank the entire disk",
"\tdisk\t\tblank the entire disk",
"\tfast\t\tminimally blank the entire disk",
"\tminimal\t\tminimally blank the entire disk",
"\tas_needed\tblank or format medium to make it ready for (re-)use",
"\tdeformat\t\tblank a formatted DVD-RW",
"\tdeformat_quickest\tminimally blank a formatted DVD-RW to DAO only",
"\tformat_overwrite\tformat a DVD-RW to \"Restricted Overwrite\"",
"@End_of_helptexT@"
};

 mem_do_close= xorriso->do_close;
 Xorriso_alloc_meM(track_source, char, SfileadrL);
 Xorriso_alloc_meM(dev_adr, char, SfileadrL);

 strcpy(mem_report_about_text, xorriso->report_about_text);

 track_source[0]= 0;
 dev_adr[0]= 0;
 blank_mode[0]= 0;
 speed[0]= 0;

 if(xorriso->in_drive_handle != NULL) {
   ret= Xorriso_option_dev(xorriso, "", 1|32); /* give up indev */
   if(ret!=1)
     goto ex;
 }


 /* Assess plan, make settings */
 for(i= 0; i<argc; i++) {
   sprintf(xorriso->info_text, "-as %s: ", whom);
   Text_shellsafe(argv[i], xorriso->info_text, 1);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);

   argpt= argv[i];
   if (strncmp(argpt, "--", 2) == 0 && strlen(argpt) > 3)
     argpt++;

   for(k=0;ignored_partial_options[k][0]!=0;k++) {
     if(argpt[0]=='-')
       if(strncmp(argpt+1,ignored_partial_options[k],
                            strlen(ignored_partial_options[k]))==0) {
         argpt++;
         goto no_volunteer;
       }
     if(strncmp(argpt,ignored_partial_options[k],
                        strlen(ignored_partial_options[k]))==0)
       goto no_volunteer;
   }
   for(k=0;ignored_full_options[k][0]!=0;k++)
     if(strcmp(argpt,ignored_full_options[k])==0)
       goto no_volunteer;
   if(0) {
no_volunteer:;
     sprintf(xorriso->info_text, "-as %s: Ignored option ", whom);
     Text_shellsafe(argpt, xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
 continue;
   }

   if(strcmp(argpt, "-atip")==0) {
     do_atip= 1;
   } else if(strcmp(argpt, "-audio")==0) {
     sprintf(xorriso->info_text, "-as %s: Option -audio not supported.", whom);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   } else if(strncmp(argpt, "-blank=", 7)==0 ||
             strncmp(argpt, "blank=", 6)==0) {
     cpt= strchr(argpt, '=')+1;
     if(strcmp(cpt,"all")==0 || strcmp(cpt,"disc")==0
        || strcmp(cpt,"disk")==0) {
       strcpy(blank_mode, "all");
     } else if(strcmp(cpt,"fast")==0 || strcmp(cpt,"minimal")==0) {
       strcpy(blank_mode, "fast");
     } else if(strcmp(cpt,"help")==0) {
       strcpy(blank_mode, "help");
     } else if(strcmp(cpt,"deformat")==0 ||
               strcmp(cpt,"deformat_sequential")==0 ||
               strcmp(cpt,"deformat_quickest")==0 ||
               strcmp(cpt,"deformat_sequential_quickest")==0) {
       strcpy(blank_mode, cpt);
     } else if(strcmp(cpt,"format_overwrite")==0) {
       strcpy(blank_mode, "format_overwrite");
     } else if(strcmp(cpt,"as_needed")==0) {
       strcpy(blank_mode, "as_needed");
     } else {
       sprintf(xorriso->info_text, "-as %s: blank=", whom);
       Text_shellsafe(cpt, xorriso->info_text, 1);
       strcat(xorriso->info_text, " not supported. See blank=help .");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
     }
   } else if(strcmp(argpt, "-checkdrive")==0) {
     do_checkdrive= 1;
   } else if(strcmp(argpt, "-dao")==0) {
     xorriso->do_tao= -1;
   } else if(strcmp(argpt, "-data")==0) {
     /* ok */;
   } else if(strncmp(argpt, "-dev=", 5)==0 ||
             strncmp(argpt, "dev=", 4)==0) {
     cpt= strchr(argpt, '=')+1;
     strcpy(dev_adr, cpt);
   } else if(strcmp(argv[i], "--devices")==0) {      /* intentional: argv[i] */
     do_scanbus= 2;
   } else if(strncmp(argpt,"driveropts=", 11)==0 ||
             strncmp(argpt,"-driveropts=", 12)==0) {
     if(strcmp(argpt+11, "help")==0) {
       fprintf(stderr,"Driver options:\n");
       fprintf(stderr,
               "burnfree\tPrepare writer to use BURN-Free technology\n");
     } 
   } else if(strcmp(argpt, "-dummy")==0) {
     xorriso->do_dummy= 1;
   } else if(strncmp(argpt, "-dvd_obs=", 9)==0 ||
             strncmp(argpt, "dvd_obs=", 8)==0) {
     cpt= strchr(argpt, '=') + 1;
     Xorriso_option_dvd_obs(xorriso, cpt, 0);
   } else if(strcmp(argpt, "-eject")==0) {
     do_eject= 1;
   } else if(strncmp(argpt, "-fs=", 4)==0 || strncmp(argpt, "fs=", 3)==0) {
     cpt= strchr(argpt, '=')+1;
     ret= Xorriso_option_fs(xorriso, cpt, 0);
     if(ret<=0)
       goto ex;
   } else if(strcmp(argv[i], "--grow_overwriteable_iso")==0 ||
             strcmp(argv[i], "--grow_overwritable_iso")==0 ||
             strcmp(argv[i], "--grow_overriteable_iso")==0
             ) { /* (A history of typos) */
                                                     /* intentional: argv[i] */
     do_grow= 1;
   } else if(strcmp(argpt, "-help")==0) {
     do_help= 1;
   } else if(strcmp(argpt, "-isosize")==0) {
     do_isosize= 1;
   } else if(strcmp(argpt, "-inq")==0) {
     do_checkdrive= 2;
   } else if(strcmp(argpt, "-mode2")==0) {
     Xorriso_msgs_submit(xorriso, 0,
                     "Defaulting option -mode2 to option -data", 0, "NOTE", 0);
   } else if(strcmp(argpt, "-msinfo")==0) {
     do_msinfo= 1;
   } else if(strcmp(argpt, "-multi")==0) {
     do_multi= 1;
   } else if(strcmp(argpt, "-nopad")==0) {
     xorriso->padding= 0;
   } else if(strcmp(argv[i], "--no_rc")==0) { /* intentional: argv[i] */
     /* already performed in Xorriso_prescan_args */;
   } else if(strcmp(argpt, "-pad")==0) {
     xorriso->padding= 15*2048;
   } else if(strncmp(argpt, "-padsize=", 9)==0 ||
             strncmp(argpt, "padsize=", 8)==0) {
     cpt= strchr(argpt, '=')+1;
     ret= Xorriso_option_padding(xorriso, cpt, 0);
     if(ret<=0)
       goto ex;
   } else if(strcmp(argpt, "-sao")==0) {
     xorriso->do_tao= -1;
   } else if(strcmp(argpt, "-scanbus")==0) {
     sprintf(xorriso->info_text, "-as %s: Option -scanbus not supported.",
             whom);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   } else if(strncmp(argpt, "-speed=", 7)==0 ||
             strncmp(argpt, "speed=", 6)==0) {
     cpt= strchr(argpt, '=')+1;
     strncpy(speed, cpt, 79);
     speed[79]= 0;
   } else if(strncmp(argpt, "-stream_recording=", 18)==0 ||
             strncmp(argpt, "stream_recording=", 17)==0) {
     cpt= strchr(argpt, '=')+1;
     Xorriso_option_stream_recording(xorriso, cpt, 0);
   } else if(strncmp(argpt, "-stdio_sync=", 12)==0 ||
             strncmp(argpt, "stdio_sync=", 11)==0) {
     cpt= strchr(argpt, '=') + 1;
     Xorriso_option_stdio_sync(xorriso, cpt, 0);
   } else if(strcmp(argpt, "-tao")==0) {
     xorriso->do_tao= 1;
   } else if(strcmp(argpt, "-toc")==0 || strcmp(argv[i], "--long_toc")==0) {
                                             /* intentional: argpt , argv[i] */
     do_toc= 1;
   } else if(strncmp(argpt, "-tsize=", 7)==0 ||
             strncmp(argpt, "tsize=", 6)==0) {
     cpt= strchr(argpt, '=')+1;
     tsize= Scanf_io_size(cpt, 1);
     if(tsize > 1024.0*1024.0*1024.0*1024.0*1024.0) {
       sprintf(xorriso->info_text, "-as %s: much too large: %s",whom, argpt);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
     }
   } else if(strcmp(argv[i], "-V")==0 || strcmp(argpt,"-Verbose")==0) {
     Xorriso_option_scsi_log(xorriso, "on", 0);
   } else if(strcmp(argv[i], "-v")==0 || strcmp(argpt,"-verbose")==0) {
     do_verbous++;
   } else if(strcmp(argv[i], "-vv")==0) { /* intentional: argv[i] */
     do_verbous+= 2;
   } else if(strcmp(argv[i], "-vvv")==0) { /* intentional: argv[i] */
     do_verbous+= 3;
   } else if(strcmp(argpt, "-version")==0) {
     do_version= 1;
   } else if(strcmp(argpt, "-waiti")==0) {
     do_waiti= 1;
   } else if(strncmp(argv[i], "write_start_address=", 20)==0) {
                                                     /* intentional: argv[i] */
     write_start_address= Scanf_io_size(argv[i]+20,0);
   } else if(strcmp(argpt, "-xa")==0) {
     Xorriso_msgs_submit(xorriso, 0,
                        "Defaulting option -xa to option -data", 0, "NOTE", 0);
   } else if(strcmp(argpt, "-xa1")==0) {
     if(do_xa1 == 0)
       do_xa1= 1;
   } else if(strcmp(argv[i], "--xa1-ignore")==0) { /* intentional: argv[i] */
     do_xa1= -1;
   } else if(strcmp(argpt, "-xa2")==0) {
     Xorriso_msgs_submit(xorriso, 0,
                       "Defaulting option -xa2 to option -data", 0, "NOTE", 0);
   } else if(strcmp(argpt, "-xamix")==0) {
     Xorriso_msgs_submit(xorriso, 0,
   "Option -xamix not implemented and data not yet convertible to other modes",
                         0, "FATAL", 0);
     ret= 0; goto ex;
   } else if(argpt[0]=='-' && argpt[1]!=0) {
     sprintf(xorriso->info_text, "-as %s: Unknown option ", whom);
     Text_shellsafe(argv[i], xorriso->info_text, 1);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   } else {
     if(track_source[0]) {
       sprintf(xorriso->info_text, "-as %s: Surplus track source ", whom);
       Text_shellsafe(argv[i], xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       sprintf(xorriso->info_text, "First and only track source is ");
       Text_shellsafe(track_source, xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
       ret= 0; goto ex;
     }
     if(Sfile_str(track_source, argv[i], 0)<=0)
       {ret= -1; goto ex;}
   }
 }

 /* Perform actions */
 Xorriso_option_report_about(xorriso, "NOTE", 0);
 if(do_version) {
   sprintf(xorriso->result_line, "Cdrecord 2.01-Emulation Copyright (C) 2013 see libburnia-project.org xorriso\n");
   Xorriso_result(xorriso, 1);
   Xorriso_option_version(xorriso, 0);
 }

 if(do_help) {
   Xorriso_cdrskin_help(xorriso, 0);
 }
 if(strcmp(blank_mode, "help")==0) {
   for(i= 0; strcmp(blank_help[i], "@End_of_helptexT@")!=0; i++) {
     sprintf(xorriso->info_text, "%s\n", blank_help[i]);
     Xorriso_info(xorriso,0);
   }
 }
 if(do_help || strcmp(blank_mode, "help") == 0 || do_version) {
   ret= 1; goto ex;
 }

 if(do_verbous<=0)
   report_about= "NOTE";
 else if(do_verbous<=2)
   report_about= "UPDATE";
 else if(do_verbous==3)
   report_about= "DEBUG";
 else
   report_about= "ALL";
 Xorriso_option_report_about(xorriso, report_about, 0);

 if(do_scanbus) {
   if(do_scanbus==1)
     /* >>> would need -scanbus compatible output and input format */;
   else
     Xorriso_option_devices(xorriso, 0);
   ret= 1; goto ex;
 }

 if(!(do_checkdrive || do_atip || do_toc || blank_mode[0] || track_source[0] ||
      do_eject || do_msinfo)) {
   sprintf(xorriso->info_text,
           "-as cdrskin: No option specified, which would cause an action.");
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "SORRY", 0);
   ret= 1; goto ex;
 }

 if(do_waiti) {
   sprintf(xorriso->info_text,
       "xorriso: Option -waiti pauses program until input appears at stdin\n");
   Xorriso_info(xorriso,0);
   sprintf(xorriso->result_line, "Waiting for data on stdin...\n");
   Xorriso_result(xorriso, 1);
   for(ret= 0; ret==0; )
     ret= Wait_for_input(0,1000000,0);
   if(ret<0 || feof(stdin)) {
     Xorriso_msgs_submit(xorriso, 0,
                    "stdin produces exception rather than data", 0, "NOTE", 0);
   }
   sprintf(xorriso->info_text, "xorriso: Option -waiti pausing is done.\n");
 }
 if(dev_adr[0]) {
   hflag= 2 | 64;       /* ts B11201 no more:  | 32 */
   if(!do_grow)
     hflag|= 8; /* consider overwriteables as blank */
   ret= Xorriso_option_dev(xorriso, dev_adr, hflag);
   if(ret<=0)
     goto ex;
 }

 if(xorriso->out_drive_handle==NULL) {
   sprintf(xorriso->info_text, "-as %s: No output drive selected", whom);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
   ret= 0; goto ex;
 }

 if(do_msinfo) {
   ret= Xorriso_msinfo(xorriso, &msc1, &msc2, 2 | !!do_grow);
   if(ret<=0)
     goto ex;
   sprintf(xorriso->result_line, "%d,%d\n", msc1, msc2);
   Xorriso_result(xorriso, 1);
 }

 if(speed[0]) {
   ret= Xorriso_option_speed(xorriso, speed, 0);
   if(ret<=0)
     goto ex;
 }

 if(do_checkdrive) {
   ret= Xorriso_atip(xorriso, 2-(do_checkdrive==2));
   if(ret<=0)
     goto ex;
 }
 if(do_atip) {
   ret= Xorriso_atip(xorriso, 0);
   if(ret<=0)
     goto ex;
 }
 if(do_toc) {
   ret= Xorriso_option_toc(xorriso, 0);
   if(ret<=0)
     goto ex;
 }
 if(strcmp(blank_mode, "format_overwrite")==0) {
   ret= Xorriso_option_blank(xorriso, "fast", 1);
   if(ret<=0)
     goto ex;
 } else if(blank_mode[0]) {
   ret= Xorriso_option_blank(xorriso, blank_mode, 0);
   if(ret<=0)
     goto ex;
 }
 if(track_source[0]) {
   xorriso->do_close= !do_multi;
   ret= Xorriso_burn_track(xorriso, (off_t) write_start_address,
                           track_source, (off_t) tsize,
                   (!!do_grow) | ((!!do_isosize) << 1) | ((do_xa1 == 1) << 2));
   aq_ret= Xorriso_reaquire_outdev(xorriso, 2*(ret>0));
   if(ret<=0 && ret<aq_ret)
     goto ex;
   if(aq_ret<=0)
     {ret= aq_ret; goto ex;}
 }

 ret= 1;
ex:;
 if(do_eject && ret>=0) {
   eject_ret= Xorriso_option_eject(xorriso, "out", 0);
   if(eject_ret<ret)
     ret= eject_ret;
 }
 if(ret<=0) {
   sprintf(xorriso->info_text, "-as %s: Job could not be performed properly.",
           whom);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
 }
 Xorriso_option_report_about(xorriso, mem_report_about_text, 0);
 xorriso->do_close= mem_do_close;
 Xorriso_free_meM(dev_adr);
 Xorriso_free_meM(track_source);
 return(ret);
}


/* This function shall know all options of mkisofs, genisoimage, xorrisofs, ...
   and the number of arguments which they expect and consume.
*/
int Xorriso_genisofs_count_args(struct XorrisO *xorriso, int argc, char **argv,
                                int *count, int flag)
{
 int i;
 char *cmd;

 static char partial_options[][41]= {
    "errctl=",
    "isolinux_mbr=", "--modification-date=",
    ""
 };
 static char arg0_options[][41]= {
    "-allow-leading-dots", "-ldots", "-allow-lowercase", "-allow-multidot",
    "-cache-inodes", "-no-cache-inodes", "-eltorito-alt-boot",
    "-hard-disk-boot", "-no-emul-boot", "-no-boot", "-boot-info-table",
    "-check-oldnames", "-d", "-D", "-dvd-video", "-f", "-gui", "-graft-points",
    "-hide-joliet-trans-tbl", "-hide-rr-moved", "-J", "-joliet-long", "-l",
    "-L", "-max-iso9660-filenames", "-N", "-nobak", "-no-bak",
    "-no-limit-pathtables", "-force-rr", "-no-rr",
    "-no-split-symlink-components", "-no-split-symlink-fields", "-pad",
    "-no-pad", "-posix-H", "-posix-L", "-posix-P", "-print-size",
    "-quiet", "-R", "-r", "-relaxed-filenames", "-rrip110", "-rrip112",
    "-split-output", "-T", "-UDF", "-udf", "-udf-symlinks", "-no-udf-symlinks",
    "-U", "-no-iso-translate", "-v", "-XA", "-xa", "-z",
    "-hfs", "-no-hfs", "-apple", "-probe", "-no-desktop", "-mac-name",
    "-part", "-icon-position", "-chrp-t", "-hfs-unlock", "--cap", "--netatalk",
    "--double", "--ethershare", "--ushare", "--exchange", "--sgi", "--xinet",
    "--macbin", "--single", "--dave", "--sfm", "--osx-double", "--osx-hfs",
    "-debug", "-omit-period", "-disable-deep-relocation", "-joliet",
    "-full-iso9660-filenames", "-follow-links", "-help",
    "-transparent-compression",
    "-omit-version-number", "-rational-rock", "-rock", "-translation-table",
    "-untranslated-filenames", "-verbose", "-version", "-g", "-h",
    "-no-mac-files", "-chrp-boot",
    "--hardlinks", "--acl", "--xattr", "--md5", "--for_backup",
    "--protective-msdos-label", "--boot-catalog-hide", "--no-emul-toc",
    "--emul-toc", "-disallow_dir_id_ext", "--old-empty",
    "--old-root-no-md5", "--old-root-devno", "--old-root-no-ino",
    "--no_rc", "--norock", "-hfsplus", "-fat", "-chrp-boot-part",
    "-isohybrid-gpt-basdat", "-isohybrid-gpt-hfsplus",
    "-isohybrid-apm-hfsplus", "--grub2-boot-info",
    ""
 };
 static char arg1_options[][41]= {
    "-abstract", "-A", "-appid", "-biblio", "-b", "-B", "-boot-load-seg",
    "-boot-load-size", "-C", "-c", "-check-session", "-copyright",
    "-dir-mode", "-eltorito-id", "-eltorito-selcrit",
    "-file-mode", "-G", "-gid", "-hide", "-hide-list",
    "-hidden", "-hidden-list", "-hide-joliet", "-hide-joliet-list",
    "-hide-hfsplus", "-hide-hfsplus-list",
    "-hide-udf", "-hide-udf-list", "-input-charset", "-output-charset",
    "-iso-level", "-jcharset", "-log-file", "-m", "-exclude-list", "-M",
    "-dev", "-new-dir-mode", "-o", "-p", "-preparer",
    "-path-list", "-publisher", "-root",
    "-old-root", "-s", "-sectype", "-sort", "-sparc-boot", "-sparc-label",
    "-stream-media-size", "-stream-file-name", "-sunx86-boot", "-sunx86-label",
    "-sysid", "-table-name", "-ucs-level", "-uid", "-V", "-volset",
    "-volset-size", "-volset-seqno", "-x", "-P",
    "-map", "-magic", "-hfs-creator", "-hfs-type", "-boot-hfs-file", "-auto",
    "-cluster-size", "-hide-hfs", "-hide-hfs-list", "-hfs-volid",
    "-root-info", "-prep-boot", "-input-hfs-charset", "-output-hfs-charset",
    "-hfs-bless", "-hfs-parms", 
    "-eltorito-boot", "-generic-boot", "-eltorito-catalog", "-cdrecord-params",
    "-errctl", "-exclude", "-prev-session", "-output", "-use-fileversion",
    "-volid", "-old-exclude",
    "-alpha-boot", "-hppa-cmdline", "-hppa-kernel-32", "-hppa-kernel-64",
    "-hppa-bootloader", "-hppa-ramdisk", "-mips-boot", "-mipsel-boot",
    "-jigdo-jigdo", "-jigdo-template", "-jigdo-min-file-size",
    "-jigdo-force-md5", "-jigdo-exclude", "-jigdo-map", "-md5-list",
    "-jigdo-template-compress",
    "-checksum_algorithm_iso", "-checksum_algorithm_template",
    "--stdio_sync", "--quoted_path_list", "--efi-boot", "--embedded-boot",
    "-isohybrid-mbr", "-e", "-partition_offset", "-partition_hd_cyl",
    "-partition_sec_hd", "-partition_cyl_align", "-untranslated_name_len",
    "-rr_reloc_dir", "-hfsplus-serial-no", "-prep-boot-part", "-efi-boot-part",
    "-hfsplus-block-size", "-apm-block-size", "--grub2-mbr",
    "--grub2-sparc-core",
    ""
 };
 static char arg2_options[][41]= {
    "-hfs-bless-by", "--scdbackup_tag", "--sort-weight",
    ""
 };
 static char arg3_options[][41]= {
    "-append_partition", "-hfsplus-file-creator-type",
    ""
 };
 static char final_options[][41]= {
    "-find",
    ""
 };

 cmd= argv[0];
 *count= 0;
 for(i=0; partial_options[i][0]!=0; i++)
   if(strncmp(partial_options[i], cmd, strlen(partial_options[i]))==0)
     return(1);
 for(i=0; arg0_options[i][0]!=0; i++)
   if(strcmp(arg0_options[i], cmd)==0)
     return(1);
 *count= 1;
 for(i=0; arg1_options[i][0]!=0; i++)
   if(strcmp(arg1_options[i], cmd)==0)
     return(1);
 *count= 2;
 for(i=0; arg2_options[i][0]!=0; i++)
   if(strcmp(arg2_options[i], cmd)==0)
     return(1);
 *count= 3;
 for(i=0; arg3_options[i][0]!=0; i++)
   if(strcmp(arg3_options[i], cmd)==0)
     return(1);
 *count= argc - 1;
 for(i=0; final_options[i][0]!=0; i++)
   if(strcmp(final_options[i], cmd)==0)
     return(1);
 *count= 0;
 return(0); 
}


/* @param flag bit0= do not report eventual ignore decision
*/
int Xorriso_genisofs_ignore(struct XorrisO *xorriso, char *whom,
                            char *argpt, int *i, int flag)
{
 /* mkisofs 2.01 options which are not scheduled for implementation, yet */
 static char ignored_arg0_options[][41]= {
   "-allow-leading-dots", "-ldots", "-allow-multidot",
   "-cache-inodes", "-check-oldnames",
   "-L", "-no-bak", "-no-cache-inodes",
   "-no-split-symlink-components", "-no-split-symlink-fields", "-nobak",
   "-force-rr", "-T",
   "-no-iso-translate", "-gui",
   ""
 };
 static char ignored_arg1_options[][41]= {
   "-check-session", "-hide-hfs", "-hide-hfs-list",
   "-table-name", "-volset-seqno", "-volset-size", "-sort",
   ""
 };
 int k;

 for(k=0;ignored_arg0_options[k][0]!=0;k++)
   if(strcmp(argpt,ignored_arg0_options[k])==0)
     goto no_volunteer;
 for(k=0;ignored_arg1_options[k][0]!=0;k++)
   if(strcmp(argpt,ignored_arg1_options[k])==0) {
     (*i)++;
     goto no_volunteer;
   }
 return(0);
no_volunteer:;
 sprintf(xorriso->info_text, "-as %s: Ignored option ", whom);
 Text_shellsafe(argpt, xorriso->info_text, 1);
 if(!(flag & 1))
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
 return(1);
}


int Xorriso_genisofs_add_boot(struct XorrisO *xorriso, int flag)
{
 int ret;

 ret= Xorriso_attach_boot_image(xorriso, 0);
 if(ret <= 0)
   xorriso->boot_image_bin_path[0]= 0;
 return(ret);
}


int Xorriso_genisofs_help(struct XorrisO *xorriso, int flag)
{
 static char helptext[][160]= {
"Usage: xorriso -as mkisofs [options] file...",
"Note: This is not mkisofs. See xorriso -help, xorriso -version, man xorrisofs",
"Options:",
"  -f, -follow-links           Follow symbolic links",
"  -graft-points               Allow to use graft points for filenames",
"  -help                       Print option help",
"  -hfsplus                    Generate HFS+ filesystem",
"  -hfsplus-file-creator-type CREATOR TYPE iso_rr_path",
"                              Attach creator and type to a File",
"  -hfs-bless FOLDER_NAME      Name of Folder to be blessed",
"  -hfs-bless-by BLESS_TYPE ISO_RR_PATH",
"                              Bless ISO_RR_PATH by BLESS_TYPE {p,i,s,9,x}",
"  -hfsplus-serial-no HEXSTRING",
"                              HFS serial number: 16 characters [0-9a-fA-F]",
"  -hfsplus-block-size NUMBER  Set HFS+ block size",
"  -apm-block-size NUMBER      Set Apple Partition Map block size",
"  -hide GLOBFILE              Hide ISO9660/RR file",
"  -hide-list FILE             File with list of ISO9660/RR files to hide",
"  -hide-joliet GLOBFILE       Hide Joliet file",
"  -hide-joliet-list FILE      File with list of Joliet files to hide",
"  -hide-hfsplus GLOBFILE      Hide HFS+ file",
"  -hide-hfsplus-list FILE     File with list of HFS+ files to hide",
"  -input-charset CHARSET      Local input charset for file name conversion",
"  -output-charset CHARSET     Output charset for file name conversion",
"  -iso-level LEVEL            Set ISO9660 conformance level (1..3) or 4 for ISO9660 version 2",
"  -disallow_dir_id_ext        Do not allow dot in ISO directory names",
"  -J, -joliet                 Generate Joliet directory information",
"  -joliet-long                Allow Joliet file names to be 103 Unicode characters",
"  -U, -untranslated-filenames Allow Untranslated filenames (for HPUX & AIX - violates ISO9660).",
"  -untranslated_name_len LEN  Allow up to LEN (1..96) name characters (heavily violates ISO9660).",
"  -allow-lowercase            Allow lower case characters in addition to the current character set (violates ISO9660)",
"  -relaxed-filenames          Allow 7 bit ASCII except lower case characters (violates ISO9660)",
"  -d, -omit-period            Omit trailing periods from filenames (violates ISO9660)",
"  -l, -full-iso9660-filenames Allow full 31 character filenames for ISO9660 names",
"  -max-iso9660-filenames      Allow 37 character filenames for ISO9660 names (violates ISO9660)",
"  -N, -omit-version-number    Omit version number from ISO9660 filename (violates ISO9660)",
"  -D, -disable-deep-relocation",
"                              Disable deep directory relocation (violates ISO9660)",
"  -hide-rr-moved              Relocate deep directories to /.rr_moved",
"  -rr_reloc_dir NAME          Set deep directory relocation target in root",
"  -o FILE, -output FILE       Set output file name",
"  -m GLOBFILE, -exclude GLOBFILE",
"                              Exclude file name",
"  -x FILE, -old-exclude FILE  Exclude file name",
"  -exclude-list FILE          File with list of file names to exclude",
"  -pad                        Pad output by 300k (default)",
"  -no-pad                     Do not pad output",
"  -M FILE, -prev-session FILE Set path to previous session to merge",
"  -C PARAMS, -cdrecord-params PARAMS",
"                              Magic paramters from cdrecord",
"  -dir-mode mode              Make the mode of all directories this mode.",
"  -file-mode mode             Make the mode of all plain files this mode.",
"  -path-list FILE             File with list of pathnames to process",
"  --quoted_path_list FILE     File with list of quoted pathnames to process",
"  -print-size                 Print estimated filesystem size and exit",
"  -quiet                      Run quietly",
"  -R, -rock                   Generate Rock Ridge directory information",
"  -r, -rational-rock          Generate rationalized Rock Ridge directory information",
"  --norock                    Disable Rock Ridge. (Strongly discouraged !)",
"  --hardlinks                 Record eventual hard link relations of files",
"  --acl                       Record eventual ACLs of files",
"  --xattr                     Record eventual user space xattr of files",
"  --md5                       Compute and record MD5 checksums of data files",
"  --scdbackup_tag PATH NAME   With --md5 record a scdbackup checksum tag",
"  --for_backup                Use all options which improve backup fidelity",
"  -V ID, -volid ID            Set Volume ID",
"  -volset ID                  Set Volume set ID",
"  -publisher PUB              Set Volume publisher",
"  -A ID, -appid ID            Set Application ID",
"  -sysid ID                   Set System ID",
"  -p PREP, -preparer PREP     Set Volume preparer",
"  -abstract FILE              Set Abstract filename",
"  -biblio FILE                Set Bibliographic filename",
"  -copyright FILE             Set Copyright filename",
"  -jigdo-jigdo FILE           Produce a jigdo .jigdo file as well as the .iso",
"  -jigdo-template FILE        Produce a jigdo .template file as well as the .iso",
"  -jigdo-min-file-size SIZE   Minimum size for a file to be listed in the jigdo file", 
"  -jigdo-force-md5 PATTERN    Pattern(s) where files MUST match an externally-supplied MD5sum",
"  -jigdo-exclude PATTERN      Pattern(s) to exclude from the jigdo file",
"  -jigdo-map PATTERN1=PATTERN2",
"                              Pattern(s) to map paths (e.g. Debian=/mirror/debian)",
"  -md5-list FILE              File containing MD5 sums of the files that should be checked",
"  -jigdo-template-compress ALGORITHM",
"                              Choose to use gzip or bzip2 compression for template data; default is gzip",
"  -checksum_algorithm_iso alg1,alg2,...",
"                              Specify the checksum types desired for the output image (in .jigdo)",
"  -checksum_algorithm_template alg1,alg2,...",
"                              Specify the checksum types desired for the output jigdo template",
"  -b FILE, -eltorito-boot FILE",
"                              Set El Torito boot image name",
"  -eltorito-alt-boot          Start specifying alternative El Torito boot parameters",
"  --efi-boot FILE             Set El Torito EFI boot image name and type",
"  -e FILE                     Set EFI boot image name (more rawly)",
"  -c FILE, -eltorito-catalog FILE",
"                              Set El Torito boot catalog name",
"  --boot-catalog-hide         Hide boot catalog from ISO9660/RR and Joliet",
"  -boot-load-size #           Set numbers of load sectors",
"  -hard-disk-boot             Boot image is a hard disk image",
"  -no-emul-boot               Boot image is 'no emulation' image",
"  -boot-info-table            Patch boot image with info table",
"  --grub2-boot-info           Patch boot image at byte 2548",
"  -eltorito-id ID             Set El Torito Id String",
"  -eltorito-selcrit HEXBYTES  Set El Torito Selection Criteria",
"  -isohybrid-gpt-basdat       Mark El Torito boot image as Basic Data in GPT",
"  -isohybrid-gpt-hfsplus      Mark El Torito boot image as HFS+ in GPT",
"  -isohybrid-apm-hfsplus      Mark El Torito boot image as HFS+ in APM",
"  -G FILE, -generic-boot FILE Set generic boot image name",
"  --embedded-boot FILE        Alias of -G",
"  --protective-msdos-label    Patch System Area by partition table",
"  -partition_offset LBA       Make image mountable by first partition, too",
"  -partition_sec_hd NUMBER    Define number of sectors per head",
"  -partition_hd_cyl NUMBER    Define number of heads per cylinder",
"  -partition_cyl_align MODE   Control cylinder alignment: off, on, auto, all",
"  -mips-boot FILE             Set mips boot image name (relative to image root)",
"  -mipsel-boot FILE           Set mipsel boot image name (relative to image root)",
"  -B FILES, -sparc-boot FILES Set sparc boot image names",
"  -sparc-label label text     Set sparc boot disk label",
"  --grub2-sparc-core FILE     Set path of core file for disk label patching",
"  -efi-boot-part DISKFILE|--efi-boot-image",
"                              Set data source for EFI System Partition",
"  -chrp-boot-part             Mark ISO image size by MBR partition type 0x41",
"  -prep-boot-part DISKFILE    Set data source for MBR partition type 0x96",
"  -append_partition NUMBER TYPE FILE",
"                              Append FILE after image. TYPE is hex: 0x..",
"  --modification-date=YYYYMMDDhhmmsscc",
"                              Override date of creation and modification",
"  -isohybrid-mbr FILE         Set SYSLINUX mbr/isohdp[fp]x*.bin for isohybrid",
"  --grub2-mbr FILE            Set GRUB2 MBR for boot image address patching",
#ifdef Xorriso_with_isohybriD
"  isolinux_mbr=on|auto|off    Control eventual isohybrid MBR generation",
#endif
"  --sort-weight NUMBER FILE   Set LBA weight number to file or file tree",
"  --stdio_sync on|off|number  Control forced output to disk files",
"  --no-emul-toc               Save 64 kB size on random access output files",
"  --emul-toc                  Multi-session history on such output files",
"  --old-empty                 Use old style block addresses for empty files",
"  -z, -transparent-compression",
"                              Enable transparent compression of files",
"  -root DIR                   Set root directory for all new files and directories",
"  -old-root DIR               Set root directory in previous session that is searched for files",
"  --old-root-no-md5           Do not record and use MD5 with -old-root",
"  --old-root-no-ino           Do not use disk inode numbers with -old-root",
"  --old-root-devno            Use disk device numbers with -old-root",
"  -log-file LOG_FILE          Re-direct messages to LOG_FILE",
"  --no_rc                     Do not execute startup files",
"  -v, -verbose                Verbose",
"  -version                    Print the current version",
"@End_of_helptexT@"
};

 char ra_text[80];
 int i;

 strcpy(ra_text, xorriso->report_about_text);

 Xorriso_option_report_about(xorriso, "NOTE", 0);
 for(i= 0; strcmp(helptext[i], "@End_of_helptexT@")!=0; i++) {
   sprintf(xorriso->info_text, "%s\n", helptext[i]);
   Xorriso_info(xorriso, 1);
 }
 Xorriso_option_report_about(xorriso, ra_text, 0);
 return(1);
}


/* Perform hiding.
   Cumbersome: The paths and patterns apply to the disk address and not
   to the Rock Ridge address. Actually even the literal form of the
   mkisofs pathspec would matter (e.g. "./" versus "").
   But xorriso normalizes disk_paths before further processing. Thus
   the literal form does not matter.
   @param hide_attrs
          bit0= hide in ISO/RR
          bit1= hide in Joliet
          bit2= hide in HFS+
          bit3 to bit5 are reserved for future hidings
*/
int Xorriso_genisofs_hide(struct XorrisO *xorriso, char *whom,
                          char *pattern, int hide_attrs, int flag)
{
 int zero= 0, ret;
 char *argv[1];

 if((hide_attrs & 63) == 0)
   return(2);

 if(strchr(pattern, '/') != NULL) {
   argv[0]= pattern;
   ret= Xorriso_option_not_paths(xorriso, 1, argv, &zero,
                                 4 | ((hide_attrs & 63) << 8));
 } else {
   ret= Xorriso_option_not_leaf(xorriso, pattern, hide_attrs & 63);
 }
 return(ret);
}


/* @param flag bit0= quoted list */
int Xorriso_genisofs_hide_list(struct XorrisO *xorriso, char *whom,
                               char *adr, int hide_attrs, int flag)
{
 int ret, linecount= 0, argc= 0, was_failure= 0, i, fret;
 char **argv= NULL, *id= "";
 FILE *fp= NULL;

 if(adr[0]==0) {
   if (hide_attrs & 2)
     id = "joliet-";
   else if (hide_attrs & 4)
     id = "hfsplus-";
   sprintf(xorriso->info_text,
          "Empty file name given with -as %s -hide-%slist", whom, id);
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "WARNING", 0);
   return(0);
 }
 ret= Xorriso_afile_fopen(xorriso, adr, "rb", &fp, 0);
 if(ret <= 0)
   return(0);
 if(ret <= 0)
   return(0);
 while(1) {
   ret= Xorriso_read_lines(xorriso, fp, &linecount, &argc, &argv,
                           4 | (flag & 1) );
   if(ret <= 0)
     goto ex;
   if(ret == 2)
 break;
   for(i= 0; i < argc; i++) {
     if(argv[i][0] == 0)
   continue;
     ret= Xorriso_genisofs_hide(xorriso, whom, argv[i], hide_attrs, 0);
     if(ret <= 0 || xorriso->request_to_abort) {
       was_failure= 1;
       fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
       if(fret>=0)
   continue;
       if(ret > 0)
         ret= 0;
       goto ex;
     }
   }
 }
 ret= 1;
ex:;
 if(flag & 1)
   Xorriso_read_lines(xorriso, fp, &linecount, &argc, &argv, 2);
 if(fp != NULL && fp != stdin)
   fclose(fp);
 if(ret<=0)
   return(ret);
 return(!was_failure);
}


/* Strip surplus dash from known single-dash long options */
int Xorriso_genisofs_strip_dash(struct XorrisO *xorriso, char *arg_in,
                                 char **arg_out, int flag)
{
 int ret, count;
 char *argv[1];

 *arg_out= arg_in;
 if(strlen(arg_in) < 4)
   return(1);
 if(arg_in[0] != '-' || arg_in[1] != '-' || arg_in[2] == '-')
   return(1);

 argv[0]= arg_in + 1; 
 ret= Xorriso_genisofs_count_args(xorriso, 1, argv, &count, 0);
 if(ret > 0)
   *arg_out= arg_in + 1;
 return(1);
}


/* Interprets a string of single-char options which have no parameters
   @param flag bit0=check whether string is ok
               bit1=this is pass 1
   @return with flag bit0: 0=no , 1=yes, 2= with bit1: non-pass-1 options seen
           else          : 1 = ok , <= 0 indicates error
*/
int Xorriso_genisofs_fused_options(struct XorrisO *xorriso, char *whom,
                                   char *opts,
                                   int *option_d, int *iso_level, int *lower_r,
                                   char ra_text[80], int flag)
{
 int ret, non_pass1= 0;
 char *cpt;
 static char pass1_covered[]= {"fvz"};
 static char covered[]= {"dDfJlNRrTUvz"};

 if(flag & 1) {
   for(cpt= opts; *cpt != 0; cpt++) {
     if(strchr(covered, *cpt) == NULL)
       {ret= 0; goto ex;}
     if(flag & 2)
       if(strchr(pass1_covered, *cpt) == NULL)
         non_pass1= 1;
   }
   ret= 1 + non_pass1; goto ex;
 }
 
 for(cpt= opts; *cpt != 0; cpt++) {
   if(*cpt == 'd') {
     if(flag & 2)
 continue;
     Xorriso_relax_compliance(xorriso, "no_force_dots", 0);
   } else if(*cpt == 'D') {
     if(flag & 2)
 continue;
     *option_d= 1;
   } else if(*cpt == 'f') {
     if(!(flag & 2))
 continue;
     ret= Xorriso_option_follow(xorriso, "on", 0);
     if(ret <= 0)
       goto ex;
   } else if(*cpt == 'J') {
     if(flag & 2)
 continue;
     xorriso->do_joliet= 1;
   } else if(*cpt == 'l') {
     if(flag & 2)
 continue;
     if(xorriso->iso_level <= 2)
       Xorriso_relax_compliance(xorriso, "iso_9660_level=2", 0);
     if(*iso_level <= 2)
       *iso_level= 2;
   } else if(*cpt == 'N') {
     if(flag & 2)
 continue;
     Xorriso_relax_compliance(xorriso, "omit_version", 0);
   } else if(*cpt == 'R') {
     if(flag & 2)
 continue;
     xorriso->do_rockridge= 1;
   } else if(*cpt == 'r') {
     if(flag & 2)
 continue;
     xorriso->do_rockridge= 1;
     *lower_r= 1;
   } else if(*cpt == 'T') {
     /* ignored */;
   } else if(*cpt == 'U') {
     if(flag & 2)
 continue;
     Xorriso_relax_compliance(xorriso,
       "no_force_dots:long_paths:long_names:omit_version:full_ascii:lowercase",
                              0);
   } else if(*cpt == 'v') {
     if(!(flag & 2))
 continue;
     strcpy(ra_text, "UPDATE");
   } else if(*cpt == 'z') {
     if(!(flag & 2))
 continue;
     Xorriso_option_zisofs(xorriso, "by_magic=on", 0);
   } else {
     sprintf(xorriso->info_text, "-as %s: Unsupported option -%c", whom, *cpt);
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     ret= 0; goto ex;
   }
 }
 ret= 1; 
ex:;
 return(ret);
}


int Xorriso_genisofs_path_pecul(struct XorrisO *xorriso, int *was_path,
                                int with_emul_toc, int *allow_dir_id_ext,
                                int *iso_level, int flag)
{
 char *sfe= NULL;
 int ret;

 if(*was_path) {
   ret= 1; goto ex;
 }
 *was_path= 1;

 Xorriso_alloc_meM(sfe, char, 5*SfileadrL);

 /* Enforce odd mkisofs defaults on first pathspec */
 xorriso->pacifier_style= 1;
 if(xorriso->allow_dir_id_ext_dflt && *allow_dir_id_ext < 0)
   *allow_dir_id_ext= 1;
 if(*allow_dir_id_ext == 1) {
   Xorriso_relax_compliance(xorriso, "allow_dir_id_ext", 0);
   *allow_dir_id_ext= 2;
 }
 if(xorriso->iso_level_is_default && *iso_level < 0)
   *iso_level= 1;
 if(*iso_level >= 1 && *iso_level <= 3) {
   sprintf(sfe, "iso_9660_level=%d", *iso_level);
   Xorriso_relax_compliance(xorriso, sfe, 0);
   iso_level= 0;
 }
 /* For the sake of compatibility give up emulated multi-session by default
 */
 if(with_emul_toc == 0)
   xorriso->no_emul_toc|= 1;
 /* mkisofs records mtime in ECMA-119 and Joliet
 */
 Xorriso_relax_compliance(xorriso, "rec_mtime", 0);

 Xorriso_free_meM(sfe);
 ret= 1;
ex:;
 return(ret);
}


/* micro emulation of mkisofs */
int Xorriso_genisofs(struct XorrisO *xorriso, char *whom,
                     int argc, char **argv, int flag)
{
 int ret, i, j, was_path= 0, was_other_option= 0, mem_graft_points, mem;
 int do_print_size= 0, fd, idx, iso_level= -1;
 int was_failure= 0, fret, lower_r= 0, zero= 0;
 int dir_mode= -1, file_mode= -1, count, partition_number;
 int allow_dir_id_ext= -1;
 int root_seen= 0, do_md5_mem, option_d= 0, arg_count;
 mode_t mode_and, mode_or;
 int with_boot_image= 0, with_cat_path= 0, with_emul_toc= 0;
 int old_root_md5= 1, old_root_dev= 0, old_root_ino= 1;
 int *weight_list= NULL, weight_count= 0;
 int *delay_opt_list= NULL, delay_opt_count= 0;
 char *sfe= NULL, *adr= NULL, ra_text[80], *pathspec= NULL;
 char *ept, *add_pt, *eff_path= NULL, *indev= NULL, msc[80], *cpt;
 char *old_root= NULL, *argpt, *hargv[1];
 char *boot_path, partno_text[8], *iso_rr_pt, *disk_pt, *rpt, *wpt;
 char *rm_merge_args[3], *rr_reloc_dir_pt= NULL;
 char *sort_weight_args[4], *bless_args[6];

 struct stat stbuf;

 Xorriso_alloc_meM(sfe, char, 5*SfileadrL);
 Xorriso_alloc_meM(adr, char, SfileadrL+8);
 Xorriso_alloc_meM(pathspec, char, 2*SfileadrL);
 Xorriso_alloc_meM(eff_path, char, SfileadrL);
 Xorriso_alloc_meM(indev, char, SfileadrL+8);
 Xorriso_alloc_meM(old_root, char, SfileadrL);

 for(i= 0; i<argc; i++) {
   if(strcmp(argv[i], "-log-file") == 0 ||
      strcmp(argv[i], "--log-file") == 0 ) {
     if(i + 1 >= argc)
       goto not_enough_args;
     i+= 1;
     if(argv[i][0]) {
       sprintf(xorriso->info_text, "re-directing all messages to %s\n",
               argv[i]);
       Xorriso_info(xorriso, 0);
     }
     ret= Xorriso_write_to_channel(xorriso, argv[i], 2,
                                   8 | ((argv[i][0] == 0) << 15));
     if(ret <= 0) {
       sprintf(xorriso->info_text, "Cannot open logfile:  %s", argv[i]);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, errno * (ret == 0),
                           "SORRY", 0);
       was_failure= 1;
       fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
       if(fret < 0)
         {ret= 0; goto ex;}
     }
     if(argv[i][0] == 0) {
       sprintf(xorriso->info_text, "Revoked stderr message redirection");
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "NOTE", 0);
     }
   }
 }

 strcpy(ra_text, xorriso->report_about_text);

 weight_list= TSOB_FELD(int, (argc / 3) + 1);
 if(weight_list == NULL) {
   Xorriso_no_malloc_memory(xorriso, NULL, 0);
   {ret= -1; goto ex;}
 }
 delay_opt_list= TSOB_FELD(int, argc + 1);
 if(delay_opt_list == NULL) {
   cpt= (char *) weight_list;
   Xorriso_no_malloc_memory(xorriso, &cpt, 0);
   {ret= -1; goto ex;}
 } 

 if(xorriso->boot_image_cat_path[0])
   with_cat_path= -1;
 adr[0]= indev[0]= msc[0]= old_root[0]= 0;
 for(i= 0; i<argc; i++) {
   ret= Xorriso_genisofs_strip_dash(xorriso, argv[i], &argpt, 0);
   if(ret <= 0)
     goto ex;
   ret= Xorriso_genisofs_ignore(xorriso, whom, argpt, &i, 1);
   if(ret == 1)
 continue;
   if(strcmp(argpt, "-version")==0) {
     sprintf(xorriso->result_line,
"mkisofs 2.01-Emulation Copyright (C) 2013 see libburnia-project.org xorriso\n"
            );
     fd= xorriso->dev_fd_1;
     if(fd<0)
       fd= 1;
     ret= write(fd, xorriso->result_line, strlen(xorriso->result_line));
     /* (result of write intentionally ignored) */
     fsync(fd);
     Xorriso_option_version(xorriso, 0);

   } else if(strcmp(argpt, "-o")==0 || strcmp(argpt, "-output")==0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     adr[0]= 0;
     if(strcmp(argv[i],"-")!=0 && strncmp(argv[i], "stdio:", 6)!=0)
       strcpy(adr, "stdio:");
     if(Sfile_str(adr+strlen(adr), argv[i], 0)<=0)
       {ret= -1; goto ex;}
   } else if(strcmp(argpt, "-M")==0  || strcmp(argpt, "-dev")==0 ||
             strcmp(argpt, "-prev-session")==0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     if(strncmp(argv[i], "stdio:", 6)!=0)
       strcpy(indev, "stdio:");
     if(Sfile_str(indev+strlen(indev), argv[i], 0)<=0)
       {ret= -1; goto ex;}
   } else if(strcmp(argpt, "-C")==0 ||
             strcmp(argpt, "-cdrecord-params")==0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     strncpy(msc, argv[i], sizeof(msc)-1);
     msc[sizeof(msc)-1]= 0;
   } else if(strcmp(argpt, "-help")==0) {
     Xorriso_genisofs_help(xorriso, 0);
   } else if(strcmp(argpt, "-v")==0 || strcmp(argpt, "-verbose")==0) {
     ret= Xorriso_genisofs_fused_options(xorriso, whom, "v",
                                  &option_d, &iso_level, &lower_r, ra_text, 2);
     if(ret <= 0)
       goto problem_handler_1;
   } else if(strcmp(argpt, "-quiet")==0) {
     strcpy(ra_text, "SORRY");
   } else if(strcmp(argpt, "-f")==0 || strcmp(argpt, "-follow-links")==0) {
     ret= Xorriso_genisofs_fused_options(xorriso, whom, "f",
                                  &option_d, &iso_level, &lower_r, ra_text, 2);
     if(ret <= 0)
       goto problem_handler_1;
   } else if(strcmp(argpt, "-iso-level")==0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     sscanf(argv[i], "%d", &iso_level);
     if(iso_level < 1 || iso_level > 4) {
       sprintf(xorriso->info_text,
               "-as %s: unsupported -iso-level '%s' (use one of: 1,2,3,4)",
               whom, argv[i]);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto problem_handler_1;
     }
     if(iso_level == 4)
       xorriso->do_iso1999= 1;
     else {
       sprintf(sfe, "iso_9660_level=%s", argv[i]);
       ret= Xorriso_relax_compliance(xorriso, sfe, 0);
       if(ret <= 0)
         goto problem_handler_1;
     }

   } else if(strcmp(argpt, "-input-charset")==0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     /* -local_charset */
     if(strcmp(argv[i], "default") == 0)
       ret= Xorriso_option_charset(xorriso, "ISO-8859-1", 4);
     else
       ret= Xorriso_option_charset(xorriso, argv[i], 4);
     if(ret <= 0)
       goto problem_handler_1;
   } else if(strcmp(argpt, "-output-charset")==0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     /* -charset */
     if(strcmp(argv[i], "default") == 0)
       ret= Xorriso_option_charset(xorriso, "ISO-8859-1", 3);
     else
       ret= Xorriso_option_charset(xorriso, argv[i], 3);
     if(ret <= 0)
       goto problem_handler_1;

   } else if(strcmp(argpt, "-hide") == 0 ||
             strcmp(argpt, "-hide-list") == 0 ||
             strcmp(argpt, "-hide-joliet") == 0 ||
             strcmp(argpt, "-hide-joliet-list") == 0 ||
             strcmp(argpt, "-hide-hfsplus") == 0 ||
             strcmp(argpt, "-hide-hfsplus-list") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     if(strcmp(argpt, "-hide") == 0)
       ret= Xorriso_genisofs_hide(xorriso, whom, argv[i], 1, 0);
     else if(strcmp(argpt, "-hide-list") == 0)
       ret= Xorriso_genisofs_hide_list(xorriso, whom, argv[i], 1, 0);
     else if(strcmp(argpt, "-hide-joliet") == 0) 
       ret= Xorriso_genisofs_hide(xorriso, whom, argv[i], 2, 0);
     else if(strcmp(argpt, "-hide-joliet-list") == 0) 
       ret= Xorriso_genisofs_hide_list(xorriso, whom, argv[i], 2, 0);
     else if(strcmp(argpt, "-hide-hfsplus") == 0)
       ret= Xorriso_genisofs_hide(xorriso, whom, argv[i], 4, 0);
     else if(strcmp(argpt, "-hide-hfsplus-list") == 0)
       ret= Xorriso_genisofs_hide_list(xorriso, whom, argv[i], 4, 0);
     if(ret <= 0)
       goto problem_handler_1;

   } else if(strcmp(argpt, "-root") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     /* Always absolute */
     strcpy(eff_path, "/");
     if(Sfile_str(eff_path, argv[i], argv[i][0] != '/') <= 0)
       {ret= -1; goto ex;}
     strcpy(xorriso->wdi, eff_path);
     root_seen= 1;

   } else if(strcmp(argpt, "-old-root") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     /* Always absolute */
     strcpy(old_root, "/");
     if(Sfile_str(old_root, argv[i], argv[i][0] != '/') <= 0)
       {ret= -1; goto ex;}

   } else if(strcmp(argpt, "--old-root-no-md5")==0) {
     old_root_md5= 0;
   } else if(strcmp(argpt, "--old-root-devno")==0) {
     old_root_dev= 1;
   } else if(strcmp(argpt, "--old-root-no-ino")==0) {
     old_root_ino= 0;

   } else if(strcmp(argpt, "-fat") == 0) {
     xorriso->do_fat= 1;
   } else if(strcmp(argpt, "-hfsplus") == 0) {
     /* Already with -indev */
     xorriso->do_hfsplus= 1;

   } else if(strcmp(argpt, "--hardlinks")==0) {
     Xorriso_option_hardlinks(xorriso, "on", 0);
   } else if(strcmp(argpt, "--acl")==0) {
     Xorriso_option_acl(xorriso, "on", 0);
   } else if(strcmp(argpt, "--xattr")==0) {
     Xorriso_option_xattr(xorriso, "on", 0);
   } else if(strcmp(argpt, "--md5")==0) {
     Xorriso_option_md5(xorriso, "on", 0);
   } else if(strcmp(argpt, "--scdbackup_tag")==0) {
     if(i + 2 >= argc)
       goto not_enough_args;
     i+= 2;
     ret= Xorriso_option_scdbackup_tag(xorriso, argv[i-1], argv[i], 0);
     if(ret <= 0)
       goto problem_handler_1;
   } else if(strcmp(argpt, "--for_backup")==0) {
     Xorriso_option_hardlinks(xorriso, "on", 0);
     Xorriso_option_acl(xorriso, "on", 0);
     Xorriso_option_xattr(xorriso, "on", 0);
     Xorriso_option_md5(xorriso, "on", 0);
   } else if(strcmp(argpt, "-z")==0 ||
             strcmp(argpt, "-transparent-compression")==0) {
     ret= Xorriso_genisofs_fused_options(xorriso, whom, "z",
                                  &option_d, &iso_level, &lower_r, ra_text, 2);
     if(ret <= 0)
       goto problem_handler_1;
     Xorriso_option_zisofs(xorriso, "by_magic=on", 0);
   } else if(strcmp(argpt, "--stdio_sync")==0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     Xorriso_option_stdio_sync(xorriso, argv[i], 0);
   } else if(strcmp(argpt, "-disallow_dir_id_ext")==0) {
     allow_dir_id_ext= 0;
   } else if(strcmp(argpt, "--emul-toc")==0) {
     with_emul_toc= 1;
     xorriso->no_emul_toc&= ~1;
   } else if(strcmp(argpt, "--no-emul-toc")==0) {
     with_emul_toc= 0;
     xorriso->no_emul_toc|= 1;
   } else if(strcmp(argpt, "-log-file") == 0) {
     /* already handled before this loop */;
   } else {
     if(argv[i][0] == '-') {
       ret= Xorriso_genisofs_fused_options(xorriso, whom, argv[i] + 1,
                              &option_d, &iso_level, &lower_r, ra_text, 1 | 2);
       if(ret != 1)
         was_other_option= 1;
     } else {
       ret= 0;
       was_other_option= 1;
     }
     if(ret > 0) {
       Xorriso_genisofs_fused_options(xorriso, whom, argv[i] + 1,
                                  &option_d, &iso_level, &lower_r, ra_text, 2);
       if(ret <= 0) 
         goto problem_handler_1;
     } else {
       hargv[0]= argpt;
       ret= Xorriso_genisofs_count_args(xorriso, argc - i, hargv, &count, 0);
       if(ret > 0)
         i+= count; /* skip eventual arguments of known option */
     }
   }
 continue; /* regular bottom of loop */
problem_handler_1:;
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret>=0)
 continue;
   goto ex;
 }
 Xorriso_option_report_about(xorriso, ra_text, 0);
 if(adr[0]) {
   if(strncmp(adr, "stdio:", 6)==0 && strncmp(adr, "stdio:/dev/fd/", 14)!=0) {
     ret= Sfile_type(adr+6, 1);
     if(ret==-1) {
       /* ok */;
     } else if(ret==2 || ret==3) {
       sprintf(xorriso->info_text,
               "-as %s: Cannot accept %s as target: -o %s",
               whom, (ret==3 ? "symbolic link" : "directory"),
               Text_shellsafe(adr+6, sfe, 0));
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
     }
   }
   /* Regard overwriteable as blank, truncate regular files on write start */
   ret= Xorriso_option_dev(xorriso, adr, 2|8|16);
   if(ret<=0)
     goto ex;
 }

 if(was_other_option && xorriso->out_drive_handle==NULL) {
   ret= Xorriso_option_dev(xorriso, "-", 2|4); /* set outdev to stdout */
   if(ret<=0)
     goto ex;
 }

 if(msc[0]) {
   cpt= strchr(msc, ',');
   if(cpt==NULL) {
illegal_c:;
     sprintf(xorriso->info_text,
             "-as %s: unusable parameter with option -C: %s",
             whom, Text_shellsafe(msc, sfe, 0));
     Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
     {ret= 0; goto ex;}
   } else if(cpt==msc || msc[1]==0)
     goto illegal_c;
   strncpy(sfe, msc, cpt-msc);
   sfe[cpt-msc]= 0;
   if(xorriso->in_drive_handle!=NULL && indev[0]) {
     /* give up indev before setting the load address */
     ret= Xorriso_option_dev(xorriso, "", 1);
     if(ret<=0)
       goto ex;
   }
   /* growisofs submits msc1+16 to avoid a theoretical bug in mkisofs.
      Therefore this bug has to be emulated here. Sigh.
   */
   ret= Xorriso_option_load(xorriso, "sbsector", sfe, 1);
   if(ret<=0)
     goto ex;
   ret= Xorriso_option_grow_blindly(xorriso, cpt+1, 0);
   if(ret<=0)
     goto ex;
 }

 if(old_root[0] || root_seen) {
   Xorriso_option_md5(xorriso, old_root_md5 ? "on" : "off", 0);
   Xorriso_option_disk_dev_ino(xorriso,
                               old_root_dev && old_root_ino ? "on" :
                               old_root_ino ? "ino_only" : "off", 0);
   if(!old_root_ino)
     Xorriso_option_hardlinks(xorriso, "without_update", 0);
 }
 if(indev[0]) {
   do_md5_mem= xorriso->do_md5;
   if(xorriso->do_md5 & 1) /* MD5 loading is enabled */
     xorriso->do_md5|= 32; /* Do not check tags of superblock,tree,session
                              because growisofs preserves the first sb tag.*/
   ret= Xorriso_option_dev(xorriso, indev, 1);
   xorriso->do_md5= do_md5_mem;
   if(ret<=0)
     goto ex;
 }

 if(!was_other_option)
   {ret= 1; goto ex;}

 if(old_root[0]) {
   ret= Xorriso_iso_lstat(xorriso, old_root, &stbuf, 0);
   if(ret >= 0) {
     if(root_seen) {
       ret= Xorriso_mkdir(xorriso, xorriso->wdi, 1 | 2);
       if(ret < 0)
         {ret= -(ret != -1); goto ex;}
     } else {
       strcpy(xorriso->wdi, "/");
     }
     if(strcmp(old_root, xorriso->wdi) != 0) {
       ret= Xorriso_clone_under(xorriso, old_root, xorriso->wdi, 0);
       if(ret <= 0)
         goto ex;
     }
   }
 }

 xorriso->padding= 300*1024;

 for(i= 0; i<argc; i++) {
   sprintf(xorriso->info_text, "-as %s: %s",
           whom, Text_shellsafe(argv[i], sfe, 0));
   Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "DEBUG", 0);
   ret= Xorriso_genisofs_strip_dash(xorriso, argv[i], &argpt, 0);
   if(ret <= 0)
     goto ex;
   ret= Xorriso_genisofs_ignore(xorriso, whom, argpt, &i, 0);
   if(ret == 1)
 continue;
   if(strcmp(argpt, "-version")==0) {
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "--norock")==0) {
     xorriso->do_rockridge= 0;
   } else if(strcmp(argpt, "-R")==0 || strcmp(argpt, "-rock")==0) {
     ret= Xorriso_genisofs_fused_options(xorriso, whom, "R",
                                  &option_d, &iso_level, &lower_r, ra_text, 0);
     if(ret <= 0)
       goto problem_handler_2;
   } else if(strcmp(argpt, "-r")==0 || strcmp(argpt, "-rational-rock")==0){
     ret= Xorriso_genisofs_fused_options(xorriso, whom, "r",
                                  &option_d, &iso_level, &lower_r, ra_text, 0);
     if(ret <= 0)
       goto problem_handler_2;
   } else if(strcmp(argpt, "-J")==0 || strcmp(argpt, "-joliet")==0) {
     ret= Xorriso_genisofs_fused_options(xorriso, whom, "J",
                                  &option_d, &iso_level, &lower_r, ra_text, 0);
     if(ret <= 0)
       goto problem_handler_2;
   } else if(strcmp(argpt, "-joliet-long")==0) {
     Xorriso_relax_compliance(xorriso,
                              "joliet_long_paths:joliet_long_names", 0);
   } else if(strcmp(argpt, "-fat") == 0) {
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "-hfs-bless") == 0 ||
             strcmp(argpt, "-hfs-bless-by") == 0 ||
             strcmp(argpt, "-hfsplus-file-creator-type") == 0) {
     arg_count= 1;
     if(strcmp(argpt, "-hfs-bless-by") == 0)
       arg_count= 2;
     else if(strcmp(argpt, "-hfsplus-file-creator-type") == 0)
       arg_count= 3;
     if(i + arg_count >= argc)
       goto not_enough_args;
     /* Memorize command until all pathspecs are processed */
     delay_opt_list[delay_opt_count++]= i;
     if(argv[i] != argpt)
       delay_opt_list[delay_opt_count - 1]|= 1<<31;
     i+= arg_count;
   } else if(strcmp(argpt, "-hfsplus") == 0) {
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "-hfsplus-serial-no") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     sprintf(pathspec, "hfsplus_serial=%.80s", argv[i]);
     ret= Xorriso_option_boot_image(xorriso, "any", pathspec, 0);
     if(ret <= 0)
       goto problem_handler_2;
   } else if(strcmp(argpt, "-hfsplus-block-size") == 0 ||
             strcmp(argpt, "-apm-block-size") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     ret= -1;
     sscanf(argv[i], "%d", &ret);
     if(argpt[1] == 'h')
       sprintf(sfe, "hfsplus_block_size=%d", ret);
     else
       sprintf(sfe, "apm_block_size=%d", ret);
     ret= Xorriso_option_boot_image(xorriso, "any", sfe, 0);
     if(ret <= 0)
       goto problem_handler_2;

   } else if(strcmp(argpt, "-graft-points")==0) {
     xorriso->allow_graft_points= 1;
   } else if(strcmp(argpt, "-path-list")==0 ||
             strcmp(argpt, "--quoted_path_list")==0) {
     if(i+1>=argc) {
not_enough_args:;
       sprintf(xorriso->info_text, "-as %s: Not enough arguments to option %s",
               whom, argv[i]);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       ret= 0; goto ex;
     }
     i++;
     xorriso->pacifier_style= 1;
     ret= Xorriso_option_path_list(xorriso, argv[i],
                                   (strcmp(argpt, "--quoted_path_list")==0));
     if(ret<=0)
       goto problem_handler_2;
     ret = Xorriso_genisofs_path_pecul(xorriso, &was_path, with_emul_toc,
                                       &allow_dir_id_ext, &iso_level, 0);
     if(ret <= 0)
       goto ex;
   } else if(strcmp(argpt, "-f")==0 || strcmp(argpt, "-follow-links")==0) {
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "-pad")==0) {
     xorriso->padding= 300*1024;
   } else if(strcmp(argpt, "-no-pad")==0) {
     xorriso->padding= 0;
   } else if(strcmp(argpt, "-print-size")==0) {
     do_print_size= 1;
   } else if(strcmp(argpt, "-o")==0 || strcmp(argpt, "-output") == 0) {
     i++;
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "-M")==0  || strcmp(argpt, "-dev")==0 ||
             strcmp(argpt, "-prev-session")==0) {
     i++;
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "-C")==0 ||
             strcmp(argpt, "-cdrecord-params")==0) {
     i++;
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "-help")==0) {
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "-V")==0 || strcmp(argpt, "-volid")==0 ||
             strcmp(argpt, "-volset")==0 ||
             strcmp(argpt, "-p")==0 || strcmp(argpt, "-preparer")==0 ||
             strcmp(argpt, "-P")==0 || strcmp(argpt, "-publisher")==0 ||
             strcmp(argpt, "-A")==0 || strcmp(argpt, "-appid")==0 ||
             strcmp(argpt, "-sysid")==0 ||
             strcmp(argpt, "-biblio")==0 ||
             strcmp(argpt, "-copyright")==0 ||
             strcmp(argpt, "-abstract")==0 ) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     ret= 1;
     if(strcmp(argpt, "-V")==0 || strcmp(argpt, "-volid")==0)
       ret= Xorriso_option_volid(xorriso, argv[i], 0);
     else if(strcmp(argpt, "-volset")==0)
       ret= Xorriso_option_volset_id(xorriso, argv[i], 0);
     else if(strcmp(argpt, "-p")==0 ||
             strcmp(argpt, "-preparer")==0)
       ret= Xorriso_option_preparer_id(xorriso, argv[i], 0);
     else if(strcmp(argpt, "-P")==0 ||
             strcmp(argpt, "-publisher")==0)
       ret= Xorriso_option_publisher(xorriso, argv[i], 0);
     else if(strcmp(argpt, "-A")==0 || strcmp(argpt, "-appid")==0)
       ret= Xorriso_option_application_id(xorriso, argv[i], 0);
     else if(strcmp(argpt, "-sysid")==0)
       ret= Xorriso_option_system_id(xorriso, argv[i], 0);
     else if(strcmp(argpt, "-biblio")==0)
       ret= Xorriso_option_biblio_file(xorriso, argv[i], 0);
     else if(strcmp(argpt, "-copyright")==0)
       ret= Xorriso_option_copyright_file(xorriso, argv[i], 0);
     else if(strcmp(argpt, "-abstract")==0)
       ret= Xorriso_option_abstract_file(xorriso, argv[i], 0);
     if(ret<=0)
       goto problem_handler_2;
   } else if(strcmp(argpt, "-m")==0 || strcmp(argpt, "-exclude")==0 ||
             strcmp(argpt, "-x")==0 || strcmp(argpt, "-old-exclude")==0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     mem= xorriso->do_disk_pattern;
     xorriso->do_disk_pattern= 1;
     if(strchr(argv[i], '/')!=NULL) {
       idx= i;
       ret= Xorriso_option_not_paths(xorriso, i+1, argv, &idx, 0);
     } else
       ret= Xorriso_option_not_leaf(xorriso, argv[i], 0);
     xorriso->do_disk_pattern= mem;
     if(ret<=0)
       goto problem_handler_2;
   } else if(strcmp(argpt, "-exclude-list")==0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     mem= xorriso->do_disk_pattern;
     xorriso->do_disk_pattern= 1;
     ret= Xorriso_option_not_list(xorriso, argv[i], 0);
     xorriso->do_disk_pattern= mem;
     if(ret<=0)
       goto problem_handler_2;
   } else if(strcmp(argpt, "-v")==0 || strcmp(argpt, "-verbose")==0 ||
             strcmp(argpt, "-quiet")==0) {
     /* was already handled in first argument scan */;

   } else if(strcmp(argpt, "-iso-level")==0) {
     i++;
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "-no-emul-boot")==0 ||
             strcmp(argpt, "-hard-disk-boot")==0 ||
             strcmp(argpt, "-boot-info-table")==0 ||
             strcmp(argpt, "--grub2-boot-info") == 0 ||
             strncmp(argpt, "isolinux_mbr=", 13)==0 ||
             strcmp(argpt, "-eltorito-alt-boot")==0 ||
             strcmp(argpt, "--protective-msdos-label")==0 ||
             strcmp(argpt, "--boot-catalog-hide")==0 ||
             strcmp(argpt, "-isohybrid-gpt-basdat")==0 ||
             strcmp(argpt, "-isohybrid-gpt-hfsplus")==0 ||
             strcmp(argpt, "-isohybrid-apm-hfsplus")==0) {
     delay_opt_list[delay_opt_count++]= i;
     if(argv[i] != argpt)
       delay_opt_list[delay_opt_count - 1]|= 1<<31;
   } else if(strcmp(argpt, "-b") == 0 ||
             strcmp(argpt, "-eltorito-boot") == 0 ||
             strcmp(argpt, "--efi-boot") == 0 ||
             strcmp(argpt, "-e") == 0 ||
             strcmp(argpt, "-mips-boot") == 0 ||
             strcmp(argpt, "-mipsel-boot") == 0 ||
             strcmp(argpt, "-c") == 0 ||
             strcmp(argpt, "-eltorito-catalog") == 0 ||
             strcmp(argpt, "-boot-load-size") == 0 ||
             strcmp(argpt, "-eltorito-id") == 0 ||
             strcmp(argpt, "-eltorito-selcrit") == 0 ||
             strcmp(argpt, "--embedded-boot")==0 ||
             strcmp(argpt, "-generic-boot")==0 ||
             strcmp(argpt, "-G") == 0 ||
             strcmp(argpt, "-partition_offset") == 0 ||
             strcmp(argpt, "-partition_hd_cyl") == 0 ||
             strcmp(argpt, "-partition_sec_hd") == 0 ||
             strcmp(argpt, "-partition_cyl_align") == 0 ||
             strcmp(argpt, "-isohybrid-mbr") == 0 ||
             strcmp(argpt, "--grub2-mbr") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     delay_opt_list[delay_opt_count++]= i;
     if(argv[i] != argpt)
       delay_opt_list[delay_opt_count - 1]|= 1<<31;
     i++;
   } else if(strncmp(argpt, "--modification-date=", 20)==0) {
     ret= Xorriso_option_volume_date(xorriso, "uuid", argpt + 20, 0);
     if(ret <= 0)
       goto problem_handler_2;

   } else if(strcmp(argpt, "-input-charset")==0) {
     i++;
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "-output-charset")==0) {
     i++;
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "--hardlinks")==0 ||
             strcmp(argpt, "--acl")==0 ||
             strcmp(argpt, "--xattr")==0 ||
             strcmp(argpt, "--md5")==0 ||
             strcmp(argpt, "--for_backup")==0) {
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "--scdbackup_tag")==0) {
     /* was already handled in first argument scan */;
     i+= 2;
   } else if(strcmp(argpt, "--sort-weight")==0) {
     if(i + 2 >= argc)
       goto not_enough_args;
     i+= 2;
     /* memorize for find runs after pathspecs have been added */
     weight_list[weight_count++]= i - 2;
   } else if(strcmp(argpt, "-z")==0 ||
             strcmp(argpt, "-transparent-compression")==0) {
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "-U") == 0 ||
             strcmp(argpt, "-untranslated-filenames") == 0) {
     ret= Xorriso_genisofs_fused_options(xorriso, whom, "U",
                                  &option_d, &iso_level, &lower_r, ra_text, 0);
     if(ret <= 0)
       goto problem_handler_2;
   } else if(strcmp(argpt, "-untranslated_name_len") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     sprintf(sfe, "untranslated_name_len=%s", argv[i]);
     ret= Xorriso_relax_compliance(xorriso, sfe, 0);
     if(ret <= 0)
       goto problem_handler_2;
   } else if(strcmp(argpt, "-N") == 0 ||
             strcmp(argpt, "-omit-version-number") == 0) {
     ret= Xorriso_genisofs_fused_options(xorriso, whom, "N",
                                  &option_d, &iso_level, &lower_r, ra_text, 0);
     if(ret <= 0)
       goto problem_handler_2;
   } else if(strcmp(argpt, "-l") == 0 ||
             strcmp(argpt, "-full-iso9660-filenames") == 0) {
     ret= Xorriso_genisofs_fused_options(xorriso, whom, "l",
                                  &option_d, &iso_level, &lower_r, ra_text, 0);
     if(ret <= 0)
       goto problem_handler_2;
   } else if(strcmp(argpt, "-max-iso9660-filenames") == 0) {
     Xorriso_relax_compliance(xorriso, "long_names", 0);
   } else if(strcmp(argpt, "-d") == 0 ||
             strcmp(argpt, "-omit-period") == 0) {
     ret= Xorriso_genisofs_fused_options(xorriso, whom, "d",
                                  &option_d, &iso_level, &lower_r, ra_text, 0);
     if(ret <= 0)
       goto problem_handler_2;
   } else if(strcmp(argpt, "-allow-lowercase") == 0) {
     Xorriso_relax_compliance(xorriso, "lowercase", 0);
   } else if(strcmp(argpt, "-relaxed-filenames") == 0) {
     Xorriso_relax_compliance(xorriso, "7bit_ascii", 0);
   } else if(strcmp(argpt, "-hide") == 0 ||
             strcmp(argpt, "-hide-list") == 0 ||
             strcmp(argpt, "-hide-joliet") == 0 ||
             strcmp(argpt, "-hide-joliet-list") == 0 ||
             strcmp(argpt, "-hide-hfsplus") == 0 ||
             strcmp(argpt, "-hide-hfsplus-list") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "-root") == 0 ||
             strcmp(argpt, "-old-root") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "--old-root-no-md5")==0 ||
             strcmp(argpt, "--old-root-devno")==0 ||
             strcmp(argpt, "--old-root-no-ino")==0) {
     /* was already handled in first argument scan */;
   } else if(strcmp(argpt, "-dir-mode") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     ret= Xorriso_convert_modstring(xorriso, "-as mkisofs -dir-mode",
                                    argv[i], &mode_and, &mode_or, 0);
     if(ret<=0)
       goto problem_handler_2;
     dir_mode= mode_or;
   } else if(strcmp(argpt, "-file-mode") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     ret= Xorriso_convert_modstring(xorriso, "-as mkisofs -file-mode",
                                    argv[i], &mode_and, &mode_or, 0);
     if(ret<=0)
       goto problem_handler_2;
     file_mode= mode_or;
   } else if(strcmp(argpt, "-jigdo-jigdo") == 0 ||
             strcmp(argpt, "-jigdo-template") == 0 ||     
             strcmp(argpt, "-jigdo-min-file-size") == 0 ||     
             strcmp(argpt, "-jigdo-exclude") == 0 ||     
             strcmp(argpt, "-jigdo-force-md5") == 0 ||     
             strcmp(argpt, "-jigdo-map") == 0 ||     
             strcmp(argpt, "-jigdo-template-compress") == 0 ||     
             strcmp(argpt, "-checksum_algorithm_iso") == 0 ||     
             strcmp(argpt, "-checksum_algorithm_template") == 0 ||     
             strcmp(argpt, "-md5-list") == 0) {
     i++;
     ret= Xorriso_option_jigdo(xorriso, argpt, argv[i], 0);
     if(ret <= 0)
       goto problem_handler_2;

   } else if(strcmp(argpt, "-chrp-boot-part") == 0) {
     ret= Xorriso_option_boot_image(xorriso, "any", "chrp_boot_part=on", 0);
     if(ret <= 0)
       goto problem_handler_2;

   } else if(strcmp(argpt, "-prep-boot-part") == 0) {
     if(i + 1 >= argc)
       goto not_enough_args;
     i++;
     ret= Sfile_str(xorriso->prep_partition, argv[i], 0);
     if(ret <= 0)
       goto ex;

   } else if(strcmp(argpt, "-efi-boot-part") == 0) {
     if(i + 1 >= argc)
       goto not_enough_args;
     i++;
     ret= Sfile_str(xorriso->efi_boot_partition, argv[i], 0);
     if(ret <= 0)
       goto ex;

   } else if(strcmp(argpt, "-append_partition") == 0) {
     if(i + 3 >= argc)
       goto not_enough_args;
     i+= 3;
     ret= Xorriso_option_append_partition(xorriso, argv[i - 2], argv[i - 1],
                                          argv[i], 0);
     if(ret <= 0)
       goto problem_handler_2;

   } else if(strcmp(argpt, "-B") == 0 ||
             strcmp(argpt, "-sparc-boot") == 0) {
     i++;
     if(strlen(argv[i]) >= SfileadrL)
 continue;

     /* Switch system area type to: SUN Disk Label */
     strcpy(pathspec, "sparc_label=");
     strcat(pathspec, xorriso->ascii_disc_label);
     ret= Xorriso_option_boot_image(xorriso, "any", pathspec, 0);
     if(ret <= 0)
       goto problem_handler_2;

     /* Interpret list of boot partition images or "..." */;
     cpt= ept= argv[i];
     partition_number= 2;
     while(ept != NULL) {
       ept= strchr(cpt, ',');
       if(ept != NULL) {
         strncpy(pathspec, cpt, ept - cpt);
         pathspec[ept - cpt]= 0;
         cpt= ept + 1;
       } else
         strcpy(pathspec, cpt);
       if(strcmp(pathspec, "...") == 0) {
         for(; partition_number <= 8; partition_number++) {
           sprintf(partno_text, "%d", partition_number);
           ret= Xorriso_option_append_partition(xorriso, partno_text, "0x0",
                                                ".", 0);
           if(ret <= 0)
             goto problem_handler_2;
         }
       } else {
         if(partition_number > 8) {
           sprintf(xorriso->info_text,
                "-as %s -sparc-boot %s : Too many boot images", whom, argv[i]);
           Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE",0);
           goto problem_handler_2;
         }
         sprintf(partno_text, "%d", partition_number);
         ret= Xorriso_option_append_partition(xorriso, partno_text, "0x0",
                                              pathspec, 0);
         if(ret <= 0)
           goto problem_handler_2;
         partition_number++;
       }
     }

   } else if(strcmp(argpt, "-sparc-label") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     strncpy(xorriso->ascii_disc_label, argv[i], Xorriso_disc_label_sizE - 1);
     xorriso->ascii_disc_label[Xorriso_disc_label_sizE - 1] = 0;

   } else if(strcmp(argpt, "--grub2-sparc-core") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     sprintf(sfe, "grub2_sparc_core=%s", argv[i]);
     ret= Xorriso_option_boot_image(xorriso, "any", sfe, 0);
     if(ret <= 0)
       goto problem_handler_2;

   } else if(strcmp(argpt, "--stdio_sync")==0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     /* was already handled in first argument scan */;

   } else if(strcmp(argpt, "--emul-toc")==0 ||
             strcmp(argpt, "--no-emul-toc")==0) {
     /* was already handled in first argument scan */;

   } else if(strcmp(argpt, "--old-empty")==0) {
     xorriso->do_old_empty= 1;

   } else if(strcmp(argpt, "-disallow_dir_id_ext")==0) {
     /* was already handled in first argument scan */;

   } else if(strcmp(argpt, "--no_rc")==0) {
     /* was already handled in Xorriso_prescan_args */;

   } else if(strcmp(argpt, "-D") == 0 ||
             strcmp(argpt, "-disable-deep-relocation") == 0) {
     ret= Xorriso_genisofs_fused_options(xorriso, whom, "D",
                                  &option_d, &iso_level, &lower_r, ra_text, 0);
     if(ret <= 0)
       goto problem_handler_2;

   } else if(strcmp(argpt, "-hide-rr-moved") == 0) {
     rr_reloc_dir_pt= ".rr_moved";
     goto rr_reloc_dir;

   } else if(strcmp(argpt, "-rr_reloc_dir") == 0) {
     i++;
     rr_reloc_dir_pt= argv[i];
rr_reloc_dir:;
     if(rr_reloc_dir_pt[0] == '/')
        rr_reloc_dir_pt++;
     if(strchr(rr_reloc_dir_pt, '/') != NULL) {
       sprintf(xorriso->info_text,
        "-as %s -rr_reloc_dir %s : May only use directories in root directory",
               whom, Text_shellsafe(argv[i], sfe, 0));
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE",0);
     }
     ret= Xorriso_option_rr_reloc_dir(xorriso, rr_reloc_dir_pt, 0);
     if(ret <= 0)
       goto problem_handler_2;
     Xorriso_relax_compliance(xorriso, "deep_paths_off:long_paths_off", 0);

   } else if(strcmp(argpt, "-log-file") == 0) {
     /* was already handled before this loop */;

   } else if(argpt[0]=='-' && argpt[1]!=0) {
     ret= Xorriso_genisofs_fused_options(xorriso, whom, argv[i] + 1,
                                  &option_d, &iso_level, &lower_r, ra_text, 1);
     if(ret == 1) {
       ret= Xorriso_genisofs_fused_options(xorriso, whom, argv[i] + 1,
                                  &option_d, &iso_level, &lower_r, ra_text, 0);
       if(ret <= 0)
         goto problem_handler_2;
     } else {
       hargv[0]= argpt;
       ret= Xorriso_genisofs_count_args(xorriso, argc - i, hargv, &count, 1);
       if(ret > 0) {
         sprintf(xorriso->info_text, "-as %s: Unsupported option %s",
                 whom, Text_shellsafe(argv[i], sfe, 0));
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         i+= count;
         goto problem_handler_2;
       } else {
         sprintf(xorriso->info_text, "-as %s: Unrecognized option %s",
                 whom, Text_shellsafe(argv[i], sfe, 0));
         Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
         goto problem_handler_2;
       }
     }
   } else {
     /* implementing mkisofs tendency to map single-path pathspecs to / */
     if((!xorriso->allow_graft_points) || 
        Fileliste__target_source_limit(argv[i], '=', &ept, 0)<=0) {
       ret= Xorriso_normalize_img_path(xorriso, xorriso->wdx, argv[i],
                                       eff_path, 2|4);
       if(ret<=0)
         goto problem_handler_2;
       ret= Sfile_type(eff_path,
                  1|((xorriso->do_follow_param||xorriso->do_follow_links)<<2));
       if(ret==2) {
         strcpy(pathspec, "/=");
       } else {
         pathspec[0]= '/';
         pathspec[1]= 0;
         ret= Sfile_leafname(eff_path, pathspec+1, 0);
         if(ret>0) {
           ret= Fileliste__escape_source_path(pathspec, SfileadrL, 0);
           if(ret <= 0) {
             Xorriso_msgs_submit(xorriso, 0,
                                 "Escaped leaf name gets much too long",
                                 0, "FAILURE", 0);
             goto problem_handler_2;
           }
           strcat(pathspec, "=");
         } else
           pathspec[0]= 0;
       }
       strcat(pathspec, eff_path);
     } else
       Sfile_str(pathspec, argv[i], 0);
     add_pt= pathspec;

     if(old_root[0]) {
       /* Split pathspec */
       ret= Fileliste__target_source_limit(add_pt, '=', &ept, 0);
       if(ret > 0) {
         *ept= 0;
         iso_rr_pt= add_pt;
         disk_pt= ept + 1;
       } else {
         iso_rr_pt= "/";
         disk_pt= add_pt;
       }

       /* Unescape iso_rr_pt */
       strcpy(eff_path, iso_rr_pt);
       iso_rr_pt= eff_path;
       for(wpt= rpt= iso_rr_pt; *rpt != 0; rpt++) {
         if(*rpt == '\\') {
           if(*(rpt + 1) == '\\')
             rpt++;
           else if(*(rpt + 1) == '=')
       continue;
         }
         *(wpt++) = *rpt;
       }
       *wpt= 0;

       if(root_seen) {
         ret= Sfile_prepend_path(xorriso->wdi, iso_rr_pt, 0);
         if(ret<=0) {
           Xorriso_msgs_submit(xorriso, 0, "Effective path gets much too long",
                               0, "FAILURE", 0);
           goto problem_handler_2;
         }
       }
       /* update_merge */
       ret= Xorriso_option_update(xorriso, disk_pt, iso_rr_pt, 1 | 8 | 32);
     } else {
       mem_graft_points= xorriso->allow_graft_points;
       xorriso->allow_graft_points= 1;
       zero= 0;
       ret= Xorriso_option_add(xorriso, 1, &add_pt, &zero,
                               (was_path << 1) | (root_seen << 2) | 8);
       xorriso->allow_graft_points= mem_graft_points;
     }
     if(ret<=0)
       goto problem_handler_2;

     /* Enforce odd mkisofs defaults on first pathspec */
     ret = Xorriso_genisofs_path_pecul(xorriso, &was_path, with_emul_toc,
                                       &allow_dir_id_ext, &iso_level, 0);
     if(ret <= 0)
       goto ex;
   }
 continue; /* regular bottom of loop */
problem_handler_2:;
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret>=0)
 continue;
   goto ex;
 }

 if(old_root[0]) {
   /* Delete all visited nodes which were not found on disk */
   if(root_seen)
     rm_merge_args[0]= xorriso->wdi;
   else
     rm_merge_args[0]= "/";
   rm_merge_args[1]= "-exec";
   rm_merge_args[2]= "rm_merge";
   zero= 0;
   ret= Xorriso_option_find(xorriso, 3, rm_merge_args, &zero, 2);
   if(ret<=0)
     goto ex;
 }

 if(lower_r) {
   static char *lower_r_args[3]= {"/", "-exec", "mkisofs_r"};
   zero= 0;
   ret= Xorriso_option_find(xorriso, 3, lower_r_args, &zero, 2);
   if(ret<=0)
     goto ex;
 }
 if(dir_mode >= 0) {
   static char *dir_mode_args[6]= {"/", "-type", "d", "-exec", "chmod", ""};
   zero= 0;
   sprintf(sfe, "0%o", (unsigned int) dir_mode);
   dir_mode_args[5]= sfe;
   ret= Xorriso_option_find(xorriso, 6, dir_mode_args, &zero, 2);
   if(ret<=0)
     goto ex;
 }
 if(file_mode >= 0) {
   static char *file_mode_args[6]= {"/", "-type", "f", "-exec", "chmod", ""};
   zero= 0;
   sprintf(sfe, "0%o", (unsigned int) file_mode);
   file_mode_args[5]= sfe;
   ret= Xorriso_option_find(xorriso, 6, file_mode_args, &zero, 2);
   if(ret<=0)
     goto ex;
 }

 for(j= 0; j < weight_count; j++) {
   i= weight_list[j];
   /* find argv[i+2] -exec sort_weight argv[i+1] */
   zero= 0;
   sort_weight_args[0]= argv[i + 2];
   sort_weight_args[1]= "-exec";
   sort_weight_args[2]= "sort_weight";
   sort_weight_args[3]= argv[i + 1];
   ret= Xorriso_option_find(xorriso, 4, sort_weight_args, &zero, 2);
   if(ret > 0)
 continue;
   /* Problem handler */
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret>=0)
 continue;
   goto ex;
 }

 if(option_d)
   Xorriso_relax_compliance(xorriso, "deep_paths:long_paths", 0);

 /* After all pathspecs are added: perform delayed options, mostly boot related
 */
 for(j= 0; j < delay_opt_count; j++) {
   i= delay_opt_list[j] & ~(1 << 31);
   if(delay_opt_list[j] & (1 << 31))
     argpt= argv[i] + 1;
   else
     argpt= argv[i];
   if(strcmp(argpt, "-no-emul-boot")==0) {
     xorriso->boot_image_emul= 0;
     xorriso->boot_emul_default= 0;
   } else if(strcmp(argpt, "-hard-disk-boot")==0) {
     xorriso->boot_image_emul= 1;
     xorriso->boot_emul_default= 0;
   } else if(strcmp(argpt, "-boot-info-table")==0) {
     xorriso->patch_isolinux_image= (xorriso->patch_isolinux_image & ~2) | 1;
   } else if(strcmp(argpt, "--grub2-boot-info") == 0) {
     xorriso->patch_isolinux_image=
                                  (xorriso->patch_isolinux_image & ~2) | 512;
   } else if(strcmp(argpt, "-b") == 0 ||
             strcmp(argpt, "-eltorito-boot") == 0 ||
             strcmp(argpt, "--efi-boot") == 0 ||
             strcmp(argpt, "-e") == 0) {
     i++;
     if(strcmp(argpt, "--efi-boot") == 0) {
       if(xorriso->boot_image_bin_path[0]) {
         ret= Xorriso_genisofs_add_boot(xorriso, 0);
         if(ret <= 0)
           goto problem_handler_boot;
       }
       boot_path= xorriso->boot_image_bin_path;
       xorriso->boot_efi_default= 1;
       xorriso->boot_image_emul= 0;
       xorriso->boot_emul_default= 0;

     } else {
       boot_path= xorriso->boot_image_bin_path; 
       if(strcmp(argpt, "-e") == 0)
         xorriso->boot_platform_id= 0xef;
       else
         xorriso->boot_platform_id= 0x00;
       xorriso->boot_efi_default= 0;
       if(xorriso->boot_emul_default)
         xorriso->boot_image_emul= 2;
     }
     boot_path[0]= 0;
     if(argv[i][0] != '/')
       strcat(boot_path, "/");
     ret= Sfile_str(boot_path + strlen(boot_path), argv[i], 0);
     if(ret <= 0)
       goto ex;
     if(xorriso->boot_efi_default && xorriso->boot_image_bin_path[0]) {
       ret= Xorriso_genisofs_add_boot(xorriso, 0);
       if(ret <= 0)
         goto problem_handler_boot;
     }
     xorriso->keep_boot_image= 0;
     with_boot_image= 1;
   } else if(strcmp(argpt, "-c") == 0 ||
             strcmp(argpt, "-eltorito-catalog") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     xorriso->boot_image_cat_path[0]= 0;
     if(argv[i][0] != '/')
       strcat(xorriso->boot_image_cat_path, "/");
     ret= Sfile_str(xorriso->boot_image_cat_path
                    + strlen(xorriso->boot_image_cat_path), argv[i], 0);
     if(ret <= 0)
       goto ex;
     if(with_cat_path == 0)
       with_cat_path= 1;
   } else if(strcmp(argpt, "-boot-load-size") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     sscanf(argv[i], "%d", &ret);
     xorriso->boot_image_load_size= ret * 512;
   } else if(strcmp(argpt, "-eltorito-id") == 0 ||
             strcmp(argpt, "-eltorito-selcrit") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     if(strcmp(argpt, "-eltorito-id") == 0)
       sprintf(sfe, "id_string=%s", argv[i]);
     else
       sprintf(sfe, "sel_crit=%s", argv[i]);
     ret= Xorriso_option_boot_image(xorriso, "any", sfe, 0);
     if(ret <= 0)
       goto problem_handler_boot;
   } else if(strncmp(argpt, "isolinux_mbr=", 13)==0) {
     sprintf(sfe, "isohybrid=%s", argpt + 13);
     ret= Xorriso_option_boot_image(xorriso, "isolinux", sfe, 0);
     if(ret <= 0)
       goto problem_handler_boot;
   } else if(strcmp(argpt, "-isohybrid-gpt-basdat") == 0) {
     xorriso->patch_isolinux_image = (xorriso->patch_isolinux_image & ~0x1fc) |
                                     (1 << 2);
   } else if(strcmp(argpt, "-isohybrid-gpt-hfsplus") == 0) {
     xorriso->patch_isolinux_image = (xorriso->patch_isolinux_image & ~0x0fc) |
                                     (2 << 2);
   } else if(strcmp(argpt, "-isohybrid-apm-hfsplus") == 0) {
     xorriso->patch_isolinux_image = xorriso->patch_isolinux_image | (1 << 8);
   } else if(strcmp(argpt, "-eltorito-alt-boot")==0) {
     ret= Xorriso_genisofs_add_boot(xorriso, 0);
     if(ret <= 0)
       goto problem_handler_boot;
   } else if(strcmp(argpt, "--embedded-boot")==0 ||
             strcmp(argpt, "-generic-boot")==0 ||
             strcmp(argpt, "-G") == 0 ||
             strcmp(argpt, "-isohybrid-mbr") == 0 ||
             strcmp(argpt, "--grub2-mbr") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++; 
     ret= Xorriso_set_system_area_path(xorriso, argv[i], 0);
     if(ret <= 0)
       goto problem_handler_boot;
     if(strcmp(argpt, "-isohybrid-mbr")==0)
       xorriso->system_area_options=
                                    (xorriso->system_area_options & ~4001) | 2;
     else if(strcmp(argpt, "--grub2-mbr") == 0)
       xorriso->system_area_options= 
                                  (xorriso->system_area_options & ~2) | 0x4000;
   } else if(strcmp(argpt, "--protective-msdos-label")==0) {
     xorriso->system_area_options= (xorriso->system_area_options & ~2) | 1;
   } else if(strcmp(argpt, "--boot-catalog-hide")==0) {
     xorriso->boot_image_cat_hidden|= 3;
   } else if(strcmp(argpt, "-partition_offset") == 0 ||
             strcmp(argpt, "-partition_sec_hd") == 0 ||
             strcmp(argpt, "-partition_hd_cyl") == 0 ||
             strcmp(argpt, "-partition_cyl_align") == 0) {
     if(i+1>=argc)
       goto not_enough_args;
     i++;
     sprintf(sfe, "%s=%.16s", argpt + 1, argv[i]);
     ret= Xorriso_option_boot_image(xorriso, "any", sfe, 0);
     if(ret <= 0)
       goto problem_handler_boot;

   } else if(strcmp(argpt, "-mips-boot") == 0 ||
             strcmp(argpt, "-mipsel-boot") == 0) {
     if(i + 1 >= argc)
       goto not_enough_args;
     i++;
     if(strcmp(argpt, "-mipsel-boot") == 0)
       strcpy(sfe, "mipsel_path=");
     else
       strcpy(sfe, "mips_path=");
     ret= Sfile_str(sfe, argv[i], 1);
     if(ret <= 0)
       goto ex;

     ret= Xorriso_option_boot_image(xorriso, "any", sfe, 0);
     if(ret <= 0)
       goto problem_handler_boot;

   } else if(strcmp(argpt, "-hfs-bless") == 0) {
     static char *bless_arg_data[6]= {
        "/", "-disk_path", "", "-exec", "set_hfs_bless", "p"};

     for(j= 0; j < 6; j++)
       bless_args[j]= bless_arg_data[j];
     bless_args[2]= argv[i + 1];
     zero= 0;
     ret= Xorriso_option_find(xorriso, 6, bless_args, &zero, 2 | 16);
     if(ret<=0)
        goto ex;
     if(ret < 2) {
       sprintf(xorriso->info_text,
               "-hfs-bless: Could not find a data file which stems from underneath disk directory ");
       Text_shellsafe(argv[i + 1], xorriso->info_text, 1);
       Xorriso_msgs_submit(xorriso, 0, xorriso->info_text, 0, "FAILURE", 0);
       Xorriso_msgs_submit(xorriso, 0,
                 "Consider to use: -hfs-bless-by p ISO_RR_PATH", 0, "HINT", 0);
       goto problem_handler_boot;
     }

   } else if(strcmp(argpt, "-hfs-bless-by") == 0) {
     ret= Xorriso_hfsplus_bless(xorriso, argv[i + 2], NULL, argv[i + 1], 0);
     if(ret <= 0)
       goto problem_handler_boot;

   } else if(strcmp(argpt, "-hfsplus-file-creator-type") == 0) {
     ret= Xorriso_hfsplus_file_creator_type(xorriso, argv[i + 3], NULL,
                                            argv[i + 1],  argv[i + 2], 0);
     if(ret <= 0)
       goto problem_handler_boot;

   }
 continue; /* regular bottom of loop */
problem_handler_boot:;
   /* Problem handler */
   was_failure= 1;
   fret= Xorriso_eval_problem_status(xorriso, ret, 1|2);
   if(fret>=0)
 continue;
   goto ex;
 }
 if(with_boot_image && with_cat_path == 0)
   strcpy(xorriso->boot_image_cat_path, "/boot.catalog");
 /* The boot catalog has to be hidden separately */
 if(xorriso->boot_image_cat_path[0]) {
   ret= Xorriso_path_is_hidden(xorriso, xorriso->boot_image_cat_path, 0);
   if(ret > 0)
     xorriso->boot_image_cat_hidden|= ret;
   else if(ret < 0)
     was_failure= 1;
 }

 if(xorriso->no_emul_toc & 1)
   xorriso->do_padding_by_libisofs= 1;

 if(do_print_size) {
   ret= Xorriso_option_print_size(xorriso, 1);
   goto ex;
 }

 ret= !was_failure;
ex:;
 if(was_path && (!do_print_size) && !old_root[0])
   Xorriso_pacifier_callback(xorriso, "files added", xorriso->pacifier_count,
                             xorriso->pacifier_total, "", 1);
 if(do_print_size && Xorriso_change_is_pending(xorriso, 1))
   xorriso->volset_change_pending= 2;
 if(weight_list != NULL)
   free(weight_list);
 if(delay_opt_list != NULL)
   free(delay_opt_list);
 Xorriso_free_meM(sfe);
 Xorriso_free_meM(adr);
 Xorriso_free_meM(pathspec);
 Xorriso_free_meM(eff_path);
 Xorriso_free_meM(indev);
 Xorriso_free_meM(old_root);
 return(ret);
}


int Xorriso_as_genisofs(struct XorrisO *xorriso, int argc, char **argv,
                      int *idx, int flag)
{
 int end_idx, ret, old_idx;

 old_idx= *idx;
 end_idx= Xorriso_end_idx(xorriso, argc, argv, *idx, 1);
 (*idx)= end_idx;
 if(end_idx<=0 || old_idx>=argc)
   return(1);
 ret= Xorriso_genisofs(xorriso, "genisofs", end_idx-old_idx, argv+old_idx, 0);
 return(ret);
}


int Xorriso_as_cdrskin(struct XorrisO *xorriso, int argc, char **argv,
                      int *idx, int flag)
{
 int end_idx, ret, old_idx;

 old_idx= *idx;
 end_idx= Xorriso_end_idx(xorriso, argc, argv, *idx, 1);
 (*idx)= end_idx;
 if(end_idx<=0 || old_idx>=argc)
   return(1);
 ret= Xorriso_cdrskin(xorriso, "cdrskin", end_idx-old_idx, argv+old_idx, 0);
 return(ret);
}

