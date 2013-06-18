
/* xorriso - libisoburn higher level API which creates, loads, manipulates
             and burns ISO 9660 filesystem images.

   Copyright 2007-2013 Thomas Schmitt, <scdbackup@gmx.net>

   Provided under GPL version 2 or later.

   This file contains the public API of xorriso which covers all of its
   operations.

   An example of its usage is xorriso_main.c which checks version compatibility,
   creates a xorriso object, initializes the libraries, and runs the command
   interpreters of the API to constitute the command line oriented batch and
   dialog tool xorriso.

   Alternatively to command interpreters it is possible to run all options of
   xorriso directly via the calls of the "Command API". 
   The "Problem Status and Message API" shall then be used to obtain the
   text output of the options.

   Mandatory calls are:
     Xorriso_new(), Xorriso_startup_libraries(), Xorriso_destroy()

   This architecture is fully public since version 0.5.8. From then on, new
   features get marked by
     @since major.minor.micro
   If this mark is missing, then the feature was present before release 0.5.8.

   Please note that struct XorrisO and its API calls are _not_ thread-safe in
   general. It is not permissible to run two API calls on the same
   XorrisO object concurrently.
   The only exception is
     Xorriso_fetch_outlists()
   in order to learn about the ongoing text output of other API calls.


   There is a lower level of API which consists of libisofs.h, libburn.h and
   libisoburn.h. One should not mix those calls with the ones of xorriso.h .
*/

/* Important: If you add a public API function then add its name to file
                  libisoburn/libisoburn.ver
*/


#ifndef Xorriso_includeD
#define Xorriso_includeD yes

/** Opaque handle of the xorriso runtime context */
struct XorrisO; 



/* Create GNU xorriso
   under GPLv3+ derived from above GPLv2+.
*/
#define Xorriso_GNU_xorrisO yes


/* --------------------- Fundamental Management ------------------- */


/** These three release version numbers tell the revision of this header file
    and of the API which it describes. They shall be memorized by applications
    at build time.
    @since 0.5.8
*/
#define Xorriso_header_version_majoR  1
#define Xorriso_header_version_minoR  3
#define Xorriso_header_version_micrO  0


/** If needed: Something like ".pl01" to indicate a bug fix. Normally empty.
    @since 0.5.8
*/
#define Xorriso_program_patch_leveL ""


/** Obtain the three release version numbers of the library. These are the
    numbers encountered by the application when linking with libisoburn,
    i.e. possibly not before run time.
    Better do not base the fundamental compatibility decision of an application
    on these numbers. For a reliable check use Xorriso__is_compatible().
    @since 0.5.8
    @param major The maturity version (0 for now, as we are still learning)
    @param minor The development goal version.
    @param micro The development step version. This has an additional meaning:

                 Pare numbers indicate a version with frozen API. I.e. you can
                 rely on the same set of features to be present in all
                 published releases with that major.minor.micro combination.
                 Features of a pare release will stay available and ABI
                 compatible as long as the SONAME of libisoburn stays "1".
                 Currently there are no plans to ever change the SONAME.
                  
                 Odd numbers indicate that API upgrades are in progress.
                 I.e. new features might be already present or they might
                 be still missing. Newly introduced features may be changed
                 incompatibly or even be revoked before release of a pare
                 version.
                 So micro revisions {1,3,5,7,9} should never be used for
                 dynamic linking unless the proper library match can be
                 guaranteed by external circumstances.

    @return 1 success, <=0 might in future become an error indication
*/
void Xorriso__version(int *major, int *minor, int *micro);


/** Check whether all features of header file xorriso.h from the given
    major.minor.micro revision triple can be delivered by the library version
    which is performing this call.
        if (! Xorriso__is_compatible(Xorriso_header_version_majoR,
                                     Xorriso_header_version_minoR,
                                     Xorriso_header_version_micrO, 0))
           ...refuse to start the program with this dynamic library version...
    @since 0.5.8
    @param major obtained at build time
    @param minor obtained at build time
    @param micro obtained at build time
    @param flag Bitfield for control purposes. Unused yet. Submit 0.
    @return 1= library can work for caller
            0= library is not usable in some aspects. Caller must restrict
               itself to an earlier API version or must not use this library
               at all.
*/
int Xorriso__is_compatible(int major, int minor, int micro, int flag);


/* Get the patch level text (e.g. "" or ".pl01") of the program code.
   @param flag     unused yet, submit 0
   @return         readonly character string
*/
char *Xorriso__get_patch_level_text(int flag);


/* Choose how Xorriso_startup_libraries() and the XorrisO object shall
   prepare for eventual signals.
   @param behavior Default is behavior 1.
                   0= no own signal handling. The main application has to do
                      that. Do not start burn runs without any handling !
                   1= use libburn signal handler. Most time with action
                      0. During writing, formatting, blanking: 0x30.
                      Only usable with a single xorriso object.
                   2= Enable system default reaction on all signals
                      @since 1.0.9
                   3= Try to ignore nearly all signals
                      @since 1.0.9
   @param flag     unused yet, submit 0
   @return         <= 0 is error, >0 is success
*/
int Xorriso__preset_signal_behavior(int behavior, int flag);


/* Mandatory call:
   Create a new xorriso object and tell it the program name to be used
   with messages and for decision of special behavior.
   @param xorriso  returns the newly created XorrisO object 
   @param progname typically argv[0] of main(). Some leafnames of the progname
                   path have special meaning and trigger special behavior:
                    "osirrox"    allows image-to-disk copying: -osirrox "on" 
                    "xorrisofs"  activates -as "mkisofs" emulation from start
                    "genisofs"   alias of "xorrisofs"
                    "mkisofs"    alias of "xorrisofs"
                    "genisoimage"  alias of "xorrisofs"
                    "xorrecord"  activates -as "cdrecord" emulation from start
                    "cdrecord"   alias of "xorrecord"
                    "wodim"      alias of "xorrecord"
                    "cdrskin"    alias of "xorrecord"
   @param flag     unused yet, submit 0
   @return         >0 success , <=0 failure, no object created
*/
int Xorriso_new(struct XorrisO ** xorriso, char *progname, int flag);


/* Note: Between Xorriso_new() and the next call Xorriso_startup_libraries()
         there may be called the special command interpreter
         Xorriso_prescan_args().
         The other command interpreters may be used only after
         Xorriso_startup_libraries(). The same restriction applies to the
         calls of the Command API further below.
*/


/* Mandatory call:
   It has to be made before calling any function listed below this point.
   Only exception is the special command interpreter Xorriso_prescan_args().

   Make global library initializations.
   This must be done with the first xorriso object that gets created and
   with the first xorriso object that gets created after Xorriso_destroy(,1).
   @param xorriso The context object.
   @param flag    unused yet, submit 0
   @return        <=0 error , >0 success
*/ 
int Xorriso_startup_libraries(struct XorrisO *xorriso, int flag);


/* Note: After library startup, you may run Command Interpreters or call
         functions from the Command API.

         Wenn all desired activities are done, you may check whether there are
         uncommited changes pending, compute an exit value, destroy the XorrisO
         object, and exit your program.
*/


/* Inquire whether option -commit would make sense.
   @param xorriso The context object to inquire.
   @param flag    @since 0.6.6
                  bit0= do not return 1 if -as mkisofs -print-size was
                        performed on the current image.
   @return        0= -commit would have nothing to do
                  1= a new image session would emerge at -commit
*/
int Xorriso_change_is_pending(struct XorrisO *xorriso, int flag);


/* Compute the exit value from the recorded maximum event severity.
   @param xorriso The context object to inquire.
   @param flag    unused yet, submit 0
   @return        The computed exit value
*/
int Xorriso_make_return_value(struct XorrisO *xorriso, int flag);


/* Mandatory call:
   Destroy xorriso object when it is no longer needed.
   @param xorriso  The context object to destroy. *xorriso will become NULL.
   @param flag     bit0= Perform global library shutdown.
                         Use only with last xorriso object to be destroyed.
   @return         <=0 error, >0 success
*/
int Xorriso_destroy(struct XorrisO **xorriso, int flag);


/* --------------------- Command Interpreters ------------------- */


/* This special interpreter may be called between Xorriso_new() and
   Xorriso_startup_libraries(). It interprets certain commands which shall
   get into effect before the libraries get initialized:
     -abort_on , -report_about , -return_with , -list_delimiter ,
     -scsi_log , -signal_handling
   This is the only occasion where command -x has an effect:
     -x
   Some commands get executed only if they are the only command in argv:
     -prog_help , -help
   The following is recognized only if it is the first of all arguments:
     -no_rc
   Some get examined for the need to redirect stdout messages:
     -dev , -outdev , -indev , -as 
   Commands -list_delimiter and -add_plainly get into effect during this
   call. But their setting at begin of the call gets restored before the
   call returns.
   @param xorriso The context object in which to perform the commands.
   @param argc    Number of arguments.
   @param argv    The arguments. argv[0] contains the program name.
                  argv[1] to argv[argc-1] contain commands and parameters.
   @param idx     Argument cursor. When this function is called, *idx must
                  be at least 1, argv[*idx] must be a command.
                  *idx will iterate over commands and parameters until this
                  function aborts or until argc is reached.
   @param flag    bit0= do not interpret argv[1]
                  bit1= produce FAILURE events on unknown commands
                        @since 1.1.0
   @return        <0 error
                   0 end program
                   1 ok, go on 
*/
int Xorriso_prescan_args(struct XorrisO *xorriso, int argc, char **argv,
                         int flag);


