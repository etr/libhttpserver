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

#include <errno.h>
#include <iostream>
#include "details/comet_manager.hpp"
#include <sys/time.h>

using namespace std;

namespace httpserver {

namespace details {

comet_manager::comet_manager()
{
}

comet_manager::~comet_manager()
{
}

void comet_manager::send_message_to_topic (const string& topic, const string& message)
{
    map<string, set<MHD_Connection*> >::const_iterator it = this->q_topics.find(topic);
    if (it == this->q_topics.end()) return;

    //copying value guarantees we iterate on a copy. Even if the original set is modified we are safe and so we stay lock free.
    const set<MHD_Connection*> connections = it->second;

    for (set<MHD_Connection*>::const_iterator c_it = connections.begin(); c_it != connections.end(); ++c_it)
    {
        map<MHD_Connection*, deque<string> >::iterator message_queue_it = this->q_messages.find(*c_it);
        if (message_queue_it == this->q_messages.end()) continue;

        message_queue_it->second.push_back(message);

        MHD_resume_connection(*c_it);
    }
}

void comet_manager::register_to_topics (const vector<string>& topics, MHD_Connection* connection_id)
{
    for(vector<string>::const_iterator it = topics.begin(); it != topics.end(); ++it)
    {
        this->q_topics[*it].insert(connection_id); // (1) Can this cause problems in concurrency with (2) ?
    }
    this->q_subscriptions.insert(make_pair(connection_id, set<string>(topics.begin(), topics.end())));
    this->q_messages.insert(make_pair(connection_id, deque<string>()));
}

size_t comet_manager::read_message(MHD_Connection* connection_id, string& message)
{
    if(this->q_messages[connection_id].empty())
    {
        MHD_suspend_connection(connection_id);
        return 0;
    }

    deque<string>& t_deq = this->q_messages[connection_id];
    message.assign(t_deq.front());
    t_deq.pop_front();
    return message.size();
}

void comet_manager::complete_request(MHD_Connection* connection_id)
{
    this->q_messages.erase(connection_id);

    map<MHD_Connection*, set<string> >::iterator topics_it = this->q_subscriptions.find(connection_id);
    if (topics_it == q_subscriptions.end()) return;
    set<string> topics = topics_it->second;

    for(set<string>::const_iterator it = topics.begin(); it != topics.end(); ++it)
    {
        map<string, set<MHD_Connection*> >::iterator connections_it = this->q_topics.find(*it);
        if (connections_it == this->q_topics.end()) continue;

        connections_it->second.erase(connection_id);
        if (connections_it->second.size() == 0) this->q_topics.erase(*it); // (2)
    }
    q_subscriptions.erase(connection_id);
}

} // namespace details

} // namespace httpserver
