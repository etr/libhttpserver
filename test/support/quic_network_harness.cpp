/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <vector>
#include <stdexcept>
#include <utility>
#include "support/quic_network_harness.hpp"
namespace quic_test {
bool logical_clock::advance(std::int64_t nanoseconds) {
    if (nanoseconds < 0 || nanoseconds > std::numeric_limits<std::int64_t>::max() - ticks_) return false;
    ticks_ += nanoseconds;
    return true;
}
namespace {
enum class event : std::uint8_t {
    emission = 1, loss, duplication, dispatch, delivery, timer, completion, cancel,
    advance, registration, retirement, destruction, drain, teardown, resources
};
struct pending_packet {
    std::uint64_t id;
    std::shared_ptr<hd::io_datagram> packet;
};
struct pending_timer {
    std::uint64_t id;
    std::shared_ptr<hd::op_state> state;
};
}  // namespace
struct network_harness::implementation {
    struct sink final : hd::datagram_sink {
        implementation* rig;
        unsigned endpoint;
        sink(implementation* rig, unsigned endpoint) : rig(rig), endpoint(endpoint) {}
        void on_datagram(std::shared_ptr<const hd::io_datagram> packet) noexcept override {
            rig->record(event::delivery, endpoint);
            rig->encode_packet(*packet);
            rig->delivered.push_back(*packet);
        }
    };
    struct endpoint {
        httpserver::manual_executor executor;
        std::unique_ptr<hd::io_connection_owner> owner;
        std::optional<hd::quic_datagram_dispatch::cid_registration> registration;
    };
    harness_limits limits;
    logical_clock clock;
    hd::quic_datagram_dispatch dispatcher{2, 4};
    std::array<endpoint, 4> endpoints;
    httpserver::manual_executor timer_executor;
    hd::io_connection_owner timer_owner{timer_executor};
    hd::fake_io_backend backend;
    std::vector<pending_packet> packets;
    std::vector<pending_timer> timers;
    std::vector<hd::io_datagram> delivered;
    std::vector<timer_result> completed;
    std::vector<std::uint8_t> trace{'Q', 'N', 1};
    std::size_t bytes = 0, actions = 0;
    std::uint64_t next_id = 1;
    bool closed = false;

