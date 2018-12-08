/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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

#ifndef _HTTP_RESPONSE_PTR_HPP_
#define _HTTP_RESPONSE_PTR_HPP_

#include "http_response.hpp"

#if defined(__CLANG_ATOMICS)

#define atomic_increment(object) \
    __c11_atomicadd_fetch(object, 1, __ATOMIC_RELAXED)

#define atomic_decrement(object) \
    __c11_atomic_sub_fetch(object, 1, __ATOMIC_ACQ_REL)

#elif defined(__GNUC_ATOMICS)

#define atomic_increment(object) \
    __atomic_add_fetch(object, 1, __ATOMIC_RELAXED)

#define atomic_decrement(object) \
    __atomic_sub_fetch(object, 1, __ATOMIC_ACQ_REL)

#else

#define atomic_increment(object) \
    __sync_add_and_fetch(object, 1)

#define atomic_decrement(object) \
    __sync_sub_and_fetch(object, 1)

#endif

namespace httpserver
{

class webserver;

namespace details
{

struct http_response_ptr
{
    public:
        http_response_ptr(http_response* res = 0x0):
            res(res)
        {
            num_references = new int(1);
        }

        http_response_ptr(const http_response_ptr& b):
            res(b.res),
            num_references(b.num_references)
        {
            atomic_increment(b.num_references);
        }

        ~http_response_ptr()
        {
            if (atomic_decrement(num_references) != 0) return;

            if (res != 0x0) delete res;
            if (num_references != 0x0) delete num_references;

            res = 0x0;
            num_references = 0x0;
        }

        http_response_ptr& operator=(http_response_ptr b)
        {
            using std::swap;

            swap(this->num_references, b.num_references);
            swap(this->res, b.res);

            return *this;
        }

        http_response& operator* ()
        {
            return *res;
        }

        http_response* operator-> ()
        {
            return res;
        }

        http_response* ptr()
        {
            return res;
        }

    private:
        http_response* res;
        int* num_references;
        friend class ::httpserver::webserver;
};

} //details
} //httpserver

#endif //_HTTP_RESPONSE_PTR_HPP_
