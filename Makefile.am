#
#     This file is part of libhttpserver
#     Copyright (C) 2011, 2012, 2013, 2014, 2015 Sebastiano Merlino
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

SUBDIRS = src
DIST_SUBDIRS = src

if !COND_CROSS_COMPILE
SUBDIRS += test
DIST_SUBDIRS += test

if BUILD_EXAMPLES
SUBDIRS += examples
DIST_SUBDIRS += examples
endif

endif

EXTRA_DIST = libhttpserver.pc.in $(DX_CONFIG)

MOSTLYCLEANFILES = $(DX_CLEANFILES) *.gcda *.gcno *.gcov
DISTCLEANFILES = DIST_REVISION

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libhttpserver.pc

cmakemoduledir = $(datadir)/cmake/Modules
cmakemodule_DATA = cmakemodule/FindLibHttpServer.cmake

include $(top_srcdir)/aminclude.am

# Update libtool, if needed.
libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck

dist-hook:
	date >DIST_REVISION
	git branch -vv >>DIST_REVISION
	cp DIST_REVISION $(distdir)/
