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

#include "httpserver/detail/dispatch_util.hpp"

#include <string>
#include <string_view>

#include "httpserver/create_webserver.hpp"

namespace httpserver {
namespace detail {

void log_dispatch_error(const webserver_config& config,
                        std::string_view msg) noexcept {
    if (config.log_error == nullptr) {
        return;
    }
    // A misbehaving user logger must not poison the catch from inside the
    // catch. Swallow any exception it throws; we have no recovery beyond
    // dropping the log line.
    try {
        config.log_error(std::string(msg));
    } catch (...) {
        // Intentionally suppressed.
    }
}

}  // namespace detail
}  // namespace httpserver
