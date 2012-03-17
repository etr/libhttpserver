#ifndef _httpserver_h_
#define _httpserver_h_

#ifdef SWIG
%include "std_string.i"
%include "std_vector.i"
%include "exception.i"
%include "stl.i"
%include "std_map.i"
%include "typemaps.i"
%include "cpointer.i"

%ignore policyCallback (void*, const struct sockaddr*, socklen_t);
%ignore error_log(void*, const char*, va_list);
%ignore access_log(Webserver*, std::string);
%ignore uri_log(void*, const char*);
%ignore unescaper_func(void*, struct MHD_Connection*, char*);
%ignore internal_unescaper(void*, char*);

namespace std {
	%template(StringVector) vector<std::string>;
	%template(StringPair) pair<string, string>;
	%template(ArgVector) vector<std::pair<std::string, std::string> >;
}

#ifdef SWIGPHP
%module(directors="1") libhttpserver_php
#endif
		
#ifdef SWIGJAVA
%javaconst(1);
%module(directors="1") libhttpserver_java
%include "jstring.i"
#endif

#ifdef SWIGRUBY
%module(directors="1") libhttpserver_ruby
#endif

#ifdef SWIGGUILE
%module(directors="1") libhttpserver_guile
#endif

#ifdef SWIGLUA
%module(directors="1") libhttpserver_lua
#endif

#ifdef SWIGPERL
%module(directors="1") libhttpserver_perl
#endif

#ifdef SWIGPYTHON
%module(directors="1") libhttpserver_python
#endif

%feature("director") Webserver;
%feature("director") HttpRequest;
%feature("director") HttpResponse;
%feature("director") HttpResource;
%feature("director") HttpEndpoint;
%feature("director") HttpUtils;

#ifdef SWIGPYTHON
%typemap(out) string {
	$result = PyString_FromStringAndSize($1,$1.size);
}

%feature("director:except") {
	if ($error != NULL) {
		throw Swig::DirectorMethodException();
	}
}

#endif

%template(SQMHeaders) std::map<std::string, std::string>;    

%extend std::map<std::string, std::string> {
	std::string getHeader(std::string key)  {
		std::map<std::string,std::string >::iterator i = self->find(key);
		if (i != self->end())
			return i->second;
		else
			return "";    
	}    
};

%exception {
	try {
		$action
	} 
#ifdef SWIGPYTHON //DirectorException should be ignored in python because is used as an internal trick
	catch (const Swig::DirectorException& e)
	{
		PyEval_SaveThread();
		PyErr_SetString(PyExc_RuntimeError, e.getMessage());
	}
#endif
	catch (const std::out_of_range& e) {
#ifdef SWIGPYTHON
		PyEval_SaveThread();
#endif
		SWIG_exception(SWIG_IndexError,const_cast<char*>(e.what()));
	} catch (const std::exception& e) {
#ifdef SWIGPYTHON
		PyEval_SaveThread();
#endif
		SWIG_exception(SWIG_RuntimeError, e.what());
	} catch (...) {
#ifdef SWIGPYTHON
		PyEval_SaveThread();
#endif
		SWIG_exception(SWIG_RuntimeError, "Generic SWIG Exception");
	}
};

%include "HttpUtils.hpp"
%include "HttpRequest.hpp"
%include "HttpResponse.hpp"
%include "HttpResource.hpp"
%include "HttpEndpoint.hpp"
%include "Webserver.hpp"

%{
#include "HttpUtils.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpResource.hpp"
#include "HttpEndpoint.hpp"
#include "Webserver.hpp"
%}

#endif

#include "httpserver/HttpUtils.hpp"
#include "httpserver/HttpEndpoint.hpp"
#include "httpserver/HttpResource.hpp"
#include "httpserver/HttpResponse.hpp"
#include "httpserver/HttpRequest.hpp"
#include "httpserver/Webserver.hpp"

#endif
