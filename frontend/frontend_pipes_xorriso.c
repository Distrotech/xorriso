
/* Beefed-up example from man 2 pipe
   to illustrate how xorriso is to be used by frontend programs via two pipes.
   Additionally there is a standalone implementation of Xorriso_parse_line().

   Copyright 2012 Thomas Schmitt, <scdbackup@gmx.net> 
   Unaltered provided under BSD license.
   You may issue licenses of your choice for derived code, provided that they
   do not infringe anybody's right to do the same for this original code.

   Build:
     cc -g -o frontend_pipes_xorriso frontend_pipes_xorriso.c

   Usage:
     ./frontend_pipes_xorriso [path_to_xorriso_binary | -h]

*/

#include <sys/wait.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>


static int usage()
{
    static char helptext[][80] = {
"usage: frontend_pipes_xorriso [path_to_xorriso|-h]",
"",
"Forks a process that runs xorriso and communicates with it via two pipes.",
"The command pipe sends payload commands and -mark commands. The reply pipe",
"receives -pkt_output lines which it dispatches to stdout and stderr.",
"The communication between both processes is made synchronous by the parent",
"awaiting the -mark message of the child.",
"Optionally the reply lines can be parsed into words. This is initiated by",
"meta command",
"  @parse [prefix [separators [max_words [flag]]]]",
"which sets the four parameters for a function equivalent to",
"Xorriso_parse_line() (see xorriso.h). All reply will then be parsed and",
"non-empty word arrays are displayed. Meta command",
"  @noparse",
"ends this mode.",
"Meta command",
"  @drain_sieve",
"reports all recorded results of all installed message sieve filter rules.",
"For illustration perform this xorriso command sequence",
"  -msg_op start_sieve - -outdev /dev/sr0 -msg_op clear_sieve - -toc",
"and then @drain_sieve.", 
"@END@"
    };
    int i;

    for (i = 0; strcmp(helptext[i], "@END@") != 0; i++)
        fprintf(stderr, "%s\n", helptext[i]);
    return(1);
}


/* Local helpers of parent process */

struct boss_state {
    /* What the parent needs to know about its connection to xorriso */

    /* The ends of the dialog pipes */
    int command_fd;
    int reply_fd;

    /* For synchronization between boss and xorriso */
    int mark_count;
    char pending_mark[16];

    /* Parsing_parameters. See xorriso.h Xorriso_parse_line */
    int do_parse;
    char progname[1024];
    char prefix[1024];
    char separators[256];
    int max_words;
    int flag;

    /* A primitive catcher for result lines */
    int reply_lines_size; /* 0= catching disabled */
    char **reply_lines;
    int reply_lines_count;
};

#define Frontend_xorriso_max_resulT 1000


/* Some basic gestures of this program: */

static int prompt_for_command(struct boss_state *boss,
                              char *line, int line_size);

static int transmit_command(struct boss_state *boss, char *line);

static int await_all_replies(struct boss_state *boss);

static int de_pkt_result(struct boss_state *boss);

static int drain_sieve(struct boss_state *boss);

int parse_line(char *progname, char *line,
               char *prefix, char *separators, int max_words,
               int *argc, char ***argv, int flag);

int dispose_parsed_words(int *argc, char ***argv);



