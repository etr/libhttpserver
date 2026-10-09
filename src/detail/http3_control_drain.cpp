/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <utility>
#include <httpserver/detail/http3_connection.hpp>
namespace httpserver::detail {
http::outcome http3_connection::begin_goaway(std::uint64_t cutoff) {
    if (error_ || stopped_ || goaway_started_)
        return {http::outcome_code::invalid_state, "HTTP/3 GOAWAY already initiated or terminal"};
    if (cutoff > k_quic_max_integer - 3 || (cutoff & 3))
        return {http::outcome_code::invalid_argument, "Invalid HTTP/3 request cutoff"};
    server::reservation charge;
    auto result = critical_.budget.reserve(server::resource::quic_reassembly_bytes, sizeof(goaway_), charge);
    if (!result.ok()) return result;
    for (unsigned i = 0; i < 2; ++i) {
        auto& frame = goaway_[i];
        frame[0] = std::byte{7};
        auto encoded = encode_quic_varint(i ? cutoff : k_quic_max_integer - 3, std::span(frame).subspan(2));
        frame[1] = static_cast<std::byte>(encoded.consumed);
        goaway_sizes_[i] = 2 + encoded.consumed;
    }
    initial_goaway_end_ = control_prefix_.size() + goaway_sizes_[0];
    final_goaway_end_ = initial_goaway_end_ + goaway_sizes_[1];
    goaway_charge_ = std::move(charge);
    goaway_started_ = true;
    return http::outcome::okay();
}
void http3_connection::acknowledge_control(std::uint64_t through) {
    if (!goaway_started_) return;
    if (through >= initial_goaway_end_ && goaway_positions_[0] == goaway_sizes_[0]) final_goaway_enabled_ = true;
    if (through >= final_goaway_end_ && goaway_positions_[1] == goaway_sizes_[1]) goaway_retired_ = true;
}
}  // namespace httpserver::detail
