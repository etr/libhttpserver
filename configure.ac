#
#     This file is part of libhttpserver
#     Copyright (C) 2011-2021 Sebastiano Merlino
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
m4_define([libhttpserver_MINOR_VERSION],[19])dnl
m4_define([libhttpserver_REVISION],[0])dnl
m4_define([libhttpserver_PKG_VERSION],[libhttpserver_MAJOR_VERSION.libhttpserver_MINOR_VERSION.libhttpserver_REVISION])dnl
m4_define([libhttpserver_LDF_VERSION],[libhttpserver_MAJOR_VERSION:libhttpserver_MINOR_VERSION:libhttpserver_REVISION])dnl
AC_INIT([libhttpserver], libhttpserver_PKG_VERSION, [electrictwister2000@gmail.com])
AM_INIT_AUTOMAKE([subdir-objects])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_HOST

AX_VALGRIND_DFLT
AX_VALGRIND_CHECK

OLD_CXXFLAGS=$CXXFLAGS
LT_INIT
AC_PROG_CC
AC_PROG_CXX()
AC_PROG_LN_S
CXXFLAGS=$OLD_CXXFLAGS
AC_LANG([C++])
AC_SYS_LARGEFILE

# Minimal feature-set required
AX_CXX_COMPILE_STDCXX([17])

native_srcdir=$srcdir

AC_MSG_CHECKING([whether it is possible to compile in the same directory])
AC_ARG_ENABLE([same-directory-build],
    [AS_HELP_STRING([--enable-same-directory-build],
        [enable to compile in the same directory. This is heavily discouraged. (def=no)])],
    [samedirectory="$enableval"],
    [samedirectory=no])
AC_MSG_RESULT([$samedirectory])

if test x"$samedirectory" = x"no"; then
    if test "`cd $srcdir; /bin/pwd`" = "`/bin/pwd`"; then
        AC_MSG_ERROR("you must configure in a separate build directory")
    fi
fi

is_windows=yes;
NETWORK_LIBS=""
case "$host" in
    *-mingw*)
        NETWORK_HEADER="winsock2.h"
        ADDITIONAL_LIBS="-lpthread -no-undefined"
        NETWORK_LIBS="-lws2_32"
        native_srcdir=$(cd $srcdir; pwd -W)
        ;;
    *-cygwin*)
        NETWORK_HEADER="arpa/inet.h"
        ADDITIONAL_LIBS="-lpthread -no-undefined"
        ;;
    *)
        NETWORK_HEADER="arpa/inet.h"
        ADDITIONAL_LIBS=""
        is_windows=no
        ;;
esac

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADER([stdint.h],[],[AC_MSG_ERROR("stdint.h not found")])
AC_CHECK_HEADER([inttypes.h],[],[AC_MSG_ERROR("inttypes.h not found")])
AC_CHECK_HEADER([errno.h],[],[AC_MSG_ERROR("errno.h not found")])
AC_CHECK_HEADER([unistd.h],[],[AC_MSG_ERROR("unistd.h not found")])
AC_CHECK_HEADER([ctype.h],[],[AC_MSG_ERROR("cctype not found")])
AC_CHECK_HEADER([sys/stat.h],[],[AC_MSG_ERROR("sys/stat.h not found")])
AC_CHECK_HEADER([sys/types.h],[],[AC_MSG_ERROR("sys/types.h not found")])
AC_CHECK_HEADER([$NETWORK_HEADER],[],[AC_MSG_ERROR("$NETWORK_HEADER not found")])
AC_CHECK_HEADER([signal.h],[],[AC_MSG_ERROR("signal.h not found")])

AC_CHECK_HEADER([gnutls/gnutls.h],[have_gnutls="yes"],[AC_MSG_WARN("gnutls/gnutls.h not found. TLS will be disabled"); have_gnutls="no"])

# Checks for libmicrohttpd
if test x"$host" = x"$build"; then
    AC_CHECK_HEADER([microhttpd.h],
        AC_CHECK_LIB([microhttpd], [MHD_get_fdset2],
            [AC_MSG_CHECKING([for libmicrohttpd >= 0.9.64])
                AC_COMPILE_IFELSE(
                    [AC_LANG_SOURCE([
                        #include <microhttpd.h>
                        #if (MHD_VERSION < 0x00096400)
                        #error needs at least version 0.9.64
                        #endif
                        int main () { return 0; }
                    ])],
                [],
                [AC_MSG_ERROR("libmicrohttpd is too old - install libmicrohttpd >= 0.9.64")]
                )
            ],
            [AC_MSG_ERROR(["libmicrohttpd not found"])]
        ),
        [AC_MSG_ERROR(["microhttpd.h not found"])]
    )

    CXXFLAGS="-DHTTPSERVER_COMPILATION -D_REENTRANT $LIBMICROHTTPD_CFLAGS $CXXFLAGS"
    LDFLAGS="$LIBMICROHTTPD_LIBS $NETWORK_LIBS $ADDITIONAL_LIBS $LDFLAGS"

    cond_cross_compile="no"
else
    AC_CHECK_HEADER([microhttpd.h],
        AC_CHECK_LIB([microhttpd], [MHD_get_fdset2],
            [],
            [AC_MSG_ERROR(["libmicrohttpd not found"])]
        ),
        [AC_MSG_ERROR(["microhttpd.h not found"])]
    )

    CXXFLAGS="-DHTTPSERVER_COMPILATION -D_REENTRANT $CXXFLAGS"
    LDFLAGS="$NETWORK_LIBS $ADDITIONAL_LIBS $LDFLAGS"

    cond_cross_compile="yes"