/* Read and interpret commands from eventual startup files as listed in
   man xorriso.
   @param xorriso The context object in which to perform the commands.
   @param flag    unused yet, submit 0
   @return <=0 = error
             1 = success
             3 = end program run (e.g. because command -end was encountered)
*/
int Xorriso_read_rc(struct XorrisO *xorriso, int flag);


/* Check whether program arguments shall be backslash decoded. If so, then
   replace *argv by a new argument vector. The old one will not be freed
   by this call. If it is dynamic memory then you need to keep a copy of
   the pointer and free it yourself after this call.
   @param xorriso The context object
   @param argc    Number of arguments.
   @param argv    The arguments. (*argv)[0] contains the program name.
                  (*argv)[1] to (*argv)[argc-1] contain commands and parameters
                  If argv after the call differs from argv before the call,
                  then one should dispose it later by:
                    Xorriso__dispose_words(argc, argv);
   @param flag    unused yet, submit 0
   @return        <= 0 error , > 0 success
*/
int Xorriso_program_arg_bsl(struct XorrisO *xorriso, int argc, char ***argv,
                            int flag);


/* Interpret argv as xorriso command options and their parameters.
   (An alternative is to call functions of the options API directly and to
    perform own error status evaluation. See below: Command API.) 
   After the first command and its parameters there may be more commands and
   parameters. All parameters must be given in the same call as their command.
   @since 1.2.2:
   Commands may get arranged in a sequence that is most likely to make sense.
   E.g. image loading settings before drive aquiration, then commands for
   adding files, then settings for writing, then writing.
   This feature may be enabled by command "-x" in Xorriso_prescan_args()
   or by parameter flag of this call.
   @param xorriso The context object in which to perform the commands.
   @param argc    Number of arguments.
   @param argv    The arguments. argv[0] contains the program name.
                  argv[1] to argv[argc-1] contain commands and parameters.
   @param idx     Argument cursor. When this function is called, *idx must
                  be at least 1, argv[*idx] must be a command.
                  *idx will iterate over commands and parameters until this
                  function aborts, or until argc is reached, or only once if
                  flag bit2 is set.
   @param flag    bit0= reserved. Indicates recursion. Submit 0. 
                  bit1= Indicates that these are the main() program start
                        arguments. This enables their use with emulations
                        which where set with Xorriso_new(), or argument
                        arranging.
                  bit2= Only execute the one command argv[*idx] and advance
                        *idx to the next command if sucessful. Then return.
                        This prevents any argument arranging.
                        @since 1.2.2
                  bit3= With bit1 and not bit2:
                        Enable argument arranging as with
                        Xorriso_prescan_args() and command "-x".
                        @since 1.2.2
                  bit4= With bit1:
                        Surely disable argument arranging.
                        @since 1.2.2
   @return <=0 = error
             1 = success
             2 = problem event ignored
             3 = end program run (e.g. because command -end was encountered)
*/
int Xorriso_interpreter(struct XorrisO *xorriso,
                        int argc, char **argv, int *idx, int flag);


/* Parse a command line into words and use them as argv for a call of
   Xorriso_interpreter(). Put out some info lines about the outcome.
   @param xorriso The context object in which to perform the commands.
   @param line    A text of one or more words according to man xorriso
                  paragraph "Command processing" up to and including
                  "Backslash Interpretation".
   @param flag    bit0 to bit15 are forwarded to Xorriso_interpreter()
                  bit16= no pageing of info lines
                  bit17= print === bar even if xorriso->found<0
   @return        see return of Xorriso_interpreter()
*/
int Xorriso_execute_option(struct XorrisO *xorriso, char *line, int flag);


/* Parse a text line into words. This parsing obeys the same rules as
   command line parsing but allows to skip a prefix, to use a user provided
   set of separator characters, and to restrict the number of parsed words.

   If parameter xorriso is NULL, then this call is safe for usage by
   a concurrent thread while a xorriso API call is being executed.

   @since 1.2.6
   @param xorriso     The context object which provides settings for parsing
                      and output channels for error messages.
                      May be NULL in order to allow concurrent execution e.g.
                      by a callback function of Xorriso_start_msg_watcher().
                      If xorriso is NULL then:
                        flag bit1-bit4 are in effect even if bit0 is not set.
                        flag bit5 and bit6 may not be set.
   @param line        A text of one or more words according to man xorriso
                      paragraph "Command processing" up to and including
                      "Backslash Interpretation".
   @param prefix      If not empty then the line will only be parsed if it
                      begins by the prefix text. Parsing will then begin after
                      the end of the prefix.
                      If the prefix does not match, then 0 will be returned
                      in *argc, argv will be NULL, and the return value will
                      be 2.
   @param separators  If not empty this overrides the default list of word
                      separating characters. Default set is the one of
                      isspace(3).
   @param max_words   If not 0: Maximum number of words to parse. If there
                      remains line text after the last parsed word and its
                      following separators, then this remainder is copied
                      unparsed into a final result word. In this case *argc
                      will be larger than max_words by one. Note that trailing
                      separators are considered to be followed by an empty
                      word.
   @param argc        Will return the number of allocated and filled word
                      strings.
   @param argv        Will return the array of word strings.
                      Do not forget to dispose the allocated memory by a
                      call to Xorriso__dispose_words().
   @param flag        Bitfield for control purposes
                      bit0=   Override setting of -backslash_codes.
                      bit1-4= With bit0: backslash behavior
                              0= off
                              1= in_double_quotes
                              2= in_quotes
                              3= with_quoted_input resp. on
                      bit5=   Prepend the program name as (*argv)[0], so that
                              *argv is suitable for Xorriso_interpreter()
                              and other calls which expect this.
                              Not allowed if xorriso is NULL.
                      bit6=   Issue failure message in case of return 0
                              Not allowed if xorriso is NULL.
   @return            <=0 means error and invalidity of *argv:
                              0 = Input format error. E.g. bad quotation mark.
                             -1 = Lack of resources. E.g. memory.
                             -2 = Improper combination of call parameters.
                      >0 means success but not necessarily a valid result:
                              1 = Result in argc and argv is valid (but may
                                  be empty by argc == 0, argv == NULL).
                              2 = Line did not match prefix. Result is invalid
                                  and empty.
*/
int Xorriso_parse_line(struct XorrisO *xorriso, char *line,
                       char *prefix, char *separators, int max_words,
                       int *argc, char ***argv, int flag);


/* Dispose a list of strings as allocated by Xorriso_parse_line() or
   Xorriso_program_arg_bsl(), or Xorriso_sieve_get_result().
   @since 1.2.6
   @param argc        A pointer to the number of allocated and filled word
                      strings. *argc will be set to 0 by this call.
   @param argv        A pointer to the array of word strings.
                      *argv will be set to NULL by this call.
*/
void Xorriso__dispose_words(int *argc, char ***argv);


/* Enter xorriso command line dialog mode, using libreadline if configured
   at build time and not disabled at run time.
   This call returns immediately if not option -dialog "on" was performed
   before.
   @param xorriso The context object in which to perform the commands.
   @param flag    unused yet, submit 0
   @return        <=0 error, 1= dialog mode ended normally ,
                  3= dialog mode ended normally,interpreter asks to end program
*/
int Xorriso_dialog(struct XorrisO *xorriso, int flag);


/* --------------------- Problem Status and Message API ------------------- */


/** Submit a problem message to the xorriso problem reporting and handling
    system. This will eventually increase problem status rank, which may
    at certain stages in the program be pardoned and reset to 0.
    The pardon is governed by Xorriso_option_abort_on() and by the anger
    of the affected program part. If no pardon has been given, then the problem
    status reaches the caller of option functions.
    Problem status should be inquired by Xorriso_eval_problem_status() and be
    reset before next option execution by Xorriso_set_problem_status().
    The problem status itself does not cause the failure of option functions.
    But in case of failures for other reasons, a remnant overly severe problem
    status can cause overly harsh program reactions.
    @param xorriso    The environment handle
    @param error_code The unique error code of your message.
                      Submit 0 if you do not have reserved error codes within
                      the libburnia project.
    @param msg_text   Not more than 8196 characters of message text.
                      A final newline character gets appended automatically.
    @param os_errno   Eventual errno related to the message. Submit 0 if
                      the message is not related to a operating system error.
    @param severity   One of "ABORT", "FATAL", "FAILURE", "MISHAP", "SORRY",
                      "WARNING", "HINT", "NOTE", "UPDATE", "DEBUG".
                      Defaults to "FATAL".
    @param flag       Bitfield for control purposes
                        bit0= use pager (as with result)
                        bit1= permission to suppress output
    @return           1 if message was delivered, <=0 if failure
*/
int Xorriso_msgs_submit(struct XorrisO *xorriso,
                        int error_code, char msg_text[], int os_errno,
                        char severity[], int flag);

/** Alternative call interface of Xorriso_msgs_submit with void* instead
    of struct XorrisO*
*/
int Xorriso_msgs_submit_void(void *xorriso,
                        int error_code, char msg_text[], int os_errno,
                        char severity[], int flag);


/** Evaluate an advise whether to abort or whether to go on with option
    processing. This should be called after any option function was processed.
    It updates the problem status by processing the library message queues
    and then it uses this status and the submitted return value of the
    option function to evaluate the situation.
    @param xorriso    The environment handle
    @param ret        The return value of the previously called option function
    @param flag       bit0= do not issue own event messages
                      bit1= take xorriso->request_to_abort as reason for abort
    @return           Gives the advice:
                        2= pardon was given, go on
                        1= no problem, go on
                        0= function failed but xorriso would not abort, go on
                       <0= do abort
                           -1 = due to xorriso->problem_status
                                or due to ret<0
                           -2 = due to xorriso->request_to_abort
*/
int Xorriso_eval_problem_status(struct XorrisO *xorriso, int ret, int flag);


