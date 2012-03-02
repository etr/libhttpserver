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
AC_INIT([libhttpserver], [0.1.1], [electrictwister2000@gmail.com])
AM_INIT_AUTOMAKE([libhttpserver], [0.1.1])
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_PROG_LIBTOOL
AC_PROG_CC
#AM_CONDITIONAL([XXX], [true])

# Checks for programs.
AC_PROG_CXX

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADER([microhttpd.h],[],
	[
	 AC_MSG_ERROR("Microhttpd header files not found")
	]
)


# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UINT16_T

# Checks for library functions.

LHT_LIBDEPS="-lmicrohttpd"

AC_ARG_WITH([python],
     [  --with-python    Compile PYTHON libraries],
     [python=true])

AC_ARG_WITH([php],
     [  --with-php    Compile PHP libraries],
     [php=true])

AC_ARG_WITH([ruby],
	 [  --with-ruby   Compile RUBY libraries],
	 [ruby=true])

AC_ARG_WITH([perl],
	 [  --with-perl   Compile PERL libraries],
	 [perl=true])

AC_ARG_WITH([lua],
	 [  --with-lua   Compile LUA libraries],
	 [lua=true])

AC_ARG_WITH(lua-include-dir,
            [  --with-lua-include-dir=DIR        where to look for lua headers],
            [   
                luahead=""
                if test -d "$withval"
                then
                        luahead="$withval"
                fi
           ])

AC_ARG_WITH(lua-lib-dir,
            [  --with-lua-lib-dir=DIR        where to look for lua libs],
            [   
                lualib=""
                if test -d "$withval"
                then
                        lualib="$withval"
                fi
           ])

AC_ARG_WITH([java],
     [  --with-java    Compile JAVA libraries],
     [java=true])

AC_ARG_WITH(java-dir,
            [  --with-java-dir=DIR        where to look for jni headers and libs. We search for jni.h in 'java-dir/include/'],
            [   
                javaloc=""
                if test -d "$withval"
                then
                        javaloc="$withval"
                fi
           ])

AM_CONDITIONAL([PYTHON], [test x$python = xtrue])
AM_CONDITIONAL([JAVA], [test x$java = xtrue])
AM_CONDITIONAL([PHP], [test x$php = xtrue])
AM_CONDITIONAL([PERL], [test x$perl = xtrue])
AM_CONDITIONAL([LUA], [test x$lua = xtrue])
AM_CONDITIONAL([RUBY], [test x$ruby = xtrue])

LANGUAGES="C++"

if test x$python = xtrue; then
	AC_CHECK_PROG([PYTHONCONFIG],[python-config],["php-config"], ["no"])
	if test "$PYTHONCONFIG" = "no"
	then
		AC_MSG_ERROR(python-config not found)
	fi
	CXXFLAGS="$CXXFLAGS `python-config --includes` -DWITH_PYTHON"
	LDFLAGS="$LDFLAGS -L`python-config --prefix`/lib"
	LIBS="$LIBS `python-config --libs`"
	AC_SUBST_FILE([PYTHON_AUTOGENERATION])
	PYTHON_AUTOGENERATION=src/autogen_helpers/support_command
	LANGUAGES="$LANGUAGES Python"
fi 

if test x$java = xtrue; then

	found=""
	for loc in $javaloc /usr /usr/local
	do

		if test "x$found" = "x" 
		then
			AC_MSG_CHECKING([for jni in])
			AC_MSG_RESULT($loc)
			AC_CHECK_FILE("$loc/include/jni.h", 
			      [CXXFLAGS="$CXXFLAGS -I$loc/include -I$loc/include/linux"; LDFLAGS="$LDFLAGS";
			       LIBS="$LIBS"; found=1],[])

		fi  

	done
	if test "x$found" = "x" 
	then
		AC_MSG_ERROR([Unable to find jni headers. Please provide a --with-java-dir=<dir> location])
	fi
	AC_SUBST_FILE([JAVA_AUTOGENERATION])
	JAVA_AUTOGENERATION=src/autogen_helpers/support_command
	LANGUAGES="$LANGUAGES Java"
