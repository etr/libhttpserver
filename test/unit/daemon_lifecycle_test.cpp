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

// Unit tests for detail::daemon_lifecycle's option-array / start-flag
// builders, driven directly against a constructed instance -- no MHD
// daemon is ever started (start()/stop() are NOT exercised here; those
// remain covered by the integ-level ws_start_stop.cpp / daemon_info.cpp
// tests, which only reach the class through webserver's orchestration
// layer).
//
// daemon_lifecycle was extracted from webserver_impl (commit e6af49f)
// and had no test targeting the class boundary: build_mhd_option_array's
// per-flag composition and compose_{start,transport,runtime}_flags were
// only reachable indirectly through a running daemon. Every
// httpserver::webserver owns exactly one daemon_lifecycle by value
// (impl_->daemon_, constructed with `this` as the owner_ back-pointer),
// so these tests reach that already-constructed instance via
// webserver_test_access rather than constructing a second, ownerless one
// -- the class requires a non-null webserver_impl* owner_ to read the
// config bag, so "directly against a constructed daemon_lifecycle" means
// the one every webserver already carries.

#include <vector>

#include "./httpserver.hpp"
#include "./httpserver/detail/webserver_impl.hpp"
#include "./littletest.hpp"

namespace ht = httpserver;

LT_BEGIN_SUITE(daemon_lifecycle_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(daemon_lifecycle_suite)

// ----- compose_transport_flags ---------------------------------------------

LT_BEGIN_AUTO_TEST(daemon_lifecycle_suite,
                   compose_transport_flags_defaults_to_zero)
    ht::webserver ws{ht::create_webserver(0)};
    auto* impl = ht::webserver_test_access::impl(ws);
    LT_CHECK_EQ(impl->daemon_.compose_transport_flags(), 0);
LT_END_AUTO_TEST(compose_transport_flags_defaults_to_zero)

LT_BEGIN_AUTO_TEST(daemon_lifecycle_suite,
                   compose_transport_flags_sets_ipv6_bit)
    ht::webserver ws{ht::create_webserver(0).use_ipv6(true)};
    auto* impl = ht::webserver_test_access::impl(ws);
    const int flags = impl->daemon_.compose_transport_flags();
    LT_CHECK((flags & MHD_USE_IPv6) != 0);
LT_END_AUTO_TEST(compose_transport_flags_sets_ipv6_bit)

LT_BEGIN_AUTO_TEST(daemon_lifecycle_suite,
                   compose_transport_flags_sets_dual_stack_bit)
    ht::webserver ws{ht::create_webserver(0).use_dual_stack(true)};
    auto* impl = ht::webserver_test_access::impl(ws);
    const int flags = impl->daemon_.compose_transport_flags();
    LT_CHECK((flags & MHD_USE_DUAL_STACK) != 0);
LT_END_AUTO_TEST(compose_transport_flags_sets_dual_stack_bit)

// ----- compose_runtime_flags ------------------------------------------------

LT_BEGIN_AUTO_TEST(daemon_lifecycle_suite,
                   compose_runtime_flags_defaults_have_neither_debug_nor_pedantic)
    ht::webserver ws{ht::create_webserver(0)};
    auto* impl = ht::webserver_test_access::impl(ws);
    const int flags = impl->daemon_.compose_runtime_flags();
    LT_CHECK((flags & MHD_USE_DEBUG) == 0);
    LT_CHECK((flags & MHD_USE_PEDANTIC_CHECKS) == 0);
LT_END_AUTO_TEST(compose_runtime_flags_defaults_have_neither_debug_nor_pedantic)

LT_BEGIN_AUTO_TEST(daemon_lifecycle_suite,
                   compose_runtime_flags_sets_debug_and_pedantic_bits)
    ht::webserver ws{ht::create_webserver(0).debug(true).pedantic(true)};
    auto* impl = ht::webserver_test_access::impl(ws);
    const int flags = impl->daemon_.compose_runtime_flags();
    LT_CHECK((flags & MHD_USE_DEBUG) != 0);
    LT_CHECK((flags & MHD_USE_PEDANTIC_CHECKS) != 0);
LT_END_AUTO_TEST(compose_runtime_flags_sets_debug_and_pedantic_bits)

LT_BEGIN_AUTO_TEST(daemon_lifecycle_suite,
                   compose_runtime_flags_sets_suspend_resume_bit_for_deferred)
    ht::webserver ws{ht::create_webserver(0).deferred(true)};
    auto* impl = ht::webserver_test_access::impl(ws);
    const int flags = impl->daemon_.compose_runtime_flags();
    LT_CHECK((flags & MHD_USE_SUSPEND_RESUME) != 0);
LT_END_AUTO_TEST(compose_runtime_flags_sets_suspend_resume_bit_for_deferred)

// ----- compose_start_flags: composition of the three pieces -----------------

LT_BEGIN_AUTO_TEST(daemon_lifecycle_suite,
                   compose_start_flags_ors_start_method_transport_and_runtime)
    ht::webserver ws{ht::create_webserver(0)
                          .start_method(ht::http::http_utils::INTERNAL_SELECT)
                          .use_ipv6(true)
                          .debug(true)};
    auto* impl = ht::webserver_test_access::impl(ws);
    const int flags = impl->daemon_.compose_start_flags();

    LT_CHECK((flags & ht::http::http_utils::INTERNAL_SELECT)
             == ht::http::http_utils::INTERNAL_SELECT);
    LT_CHECK((flags & MHD_USE_IPv6) != 0);
    LT_CHECK((flags & MHD_USE_DEBUG) != 0);
LT_END_AUTO_TEST(compose_start_flags_ors_start_method_transport_and_runtime)

// ----- build_mhd_option_array -----------------------------------------------

LT_BEGIN_AUTO_TEST(daemon_lifecycle_suite,
                   build_mhd_option_array_is_terminated_by_option_end)
    ht::webserver ws{ht::create_webserver(0)};
    auto* impl = ht::webserver_test_access::impl(ws);
    std::vector<MHD_OptionItem> iov;

    impl->daemon_.build_mhd_option_array(iov);

    LT_CHECK(!iov.empty());
    LT_CHECK(iov.back().option == MHD_OPTION_END);
LT_END_AUTO_TEST(build_mhd_option_array_is_terminated_by_option_end)

LT_BEGIN_AUTO_TEST(daemon_lifecycle_suite,
                   build_mhd_option_array_carries_connection_timeout_value)
    ht::webserver ws{ht::create_webserver(0).connection_timeout(42)};
    auto* impl = ht::webserver_test_access::impl(ws);
    std::vector<MHD_OptionItem> iov;

    impl->daemon_.build_mhd_option_array(iov);

    bool found = false;
    for (const auto& item : iov) {
        if (item.option == MHD_OPTION_CONNECTION_TIMEOUT) {
            LT_CHECK_EQ(item.value, static_cast<intptr_t>(42));
            found = true;
        }
    }
    LT_CHECK(found);
LT_END_AUTO_TEST(build_mhd_option_array_carries_connection_timeout_value)

// add_base_mhd_options only pushes the thread-pool-size / connection-
// limit / memory-limit / per-IP-limit / stack-size options when their
// config field is non-zero. Pin the omission (default config: all zero).
LT_BEGIN_AUTO_TEST(daemon_lifecycle_suite,
                   build_mhd_option_array_omits_zero_valued_optional_fields)
    ht::webserver ws{ht::create_webserver(0)};
    auto* impl = ht::webserver_test_access::impl(ws);
    std::vector<MHD_OptionItem> iov;

    impl->daemon_.build_mhd_option_array(iov);

    for (const auto& item : iov) {
        LT_CHECK(item.option != MHD_OPTION_THREAD_POOL_SIZE);
        LT_CHECK(item.option != MHD_OPTION_CONNECTION_LIMIT);
        LT_CHECK(item.option != MHD_OPTION_CONNECTION_MEMORY_LIMIT);
        LT_CHECK(item.option != MHD_OPTION_PER_IP_CONNECTION_LIMIT);
        LT_CHECK(item.option != MHD_OPTION_THREAD_STACK_SIZE);
    }
LT_END_AUTO_TEST(build_mhd_option_array_omits_zero_valued_optional_fields)

// The counterpart: a non-zero max_threads must surface as
// MHD_OPTION_THREAD_POOL_SIZE carrying that value.
LT_BEGIN_AUTO_TEST(daemon_lifecycle_suite,
                   build_mhd_option_array_carries_max_threads_value)
    ht::webserver ws{ht::create_webserver(0).max_threads(4)};
    auto* impl = ht::webserver_test_access::impl(ws);
    std::vector<MHD_OptionItem> iov;

    impl->daemon_.build_mhd_option_array(iov);

    bool found = false;
    for (const auto& item : iov) {
        if (item.option == MHD_OPTION_THREAD_POOL_SIZE) {
            LT_CHECK_EQ(item.value, static_cast<intptr_t>(4));
            found = true;
        }
    }
    LT_CHECK(found);
LT_END_AUTO_TEST(build_mhd_option_array_carries_max_threads_value)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
