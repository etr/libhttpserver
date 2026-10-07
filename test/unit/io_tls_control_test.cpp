/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include <httpserver/detail/io_kqueue_backend.hpp>
#include <httpserver/detail/tls_io_backend.hpp>
#include "./littletest.hpp"
namespace hd = httpserver::detail;
namespace hh = httpserver::http;
LT_BEGIN_SUITE(io_tls_control_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(io_tls_control_suite)
LT_BEGIN_AUTO_TEST(io_tls_control_suite, raw_socket_backends_reject_tls_controls)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    hd::io_poll_backend raw(httpserver::server::loop_mode::external);
    for (bool shutdown : {false, true}) {
        hd::tls_handshake_operation handshake(owner, 0);
        hd::tls_shutdown_operation close(owner, 0);
        auto& op = shutdown ? static_cast<hd::op_handle&>(close) : static_cast<hd::op_handle&>(handshake);
        op.submit(raw);
        ex.run_pending();
        LT_CHECK(op.sequence() != 0);
        LT_CHECK(op.state()->applied());
        LT_CHECK(op.state()->stored_result().code == hh::outcome_code::not_supported);
    }
    raw.close();
    ex.run_pending();
LT_END_AUTO_TEST(raw_socket_backends_reject_tls_controls)

#if defined(__APPLE__) || defined(__FreeBSD__)
LT_BEGIN_AUTO_TEST(io_tls_control_suite, managed_backend_rejects_tls_controls)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    hd::io_kqueue_backend raw;
    hd::tls_handshake_operation op(owner, 0);
    op.submit(raw);
    ex.run_pending();
    LT_CHECK(op.state()->applied());
    LT_CHECK(op.state()->stored_result().code == hh::outcome_code::not_supported);
    LT_CHECK_EQ(raw.pending_count(), std::size_t{0});
    raw.close();
    ex.run_pending();
LT_END_AUTO_TEST(managed_backend_rejects_tls_controls)
#endif
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
