dnl Process this file with autoconf to produce a configure script.  -*- Autoconf
 -*-

dnl This file is a part of Geeqie project (https://www.geeqie.org/).
dnl Copyright (C) 2008 - 2018 The Geeqie Team
dnl
dnl This program is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 2 of the License, or
dnl (at your option) any later version.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.

AC_PREREQ(2.57)
AC_INIT([geeqie], m4_translit(m4_esyscmd([if [ $(git tag --list 'v[1-9]*' --points-at HEAD | wc -c) -gt 0 ]; then git tag --list v[1-9]* --points-at HEAD | tail -n 1 | tr -d 'v' ; else git tag --list v[1-9]* | tail -n 1 | tr -d 'v' && echo "+git" && git log --max-count=1 --date=format:"%Y%m%d" --format="%ad" && echo "-" && git rev-parse --quiet --verify --short HEAD; fi ]), m4_newline), [geeqie@freelists.org], [], [https://www.geeqie.org/])

# Add -Werror to the default CFLAGS
CFLAGS+=" -Werror -Wno-error=deprecated-declarations -Wno-error=sign-compare -Wno-error=return-type"

# Check for rightly dirs
AC_CONFIG_SRCDIR([src/main.c])

AC_CONFIG_AUX_DIR(auxdir)
# Require Automake 1.14 for %reldir% support
AM_INIT_AUTOMAKE([1.14.1 subdir-objects foreign dist-xz no-dist-gzip tar-ustar])

AC_CONFIG_HEADER([config.h])

# Only for developers
AM_MAINTAINER_MODE

# Silent build for automake >= 1.11
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_ARG_ENABLE(developer, [
Development options:
AC_HELP_STRING([--enable-developer], [turn on developers mode [default=no]])],
[
  __IS_DEVELOPER=yes
],
[
if test "x${enable_developer}" != "xyes"
then
  __IS_DEVELOPER=no
else
  __IS_DEVELOPER=yes
fi
])

DX_HTML_FEATURE(ON)
DX_CHM_FEATURE(OFF)
DX_CHI_FEATURE(OFF)
DX_MAN_FEATURE(OFF)
DX_RTF_FEATURE(OFF)
DX_XML_FEATURE(OFF)
DX_PDF_FEATURE(OFF)
DX_PS_FEATURE(OFF)

DX_INIT_DOXYGEN($PACKAGE_NAME, doxygen.conf, doc/doxygen)

#  Debug support
# ----------------------------------------------------------------------

dnl Debugging option
dnl FIXME: official release convert default to 'no'
dnl

AC_ARG_ENABLE([debug-flags], [
Development options:
AC_HELP_STRING([--enable-debug-flags], [use compiler flags for debugging [default=no]])], [],
[
if test "x${enable_developer}" != "xyes"
then
  enable_debug_flags="no"
else
  enable_debug_flags="yes"
fi
])

AC_ARG_ENABLE([debug-log], [
Development options:
AC_HELP_STRING([--enable-debug-log], [enable debugging messages [default=yes]])], [], [enable_debug_log="yes"])

if test "x${enable_debug_flags}" != "xno"
then
  if test "x${enable_developer}" = "xyes"
  then
    CXXFLAGS="${CXXFLAGS} -Wall"
    CFLAGS="${CFLAGS} -Wstrict-prototypes -Wall"
  fi
  __COMMONFLAGS="-g -O0 -Wextra -Wunused-value -Wunused-variable -Wunused-function -Wunused-label -Wcomment -Wmissing-braces -Wparentheses -Wreturn-type -Wswitch -Wstrict-aliasing -Wno-unused-parameter -Wformat -Wformat-security -DGQ_DEBUG_PATH_UTF8=1"
  CXXFLAGS="${CXXFLAGS} ${__COMMONFLAGS}"
  CFLAGS="${CFLAGS} ${__COMMONFLAGS} -Wimplicit-int -Werror-implicit-function-declaration"
  __IS_DEBUG_FLAGS=yes
else
  __IS_DEBUG_FLAGS=no
fi

if test "x${enable_debug_log}" != "xno"
then
  AC_DEFINE(DEBUG,1,[Defined if Geeqie is compiled with debugging messages support])
  __IS_DEBUG_LOG=yes
else
  __IS_DEBUG_LOG=no
fi

AM_CONDITIONAL(DEBUG, test x$enable_debug_flags = xyes)

AC_ARG_ENABLE(deprecated, [
AC_HELP_STRING([--enable-deprecated], [turn off checking of deprecated functions [default=yes]])], [],
[
if test "x${enable_developer}" != "xyes"
then
  enable_deprecated="no"
else
  enable_deprecated="yes"
fi
])

if test "x${enable_deprecated}" != "xno"
then
  CXXFLAGS="${CXXFLAGS} -DGTK_DISABLE_DEPRECATED=1 -DGDK_DISABLE_DEPRECATED=1 -DGDK_PIXBUF_DISABLE_DEPRECATED=1 -DG_DISABLE_DEPRECATED=1"
  CFLAGS="${CFLAGS} -DGTK_DISABLE_DEPRECATED=1 -DGDK_DISABLE_DEPRECATED=1 -DGDK_PIXBUF_DISABLE_DEPRECATED=1 -DG_DISABLE_DEPRECATED=1"
  __IS_DEPRECATED=yes
else
  __IS_DEPRECATED=no
fi


AC_ISC_POSIX
AC_PROG_CC
AC_PROG_CC_C99
AC_PROG_CXX
AC_STDC_HEADERS
AC_ARG_PROGRAM
AC_SYS_LARGEFILE
IT_PROG_INTLTOOL([0.35.0])


dnl checks for functions
AC_CHECK_FUNCS(strverscmp access fsync fflush)


# Check target architecture

# Check for Win32
AC_MSG_CHECKING([for some Win32 platform])
case "$target_os" in
  mingw* | cygwin*)
    platform_win32=yes
    AC_DEFINE(PLATFORM_WIN32, 1, [Build on win32 OS])
    ;;
  *)
    platform_win32=no
    ;;
esac
AC_MSG_RESULT([$platform_win32])
AM_CONDITIONAL(PLATFORM_WIN32, test "$platform_win32" = "yes")

AC_MSG_CHECKING([for native Win32])
case "$target_os" in
  mingw*)
    os_win32=yes
    AC_DEFINE(OS_WIN32, 1, [Build on native win32 OS])
    os_unix=no
    PATHSEP=';'
    ;;
  *)
    os_win32=no
    os_unix=yes
    PATHSEP=':'
    ;;
