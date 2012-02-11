INCLUDES = -I../ -I$(top_srcdir)/src
METASOURCES = AUTO
lib_LTLIBRARIES = libphpwebserver_framework.la
libphpwebserver_framework_la_LIBADD = -lmicrohttpd
libphpwebserver_framework_la_SOURCES = WebserverWrap.cpp ../Webserver.cpp ../HttpUtils.cpp
libphpwebserver_framework_la_includedir = 
libphpwebserver_framework_la_include_HEADERS = libphpwebserver_framework.php

language=php
@PHP_AUTOGENERATION@