/* Parent and child */
int main(int argc, char *argv[])
{
    int command_pipe[2], reply_pipe[2];
    pid_t cpid;
    char *xorriso_path = "/usr/bin/xorriso";

    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "-help") == 0 ||
            strcmp(argv[1], "--help") == 0) {
            usage();
            exit(0);
        }
        xorriso_path = argv[1];
    }

    if (pipe(command_pipe) == -1)
        { perror("pipe"); exit(1); }
    if (pipe(reply_pipe) == -1)
        { perror("pipe"); exit(1); }

    cpid = fork();
    if (cpid == -1)
        { perror("fork"); exit(2); }

    if (cpid == 0) {
        /* Child redirects stdin and stdout. Then it becomes xorriso. */

        char *xargv[8];

        close(command_pipe[1]);        /* Close unused write end */
        close(reply_pipe[0]);          /* Close unused read end */

        /* Attach pipe ends to stdin and stdout */
        close(0);
        if (dup2(command_pipe[0], 0) == -1)
            { perror("dup2(,0)"); exit(1); }
        close(1);
        if (dup2(reply_pipe[1], 1) == -1)
            { perror("dup2(,1)"); exit(1); }

        xargv[0] = xorriso_path;
        xargv[1] = "-dialog";
        xargv[2] = "on";
        xargv[3] = "-pkt_output";
        xargv[4] = "on";
        xargv[5] = "-mark";
        xargv[6] = "0";       /* corresponds to mark_count = 0 in parent */
        xargv[7] = NULL;
        execv(xorriso_path, xargv);
        perror("execv"); exit(1);

    } else {
        /* Parent prompts user for command lines and prints xorriso replies.
           It knows when all reply text of the pending command line has arrived
           by watching for -mark reply pending_mark. 
        */

        int ret;
        char line[4096];
        struct boss_state boss;

        close(command_pipe[0]);        /* Close unused read end */
        close(reply_pipe[1]);          /* Close unused write end */

        memset(&boss, 0, sizeof(boss));
        boss.command_fd = command_pipe[1];
        boss.reply_fd = reply_pipe[0];
        strcpy(boss.progname, argv[0]);
        boss.reply_lines = NULL;

        /* Dialog loop */
        sprintf(boss.pending_mark, "%d", boss.mark_count);
        while (1) {

            /* Wait for pending mark and print all replies */
            ret = await_all_replies(&boss);
            if (ret < 0)
        break;

            /* Prompt for command line */
            ret = prompt_for_command(&boss, line, sizeof(line));
            if (ret <= 0)
        break;

            /* Send line and -mark command */
            ret = transmit_command(&boss, line);
            if (ret <= 0)
        break;
        }


        /* >>> if child is still operational: send -rollback_end */;

        /* >>> wait a short while */;

        /* >>> if not yet ended: kill child  */;

        wait(NULL);             /* Wait for child */
        exit(0);
    }
}


/* ------------------- Local helpers of parent process -------------------- */

static int show_parsed(struct boss_state *boss, char *line);
static int record_reply_line(struct boss_state *boss, char *line);
static int make_reply_lines(struct boss_state *boss);
static int input_interpreter(char *line, struct boss_state *boss);


/* Ask the user for command input and trigger processing of meta commands.
*/
static int prompt_for_command(struct boss_state *boss,
                              char *line, int line_size)
{
    int l, ret;
    char *line_res;

    while (1) {
        fprintf(stderr, "+++ Enter a command and its parameters :\n");
        line_res = fgets(line, line_size - 1, stdin);
        if (line_res == NULL)
            return(0);
        l = strlen(line);
        if (l == 0) {
            line[0] = '\n';
            line[1] = 0;
        } else if (line[l - 1] != '\n') {
            line[l] = '\n';
            line[l + 1] = 0;
        }
        /* Interpret meta commands which begin by @ */
        ret = input_interpreter(line, boss);
        if (ret == 0)
            return(1);
        if (ret < 0)
            return(-1);
    }
}


/* Transmit a command (which must end by white space, e.g. newline)
   and its unique synchronization mark.
*/
static int transmit_command(struct boss_state *boss, char *line)
{
    int ret;
    char mark_line[32];

    ret = write(boss->command_fd, line, strlen(line));
    if (ret == -1) {
        perror("write");
        return(0);
    }
    /* Produce new unique -mark text to watch for */
    (boss->mark_count)++;
    sprintf(boss->pending_mark, "%d", boss->mark_count);
    sprintf(mark_line, "-mark %s\n", boss->pending_mark);
    ret = write(boss->command_fd, mark_line, strlen(mark_line));
    if (ret == -1) {
        perror("write");
        return(0);
    }
    return(1);
}