/** Set the current problem status of the xorriso handle.
    @param xorriso    The environment handle
    @param severity   A severity text. Empty text resets to "No Problem". 
    @param flag       Unused yet. Submit 0.
    @return           <=0 failure (e.g. wrong severity text), 1 success.
*/
int Xorriso_set_problem_status(struct XorrisO *xorriso, char *severity,
                               int flag);


/* The next three functions are part of Xorriso_eval_problem_status().
   You may use them to build an own advisor function.
*/

/** Compare two severity texts for their severeness.
    Unknown severity texts get defaulted to "FATAL".
    @since 1.2.6
    @param sev1  First severity text to compare
    @param sev2  Second severity text to compare
    @return      -1 sev1 is less severe than sev2
                  0 sev1 is equally severe to sev2
                  1 sev1 is more severe than sev2
*/
int Xorriso__severity_cmp(char *sev1, char *sev2);


/** Return a blank separated list of severity names. Sorted from low
    to high severity.
    @since 1.2.6
    @param flag Bitfield for control purposes (unused yet, submit 0)
    @return  A constant string with the severity names
*/
char *Xorriso__severity_list(int flag);



/** Obtain the current problem status of the xorriso handle.
    @param xorriso    The environment handle
    @param severity   The severity text matching the current problem status
    @param flag       Unused yet. Submit 0.
    @return           The severity rank number. 0= no problem occured.
*/
int Xorriso_get_problem_status(struct XorrisO *xorriso, char severity[80],
                               int flag);


/** Forward any pending messages from the library message queues to the
    xorriso message system which puts out on info channel. This registers
    the severity of the library events like the severity of a message submitted
    via Xorriso_msgs_submit().
    xorriso sets the message queues of the libraries to queuing "ALL".
    Many inner functions of xorriso call Xorriso_process_msg_queues() on their
    own because they expect library output pending. Nevertheless, a loop of
    xorriso option calls should either call Xorriso_eval_problem_status() or
    Xorriso_process_msg_queues() with each cycle.
    @param xorriso    The environment handle
    @param flag       Unused yet. Submit 0.
    @return           1 on success, <=0 if failure
*/
int Xorriso_process_msg_queues(struct XorrisO *xorriso, int flag);


/** Write a message for option -errfile_log. 
    @param xorriso    The environment handle
    @param error_code The unique error code of your message.
                      Submit 0 if you do not have reserved error codes within
                      the libburnia project.
    @param msg_text   Not more than 8196 characters of message text.
    @param os_errno   Eventual errno related to the message. Submit 0 if
                      the message is not related to a operating system error.
    @param flag       bit0-7= meaning of msg_text
                      ( 0= ERRFILE path , for internal use mainly )
                        1= mark line text (only to be put out if enabled)
    @return <=0 error , >0 success
*/
int Xorriso_process_errfile(struct XorrisO *xorriso,
                            int error_code, char msg_text[], int os_errno,
                            int flag);


/*
                           Message output evaluation

   xorriso is basically a dialog software which reacts on commands by
   side effects and by messages. The side effects manipulate the state of
   the ISO image model and of drives. This state can be inquired by commands
   which emit messages.

   There are several approaches how a program that uses xorriso via this API
   can receive and use the message output of xorriso.

   - The message sieve may be programmed to pick certain information snippets
     out of the visible message stream. This covers all messages on the
     result channel and those info channel messages which get not suppressed
     by command -report_about. All important info messages have severity NOTE
     or higher.
     Much of the message interpretation is supposed to happen by the sieve
     filter rules which describe the interesting message lines and the
     positions of the interesting message parts.
     The call Xorriso_sieve_big() installs a sieve that looks out for most
     model state messages which xorriso can emit. After a few commands
     the user will ask the sieve for certain text pieces that might have been
     caught.

   - The outlist stack may be used to catch messages in linked lists rather
     than putting them out on the message channels.
     All interpretation of the messages has to be done by the user of the
     xorriso API. Function Xorriso_parse_line() is intended to help with
     splitting up messages into words.
     The outlist stack is handy for catching the results of information
     commands with large uniform output or no well recognizable message
     prefix. Like -lsl, -getfacl, -status, -find ... -exec get_md5.
     One should push the stack before the command, pull it afterwards, examine
     the text list by Xorriso_lst_get_*(), and finally dispose the list.

   - The message watcher is a separate program thread which uses the outlist
     stack to catch the messages and to call user provided handler functions.
     These functions can use Xorriso_parse_line() too, if they submit the
     xorriso parameter as NULL. They may not use the struct XorrisO object
     in any way.
     Synchronization between watcher and emitters of commands can be achieved
     by Xorriso_peek_outlists().
     The main motivation for the message watcher is to inspect and display
     messages of long lasting xorriso commands while they are still executing.
     E.g. of -commit, -blank, -format.
     One would normally start it before such a command and stop it afterwards.
     But of course, the watcher can stay activated all the time and process
     all message output via its handler calls.

   The message sieve does not interfere with outlists and message watcher.
   The message watcher will only see messages which are not caught by outlists
   which were enabled after the watcher thread was started.
     
*/

/* The programmable message sieve picks words out of the program messages
   of xorriso.
   The sieve is a collection of filter rules. Each one is defined by a call of
   Xorriso_sieve_add_filter(). The sieve watches the given output channels for
   messages which begin by the given text prefixes of the filters.
   Matching lines get split into words by Xorriso_parse_line() using
   the given separators. The words described by the filter's word index array
   get recorded by the filter and can be inquired by Xorriso_sieve_get_result()
   after one or more xorriso commands have been performed.
   The recorded results may be disposed by Xorriso_sieve_clear_results without
   giving up the sieve.
   The whole sieve may be disposed by Xorriso_sieve_dispose().
   Default at library start is an inactive sieve without any filter rules.
*/

/** Add a filter rule to the message sieve.
    Start watching output messages, if this is not already enabled.
    @since 1.2.6
    @param xorriso    The environment handle
    @param name       The filter name by which its recorded results shall
                      be inquired via Xorriso_sieve_get_result()
    @param channels   Which of the output channels the filter shall watch
                      bit0= result channel
                      bit1= info channel
                      bit2= mark channel
    @param prefix     The line start to watch for. Will also be handed over
                      to Xorriso_parse_line(). Empty text matches all lines. 
                      If the prefix begins by '?' characters, then these
                      match any character at the beginning of a message.
                      The prefix of the filter rule will then be adapted
                      to really match the line, before it gets handed over
                      to Xorriso_parse_line().
    @param separators List of separator characters for Xorriso_parse_line()
    @param num_words  Number of word indice in word_idx
    @param word_idx   Array with the argv indice to be picked from the
                      the result of Xorriso_parse_line(). Must at least
                      contain num_words elements.
    @param max_results If not 0, then the maximum number of line results that
                      shall be recorded by the filter. When this number is
                      exceeded, then results of older lines get discarded
                      when new results get recorded.
    @param flag       Bitfield for control purposes
                      bit0= Last result word shall contain the remainder of
                            the message line
    @return           <=0 error , >0 success
*/
int Xorriso_sieve_add_filter(struct XorrisO *xorriso, char *name,
                             int channels, char *prefix, char *separators,
                             int num_words, int *word_idx, int max_results,
                             int flag);


/** Inquire recorded results from a particular filter rule.
    @param xorriso    The environment handle
    @param name       The filter name as given by Xorriso_sieve_add_filter()
    @param argc       Will return the number of allocated and filled word
                      strings.
    @param argv       Will return the array of word strings.
                      Do not forget to dispose the allocated memory by a
                      call to Xorriso__dispose_words().
    @param available  Will return the number of results which are still
                      available for further calls of Xorriso_sieve_get_result()
                      with the given name.
    @param flag       Bitfield for control purposes:
                      bit0= Reset reading to first matching result.
                      bit1= Only inquire number of available results.
                            Do not allocate memory.
                      bit2= If *argv is not NULL, then free it before attaching
                            new memory.
                      bit3= Do not read recorded data but rather list all
                            filter names.
    @return           <0 error: -1 = memory shortage
                                -2 = no filter rule found
                       0 No more data available for the given name
                         With bit3: No filter rules installed.
                      >0 argc and argv are valid
*/
int Xorriso_sieve_get_result(struct XorrisO *xorriso, char *name,
                             int *argc, char ***argv, int *available,
                             int flag);


/** Dispose all recorded results. Keep filter rules. Continue watching
    and recording.
    @since 1.2.6
    @param xorriso    The environment handle
    @param flag       Unused yet. Submit 0.
    @return           <=0 error , >0 success
*/
int Xorriso_sieve_clear_results(struct XorrisO *xorriso, int flag);


/** Dispose all filter rules. End watching and recording.
    This is the default state at library startup.
    @since 1.2.6
    @param xorriso    The environment handle
    @param flag       Unused yet. Submit 0.
    @return           <=0 error , >0 success
*/
int Xorriso_sieve_dispose(struct XorrisO *xorriso, int flag);


