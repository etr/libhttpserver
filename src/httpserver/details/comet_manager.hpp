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

class webserver;

namespace details
{

class comet_manager
{
    private:
        comet_manager();

        ~comet_manager();

        void send_message_to_topic(const std::string& topic, const std::string& message);

        void register_to_topics(const std::vector<std::string>& topics, MHD_Connection* connection_id);

        size_t read_message(MHD_Connection* connection_id, std::string& message);

        void complete_request(MHD_Connection* connection_id);

        comet_manager(const comet_manager&)
        {
        }

        std::map<MHD_Connection*, std::deque<std::string> > q_messages;
        std::map<std::string, std::set<MHD_Connection*> > q_topics;
        std::map<MHD_Connection*, std::set<std::string> > q_subscriptions;
        friend class httpserver::webserver;
};

} //details

} //httpserver

#endif //_COMET_MANAGER_HPP_