/* Read reply messages from xorriso and wait for the expected synchronization
   mark. Messages can be printed or collected in boss->reply_lines.
*/
static int await_all_replies(struct boss_state *boss)
{
    int count, remainder = 0, ret;
    char buf[32769], *line, *npt;

    while (1) {
        count = read(boss->reply_fd, buf + remainder,
                     sizeof(buf) - 1 - remainder);
        if (count == -1) {
            perror("read");
            return(-1);
        }
        if (count == 0) {
            fprintf(stderr, "+++ EOF encounterd by Master process\n");
            return(-2);
        }
        for (npt = buf + remainder; npt < buf + count; npt++) {
             if (*npt == 0) {
                    fprintf(stderr,
                            "+++ Protocol error : Reply contains 0-chars\n");
                    return(-1);
             }
        }

        /* Split buf into lines */
        buf[remainder + count] = 0; /* for convenience */
        line = buf;
        while (1) {
            npt = strchr(line, '\n');
            if (npt == NULL) {
                /* Move line to start of buffer and set remainder */
                if (line != buf) {
                    remainder = 0;
                    for (npt = line; *npt; npt++)
                         buf[remainder++] = *npt;
                }
                /* Now read more data in the hope to get a newline char */
        break;
            }
            /* Interpret line */
            *npt = 0;
            if (line[0] == 'M') {
                /* M-replies will be outdated until the pending command line
                   is completely done and the appended -mark command gets
                   into effect.
                */
                if (strlen(line) < 6) {
                    fprintf(stderr,
                 "+++ Protocol error : M-channel line shorter than 6 chars\n");
                    return(-1);
                }
                if (strcmp(line + 5, boss->pending_mark) == 0) {
                    if ((line - buf) + strlen(line) + 1 < count) {
                        fprintf(stderr,
                    "+++ Protocol error : Surplus reply data after M-match\n");
                        fprintf(stderr, "%s\n", line + strlen(line) + 1);
                        return(-1);
                    }
                    return (1); /* Expected mark has arrived */
                }
            } else if (line[0] == 'R') {
                /* R-replies are result lines of inquiry commands, like -ls.
                   They should be handled by specialized code which knows
                   how to parse and interpret them.
                */
                if (boss->reply_lines_count < boss->reply_lines_size) {
                    ret = record_reply_line(boss, line);
                    if (ret <= 0)
                        return(ret);
                } else
                    printf("%s\n", line);
            } else {
                /* I-replies are pacifiers, notifications, warnings, or
                   error messages. They should be handled by a general
                   message interpreter which determines their severity
                   and decides whether to bother the user.
                */
                if (boss->reply_lines_count < boss->reply_lines_size) {
                    ret = record_reply_line(boss, line);
                    if (ret <= 0)
                        return(ret);
                } else
                    fprintf(stderr, "%s\n", line);
            }

            /* Parse line and show words */
            if (strlen(line) >= 5)
                show_parsed(boss, line + 5);

            line = npt + 1;
        }
    }
    return(1);
}


/* Throw away I channel.
   Unpack and reconstruct payload of R channel lines.
*/
static int de_pkt_result(struct boss_state *boss)
{
    int i, l, w;
    char *payload = NULL, *new_payload = NULL;

    w = 0;
    for (i = 0; i < boss->reply_lines_count; i++) {
        if (boss->reply_lines[i][0] != 'R' ||
            strlen(boss->reply_lines[i]) < 5)
    continue;

        if (payload == NULL) {
            payload = strdup(boss->reply_lines[i] + 5);
        } else {
            l = strlen(payload);
            new_payload = calloc(l + strlen(boss->reply_lines[i] + 5) + 1, 1);
            if (new_payload == NULL)
                goto no_mem;
            strcpy(new_payload, payload);
            strcpy(new_payload + l, boss->reply_lines[i] + 5);
            free(payload);
            payload = new_payload;
        }
        if (payload == NULL)
            goto no_mem;
        l = strlen(payload);
        if (l > 0)
           if (payload[l - 1] == '\n')
               payload[l - 1] = 0;

        if (boss->reply_lines[i][2] != '0') {
           free(boss->reply_lines[w]);
           boss->reply_lines[w] = payload;
           w++;
           payload = NULL;
        }
    }
    for (i = w ; i < boss->reply_lines_count; i++) {
        free(boss->reply_lines[i]);
        boss->reply_lines[i] = NULL;
    }
    boss->reply_lines_count = w;
    return(1);
no_mem:;
    fprintf(stderr, "FATAL: Out of memory !\n");
    return(-1);
}


