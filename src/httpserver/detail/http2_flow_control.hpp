/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "http2_flow_control.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP2_FLOW_CONTROL_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP2_FLOW_CONTROL_HPP_
#include <cstdint>
namespace httpserver::detail {
// SETTINGS may leave a stream send window negative. DATA never may.
struct http2_window {
    std::int64_t available = 65535;
    bool debit(std::uint32_t n) {
        if (available < n) return false;
        available -= n;
        return true;
    }
    // Empty terminal DATA does not require positive credit (RFC 9113 6.9.1).
    bool debit_data(std::uint32_t n, bool end) { return (n == 0 && end) || debit(n); }
    bool increase(std::uint32_t n) {
        if (!n || n > 0x7fffffff || available > 0x7fffffff - static_cast<std::int64_t>(n)) return false;
        available += n;
        return true;
    }
    bool adjust(std::int64_t delta) {
        if (available + delta > 0x7fffffff || available + delta < -0x7fffffff) return false;
        available += delta;
        return true;
    }
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP2_FLOW_CONTROL_HPP_