esac
AC_MSG_RESULT([$os_win32])
AC_SUBST(PATHSEP)
AM_CONDITIONAL(OS_WIN32, test "$os_win32" = "yes")
AM_CONDITIONAL(OS_UNIX, test "$os_unix" = "yes")

if test "$os_win32" = "yes"; then
  AC_CHECK_PROG(ms_librarian, lib.exe, yes, no)
  AC_CHECK_TOOL(WINDRES, windres, :)
else
  WINDRES=":"
fi

AM_CONDITIONAL(MS_LIB_AVAILABLE, test x$ms_librarian = xyes)
AM_CONDITIONAL(HAVE_WINDRES, test "x$WINDRES" != "x:")
AC_SUBST(WINDRES)

AM_CONDITIONAL([EXTERNAL_PREFIX], [test "x$prefix" != "xNONE"])

dnl reasonable guesses for where stuff is installed
if test "x$prefix" = "xNONE"; then
  prefix="/usr/local"
else
  prefix=$prefix
fi

AM_PATH_GLIB_2_0(2.52.0,,AC_MSG_ERROR(GLIB >= 2.52.0 not installed.))
AC_ARG_ENABLE([gtk3], AC_HELP_STRING([--disable-gtk3], [use gtk2 instead of gtk3]),[gtk3="${enableval}"], [gtk3=m4_ifdef([AM_PATH_GTK_3_0], [auto], [no])])

if test x$gtk3 = xyes; then
    AC_MSG_WARN("Gtk3 enabled")
    m4_ifdef([AM_PATH_GTK_3_0], [AM_PATH_GTK_3_0(3.0.0,,AC_MSG_ERROR(GTK+ >= 3.0.0 not installed.))])
else
    if test x$gtk3 != xno; then
       AC_MSG_WARN("Gtk3 is preferred...")
       m4_ifdef([AM_PATH_GTK_3_0], [AM_PATH_GTK_3_0(3.0.0,,[gtk3=no])])
    fi
fi

if test x$gtk3 = xno; then
    m4_ifdef([AM_PATH_GTK_2_0], [AM_PATH_GTK_2_0(2.20.0,,AC_MSG_ERROR(GTK+ >= 2.20.0 not installed.))])
    true
fi

