/*
     This file is part of libhttpserver
     Copyright (C) 2021 Alexander Dahl

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

#include <microhttpd.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using std::shared_ptr;
using std::sort;
using std::string;
using std::vector;

using httpserver::http_request;
using httpserver::http_resource;
using httpserver::http_response;
using httpserver::string_response;

class simple_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<string_response>(new string_response("OK"));
     }
};

LT_BEGIN_SUITE(http_resource_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(http_resource_suite)

LT_BEGIN_AUTO_TEST(http_resource_suite, disallow_all_methods)
    simple_resource sr;
    sr.disallow_all();
    auto allowed_methods = sr.get_allowed_methods();
    LT_CHECK_EQ(allowed_methods.size(), 0);
LT_END_AUTO_TEST(disallow_all_methods)

LT_BEGIN_AUTO_TEST(http_resource_suite, allow_some_methods)
    simple_resource sr;
    sr.disallow_all();
    sr.set_allowing(MHD_HTTP_METHOD_GET, true);
    sr.set_allowing(MHD_HTTP_METHOD_POST, true);
    auto allowed_methods = sr.get_allowed_methods();
    LT_CHECK_EQ(allowed_methods.size(), 2);
    // elements in http_resource::method_state are sorted (std::map)
    vector<string> some_methods{MHD_HTTP_METHOD_GET, MHD_HTTP_METHOD_POST};
    sort(some_methods.begin(), some_methods.end());
    LT_CHECK_COLLECTIONS_EQ(allowed_methods.cbegin(), allowed_methods.cend(),
            some_methods.cbegin())
LT_END_AUTO_TEST(allow_some_methods)

LT_BEGIN_AUTO_TEST(http_resource_suite, allow_all_methods)
    simple_resource sr;
    sr.allow_all();
    auto allowed_methods = sr.get_allowed_methods();
    // elements in http_resource::method_state are sorted (std::map)
    vector<string> all_methods{MHD_HTTP_METHOD_GET, MHD_HTTP_METHOD_POST,
            MHD_HTTP_METHOD_PUT, MHD_HTTP_METHOD_HEAD, MHD_HTTP_METHOD_DELETE,
            MHD_HTTP_METHOD_TRACE, MHD_HTTP_METHOD_CONNECT,
            MHD_HTTP_METHOD_OPTIONS, MHD_HTTP_METHOD_PATCH};
    sort(all_methods.begin(), all_methods.end());
    LT_CHECK_COLLECTIONS_EQ(allowed_methods.cbegin(), allowed_methods.cend(),
            all_methods.cbegin())
LT_END_AUTO_TEST(allow_all_methods)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
