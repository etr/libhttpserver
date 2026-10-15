/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <httpserver/detail/quic_flow_internal.hpp>
namespace httpserver::detail {
quic_flow_result quic_flow_control::reserve_critical(std::uint64_t id, std::uint64_t offset, std::size_t length) {
    if (offset > k_quic_max_integer || length > k_quic_max_integer - offset) return {quic_flow_code::frame_encoding_error};
    auto* value = impl_->find(id);
    const auto identity = quic_stream_id(id);
    if (!value || value->retired || value->reset_sent || !identity || !identity->unidirectional ||
        identity->initiator != impl_->ids.local_role()) return {quic_flow_code::stream_state_error};
    const auto end = offset + length;
    if (end > value->send_limit || end > value->sent_final.value_or(k_quic_max_integer)) return {quic_flow_code::blocked};
    const auto needed = end > value->sent ? end - value->sent : 0;
    const auto available = impl_->send_limit - impl_->sent - impl_->reserved + value->reserved;
    if (needed > available) return {quic_flow_code::blocked};
    impl_->reserved = impl_->reserved - value->reserved + needed;
    value->reserved = needed;
    return {};
}
void quic_flow_control::abandon_critical(std::uint64_t id) {
    if (auto* value = impl_->find(id)) {
        impl_->reserved -= value->reserved; value->reserved = 0;
    }
}
quic_flow_allowance quic_flow_control::send_allowance(std::uint64_t id, std::uint64_t offset, std::size_t requested) const {
    if (offset > k_quic_max_integer || requested > k_quic_max_integer - offset) return {{quic_flow_code::frame_encoding_error}};
    auto* value = impl_->find(id);
    if (!value || value->retired || !quic_stream_sender_allowed(id, true, impl_->ids.local_role())) return {{quic_flow_code::stream_state_error}};
    if (value->reset_sent) return {{quic_flow_code::stream_state_error}};
    const auto ceiling = std::min({value->send_limit, flow_add(value->sent, impl_->send_limit - impl_->sent - impl_->reserved + value->reserved), value->sent_final.value_or(k_quic_max_integer)});
    if (offset > ceiling || (requested && offset == ceiling)) return {{quic_flow_code::blocked}};
    const auto bytes = std::min<std::uint64_t>(requested, ceiling - offset);
    return {{}, static_cast<std::size_t>(bytes)};
}
namespace {
bool inconsistent_final(std::optional<std::uint64_t> final, std::uint64_t highest, std::uint64_t end, bool fin) {
    if (final && (end > *final || (fin && end != *final))) return true;
    return fin && end < highest;
}
}  // namespace
quic_flow_result quic_flow_control::check_stream_sent(std::uint64_t id, std::uint64_t offset, std::size_t length, bool fin) const {
    auto allowance = send_allowance(id, offset, length);
    if (!allowance) return {allowance.code};
    if (allowance.bytes < length) return {quic_flow_code::blocked};
    auto* value = impl_->find(id);
    const auto end = offset + length;
    if (inconsistent_final(value->sent_final, value->sent, end, fin)) return {quic_flow_code::final_size_error};
    return {};
}
quic_flow_result quic_flow_control::record_stream_sent(std::uint64_t id, std::uint64_t offset, std::size_t length, bool fin) {
    auto checked = check_stream_sent(id, offset, length, fin);
    if (!checked) return checked;
    auto* value = impl_->find(id);
    const auto end = offset + length;
    const auto added = end > value->sent ? end - value->sent : 0;
    const auto used = std::min(added, value->reserved);
    value->reserved -= used; impl_->reserved -= used;
    impl_->sent += added;
    value->sent = std::max(value->sent, end);
    if (fin) {
        value->sent_final = end; abandon_critical(id);
    }
    return {};
}
quic_flow_result quic_flow_control::check_reset_sent(std::uint64_t id, std::uint64_t final_size) const {
    auto* value = impl_->find(id);
    if (!value || value->retired || !quic_stream_sender_allowed(id, true, impl_->ids.local_role())) return {quic_flow_code::stream_state_error};
    return {final_size == value->sent ? quic_flow_code::ok : quic_flow_code::final_size_error};
}
quic_flow_result quic_flow_control::record_reset_sent(std::uint64_t id, std::uint64_t final_size) {
    auto checked = check_reset_sent(id, final_size);
    if (!checked) return checked;
    auto* value = impl_->find(id);
    abandon_critical(id); value->reset_sent = true;
    return {};
}
}  // namespace httpserver::detail
