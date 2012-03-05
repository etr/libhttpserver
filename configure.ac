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
     [python=true; swig=true])

AC_ARG_WITH([php],
     [  --with-php    Compile PHP libraries],
     [php=true; swig=true])

AC_ARG_WITH([ruby],
	 [  --with-ruby   Compile RUBY libraries],
	 [ruby=true; swig=true])

AC_ARG_WITH([perl],
	 [  --with-perl   Compile PERL libraries],
	 [perl=true; swig=true])

AC_ARG_WITH([lua],
	 [  --with-lua   Compile LUA libraries],
	 [lua=true; swig=true])

AC_ARG_WITH(lua-include-dir,
            [  --with-lua-include-dir=DIR        where to look for lua headers],
            [   
                luahead=""
                if test -d "$withval"
                then
                        luahead="$withval"
                fi
           ])

AC_ARG_WITH([java],
     [  --with-java    Compile JAVA libraries],
     [java=true; swig=true])

AC_ARG_WITH(java-dir,
            [  --with-java-dir=DIR        where to look for jni headers and libs. We search for jni.h in 'java-dir/include/'],
            [   
                javaloc=""
                if test -d "$withval"
                then
                        javaloc="$withval"
                fi
           ])

AC_ARG_WITH([guile],
     [  --with-guile    Compile GUILE libraries],
     [guile=true; swig=true])

AC_ARG_WITH(doc,
            [  --with-doc   Compile DOC],
            [doc=true])

AM_CONDITIONAL([PYTHON], [test x$python = xtrue])
AM_CONDITIONAL([JAVA], [test x$java = xtrue])
AM_CONDITIONAL([PHP], [test x$php = xtrue])
AM_CONDITIONAL([PERL], [test x$perl = xtrue])
AM_CONDITIONAL([LUA], [test x$lua = xtrue])
AM_CONDITIONAL([RUBY], [test x$ruby = xtrue])
AM_CONDITIONAL([GUILE], [test x$guile = xtrue])
AM_CONDITIONAL([DOC], [test x$doc = xtrue])

LANGUAGES="C++"

if test x$swig = xtrue; then
	AC_CHECK_PROG([SWIG], [swig], ["yes"], ["no"])
	if test "SWIG" = "no"
	then
		AC_MSG_ERROR(swig not found)
	fi
fi

if test x$python = xtrue; then
	AC_CHECK_PROG([PYTHONCONFIG],[python-config],["yes"], ["no"])
	if test "$PYTHONCONFIG" = "no"
	then
		AC_MSG_ERROR(python-config not found)
	fi
    PYINCLUDES="`python-config --includes`"
    PYDEFINES="-DWITHPYTHON"
	PYLDFLAGS="$LDFLAGS -L`python-config --prefix`/lib"
	PYLIBS="$LIBS `python-config --libs`"
    AC_SUBST(PYINCLUDES)
    AC_SUBST(PYDEFINES)
    AC_SUBST(PYLDFLAGS)
    AC_SUBST(PYLIBS)
	AC_SUBST_FILE([PYTHON_AUTOGENERATION])
	PYTHON_AUTOGENERATION=$srcdir/src/autogen_helpers/support_command
	LANGUAGES="$LANGUAGES Python"
fi 

if test x$java = xtrue; then
    JAVAINCLUDES=""
    JAVALDFLAGS="$LDFLAGS"
    JAVALIBS="$LIBS"
	found=""
	for loc in $javaloc /usr /usr/local
	do

		if test "x$found" = "x" 
		then
			AC_MSG_CHECKING([for jni in])
			AC_MSG_RESULT($loc)
			AC_CHECK_FILE("$loc/include/jni.h", 
			      [JAVAINCLUDES="$CXXFLAGS -I$loc/include -I$loc/include/linux"; found=1],[])

		fi  

	done
	if test "x$found" = "x" 
	then
		AC_MSG_ERROR([Unable to find jni headers. Please provide a --with-java-dir=<dir> location])
	fi
    AC_SUBST(JAVAINCLUDES)
    AC_SUBST(JAVALDFLAGS)
    AC_SUBST(JABALIBS)
	AC_SUBST_FILE([JAVA_AUTOGENERATION])
	JAVA_AUTOGENERATION=$srcdir/src/autogen_helpers/support_command
	LANGUAGES="$LANGUAGES Java"
fi 

