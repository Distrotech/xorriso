# Copyright 2011 George Danchev <danchev@spnet.net>
# Copyright 2011 Thomas Schmitt <scdbackup@gmx.net>
# Licensed under GNU GPL version 2 or later

SELF=$(basename $0)
RELENG_XORRISO=0
SIMULATE_FAILURE=0
CLEANUP=1
SPECIFIC_HELP=0
START_DIR_DONT_CHANGE=`pwd`
GEN_DATA_DIR=releng_generated_data/${SELF}

#############################################
standalone_or_supervised() {
 case "${RELENG_SCRIPT_RUN_BY_RUN_ALL_AUTO}" in
  1)
     echo "${SELF}: Running in Supervised mode"
  ;;
  *)
     echo "${SELF}: Running in Standalone mode"
  ;;
 esac
}

# Unconditionally shout out the invocation mode - standalone or supervised
standalone_or_supervised

#############################################
print_help() {
cat << EOF

Usage: $SELF -x path/to/xorriso [-k] [-f] [-c] [-h] 
                                              [-- ...test specific options...]
General options:
       -x  absolute or relative path to xorriso binary to be run.
       -k  keep self-generated data.
       -c  cleanup self-generated data kept from previous run and exit.
       -f  simulate failure.
       -h  print this help text
       --  end of general options, begin of test specific options.
EOF
}

#############################################
boldify() {
 if which tput >/dev/null 2>&1; then tput smso; fi
}

#############################################
unboldify() {
 if which tput >/dev/null 2>&1; then tput rmso; fi
}

#############################################
cleanup() {
 if [ ${CLEANUP} -eq 1 ]; then
   # safety net, just in case -> we want to be in the starting
   # directory before removing whatever self-generated stuff
   if [ -d "${GEN_DATA_DIR}" ]; then
       cd "${START_DIR_DONT_CHANGE}" || exit 2
       
       # Verify once again we are in the releng_generated_data directory
       # Check for both returned code of grep and returned matching string
       # There is no "readlink -e" on FreeBSD
       READ_CANON_EXISTS=`cd "${GEN_DATA_DIR}" 2>/dev/null && pwd`
       DIR_NAME_GEN_DATA=`dirname "${READ_CANON_EXISTS}"`
       set +e
       # There is no "grep -P" on FreeBSD
       RET_NON_EMPTY_STRING=`echo "${DIR_NAME_GEN_DATA}" | grep "[a-zA-Z0-9_][a-zA-Z0-9_]*/releng_generated_data$"`
       GREP_RET_GEN_DATA="$?"
       case "${GREP_RET_GEN_DATA}" in
            0)
	      if [ x"${RET_NON_EMPTY_STRING}" != x"" ]; then
                 # now call the nastiness
                 chmod -R +w ${GEN_DATA_DIR}
                 rm -rf ${GEN_DATA_DIR}
#		 boldify
                 printf "${SELF}: Removed (self-generated) %s\n" ${GEN_DATA_DIR}
#		 unboldify
	      else
                 printf "FAIL : ${SELF} : Safety check for being in releng_generated_data directory.\n"
                 printf "FAIL : ${SELF} : GREP returned empty string: ${RET_NON_EMPTY_STRING}.\n"
                 printf "FAIL : ${SELF} : Skipped trying to remove ${GEN_DATA_DIR} directory. Exiting.\n"
	      fi
            ;;
	    *)
              printf "FAIL : ${SELF} : Safety check for being in releng_generated_data directory.\n"
              printf "FAIL : ${SELF} : GREP returned code: ${GREP_RET_GEN_DATA}.\n"
              printf "FAIL : ${SELF} : Skipped trying to remove ${GEN_DATA_DIR} directory. Exiting.\n"
	      exit 30
	    ;;
       esac
   else
       printf "${SELF}: ${GEN_DATA_DIR} does not exist. Nothing to clean.\n"
   fi
 else
#   boldify
   printf "${SELF}: Leaving (self-generated) %s\n" ${GEN_DATA_DIR}
#   unboldify
 fi 
}

#############################################
check_for_xorriso() {
 # $1: if "-x" then check executability

 if test -z "$RELENG_XORRISO" -o "$RELENG_XORRISO" = "0"
 then
   print_help
#  print_specific_help
   echo
   echo "${SELF}: Need -x  absolute or relative path to xorriso binary."
   echo
   exit 31
 fi
 if [ x"$1" = x"-x" -a ! -x "$RELENG_XORRISO" ]
 then
   print_help
#  print_specific_help
   echo
   echo "${SELF}: Path given by option -x does not lead to an executable file."
   echo "Given is: '$RELENG_XORRISO'"
   if test "$RELENG_XORRISO" = "xorriso"
   then
     xorriso=$(type -p xorriso)
     if test -n "xorriso"
     then
       echo "Hint: Try '$xorriso'"
     fi
   fi
   echo
   exit 31
 fi
}


#############################################

# To catch the exit value of a command in a pipe
return_value_file="$GEN_DATA_DIR"/wrapper_"$$"_return_value
return_wrapper()
{
 cmd="$1"
 shift 1
 "$cmd" "$@"
 RET="$?"
 echo "$RET" >"$return_value_file"
 return "$RET"
}

#############################################

next_is=
for i in "$@"
do
  if test "$next_is" = "ignore"
  then :
  elif test "$next_is" = "x"
  then
       RELENG_XORRISO="$i"
       next_is=
  elif test x"$i" = x"-x"
  then
       next_is="x"
  elif test x"$i" = x"-k"
  then
       CLEANUP=0
  elif test x"$i" = x"-c"
  then
       CLEANUP=1
       cleanup
       exit 0
  elif test x"$i" = x"-f"
  then
       SIMULATE_FAILURE=1
  elif test x"$i" = x"-h"
  then
       print_help
       SPECIFIC_HELP=1
  elif test x"$i" = x"--"
  then 
       # Begin of private arguments for caller
       next_is="ignore"
  else
       echo >&2
       echo "Unknown general option: $i" >&2
       print_help
       exit 31
  fi
done

if test "$next_is" = x
then
       echo >&2
       echo "Option -x expects an argument (the path to the xorriso program)" >&2
       print_help
       exit 31
fi
