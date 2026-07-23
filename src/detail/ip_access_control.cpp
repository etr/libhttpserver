/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

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

#include "httpserver/detail/ip_access_control.hpp"

#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>

#include "httpserver/ip_representation.hpp"

namespace httpserver {
namespace detail {

using http::ip_representation;

void ip_access_control::insert_wildcard_aware(
        std::set<ip_representation>& list, const ip_representation& t_ip) {
    auto it = list.find(t_ip);
    if (it != list.end() && t_ip.weight() < it->weight()) {
        list.erase(it);
    }
    list.insert(t_ip);
}

void ip_access_control::deny(std::string_view ip) {
    std::unique_lock deny_lock(deny_list_mutex_);
    insert_wildcard_aware(deny_list_, ip_representation{std::string{ip}});
}

void ip_access_control::remove_denied(std::string_view ip) {
    std::unique_lock deny_lock(deny_list_mutex_);
    deny_list_.erase(ip_representation{std::string{ip}});
}

void ip_access_control::allow(std::string_view ip) {
    std::unique_lock allow_lock(allow_list_mutex_);
    insert_wildcard_aware(allow_list_, ip_representation{std::string{ip}});
}

void ip_access_control::remove_allowed(std::string_view ip) {
    std::unique_lock allow_lock(allow_list_mutex_);
    allow_list_.erase(ip_representation{std::string{ip}});
}

ip_access_control::membership ip_access_control::classify(
        const ip_representation& peer) const {
    std::shared_lock deny_lock(deny_list_mutex_);
    std::shared_lock allow_lock(allow_list_mutex_);
    return {deny_list_.count(peer) != 0, allow_list_.count(peer) != 0};
}

}  // namespace detail
}  // namespace httpserver