/* Inquire and print all recorded message sieve results.
*/
static int drain_sieve(struct boss_state *boss)
{
    int ret, i, j, names_size = 0, names_count = 0, first_result;
    int number_of_strings, available, xorriso_ret, number_of_lines, k, r;
    char **names = NULL, line[1024];

    /* Install catcher for reply_lines */
    ret = make_reply_lines(boss);
    if (ret <= 0)
        goto ex;
    boss->reply_lines_size = Frontend_xorriso_max_resulT;
    boss->reply_lines_count = 0;

    /* Get list of filter rule names from -msg_op show_sieve */
    ret = transmit_command(boss, "-msg_op show_sieve -\n");
    if (ret <= 0)
        goto ex;
    ret = await_all_replies(boss);
    if (ret <= 0)
        goto ex;
    ret = de_pkt_result(boss);
    if (ret <= 0)
        goto ex;

    names = boss->reply_lines;
    boss->reply_lines = NULL;
    names_size = Frontend_xorriso_max_resulT;
    names_count= boss->reply_lines_count;
    ret = make_reply_lines(boss);
    if (ret <= 0)
        goto ex;
    boss->reply_lines_size = Frontend_xorriso_max_resulT;

    /* Inquire caught results of each name by -msg_op read_sieve
       until return value is <= 0
    */
    printf("--------------------------------------------------\n");
    for (i = 0; i < names_count; i++) {
        available = 1;
        first_result = 1;
        while (available > 0) {
            boss->reply_lines_count = 0;
            sprintf(line, "-msg_op read_sieve '%s'\n", names[i]);
            ret = transmit_command(boss, line);
            if (ret <= 0)
                goto ex;
            ret = await_all_replies(boss);
            if (ret <= 0)
                goto ex;
            ret = de_pkt_result(boss);
            if (ret <= 0)
                goto ex;

            if (boss->reply_lines_count < 2) {
                fprintf(stderr, "drain_sieve: illegible result reply\n");
                {ret= 0; goto ex;}
            }
            xorriso_ret = -1;
            sscanf(boss->reply_lines[0], "%d", &xorriso_ret);
            if(xorriso_ret <= 0)
        break;
            number_of_strings = -1;
            sscanf(boss->reply_lines[1], "%d", &number_of_strings);
            if(xorriso_ret < 0)
        break;
            if (first_result)
                printf(" '%s' |\n", names[i]);
            first_result = 0;
            for (j = 0; names[i][j] != 0; j++)
                printf("-");
            printf("-----\n");
            r = 2;
            for (j = 0; j < number_of_strings && r < boss->reply_lines_count;
                 j++) {
                number_of_lines = -1;
                sscanf(boss->reply_lines[r], "%d", &number_of_lines);
                r++;
                printf("|");
                for (k = 0; k < number_of_lines
                            && r < boss->reply_lines_count; k++) {
                   printf("%s%s", boss->reply_lines[r],
                                  k < number_of_lines - 1 ? "\n" : "");
                   r++;
                }
                printf("|\n");
            }
        }
        if (first_result == 0)
            printf("--------------------------------------------------\n");
    }

    /* Dispose all recorded results */
    ret = transmit_command(boss, "-msg_op clear_sieve -\n");
    if (ret <= 0)
        goto ex;
    ret = await_all_replies(boss);
    if (ret <= 0)
        goto ex;

    ret = 1;
ex:;
    /* Disable result catcher */
    boss->reply_lines_size = 0;
    if (names != NULL)
        dispose_parsed_words(&names_size, &names);
    return(ret);
}


/* ------------------------- Helpers of local helpers ---------------------- */


static int show_parsed(struct boss_state *boss, char *line)
{
    int argc, ret = 0, i;
    char **argv = NULL;

    if (!boss->do_parse)
        return(2);
    ret = parse_line(boss->progname, line, boss->prefix, boss->separators,
                     boss->max_words, &argc, &argv, boss->flag);
    if (ret <= 0 || argc <= 0)
        return(0);
    fprintf(stderr, "-----------------------------------------------------\n");
    fprintf(stderr, "parse_line returns %d, argc = %d\n", ret, argc);
    for (i = 0; i < argc; i++)
         fprintf(stderr, "%2d : %s\n", i, argv[i]);
    fprintf(stderr, "-----------------------------------------------------\n");
    dispose_parsed_words(&argc, &argv); /* release memory */
    return(1);    
}


