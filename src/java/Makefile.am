INCLUDES = -I../ -I$(top_srcdir)/src
METASOURCES = AUTO
lib_LTLIBRARIES = libjavawebserver_framework.la
libjavawebserver_framework_la_LIBADD = -lmicrohttpd
libjavawebserver_framework_la_SOURCES = WebserverWrap.cpp ../Webserver.cpp ../HttpUtils.cpp
libjavasqi_la_includedir = $(includedir)/amgx/swig/java
libjavasqi_la_include_HEADERS = Webserver.java HttpRequest.java HttpResponse.java HttpResource.java HttpEndpoint.java HttpUtils.java

javadir=$(datadir)/java

language=java
@JAVA_AUTOGENERATION@