if test x$php = xtrue; then
	AC_CHECK_PROG([PHPCONFIG],[php-config],["yes"], ["no"])
	if test "$PHPCONFIG" = "no"
	then
		AC_MSG_ERROR(php-config not found)
	fi
	PHPINCLUDES="`php-config --includes`"
	PHPLDFLAGS="$LDFLAGS -L`php-config --prefix`/lib"
    AC_SUBST(PHPINCLUDES)
    AC_SUBST(PHPLDFLAGS)
	AC_SUBST_FILE([PHP_AUTOGENERATION])
	PHP_AUTOGENERATION=$srcdir/src/autogen_helpers/support_command
	LANGUAGES="$LANGUAGES PHP"
fi 

if test x$lua = xtrue; then
    LUAINCLUDES=""
	found=""
	for loc in $luahead /usr/include /usr/local/include
	do

		if test "x$found" = "x" 
		then
			AC_MSG_CHECKING([for lua.h in])
			AC_MSG_RESULT($loc)
			AC_CHECK_FILE("$loc/lua.h",
			      [LUAINCLUDES="$LUACXXFLAGS -I$loc";
			       found=1],[])

		fi  
	done
	if test "x$found" = "x" 
	then
		AC_MSG_ERROR([Unable to find lua headers. Please provide a --with-lua-include-dir=<dir> location])
	fi
    AC_SUBST(LUAINCLUDES)
	AC_SUBST_FILE([LUA_AUTOGENERATION])
	LUA_AUTOGENERATION=$srcdir/src/autogen_helpers/support_command
	LANGUAGES="$LANGUAGES LUA"
fi

if test x$perl = xtrue; then
	AC_CHECK_PROG([PERLINT],[perl],["yes"], ["no"])
	if test "$PERLINT" = "no"
	then
		AC_MSG_ERROR(perl interpreter not found)
	fi
	PERLINCLUDES="$PERLINCLUDES -I`perl -e 'use Config; print $Config{archlib};'`/CORE"
    PERLDEFINES="-D_GNU_SOURCE"
    AC_SUBST(PERLINCLUDES)
    AC_SUBST(PERLDEFINES)
	AC_SUBST_FILE([PERL_AUTOGENERATION])
	PERL_AUTOGENERATION=$srcdir/src/autogen_helpers/support_command
	LANGUAGES="$LANGUAGES Perl"
fi

if test x$ruby = xtrue; then
	AC_CHECK_PROG([RUBYINT],[ruby],["yes"], ["no"])
    RUBYINCLUDES=""
	if test "$RUBYINT" = "no"
	then
		AC_MSG_ERROR(ruby interpreter not found)
	fi
	found=""
	for loc in `ruby -e 'puts $:.join(" ")'`
	do

		if test "x$found" = "x" 
		then
			AC_MSG_CHECKING([for ruby.h in])
			AC_MSG_RESULT($loc)
			AC_CHECK_FILE("$loc/ruby.h", [RUBYINCLUDES="$RUBYINCLUDES -I$loc";found=1],[])
		fi  
	done
	if test "x$found" = "x" 
	then
		AC_MSG_ERROR([Unable to find ruby headers.])
	fi
    RUBYDEFINES="-fno-strict-aliasing -D_FILE_OFFSET_BITS=64"
    AC_SUBST(RUBYINCLUDES)
    AC_SUBST(RUBYDEFINES)
	AC_SUBST_FILE([RUBY_AUTOGENERATION])
	RUBY_AUTOGENERATION=$srcdir/src/autogen_helpers/support_command
	LANGUAGES="$LANGUAGES Ruby"
fi

if test x$guile = xtrue; then
	AC_CHECK_PROG([GUILEINT],[guile],["yes"], ["no"])
	if test "$GUILEINT" = "no"
	then
		AC_MSG_ERROR(guile interpreter not found)
	fi
	AC_SUBST_FILE([GUILE_AUTOGENERATION])
	GUILE_AUTOGENERATION=$srcdir/src/autogen_helpers/support_command
	LANGUAGES="$LANGUAGES Guile"
fi

AC_SUBST(LHT_LIBDEPS)
AC_SUBST(CPPFLAGS)
AC_SUBST(LIBS)
AC_SUBST(LDFLAGS)
AC_SUBST(EXT_LIB_PATH)
AC_SUBST(EXT_LIBS)

AC_OUTPUT(
	  libhttpserver.pc Makefile src/Makefile src/python/Makefile src/java/Makefile src/php/Makefile src/lua/Makefile src/perl/Makefile src/ruby/Makefile src/guile/Makefile
)

AC_MSG_NOTICE([Configuration Summary:  
  Operating System:  ${host_os}
  Target directory:  ${prefix}
  License         :  LGPL only
  Languages       :  ${LANGUAGES}
])
