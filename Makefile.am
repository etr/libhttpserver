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

# not a GNU package. You can remove this line, if
# have all needed files, that a GNU package needs
AUTOMAKE_OPTIONS = foreign 1.4

SUBDIRS = src $(PYTHON_DIR) $(JAVA_DIR) $(PHP_DIR) $(LUA_DIR) $(PERL_DIR) $(RUBY_DIR)
DIST_SUBDIRS = src $(PYTHON_DIR) $(JAVA_DIR) $(PHP_DIR) $(LUA_DIR) $(PERL_DIR) $(RUBY_DIR)
EXTRA_DIST = libhttpserver.pc.in

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libhttpserver.pc

if PYTHON
PYTHON_DIR = src/python
endif

if JAVA
JAVA_DIR = src/java
endif

if PHP
PHP_DIR = src/php
endif

if LUA
LUA_DIR = src/lua
endif

if PERL
PERL_DIR = src/perl
endif

if RUBY
RUBY_DIR = src/ruby
endif
