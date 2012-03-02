INCLUDES = -I../ -I$(top_srcdir)/src
METASOURCES = AUTO
lib_LTLIBRARIES = libhttpserver_perl.la
libhttpserver_perl_la_LIBADD = -lmicrohttpd
libhttpserver_perl_la_SOURCES = WebserverWrap.cpp ../Webserver.cpp ../HttpUtils.cpp
libhttpserver_perl_la_includedir = 
libhttpserver_perl_la_include_HEADERS = libhttpserver_perl.pl

language=perl
@PERL_AUTOGENERATION@
