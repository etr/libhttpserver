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

INCLUDES = -I../ -I$(top_srcdir)/src -I$(top_srcdir)/src/httpserver $(PYINCLUDES)
AM_CPPFLAGS=$(PYDEFINES)
pythonSitePackagesdirdir = $(pythondir)
METASOURCES = AUTO
pythonSitePackagesdir_LTLIBRARIES = _libhttpserver_python.la
pythonSitePackagesdir_SCRIPTS = libhttpserver_python.py
_libhttpserver_python_la_LDFLAGS = -module $(PYLDFLAGS)
_libhttpserver_python_la_LIBADD = -lmicrohttpd $(PYLIBS)
_libhttpserver_python_la_SOURCES = webserver_wrap.cpp $(top_srcdir)/src/webserver.cpp $(top_srcdir)/src/http_utils.cpp $(top_srcdir)/src/http_endpoint.cpp $(top_srcdir)/src/http_resource.cpp $(top_srcdir)/src/http_request.cpp $(top_srcdir)/src/http_response.cpp $(top_srcdir)/src/string_utilities.cpp

language=python
swigoptions=-Wall
swigscripts=libhttpserver_python.py
@PYTHON_AUTOGENERATION@
