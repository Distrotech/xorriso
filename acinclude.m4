AC_DEFUN([LIBBURNIA_SET_FLAGS],
[
case $target_os in
freebsd*)
        LDFLAGS="$LDFLAGS -L/usr/local/lib"
        CPPFLAGS="$CPPFLAGS -I/usr/local/include"
        ;;
solaris*)
        LDFLAGS="$LDFLAGS -L/usr/local/lib"
esac
])


AC_DEFUN([TARGET_SHIZZLE],
[
  ARCH=""
  LIBBURNIA_PKGCONFDIR="$libdir"/pkgconfig

  AC_MSG_CHECKING([target operating system])

  LIBBURNIA_LDCONFIG_CMD="echo 'No ldconfig run performed. If needed, configure manually for:'"
  case $target in
    *-*-linux*)
      ARCH=linux
      LIBBURN_ARCH_LIBS=
      LIBBURNIA_LDCONFIG_CMD=ldconfig
      ;;
    *-*-freebsd*)
      ARCH=freebsd
      LIBBURN_ARCH_LIBS=-lcam

      # This may later be overridden by configure --enable-libdir-pkgconfig
      LIBBURNIA_PKGCONFDIR=$(echo "$libdir" | sed 's/\/lib$/\/libdata/')/pkgconfig
      ;;
    *)
      ARCH=
      LIBBURN_ARCH_LIBS=
#      AC_ERROR([You are attempting to compile for an unsupported platform])
      ;;
  esac

  AC_MSG_RESULT([$ARCH])
])