/** Install a large sieve with filters for about any interesting message
    of xorriso. The filter rule names are mostly the same as the prefixes they
    search for. If you do not find the message prefix of your desire, then
    you may add a filter rule by Xorriso_sieve_add_filter().
    If you do not want all these filter any more, call Xorriso_sieve_dispose().

    You should obtain your recorded data often and then call
    Xorriso_sieve_clear_results(). It is nevertheless ok to perform several
    different xorriso information commands and to then obtain results from the
    sieve.

    The installed filters in particular:
    Name             Recorded values, returned by Xorriso_sieve_get_result()
    ------------------------------------------------------------------------
    "-changes_pending" up to 1 result from -changes_pending show_status
                     argv[0]= "yes" or "no"
    "?  -dev"        up to 10 results from -devices or -device_links
                     (records drives with single digit index number)
                     argv[0]= drive address
                     argv[1]= permissions
                     argv[2]= drive vendor
                     argv[3]= product id
    "??  -dev"       up to 90 results from -devices or -device_links
                     (records drives with double digit index number)
                     argv[0]= drive address
                     argv[1]= permissions
                     argv[2]= drive vendor
                     argv[3]= product id
    "Abstract File:" up to 1 result from -pvd_info
                     argv[0]= file name
                     (Note: prefix is "Abstract File: ")
    "After commit :" up to 1 result from -tell_media_space
                     argv[0]= number of blocks with "s" appended
    "App Id       :" up to 1 result from -pvd_info
                     argv[0]= id
                     (Note: prefix is "App Id       : ")
    "Biblio File  :" up to 1 result from -pvd_info
                     argv[0]= file name
                     (Note: prefix is "Biblio File  : ")
    "Build timestamp   :" up to 1 result from -version
                     argv[0]= timestamp
                     (Note: prefix is "Build timestamp   :  ")
    "CopyrightFile:" up to 1 result from -pvd_info
                     argv[0]= file name
                     (Note: prefix is "CopyrightFile: ")
    "Creation Time:" up to 1 result from -pvd_info
                     argv[0]= YYYYMMDDhhmmsscc
                     (Note: prefix is "Creation Time: ")
    "DVD obs 64 kB:" up to 1 result from -list_extras
                     argv[0]= "yes" or "no"
    "Drive current:" up to 2 results from -dev, -indev, -toc, others
                     argv[0]= command ("-dev", "-outdev", "-indev")
                     argv[1]= drive address
    "Drive type   :" up to 2 results from -toc
                     argv[0]= vendor
                     argv[1]= product
                     argv[2]= revision
    "Eff. Time    :" up to 1 result from -pvd_info
                     argv[0]= YYYYMMDDhhmmsscc
                     (Note: prefix is "EffectiveTime: ")
    "Expir. Time  :" up to 1 result from -pvd_info
                     argv[0]= YYYYMMDDhhmmsscc
                     (Note: prefix is "Expir. Time  : ")
    "Ext. filters :" up to 1 result from -list_extras
                     argv[0]= "yes" or "no" , possibly more info
                     (Note: prefix is "Ext. filters : ")
    "File damaged :" up to 10000 results from -find ... -exec report_damage
                     argv[0]= damage start byte in file
                     argv[1]= damage range size in file
                     argv[2]= file size
                     argv[3]= path in ISO image
    "File data lba:" up to 10000 results from -find ... -exec report_lba
                     argv[0]= extent number (all extents of same path together
                              are the content of one file)
                     argv[1]= start block number of extent
                     argv[2]= number of blocks of extent
                     argv[3]= overall file content size in all extents
                     argv[4]= path in ISO image
    "Format idx   :" up to 100 results from -list_formats
                     argv[0]= index
                     argv[1]= MMC code
                     argv[2]= number of blocks with "s" appended
                     argv[3]= roughly the size in MiB
                     (Note: prefix is "Format idx ")
    "Format status:" up to 1 result from -list_formats
                     argv[0]= status
                     argv[1]= capacity
    "ISO session  :" up to 10000 results from -toc
                     argv[0]= Idx
                     argv[1]= sbsector
                     argv[2]= Size
                     argv[3]= Volume Id
    "Image size   :" up to 1 result from -print_size
                     argv[0]= number of blocks with "s" appended
    "Jigdo files  :" up to 1 result from -list_extras
                     argv[0]= "yes" or "no"
    "Local ACL    :" up to 1 result from -list_extras
                     argv[0]= "yes" or "no"
    "Local xattr  :" up to 1 result from -list_extras
                     argv[0]= "yes" or "no"
    "MD5 MISMATCH:"  up to 10000 results from -check_md5*
                     argv[0]= path of mismatching file
    "MD5 tag range:" up to 10000 results from -check_media
                     argv[0]= lba
                     argv[1]= size in blocks
                     argv[2]= result text (starting with "+", "-", or "0")
    "Media blocks :" up to 2 results from -toc
                     argv[0]= readable
                     argv[1]= writable
                     argv[2]= overall
    "Media current:" up to 2 results from -dev, -indev, -toc, others
                     argv[0]= media type / MMC profile name
                     (Note: prefix is "Media current: " which eats extra blank)
    "Media nwa    :" up to 1 result from -toc
                     argv[0]= next writable address
    "Media product:" up to 2 results from -toc
                     argv[0]= product id
                     argv[1]= manufacturer
    "Media region :" up to 10000 results from -check_media
                     argv[0]= lba
                     argv[1]= size in blocks
                     argv[2]= quality text (starting with "+", "-", or "0")
    "Media space  :" up to 1 result from -tell_media_space
                     argv[0]= number of blocks with "s" appended
    "Media status :" up to 2 results from -dev, -indev, -toc, others
                     argv[0]= status description
                     (Note: prefix is "Media status : ")
    "Media summary:" up to 2 results from -dev, -indev, -toc, others 
                     argv[0]= sessions
                     argv[1]= data blocks (full count)
                     argv[2]= data (with unit letter k,m,g)
                     argv[3]= free (with unit letter k,m,g)
    "Modif. Time  :" up to 1 result from -pvd_info
                     argv[0]= YYYYMMDDhhmmsscc
                     (Note: prefix is "Modif. Time  : ")
    "PVD address  :" up to 1 result from -pvd_info
                     argv[0]= block address with "s" appended
    "Preparer Id  :" up to 1 result from -pvd_info
                     argv[0]= id
                     (Note: prefix is "Preparer Id  : ")
    "Profile      :" up to 256 results from -list_profiles
                     argv[0]= MMC code
                     argv[1]= profile name in round brackets
                              possibly appended: " (current)"
    "Publisher Id :" up to 1 result from -pvd_info
                     argv[0]= id
                     (Note: prefix is "Publisher Id : ")
    "Readline     :" up to 1 result from -list_extras
                     argv[0]= "yes" or "no"
    "Size lower   :" up to 1 result from -findx ... -exec estimate_size
                     argv[0]= size with appended "s"
    "Size upper   :" up to 1 result from -findx ... -exec estimate_size
                     argv[0]= size with appended "s"
    "System Id    :" up to 1 result from -pvd_info
                     argv[0]= id
                     (Note: prefix is "System Id    : ")
    "Version timestamp :" up to 1 result from -version
                     argv[0]= timestamp
    "Volume Id    :" up to 1 result from -pvd_info
                     argv[0]= id
                     (Note: Not output from -dev or -toc but from -pvd_info)
    "Volume Set Id:" up to 1 result from -pvd_info
                     argv[0]= id
                     (Note: prefix is "Volume Set Id: ")
    "Volume id    :" up to 2 results from -dev, -indev, -toc, others
                     argv[0]= volume id
                     (Note: Not output from -pvd_info but from -dev or -toc)
    "Write speed  :" up to 100 results from -list_speeds
                     argv[0]= kilobytes per second
                     argv[1]= speed factor
    "Write speed H:" up to 1 result from -list_speeds
                     see "Write speed  :" 
    "Write speed L:" up to 1 result from -list_speeds
                     see "Write speed  :" 
    "Write speed h:" up to 1 result from -list_speeds
                     see "Write speed  :" 
    "Write speed l:" up to 1 result from -list_speeds
                     see "Write speed  :" 
    "libburn    in use :" up to 1 result from -version
                     argv[0]= version text
                     argv[1]= minimum version requirement
    "libburn OS adapter:" up to 1 result from -version
                     argv[0]= adapter description
                     (Note: prefix is "libburn OS adapter:  ")
    "libisoburn in use :" up to 1 result from -version
                     argv[0]= version text
                     argv[1]= minimum version requirement
    "libisofs   in use :" up to 1 result from -version
                     argv[0]= version text
                     argv[1]= minimum version requirement
    "libjte     in use :" up to 1 result from -version
                     argv[0]= version text
                     argv[1]= minimum version requirement
    "xorriso version   :" up to 1 result from -version
                     argv[0]= version text
    "zisofs       :" up to 1 result from -list_extras
                     argv[0]= "yes" or "no"
    ------------------------------------------------------------------------

    @since 1.2.6
    @param xorriso    The environment handle
    @param flag       Unused yet. Submit 0.
    @return           <=0 error , >0 success
*/
int Xorriso_sieve_big(struct XorrisO *xorriso, int flag);


/* The outlist stack allows to redirect the info and result messages from
   their normal channels into a pair of string lists which can at some
   later time be retrieved by the application.
   These redirection caches can be stacked to allow stacked applications.
   xorriso itself uses them for internal purposes. 

   The call Xorriso_start_msg_watcher() starts a concurrent thread which
   uses outlists to collect messages and to hand them over by calling
   application provided functions.
*/