static int make_reply_lines(struct boss_state *boss)
{
    int i;

    if (boss->reply_lines != NULL)
        return(1);

    boss->reply_lines = calloc(Frontend_xorriso_max_resulT,
                                 sizeof(char *));
    if (boss->reply_lines == 0) {
        fprintf(stderr, "FATAL: Out of memory !\n");
        return(-1);
    }
    boss->reply_lines_count = 0;
    boss->reply_lines_size = 0;
    for (i = 0; i < Frontend_xorriso_max_resulT; i++)
        boss->reply_lines[i] = NULL;
    return(1);
}


static int record_reply_line(struct boss_state *boss, char *line)
{
    if (boss->reply_lines[boss->reply_lines_count] != NULL)
        free(boss->reply_lines[boss->reply_lines_count]);
    boss->reply_lines[boss->reply_lines_count] = strdup(line);
    if (boss->reply_lines[boss->reply_lines_count] == NULL) {
        fprintf(stderr, "FATAL: Out of memory !\n");
        return(-1);
    }
    boss->reply_lines_count++;
    return(1);
}


static int input_interpreter(char *line, struct boss_state *boss)
{
    int argc, ret = 0;
    char **argv = NULL;

    ret = parse_line(boss->progname, line, "", "", 6, &argc, &argv, 0);
    if (ret <= 0 || argc <= 0)
        return(0);
    if (strcmp(argv[0], "@parse") == 0) {
        boss->do_parse = 1;
        boss->prefix[0] = 0;
        if (argc > 1)
            strcpy(boss->prefix, argv[1]);
        boss->separators[0] = 0;
        if (argc > 2)
            strcpy(boss->separators, argv[2]);
        boss->max_words = 0;
        if (argc > 3)
            sscanf(argv[3], "%d", &(boss->max_words));
        boss->max_words = 0;
        if (argc > 4)
            sscanf(argv[4], "%d", &(boss->flag));
        ret = 1;
    } else if(strcmp(argv[0], "@noparse") == 0) {
        boss->do_parse = 0;
        ret = 1;
    } else if(strcmp(argv[0], "@drain_sieve") == 0) {
        ret= drain_sieve(boss);
    } else {
        ret = 0;
    }
    dispose_parsed_words(&argc, &argv); /* release memory */
    return ret;
}


/* -------- Line-to-word parser equivalent to Xorriso_parse_line() -------- */


static int Sfile_sep_make_argv(char *progname, char *line, char *separators,
                             int max_words, int *argc, char ***argv, int flag);


int parse_line(char *progname, char *line,
               char *prefix, char *separators, int max_words,
               int *argc, char ***argv, int flag)
{
    int ret, bsl_mode;
    char *to_parse;
 
    *argc = 0;
    *argv = NULL;
 
    to_parse = line;
    bsl_mode = (flag >> 1) & 15;
    if (prefix[0]) {
        if (strncmp(line, prefix, strlen(prefix)) == 0) {
            to_parse = line + strlen(prefix);
        } else {
            ret = 2; goto ex;
        }
    }
    ret = Sfile_sep_make_argv(progname, to_parse, separators,
                              max_words, argc, argv,
                              (!(flag & 32)) | 4 | (bsl_mode << 5));
    if (ret < 0) {
        fprintf(stderr,
                "%s : Severe lack of resources during command line parsing\n",
                progname);
        goto ex;
    }
    if (ret == 0) {
        fprintf(stderr,
                "%s : Incomplete quotation in %s line: %s\n",
                progname, (flag & 32) ? "command" : "parsed", to_parse);
        goto ex;
    }
ex:;
    if (ret <= 0)
        Sfile_sep_make_argv("", "", "", 0, argc, argv, 2); /* release memory */
    return(ret);
}


int dispose_parsed_words(int *argc, char ***argv)
{
    Sfile_sep_make_argv("", "", "", 0, argc, argv, 2);
    return(1);
}


/* -------------- Some helpers copied from xorriso/sfile.c ----------------- */


