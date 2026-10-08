/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <vector>
#include "./io_udp_backend_contract.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace ps = hd::pollsys;
namespace hh = httpserver::http;
using io_udp_contract::udp_socket;
using io_udp_contract::udp_contract;
LT_BEGIN_SUITE(udp_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(udp_suite)
LT_BEGIN_AUTO_TEST(udp_suite, poll_message_metadata_and_lifecycle)
    LT_CHECK(udp_contract<hd::io_poll_backend>());
    LT_CHECK(udp_contract<hd::io_poll_backend>(AF_INET6));
LT_END_AUTO_TEST(poll_message_metadata_and_lifecycle)
LT_BEGIN_AUTO_TEST(udp_suite, native_message_metadata_and_lifecycle)
#if defined(__APPLE__) || defined(__FreeBSD__)
    LT_CHECK(udp_contract<hd::io_kqueue_backend>());
    LT_CHECK(udp_contract<hd::io_kqueue_backend>(AF_INET6));
#elif defined(__linux__)
    LT_CHECK(udp_contract<hd::io_epoll_backend>());
    LT_CHECK(udp_contract<hd::io_epoll_backend>(AF_INET6));
#elif defined(_WIN32)
    LT_CHECK(udp_contract<hd::io_iocp_backend>());
    LT_CHECK(udp_contract<hd::io_iocp_backend>(AF_INET6));
#endif
LT_END_AUTO_TEST(native_message_metadata_and_lifecycle)
LT_BEGIN_AUTO_TEST(udp_suite, external_budget_rearm_and_stale_generation)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    hd::io_poll_backend backend(httpserver::server::loop_mode::external);
    backend.activate_external();
    backend.adopt_datagram(1, udp_socket());
    std::vector<std::shared_ptr<hd::op_state>> states;
    for (std::size_t i = 0; i <= hd::k_udp_pending_operations; ++i) {
        hd::udp_receive_operation receive(owner, 1, 1);
        states.push_back(receive.state());
        receive.submit(backend);
    }
    ex.run_pending();
    LT_CHECK(states.back()->applied());
    LT_CHECK(states.back()->stored_result().code == hh::outcome_code::limit_exceeded);
    const auto interest = backend.interests().sockets.front();
    httpserver::server::readiness_event event{interest.key, interest.generation, true};
    LT_CHECK(backend.dispatch(std::span(&event, 1), std::chrono::steady_clock::now()).ok());
    LT_CHECK_EQ(backend.pending_count(), hd::k_udp_pending_operations);
    event.error = true;
    LT_CHECK(backend.dispatch(std::span(&event, 1), std::chrono::steady_clock::now()).ok());
    LT_CHECK_EQ(backend.pending_count(), hd::k_udp_pending_operations);
    LT_CHECK(backend.native_handle(1) != ps::k_invalid_socket);
    event.error = false;
    LT_CHECK(backend.request_cancel(*states.front()) == hh::outcome_code::ok);
    hd::udp_receive_operation replacement(owner, 1, 1);
    replacement.submit(backend);
    LT_CHECK(!replacement.is_terminal());
    backend.release_connection(1);
    backend.adopt_datagram(1, udp_socket());
    hd::udp_receive_operation fresh(owner, 1, 1);
    fresh.submit(backend);
    LT_CHECK(backend.dispatch(std::span(&event, 1), std::chrono::steady_clock::now()).ok());
    LT_CHECK(!fresh.is_terminal());
    backend.close();
    ex.run_pending();
LT_END_AUTO_TEST(external_budget_rearm_and_stale_generation)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
