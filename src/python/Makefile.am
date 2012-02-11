INCLUDES = -I../ -I$(top_srcdir)/src
METASOURCES = AUTO
lib_LTLIBRARIES = libpythonwebserver_framework.la
libpythonwebserver_framework_la_LIBADD = -lmicrohttpd
libpythonwebserver_framework_la_SOURCES = WebserverWrap.cpp ../Webserver.cpp ../HttpUtils.cpp

pydir=$(datadir)/python

py_DATA=libpythonwebserver_framework.py

language=python
@PYTHON_AUTOGENERATION@
