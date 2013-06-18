
# configure.ac stems from xorriso/configure_ac.txt and leads to ./configure
# Copyright (c) 2007 - 2013 Thomas Schmitt <scdbackup@gmx.net>
# Provided under GPL version 2 or later.

AC_INIT([xorriso], [1.3.0], [http://libburnia-project.org])
AC_PREREQ([2.50])

AC_CANONICAL_HOST
AC_CANONICAL_TARGET

LIBBURNIA_SET_FLAGS

AM_INIT_AUTOMAKE([subdir-objects])
AC_CONFIG_MACRO_DIR([./])

dnl Let autoheader produce config.h.in and let configure produce config.h
dnl This saves about 500 characters of compile message per source file.
AC_CONFIG_HEADER(config.h)

AH_TEMPLATE([Xorriso_standalonE], [Define to prepare sources for statically linked xorriso])
AC_DEFINE([Xorriso_standalonE], [])

BURN_MAJOR_VERSION=1
BURN_MINOR_VERSION=3
BURN_MICRO_VERSION=0
AC_SUBST(BURN_MAJOR_VERSION)
AC_SUBST(BURN_MINOR_VERSION)
AC_SUBST(BURN_MICRO_VERSION)

LIBISOFS_MAJOR_VERSION=1
LIBISOFS_MINOR_VERSION=3
LIBISOFS_MICRO_VERSION=0
AC_SUBST(LIBISOFS_MAJOR_VERSION)
AC_SUBST(LIBISOFS_MINOR_VERSION)
AC_SUBST(LIBISOFS_MICRO_VERSION)

dnl The API version codes are defined in libisoburn/libisoburn.h
dnl #define  isoburn_header_version_*


AC_PREFIX_DEFAULT([/usr/local])
test "$prefix" = "NONE" && prefix=$ac_default_prefix

AM_MAINTAINER_MODE

AM_PROG_CC_C_O
AC_C_CONST
AC_C_INLINE
AC_C_BIGENDIAN

dnl Large file support
AC_SYS_LARGEFILE
AC_FUNC_FSEEKO
AC_CHECK_FUNC([fseeko])
if test ! $ac_cv_func_fseeko; then
   AC_ERROR([Libburn requires largefile support.])
fi

AH_TEMPLATE([ICONV_CONST], [Whether to apply const qualifier to iconv inbuf])
if test x$XORRISO_OLD_ICONV_CONFIGURE = x
then

dnl ts B00410 : To detect the need for -liconv and const argument of iconv()
LIBBURNIA_CHECK_ICONV
dnl ts B00411 : To abort configuration if iconv() still cannot be compiled
LIBBURNIA_ASSERT_ICONV

else

dnl Outdated: produces double -liconv and warnings about parameter mismatch
dnl If iconv(3) is in an extra libiconv, then it gets added to variable LIBS.
dnl If not, then no -liconv will be added.
AC_CHECK_LIB(iconv, iconv, , )
dnl GNU iconv has no function iconv() but libiconv() and a macro iconv()
AC_CHECK_LIB(iconv, libiconv, , )

fi

AC_PROG_LIBTOOL
AC_SUBST(LIBTOOL_DEPS)
LIBTOOL="$LIBTOOL --silent"

AC_PROG_INSTALL

AC_CHECK_HEADERS()

AC_CHECK_MEMBER([struct tm.tm_gmtoff],
	[AC_DEFINE(HAVE_TM_GMTOFF, 1,
		[Define this if tm structure includes a tm_gmtoff entry.])],
	,
	[#include <time.h>])

dnl Check if non standard timegm() function is available
AC_CHECK_DECL([timegm],
        [AC_DEFINE(HAVE_TIMEGM, 1, [Define this if timegm function is available])],
        ,
        [#include <time.h>])

dnl Check if non standard eaccess() function is available
AC_CHECK_DECL([eaccess],
        [AC_DEFINE(HAVE_EACCESS, 1, [Define this if eaccess function is available])],
        ,
        [#include <unistd.h>])

THREAD_LIBS=-lpthread
AC_SUBST(THREAD_LIBS)

TARGET_SHIZZLE
AC_SUBST(ARCH)
AC_SUBST(LIBBURNIA_PKGCONFDIR)
AC_SUBST(LIBBURN_ARCH_LIBS)

dnl Check the preconditions for using statvfs() in sg-dummy
dnl (sg-linux and sg-freebsd use statvfs() unconditionally)
AH_TEMPLATE([Libburn_os_has_statvfS], [Define to use statvfs() with libburn stdio])
STATVFS_DEF=-DLibburn_os_has_statvfS
AC_CHECK_HEADER(sys/statvfs.h, X=, STATVFS_DEF=)
AC_CHECK_FUNC([statvfs], X=, STATVFS_DEF=)
if test x$STATVFS_DEF = x-DLibburn_os_has_statvfS
then
    AC_DEFINE([Libburn_os_has_statvfS], [])
fi


dnl Add compiler-specific flags

dnl See if the user wants aggressive optimizations of the code
AC_ARG_ENABLE(debug,
[  --enable-debug          Disable aggressive optimizations [default=yes]],
              , enable_debug=yes)
if test x$enable_debug != xyes; then
    if test x$GCC = xyes; then
        CFLAGS="-O3 $CFLAGS"
        CFLAGS="-fexpensive-optimizations $CFLAGS"
    fi
    CFLAGS="-DNDEBUG $CFLAGS"
else
    if test x$GCC = xyes; then
        CFLAGS="-g -pedantic -Wall -Wextra -Wno-unused-parameter $CFLAGS"
    fi
    CFLAGS="-DDEBUG $CFLAGS"
fi


dnl Determine target directory for libisoburn-*.pc
dnl Important: Must be performed _after_ TARGET_SHIZZLE
dnl
LIBBURNIA_SET_PKGCONFIG


AH_TEMPLATE([Xorriso_with_readlinE], [Define to use libreadline])
AC_ARG_ENABLE(libreadline,
[  --enable-libreadline    Enable use of libreadline by xorriso, default=yes],
               , enable_libreadline=yes)
if test x$enable_libreadline = xyes; then
dnl Check whether there is readline-devel and readline-runtime.
dnl If not, erase this macro which would enable use of readline(),add_history()
    READLINE_DEF="-DXorriso_with_readlinE"

    if test x$XORRISO_OLD_READLINE_CONFIGURE = x
    then

dnl ts B00411 : To disable readline if not all needed functions are present
        LIBBURNIA_ASSERT_READLINE

    else
dnl The empty yes case obviously causes -lreadline to be linked
        AC_CHECK_HEADER(readline/readline.h, AC_CHECK_LIB(readline, readline, , READLINE_DEF= ), READLINE_DEF= )
dnl The X= in the yes case prevents that -lreadline gets linked twice
        AC_CHECK_HEADER(readline/history.h, AC_CHECK_LIB(readline, add_history, X= , READLINE_DEF= ), READLINE_DEF= )

    fi
else
    READLINE_DEF=
fi
if test x$READLINE_DEF = x-DXorriso_with_readlinE
then
    AC_DEFINE([Xorriso_with_readlinE], [])
fi

AH_TEMPLATE([Libisofs_with_aaip_acL], [Define to use ACL capabilities])
AC_ARG_ENABLE(libacl,
[  --enable-libacl         Enable use of ACL functions by libisofs, default=yes],
               , enable_libacl=yes)
if test x$enable_libacl = xyes; then
dnl Check whether there is libacl-devel and libacl-runtime.
dnl If not, erase this macro which would enable use of acl_to_text and others
    LIBACL_DEF="-DLibisofs_with_aaip_acL"
dnl The empty yes case obviously causes -lacl to be linked
    has_acl_h_but_no_func=0
    AC_CHECK_HEADER(sys/acl.h, AC_CHECK_LIB(acl, acl_to_text, , has_acl_h_but_no_libacl=1 ), LIBACL_DEF= )
    if test "$has_acl_h_but_no_libacl" = 1
    then
       AC_CHECK_LIB(c, acl_to_text, X= , LIBACL_DEF= )
    fi
else
    LIBACL_DEF=
fi
if test x$LIBACL_DEF = x-DLibisofs_with_aaip_acL
then
    AC_DEFINE([Libisofs_with_aaip_acL], [])
fi


AH_TEMPLATE([Libisofs_with_aaip_xattR], [Define to use xattr capabilities])
AH_TEMPLATE([Libisofs_with_freebsd_extattR], [Define to use extattr capabilities])
AC_ARG_ENABLE(xattr,
[  --enable-xattr          Enable use of xattr by libisofs, default=yes],
               , enable_xattr=yes)
if test x$enable_xattr = xyes; then
dnl Check whether there is the header for GNU/Linux xattr.
dnl If not, erase this macro which would enable use of listxattr and others
    XATTR_DEF="-DLibisofs_with_aaip_xattR"
    AC_CHECK_HEADER(attr/xattr.h, AC_CHECK_LIB(c, listxattr, X= , XATTR_DEF= ), XATTR_DEF= )
    if test "x$XATTR_DEF" = x
    then
       XATTR_DEF="-DLibisofs_with_freebsd_extattR"
       AC_CHECK_HEADER(sys/extattr.h, AC_CHECK_LIB(c, extattr_list_file, X=, XATTR_DEF= ), XATTR_DEF= )
    fi
else
    XATTR_DEF=
fi
if test x$XATTR_DEF = x-DLibisofs_with_aaip_xattR
then
    AC_DEFINE([Libisofs_with_aaip_xattR], [])
fi
if test x$XATTR_DEF = x-DLibisofs_with_freebsd_extattR
then
    AC_DEFINE([Libisofs_with_freebsd_extattR], [])
fi


AH_TEMPLATE([Libisofs_with_zliB], [Define to use compression via zlib])
AH_TEMPLATE([LIBJTE_WITH_ZLIB], [Allow libjte to use zlib])
AC_ARG_ENABLE(zlib,
[  --enable-zlib           Enable use of zlib by libisofs, default=yes],
               , enable_zlib=yes)
if test x$enable_zlib = xyes; then
dnl Check whether there is the header for zlib.
dnl If not, erase this macro which would enable use of compress2() and others.
dnl Linking fails on SuSE 9.0 because zlib has compress2() but lacks 
dnl compressBound(). So compressBound is the more modern thing to test.
dnl The empty parameter after "compressBound" causes -lz.
    ZLIB_DEF=yes
    AC_CHECK_HEADER(zlib.h, AC_CHECK_LIB(z, compressBound, , ZLIB_DEF= ), ZLIB_DEF= )
else
    ZLIB_DEF=
fi
if test x$ZLIB_DEF = xyes
then
    AC_DEFINE([Libisofs_with_zliB], [])
    AC_DEFINE([LIBJTE_WITH_ZLIB], [])
fi


# libjte source is included in GNU xorriso. Enable it if zlib is enabled.
AH_TEMPLATE([Xorriso_with_libjtE],
            [Define to use Jigdo Template Extraction via libjte])
AH_TEMPLATE([Libisofs_with_libjtE],
            [Define to use Jigdo Template Extraction via libjte])
if test x$ZLIB_DEF = xyes
then
    AC_DEFINE([Xorriso_with_libjtE], [])
    AC_DEFINE([Libisofs_with_libjtE], [])
    LIBJTE_DEF=yes
    echo "enabled  built-in libjte"
else
    LIBJTE_DEF=
    echo "disabled libjte because zlib not enabled or not available"
fi


AH_TEMPLATE([THREADED_CHECKSUMS],
            [Define to use multi-threading in built-in libjte])
AC_ARG_ENABLE(jtethreads,
[  --enable-jtethreads     Enable multi-threading in libjte, default=yes],
               , enable_jtethreads=yes) 
if test x$LIBJTE_DEF = xyes
then
    if test "x$enable_jtethreads" = xyes; then
        AC_DEFINE([THREADED_CHECKSUMS], [])
        echo "enabled  multi-threading in libjte"
    else
        echo "disabled multi-threading in libjte"
    fi

fi


AH_TEMPLATE([LIBJTE_WITH_LIBBZ2],
            [Define to use libbz2 by built-in libjte])
AC_ARG_ENABLE(libbz2,
[  --enable-libbz2         Enable use of libbz2 by libjte, default=yes],
               , enable_libbz2=yes)
if test "x$enable_libbz2" = xyes -a x$LIBJTE_DEF = xyes ; then
dnl Check whether there is the header for libbz2.
    BZIP2_DEF=yes
    AC_CHECK_HEADER(bzlib.h, AC_CHECK_LIB(bz2, BZ2_bzCompressInit, , BZIP2_DEF= ), BZIP2_DEF= )
else
    BZIP2_DEF=
fi
if test x$BZIP2_DEF = xyes
then
    AC_DEFINE([LIBJTE_WITH_LIBBZ2], [])
    BZIP2_DEF="-DLIBJTE_WITH_LIBBZ2"
fi


case $host_os in
    cygwin*|mingw*)
        default_libcdio=yes
        ;;
    *)
        default_libcdio=no
        ;;
esac
AH_TEMPLATE([Libburn_use_libcdiO], [Define to use libcdio as system adapter])
AC_ARG_ENABLE(libcdio,
[  --enable-libcdio        Enable use of libcdio as system adapter, default=no (except on MSWindows)],
               , enable_libcdio=$default_libcdio)
if test x$enable_libcdio = xyes; then
dnl Check whether there is libcdio-devel and libcdio-runtime.
dnl If not, erase this macro
    LIBCDIO_DEF="-DLibburn_use_libcdiO"
dnl The empty yes case obviously causes -lcdio to be linked
    AC_CHECK_HEADER(cdio/cdio.h, AC_CHECK_LIB(cdio, mmc_last_cmd_sense, , LIBCDIO_DEF= ), LIBCDIO_DEF= )
else
    LIBCDIO_DEF=
fi
if test x$LIBCDIO_DEF = x
then
    if test x$enable_libcdio = xyes
    then
      echo "WARNING: could not enable use of libcdio as system adapter"
    fi
else
    echo "enabled  use of libcdio as system adapter"
fi
if test x$LIBCDIO_DEF = x-DLibburn_use_libcdiO
then
    AC_DEFINE([Libburn_use_libcdiO], [])
fi


AH_TEMPLATE([Xorriso_allow_external_filterS], [Define to allow xorriso to start external filter processes])
AC_ARG_ENABLE(external-filters,
[  --enable-external-filters   Enable use of external filter programs by xorriso, default=yes],
               , enable_external_filters=yes)
if test x"$enable_external_filters" = xyes; then
    EXTF_DEF="-DXorriso_allow_external_filterS"
    echo "enabled  xorriso external filter programs"
else
    EXTF_DEF=
    echo "disabled xorriso external filter programs"
fi
if test x$EXTF_DEF = x-DXorriso_allow_external_filterS
then
    AC_DEFINE([Xorriso_allow_external_filterS], [])
fi


AH_TEMPLATE([Xorriso_allow_extf_suiD], [Define to allow external filters to xorriso when running under setuid])
AC_ARG_ENABLE(external-filters-setuid,
[  --enable-external-filters-setuid   Enable xorriso external filter programs under setuid, default=no],
               , enable_external_filters_setuid=no)
if test x$enable_external_filters_setuid = xyes; then
    EXTF_SUID_DEF="-DXorriso_allow_extf_suiD"
    echo "enabled  xorriso external filter programs under setuid"
else
    EXTF_SUID_DEF=
    echo "disabled xorriso external filter programs under setuid"
fi
if test x$EXTF_SUID_DEF = x-DXorriso_allow_extf_suiD
then
    AC_DEFINE([Xorriso_allow_extf_suiD], [])
fi

AH_TEMPLATE([Xorriso_allow_launch_frontenD], [Define to allow xorriso command -launch_frontend])
AC_ARG_ENABLE(launch-frontend,
[  --enable-launch-frontend        Enable start of piped frontend program by xorriso, default=yes],
               , enable_launch_frontend=yes)
if test x"$enable_launch_frontend" = xyes; then
    LFRONT_DEF="-DXorriso_allow_launch_frontenD"
    echo "enabled  xorriso command -launch_frontend"
else
    LFRONT_DEF=
    echo "disabled xorriso command -launch_frontend"
fi
if test x$LFRONT_DEF = x-DXorriso_allow_launch_frontenD
then
    AC_DEFINE([Xorriso_allow_launch_frontenD], [])
fi

AH_TEMPLATE([Xorriso_allow_extf_suiD], [Define to allow xorriso command -launch_frontend when running under setuid])
AC_ARG_ENABLE(launch-frontend-setuid,
[  --enable-launch-frontend-setuid   Enable start of piped frontend program under setuid, default=no],
               , enable_launch_frontend_setuid=no)
if test x$enable_launch_frontend_setuid = xyes; then
    LFRONT_SUID_DEF="-DXorriso_allow_extf_suiD"
    echo "enabled  xorriso command -launch_frontend under setuid"
else
    LFRONT_SUID_DEF=
    echo "disabled xorriso command -launch_frontend under setuid"
fi
if test x$LFRONT_SUID_DEF = x-DXorriso_allow_extf_suiD
then
    AC_DEFINE([Xorriso_allow_extf_suiD], [])
fi


AH_TEMPLATE([Xorriso_dvd_obs_default_64K], [Define to make 64 KB default size for DVD/BD writing])
AC_ARG_ENABLE(dvd-obs-64k,
[  --enable-dvd-obs-64k         64 KB default size for DVD/BD writing, default=no],
               , enable_dvd_obs=no)
if test x$enable_dvd_obs_64k = xyes; then
    XORRISO_DVD_OBS_64K="-DXorriso_dvd_obs_default_64K"
    echo "enabled  xorriso write size default 64 KB on DVD and BD"
else
    XORRISO_DVD_OBS_64K=
    echo "disabled xorriso write size default 64 KB on DVD and BD"
fi
if test x$XORRISO_DVD_OBS_64K = x-DXorriso_dvd_obs_default_64K
then
    AC_DEFINE([Xorriso_dvd_obs_default_64K], [])
fi


AH_TEMPLATE([Libburn_read_o_direcT], [Define to use O_DIRECT with -as cdrskin])
AC_ARG_ENABLE(track-src-odirect,
[  --enable-track-src-odirect   Enable use of O_DIRECT with -as cdrskin, default=no],
               , enable_track_src_odirect=no)
if test x$enable_track_src_odirect = xyes; then
    LIBBURN_O_DIRECT_DEF="-DLibburn_read_o_direcT"
    echo "enabled  use of O_DIRECT with input of -as cdrskin"
else
    LIBBURN_O_DIRECT_DEF=
    echo "disabled use of O_DIRECT with input of -as cdrskin"
fi
if test x$LIBBURN_O_DIRECT_DEF = x-DLibburn_read_o_direcT
then
    AC_DEFINE([Libburn_read_o_direcT], [])
fi


# ------- Visible mark in configure : Start of library check

# Check for proper library versions if this is desired.
# (It fails too often on too many systems.)
AC_ARG_ENABLE(pkg-check-modules,
[  --enable-pkg-check-modules      Enable pkg-config check for libburn and libisofs , default=no],
               , enable_pkg_check_modules=no)
if test x$enable_pkg_check_modules = xyes; then

dnl If PKG_CHECK_MODULES is to be used after this if-block,
dnl then it might be necessary to perform PKG_PROG_PKG_CONFIG before the block.

  if test x$LIBCDIO_DEF = x; then
    dummy=dummy
  else
    LIBCDIO_REQUIRED=0.83
    PKG_CHECK_MODULES(LIBCDIO, libcdio >= $LIBCDIO_REQUIRED)
  fi
else
  if test x$LIBCDIO_DEF = x; then
    dummy=dummy
  else
    echo "checking for LIBCDIO... skipped, no --enable-pkg-check-modules"
  fi
fi

# ------- Visible mark in configure : End of library check


AC_CONFIG_FILES([
	Makefile
	version.h
	])
AC_OUTPUT
#	xorriso.pc

