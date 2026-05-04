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
#include "httpserver/detail/http_request_impl.hpp"

#include <memory>
#include <string>
#include <utility>

namespace httpserver {

http_request create_test_request::build() {
    http_request req;

    // Allocate an impl for this test request (connection_ stays null,
    // indicating the test-request path to all MHD-touching accessors).
    req.impl_ = std::make_unique<detail::http_request_impl>();

    req.set_method(_method);
    req.set_path(_path);
    req.set_version(_version);
    req.set_content(_content);

    req.impl_->headers_local = std::move(_headers);
    req.impl_->footers_local = std::move(_footers);
    req.impl_->cookies_local = std::move(_cookies);

    for (auto& [key, values] : _args) {
        for (auto& value : values) {
            req.impl_->unescaped_args[key].push_back(std::move(value));
        }
    }
    req.impl_->args_populated = true;

    if (!_querystring.empty()) {
        req.impl_->querystring = std::move(_querystring);
    }

#ifdef HAVE_BAUTH
    req.impl_->username = std::move(_user);
    req.impl_->password = std::move(_pass);
#endif  // HAVE_BAUTH

#ifdef HAVE_DAUTH
    req.impl_->digested_user = std::move(_digested_user);
#endif  // HAVE_DAUTH

    req.impl_->requestor_ip = std::move(_requestor);
    req.impl_->requestor_port_local = _requestor_port;

#ifdef HAVE_GNUTLS
    req.impl_->tls_enabled_local = _tls_enabled;
#endif  // HAVE_GNUTLS

    return req;
}

}  // namespace httpserver
