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
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-097 Step 5 (Check A): a consumer including the v3 semantic
// umbrella <httpserver/http.hpp> — and each sub-header directly —
// without HTTPSERVER_COMPILATION (or any other build/TLS
// configuration macro) must compile cleanly. This proves the v3
// headers are self-contained and compile identically in TLS-on and
// TLS-off installs. The test passes by virtue of compiling and
// linking; main() asserts nothing.
#include <httpserver/http.hpp>

#include <httpserver/http/fields.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/outcome.hpp>
#include <httpserver/http/protocol.hpp>
#include <httpserver/http/request_head.hpp>

// Touch every public type once so a missing declaration is a link (not
// just a compile) failure.
int use_v3_semantic_types() {
    httpserver::http::outcome o{httpserver::http::outcome_code::timeout, "t"};
    const auto p = httpserver::http::parse("HTTP/1.1");
    const auto m = httpserver::http::method::parse("GET");

    httpserver::http::fields f;
    f.append("X", "1");

    httpserver::http::request_head head;
    head.request_method = m.value();
    head.request_protocol = p.value_or(httpserver::http::protocol::http_1_0);
    head.head_fields = f;
    head.raw_target = "/";
    head.route_path = "/";

    return (o.ok() || f.empty() || head.route_path.empty()) ? 1 : 0;
}

int main() { return 0; }