/* A list item able of forming double chained lists */
struct Xorriso_lsT;

/** Maximum number of stacked redirections */
#define Xorriso_max_outlist_stacK 32

/** Enable a new redirection of info and/or result channel. The normal message
    output and eventual older redirections will not see new messages until
    the redirection is ended by a call to Xorriso_pull_outlists() with the
    stack_handle value returned by this call.
    If Xorriso_option_pkt_output() is set to "on", then it will consolidate
    output in the result_list of Xorriso_fetch_outlists() resp.
    Xorriso_pull_outlists().
    @param xorriso      The environment handle
    @param stack_handle returns an id number which is unique as long as
                        its redirection is stacked. Do not interpret it and
                        do not use it after its redirection was pulled from
                        the stack.
    @param flag         Bitfield for control purposes
                         bit0= redirect result channel
                         bit1= redirect info channel
                        If bit0 and bit1 are 0, both channels get redirected.
    @return           1 on success, <=0 if failure
*/
int Xorriso_push_outlists(struct XorrisO *xorriso, int *stack_handle,
                          int flag);


/** Obtain the currently collected text messages of redirected info and
    result channel. 
    The messages are handed out as two lists. Both lists have to be disposed
    via Xorriso_lst_destroy_all() when they are no longer needed.
    The message lists are either NULL or represented by their first
    Xorriso_lsT item.

    This call is safe for being used by a concurrent thread while a
    xorriso API call is being executed on the same struct XorrisO.
    In such a situation, it should not be used with high frequency in order
    not to hamper the ongoing xorriso operation by blocking its message
    output facility. A hundred times per second should be enough.

    @since 1.2.6
    @param xorriso       The environment handle
    @param stack_handle  An id number returned by Xorriso_push_outlists()
                         and not yet revoked by Xorriso_pull_outlists().
                         Submit -1 to address the most recent valid id.
    @param result_list   Result and mark messages (usually directed to stdout)
    @param info_list     Info and mark messages (usually directed to stderr)
    @param flag          Bitfield for control purposes
                          bit0= fetch result channel
                          bit1= fetch info channel
                         If bit0 and bit1 are 0, both channels get fetched.
    @return              1 on success, <=0 if failure
*/
int Xorriso_fetch_outlists(struct XorrisO *xorriso, int stack_handle,
                           struct Xorriso_lsT **result_list,
                           struct Xorriso_lsT **info_list, int flag);


/** Inquire whether messages are pending in redirected result and info channel.
    This may be used to determine whether a concurrent message watcher already
    has obtained all pending messages.
    @since 1.2.6
    @param xorriso       The environment handle
    @param stack_handle  An id number returned by Xorriso_push_outlists()
                         and not yet revoked by Xorriso_pull_outlists().
                         Submit -1 to address the most recent valid id.
    @param timeout       Number of seconds after which to return despite
                         flag bit 2
    @param flag          Bitfield for control purposes
                          bit0= fetch result channel
                          bit1= fetch info channel
                          bit2= wait and retry until return is 0 or -1
                         If bit0 and bit1 are 0, both channels get fetched.
    @return              If > 0:
                           bit0= messages are pending in outlists
                           bit1= message watcher is processing fetched messages
                         Else:
                          0= no messages are pending anywhere
                         -1= inappropriate stack_handle
                         -2= locking failed
*/
int Xorriso_peek_outlists(struct XorrisO *xorriso, int stack_handle,
                          int timeout, int flag);


/** Disable the redirection given by stack_handle. If it was the current
    receiver of messages then switch output to the next older redirection
    resp. to the normal channels if no redirections are stacked any more.
    The messages collected by the disabled redirection are handed out as
    two lists. Both lists have to be disposed via Xorriso_lst_destroy_all() 
    when they are no longer needed.
    The message lists are either NULL or represented by their first
    Xorriso_lsT item.
    @param xorriso       The environment handle
    @param stack_handle  An id number returned by Xorriso_push_outlists()
                         and not yet revoked by Xorriso_pull_outlists().
                         This handle is invalid after the call.
                         Submit -1 to address the most recent valid id.
    @param result_list   Result and mark messages (usually directed to stdout)
    @param info_list     Info and mark messages (usually directed to stderr)
    @param flag          unused yet, submit 0
    @return              1 on success, <=0 if failure
*/
int Xorriso_pull_outlists(struct XorrisO *xorriso, int stack_handle,
                          struct Xorriso_lsT **result_list,
                          struct Xorriso_lsT **info_list, int flag);


/** Redirect output by Xorriso_push_outlists() and start a thread which
    fetches this output and performs a call of a given function with each
    message that is obtained.
    @since 1.2.6
    @param xorriso        The environment handle
    @param result_handler Pointer to the function which shall be called with
                          each result message. A NULL pointer causes output
                          to be directed to stdout resp. to be interpreted
                          as -pkt_output format if this is enabled by
                          Xorriso_option_pkt_output().
                          The function should return 1. A return value of -1
                          urges not to call again with further lines.
    @param result_handle  The first argument of (*result_handler)(). It shall
                          point to a memory object that knows all necessary
                          external parameters for running (*result_handler)().
                          Submit NULL if result_handler is NULL.
    @param info_handler   Pointer to the function which shall be called with
                          each info message. A NULL pointer causes output to
                          be directed to stderr resp. to -as mkisofs -log-file.
                          The function should return 1. A return value of -1
                          urges not to call again with further lines.
    @param info_handle    The first argument of (*info_handler)(). It shall
                          point to a memory object that knows all necessary
                          external parameters for running (*info_handler)().
                          Submit NULL if info_handler is NULL.
    @param flag           unused yet, submit 0
    @return               1 on success, <=0 if failure (e.g. there is already
                                                        a watcher active)
*/
int Xorriso_start_msg_watcher(struct XorrisO *xorriso,
                    int (*result_handler)(void *handle, char *text),
                    void *result_handle,
                    int (*info_handler)(void *handle, char *text),
                    void *info_handle,
                    int flag);


/** Revoke output redirection by Xorriso_start_msg_watcher() and end the
    watcher thread. If text messages are delivered when Xorriso_pull_outlists()
    is called, then they get put out through the active handler functions.
    @since 1.2.6
    @param xorriso        The environment handle
    @param flag           Bitfield for control purposes:
                          bit0= do not issue SORRY message if no message
                                watcher is active
    @return               1 on success, <=0 if failure
*/
int Xorriso_stop_msg_watcher(struct XorrisO *xorriso, int flag);


/** Obtain the text message from the current list item.
    @param entry    The current list item
    @param flag     unused yet, submit 0
    @return         Pointer to the text content of the list item.
                    This pointer does not have to be freed.
*/
char *Xorriso_lst_get_text(struct Xorriso_lsT *entry, int flag);


/** Obtain the address of the next item in the chain of messages.
    An iteration over the output of Xorriso_pull_outlists() starts at the
    returned result_list resp. info_list and ends when this function returns
    NULL.
    @param entry    The current list item
    @param flag     unused yet, submit 0
    @return         Pointer to the next list item or NULL if end of list.
                    This pointer does not have to be freed.
*/
struct Xorriso_lsT *Xorriso_lst_get_next(struct Xorriso_lsT *entry, int flag);


/** Obtain the address of the previous item in the chain of messages.
    @param entry    The current list item
    @param flag     unused yet, submit 0
    @return         Pointer to the previous list item or NULL if start of list.
                    This pointer does not have to be freed.
*/
struct Xorriso_lsT *Xorriso_lst_get_prev(struct Xorriso_lsT *entry, int flag);


/** Destroy all list items which are directly or indirectly connected to
    the given link item.
    All pointers obtained by Xorriso_lst_get_text() become invalid by this.
    Apply this to each of the two list handles obtained by 
    Xorriso_pull_outlists() when the lists are no longer needed.
    @param lstring  *lstring will be freed and set to NULL.
                    It is not dangerous to submit a pointer to a NULL-pointer.
    @param flag     unused yet, submit 0
    @return         -1= lstring was NULL (i.e. wrong use of this call),
                     0= *lstring was already NULL,
                     1= item actually disposed
*/
int Xorriso_lst_destroy_all(struct Xorriso_lsT **lstring, int flag);



/* ---------------------------- Command API ------------------------ */
/*    See man 1 xorriso for explanation of the particular commands   */
/*
   Before each call to a command function, there should happen:
   Xorriso_set_problem_status() with empty severity text.

   After each call to a command function, there should happen:
   Xorriso_eval_problem_status()
   One should follow its eventual advice to abort.

   Commands with a varying number of arguments get then passed like
   Xorriso_interpreter(). E.g.:
     int Xorriso_option_add(struct XorrisO *xorriso, int argc, char **argv,
                            int *idx, int flag);
   The command functions will begin to read the arguments at position *idx
   and will see the list end either at the next argv which contains the
   -list_delimiter text or at argv[argc-1].
   After the call, *idx will be the index of the first not yet interpreted
   argv.

   Do not set any flag bits which are not described by "@param flag".
   I.e. if flag is not mentioned, then submit 0.
   Yet undefined flag bits might get a meaning in future. Unset bits will
   then produce the traditional behavior, whereas set bits might bring
   surprises to inadverted callers.
*/


/* Command -abort_on */
int Xorriso_option_abort_on(struct XorrisO *xorriso, char *severity, int flag);

/* Command -abstract_file */
/* @since 0.6.0 */
int Xorriso_option_abstract_file(struct XorrisO *xorriso, char *name,
                                 int flag);

