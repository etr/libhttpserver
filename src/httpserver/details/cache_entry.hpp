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

#ifndef _CACHE_ENTRY_HPP_
#define _CACHE_ENTRY_HPP_

#include <pthread.h>
#include <set>
#include "details/http_response_ptr.hpp"

namespace httpserver
{
namespace details
{

struct pthread_t_comparator
{
    bool operator()(const pthread_t& t1, const pthread_t& t2) const
    {
        return pthread_equal(t1, t2);
    }
};

struct cache_entry
{
    long ts;
    int validity;
    details::http_response_ptr response;
    pthread_rwlock_t elem_guard;
    pthread_mutex_t lock_guard;
    std::set<pthread_t, pthread_t_comparator> lockers;

    cache_entry():
        ts(-1),
        validity(-1)
    {
        pthread_rwlock_init(&elem_guard, NULL);
        pthread_mutex_init(&lock_guard, NULL);
    }

    ~cache_entry()
    {
        pthread_rwlock_destroy(&elem_guard);
        pthread_mutex_destroy(&lock_guard);
    }

    cache_entry(const cache_entry& b):
        ts(b.ts),
        validity(b.validity),
        response(b.response),
        elem_guard(b.elem_guard),
        lock_guard(b.lock_guard)
    {
    }

    void operator= (const cache_entry& b)
    {
        ts = b.ts;
        validity = b.validity;
        response = b.response;
        pthread_rwlock_destroy(&elem_guard);
        pthread_mutex_destroy(&lock_guard);
        elem_guard = b.elem_guard;
    }

    cache_entry(
            details::http_response_ptr response,
            long ts = -1,
            int validity = -1
    ):
        ts(ts),
        validity(validity),
        response(response)
    {
        pthread_rwlock_init(&elem_guard, NULL);
        pthread_mutex_init(&lock_guard, NULL);
    }

    void lock(bool write = false)
    {
        pthread_mutex_lock(&lock_guard);
        pthread_t tid = pthread_self();
        if(!lockers.count(tid))
        {
            if(write)
            {
                lockers.insert(tid);
                pthread_mutex_unlock(&lock_guard);
                pthread_rwlock_wrlock(&elem_guard);
            }
            else
            {
                lockers.insert(tid);
                pthread_mutex_unlock(&lock_guard);
                pthread_rwlock_rdlock(&elem_guard);
            }
        }
        else
            pthread_mutex_unlock(&lock_guard);
    }

    void unlock()
    {
        pthread_mutex_lock(&lock_guard);
        {
            pthread_t tid = pthread_self();
            if(lockers.count(tid))
            {
                lockers.erase(tid);
                pthread_rwlock_unlock(&elem_guard);
            }
        }
        pthread_mutex_unlock(&lock_guard);
    }
};

} //details
} //httpserver

#endif //_CACHE_ENTRY_HPP_
