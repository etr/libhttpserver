INCLUDES = -I../ -I$(top_srcdir)/src
METASOURCES = AUTO
lib_LTLIBRARIES = libhttpserver_lua.la
libhttpserver_lua_la_LIBADD = -lmicrohttpd
libhttpserver_lua_la_SOURCES = WebserverWrap.cpp ../Webserver.cpp ../HttpUtils.cpp
libhttpserver_lua_la_includedir = 
libhttpserver_lua_la_include_HEADERS = libhttpserver_lua.lua

language=lua
@LUA_AUTOGENERATION@
