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

#include <memory>
#include <string>
#include <utility>

#include "httpserver/detail/http_request_impl.hpp"
#include "httpserver/string_utilities.hpp"

namespace httpserver {

namespace {

// TASK-016: heap-deleter used for the test-request impl. The live-request
// constructor in http_request.cpp uses an internal-linkage delete_impl_heap
// of the same shape; reproducing it here avoids exposing it across TUs.
// Both implementations call operator delete, matching v1 lifetime exactly.
void delete_test_impl_heap(detail::http_request_impl* p) noexcept {
    delete p;
}

}  // namespace

http_request create_test_request::build() {
    http_request req;

    // Allocate an impl for this test request (connection_ stays null,
    // indicating the test-request path to all MHD-touching accessors).
    // Heap-allocated; uses the heap deleter so destruction frees via
    // operator delete -- same lifetime as v1.
    req.impl_.reset(new detail::http_request_impl());
    req.impl_.get_deleter().fn = &delete_test_impl_heap;

    req.set_method(string_utilities::to_upper_copy(_method));
    req.set_path(_path);
    req.set_version(_version);
    req.set_content(_content);

    req.impl_->headers_local = std::move(_headers);
    req.impl_->footers_local = std::move(_footers);
    req.impl_->cookies_local = std::move(_cookies);

    // Test-request path: the impl was default-constructed (no arena), so
    // its pmr-aware members fall back to std::pmr::get_default_resource()
    // -- equivalent to plain heap allocation. Cross-allocator move is not
    // available, so we copy element-wise via .assign(ptr, len) /
    // emplace_back(view, alloc).
    auto args_alloc = req.impl_->unescaped_args.get_allocator();
    for (auto& [key, values] : _args) {
        auto it = req.impl_->unescaped_args.find(std::string_view(key));
        if (it == req.impl_->unescaped_args.end()) {
            std::pmr::vector<std::pmr::string> empty(args_alloc);
            auto inserted = req.impl_->unescaped_args.emplace(
                std::pmr::string(key.data(), key.size(), args_alloc),
                std::move(empty));
            it = inserted.first;
        }
        for (auto& value : values) {
            it->second.emplace_back(value.data(), value.size());
        }
    }
    req.impl_->args_populated = true;

    if (!_querystring.empty()) {
        req.impl_->querystring.assign(_querystring.data(), _querystring.size());
    }

#ifdef HAVE_BAUTH
    req.impl_->username.assign(_user.data(), _user.size());
    req.impl_->password.assign(_pass.data(), _pass.size());
#endif  // HAVE_BAUTH

#ifdef HAVE_DAUTH
    req.impl_->digested_user.assign(_digested_user.data(),
                                    _digested_user.size());
#endif  // HAVE_DAUTH

    req.impl_->requestor_ip.assign(_requestor.data(), _requestor.size());
    req.impl_->requestor_port_local = _requestor_port;

#ifdef HAVE_GNUTLS
    req.impl_->tls_enabled_local = _tls_enabled;
#endif  // HAVE_GNUTLS

    return req;
}

}  // namespace httpserver
