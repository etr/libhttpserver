/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fuzz/quic_recovery_fuzz.hpp"
#include <algorithm>
#include <memory>
#include <optional>
#include <httpserver/detail/quic_connection_identity.hpp>
#include <httpserver/detail/quic_connection_lifecycle.hpp>
#include <httpserver/detail/quic_flow_control.hpp>
namespace {
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
using quic_fuzz::require;
using event = quic_fuzz::event;
using space = hd::quic_pn_space;
constexpr auto storage_resource = hs::resource::quic_reassembly_bytes;
hs::resource_budget root_budget() {
    hs::budget_limits limits;
    limits.set(storage_resource, 262144);
    return hs::resource_budget::root(limits);
}
hd::quic_recovery_config recovery_config() {
    hd::quic_recovery_config config;
    config.max_receive_ranges = 4;
    config.max_sent_packets = 8;
    config.max_information = 16;
    config.max_retained_bytes = 128;
    return config;
}
hd::quic_transport_parameters parameters() {
    hd::quic_transport_parameters p;
    p.initial_max_data = 32;
    p.initial_max_stream_data_bidi_local = p.initial_max_stream_data_bidi_remote = 16;
    p.initial_max_streams_bidi = 2;
    return p;
}
hd::quic_cid cid(unsigned value) {
    hd::quic_cid result;
    result.size = 8;
    result.bytes[0] = std::byte(value);
    return result;
}
hd::quic_path_identity path(unsigned value) {
    hd::quic_path_identity result;
    result.peer.peer = {*httpserver::net::parse_address("127.0.0.1"), static_cast<std::uint16_t>(4000 + value % 3)};
    result.socket_id = 1;
    return result;
}
struct recovery_driver {
    hd::quic_storage_pool storage;
    hd::quic_recovery recovery;
    hd::quic_flow_control flow;
    std::array<hd::quic_reassembly, 3> crypto;
    hs::peer_policy policy;
    std::shared_ptr<hd::quic_amplification_budget> amplification = std::make_shared<hd::quic_amplification_budget>(path(0));
    unsigned random = 1;
    hd::quic_connection_identity identity;
    hd::quic_connection_lifecycle lifecycle{std::chrono::milliseconds(0), {}, {}};
    hd::quic_recovery::time_point now{};
    hd::quic_recovery_environment environment;
    std::optional<hd::quic_send_plan> pending;
    std::optional<hd::quic_identity_control> control;
    std::array<hd::quic_datagram_emission, 3> batch{};
    std::size_t batch_size = 0;
    bool batch_active = false;
    std::array<std::optional<std::uint64_t>, 3> allocated{};
    std::array<std::byte, 8> challenge{};
    std::array<std::optional<hd::quic_information_completion>, 65> completed{};
    explicit recovery_driver(hs::resource_budget root)
        : storage(131072, 131072, root), recovery(recovery_config(), storage.data(), storage.critical()),
          flow(hd::quic_endpoint_role::server, parameters(), parameters(), 4, storage.data()),
          crypto{hd::quic_reassembly({64, 4, 128}, storage.critical()), hd::quic_reassembly({64, 4, 128}, storage.critical()),
                 hd::quic_reassembly({64, 4, 128}, storage.critical())},
          identity(cid(1), cid(2), amplification, recovery, policy,
                   [this](std::span<std::byte> out) { std::fill(out.begin(), out.end(), std::byte(++random)); return true; }, storage.critical()) {
        amplification->mark_address_validated();
        require(static_cast<bool>(flow.open_local(false)) && static_cast<bool>(flow.open_local(false)), "bounded flow setup");
    }
    void allocation(space s, const hd::quic_send_plan& plan) {
        if (!plan) return;
        auto& last = allocated[static_cast<unsigned>(s)];
        require(!last || plan.packet_number > *last, "monotonic packet allocation");
        last = plan.packet_number;
    }
    void finish_pending() {
        if (control) identity.complete_control(control->token, hd::quic_identity_emission::unsent, now);
        control.reset();
        if (pending) recovery.abandon_packet(pending->token);
        pending.reset();
        if (batch_active) recovery.abandon_datagram();
        batch_active = false;
        batch_size = 0;
    }
    void stop() {
        identity.stop();
        finish_pending();
        for (unsigned i = 0; i < 3; ++i) {
            recovery.discard_space(static_cast<space>(i));
            crypto[i].clear();
        }
        require(!recovery.preparation_pending() && recovery.bytes_in_flight() == 0, "terminal send retirement");
    }
    void completions() {
        unsigned count = 0;
        while (auto done = recovery.take_completion()) {
            require(++count <= 16 && done->id < completed.size(), "bounded completion count");
            auto& previous = completed[done->id];
            require(!previous || done->through_offset > previous->through_offset || (done->fin && !previous->fin), "unique progress completion");
            previous = *done;
            identity.information_completed(*done);
        }
    }
    int receive_path(event e, bool response, quic_fuzz::report& report) {
        const std::array<hd::quic_frame, 1> frames = response ? std::array<hd::quic_frame, 1>{hd::quic_path_frame{true, challenge}}
                                                           : std::array<hd::quic_frame, 1>{hd::quic_ping_frame{}};
        const auto result = identity.receive({path(e.a), cid(1), e.b, true, true, 1200}, frames, now);
        report.paths_activated += result.activated;
        return static_cast<int>(result.code);
    }
    int prepare(event e, space s) {
        std::array<std::byte, 128> output{};
        auto room = std::span(output).first(e.b % 129);
        auto plan = (e.c & 1) ? recovery.reserve_packet(s) : recovery.prepare_packet(s, room, now, flow, (e.c & 2) != 0);
        require(plan.bytes <= room.size(), "preparation output bound");
        allocation(s, plan);
        if (plan) pending = plan;
        return static_cast<int>(plan.code);
    }
    int commit(event e, quic_fuzz::report& report) {
        if (!pending) return static_cast<int>(recovery.commit_sent(0, now, 100, true, true).code);
        const auto wire = e.b ? std::max<std::size_t>(pending->bytes, e.b) : 0;
        const auto result = recovery.commit_sent(pending->token, now, wire, pending->ack_eliciting, true);
        if (result) {
            lifecycle.emitted(true, pending->ack_eliciting, now);
            pending.reset();
        }
        report.trace.push_back(flow.sent());
        return static_cast<int>(result.code);
    }
    int datagram(event e, space s) {
        if (e.kind == 17) {
            const auto result = recovery.begin_datagram();
            if (result) {
                batch_active = true;
                batch_size = 0;
            }
            return static_cast<int>(result.code);
        }
        if (e.kind == 18) {
            std::array<std::byte, 128> output{};
            auto plan = recovery.prepare_datagram_packet(s, std::span(output).first(e.b % 129), now, {400, 40, (e.c & 1) != 0}, flow);
            allocation(s, plan);
            require(plan.bytes <= e.b % 129, "datagram output bound");
            if (plan) {
                require(batch_size < 3, "bounded datagram");
                batch[batch_size++] = {plan.token, 400, plan.ack_eliciting, true};
            }
            return static_cast<int>(plan.code);
        }
        const auto result = e.kind == 19 ? recovery.commit_datagram(std::span(batch).first(batch_size), now) : recovery.abandon_datagram();
        if (result) {
            batch_active = false;
            batch_size = 0;
        }
        return static_cast<int>(result.code);
    }
    int apply(event e, quic_fuzz::report& report) {
        const auto s = static_cast<space>(e.a % 3);
        auto data = quic_fuzz::payload(e);
        const auto bytes = std::span(data).first(e.c % 17);
        switch (e.kind) {
            case 0: {
                report.trace.push_back(static_cast<unsigned>(recovery.inspect_received(s, e.b)));
                return static_cast<int>(recovery.receive_packet(s, e.b, (e.c & 1) != 0, now).code);
            }
            case 1: {
                const auto result = crypto[e.a % 3].insert(e.b, bytes);
                data.fill(std::byte{255});  // Later reads must use owned data.
                return static_cast<int>(result.code);
            }
            case 2: return static_cast<int>(recovery.retain_crypto(s, e.b, bytes).code);
            case 3: return prepare(e, s);
            case 4: return commit(e, report);
            case 5: {
                const auto result = recovery.abandon_packet(pending ? pending->token : 0);
                if (result) pending.reset();
                return static_cast<int>(result.code);
            }
            case 6: {
                const hd::quic_ack_frame ack{e.b, e.c, std::uint64_t{e.a} / 3, 0, {}, {}};
                const auto result = recovery.receive_ack(s, ack, now);
                report.acknowledged += result.acknowledged_bytes;
                report.acknowledged_packets += result.acknowledged_packets;
                report.lost += result.lost_bytes;
                return static_cast<int>(result.code);
            }
            case 7: {
                now += std::chrono::milliseconds(1 + e.b);
                const auto result = recovery.expire(now);
                report.lost += result.lost_bytes;
                report.trace.push_back(result.probes);
                identity.expire(now);
                lifecycle.expire(now, recovery.current_pto());
                return static_cast<int>(result.code);
            }
            case 8:
                if (pending || control || batch_active) finish_pending();
                return static_cast<int>(recovery.discard_space(s).code);
            case 9:
                environment.write_keys = {(e.b & 1) != 0, (e.b & 2) != 0, (e.b & 4) != 0};
                environment.send_permitted = (e.c & 1) != 0;
                environment.handshake_confirmed = (e.c & 2) != 0;
                recovery.set_environment(environment, now);
                return 0;
            case 10: return receive_path(e, false, report);
            case 11: {
                auto plan = identity.prepare_control(now, 29);
                if (plan) {
                    allocation(space::application, plan->packet);
                    control = plan;
                    challenge = plan->data;
                }
                return plan ? 0 : 1;
            }
            case 12: {
                if (!control) return 1;
                const auto result = identity.complete_control(control->token, (e.a & 1) ? hd::quic_identity_emission::sent : hd::quic_identity_emission::unsent, now);
                if (result) control.reset();
                return static_cast<int>(result.code);
            }
            case 13: return receive_path(e, true, report);
            case 14: {
                const hd::quic_close_frame frame{false, e.b, 0, {}};
                bool changed = e.a % 3 == 0 ? lifecycle.local_close(frame, now, recovery.current_pto())
                    : e.a % 3 == 1 ? lifecycle.peer_close(frame, now, recovery.current_pto()) : lifecycle.stateless_reset(now, recovery.current_pto());
                if (changed && report.close_notifications == 0) {
                    ++report.close_notifications;
                    stop();
                }
                return changed ? 0 : 1;
            }
            case 15: {
                std::array<std::byte, 16> output{};
                const auto before = crypto[e.a % 3].consumed();
                const auto size = crypto[e.a % 3].read(std::span(output).first(e.c % 17));
                for (std::size_t i = 0; i < size; ++i) require(output[i] == std::byte((before + i) % 256), "owned ordered CRYPTO bytes");
                report.crypto_read += size;
                return 0;
            }
            case 16: return static_cast<int>(recovery.retain_stream({std::uint64_t{1} + 4 * (e.a % 2), e.b, bytes, (e.a & 4) != 0}).code);
            case 17: case 18: case 19: case 20: return datagram(e, s);
            case 21: {
                std::array<std::byte, 64> out{};
                const auto plan = recovery.prepare_ack(s, std::span(out).first(e.b % 65), now);
                require(plan.bytes <= e.b % 65, "ACK output bound");
                if (plan && (e.c & 1)) recovery.publish_ack(s, plan.generation);
                return static_cast<int>(plan.code);
            }
            case 22: return static_cast<int>(flow.apply({hd::quic_flow_kind::max_data, e.b}).code);
            case 23: stop(); lifecycle.stateless_reset(now, recovery.current_pto()); return 0;
            default: return 1;
        }
    }
    void step(event e, quic_fuzz::report& report) {
        e.kind %= 24;
        ++report.attempted[e.kind];
        int result = 1;
        if (lifecycle.state() == hd::quic_lifecycle_state::active || e.kind == 7 || e.kind == 14 || e.kind == 23) result = apply(e, report);
        else ++report.rejected_queue;
        if (result == 0) ++report.accepted[e.kind];
        completions();
        require(recovery.preparation_pending() == (pending.has_value() || control.has_value() || batch_active), "single pending owner transaction");
        require(recovery.bytes_in_flight() <= 3 * 8 * 1200, "bounded in-flight bytes");
        for (const auto& input : crypto) require(input.buffered_bytes() <= 64 && input.ranges() <= 4, "bounded CRYPTO state");
        for (auto lease : {storage.data(), storage.critical()}) require(lease.budget.in_use(storage_resource) <= 131072, "storage envelope");
        require(identity.path_count() <= 3 && report.close_notifications <= 1, "path and terminal bounds");
        report.trace.insert(report.trace.end(), {e.kind, static_cast<std::uint64_t>(result), recovery.bytes_in_flight(),
            flow.sent(), static_cast<unsigned>(lifecycle.state()), identity.path_count(), storage.data().budget.in_use(storage_resource),
            storage.critical().budget.in_use(storage_resource)});
    }
};
quic_fuzz::report replay(std::span<const std::uint8_t> input) {
    auto budget = root_budget();
    quic_fuzz::report result;
    {
        recovery_driver driver(budget);
        result = quic_fuzz::drive(input, [&](auto e, auto& r) { driver.step(e, r); });
        driver.stop();
        require(!driver.recovery.reserve_packet(space::application), "terminal recovery cannot restart");
    }
    require(budget.in_use(storage_resource) == 0, "all recovery reservations released");
    result.released = true;
    return result;
}
}  // namespace
quic_fuzz::report quic_recovery_fuzz_input(std::span<const std::uint8_t> input) {
    auto result = replay(input);
    require(result == replay(input), "deterministic recovery event replay");
    return result;
}
#ifdef QUIC_RECOVERY_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    quic_recovery_fuzz_input({data, size});
    return 0;
}
#endif
