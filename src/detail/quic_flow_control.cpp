/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include "./quic_flow_internal.hpp"
namespace httpserver::detail {
namespace {
constexpr auto maximum_streams = std::uint64_t{1} << 60;
void validate_parameters(const quic_transport_parameters& p) {
    for (auto value : {p.initial_max_data, p.initial_max_stream_data_bidi_local, p.initial_max_stream_data_bidi_remote, p.initial_max_stream_data_uni})
        if (value > k_quic_max_integer) throw std::invalid_argument("Invalid initial QUIC byte limit");
    if (p.initial_max_streams_bidi > maximum_streams || p.initial_max_streams_uni > maximum_streams)
        throw std::invalid_argument("Invalid initial QUIC stream count");
}
}  // namespace
std::size_t quic_flow_control::storage_capacity(std::size_t records) {
    if (records > 65536) throw std::invalid_argument("QUIC flow descriptor bound too large");
    return records * sizeof(implementation::record);
}
quic_flow_control::implementation::implementation(quic_endpoint_role role, const quic_transport_parameters& local,
                                   const quic_transport_parameters& peer, std::size_t bound, server::resource_budget budget) : ids(role), maximum(bound) {
    validate_parameters(local);
    validate_parameters(peer);
    send_counts = {peer.initial_max_streams_bidi, peer.initial_max_streams_uni};
    receive_counts = {credit(local.initial_max_streams_bidi), credit(local.initial_max_streams_uni)};
    receive_limit = credit(local.initial_max_data);
    receive_window = local.initial_max_data;
    send_limit = peer.initial_max_data;
    const auto bytes = quic_flow_control::storage_capacity(bound);
    for (unsigned c = 0; c < 4; ++c) {
        const bool own = (c & 1) == (role == quic_endpoint_role::server ? 1U : 0U);
        receive_windows[c] = (c & 2) ? local.initial_max_stream_data_uni :
            own ? local.initial_max_stream_data_bidi_local : local.initial_max_stream_data_bidi_remote;
        send_windows[c] = (c & 2) ? peer.initial_max_stream_data_uni :
            own ? peer.initial_max_stream_data_bidi_remote : peer.initial_max_stream_data_bidi_local;
    }
    try {
        if (bytes && !budget.reserve(server::resource::quic_reassembly_bytes, bytes, storage).ok()) {
            admission = quic_flow_code::no_memory;
            return;
        }
        records.reserve(bound);
    } catch (const std::bad_alloc&) {
        storage.release();
        admission = quic_flow_code::no_memory;
    }
}
quic_flow_control::quic_flow_control(quic_endpoint_role role, const quic_transport_parameters& local, const quic_transport_parameters& peer,
                                   std::size_t bound, server::resource_budget budget)
    : impl_(std::make_unique<implementation>(role, local, peer, bound, std::move(budget))) {}
quic_flow_control::~quic_flow_control() = default;
const quic_stream_ids& quic_flow_control::ids() const { return impl_->ids; }
quic_flow_control::implementation::record* quic_flow_control::implementation::find(std::uint64_t id) {
    for (auto& value : records) if (value.id == id) return &value;
    return nullptr;
}
const quic_flow_control::implementation::record* quic_flow_control::implementation::find(std::uint64_t id) const {
    for (const auto& value : records) if (value.id == id) return &value;
    return nullptr;
}
quic_flow_code quic_flow_control::implementation::record_admission(std::uint64_t id) const {
    if (admission != quic_flow_code::ok) return admission;
    const auto* value = find(id);
    if (value) return value->retired ? quic_flow_code::stream_state_error : quic_flow_code::ok;
    return records.size() < maximum ? quic_flow_code::ok : quic_flow_code::capacity;
}
quic_flow_control::implementation::record* quic_flow_control::implementation::create(std::uint64_t id) {
    if (auto* value = find(id)) return value;
    record value;
    value.id = id;
    value.receive_window = receive_windows[id & 3];
    value.receive_limit = credit(value.receive_window);
    value.send_limit = send_windows[id & 3];
    records.push_back(value);
    return &records.back();
}
quic_flow_open_result quic_flow_control::open_local(bool uni) {
    const auto count = impl_->ids.opened_count((impl_->ids.local_role() == quic_endpoint_role::server ? 1 : 0) + (uni ? 2 : 0));
    if (count >= impl_->send_counts[uni]) return {{quic_flow_code::blocked}};
    if (impl_->admission != quic_flow_code::ok) return {{impl_->admission}};
    if (impl_->records.size() >= impl_->maximum) return {{quic_flow_code::capacity}};
    auto id = impl_->ids.open_local(uni);
    if (!id) return {{quic_flow_code::blocked}};
    impl_->create(*id);
    return {{}, *id};
}
quic_flow_result quic_flow_control::observe_peer(std::uint64_t id) {
    auto identity = quic_stream_id(id);
    if (!identity) return {quic_flow_code::frame_encoding_error};
    if (identity->initiator == impl_->ids.local_role() && !impl_->ids.opened(id)) return {quic_flow_code::stream_state_error};
    if (identity->initiator != impl_->ids.local_role() && identity->ordinal >= impl_->receive_counts[identity->unidirectional].desired)
        return {quic_flow_code::stream_limit_error};
    auto admission = impl_->record_admission(id);
    if (admission != quic_flow_code::ok) return {admission};
    impl_->create(id);
    return flow_result(impl_->ids.observe_peer(id));
}
quic_flow_result quic_flow_control::apply_stream_control(const quic_flow_frame& frame) {
    const bool send_half = frame.kind == quic_flow_kind::max_stream_data;
    if (frame.stream > k_quic_max_integer) return {quic_flow_code::frame_encoding_error};
    if (!quic_stream_sender_allowed(frame.stream, send_half, impl_->ids.local_role())) return {quic_flow_code::stream_state_error};
    auto opened = observe_peer(frame.stream);
    if (!opened) return opened;
    if (send_half) {
        auto& limit = impl_->find(frame.stream)->send_limit;
        limit = std::max(limit, frame.limit);
    }
    return {};
}
namespace {
bool valid_flow_limit(const quic_flow_frame& frame) {
    return frame.limit <= k_quic_max_integer && (!quic_flow_is_stream_count(frame.kind) || frame.limit <= maximum_streams);
}
}  // namespace
quic_flow_result quic_flow_control::apply(const quic_flow_frame& frame) {
    if (!valid_flow_limit(frame)) return {quic_flow_code::frame_encoding_error};
    switch (frame.kind) {
    case quic_flow_kind::max_data: impl_->send_limit = std::max(impl_->send_limit, frame.limit); break;
    case quic_flow_kind::max_streams_bidi:
    case quic_flow_kind::max_streams_uni: {
        auto& count = impl_->send_counts[frame.kind == quic_flow_kind::max_streams_uni];
        count = std::max(count, frame.limit);
        break;
    }
    case quic_flow_kind::max_stream_data:
    case quic_flow_kind::stream_data_blocked: return apply_stream_control(frame);
    case quic_flow_kind::data_blocked:
    case quic_flow_kind::streams_blocked_bidi:
    case quic_flow_kind::streams_blocked_uni: break;
    default: return {quic_flow_code::frame_encoding_error};
    }
    return {};
}
std::uint64_t quic_flow_control::received() const { return impl_->received; }
std::uint64_t quic_flow_control::sent() const { return impl_->sent; }

quic_flow_control::quic_flow_control(quic_endpoint_role role, const quic_transport_parameters& local, const quic_transport_parameters& peer,
                                   std::size_t bound, quic_storage_lease storage)
    : quic_flow_control(role, local, peer, bound, storage.budget) { storage_owner_ = std::move(storage); }
}  // namespace httpserver::detail
