#
#     This file is part of libhttpserver
#     Copyright (C) 2011 Sebastiano Merlino
#
#     This library is free software; you can redistribute it and/or
#     modify it under the terms of the GNU Lesser General Public
#     License as published by the Free Software Foundation; either
#     version 2.1 of the License, or (at your option) any later version.
#
#     This library is distributed in the hope that it will be useful,
#     but WITHOUT ANY WARRANTY; without even the implied warranty of
#     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#     Lesser General Public License for more details.
#
#     You should have received a copy of the GNU Lesser General Public
#     License along with this library; if not, write to the Free Software
#     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
m4_define([libhttpserver_MAJOR_VERSION],[0])dnl
m4_define([libhttpserver_MINOR_VERSION],[6])dnl
m4_define([libhttpserver_REVISION],[3])dnl
m4_define([libhttpserver_PKG_VERSION],[libhttpserver_MAJOR_VERSION.libhttpserver_MINOR_VERSION.libhttpserver_REVISION])dnl
m4_define([libhttpserver_LDF_VERSION],[libhttpserver_MAJOR_VERSION:libhttpserver_MINOR_VERSION:libhttpserver_REVISION])dnl
AC_INIT([libhttpserver], libhttpserver_PKG_VERSION, [electrictwister2000@gmail.com])
AM_INIT_AUTOMAKE([libhttpserver], libhttpserver_PKG_VERSION)
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

OLD_CXXFLAGS=$CXXFLAGS
AC_PROG_LIBTOOL
AC_PROG_CC
AC_PROG_CXX
AC_PROG_LN_S
CXXFLAGS=$OLD_CXXFLAGS
AC_LANG([C++])
AC_SYS_LARGEFILE

if test "`cd $srcdir; /bin/pwd`" = "`/bin/pwd`"; then 
	AC_MSG_ERROR("you must configure in a separate build directory")
fi

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADER([string],[],[AC_MSG_ERROR("C++ strings not found")])
AC_CHECK_HEADER([vector],[],[AC_MSG_ERROR("C++ vector not found")])
AC_CHECK_HEADER([map],[],[AC_MSG_ERROR("C++ map not found")])
AC_CHECK_HEADER([algorithm],[],[AC_MSG_ERROR("C++ algorithm not found")])
AC_CHECK_HEADER([istream],[],[AC_MSG_ERROR("C++ istream not found")])
AC_CHECK_HEADER([sstream],[],[AC_MSG_ERROR("C++ sstream not found")])
AC_CHECK_HEADER([iostream],[],[AC_MSG_ERROR("C++ iostream not found")])
AC_CHECK_HEADER([stdint.h],[],[AC_MSG_ERROR("stdint.h not found")])
AC_CHECK_HEADER([inttypes.h],[],[AC_MSG_ERROR("inttypes.h not found")])
AC_CHECK_HEADER([stdlib.h],[],[AC_MSG_ERROR("stdlib.h not found")])
AC_CHECK_HEADER([string.h],[],[AC_MSG_ERROR("cstring not found")])
AC_CHECK_HEADER([stdio.h],[],[AC_MSG_ERROR("cstdio not found")])
AC_CHECK_HEADER([errno.h],[],[AC_MSG_ERROR("errno.h not found")])
AC_CHECK_HEADER([unistd.h],[],[AC_MSG_ERROR("unistd.h not found")])
AC_CHECK_HEADER([ctype.h],[],[AC_MSG_ERROR("cctype not found")])
AC_CHECK_HEADER([regex.h],[],[AC_MSG_ERROR("regex.h not found")])
AC_CHECK_HEADER([sys/stat.h],[],[AC_MSG_ERROR("sys/stat.h not found")])
AC_CHECK_HEADER([sys/types.h],[],[AC_MSG_ERROR("sys/types.h not found")])
AC_CHECK_HEADER([arpa/inet.h],[],[AC_MSG_ERROR("arpa/inet.h not found")])
AC_CHECK_HEADER([signal.h],[],[AC_MSG_ERROR("signal.h not found")])

AC_CHECK_HEADER([gnutls/gnutls.h],[have_gnutls="yes"],[AC_MSG_WARN("gnutls/gnutls.h not found. TLS will be disabled"); have_gnutls="no"])

