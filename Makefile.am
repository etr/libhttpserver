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

LIBTOOL_DEPS = @LIBTOOL_DEPS@

AUTOMAKE_OPTIONS = foreign 1.4
ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src test $(PYTHON_DIR) $(JAVA_DIR) $(PHP_DIR) $(LUA_DIR) $(PERL_DIR) $(RUBY_DIR) $(GUILE_DIR)
DIST_SUBDIRS = src test $(PYTHON_DIST_DIR) $(JAVA_DIST_DIR) $(PHP_DIST_DIR) $(LUA_DIST_DIR) $(PERL_DIST_DIR) $(RUBY_DIST_DIR) $(GUILE_DIST_DIR)
EXTRA_DIST = libhttpserver.pc.in debian/changelog.in debian/control.in debian/copyright.in debian/rules.in debian/libhttpserver-dev.install.in debian/libhttpserver.install.in redhat/libhttpserver.SPEC.in $(DX_CONFIG) $(PYTHON_EXTRA) $(PHP_EXTRA) $(PERL_EXTRA)

JAVACLEANFILES = $(top_srcdir)/src/java/*.java $(top_srcdir)/src/java/webserver_wrap.*
PYTHONCLEANFILES = $(top_srcdir)/src/python/*.py $(top_srcdir)/src/python/webserver_wrap.*

MOSTLYCLEANFILES = $(DX_CLEANFILES) redhat/SOURCES/* $(JAVACLEANFILES) $(PYTHONCLEANFILES)
DISTCLEANFILES = redhat/SOURCES/* redhat/SPEC/* redhat/RPMS/* redhat/SRPMS/* redhat/* debian/* DIST_REVISION

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libhttpserver.pc

PYTHON_DIST_DIR = src/python
if PYTHON
PYTHON_DIR = $(PYTHON_DIST_DIR)
PYTHON_EXTRA = src/python/libhttpserver_python.py
endif

JAVA_DIST_DIR = src/java
if JAVA
JAVA_DIR = $(JAVA_DIST_DIR)
endif

PHP_DIST_DIR = src/php
if PHP
PHP_DIR = $(PHP_DIST_DIR)
PHP_EXTRA = src/php/libhttpserver_php.php
endif

LUA_DIST_DIR = src/lua
if LUA
LUA_DIR = $(LUA_DIST_DIR)
endif

PERL_DIST_DIR = src/perl
if PERL
PERL_DIR = $(PERL_DIST_DIR)
PERL_EXTRA = src/php/libhttpserver_perl.pm
endif

RUBY_DIST_DIR = src/ruby
if RUBY
RUBY_DIR = $(RUBY_DIST_DIR)
endif

GUILE_DIST_DIR = src/guile
if GUILE
GUILE_DIR = $(GUILE_DIST_DIR)
endif

include $(top_srcdir)/aminclude.am

# Update libtool, if needed.
libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck

dist-hook:
	date >DIST_REVISION
	git branch -vv >>DIST_REVISION
	cp DIST_REVISION $(distdir)/

deb:
	debuild -i -us -uc -b
	mv ../libhttpserver_@VERSION@_* .
	mv ../libhttpserver-dev_@VERSION@_* .
	mv ../libhttpserver-dbg_@VERSION@_* .

rpm: dist
	mkdir -p redhat/SOURCES
	mkdir -p redhat/BUILD
	mkdir -p redhat/RPMS
	mkdir -p redhat/SRPMS
	cp libhttpserver-@VERSION@.tar.gz redhat/SOURCES
	rpmbuild -v -bb redhat/libhttpserver.SPEC
	rpmbuild -v -ba redhat/libhttpserver.SPEC
