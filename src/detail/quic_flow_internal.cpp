/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/quic_flow_internal.hpp>

namespace httpserver::detail {

quic_flow_control::implementation::credit::credit(std::uint64_t initial) : desired(initial), emitted(initial), acknowledged(initial) {}

quic_flow_result flow_result(quic_stream_result result) {
    switch (result.code) {
        case quic_stream_code::ok:
            return {};
        case quic_stream_code::stream_state_error:
            return {quic_flow_code::stream_state_error};
        case quic_stream_code::final_size_error:
            return {quic_flow_code::final_size_error};
        case quic_stream_code::frame_encoding_error:
            return {quic_flow_code::frame_encoding_error};
        case quic_stream_code::protocol_violation:
            return {quic_flow_code::protocol_violation};
        case quic_stream_code::no_memory:
            return {quic_flow_code::no_memory};
        default:
            return {quic_flow_code::capacity};
    }
}

}  // namespace httpserver::detail