/* Command -acl "on"|"off" */
int Xorriso_option_acl(struct XorrisO *xorriso, char *mode, int flag);

/* Command -add */
/* @param flag bit0=do not report the added item
               bit1=do not reset pacifier, no final pacifier message
*/
int Xorriso_option_add(struct XorrisO *xorriso, int argc, char **argv,
                       int *idx, int flag);

/* Command -add_plainly "on"|"off" */
int Xorriso_option_add_plainly(struct XorrisO *xorriso, char *mode,
                                   int flag);


/* Command -alter_date, -alter_date_r */
/* @param flag bit0=recursive (-alter_date_r)
*/
int Xorriso_option_alter_date(struct XorrisO *xorriso, 
                              char *time_type, char *timestring,
                              int argc, char **argv, int *idx, int flag);

/* Command -append_partition */
/* @since 0.6.4 */
int Xorriso_option_append_partition(struct XorrisO *xorriso, char *partno_text,
                                  char *type_text, char *image_path, int flag);

/* Command -application_id */
int Xorriso_option_application_id(struct XorrisO *xorriso, char *name, 
                                  int flag);

/* Command -as */
/* @param flag bit0=do not report the added item
               bit1=do not reset pacifier, no final pacifier message
*/  
int Xorriso_option_as(struct XorrisO *xorriso, int argc, char **argv,
                      int *idx, int flag);

/* Command -assert_volid */
int Xorriso_option_assert_volid(struct XorrisO *xorriso, char *pattern,
                                char *severity, int flag);

/* Command -auto_charset "on"|"off" */
int Xorriso_option_auto_charset(struct XorrisO *xorriso, char *mode, int flag);

/* Command -backslash_codes */
int Xorriso_option_backslash_codes(struct XorrisO *xorriso, char *mode,
                                   int flag);

/* Command -ban_stdio_write */
int Xorriso_option_ban_stdio_write(struct XorrisO *xorriso, int flag);

/* Command -biblio_file */
/* @since 0.6.0 */
int Xorriso_option_biblio_file(struct XorrisO *xorriso, char *name, int flag);

/* Command -blank and -format */
/* @param flag bit0= format rather than blank
   @return <=0 error , 1 success, 2 revoked by -reassure
*/
int Xorriso_option_blank(struct XorrisO *xorriso, char *mode, int flag);

/* Command -boot_image */
int Xorriso_option_boot_image(struct XorrisO *xorriso, char *form,
                              char *treatment, int flag);

/* Command -calm_drive */
int Xorriso_option_calm_drive(struct XorrisO *xorriso, char *which, int flag);

/* Command -cd alias -cdi */
int Xorriso_option_cdi(struct XorrisO *xorriso, char *iso_rr_path, int flag);

/* Command -cdx */
int Xorriso_option_cdx(struct XorrisO *xorriso, char *disk_path, int flag);

/* Command -changes_pending */
/* @since 1.2.2 */
int Xorriso_option_changes_pending(struct XorrisO *xorriso, char *state,
                                   int flag);

/* Command -charset */
/* @param flag bit0= set in_charset
               bit1= set out_charset
*/
int Xorriso_option_charset(struct XorrisO *xorriso, char *name, int flag);

/* Command -check_md5 and -check_md5_r
   @param flag bit0= issue summary message
               bit1= do not reset pacifier, no final pacifier message
               bit2= do not issue pacifier messages at all
               bit3= recursive: -check_md5_r
*/
int Xorriso_option_check_md5(struct XorrisO *xorriso,
                             int argc, char **argv, int *idx, int flag);

/* Command -check_media */
int Xorriso_option_check_media(struct XorrisO *xorriso,
                               int argc, char **argv, int *idx, int flag);

/* Command -check_media_defaults */
int Xorriso_option_check_media_defaults(struct XorrisO *xorriso,
                                    int argc, char **argv, int *idx, int flag);

/* Command -chgrp alias -chgrpi , chgrp_r alias chgrpi */
/* @param flag bit0=recursive (-chgrp_r)
*/
int Xorriso_option_chgrpi(struct XorrisO *xorriso, char *gid,
                          int argc, char **argv, int *idx, int flag);

/* Command -chmod alias -chmodi , -chmod_r alias chmod_ri */
/* @param flag bit0=recursive (-chmod_r)
*/
int Xorriso_option_chmodi(struct XorrisO *xorriso, char *mode,
                          int argc, char **argv, int *idx, int flag);

/* Command -chown alias -chowni , chown_r alias chown_ri */
/* @param flag bit0=recursive (-chown_r)
*/
int Xorriso_option_chowni(struct XorrisO *xorriso, char *uid,
                          int argc, char **argv, int *idx, int flag);

/* Command -clone */
/* @since 1.0.2 */
int Xorriso_option_clone(struct XorrisO *xorriso, char *origin, char *dest,
                         int flag);

/* Command -close "on"|"off" */
int Xorriso_option_close(struct XorrisO *xorriso, char *mode, int flag);

/* Command -close_damaged */
/* @since 1.1.0 */
int Xorriso_option_close_damaged(struct XorrisO *xorriso, char *mode,
                                 int flag);

/* Command -close_filter_list */
int Xorriso_option_close_filter_list(struct XorrisO *xorriso, int flag);

/* Command -commit */
/* @param flag bit0= leave indrive and outdrive acquired as they were,
                     i.e. do not acquire outdrive as new in-out-drive
               bit1= do not perform eventual -reassure
   @return <=0 error , 1 success, 2 revoked by -reassure
*/
int Xorriso_option_commit(struct XorrisO *xorriso, int flag);

/* Command -commit_eject  */
/* @return <=0 error , 1 success, 2 revoked by -reassure
*/
int Xorriso_option_commit_eject(struct XorrisO *xorriso, char *which, int flag);

/* Command -compare and -compare_r
   @param flag bit0= issue summary message
               bit1= do not reset pacifier, no final pacifier message
               bit2= do not issue pacifier messages at all
               bit3= recursive: -compare_r
*/
int Xorriso_option_compare(struct XorrisO *xorriso, char *disk_path,
                           char *iso_path, int flag);

/* Command -compliance */
int Xorriso_option_compliance(struct XorrisO *xorriso, char *mode, int flag);

/* Command -copyright_file */
/* @since 0.6.0 */
int Xorriso_option_copyright_file(struct XorrisO *xorriso, char *name,
                                  int flag);

/* Command -cp_clone */
/* @since 1.0.2 */
int Xorriso_option_cp_clone(struct XorrisO *xorriso, int argc, char **argv,
                            int *idx, int flag);

/* Command -cpr alias -cpri */
int Xorriso_option_cpri( struct XorrisO *xorriso, int argc, char **argv,
                         int *idx, int flag);

/* Command -cpx , -cpax, -cp_rx , -cp_rax */
/* @param flag bit0= recursive (-cp_rx, -cp_rax)
               bit1= full property restore (-cpax, -cp_rax)
*/
int Xorriso_option_cpx(struct XorrisO *xorriso, int argc, char **argv,
                         int *idx, int flag);

/* Command -cut_out */
int Xorriso_option_cut_out(struct XorrisO *xorriso, char *disk_path,
                char *start, char *count, char *iso_rr_path, int flag);

/* Command -dev , -indev, -outdev */
/* @param flag bit0=use as indev , bit1= use as outdev
   @return <=0 error , 1 success, 2 revoked by -reassure
*/
int Xorriso_option_dev(struct XorrisO *xorriso, char *adr, int flag);

/* Command -data_cache_size */
/* @since 1.2.2 */
int Xorriso_option_data_cache_size(struct XorrisO *xorriso, char *num_tiles,
                                   char *tile_blocks, int flag);

/* Command -devices */
/* @param flag bit0= perform -device_links rather than -devices
   @return <=0 error , 1 success, 2 revoked by -reassure
*/
int Xorriso_option_devices(struct XorrisO *xorriso, int flag);

/* Command -dialog "on"|"off" */
int Xorriso_option_dialog(struct XorrisO *xorriso, char *mode, int flag);

/* Command -disk_dev_ino "on"|"off" */
int Xorriso_option_disk_dev_ino(struct XorrisO *xorriso, char *mode, int flag);

/* Command -disk_pattern "on"|"ls"|"off" */
int Xorriso_option_disk_pattern(struct XorrisO *xorriso, char *mode, int flag);

/* Command -displacement [-]offset */
/* @since 0.6.6 */
int Xorriso_option_displacement(struct XorrisO *xorriso, char *value,
                                int flag);

/* Command -drive_class */
int Xorriso_option_drive_class(struct XorrisO *xorriso,
                               char *d_class, char *pattern, int flag);

/* Command -dummy "on"|"off" */
int Xorriso_option_dummy(struct XorrisO *xorriso, char *mode, int flag);

/* Command -dvd_obs "default"|"32k"|"64k" */
int Xorriso_option_dvd_obs(struct XorrisO *xorriso, char *obs, int flag);

/* Command -early_stdio_test */
/* @since 1.0.6 */
int Xorriso_option_early_stdio_test(struct XorrisO *xorriso, char *mode,
                                    int flag);

/* Command -eject */
/* @param flag bit0=do not report toc of eventually remaining drives
*/
int Xorriso_option_eject(struct XorrisO *xorriso, char *which, int flag);

/* Command -end , and -rollback_end */
/* @param flag bit0= discard pending changes 
   @return <=0 error , 1 success, 2 revoked by -reassure
*/
int Xorriso_option_end(struct XorrisO *xorriso, int flag);

