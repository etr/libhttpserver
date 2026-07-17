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

#include <string>

#include "./httpserver.hpp"
#include "httpserver/create_test_request.hpp"
#include "httpserver/detail/modded_request.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include "./littletest.hpp"

// post_iterator is the MHD post-processor callback defined as a static
// member of webserver_impl (it lives in the dispatch helper header under
// the class's public: section). The no-file branch funnels into
// handle_post_form_arg, which is the function issue #375 hardens. We drive
// the public callback so the test exercises the same entry point MHD does.
//
// modded_request::request is a unique_ptr<http_request> with the default
// deleter, and http_request's move constructor is private (friended only
// to create_test_request), so the request cannot be make_unique'd. Instead
// we own an http_request on the stack -- built via the public test builder
// using guaranteed copy elision -- and point request at it, detaching request
// before destruction so the stack object is not double-freed.
namespace {
struct request_fixture {
    httpserver::http_request req =
        httpserver::create_test_request().build();
    httpserver::detail::modded_request mr;

    request_fixture() {
        // ws is never read on the no-file form-arg path, so leave it null.
        mr.request.reset(&req);
    }

    ~request_fixture() {
        // Detach before mr (and its unique_ptr) is destroyed: req is
        // stack-owned and must not be deleted through request.
        mr.request.release();
    }

    MHD_Result feed(const char* key, const char* data, uint64_t off,
                    size_t size) {
        return httpserver::detail::webserver_impl::post_iterator(
            &mr, MHD_POSTDATA_KIND, key, /*filename=*/nullptr,
            /*content_type=*/nullptr, /*transfer_encoding=*/nullptr,
            data, off, size);
    }
};
}  // namespace

LT_BEGIN_SUITE(post_iterator_null_key_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(post_iterator_null_key_suite)

// Regression test for issue #375: MHD may invoke the post iterator with a
// null key on a continuation chunk (off > 0) because the field name was
// only supplied on the first call. The previous implementation passed the
// raw key pointer into std::string, which throws std::logic_error on null
// and aborts the process via std::terminate (the throw escapes a C
// callback). The guard must instead accept and silently skip the chunk.
LT_BEGIN_AUTO_TEST(post_iterator_null_key_suite, null_key_continuation_does_not_throw)
    request_fixture f;
    MHD_Result r = MHD_NO;
    LT_CHECK_NOTHROW(r = f.feed(/*key=*/nullptr, "value", /*off=*/5, 5));
    // MHD_YES keeps the request alive; MHD_NO would abort it.
    LT_CHECK_EQ(r, MHD_YES);
    // Nothing was stored: there was no field name to key the value under.
    LT_CHECK_EQ(f.mr.request->get_args().size(), static_cast<size_t>(0));
LT_END_AUTO_TEST(null_key_continuation_does_not_throw)

// Same guard on the initial-chunk path (off == 0). MHD should not normally
// hand us a null key here, but the guard is unconditional, so pin it.
LT_BEGIN_AUTO_TEST(post_iterator_null_key_suite, null_key_initial_does_not_throw)
    request_fixture f;
    MHD_Result r = MHD_NO;
    LT_CHECK_NOTHROW(r = f.feed(/*key=*/nullptr, "value", /*off=*/0, 5));
    LT_CHECK_EQ(r, MHD_YES);
    LT_CHECK_EQ(f.mr.request->get_args().size(), static_cast<size_t>(0));
LT_END_AUTO_TEST(null_key_initial_does_not_throw)

// Happy path: a non-null key on the initial chunk stores the value under
// that field name, proving the guard did not regress normal form handling.
LT_BEGIN_AUTO_TEST(post_iterator_null_key_suite, valid_key_stores_arg)
    request_fixture f;
    MHD_Result r = f.feed("field", "value", /*off=*/0, 5);
    LT_CHECK_EQ(r, MHD_YES);
    LT_CHECK_EQ(std::string(f.mr.request->get_arg_flat("field")),
                std::string("value"));
LT_END_AUTO_TEST(valid_key_stores_arg)

// Continuation chunk with a (repeated) non-null key appends to the value
// MHD started on the first call - the legitimate large-field split that
// commit 1b5fe8f (issue #337) introduced grow_last_arg to handle.
LT_BEGIN_AUTO_TEST(post_iterator_null_key_suite, valid_key_continuation_appends)
    request_fixture f;
    LT_CHECK_EQ(f.feed("field", "hel", /*off=*/0, 3), MHD_YES);
    LT_CHECK_EQ(f.feed("field", "lo", /*off=*/3, 2), MHD_YES);
    LT_CHECK_EQ(std::string(f.mr.request->get_arg_flat("field")),
                std::string("hello"));
LT_END_AUTO_TEST(valid_key_continuation_appends)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
