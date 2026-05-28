/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-049 review cleanup (finding #39 / test-quality-reviewer):
// Shared test utility for capturing log_error output in integration tests.
// Extracted from hooks_handler_exception_fallback_to_hardcoded_500.cpp and
// hooks_handler_exception_user_handler_throws_continues_chain.cpp, where
// identical log_capture structs were duplicated verbatim.
//
// Usage:
//   log_capture cap;
//   auto logger = [&cap](const std::string& msg) { cap.append(msg); };
//   webserver ws{create_webserver(PORT).log_error(logger)};
//   // ... run test ...
//   std::string buf = cap.read();

#ifndef TEST_INTEG_LOG_CAPTURE_HPP_
#define TEST_INTEG_LOG_CAPTURE_HPP_

#include <mutex>
#include <string>

struct log_capture {
    std::mutex m;
    std::string buf;

    void append(const std::string& msg) {
        std::lock_guard<std::mutex> g(m);
        buf.append(msg).append("\n");
    }

    std::string read() {
        std::lock_guard<std::mutex> g(m);
        return buf;
    }
};

#endif  // TEST_INTEG_LOG_CAPTURE_HPP_