/* Command -errfile_log marked|plain  path|-|"" */
int Xorriso_option_errfile_log(struct XorrisO *xorriso,
                               char *mode, char *path, int flag);

/* Command -error_behavior */
int Xorriso_option_error_behavior(struct XorrisO *xorriso, 
                                  char *occasion, char *behavior, int flag);

/* Command -external_filter */
int Xorriso_option_external_filter(struct XorrisO *xorriso,
                                   int argc, char **argv, int *idx, int flag);

/* Command -extract , -extract_single */
/* @param flag bit0=do not report the restored item
               bit1=do not reset pacifier, no final pacifier message
               bit5= -extract_single: do not insert directory tree
*/
int Xorriso_option_extract(struct XorrisO *xorriso, char *disk_path,
                       char *iso_path, int flag);

/* Command -extract_cut */
int Xorriso_option_extract_cut(struct XorrisO *xorriso, char *iso_rr_path,
                          char *start, char *count, char *disk_path, int flag);

/* Command -file_size_limit */
int Xorriso_option_file_size_limit(struct XorrisO *xorriso,
                                   int argc, char **argv, int *idx, int flag);


/* Command -find alias -findi, and -findx */
/* @param flag bit0= -findx rather than -findi
               bit1= do not reset pacifier, no final pacifier message
                     do not reset find_compare_result
*/ 
int Xorriso_option_find(struct XorrisO *xorriso, int argc, char **argv,
                       int *idx, int flag);

/* Command -follow */
int Xorriso_option_follow(struct XorrisO *xorriso, char *mode, int flag);

/* Command -fs */
int Xorriso_option_fs(struct XorrisO *xorriso, char *size, int flag);

/* Command -getfacl alias -getfacli, -getfacl_r alias -getfacl_ri */
/* @param flag bit0=recursive -getfacl_r
*/
int Xorriso_option_getfacli(struct XorrisO *xorriso,
                            int argc, char **argv, int *idx, int flag);

/* Command -gid */
int Xorriso_option_gid(struct XorrisO *xorriso, char *gid, int flag);

/* Command -grow_blindly */
int Xorriso_option_grow_blindly(struct XorrisO *xorriso, char *msc2, int flag);

/* Command -hardlinks "on"|"off" */
int Xorriso_option_hardlinks(struct XorrisO *xorriso, char *mode, int flag);

/* Command -help and part of -prog_help */
int Xorriso_option_help(struct XorrisO *xorriso, int flag);

/* Option -hfsplus "on"|"off" */
int Xorriso_option_hfsplus(struct XorrisO *xorriso, char *mode, int flag);

/* Command -hide */
/* @since 0.6.0 */
int Xorriso_option_hide(struct XorrisO *xorriso, char *hide_state,
                        int argc, char **argv, int *idx, int flag);

/* Command -history */
int Xorriso_option_history(struct XorrisO *xorriso, char *line, int flag);

/* Command -iso_rr_pattern "on"|"ls"|"off" */
int Xorriso_option_iso_rr_pattern(struct XorrisO *xorriso, char *mode,
                                  int flag);

/* Command -jigdo aspect argument */
/* @since 0.6.4 */
int Xorriso_option_jigdo(struct XorrisO *xorriso, char *aspect, char *arg,
                         int flag);

/* Command -joliet "on"|"off" */
int Xorriso_option_joliet(struct XorrisO *xorriso, char *mode, int flag);

/* Command -launch_frontend */
/* @since 1.2.6 */
int Xorriso_option_launch_frontend(struct XorrisO *xorriso,
                                   int argc, char **argv, int *idx, int flag);

/* Command -list_arg_sorting */
/* @since 1.2.2 */
int Xorriso_option_list_arg_sorting(struct XorrisO *xorriso, int flag);

/* Command -list_delimiter */
int Xorriso_option_list_delimiter(struct XorrisO *xorriso, char *text,
                                  int flag);

/* Command -list_extras */
/* @since 1.1.6 */
int Xorriso_option_list_extras(struct XorrisO *xorriso, char *mode, int flag);

/* Command -list_formats */
int Xorriso_option_list_formats(struct XorrisO *xorriso, int flag);

/* Command -list_profiles */
int Xorriso_option_list_profiles(struct XorrisO *xorriso, char *which,
                                 int flag);

/* Command -list_speeds */
/* @since 1.1.2 */
int Xorriso_option_list_speeds(struct XorrisO *xorriso, int flag);

/* Command -lns alias -lnsi */
/* @since 1.2.6 */
int Xorriso_option_lnsi(struct XorrisO *xorriso, char *target, char *path,
                        int flag);

/* Command -load session|track|sbsector value */
/* @param flag bit0= with adr_mode sbsector: adr_value is possibly 16 too high
   @return <=0 error , 1 success, 2 revoked by -reassure
*/
int Xorriso_option_load(struct XorrisO *xorriso, char *adr_mode,
                          char *adr_value, int flag);

/* Command -logfile */
int Xorriso_option_logfile(struct XorrisO *xorriso, char *channel, 
                                                      char *fileadr, int flag);

/* Command -ls  alias -lsi   and -lsl  alias -lsli
       and -lsd alias -lsdi  and -lsdl alias -lsdli
       and -du  alias -dui   and -dus  alias -dusi
   @param flag bit0= long format (-lsl , -du)
               bit1= do not expand patterns but use literally
               bit2= du rather than ls
               bit3= list directories as themselves (ls -d)
*/
int Xorriso_option_lsi(struct XorrisO *xorriso, int argc, char **argv,
                      int *idx, int flag);

/* Command -lsx, -lslx, -lsdx , -lsdlx , -dux , -dusx
   @param flag bit0= long format (-lslx , -dux)
               bit1= do not expand patterns but use literally
               bit2= du rather than ls
               bit3= list directories as themselves (ls -d)
*/
int Xorriso_option_lsx(struct XorrisO *xorriso, int argc, char **argv,
                      int *idx, int flag);

/* Command -map */
/* @param flag bit0=do not report the added item
               bit1=do not reset pacifier, no final pacifier message
*/
int Xorriso_option_map(struct XorrisO *xorriso, char *disk_path,
                       char *iso_path, int flag);

/* Command -map_l , -compare_l , -update_l , -extract_l */
/* @param flag bit8-11= mode 0= -map_l
                             1= -compare_l
                             2= -update_l
                             3= -extract_l
*/
int Xorriso_option_map_l(struct XorrisO *xorriso, int argc, char **argv,
                         int *idx, int flag);

/* Command -mark */
int Xorriso_option_mark(struct XorrisO *xorriso, char *mark, int flag);

/* Command -md5 */
int Xorriso_option_md5(struct XorrisO *xorriso, char *mode, int flag);

/* Command -mkdir alias -mkdiri */
int Xorriso_option_mkdiri(struct XorrisO *xorriso, int argc, char **argv,
                          int *idx, int flag);

/* Command -mount , -mount_cmd , -session_string */
/* @param bit0= -mount_cmd: print mount command to result channel rather
                            than performing it
          bit1= perform -session_string rather than -mount_cmd
*/ 
int Xorriso_option_mount(struct XorrisO *xorriso, char *dev, char *adr_mode,
                         char *adr, char *cmd, int flag);

/* Command -mount_opts option[:...] */
int Xorriso_option_mount_opts(struct XorrisO *xorriso, char *mode, int flag);

/* Command -move */
int Xorriso_option_move(struct XorrisO *xorriso, char *origin, char *dest,
                        int flag);

/* Command -msg_op */
int Xorriso_option_msg_op(struct XorrisO *xorriso, char *what, char *arg,
                          int flag);

/* Command -mv alias -mvi */
int Xorriso_option_mvi(struct XorrisO *xorriso, int argc, char **argv,
                      int *idx, int flag);

/* Command -no_rc */
int Xorriso_option_no_rc(struct XorrisO *xorriso, int flag);

/* Command -not_leaf , -as mkisofs -hide without '/' */
/* @param flag  bit0= add to iso_rr hide list rather than to disk exclusions
                      @since 0.6.0
                bit1= add to joliet hide list rather than disk exclusions
                      @since 0.6.0
                bit2= add to HFS+ hide list rather than disk exclusions
                      @since 1.2.4
*/
int Xorriso_option_not_leaf(struct XorrisO *xorriso, char *pattern, int flag);

/* Command -not_list , -quoted_not_list */
/* @param flag bit0= -quoted_not_list */
int Xorriso_option_not_list(struct XorrisO *xorriso, char *adr, int flag);

/* Command -not_mgt */
int Xorriso_option_not_mgt(struct XorrisO *xorriso, char *setting, int flag);

/* Command -not_paths , -as mkisofs -hide with '/' */
/* @param flag  bit0= add to iso_rr hide list rather than to disk exclusions
                      @since 0.6.0
                bit1= add to joliet hide list rather than disk exclusions
                      @since 0.6.0
                bit2= enable disk pattern expansion regardless of -disk_pattern
                bit8-13= consolidated hide state bits, duplicating bit0-1
                         @since 1.2.4
                   bit8= add to iso_rr_hidings, same as bit0
                   bit9= add to joliet_hidings, same as bit1
                  bit10= add to hfsplus_hidings
*/ 
int Xorriso_option_not_paths(struct XorrisO *xorriso, int argc, char **argv,
                             int *idx, int flag);

/* Command -options_from_file */
/* @return <=0 error , 1 = success , 3 = request to end program run */
int Xorriso_option_options_from_file(struct XorrisO *xorriso, char *adr,
                                     int flag);

