/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_SUPPORT_QUIC_INTEROP_FILES_HPP_
#define TEST_SUPPORT_QUIC_INTEROP_FILES_HPP_
#include <map>
#include <memory>
#include <string>
#include <httpserver/detail/quic_flow_control.hpp>
#include <httpserver/detail/quic_recovery.hpp>
namespace hqnet {
namespace hd = httpserver::detail;
// Test-only HTTP/0.9 application. Eight retained 8 KiB chunks per stream; transport owns retransmission.
class files final {
 public:
    explicit files(const std::string& root);
    ~files();
    void receive(hd::quic_stream_state& stream, hd::quic_flow_control& flow);
    void pump(hd::quic_flow_control& flow, hd::quic_recovery& recovery);
    void completed(const hd::quic_information_completion& receipt);
    void cancel(std::uint64_t stream, hd::quic_recovery& recovery);
    std::size_t active_files() const;
 private:
    struct request;
    int root_ = -1;
    std::map<std::uint64_t, std::unique_ptr<request>> requests_;
};
}  // namespace hqnet
#endif  // TEST_SUPPORT_QUIC_INTEROP_FILES_HPP_