threads="auto"
AC_ARG_ENABLE([threads],
  AC_HELP_STRING([--disable-threads], [disable thread support]), [threads="${enableval}"])

have_gthread="no"
if test "x${threads}" != "xno" ; then
  PKG_CHECK_MODULES(GTHREAD, [gthread-2.0], have_gthread="yes", [AC_MSG_WARN("No thread support in glib")])
fi

if test "x$have_gthread" != "xno"; then
        AC_DEFINE(HAVE_GTHREAD, 1, Define if you have gthread library)
        GLIB_CFLAGS="$GTHREAD_CFLAGS"
        GLIB_LIBS="$GTHREAD_LIBS"
fi


AC_PATH_PROGS(GDK_PIXBUF_CSOURCE, "gdk-pixbuf-csource")
AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

AC_ARG_WITH(readmedir, [  --with-readmedir=DIR    install path for readme files],
            readmedir=$withval, readmedir="$prefix/share/doc/geeqie-$VERSION")
AC_ARG_WITH(htmldir, [  --with-htmldir=DIR      install path for html files],
            htmldir=$withval, htmldir="$readmedir/html")

AC_DEFINE_UNQUOTED(GQ_HELPDIR, "$readmedir", [Location of documentation files])
AC_DEFINE_UNQUOTED(GQ_HTMLDIR, "$htmldir", [Location of html documentation])

AC_PATH_PROG(GNOME_DOC_TOOL, yelp-build)

AC_SUBST(readmedir)
AC_SUBST(htmldir)

eval "eval appdir=${datadir}/${PACKAGE}"
AC_DEFINE_UNQUOTED([GQ_APP_DIR], "$appdir", [Location of application data])
AC_SUBST(appdir)

eval "eval gq_bindir=${prefix}/lib/${PACKAGE}"
AC_DEFINE_UNQUOTED([GQ_BIN_DIR], "$gq_bindir", [Location of helper scripts and executables])
AC_SUBST(gq_bindir)

#  LIRC support
# ----------------------------------------------------------------------

dnl Check for LIRC client support
AC_MSG_CHECKING(if LIRC support is enabled)
lirc=no
AC_ARG_ENABLE([lirc],
  AC_HELP_STRING([--disable-lirc], [disable lirc support (auto)]),
[
if test "x${enableval}" = "xyes" -a "x$GCC" = "xyes"; then
  AC_MSG_RESULT(yes)
  lirc=yes
else
  AC_MSG_RESULT(no)
  lirc=no
fi], AC_MSG_RESULT(no))


AC_ARG_WITH(lirc-prefix,
[  --with-lirc-prefix=PATH Prefix where LIRC is installed],
[
for dir in `echo "$withval" | tr : ' '`; do
  if test -d $dir/lib; then CXXFLAGS="$CXXFLAGS -L$dir/lib"; fi
  if test -d $dir/include; then CXXFLAGS="$CXXFLAGS -I$dir/include"; fi
  done
])

HAVE_LIRC=no
if test "x${lirc}" != "xno" ; then
  AC_CHECK_HEADER(lirc/lirc_client.h,
    [AC_CHECK_LIB(lirc_client,lirc_init,[HAVE_LIRC=yes;LIBS=-llirc_client $LIBS;AC_DEFINE([HAVE_LIRC],[],[Define to 1 if LIRC must be used])],,)],)
fi
AM_CONDITIONAL(HAVE_LIRC, [test "x$HAVE_LIRC" = xyes])


#  LCMS support
# ----------------------------------------------------------------------

AC_ARG_ENABLE([lcms],
  AC_HELP_STRING([--disable-lcms], [disable lcms support]),
    [liblcms=$enableval], [liblcms=auto])

if test "x${liblcms}" != "xno"; then
  PKG_CHECK_MODULES(LCMS, [lcms2 >= 2.0],
    [
      HAVE_LCMS=yes
      AC_DEFINE(HAVE_LCMS, 1, [define to enable use of color profiles with lcms])
      AC_DEFINE(HAVE_LCMS2, 1, [lcms2 is used])
    ],
    [
    PKG_CHECK_MODULES(LCMS, [lcms >= 1.14],
      [
        HAVE_LCMS=yes
        AC_DEFINE(HAVE_LCMS, 1, [define to enable use of color profiles with lcms])
      ],
      [
        HAVE_LCMS=no
        AC_MSG_WARN([$LCMS_PKG_ERRORS])
      ])
    ])
else
  HAVE_LCMS=disabled
fi