static int Sfile_destroy_argv(int *argc, char ***argv, int flag)
{
 int i;

 if(*argc>0 && *argv!=NULL){
   for(i=0;i<*argc;i++){
     if((*argv)[i]!=NULL)
       free((*argv)[i]);
   }
   free((char *) *argv);
 }
 *argc= 0;
 *argv= NULL;
 return(1);
}


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
static int Sfile_bsl_interpreter(char *text, int upto, int *eaten, int flag)
{
 char *rpt, *wpt, num_text[8], wdummy[8];
 unsigned int num= 0;

 *eaten= 0;
 wpt= text;
 for(rpt= text; *rpt != 0 && rpt - text < upto; rpt++) {
   if(flag & 1)
     wpt= wdummy;
   if(*rpt == '\\') {
     rpt++;
     (*eaten)++;
     if(*rpt == 'a') {
       *(wpt++)= 7;
     } else if(*rpt == 'b') {
       *(wpt++)= 8;
     } else if(*rpt == 'e') {
       *(wpt++)= 27;
     } else if(*rpt == 'f') {
       *(wpt++)= 12;
     } else if(*rpt == 'n') {
       *(wpt++)= 10;
     } else if(*rpt == 'r') {
       *(wpt++)= 13;
     } else if(*rpt == 't') {
       *(wpt++)= 9;
     } else if(*rpt == 'v') {
       *(wpt++)= 11;
     } else if(*rpt == '\\') {
       *(wpt++)= '\\';
     } else if(rpt[0] >= '0' && rpt[0] <= '7' &&
               rpt[1] >= '0' && rpt[1] <= '7' &&
               rpt[2] >= '0' && rpt[2] <= '7') {
       num_text[0]= '0';
       num_text[1]= *(rpt + 0);
       num_text[2]= *(rpt + 1);
       num_text[3]= *(rpt + 2);
       num_text[4]= 0;
       sscanf(num_text, "%o", &num);
       if((num > 0 || (flag & 2)) && num <= 255) {
         rpt+= 2;
         (*eaten)+= 2;
         *(wpt++)= num;
       } else
         goto not_a_code;
     } else if(rpt[0] == 'x' &&
               ((rpt[1] >= '0' && rpt[1] <= '9') ||
                (rpt[1] >= 'A' && rpt[1] <= 'F') ||
                (rpt[1] >= 'a' && rpt[1] <= 'f'))
               &&
               ((rpt[2] >= '0' && rpt[2] <= '9') ||
                (rpt[2] >= 'A' && rpt[2] <= 'F') ||
                (rpt[2] >= 'a' && rpt[2] <= 'f'))
               ) {
       num_text[0]= *(rpt + 1);
       num_text[1]= *(rpt + 2);
       num_text[2]= 0;
       sscanf(num_text, "%x", &num);
       if(num > 0 && num <= 255) {
         rpt+= 2;
         (*eaten)+= 2;
         *(wpt++)= num;
       } else
         goto not_a_code;
     } else if(*rpt == 'c') {
       if(rpt[1] > 64 && rpt[1] < 96) {
         *(wpt++)= rpt[1] - 64;
         rpt++;
         (*eaten)++;
       } else
         goto not_a_code;
     } else {
not_a_code:;
       *(wpt++)= '\\';
       rpt--;
       (*eaten)--;
     }
   } else
     *(wpt++)= *rpt;
 }
 *wpt= *rpt;
 return(1);
}


#define SfileadrL 4096

