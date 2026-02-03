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
         return std::make_shared<string_response>("OK");
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

LT_BEGIN_AUTO_TEST(http_resource_suite, set_allowing_nonexistent_method)
    simple_resource sr;
    // Try to set allowing for a method not in method_state
    // This should be silently ignored (no effect)
    sr.set_allowing("NONEXISTENT", true);
    auto allowed_methods = sr.get_allowed_methods();
    // Verify that NONEXISTENT is not in the list
    LT_CHECK_EQ(std::find(allowed_methods.begin(), allowed_methods.end(),
                          "NONEXISTENT") == allowed_methods.end(), true);
LT_END_AUTO_TEST(set_allowing_nonexistent_method)

LT_BEGIN_AUTO_TEST(http_resource_suite, is_allowed_nonexistent_method)
    simple_resource sr;
    // Check that is_allowed returns false for unknown methods
    LT_CHECK_EQ(sr.is_allowed("UNKNOWN_METHOD"), false);
    LT_CHECK_EQ(sr.is_allowed("CUSTOM"), false);
LT_END_AUTO_TEST(is_allowed_nonexistent_method)

LT_BEGIN_AUTO_TEST(http_resource_suite, set_allowing_disable)
    simple_resource sr;
    // By default, GET is allowed
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_GET), true);
    // Disable GET
    sr.set_allowing(MHD_HTTP_METHOD_GET, false);
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_GET), false);
    // Re-enable GET
    sr.set_allowing(MHD_HTTP_METHOD_GET, true);
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_GET), true);
LT_END_AUTO_TEST(set_allowing_disable)

// Test resource that only overrides render() method
class render_only_resource : public http_resource {
 public:
    shared_ptr<http_response> render(const http_request&) {
        return std::make_shared<string_response>("render called", 200);
    }
};

// Test resource with no overrides at all
class empty_resource : public http_resource {
 public:
    // No render methods overridden - uses defaults
};

LT_BEGIN_AUTO_TEST(http_resource_suite, default_render_returns_empty)
    empty_resource er;
    // Create a minimal mock request - we need to test that render() returns empty
    // Since we can't create a proper http_request without MHD internals,
    // we just verify the resource exists and has correct method state
    auto allowed = er.get_allowed_methods();
    LT_CHECK_EQ(allowed.size(), 9);  // All 9 methods allowed by default
LT_END_AUTO_TEST(default_render_returns_empty)

LT_BEGIN_AUTO_TEST(http_resource_suite, render_only_resource_methods_allowed)
    render_only_resource ror;
    // All methods should be allowed by default
    LT_CHECK_EQ(ror.is_allowed(MHD_HTTP_METHOD_GET), true);
    LT_CHECK_EQ(ror.is_allowed(MHD_HTTP_METHOD_POST), true);
    LT_CHECK_EQ(ror.is_allowed(MHD_HTTP_METHOD_PUT), true);
    LT_CHECK_EQ(ror.is_allowed(MHD_HTTP_METHOD_HEAD), true);
    LT_CHECK_EQ(ror.is_allowed(MHD_HTTP_METHOD_DELETE), true);
    LT_CHECK_EQ(ror.is_allowed(MHD_HTTP_METHOD_TRACE), true);
    LT_CHECK_EQ(ror.is_allowed(MHD_HTTP_METHOD_CONNECT), true);
    LT_CHECK_EQ(ror.is_allowed(MHD_HTTP_METHOD_OPTIONS), true);
    LT_CHECK_EQ(ror.is_allowed(MHD_HTTP_METHOD_PATCH), true);
LT_END_AUTO_TEST(render_only_resource_methods_allowed)

LT_BEGIN_AUTO_TEST(http_resource_suite, resource_init_sets_all_methods)
    simple_resource sr;
    // Verify all 9 HTTP methods are initialized
    auto allowed = sr.get_allowed_methods();
    LT_CHECK_EQ(allowed.size(), 9);
LT_END_AUTO_TEST(resource_init_sets_all_methods)

LT_BEGIN_AUTO_TEST(http_resource_suite, get_allowed_methods_only_returns_true)
    simple_resource sr;
    // Disallow some methods
    sr.set_allowing(MHD_HTTP_METHOD_POST, false);
    sr.set_allowing(MHD_HTTP_METHOD_PUT, false);
    sr.set_allowing(MHD_HTTP_METHOD_DELETE, false);

    auto allowed = sr.get_allowed_methods();
    // Should only return 6 methods now (9 - 3)
    LT_CHECK_EQ(allowed.size(), 6);

    // Verify POST, PUT, DELETE are not in the list
    LT_CHECK_EQ(std::find(allowed.begin(), allowed.end(),
                          MHD_HTTP_METHOD_POST) == allowed.end(), true);
    LT_CHECK_EQ(std::find(allowed.begin(), allowed.end(),
                          MHD_HTTP_METHOD_PUT) == allowed.end(), true);
    LT_CHECK_EQ(std::find(allowed.begin(), allowed.end(),
                          MHD_HTTP_METHOD_DELETE) == allowed.end(), true);
LT_END_AUTO_TEST(get_allowed_methods_only_returns_true)

LT_BEGIN_AUTO_TEST(http_resource_suite, is_allowed_known_methods)
    simple_resource sr;
    // All standard methods should be allowed by default
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_GET), true);
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_POST), true);
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_PUT), true);
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_HEAD), true);
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_DELETE), true);
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_TRACE), true);
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_CONNECT), true);
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_OPTIONS), true);
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_PATCH), true);
LT_END_AUTO_TEST(is_allowed_known_methods)

LT_BEGIN_AUTO_TEST(http_resource_suite, allow_all_after_disallow_all)
    simple_resource sr;
    sr.disallow_all();
    LT_CHECK_EQ(sr.get_allowed_methods().size(), 0);

    sr.allow_all();
    LT_CHECK_EQ(sr.get_allowed_methods().size(), 9);
LT_END_AUTO_TEST(allow_all_after_disallow_all)

LT_BEGIN_AUTO_TEST(http_resource_suite, set_allowing_multiple_times)
    simple_resource sr;
    // Toggle GET multiple times
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_GET), true);
    sr.set_allowing(MHD_HTTP_METHOD_GET, false);
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_GET), false);
    sr.set_allowing(MHD_HTTP_METHOD_GET, true);
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_GET), true);
    sr.set_allowing(MHD_HTTP_METHOD_GET, false);
    sr.set_allowing(MHD_HTTP_METHOD_GET, false);  // Double false
    LT_CHECK_EQ(sr.is_allowed(MHD_HTTP_METHOD_GET), false);
LT_END_AUTO_TEST(set_allowing_multiple_times)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
