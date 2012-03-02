INCLUDES = -I../ -I$(top_srcdir)/src
METASOURCES = AUTO
lib_LTLIBRARIES = libhttpserver_php.la
libhttpserver_php_la_LIBADD = -lmicrohttpd
libhttpserver_php_la_SOURCES = WebserverWrap.cpp ../Webserver.cpp ../HttpUtils.cpp
libhttpserver_php_la_includedir = 
libhttpserver_php_la_include_HEADERS = libhttpserver_php.php

language=php
@PHP_AUTOGENERATION@
