/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <utility>
#include "./quic_recovery_internal.hpp"
namespace httpserver::detail {
namespace {
bool valid_max_frame(const quic_flow_frame& frame, quic_endpoint_role role) {
    if (frame.limit > k_quic_max_integer) return false;
    switch (frame.kind) {
    case quic_flow_kind::max_data: return true;
    case quic_flow_kind::max_stream_data: return frame.stream <= k_quic_max_integer && quic_stream_sender_allowed(frame.stream, false, role);
    case quic_flow_kind::max_streams_bidi:
    case quic_flow_kind::max_streams_uni: return frame.limit <= (std::uint64_t{1} << 60);
    default: return false;
    }
}
bool same_control(const quic_flow_frame& a, const quic_flow_frame& b) {
    return a.kind == b.kind && (a.kind != quic_flow_kind::max_stream_data || a.stream == b.stream);
}
}  // namespace
quic_information_result quic_recovery::retain_flow(const quic_flow_frame& frame) {
    if (!valid_max_frame(frame, impl_->config.role)) return {{quic_recovery_code::invalid}};
    for (auto& value : impl_->information_records) {
        if (value.kind != quic_information_kind::flow || value.cancelled || !same_control(value.flow, frame)) continue;
        if (frame.limit <= value.flow.limit) return {{}, value.id};
        // A fresh information ID separates generations from all old packet ACKs.
        // Reuse its bounded descriptor, never the old delivery identity.
        if (!impl_->next_information) return {{quic_recovery_code::capacity}};
        value.id = impl_->next_information++;
        value.flow = frame;
        value.offset = frame.limit;
        value.terminal = information_status::pending;
        value.completed = value.completion_pending = false;
        return {{}, value.id};
    }
    implementation::information value;
    value.kind = quic_information_kind::flow;
    value.space = quic_pn_space::application;
    value.stream = frame.stream;
    value.offset = frame.limit;
    value.flow = frame;
    value.terminal = information_status::pending;
    return impl_->retain(std::move(value), {});
}
}  // namespace httpserver::detail
