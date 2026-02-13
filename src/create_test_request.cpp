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

#include "httpserver/create_test_request.hpp"

#include <string>
#include <utility>

namespace httpserver {

http_request create_test_request::build() {
    http_request req;

    req.set_method(_method);
    req.set_path(_path);
    req.set_version(_version);
    req.set_content(_content);

    req.headers_local = std::move(_headers);
    req.footers_local = std::move(_footers);
    req.cookies_local = std::move(_cookies);

    for (auto& [key, values] : _args) {
        for (auto& value : values) {
            req.cache->unescaped_args[key].push_back(std::move(value));
        }
    }
    req.cache->args_populated = true;

    if (!_querystring.empty()) {
        req.cache->querystring = std::move(_querystring);
    }

    req.cache->username = std::move(_user);
    req.cache->password = std::move(_pass);

#ifdef HAVE_DAUTH
    req.cache->digested_user = std::move(_digested_user);
#endif  // HAVE_DAUTH

    req.cache->requestor_ip = std::move(_requestor);
    req.requestor_port_local = _requestor_port;

#ifdef HAVE_GNUTLS
    req.tls_enabled_local = _tls_enabled;
#endif  // HAVE_GNUTLS

    return req;
}

}  // namespace httpserver
