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

#ifndef _COMET_MANAGER_HPP_
#define _COMET_MANAGER_HPP_

#include <pthread.h>
#include <vector>
#include <set>
#include <map>
#include <deque>
#include <string>
#include "http_utils.hpp"

namespace httpserver
{

namespace http
{
struct httpserver_ska;
};

namespace details
{

class comet_manager
{
    public:
        comet_manager();

        ~comet_manager();

        void send_message_to_topic(const std::string& topic,
                const std::string& message, const httpserver::http::http_utils::start_method_T& start_method
        );

        void send_message_to_consumer(const http::httpserver_ska& connection_id,
                const std::string& message, bool to_lock,
                const httpserver::http::http_utils::start_method_T& start_method
        );

        void register_to_topics(const std::vector<std::string>& topics,
                const http::httpserver_ska& connection_id, int keepalive_secs,
                std::string keepalive_msg, const httpserver::http::http_utils::start_method_T& start_method
        );

        size_t read_message(const http::httpserver_ska& connection_id,
            std::string& message
        );

        size_t get_topic_consumers(const std::string& topic,
                std::set<http::httpserver_ska>& consumers
        );

        bool pop_signaled(const http::httpserver_ska& consumer, const httpserver::http::http_utils::start_method_T& start_method);

        void complete_request(const http::httpserver_ska& connection_id);

        void comet_select(unsigned long long* timeout_secs,
                unsigned long long* timeout_microsecs,
                const httpserver::http::http_utils::start_method_T& start_method
        );

    protected:
        comet_manager(const comet_manager&)
        {
        }

    private:
        std::map<http::httpserver_ska, std::deque<std::string> > q_messages;
        std::map<std::string, std::set<http::httpserver_ska> > q_waitings;
        std::map<http::httpserver_ska, std::pair<pthread_mutex_t, pthread_cond_t> > q_blocks;
        std::set<http::httpserver_ska> q_signal;
        std::map<http::httpserver_ska, long> q_keepalives;
        std::map<http::httpserver_ska, std::pair<int, std::string> > q_keepalives_mem;
        pthread_rwlock_t comet_guard;
        pthread_mutex_t cleanmux;
        pthread_cond_t cleancond;
};

} //details

} //httpserver

#endif //_COMET_MANAGER_HPP_