/* Command -osirrox "on"|"off" */
int Xorriso_option_osirrox(struct XorrisO *xorriso, char *mode, int flag);

/* Command -overwrite "on"|"nondir"|"off" */
int Xorriso_option_overwrite(struct XorrisO *xorriso, char *mode, int flag);

/* Command -pacifier */
int Xorriso_option_pacifier(struct XorrisO *xorriso, char *style, int flag);

/* Command -padding */
int Xorriso_option_padding(struct XorrisO *xorriso, char *size, int flag);

/* Command -page */
int Xorriso_option_page(struct XorrisO *xorriso, int len, int width, int flag);

/* Command -paste_in */
int Xorriso_option_paste_in(struct XorrisO *xorriso, char *iso_rr_path,
                          char *disk_path, char *start, char *count, int flag);

/* Command -path_list , -quoted_path_list */
/* @param flag bit0= -quoted_path_list */
int Xorriso_option_path_list(struct XorrisO *xorriso, char *adr, int flag);

/* Command -pathspecs */
int Xorriso_option_pathspecs(struct XorrisO *xorriso, char *mode, int flag);

/* Command -pkt_output */
/* Note: If output is redirected by Xorriso_push_outlists() then mode "on"
         consolidates output in the result output list, not on stdout.
*/
int Xorriso_option_pkt_output(struct XorrisO *xorriso, char *mode, int flag);

/* Command -preparer_id */
/* @since 0.6.2 */
int Xorriso_option_preparer_id(struct XorrisO *xorriso, char *name, int flag);

/* Command -print, -print_info , -print_mark */
/* @param flag bit0-1= output channel:
                       0= result channel
                       1= info channel @since 1.0.6
                       2= mark channel @since 1.0.6
*/
int Xorriso_option_print(struct XorrisO *xorriso, char *text, int flag);

/* Command -print_size
   @param flag bit0= report in mkisofs compatible form on real stdout
*/
int Xorriso_option_print_size(struct XorrisO *xorriso, int flag);

/* Command -prog */
int Xorriso_option_prog(struct XorrisO *xorriso, char *name, int flag);

/* Command -prompt */
int Xorriso_option_prompt(struct XorrisO *xorriso, char *text, int flag);

/* Command -prog_help */
int Xorriso_option_prog_help(struct XorrisO *xorriso, char *name, int flag);

/* Command -publisher */
int Xorriso_option_publisher(struct XorrisO *xorriso, char *name, int flag);

/* Command -pvd_info */
int Xorriso_option_pvd_info(struct XorrisO *xorriso, int flag);

/* Command -pwd alias -pwdi */
int Xorriso_option_pwdi(struct XorrisO *xorriso, int flag);

/* Command -pwdx */
int Xorriso_option_pwdx(struct XorrisO *xorriso, int flag);

/* Command -read_mkisofsrc */
/* @since 0.6.0 */
int Xorriso_option_read_mkisofsrc(struct XorrisO *xorriso, int flag);

/* Command -reassure "on"|"tree"|"off" */
int Xorriso_option_reassure(struct XorrisO *xorriso, char *mode, int flag);

/* Command -report_about */
int Xorriso_option_report_about(struct XorrisO *xorriso, char *severity, 
                                int flag);

/* Command -return_with */
int Xorriso_option_return_with(struct XorrisO *xorriso, char *severity,
                               int exit_value, int flag);

/* Command -rm alias -rmi , -rm_r alias -rm_ri , -rmdir alias -rmdiri */
/* @param flag bit0=recursive , bit2= remove empty directory: rmdir */
int Xorriso_option_rmi(struct XorrisO *xorriso, int argc, char **argv,
                       int *idx, int flag);

/* Command -rockridge "on"|"off" */
int Xorriso_option_rockridge(struct XorrisO *xorriso, char *mode, int flag);

/* Command -rollback */
/* @param flag bit0= do not -reassure
   @return <=0 error , 1 success, 2 revoked by -reassure
*/
int Xorriso_option_rollback(struct XorrisO *xorriso, int flag);

/* Command -rom_toc_scan */
int Xorriso_option_rom_toc_scan(struct XorrisO *xorriso, char *mode, int flag);

/* Command -rr_reloc_dir */
/* @since 1.2.2 */
int Xorriso_option_rr_reloc_dir(struct XorrisO *xorriso, char *name, int flag);

/* Command -scdbackup_tag */
int Xorriso_option_scdbackup_tag(struct XorrisO *xorriso, char *list_path,
                                 char *record_name, int flag);
/* Command -scsi_log */
int Xorriso_option_scsi_log(struct XorrisO *xorriso, char *mode, int flag);

/* Command -session_log */
int Xorriso_option_session_log(struct XorrisO *xorriso, char *path, int flag);

/* Command -setfacl_list alias -setfacl_listi */
int Xorriso_option_setfacl_listi(struct XorrisO *xorriso, char *disk_path,
                                 int flag);

/* Command -setfacl alias -setfacli , -setfacl_r  alias -setfacl_ri */
/* @param flag bit0=recursive -setfacl_r
*/
int Xorriso_option_setfacli(struct XorrisO *xorriso, char *acl_text,
                            int argc, char **argv, int *idx, int flag);

/* Command -setfattr alias -setfattri, -setfattr_r alias -setfattr_ri */
/* @param flag   bit0=recursive -setfattr_r
*/
int Xorriso_option_setfattri(struct XorrisO *xorriso, char *name, char *value,
                            int argc, char **argv, int *idx, int flag);

/* Command -setfattr_list alias -setfattr_listi */
int Xorriso_option_setfattr_listi(struct XorrisO *xorriso, char *path, 
                                  int flag);

/* Command -set_filter , -set_filter_r */ 
/* @param flag   bit0=recursive -set_filter_r
*/
int Xorriso_option_set_filter(struct XorrisO *xorriso, char *name,
                              int argc, char **argv, int *idx, int flag);

/* Command -signal_handling */
/* @param flag bit0= do not yet install the eventual handler
   @since 1.1.0
*/
int Xorriso_option_signal_handling(struct XorrisO *xorriso, char *mode,
                                   int flag);

/* Command -sleep */
/* @since 1.1.8 */
int Xorriso_option_sleep(struct XorrisO *xorriso, char *duration, int flag);

/* Command -speed */
int Xorriso_option_speed(struct XorrisO *xorriso, char *speed, int flag);

/* Command -split_size */
int Xorriso_option_split_size(struct XorrisO *xorriso, char *s, int flag);

/* Command -status */
int Xorriso_option_status(struct XorrisO *xorriso, char *mode, int flag);

/* Command -status_history_max */
int Xorriso_option_status_history_max(struct XorrisO *xorriso, int num1, 
                                      int flag);

/* Command -stdio_sync "on"|"off"|size */
int Xorriso_option_stdio_sync(struct XorrisO *xorriso, char *rythm, int flag);

/* Command -stream_recording */
int Xorriso_option_stream_recording(struct XorrisO *xorriso, char *mode,
                                    int flag);

/* Command -system_id */
int Xorriso_option_system_id(struct XorrisO *xorriso, char *name, int flag);

/* Command -tell_media_space */
int Xorriso_option_tell_media_space(struct XorrisO *xorriso, int flag);

/* Command -temp_mem_limit */
int Xorriso_option_temp_mem_limit(struct XorrisO *xorriso, char *size, 
                                  int flag);

/* Command -toc */
/* @param flag   bit0= short report form as with -dev, no table-of-content
*/
int Xorriso_option_toc(struct XorrisO *xorriso, int flag);

/* Command -toc_of */
int Xorriso_option_toc_of(struct XorrisO *xorriso, char *which, int flag);

/* Command -uid */
int Xorriso_option_uid(struct XorrisO *xorriso, char *uid, int flag);

/* Command -unregister_filter */
int Xorriso_option_unregister_filter(struct XorrisO *xorriso, char *name,
                                     int flag);

/* Command -update and -update_r
   @param flag bit0= issue summary message
               bit1= do not reset pacifier, no final pacifier message
               bit2= do not issue pacifier messages at all
               bit3= recursive: -update_r
*/ 
int Xorriso_option_update(struct XorrisO *xorriso, char *disk_path,
                          char *iso_path, int flag);

/* Command -use_readline */
int Xorriso_option_use_readline(struct XorrisO *xorriso, char *mode, int flag);

/* Command -version */
int Xorriso_option_version(struct XorrisO *xorriso, int flag);

/* Command -volid */
/* @param flag bit0= do not warn of problematic volid
*/
int Xorriso_option_volid(struct XorrisO *xorriso, char *volid, int flag);

/* Command -volset_id */
int Xorriso_option_volset_id(struct XorrisO *xorriso, char *name, int flag);

/* Command -volume_date */
int Xorriso_option_volume_date(struct XorrisO *xorriso,
                               char *time_type, char *timestring, int flag);

/* Command -write_type */
int Xorriso_option_write_type(struct XorrisO *xorriso, char *mode, int flag);

/* There is no Xorriso_option_x() because -x has an effect only in
   Xorriso_prescan_args(). Use the flag bits of Xorriso_interpreter() if
   you want to impose command sorting on your own.
*/

/* Command -xattr "on"|"off" */
int Xorriso_option_xattr(struct XorrisO *xorriso, char *mode, int flag);

/* Command -zisofs */
int Xorriso_option_zisofs(struct XorrisO *xorriso, char *mode, int flag);


#endif /* Xorriso_includeD */