fi 

if test x$php = xtrue; then
	AC_CHECK_PROG([PHPCONFIG],[php-config],["php-config"], ["no"])
	if test "$PHPCONFIG" = "no"
	then
		AC_MSG_ERROR(php-config not found)
	fi
	CXXFLAGS="$CXXFLAGS `php-config --includes`"
	LDFLAGS="$LDFLAGS -L`php-config --prefix`/lib"
	AC_SUBST_FILE([PHP_AUTOGENERATION])
	PHP_AUTOGENERATION=src/autogen_helpers/support_command
	LANGUAGES="$LANGUAGES PHP"
fi 

if test x$lua = xtrue; then
	found=""
	for loc in $luahead /usr/include /usr/local/include
	do

		if test "x$found" = "x" 
		then
			AC_MSG_CHECKING([for lua.h in])
			AC_MSG_RESULT($loc)
			AC_CHECK_FILE("$loc/lua.h",
			      [CXXFLAGS="$CXXFLAGS -I$loc";
			       found=1],[])

		fi  
	done
	if test "x$found" = "x" 
	then
		AC_MSG_ERROR([Unable to find lua headers. Please provide a --with-lua-include-dir=<dir> location])
	fi

	found=""
	for loc in $lualib /usr/lib /usr/local/lib
	do

		if test "x$found" = "x" 
		then
			AC_MSG_CHECKING([for liblua.so in])
			AC_MSG_RESULT($loc)
			AC_CHECK_FILE("$loc/liblua.so",
			      [LDFLAGS="$LDFLAGS -L$loc";
			       found=1],[])

		fi  
	done
	if test "x$found" = "x" 
	then
		AC_MSG_ERROR([Unable to find lua libs. Please provide a --with-lua-lib-dir=<dir> location])
	fi
	AC_SUBST_FILE([LUA_AUTOGENERATION])
	LUA_AUTOGENERATION=src/autogen_helpers/support_command
	LANGUAGES="$LANGUAGES LUA"
fi

if test x$perl = xtrue; then
	AC_CHECK_PROG([PERLINT],[perl],["perl"], ["no"])
	if test "$PERLINT" = "no"
	then
		AC_MSG_ERROR(perl interpreter not found)
	fi
	CXXFLAGS="$CXXFLAGS -I`perl -e 'use Config; print $Config{archlib};'`/CORE -Dbool=char"
	AC_SUBST_FILE([PERL_AUTOGENERATION])
	PERL_AUTOGENERATION=src/autogen_helpers/support_command
	LANGUAGES="$LANGUAGES Perl"
fi

if test x$ruby = xtrue; then
	AC_CHECK_PROG([RUBYINT],[ruby],["ruby"], ["no"])
	if test "$RUBYINT" = "no"
	then
		AC_MSG_ERROR(ruby interpreter not found)
	fi
	CXXFLAGS="$CXXFLAGS -I`ruby -e 'puts $:.join(\" -I\")'`"
	AC_SUBST_FILE([RUBY_AUTOGENERATION])
	RUBY_AUTOGENERATION=src/autogen_helpers/support_command
	LANGUAGES="$LANGUAGES Ruby"
fi

AC_SUBST(LHT_LIBDEPS)
AC_SUBST(CPPFLAGS)
AC_SUBST(LIBS)
AC_SUBST(LDFLAGS)
AC_SUBST(EXT_LIB_PATH)
AC_SUBST(EXT_LIBS)

AC_OUTPUT(
	  libhttpserver.pc Makefile src/Makefile src/python/Makefile src/java/Makefile src/php/Makefile src/lua/Makefile src/perl/Makefile src/ruby/Makefile
)

AC_MSG_NOTICE([Configuration Summary:  
  Operating System:  ${host_os}
  Target directory:  ${prefix}
  License         :  LGPL only
  Languages       :  ${LANGUAGES}
])