# Checks for libmicrohttpd
PKG_CHECK_MODULES([LIBMICROHTTPD],[libmicrohttpd >= 0.9.7],[],[AC_MSG_ERROR("libmicrohttpd not present or too old - install libmicrohttpd >= 0.9.7")])
#AC_CHECK_HEADER([microhttpd.h],[],[AC_MSG_ERROR("Microhttpd header files not found. Please use a version >= 0.9.9.")])
#AC_CHECK_LIB([microhttpd],[MHD_start_daemon],[],[AC_MSG_ERROR("Microhttpd header files not found. Please use a version >= 0.9.9.")])

CXXFLAGS="-DHTTPSERVER_COMPILATION -D_REENTRANT $LIBMICROHTTPD_CFLAGS $CXXFLAGS"
LDFLAGS="$LIBMICROHTTPD_LIBS $LD_FLAGS"

m4_pattern_allow([AC_TYPE_SIZE_T])
m4_pattern_allow([AC_TYPE_UINT16_T])
m4_pattern_allow([AC_TYPE_UINT32_T])
m4_pattern_allow([AC_TYPE_UINT64_T])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

LHT_LIBDEPS="-lmicrohttpd"

AC_MSG_CHECKING([whether to build with debug information])
AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug],
        [enable debug data generation (def=no)])],
    [debugit="$enableval"],
    [debugit=no])
AC_MSG_RESULT([$debugit])

if test x"$debugit" = x"yes"; then
    AC_DEFINE([DEBUG],[],[Debug Mode])
    AM_CXXFLAGS="$AM_CXXFLAGS -DDEBUG -g -Wall -Wno-uninitialized -O0 -fprofile-arcs -ftest-coverage"
    LDFLAGS="-lgcov $LDFLAGS"
else
    AC_DEFINE([NDEBUG],[],[No-debug Mode])
    AM_CXXFLAGS="$AM_CXXFLAGS -O3"
fi

if test x"have_gnutls" = x"yes"; then
    AM_CXXFLAGS="$AM_CXXFLAGS -DHAVE_GNUTLS"
fi

AC_MSG_CHECKING([whether to use c++0x std classes])
AC_ARG_ENABLE([cpp0x],
              [AS_HELP_STRING([--enable-cpp0x],
              [enable c++0x std classes (def=no)])],
              [cpp0x="$enableval"],
              [cpp0x=no])
AC_MSG_RESULT([$cpp0x])

if test x"$cpp0x" = x"yes"; then
    AC_DEFINE([CPP0X],[],[c++0x Mode])
    AM_CXXFLAGS="$AM_CXXFLAGS -DUSE_CPP_ZEROX -std=c++0x"
else
    AC_DEFINE([NCPP0X],[],[standard Mode])
fi

DX_HTML_FEATURE(ON)
DX_CHM_FEATURE(OFF)
DX_CHI_FEATURE(OFF)
DX_MAN_FEATURE(ON)
DX_RTF_FEATURE(OFF)
DX_XML_FEATURE(OFF)
DX_PDF_FEATURE(OFF)
DX_PS_FEATURE(OFF)
DX_INIT_DOXYGEN([$PACKAGE_NAME],[doxyconfig.in])

LDFLAGS="$LDFLAGS -version-number libhttpserver_LDF_VERSION"

AC_SUBST(LHT_LIBDEPS)
AC_SUBST(AM_CXXFLAGS)
AC_SUBST(CPPFLAGS)
AC_SUBST(LIBS)
AC_SUBST(LDFLAGS)
AC_SUBST(EXT_LIB_PATH)
AC_SUBST(EXT_LIBS)

AC_OUTPUT(
      libhttpserver.pc
      Makefile
      src/Makefile
      test/Makefile
      debian/changelog
      debian/copyright
      debian/control
      debian/libhttpserver-dev.install
      debian/libhttpserver.install
      debian/rules
      redhat/libhttpserver.SPEC
)

AC_MSG_NOTICE([Configuration Summary:  
  Operating System:  ${host_os}
  Target directory:  ${prefix}
  License         :  LGPL only
  Debug	          :  ${debugit}
  TLS Enabled     :  ${have_gnutls}
  C++0x           :  ${cpp0x}
])