    explicit implementation(harness_limits value) : limits(value) {
        // Hard caps bound arithmetic and observation storage for arbitrary fuzz
        // inputs. Caller limits can tighten these, never enlarge the rig.
        if (limits.packets > 16 || limits.bytes > 65536 || limits.packet_bytes > 4096 || limits.timers > 16 ||
            limits.actions > 128 || limits.trace_bytes > 2097152 || limits.owner_packets > 8 || limits.owner_bytes > 65536) {
            throw std::invalid_argument("QUIC harness limits exceed hard caps");
        }
        if (limits.trace_bytes < trace.size()) throw std::invalid_argument("QUIC trace too small");
    }
    std::size_t action_reserve() const {
        return (4 * limits.owner_packets + 1) * (limits.packet_bytes + 256) + limits.timers * 64 + 512;
    }
    bool begin() {
        // Reserve one worst-case drain and one teardown before mutating. This
        // makes trace exhaustion fail admission instead of silently truncating.
        if (closed || actions == limits.actions || 2 * action_reserve() > limits.trace_bytes - trace.size()) return false;
        ++actions;
        return true;
    }
    void integer(std::uint64_t value) {
        for (unsigned i = 0; i < 8; ++i) trace.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    }
    void record(event kind, std::uint64_t id = 0) {
        trace.push_back(static_cast<std::uint8_t>(kind));
        integer(id);
        integer(static_cast<std::uint64_t>(clock.ticks()));
    }
    void encode_endpoint(const hd::datagram_endpoint& endpoint) {
        integer(static_cast<std::uint64_t>(endpoint.peer.address.family));
        for (auto byte : endpoint.peer.address.bytes) trace.push_back(std::to_integer<std::uint8_t>(byte));
        integer(endpoint.peer.port);
        integer(endpoint.scope);
    }
    void encode_packet(const hd::io_datagram& packet) {
        integer(packet.bytes.size());
        for (auto byte : packet.bytes) trace.push_back(std::to_integer<std::uint8_t>(byte));
        integer(packet.socket_id);
        encode_endpoint(packet.peer);
        integer(packet.local.has_value());
        if (packet.local) encode_endpoint(*packet.local);
        integer(packet.interface_index.has_value());
        if (packet.interface_index) integer(*packet.interface_index);
        integer(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(packet.received_at.time_since_epoch()).count()));
    }
    resource_snapshot resources() const {
        resource_snapshot result{packets.size(), bytes, timers.size(), timer_owner.pending()};
        for (const auto& endpoint : endpoints) if (endpoint.owner) result.owner_pending += endpoint.owner->pending();
        return result;
    }
    void snapshot() {
        record(event::resources);
        const auto state = resources();
        integer(state.packets); integer(state.bytes); integer(state.timers); integer(state.owner_pending);
    }
    auto find_packet(std::uint64_t id) {
        return std::find_if(packets.begin(), packets.end(), [id](const auto& packet) { return packet.id == id; });
    }
    bool admits(const hd::io_datagram& packet) const {
        return packets.size() < limits.packets && packet.bytes.size() <= limits.packet_bytes && packet.bytes.size() <= limits.bytes - bytes;
    }
    std::uint64_t store(const hd::io_datagram& packet) {
        auto copy = std::make_shared<hd::io_datagram>(packet);
        auto id = next_id++;
        bytes += copy->bytes.size();
        packets.push_back({id, std::move(copy)});
        return id;
    }
    httpserver::task<void> observe_timer(hd::timer_operation operation, std::uint64_t id) {
        auto result = co_await operation;
        record(event::completion, id);
        integer(static_cast<std::uint64_t>(result.code));
        completed.push_back({id, result.code});
    }
    void drain_timers() {
        timer_executor.run_pending();
        std::erase_if(timers, [](const auto& timer) { return timer.state->applied(); });
    }
    void drain_endpoints() {
        for (auto& endpoint : endpoints) endpoint.executor.run_pending();
    }
};
network_harness::network_harness(harness_limits limits) : impl_(std::make_unique<implementation>(limits)) {}
network_harness::~network_harness() { teardown(); }
bool network_harness::register_endpoint(unsigned endpoint) {
    auto& r = *impl_;
    if (endpoint < 1 || endpoint > 4 || !r.begin()) return false;
    auto& target = r.endpoints[endpoint - 1];
    if (target.registration) return false;
    if (!target.owner) target.owner = std::make_unique<hd::io_connection_owner>(target.executor, r.limits.owner_packets, r.limits.owner_bytes);
    hd::quic_cid cid; cid.size = 2; cid.bytes[0] = std::byte(endpoint);
    target.registration = r.dispatcher.register_cid(cid, target.owner->datagrams(), std::make_shared<implementation::sink>(&r, endpoint));
    r.record(event::registration, endpoint); r.snapshot();
    return target.registration.has_value();
}
bool network_harness::retire_endpoint(unsigned endpoint) {
    auto& r = *impl_;
    if (endpoint < 1 || endpoint > 4 || !r.begin()) return false;
    auto& target = r.endpoints[endpoint - 1];
    if (!target.registration) return false;
    r.dispatcher.remove(*target.registration); target.registration.reset();
    r.record(event::retirement, endpoint); r.snapshot();
    return true;
}
bool network_harness::destroy_endpoint(unsigned endpoint) {
    auto& r = *impl_;
    if (endpoint < 1 || endpoint > 4 || !r.begin()) return false;
    auto& target = r.endpoints[endpoint - 1];
    if (target.registration) r.dispatcher.remove(*target.registration);
    target.registration.reset(); target.owner.reset(); target.executor.run_pending();
    r.record(event::destruction, endpoint); r.snapshot();
    return true;
}
std::optional<std::uint64_t> network_harness::send(const hd::io_datagram& packet) {
    auto& r = *impl_;
    if (!r.admits(packet) || !r.begin()) return {};
    auto id = r.store(packet);
    r.record(event::emission, id); r.encode_packet(packet); r.snapshot();
    return id;
}
std::optional<std::uint64_t> network_harness::duplicate(std::uint64_t id) {
    auto& r = *impl_;
    auto found = r.find_packet(id);
    if (found == r.packets.end() || !r.admits(*found->packet) || !r.begin()) return {};
    auto packet = found->packet;
    auto copy = r.store(*packet);
    r.record(event::duplication, copy); r.integer(id); r.encode_packet(*packet); r.snapshot();
    return copy;
}
bool network_harness::drop(std::uint64_t id) {
    auto& r = *impl_;
    auto found = r.find_packet(id);
    if (found == r.packets.end() || !r.begin()) return false;
    r.bytes -= found->packet->bytes.size(); r.packets.erase(found);
    r.record(event::loss, id); r.snapshot();
    return true;
}
std::optional<hd::datagram_dispatch_code> network_harness::deliver(std::uint64_t id) {
    auto& r = *impl_;
    auto found = r.find_packet(id);
    if (found == r.packets.end() || !r.begin()) return {};
    auto packet = std::move(found->packet);
    r.bytes -= packet->bytes.size(); r.packets.erase(found);
    packet->received_at = r.clock.now();
    const auto result = r.dispatcher.dispatch(std::move(packet));
    r.record(event::dispatch, id); r.integer(static_cast<std::uint64_t>(result.code)); r.snapshot();
    return result.code;
}
bool network_harness::advance(std::int64_t nanoseconds) {
    auto& r = *impl_;
    if (nanoseconds < 0 || nanoseconds > std::numeric_limits<std::int64_t>::max() - r.clock.ticks() || !r.begin()) return false;
    r.clock.advance(nanoseconds);
    r.record(event::advance); r.integer(static_cast<std::uint64_t>(nanoseconds));
    r.backend.expire_timers(r.clock.now()); r.drain_timers(); r.snapshot();
    return true;
}
bool network_harness::drain() {
    auto& r = *impl_;
    if (!r.begin()) return false;
    r.record(event::drain); r.drain_endpoints(); r.drain_timers(); r.snapshot();
    return true;
}
std::optional<std::uint64_t> network_harness::timer(std::int64_t delay) {
    auto& r = *impl_;
    if (delay < 0 || delay > std::numeric_limits<std::int64_t>::max() - r.clock.ticks() || r.timers.size() == r.limits.timers || !r.begin()) return {};
    auto id = r.next_id++;
    auto deadline = logical_clock::time_point(std::chrono::duration_cast<logical_clock::time_point::duration>(std::chrono::nanoseconds(r.clock.ticks() + delay)));
    hd::timer_operation operation(r.timer_owner, 0, deadline);
    operation.submit(r.backend); r.timers.push_back({id, operation.state()});
    r.record(event::timer, id); r.integer(static_cast<std::uint64_t>(delay));
    httpserver::spawn(r.timer_executor, r.observe_timer(std::move(operation), id), [](httpserver::task_result<void>) {});
    r.timer_executor.run_pending(); r.snapshot();
    return id;
}
bool network_harness::cancel(std::uint64_t id) {
    auto& r = *impl_;
    auto found = std::find_if(r.timers.begin(), r.timers.end(), [id](const auto& timer) { return timer.id == id; });
    if (found == r.timers.end() || !r.begin()) return false;
    const auto result = r.backend.request_cancel(*found->state);
    r.record(event::cancel, id); r.integer(static_cast<std::uint64_t>(result)); r.drain_timers(); r.snapshot();
    return result == httpserver::http::outcome_code::ok;
}
void network_harness::teardown() {
    auto& r = *impl_;
    if (r.closed) return;
    r.closed = true;
    // Cancel in submission order before close: fake_backend::close() iterates
    // an unordered registry. Explicit completion makes teardown replay stable.
    for (const auto& timer : r.timers) r.backend.complete(*timer.state, {httpserver::http::outcome_code::connection_closed});
    r.backend.close(); r.drain_timers();
    for (auto& endpoint : r.endpoints) {
        if (endpoint.registration) r.dispatcher.remove(*endpoint.registration);
        endpoint.registration.reset(); endpoint.owner.reset(); endpoint.executor.run_pending();
    }
    r.packets.clear(); r.bytes = 0;
    // Even a rig with no trace capacity must reclaim its resources.
    if (r.limits.trace_bytes - r.trace.size() >= 66) {
        r.record(event::teardown); r.snapshot();
    }
}
resource_snapshot network_harness::resources() const { return impl_->resources(); }
std::vector<std::uint64_t> network_harness::pending_ids() const {
    std::vector<std::uint64_t> ids;
    for (const auto& packet : impl_->packets) ids.push_back(packet.id);
    return ids;
}
std::weak_ptr<const hd::io_datagram> network_harness::packet_lifetime(std::uint64_t id) const {
    auto found = impl_->find_packet(id);
    return found == impl_->packets.end() ? std::weak_ptr<const hd::io_datagram>{} : found->packet;
}
const std::vector<hd::io_datagram>& network_harness::delivered() const { return impl_->delivered; }
const std::vector<timer_result>& network_harness::completed_timers() const { return impl_->completed; }
const std::vector<std::uint8_t>& network_harness::trace() const { return impl_->trace; }
std::int64_t network_harness::ticks() const { return impl_->clock.ticks(); }
hd::io_datagram short_packet(unsigned endpoint, unsigned marker) {
    hd::io_datagram packet;
    packet.bytes = {std::byte{0x40}, std::byte(endpoint), {}, std::byte(marker)};
    return packet;
}
}  // namespace quic_test
