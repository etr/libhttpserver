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

// White-box unit test for issue #375. webserver::post_iterator (the MHD
// post-processor callback) and http_request's constructor / set_arg are
// private, so this TU drives them directly to exercise the exact code path
// MHD hits. The standard-library and libmicrohttpd headers are pulled in
// first, normally; the `private -> public` redefinition that follows then
// only affects libhttpserver's own class declarations. Member order is
// unchanged, so object layout matches the normally-compiled library
// (only access labels differ).
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <microhttpd.h>

#define private public
#include "./httpserver.hpp"
#include "httpserver/details/modded_request.hpp"
#undef private

#include "./littletest.hpp"

namespace {
// Build a modded_request owning a fresh http_request (its arg cache is
// default-initialised, so set_arg / grow_last_arg / get_arg work without a
// live MHD connection). The no-file form-arg branch never reads mr.ws, so
// it is left null.
httpserver::details::modded_request make_request() {
    httpserver::details::modded_request mr;
    mr.dhr = std::unique_ptr<httpserver::http_request>(
        new httpserver::http_request());
    return mr;
}

MHD_Result feed(httpserver::details::modded_request* mr, const char* key,
                const char* data, uint64_t off, size_t size) {
    return httpserver::webserver::post_iterator(
        mr, MHD_POSTDATA_KIND, key, /*filename=*/nullptr,
        /*content_type=*/nullptr, /*transfer_encoding=*/nullptr,
        data, off, size);
}
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
    httpserver::details::modded_request mr = make_request();
    MHD_Result r = MHD_NO;
    LT_CHECK_NOTHROW(r = feed(&mr, /*key=*/nullptr, "value", /*off=*/5, 5));
    // MHD_YES keeps the request alive; MHD_NO would abort it.
    LT_CHECK_EQ(r, MHD_YES);
    // Nothing was stored: there was no field name to key the value under.
    LT_CHECK_EQ(mr.dhr->get_args().size(), static_cast<size_t>(0));
LT_END_AUTO_TEST(null_key_continuation_does_not_throw)

// Same guard on the initial-chunk path (off == 0). MHD should not normally
// hand us a null key here, but the guard is unconditional, so pin it.
LT_BEGIN_AUTO_TEST(post_iterator_null_key_suite, null_key_initial_does_not_throw)
    httpserver::details::modded_request mr = make_request();
    MHD_Result r = MHD_NO;
    LT_CHECK_NOTHROW(r = feed(&mr, /*key=*/nullptr, "value", /*off=*/0, 5));
    LT_CHECK_EQ(r, MHD_YES);
    LT_CHECK_EQ(mr.dhr->get_args().size(), static_cast<size_t>(0));
LT_END_AUTO_TEST(null_key_initial_does_not_throw)

// Happy path: a non-null key on the initial chunk stores the value under
// that field name, proving the guard did not regress normal form handling.
LT_BEGIN_AUTO_TEST(post_iterator_null_key_suite, valid_key_stores_arg)
    httpserver::details::modded_request mr = make_request();
    MHD_Result r = feed(&mr, "field", "value", /*off=*/0, 5);
    LT_CHECK_EQ(r, MHD_YES);
    LT_CHECK_EQ(std::string(mr.dhr->get_arg_flat("field")),
                std::string("value"));
LT_END_AUTO_TEST(valid_key_stores_arg)

// Continuation chunk with a (repeated) non-null key appends to the value
// MHD started on the first call - the legitimate large-field split that
// commit 1b5fe8f (issue #337) introduced grow_last_arg to handle.
LT_BEGIN_AUTO_TEST(post_iterator_null_key_suite, valid_key_continuation_appends)
    httpserver::details::modded_request mr = make_request();
    LT_CHECK_EQ(feed(&mr, "field", "hel", /*off=*/0, 3), MHD_YES);
    LT_CHECK_EQ(feed(&mr, "field", "lo", /*off=*/3, 2), MHD_YES);
    LT_CHECK_EQ(std::string(mr.dhr->get_arg_flat("field")),
                std::string("hello"));
LT_END_AUTO_TEST(valid_key_continuation_appends)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
