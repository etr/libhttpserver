// Copyright (C) 2026 Sebastiano Merlino
// SPDX-License-Identifier: LGPL-2.1-or-later
// Exercise the real diagnostic wait helper against offline native-call fakes.
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <climits>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include <httpserver/server/readiness.hpp>
#define _WIN32
#include "./host_poll.hpp"

int main(int argc, char**) {
    namespace srv = httpserver::server;
    srv::interest_snapshot snapshot;
    snapshot.sockets.push_back({srv::socket_key(42), 73,
        {UINT64_C(0x100000001), srv::native_handle_kind::winsock_socket, true}, true, false});
    bool threw = false;
    try {
        v3_host::wait(snapshot);
    } catch (const std::runtime_error& error) {
        threw = std::strcmp(error.what(), "WSAPoll failed") == 0;
    }
    if (!threw || fake_probes != 1) return 1;
    if (argc > 1) return 0;  // Simulate exit before the final join/flush.
    fake_poll_error = WSAEINTR;
    try {
        v3_host::wait(snapshot);
    } catch (...) {
        return 2;
    }
    v3_host::capture_event("main_stopping_before");
    v3_host::capture_dispatch("dispatch_begin");
    v3_host::capture_event("request_stop_begin");
    v3_host::capture_dispatch("dispatch_end");
    v3_host::capture_flush();
}