AM_CONDITIONAL(HAVE_LCMS, [test "x$HAVE_LCMS" = xyes])
AC_SUBST(LCMS_CFLAGS)
AC_SUBST(LCMS_LIBS)

#  libjpeg support
# ----------------------------------------------------------------------

AC_ARG_ENABLE([jpeg],
  AC_HELP_STRING([--disable-jpeg], [disable direct jpeg support]),
    [libjpeg=$enableval], [libjpeg=auto])

if test "x${libjpeg}" != "xno"; then
  AC_CHECK_LIB(jpeg, jpeg_destroy_decompress,
      HAVE_JPEG=yes
      JPEG_LIBS=-ljpeg
      AC_DEFINE(HAVE_JPEG, 1, [define to enable use of custom jpeg loader]),
      HAVE_JPEG=no)
else
  HAVE_JPEG=disabled
fi

AM_CONDITIONAL(HAVE_JPEG, [test "x$HAVE_JPEG" = xyes])
AC_SUBST(JPEG_CFLAGS)
AC_SUBST(JPEG_LIBS)


#  libtiff support
# ----------------------------------------------------------------------

AC_ARG_ENABLE([tiff],
  AC_HELP_STRING([--disable-tiff], [disable direct tiff support]),
    [libtiff=$enableval], [libtiff=auto])

if test "x${libtiff}" != "xno"; then
  AC_CHECK_LIB(tiff, TIFFClientOpen,
      HAVE_TIFF=yes
      TIFF_LIBS=-ltiff
      AC_DEFINE(HAVE_TIFF, 1, [define to enable use of custom tiff loader]),
      HAVE_TIFF=no)
else
  HAVE_TIFF=disabled
fi

AM_CONDITIONAL(HAVE_TIFF, [test "x$HAVE_TIFF" = xyes])
AC_SUBST(TIFF_CFLAGS)
AC_SUBST(TIFF_LIBS)

#  libraw support
# ----------------------------------------------------------------------

AC_ARG_ENABLE([raw],
  AC_HELP_STRING([--disable-raw], [disable LibRaw support]),
    [libraw=$enableval], [libraw=auto])

if test "x${libraw}" != "xno"; then
  PKG_CHECK_MODULES(RAW, [libraw >= 0.20],
    [
      HAVE_RAW=yes
      AC_DEFINE(HAVE_RAW, 1, [define to enable libraw support])
    ],
    [
      HAVE_RAW=no
      AC_MSG_WARN([$RAW_PKG_ERRORS])
    ])
else
  HAVE_RAW=disabled
fi

AM_CONDITIONAL(HAVE_RAW, [test "x$HAVE_RAW" = xyes])
AC_SUBST(RAW_CFLAGS)
AC_SUBST(RAW_LIBS)

#  libffmpegthumbnailer support
# ----------------------------------------------------------------------

AC_ARG_ENABLE([ffmpegthumbnailer],
  AC_HELP_STRING([--disable-ffmpegthumbnailer], [disable ffmpegthumbnailer support for generating thumbnails of video files]),
    [ffmpegthumbnailer=$enableval], [ffmpegthumbnailer=auto])