dnl LIBBURNIA_CHECK_ICONV is by Thomas Schmitt, libburnia project
dnl It is based on gestures from:
dnl iconv.m4 serial AM7 (gettext-0.18)
dnl Copyright (C) 2000-2002, 2007-2009 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.
dnl From Bruno Haible.
dnl
AC_DEFUN([LIBBURNIA_CHECK_ICONV],
[

  dnl Check whether it is allowed to link with -liconv
  AC_MSG_CHECKING([for iconv() in separate -liconv ])
  libburnia_liconv="no"
  libburnia_save_LIBS="$LIBS"
  LIBS="$LIBS -liconv"
  AC_TRY_LINK([#include <stdlib.h>
#include <iconv.h>],
    [iconv_t cd = iconv_open("","");
     iconv(cd,NULL,NULL,NULL,NULL);
     iconv_close(cd);],
     [libburnia_liconv="yes"],
     [LIBS="$libburnia_save_LIBS"]
  )
  AC_MSG_RESULT([$libburnia_liconv])

  if test x"$libburnia_save_LIBS" = x"$LIBS"
  then
    dnl GNU iconv has no function iconv() but libiconv() and a macro iconv()
    dnl It is not tested whether this is detected by above macro.
    AC_CHECK_LIB(iconv, libiconv, , )
  fi

  dnl Check for iconv(..., const char **inbuf, ...)
  AC_MSG_CHECKING([for const qualifier with iconv() ])
  AC_TRY_COMPILE([
#include <stdlib.h>
#include <iconv.h>
size_t iconv (iconv_t cd, char * *inbuf, size_t *inbytesleft, char * *outbuf, size_t *outbytesleft);
], [], [libburnia_iconv_const=""], [libburnia_iconv_const="const"]
  )
  if test x$libburnia_iconv_const = xconst
  then
    AC_DEFINE_UNQUOTED([ICONV_CONST], [const])
  else
    AC_DEFINE_UNQUOTED([ICONV_CONST], [])
  fi
  test -z "$libburnia_iconv_const" && libburnia_iconv_const="no"
  AC_MSG_RESULT([$libburnia_iconv_const])
])


dnl LIBBURNIA_ASSERT_ICONV is by Thomas Schmitt, libburnia project
dnl 
AC_DEFUN([LIBBURNIA_ASSERT_ICONV],
[
  if test x$XORRISO_ASSUME_ICONV = x
  then
    dnl Check for the essential gestures of libisofs/util.c
    AC_MSG_CHECKING([for iconv() to be accessible now ])
    AC_TRY_LINK([
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <limits.h>
#include <iconv.h>
#include <locale.h>
#include <langinfo.h>
#include <unistd.h>],
[iconv_t cd = iconv_open("","");
iconv(cd,NULL,NULL,NULL,NULL);
iconv_close(cd);
], [iconv_test="yes"], [iconv_test="no"]
    )
    AC_MSG_RESULT([$iconv_test])
    if test x$iconv_test = xno
    then
      echo >&2
      echo "Cannot get function iconv() to work. Configuration aborted." >&2
      echo "Check whether your system needs a separate libiconv installed." >&2
      echo "If it is installed but not found, try something like" >&2
      echo '  export LDFLAGS="$LDFLAGS -L/usr/local/lib"' >&2 
      echo '  export CPPFLAGS="$CPPFLAGS -I/usr/local/include"' >&2
      echo '  export LIBS="$LIBS -liconv"' >&2
      echo "You may override this test by exporting variable" >&2
      echo "  XORRISO_ASSUME_ICONV=yes" >&2
      echo >&2
      (exit 1); exit 1;
    fi
  fi
])


dnl LIBBURNIA_TRY_READLINE is by Thomas Schmitt, libburnia project
dnl It performs the actual test compilation for readline.
dnl Variable LIBS has to be set by the caller.
AC_DEFUN([LIBBURNIA_TRY_READLINE],
[
    AC_TRY_LINK([
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>],
[HIST_ENTRY **hl;
readline("");
add_history("");
hl= history_list();
], [readline_test="yes"], [readline_test="no"]
    )
])


dnl LIBBURNIA_ASSERT_READLINE is by Thomas Schmitt, libburnia project
dnl It disables xorriso readline if not all needed functions are present
AC_DEFUN([LIBBURNIA_ASSERT_READLINE],
[
  if test x$XORRISO_ASSUME_READLINE = x
  then
    dnl Check for the essential gestures of libisofs/util.c
    AC_MSG_CHECKING([for desired functions in libreadline])
    readline_msg=
    libburnia_save_LIBS="$LIBS"
    LIBS="$LIBS -lreadline"
    LIBBURNIA_TRY_READLINE
    if test x$readline_test = xno
    then
        LIBS="$libburnia_save_LIBS"
        LIBS="$LIBS -lreadline -lcurses"
        LIBBURNIA_TRY_READLINE
        if test x$readline_test = xyes
        then
            readline_msg=", with -lcurses"
        fi
    fi
    if test x$readline_test = xno
    then
        READLINE_DEF=
        LIBS="$libburnia_save_LIBS"
    fi
    AC_MSG_RESULT([$readline_test $readline_msg])
  fi
])


dnl LIBISOBURN_ASSERT_VERS_LIBS is by Thomas Schmitt, libburnia project
dnl It tests whether -Wl,--version-script=... works with the compiler
AC_DEFUN([LIBISOBURN_ASSERT_VERS_LIBS],
[
    libburnia_save_LDFLAGS="$LDFLAGS"
    LDFLAGS="$LDFLAGS -Wl,--version-script=libisoburn/libisoburn.ver"
    AC_TRY_LINK([#include <stdio.h>], [printf("Hello\n");],
                [vers_libs_test="yes"], [vers_libs_test="no"])
    if test x$vers_libs_test = xno
    then
        LDFLAGS="$libburnia_save_LDFLAGS"
    fi
])


dnl LIBBURNIA_SET_PKGCONFIG is by Thomas Schmitt, libburnia project
dnl It determines the install directory for the *.pc file.
dnl Important: Must be performed _after_ TARGET_SHIZZLE
dnl
AC_DEFUN([LIBBURNIA_SET_PKGCONFIG],
[
### for testing --enable-libdir-pkgconfig on Linux
### LIBBURNIA_PKGCONFDIR="$libdir"data/pkgconfig

if test "x$LIBBURNIA_PKGCONFDIR" = "x$libdir"/pkgconfig
then
  dummy=dummy
else
  AC_ARG_ENABLE(libdir-pkgconfig,
  [  --enable-libdir-pkgconfig  Install to $libdir/pkgconfig on any OS, default=no],
   , enable_libdir_pkgconfig="no")
  AC_MSG_CHECKING([for --enable-libdir-pkgconfig])
  if test "x$enable_libdir_pkgconfig" = xyes
  then
    LIBBURNIA_PKGCONFDIR="$libdir"/pkgconfig
  fi
  AC_MSG_RESULT([$enable_libdir_pkgconfig])
fi

libburnia_pkgconfig_override="no"
AC_ARG_ENABLE(pkgconfig-path,
[  --enable-pkgconfig-path=DIR  Absolute path of directory for libisofs-*.pc],
libburnia_pkgconfig_override="yes" , enable_pkgconfig_path="none")
AC_MSG_CHECKING([for overridden pkgconfig directory path])
if test "x$enable_pkgconfig_path" = xno
then
  libburnia_pkgconfig_override="no"
fi
if test "x$enable_pkgconfig_path" = x -o "x$enable_pkgconfig_path" = xyes
then
  libburnia_pkgconfig_override="invalid argument"
fi
if test "x$libburnia_pkgconfig_override" = xyes
then
  LIBBURNIA_PKGCONFDIR="$enable_pkgconfig_path"
  AC_MSG_RESULT([$LIBBURNIA_PKGCONFDIR])
else
  AC_MSG_RESULT([$libburnia_pkgconfig_override])
fi
AC_SUBST(LIBBURNIA_PKGCONFDIR)

dnl For debugging only
### AC_MSG_RESULT([LIBBURNIA_PKGCONFDIR = $LIBBURNIA_PKGCONFDIR])
 
])

