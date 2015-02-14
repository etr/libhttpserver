/*
     This file is part of libhttpserver
     Copyright (C) 2011, 2012, 2013, 2014 Sebastiano Merlino

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

#ifndef _EVENT_TUPLE_HPP_
#define _EVENT_TUPLE_HPP_

#include "httpserver/event_supplier.hpp"
#include "httpserver/binders.hpp"

namespace httpserver {

namespace details
{
    class http_endpoint;
    struct modded_request;
    struct daemon_item;

    class event_tuple
    {
        private:
            typedef void(*supply_events_ptr)(
                            fd_set*,
                            fd_set*,
                            fd_set*,
                            MHD_socket*
                    );

            binders::functor_four<fd_set*, fd_set*, fd_set*, MHD_socket*, void> supply_events;
            binders::functor_zero<struct timeval> get_timeout;
            binders::functor_zero<void> dispatch_events;

            event_tuple();

            friend class ::httpserver::webserver;
        public:
            template<typename T>
            event_tuple(event_supplier<T>* es):
                supply_events(binders::functor_four<fd_set*, fd_set*, fd_set*, MHD_socket*, void>(es, &T::supply_events)),
                get_timeout(binders::functor_zero<struct timeval>(es, &T::get_timeout)),
                dispatch_events(binders::functor_zero<void>(es, &T::dispatch_events))
            {
            }
    };
} //details

} //httpserver

#endif //_EVENT_TUPLE_HPP_
