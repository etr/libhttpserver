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

#include "httpserver/detail/ws_registry.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>

namespace httpserver {
namespace detail {

bool ws_registry::try_register(std::string url_key,
                               std::shared_ptr<websocket_handler> handler) {
    std::unique_lock lock(mutex_);
    return handlers_.emplace(std::move(url_key), std::move(handler)).second;
}

void ws_registry::unregister(const std::string& url_key) {
    std::unique_lock lock(mutex_);
    handlers_.erase(url_key);
}

std::shared_ptr<websocket_handler> ws_registry::find(
        const std::string& url) const {
    std::shared_lock lock(mutex_);
    auto it = handlers_.find(url);
    return it == handlers_.end() ? nullptr : it->second;
}

bool ws_registry::empty() const {
    std::shared_lock lock(mutex_);
    return handlers_.empty();
}

}  // namespace detail
}  // namespace httpserver
