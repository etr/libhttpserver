INCLUDES = -I../ -I$(top_srcdir)/src
METASOURCES = AUTO
lib_LTLIBRARIES = libhttpserver_python.la
libhttpserver_python_la_LIBADD = -lmicrohttpd
libhttpserver_python_la_SOURCES = WebserverWrap.cpp ../Webserver.cpp ../HttpUtils.cpp

pydir=$(datadir)/python

py_DATA=libhttpserver_python.py

language=python
@PYTHON_AUTOGENERATION@
