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

#include <microhttpd.h>

#include "./httpserver.hpp"
#include "httpserver/details/modded_request.hpp"

#include "./littletest.hpp"

// webserver::post_iterator (the MHD post-processor callback) is a private
// static member, so this test cannot name it directly. Rather than the
// `#define private public` hack -- which breaks libstdc++'s <sstream> when
// it is pulled in transitively under the redefined keyword -- we use the
// explicit-instantiation access loophole: the usual access checks are not
// applied to the names that appear in an explicit template instantiation
// ([temp.spec]/6). The filler<>::instance object below runs its constructor
// at dynamic-initialisation time (before main) and stashes the captured
// pointer into post_iterator_access::ptr. No shipped header is modified and
// no language keyword is redefined, so the technique is portable across
// libstdc++ and libc++.
namespace {

struct post_iterator_access {
    using fn_t = MHD_Result (*)(void*, enum MHD_ValueKind, const char*,
                                const char*, const char*, const char*,
                                const char*, uint64_t, size_t);
    static fn_t ptr;
};
post_iterator_access::fn_t post_iterator_access::ptr = nullptr;

template <post_iterator_access::fn_t Value>
struct filler {
    filler() { post_iterator_access::ptr = Value; }
    static filler instance;
};
template <post_iterator_access::fn_t Value>
filler<Value> filler<Value>::instance;

// The template argument names the private member; access checking does not
// apply here, so this is well-formed.
template struct filler<&httpserver::webserver::post_iterator>;

// Invoke the captured callback for the no-file form-arg path.
MHD_Result feed(httpserver::details::modded_request* mr, const char* key,
                const char* data, uint64_t off, size_t size) {
    return post_iterator_access::ptr(
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
// callback). The guard must instead accept and silently skip the chunk,
// returning before any field name is needed -- so dhr is deliberately left
// null here: a correct guard never reaches the std::string construction nor
// the dhr dereference. Without the guard, std::string(nullptr) throws.
LT_BEGIN_AUTO_TEST(post_iterator_null_key_suite, null_key_continuation_does_not_throw)
    httpserver::details::modded_request mr{};
    MHD_Result r = MHD_NO;
    LT_CHECK_NOTHROW(r = feed(&mr, /*key=*/nullptr, "value", /*off=*/5, 5));
    // MHD_YES keeps the request alive; MHD_NO would abort it.
    LT_CHECK_EQ(r, MHD_YES);
LT_END_AUTO_TEST(null_key_continuation_does_not_throw)

// Same guard on the initial-chunk path (off == 0). MHD should not normally
// hand us a null key here, but the guard is unconditional, so pin it.
LT_BEGIN_AUTO_TEST(post_iterator_null_key_suite, null_key_initial_does_not_throw)
    httpserver::details::modded_request mr{};
    MHD_Result r = MHD_NO;
    LT_CHECK_NOTHROW(r = feed(&mr, /*key=*/nullptr, "value", /*off=*/0, 5));
    LT_CHECK_EQ(r, MHD_YES);
LT_END_AUTO_TEST(null_key_initial_does_not_throw)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