static int Sfile_sep_make_argv(char *progname, char *line, char *separators,
                              int max_words, int *argc, char ***argv, int flag)
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
{
 int i,pass,maxl=0,l,argzaehl=0,bufl,line_start_argc, bsl_mode, ret= 0, eaten;
 char *cpt,*start;
 char *buf= NULL;

 Sfile_destroy_argv(argc,argv,0);
 if(flag&2)
   {ret= 1; goto ex;}

 if(flag & 16)
   flag|= 4;
 bsl_mode= (flag >> 5) & 3;

 buf= calloc(strlen(line) + SfileadrL, 1);
 if(buf == NULL)
   {ret= -1; goto ex;}
 for(pass=0;pass<2;pass++) {
   cpt= line-1;
   if(!(flag&1)){
     argzaehl= line_start_argc= 1;
     if(pass==0)
       maxl= strlen(progname);
     else
       strcpy((*argv)[0],progname);
   } else {
     argzaehl= line_start_argc= 0;
     if(pass==0) maxl= 0;
   }
   while(*(++cpt)!=0){
     if(*separators) {
       if(strchr(separators, *cpt) != NULL)
   continue;
     } else if(isspace(*cpt))
   continue;
     start= cpt;
     buf[0]= 0;
     cpt--;

     if(max_words > 0 && argzaehl >= max_words && *cpt != 0) {
       /* take uninterpreted up to the end */
       cpt+= strlen(cpt) - 1;
     }

     while(*(++cpt)!=0) {
       if(*separators) {
         if(strchr(separators, *cpt) != NULL)
     break;
       } else if(isspace(*cpt))
     break;
       if(*cpt=='"'){
         l= cpt-start; bufl= strlen(buf);
         if(l>0) {
           strncpy(buf + bufl, start, l); buf[bufl + l]= 0;
           if(bsl_mode >= 3) {
             ret= Sfile_bsl_interpreter(buf, l, &eaten, 0);
             if(ret <= 0)
               goto ex;
           }
         }
         l= strlen(buf);
         start= cpt+1;
         while(*(++cpt)!=0) if(*cpt=='"') break;
         if((flag&4) && *cpt==0)
           {ret= 0; goto ex;}
         l= cpt-start; bufl= strlen(buf);
         if(l>0) {
           strncpy(buf + bufl, start, l);
           buf[bufl + l]= 0;
           if(bsl_mode >= 1) {
             ret= Sfile_bsl_interpreter(buf + bufl, l, &eaten, 0);
             if(ret <= 0)
               goto ex;
           }
         }
         start= cpt+1;
       }else if(*cpt=='\''){
         l= cpt-start; bufl= strlen(buf);
         if(l>0) {
           strncpy(buf + bufl, start, l); buf[bufl + l]= 0;
           if(bsl_mode >= 3) {
             ret= Sfile_bsl_interpreter(buf, l, &eaten, 0);
             if(ret <= 0)
               goto ex;
           }
         }
         l= strlen(buf);
         start= cpt+1;
         while(*(++cpt)!=0) if(*cpt=='\'') break;
         if((flag&4) && *cpt==0)
           {ret= 0; goto ex;}
         l= cpt-start; bufl= strlen(buf);
         if(l>0) {
           strncat(buf,start,l);buf[bufl+l]= 0;
           if(bsl_mode >= 2) {
             ret= Sfile_bsl_interpreter(buf + bufl, l, &eaten, 0);
             if(ret <= 0)
               goto ex;
           }
         }
         start= cpt+1;
       }
     if(*cpt==0) break;
     }
     l= cpt-start;
     bufl= strlen(buf);
     if(l>0) {
       strncpy(buf + bufl, start, l); buf[bufl + l]= 0;
       if(bsl_mode >= 3) {
         ret= Sfile_bsl_interpreter(buf, l, &eaten, 0);
         if(ret <= 0)
           goto ex;
       }
     }
     l= strlen(buf);
     if(pass==0){
       if(argzaehl==line_start_argc && (flag&8))
         if(buf[0]!='-' && buf[0]!=0 && buf[0]!='#')
           l++;
       if(l>maxl) maxl= l;
     }else{
       strcpy((*argv)[argzaehl],buf);
       if(argzaehl==line_start_argc && (flag&8))
         if(buf[0]!='-' && buf[0]!=0 && buf[0]!='#')
           sprintf((*argv)[argzaehl],"-%s", buf);
     }
     argzaehl++;
   if(*cpt==0) break;
   }
   if(pass==0){
     if(flag & 16)
       {ret= 1; goto ex;}
     *argc= argzaehl;
     if(argzaehl>0 || (flag & 128)) {
       *argv= (char **) calloc((argzaehl + !!(flag & 128)), sizeof(char *));
       if(*argv==NULL)
         {ret= -1; goto ex;}
     }
     for(i=0;i<*argc;i++) {
       (*argv)[i]= (char *) calloc(maxl + 1, 1);
       if((*argv)[i]==NULL)
         {ret= -1; goto ex;}
     }
     if(flag & 128)
       (*argv)[*argc]= NULL;
   }
 }
 ret= 1;
ex:
 if(buf != NULL)
   free(buf);
 return(ret);
}

