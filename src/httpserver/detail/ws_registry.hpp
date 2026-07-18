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

// WebSocket handler registry collaborator. Internal header; only reachable
// when compiling libhttpserver translation units. NOT part of the installed
// surface.
#if !defined(HTTPSERVER_COMPILATION)
#error "ws_registry.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_WS_REGISTRY_HPP_
#define SRC_HTTPSERVER_DETAIL_WS_REGISTRY_HPP_

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>

namespace httpserver {

class websocket_handler;

namespace detail {

// Owns the URL -> websocket_handler map and its mutex — the whole of the
// webserver's websocket-registration state, extracted from webserver_impl.
// webserver::{register,unregister}_ws_resource mutate it; the dispatch-side
// upgrade path resolves a handler via find(); start() consults empty() to
// decide MHD_ALLOW_UPGRADE. This type never dereferences websocket_handler
// (it only stores/erases/copies shared_ptrs), so it is feature-independent
// and compiles even on HAVE_WEBSOCKET-off builds.
class ws_registry {
 public:
    // Register @p handler at @p url_key. Returns false if a handler is
    // already present at that key (the caller surfaces the collision), true
    // on insert. Takes the write lock.
    bool try_register(std::string url_key,
                      std::shared_ptr<websocket_handler> handler);

    // Erase any handler registered at @p url_key. Takes the write lock.
    void unregister(const std::string& url_key);

    // Return a shared_ptr copy of the handler registered at @p url, or
    // nullptr if none. The copy keeps the handler alive across an MHD
    // upgrade even if a concurrent unregister races to drop the slot.
    // Takes the read lock.
    [[nodiscard]] std::shared_ptr<websocket_handler> find(
        const std::string& url) const;

    // True iff no handler is registered. Takes the read lock.
    [[nodiscard]] bool empty() const;

 private:
    mutable std::shared_mutex mutex_;
    std::map<std::string, std::shared_ptr<websocket_handler>> handlers_;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_WS_REGISTRY_HPP_
