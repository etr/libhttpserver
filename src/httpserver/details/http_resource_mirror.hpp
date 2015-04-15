/*
     This file is part of libhttpserver
     Copyright (C) 2011, 2012, 2013, 2014, 2015 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef _HTTP_RESOURCE_MIRROR_HPP_
#define _HTTP_RESOURCE_MIRROR_HPP_

#include "httpserver/binders.hpp"

#define CREATE_METHOD_DETECTOR(X) \
    template<typename T, typename RESULT, typename ARG1, typename ARG2> \
    class has_##X \
    { \
        template <typename U, RESULT (U::*)(ARG1, ARG2)> struct Check; \
        template <typename U> static char func(Check<U, &U::X> *); \
        template <typename U> static int func(...); \
        public: \
            enum { value = sizeof(func<T>(0)) == sizeof(char) }; \
    };

#define HAS_METHOD(X, T, RESULT, ARG1, ARG2) \
    has_##X<T, RESULT, ARG1, ARG2>::value

namespace httpserver {

template<typename CHILD> class http_resource;
class http_request;
class http_response;
class webserver;

namespace details
{
    class http_endpoint;
    struct modded_request;
    struct daemon_item;
    typedef bool(*is_allowed_ptr)(const std::string&);

    CREATE_METHOD_DETECTOR(render);
    CREATE_METHOD_DETECTOR(render_GET);
    CREATE_METHOD_DETECTOR(render_POST);
    CREATE_METHOD_DETECTOR(render_PUT);
    CREATE_METHOD_DETECTOR(render_HEAD);
    CREATE_METHOD_DETECTOR(render_DELETE);
    CREATE_METHOD_DETECTOR(render_TRACE);
    CREATE_METHOD_DETECTOR(render_OPTIONS);
    CREATE_METHOD_DETECTOR(render_CONNECT);
    CREATE_METHOD_DETECTOR(render_not_acceptable);

    void empty_render(const http_request& r, http_response** res);
    void empty_not_acceptable_render(
            const http_request& r, http_response** res
    );
    bool empty_is_allowed(const std::string& method);

    class http_resource_mirror
    {
        public:
            http_resource_mirror()
            {
            }

            ~http_resource_mirror()
            {
            }
        private:
            typedef binders::functor_two<const http_request&,
                    http_response**, void> functor;

            typedef binders::functor_one<const std::string&,
                    bool> functor_allowed;

            const functor render;
            const functor render_GET;
            const functor render_POST;
            const functor render_PUT;
            const functor render_HEAD;
            const functor render_DELETE;
            const functor render_TRACE;
            const functor render_OPTIONS;
            const functor render_CONNECT;
            const functor_allowed is_allowed;

            functor method_not_acceptable_resource;

            http_resource_mirror& operator= (const http_resource_mirror& o)
            {
                return *this;
            }

            template<typename T>
            http_resource_mirror(http_resource<T>* res):
                render(
                    HAS_METHOD(render, T, void,
                        const http_request&, http_response**
                    ) ? functor(res, &T::render) : functor(&empty_render)
                ),
                render_GET(
                    HAS_METHOD(render_GET, T, void,
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_GET) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                render_POST(
                    HAS_METHOD(render_POST, T, void,
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_POST) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                render_PUT(
                    HAS_METHOD(render_PUT, T, void,
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_PUT) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                render_HEAD(
                    HAS_METHOD(render_HEAD, T, void,
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_HEAD) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                render_DELETE(
                    HAS_METHOD(render_DELETE, T, void,
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_DELETE) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                render_TRACE(
                    HAS_METHOD(render_TRACE, T, void,
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_TRACE) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                render_OPTIONS(
                    HAS_METHOD(render_OPTIONS, T, void,
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_OPTIONS) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                render_CONNECT(
                    HAS_METHOD(render_CONNECT, T, void,
                        const http_request&, http_response**
                    ) ? functor(res, &T::render_CONNECT) :
                    (
                        HAS_METHOD(render, T, void,
                            const http_request&, http_response**
                        ) ? functor(res, &T::render) : functor(&empty_render)
                    )
                ),
                is_allowed(res, &T::is_allowed)
            {
            }

            friend class ::httpserver::webserver;
    };
} //details
} //httpserver

#endif //_HTTP_RESOURCE_MIRROR_HPP_
