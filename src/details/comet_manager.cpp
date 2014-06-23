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

#include <errno.h>
#include <iostream>
#include "details/comet_manager.hpp"
#include <sys/time.h>

using namespace std;

namespace httpserver
{

namespace details
{

comet_manager::comet_manager()
{
    pthread_rwlock_init(&comet_guard, NULL);
    pthread_mutex_init(&cleanmux, NULL);
    pthread_cond_init(&cleancond, NULL);
}

comet_manager::~comet_manager()
{
    pthread_rwlock_destroy(&comet_guard);
    pthread_mutex_destroy(&cleanmux);
    pthread_cond_destroy(&cleancond);
}

void comet_manager::send_message_to_topic (
        const string& topic,
        const string& message,
        const httpserver::http::http_utils::start_method_T& start_method
)
{
    pthread_rwlock_wrlock(&comet_guard);
    for(set<http::httpserver_ska>::const_iterator it = q_waitings[topic].begin();
            it != q_waitings[topic].end();
            ++it
    )
    {
        q_messages[(*it)].push_back(message);
        q_signal.insert((*it));
        if(start_method != http::http_utils::INTERNAL_SELECT)
        {
            pthread_mutex_lock(&q_blocks[(*it)].first);
            pthread_cond_signal(&q_blocks[(*it)].second);
            pthread_mutex_unlock(&q_blocks[(*it)].first);
        }
        map<http::httpserver_ska, long>::const_iterator itt;
        if((itt = q_keepalives.find(*it)) != q_keepalives.end())
        {
            struct timeval curtime;
            gettimeofday(&curtime, NULL);
            q_keepalives[*it] = curtime.tv_sec;
        }
    }
    pthread_rwlock_unlock(&comet_guard);
    if(start_method != http::http_utils::INTERNAL_SELECT)
    {
        pthread_mutex_lock(&cleanmux);
        pthread_cond_signal(&cleancond);
        pthread_mutex_unlock(&cleanmux);
    }
}

void comet_manager::register_to_topics (
        const vector<string>& topics,
        const http::httpserver_ska& connection_id,
        int keepalive_secs,
        string keepalive_msg,
        const httpserver::http::http_utils::start_method_T& start_method
)
{
    pthread_rwlock_wrlock(&comet_guard);
    for(vector<string>::const_iterator it = topics.begin();
            it != topics.end(); ++it
    )
        q_waitings[*it].insert(connection_id);
    if(keepalive_secs != -1)
    {
        struct timeval curtime;
        gettimeofday(&curtime, NULL);
        q_keepalives[connection_id] = curtime.tv_sec;
        q_keepalives_mem[connection_id] = make_pair(
                keepalive_secs, keepalive_msg
        );
    }
    if(start_method != http::http_utils::INTERNAL_SELECT)
    {
        pthread_mutex_t m;
        pthread_cond_t c;
        pthread_mutex_init(&m, NULL);
        pthread_cond_init(&c, NULL);
        q_blocks[connection_id] =
            make_pair(m, c);
    }
    pthread_rwlock_unlock(&comet_guard);
}

size_t comet_manager::read_message(const http::httpserver_ska& connection_id,
    string& message
)
{
    pthread_rwlock_wrlock(&comet_guard);
    deque<string>& t_deq = q_messages[connection_id];
    message.assign(t_deq.front());
    t_deq.pop_front();
    pthread_rwlock_unlock(&comet_guard);
    return message.size();
}

size_t comet_manager::get_topic_consumers(
        const string& topic,
        set<http::httpserver_ska>& consumers
)
{
    pthread_rwlock_rdlock(&comet_guard);

    for(set<http::httpserver_ska>::const_iterator it = q_waitings[topic].begin();
            it != q_waitings[topic].end(); ++it
    )
    {
        consumers.insert((*it));
    }
    std::set<httpserver::http::httpserver_ska>::size_type size = consumers.size();
    pthread_rwlock_unlock(&comet_guard);
    return size;
}

bool comet_manager::pop_signaled(const http::httpserver_ska& consumer,
        const httpserver::http::http_utils::start_method_T& start_method
)
{
    if(start_method == http::http_utils::INTERNAL_SELECT)
    {
        pthread_rwlock_wrlock(&comet_guard);
        set<http::httpserver_ska>::iterator it = q_signal.find(consumer);
        if(it != q_signal.end())
        {
            if(q_messages[consumer].empty())
            {
                q_signal.erase(it);
                pthread_rwlock_unlock(&comet_guard);
                return false;
            }
            pthread_rwlock_unlock(&comet_guard);
            return true;
        }
        else
        {
            pthread_rwlock_unlock(&comet_guard);
            return false;
        }
    }
    else
    {
        pthread_rwlock_rdlock(&comet_guard);
        pthread_mutex_lock(&q_blocks[consumer].first);
        struct timespec t;
        struct timeval curtime;

        {
            bool to_unlock = true;
            while(q_signal.find(consumer) == q_signal.end())
            {
                if(to_unlock)
                {
                    pthread_rwlock_unlock(&comet_guard);
                    to_unlock = false;
                }
                gettimeofday(&curtime, NULL);
                t.tv_sec = curtime.tv_sec + q_keepalives_mem[consumer].first;
                t.tv_nsec = 0;
                int rslt = pthread_cond_timedwait(&q_blocks[consumer].second,
                        &q_blocks[consumer].first, &t
                );
                if(rslt == ETIMEDOUT)
                {
                    pthread_rwlock_wrlock(&comet_guard);
                    send_message_to_consumer(consumer,
                            q_keepalives_mem[consumer].second, false, start_method
                    );
                    pthread_rwlock_unlock(&comet_guard);
                }
            }
            if(to_unlock)
                pthread_rwlock_unlock(&comet_guard);
        }

        if(q_messages[consumer].size() == 0)
        {
            pthread_rwlock_wrlock(&comet_guard);
            q_signal.erase(consumer);
            pthread_mutex_unlock(&q_blocks[consumer].first);
            pthread_rwlock_unlock(&comet_guard);
            return false;
        }
        pthread_rwlock_rdlock(&comet_guard);
        pthread_mutex_unlock(&q_blocks[consumer].first);
        pthread_rwlock_unlock(&comet_guard);
        return true;
    }
    return false;
}

void comet_manager::complete_request(const http::httpserver_ska& connection_id)
{
    pthread_rwlock_wrlock(&comet_guard);
    q_messages.erase(connection_id);
    q_blocks.erase(connection_id);
    q_signal.erase(connection_id);
    q_keepalives.erase(connection_id);

    typedef map<string, set<http::httpserver_ska> >::iterator conn_it;
    for(conn_it it = q_waitings.begin(); it != q_waitings.end(); ++it)
    {
        it->second.erase(connection_id);
    }
    pthread_rwlock_unlock(&comet_guard);
}

void comet_manager::comet_select(unsigned long long* timeout_secs,
        unsigned long long* timeout_microsecs,
        const httpserver::http::http_utils::start_method_T& start_method
)
{
    pthread_rwlock_wrlock(&comet_guard);
    for(map<http::httpserver_ska, long>::iterator it = q_keepalives.begin(); it != q_keepalives.end(); ++it)
    {
        struct timeval curtime;
        gettimeofday(&curtime, NULL);
        int waited_time = curtime.tv_sec - (*it).second;
        if(waited_time >= q_keepalives_mem[(*it).first].first)
        {
            send_message_to_consumer((*it).first, q_keepalives_mem[(*it).first].second, true, start_method);
        }
        else
        {
            unsigned long long to_wait_time = (q_keepalives_mem[(*it).first].first - waited_time);
            if(to_wait_time < *timeout_secs)
            {
                *timeout_secs = to_wait_time;
                *timeout_microsecs = 0;
            }
        }
    }
    pthread_rwlock_unlock(&comet_guard);
}

void comet_manager::send_message_to_consumer(
        const http::httpserver_ska& connection_id,
        const std::string& message,
        bool to_lock,
        const httpserver::http::http_utils::start_method_T& start_method
)
{
    //This function need to be externally locked on write
    q_messages[connection_id].push_back(message);
    map<http::httpserver_ska, long>::const_iterator it;
    if((it = q_keepalives.find(connection_id)) != q_keepalives.end())
    {
        struct timeval curtime;
        gettimeofday(&curtime, NULL);
        q_keepalives[connection_id] = curtime.tv_sec;
    }
    q_signal.insert(connection_id);
    if(start_method != http::http_utils::INTERNAL_SELECT)
    {
        if(to_lock)
            pthread_mutex_lock(&q_blocks[connection_id].first);
        pthread_cond_signal(&q_blocks[connection_id].second);
        if(to_lock)
            pthread_mutex_unlock(&q_blocks[connection_id].first);
    }
}

} //details

} //httpserver
