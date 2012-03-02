INCLUDES = -I../ -I$(top_srcdir)/src
METASOURCES = AUTO
lib_LTLIBRARIES = libhttpserver_java.la
libhttpserver_java_la_LIBADD = -lmicrohttpd
libhttpserver_java_la_SOURCES = WebserverWrap.cpp ../Webserver.cpp ../HttpUtils.cpp
libjavasqi_la_includedir = $(includedir)/amgx/swig/java
libjavasqi_la_include_HEADERS = Webserver.java HttpRequest.java HttpResponse.java HttpResource.java HttpEndpoint.java HttpUtils.java

javadir=$(datadir)/java

language=java
@JAVA_AUTOGENERATION@