fi

AM_CONDITIONAL([COND_CROSS_COMPILE],[test x"$cond_cross_compile" = x"yes"])
AC_SUBST(COND_CROSS_COMPILE)

AC_MSG_CHECKING([whether to build with TCP_FASTOPEN support])
AC_ARG_ENABLE([fastopen],
    [AS_HELP_STRING([--enable-fastopen],
        [enable use of TCP_FASTOPEN (def=yes)])],
    [fastopen="$enableval"],
    [fastopen=yes])
AC_MSG_RESULT([$fastopen])

is_fastopen_supported=no;
if test x"$fastopen" = x"yes"; then
    if test x"$is_windows" = x"no"; then
        if test `uname -r |cut -d. -f1` -ge 3; then
            if test `uname -r |cut -d. -f2` -ge 7; then
                CXXFLAGS="-DUSE_FASTOPEN $CXXFLAGS";
                is_fastopen_supported=yes;
            fi
        fi
    fi
fi

AC_MSG_CHECKING([whether to link statically])
AC_ARG_ENABLE([static],
    [AS_HELP_STRING([--enable-static],
        [enable use static linking (def=yes)])],
    [static="$enableval"],
    [static=yes])
AC_MSG_RESULT([$static])

if test x"$static" = x"$yes"; then
    LDFLAGS="-static $LDFLAGS";
fi

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

AM_LDFLAGS="-lstdc++"

if test x"$debugit" = x"yes"; then
    AC_DEFINE([DEBUG],[],[Debug Mode])
    AM_CXXFLAGS="$AM_CXXFLAGS -DDEBUG -g -Wall -Wextra -Werror -pedantic -std=c++17 -Wno-unused-command-line-argument -O0"
    AM_CFLAGS="$AM_CXXFLAGS -DDEBUG -g -Wall -Wextra -Werror -pedantic -Wno-unused-command-line-argument -O0"
else
    AC_DEFINE([NDEBUG],[],[No-debug Mode])
    AM_CXXFLAGS="$AM_CXXFLAGS -O3"
    AM_CFLAGS="$AM_CXXFLAGS -O3"
fi

case $host_os in
    darwin* )
        AM_CXXFLAGS="$AM_CXXFLAGS -DDARWIN"
        AM_CFLAGS="$AM_CFLAGS -DDARWIN"
        ;;
    freebsd* )
        AM_LDFLAGS=""
        ;;
esac

AC_MSG_CHECKING([whether to build with coverage information])
AC_ARG_ENABLE([coverage],
    [AS_HELP_STRING([--enable-coverage],
        [enable coverage data generation (def=no)])],
    [coverit="$enableval"],
    [coverit=no])
AC_MSG_RESULT([$coverit])

if test x"$coverit" = x"yes"; then
    case $host_os in
        darwin* )
            echo "Coverage not supported on OSX"
            cond_gcov="no"
            ;;
        *)
            AC_CHECK_PROG(GCOV, gcov, gcov)
            cond_gcov="yes"
            ;;
    esac
fi

AC_ARG_ENABLE([[examples]],
  [AS_HELP_STRING([[--disable-examples]], [do not build any examples])], ,
    [enable_examples=yes])
test "x$enable_examples" = "xno" || enable_examples=yes
AM_CONDITIONAL([BUILD_EXAMPLES], [test "x$enable_examples" = "xyes"])

AM_CONDITIONAL([COND_GCOV],[test x"$cond_gcov" = x"yes"])
AC_SUBST(COND_GCOV)

if test x"have_gnutls" = x"yes"; then
    AM_CXXFLAGS="$AM_CXXFLAGS -DHAVE_GNUTLS"
    AM_CFLAGS="$AM_CXXFLAGS -DHAVE_GNUTLS"
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
AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_LDFLAGS)
AC_SUBST(CPPFLAGS)
AC_SUBST(LIBS)
AC_SUBST(LDFLAGS)
AC_SUBST(EXT_LIB_PATH)
AC_SUBST(EXT_LIBS)

AC_CONFIG_FILES([test/test_content:test/test_content])
AC_CONFIG_FILES([test/test_content_2:test/test_content_2])
AC_CONFIG_FILES([test/test_content_empty:test/test_content_empty])
AC_CONFIG_FILES([test/test_content_large:test/test_content_large])
AC_CONFIG_FILES([test/cert.pem:test/cert.pem])
AC_CONFIG_FILES([test/key.pem:test/key.pem])
AC_CONFIG_FILES([test/test_root_ca.pem:test/test_root_ca.pem])
AC_CONFIG_FILES([test/libhttpserver.supp:test/libhttpserver.supp])
AC_CONFIG_FILES([examples/cert.pem:examples/cert.pem])
AC_CONFIG_FILES([examples/key.pem:examples/key.pem])
AC_CONFIG_FILES([examples/test_content:examples/test_content])

AC_OUTPUT(
      libhttpserver.pc
      Makefile
      doc/Makefile
      src/Makefile
      test/Makefile
      examples/Makefile
)

AC_MSG_NOTICE([Configuration Summary:
  Operating System:  ${host_os}
  Target directory:  ${prefix}
  License         :  LGPL only
  Debug	          :  ${debugit}
  TLS Enabled     :  ${have_gnutls}
  TCP_FASTOPEN    :  ${is_fastopen_supported}
  Static          :  ${static}
  Build examples  :  ${enable_examples}
])