if test "x${ffmpegthumbnailer}" != "xno"; then
  PKG_CHECK_MODULES(FFMPEGTHUMBNAILER, [libffmpegthumbnailer >= 2.1.0],
    [
      HAVE_FFMPEGTHUMBNAILER=yes
      AC_DEFINE(HAVE_FFMPEGTHUMBNAILER, 1, [define to enable ffmpegthumbnailer support])
      AC_CHECK_MEMBER([video_thumbnailer.prefer_embedded_metadata], [AC_DEFINE(HAVE_FFMPEGTHUMBNAILER_METADATA, 1, [define if ffmpegthumbnailer supports embedded metadata])], [], [[#include <libffmpegthumbnailer/videothumbnailerc.h>]])
      AC_CHECK_MEMBER([image_data.image_data_width], [AC_DEFINE(HAVE_FFMPEGTHUMBNAILER_RGB, 1, [define if ffmpegthumbnailer supports raw RGB output])], [], [[#include <libffmpegthumbnailer/videothumbnailerc.h>]])
      AC_CHECK_LIB([ffmpegthumbnailer], [video_thumbnailer_set_size], [AC_DEFINE(HAVE_FFMPEGTHUMBNAILER_WH, 1, [define if ffmpegthumbnailer supports specifying size by width/height])])
    ],
    [
      HAVE_FFMPEGTHUMBNAILER=no
    ])
else
  HAVE_FFMPEGTHUMBNAILER=disabled
fi

AM_CONDITIONAL(HAVE_FFMPEGTHUMBNAILER, [test "x$HAVE_FFMPEGTHUMBNAILER" = xyes])
AC_SUBST(FFMPEGTHUMBNAILER_CFLAGS)
AC_SUBST(FFMPEGTHUMBNAILER_LIBS)

#  Exiv2 support
# ----------------------------------------------------------------------

AC_ARG_ENABLE([exiv2],
  AC_HELP_STRING([--disable-exiv2], [disable exiv2 support]),
    [libexiv2=$enableval], [libexiv2=auto])

if test "x${libexiv2}" != "xno"; then
  PKG_CHECK_MODULES(EXIV2, [exiv2 >= 0.11],
    [
      HAVE_EXIV2=yes
      AC_DEFINE(HAVE_EXIV2, 1, [define to enable exiv2 support])
    ],
    [
      HAVE_EXIV2=no
      AC_MSG_WARN([$EXIV2_PKG_ERRORS])
    ])
else
  HAVE_EXIV2=disabled
fi

AM_CONDITIONAL(HAVE_EXIV2, [test "x$HAVE_EXIV2" = xyes])
AC_SUBST(EXIV2_CFLAGS)
AC_SUBST(EXIV2_LIBS)


#  Gettext support
# ----------------------------------------------------------------------

dnl Set of available languages
ALL_LINGUAS="`grep -v '^#' "$srcdir/po/LINGUAS" | tr '\n' ' '`"
AC_SUBST([CONFIG_STATUS_DEPENDENCIES],['$(top_srcdir)/po/LINGUAS'])

GETTEXT_PACKAGE=$PACKAGE
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["${GETTEXT_PACKAGE}"],[Name of gettext file])
AM_GLIB_GNU_GETTEXT
AM_GLIB_DEFINE_LOCALEDIR(GQ_LOCALEDIR)

AC_SUBST(CFLAGS)
AC_SUBST(CXXFLAGS)
AC_SUBST(CPPFLAGS)
AC_SUBST(LDFLAGS)


#  clutter and champlain support
# ----------------------------------------------------------------------

AC_ARG_ENABLE([map],
  AC_HELP_STRING([--disable-map], [disable map support]),
    [libgps=$enableval], [libgps=auto])

AC_ARG_ENABLE([gpu-accel],
  AC_HELP_STRING([--disable-gpu-accel], [disable GPU acceleration support - experimental]),
    [libclutter=$enableval], [libclutter=auto])

if test "x${libclutter}" != "xno" -a "x${gtk3}" != "xno"; then
  PKG_CHECK_MODULES(CLUTTER, [clutter-1.0 >= 1.0],
    [
      PKG_CHECK_MODULES(CLUTTER_GTK, [clutter-gtk-1.0 >= 1.0],
        [
          HAVE_CLUTTER=yes
          AC_DEFINE(HAVE_CLUTTER, 1, [define to enable use of clutter library])
        ],
        [
          HAVE_CLUTTER=no
          AC_MSG_WARN([$CLUTTER_GTK_PKG_ERRORS])
        ])
    ],
    [
      HAVE_CLUTTER=no
      AC_MSG_WARN([$CLUTTER_PKG_ERRORS])
    ])
else
  HAVE_CLUTTER=disabled
fi

AC_SUBST(CLUTTER_CFLAGS)
AC_SUBST(CLUTTER_LIBS)
AC_SUBST(CLUTTER_GTK_CFLAGS)
AC_SUBST(CLUTTER_GTK_LIBS)


#  Libchamplain support - used for map facility
# ----------------------------------------------------------------------

if test "x${libgps}" != "xno" -a "x${HAVE_CLUTTER}" = "xyes"; then
    if test "x${gtk3}" != "xno"; then
      PKG_CHECK_MODULES(LIBCHAMPLAIN, [champlain-0.12 >= 0.12],
        [
          HAVE_LIBCHAMPLAIN=yes
          AC_DEFINE(HAVE_LIBCHAMPLAIN, 1, [define to enable use of maps])
        ],
        [
          HAVE_LIBCHAMPLAIN=no
          AC_MSG_WARN([$LIBCHAMPLAIN_PKG_ERRORS])
        ])
    else
      HAVE_LIBCHAMPLAIN=disabled
    fi
else
  HAVE_LIBCHAMPLAIN=disabled
fi

if test "x${libgps}" != "xno" -a "x${HAVE_LIBCHAMPLAIN}" = "xyes" ; then
  PKG_CHECK_MODULES(LIBCHAMPLAIN_GTK, [champlain-gtk-0.12 >= 0.12],
    [
      HAVE_LIBCHAMPLAIN_GTK=yes
      AC_DEFINE(HAVE_LIBCHAMPLAIN_GTK, 1, [define to enable use of maps])
    ],
    [
      HAVE_LIBCHAMPLAIN_GTK=no
      AC_MSG_WARN([$LIBCHAMPLAIN_GTK_PKG_ERRORS])
    ])
else
  HAVE_LIBCHAMPLAIN_GTK=disabled
fi

AM_CONDITIONAL(HAVE_LIBCHAMPLAIN_GTK, [test "x$HAVE_LIBCHAMPLAIN_GTK" = xyes])
AC_SUBST(LIBCHAMPLAIN_GTK_CFLAGS)
AC_SUBST(LIBCHAMPLAIN_GTK_LIBS)

#  Lua support
# ----------------------------------------------------------------------

AC_ARG_ENABLE([lua],
  AC_HELP_STRING([--disable-lua], [disable lua support]),
    [liblua=$enableval], [liblua=auto])

if test "x${liblua}" != "xno"; then
  PKG_CHECK_MODULES(LUA, lua5.3 >= 5.3,
    [
      HAVE_LUA=yes
      AC_DEFINE(HAVE_LUA, 1, [define to enable lua support])
    ],
    [
     PKG_CHECK_MODULES(LUA, lua5.1 >= 5.1,
       [
	 HAVE_LUA=yes
	 AC_DEFINE(HAVE_LUA, 1, [define to enable lua support])
       ],
       [
	 PKG_CHECK_MODULES(LUA, lua >= 5.1,
	    [
	      HAVE_LUA=yes
	      AC_DEFINE(HAVE_LUA, 1, [define to enable lua support])
	    ],
	    [
	       HAVE_LUA=no
	       AC_MSG_WARN([$LUA_PKG_ERRORS])
	    ])
       ])
     ])
else
  HAVE_LUA=disabled
fi

AM_CONDITIONAL(HAVE_LUA, [test "x$HAVE_LUA" = xyes])
AC_SUBST(LUA_CFLAGS)
AC_SUBST(LUA_LIBS)

#  Pdf support
# ----------------------------------------------------------------------

if test "x${gtk3}" != "xno"; then
    AC_ARG_ENABLE([pdf],
      AC_HELP_STRING([--disable-pdf], [disable pdf support]),
        [libpdf=$enableval], [libpdf=auto])

    if test "x${libpdf}" != "xno"; then
      PKG_CHECK_MODULES(PDF, poppler-glib >= 0.62,
        [
          HAVE_PDF=yes
          AC_DEFINE(HAVE_PDF, 1, [define to enable pdf support])
        ],
        [
          HAVE_PDF=no
          AC_MSG_WARN([$PDF_PKG_ERRORS])
        ])
    else
        HAVE_PDF=disabled
    fi
else
    HAVE_PDF=disabled
fi

AM_CONDITIONAL(HAVE_PDF, [test "x$HAVE_PDF" = xyes])
AC_SUBST(PDF_CFLAGS)
AC_SUBST(PDF_LIBS)

#  Heif support
# ----------------------------------------------------------------------

AC_ARG_ENABLE([heif],
  AC_HELP_STRING([--disable-heif], [disable heif support]),
    [libheif=$enableval], [libheif=auto])

if test "x${libheif}" != "xno"; then
  PKG_CHECK_MODULES(HEIF, libheif >= 1.3.2,
    [
      HAVE_HEIF=yes
      AC_DEFINE(HAVE_HEIF, 1, [define to enable heif support])
    ],
    [
      HAVE_HEIF=no
      AC_MSG_WARN([$HEIF_PKG_ERRORS])
    ])
else
    HAVE_HEIF=disabled
fi

AM_CONDITIONAL(HAVE_HEIF, [test "x$HAVE_HEIF" = xyes])
AC_SUBST(HEIF_CFLAGS)
AC_SUBST(HEIF_LIBS)

#  WebP support
# ----------------------------------------------------------------------

AC_ARG_ENABLE([webp],
  AC_HELP_STRING([--disable-webp], [disable webp support]),
    [libwebp=$enableval], [libwebp=auto])

if test "x${libwebp}" != "xno"; then
  PKG_CHECK_MODULES(WEBP, libwebp >= 0.6.1,
    [
      HAVE_WEBP=yes
      AC_DEFINE(HAVE_WEBP, 1, [define to enable webp support])
    ],
    [
      HAVE_WEBP=no
      AC_MSG_WARN([$WEBP_PKG_ERRORS])
    ])
else
    HAVE_WEBP=disabled
fi

AM_CONDITIONAL(HAVE_WEBP, [test "x$HAVE_WEBP" = xyes])
AC_SUBST(WEBP_CFLAGS)
AC_SUBST(WEBP_LIBS)

#  Libarchive support
# ----------------------------------------------------------------------

AC_ARG_ENABLE([archive],
  AC_HELP_STRING([--disable-archive], [disable archive support]),
    [libarchive=$enableval], [libarchive=auto])

if test "x${libarchive}" != "xno"; then
  PKG_CHECK_MODULES(ARCHIVE, libarchive >= 3.4.0,
    [
      HAVE_ARCHIVE=yes
      AC_DEFINE(HAVE_ARCHIVE, 1, [define to enable archive support])
    ],
    [
      HAVE_ARCHIVE=no
      AC_MSG_WARN([$ARCHIVE_PKG_ERRORS])
    ])
else
    HAVE_ARCHIVE=disabled
fi

AM_CONDITIONAL(HAVE_ARCHIVE, [test "x$HAVE_ARCHIVE" = xyes])
AC_SUBST(ARCHIVE_CFLAGS)
AC_SUBST(ARCHIVE_LIBS)

#  J2K support
# ----------------------------------------------------------------------

AC_ARG_ENABLE([j2k],
  AC_HELP_STRING([--disable-j2k], [disable j2k support]),
    [libopenjp2=$enableval], [libopenjp2=auto])

if test "x${libopenjp2}" != "xno"; then
  PKG_CHECK_MODULES(J2K, libopenjp2 >= 2.3.0,
    [
      HAVE_J2K=yes
      AC_DEFINE(HAVE_J2K, 1, [define to enable j2k support])
    ],
    [
      HAVE_J2K=no
      AC_MSG_WARN([$J2K_PKG_ERRORS])
    ])
else
    HAVE_J2K=disabled
fi

AM_CONDITIONAL(HAVE_J2K, [test "x$HAVE_J2K" = xyes])
AC_SUBST(J2K_CFLAGS)
AC_SUBST(J2K_LIBS)

#  DjVu support
# ----------------------------------------------------------------------

AC_ARG_ENABLE([djvu],
  AC_HELP_STRING([--disable-djvu], [disable djvu support]),
    [libdjvulibre=$enableval], [libdjvulibre=auto])

if test "x${libdjvulibre}" != "xno"; then
  PKG_CHECK_MODULES(DJVU, ddjvuapi >= 3.5.27,
    [
      HAVE_DJVU=yes
      AC_DEFINE(HAVE_DJVU, 1, [define to enable DjVu support])
    ],
    [
      HAVE_DJVU=no
      AC_MSG_WARN([$DJVU_PKG_ERRORS])
    ])
else
    HAVE_DJVU=disabled
fi

AM_CONDITIONAL(HAVE_DJVU, [test "x$HAVE_DJVU" = xyes])
AC_SUBST(DJVU_CFLAGS)
AC_SUBST(DJVU_LIBS)

#  JPEG XL support
# ----------------------------------------------------------------------

AC_ARG_ENABLE([jpegxl],
  AC_HELP_STRING([--disable-jpegxl], [disable jpeg xl support]),
    [libjxl=$enableval], [libjxl=auto])

if test "x${libjxl}" != "xno"; then
  PKG_CHECK_MODULES(JPEGXL, libjxl >= 0.3.7,
    [
      HAVE_JPEGXL=yes
      AC_DEFINE(HAVE_JPEGXL, 1, [define to enable JPEG XL support])
    ],
    [
      HAVE_JPEGXL=no
      AC_MSG_WARN([$JPEGXL_PKG_ERRORS])
    ])
else
    HAVE_JPEGXL=disabled
fi

AM_CONDITIONAL(HAVE_JPEGXL, [test "x$HAVE_JPEGXL" = xyes])
AC_SUBST(JPEGXL_CFLAGS)
AC_SUBST(JPEGXL_LIBS)

#  Markdown support
# ----------------------------------------------------------------------

AM_CONDITIONAL(HAVE_MARKDOWN, [ "$(command -v markdown)" ])

# _NL_TIME_FIRST_WEEKDAY support
# note that it is an enum and not a define
# ----------------------------------------------------------------------

AC_MSG_CHECKING([for _NL_TIME_FIRST_WEEKDAY])
AC_TRY_LINK([#include <langinfo.h>], [
char c;
c = *((unsigned char *)  nl_langinfo(_NL_TIME_FIRST_WEEKDAY));
], nl_ok=yes, nl_ok=no)
AC_MSG_RESULT($nl_ok)
if test "$nl_ok" = "yes"; then
  AC_DEFINE([HAVE__NL_TIME_FIRST_WEEKDAY], [1],
      [Define if _NL_TIME_FIRST_WEEKDAY is available])
fi

# ----------------------------------------------------------------------

AH_TOP([
/** \file
 * \short autogenerated definition by autoheader.
 * \author Bruclik
 */

/*
 *  This file is a part of Geeqie project (https://www.geeqie.org/).
 *  Copyright (C) 2008 - 2016 The Geeqie Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef _INCLUDE_CONFIG_H
#define _INCLUDE_CONFIG_H
])

AH_BOTTOM([#endif])

dnl Write the output
dnl

AC_CONFIG_FILES([
    Makefile
    src/Makefile
    src/icons/Makefile
    src/icons/svg/Makefile
    po/Makefile.in
    doc/Makefile
    plugins/Makefile
    plugins/symlink/Makefile
    plugins/rotate/Makefile
    plugins/ufraw/Makefile
    plugins/geocode-parameters/Makefile
    plugins/export-jpeg/Makefile
    plugins/tethered-photography/Makefile
    plugins/camera-import/Makefile
    plugins/image-crop/Makefile
    plugins/random-image/Makefile
    plugins/lens/Makefile
    geeqie.spec
])

AC_OUTPUT
dnl Print the results
dnl

cat > config.report << END

     Config results:
    -=-=-=-=-=-=-=-=-

Package:
  Name:          $PACKAGE_NAME
  Version:       $PACKAGE_VERSION

Architecture:
  UNIX:          $os_unix
  Win32:         $platform_win32 (native: $os_win32)

Flags:
  Geeqie:        $GQ_CFLAGS
  DEFS:          $DEFS
  CPPFLAGS:      $__CPPFLAGS
  CFLAGS:        $CFLAGS
  CXXFLAGS:      $CXXFLAGS
  Gtk:           $GTK_CFLAGS
  Glib:          $GLIB_CFLAGS
  Thread:        $GTHREAD_LIBS
  Others:	 $JPEG_LIBS $TIFF_LIBS $LCMS_LIBS $EXIV2_LIBS $CLUTTER_LIBS $CLUTTER_GTK_LIBS $LIBCHAMPLAIN_LIBS $LIBCHAMPLAIN_GTK_LIBS $LUA_LIBS $RAW_LIBS $JPEGXL_LIBS

Localization:
  NLS support:   $USE_NLS
  LINGUAS:       $LINGUAS

Settings:
  Developer:     $__IS_DEVELOPER
  Debug flags:   $__IS_DEBUG_FLAGS
  Debug log:     $__IS_DEBUG_LOG
  Deprecated:    $__IS_DEPRECATED

Support:
  LCMS:          $HAVE_LCMS
  Exiv2:	 $HAVE_EXIV2
  Lirc:          $HAVE_LIRC
  Clutter:       $HAVE_CLUTTER
  Libchamplain:		$HAVE_LIBCHAMPLAIN
  Libchamplain-gtk:	$HAVE_LIBCHAMPLAIN_GTK
  Lua:	         $HAVE_LUA
  FFmpegthumbnailer:	$HAVE_FFMPEGTHUMBNAILER
  Pdf:	         $HAVE_PDF
  HEIF:	         $HAVE_HEIF
  WebP:	         $HAVE_WEBP
  DjVu:	         $HAVE_DJVU
  J2K:	         $HAVE_J2K
  LibRaw:        $HAVE_RAW
  Libjxl:        $HAVE_JPEGXL
  Libarchive:    $HAVE_ARCHIVE

Documentation:
  Doxygen:       $DX_DOXYGEN
  doc-tool:      $GNOME_DOC_TOOL

END

cat config.report
cat <<EOF

  Now you can type "make" to build Geeqie
  (or you take blue pill and the story ends :)

EOF
