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

#ifndef _EVENT_SUPPLIER_HPP_
#define _EVENT_SUPPLIER_HPP_

namespace httpserver {

template <typename CHILD>
class event_supplier
{
    public:
        event_supplier()
        {
        }

        ~event_supplier()
        {
        }

        void supply_events(
                fd_set* read_fdset,
                fd_set* write_fdset,
                fd_set* exc_fdset,
                MHD_socket* max
        ) const
        {
            static_cast<CHILD*>(this)->supply_events(
                    read_fdset, write_fdset, exc_fdset, max
            );
        }

        struct timeval get_timeout() const
        {
            return static_cast<CHILD*>(this)->get_timeout();
        }

        void dispatch_events() const
        {
            static_cast<CHILD*>(this)->dispatch_events();
        }
};

} //event_supplier

#endif //_EVENT_SUPPLIER_HPP_
