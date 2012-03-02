INCLUDES = -I../ -I$(top_srcdir)/src
METASOURCES = AUTO
lib_LTLIBRARIES = libhttpserver_ruby.la
libhttpserver_ruby_la_LIBADD = -lmicrohttpd
libhttpserver_ruby_la_SOURCES = WebserverWrap.cpp ../Webserver.cpp ../HttpUtils.cpp
libhttpserver_ruby_la_includedir = 
libhttpserver_ruby_la_include_HEADERS = libhttpserver_ruby.rb

language=ruby
@RUBY_AUTOGENERATION@
