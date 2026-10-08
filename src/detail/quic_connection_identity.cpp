/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <utility>
#include <httpserver/detail/quic_connection_identity.hpp>
namespace httpserver::detail {
namespace {
quic_cid copy_cid(std::span<const std::byte> bytes) {
    quic_cid result;
    result.size = bytes.size();
    std::copy(bytes.begin(), bytes.end(), result.bytes.begin());
    return result;
}
std::span<const std::byte> cid_bytes(const quic_cid& cid) { return std::span(cid.bytes).first(cid.size); }
bool probing(const quic_frame& frame) {
    return std::holds_alternative<quic_padding_frame>(frame) || std::holds_alternative<quic_path_frame>(frame) || std::holds_alternative<quic_new_connection_id_frame>(frame);
}
quic_identity_code recovery_code(quic_recovery_code code) {
    if (code == quic_recovery_code::no_memory) return quic_identity_code::no_memory;
    if (code == quic_recovery_code::busy) return quic_identity_code::busy;
    return quic_identity_code::capacity;
}
}  // namespace
struct quic_connection_identity::state {
    struct cid_record {
        quic_cid cid;
        std::array<std::byte, 16> reset{};
        std::uint64_t sequence = 0;
        bool active = false, token_known = false;
        std::optional<quic_datagram_dispatch::cid_registration> route;
        quic_information_id advertisement = 0;
    };
    struct retirement {
        std::uint64_t sequence = 0;
        quic_information_id information = 0;
    };
    struct path_record {
        std::shared_ptr<quic_amplification_budget> budget;
        quic_cid incoming;
        std::size_t peer_cid = 0;
        std::optional<std::array<std::byte, 8>> response, sent_challenge;
        std::optional<time_point> sent_until, retry_due, abandon_due;
        unsigned attempts = 0;
        bool challenge_due = false, padded_challenge = false, mtu_validated = false;
    };
    std::array<cid_record, max_cid_history> local{}, peer{};
    std::array<retirement, max_pending_retirements> retirements{};
    std::array<path_record, max_paths> paths{};
    std::size_t local_used = 1, peer_used = 1, active = 0;
    std::uint64_t floor = 0, peer_limit = 2;
    std::optional<std::uint64_t> newest;
    std::optional<std::size_t> intent;
    quic_recovery& recovery;
    const server::peer_policy& policy;
    random_fill random;
    quic_storage_lease storage;
    server::reservation metadata;
    quic_datagram_dispatch* routes = nullptr;
    std::optional<io_connection_owner::datagram_port> owner;
    std::shared_ptr<datagram_sink> forwarder;
    struct preparation {
        quic_identity_control control;
        quic_amplification_budget::reservation debit;
        std::size_t path;
        bool padded;
    };
    std::optional<preparation> pending;
    state(quic_cid local_cid, quic_cid peer_cid, std::shared_ptr<quic_amplification_budget> admitted, quic_recovery& r, const server::peer_policy& p, random_fill rng, quic_storage_lease lease)
        : recovery(r), policy(p), random(std::move(rng)), storage(std::move(lease)) {
        if (!local_cid.size || local_cid.size > 20 || peer_cid.size > 20 || !admitted || !random) throw std::invalid_argument("Invalid QUIC identity seed");
        if (!storage.budget.reserve(server::resource::quic_reassembly_bytes, sizeof(state) + max_paths * 2048 + max_cid_history * 256, metadata).ok()) throw std::bad_alloc();
        local[0].cid = local_cid;
        local[0].active = true;
        peer[0].cid = peer_cid;
        peer[0].active = true;
        paths[0].budget = std::move(admitted);
        paths[0].incoming = local_cid;
        paths[0].mtu_validated = true;
    }
    std::size_t peer_count() const {
        return std::count_if(peer.begin(), peer.begin() + peer_used, [](const auto& c) { return c.active; });
    }
    std::optional<std::size_t> find_path(const quic_path_identity& path) const {
        for (std::size_t i = 0; i < paths.size(); ++i)
            if (paths[i].budget && paths[i].budget->path() == path) return i;
        return {};
    }
    bool nat_rebinding(const quic_authenticated_path& packet) const {
        const auto& old = paths[active];
        const auto& p = old.budget->path();
        return packet.destination == old.incoming && packet.path.local == p.local && packet.path.socket_id == p.socket_id && packet.path.interface_index == p.interface_index;
    }
    std::optional<std::size_t> select_peer(const quic_authenticated_path& packet) const {
        for (std::size_t i = 0; i < peer_used; ++i) {
            if (!peer[i].active) continue;
            const bool unused = std::none_of(paths.begin(), paths.end(), [i](const auto& p) { return p.budget && p.peer_cid == i; });
            if (unused) return i;
        }
        if (nat_rebinding(packet) && peer[paths[active].peer_cid].active) return paths[active].peer_cid;
        return {};
    }
    quic_identity_result retain_retirement(std::uint64_t sequence) {
        for (const auto& r : retirements)
            if (r.information && r.sequence == sequence) return {};
        auto slot = std::find_if(retirements.begin(), retirements.end(), [](auto r) { return !r.information; });
        if (slot == retirements.end()) return {quic_identity_code::capacity};
        auto retained = recovery.retain_retire_connection_id({sequence});
        if (!retained) return {recovery_code(retained.code)};
        *slot = {sequence, retained.id};
        return {};
    }
    quic_identity_result receive_new(const quic_new_connection_id_frame& frame, std::uint64_t packet_floor) {
        if (frame.sequence > k_quic_max_integer || frame.retire_prior_to > frame.sequence || frame.cid.empty() || frame.cid.size() > 20 || frame.reset_token.size() != 16)
            return {quic_identity_code::protocol};
        const auto cid = copy_cid(frame.cid);
        auto existing = std::find_if(peer.begin(), peer.begin() + peer_used, [&](const auto& c) { return c.sequence == frame.sequence; });
        if (existing != peer.begin() + peer_used && (existing->cid != cid || (existing->token_known && !std::equal(existing->reset.begin(), existing->reset.end(), frame.reset_token.begin()))))
            return {quic_identity_code::protocol};
        const auto new_floor = std::max({floor, frame.retire_prior_to, packet_floor});
        if (frame.sequence < new_floor) {
            auto result = retain_retirement(frame.sequence);
            if (!result) return result;
        } else {
            for (const auto& c : peer) {
                if (&c == (existing == peer.begin() + peer_used ? nullptr : &*existing)) continue;
                if ((c.cid.size && c.cid == cid) || (c.token_known && std::equal(c.reset.begin(), c.reset.end(), frame.reset_token.begin()))) return {quic_identity_code::protocol};
            }
            const auto remaining = std::count_if(peer.begin(), peer.begin() + peer_used, [&](const auto& c) { return c.active && c.sequence >= new_floor; });
            if (remaining + (existing == peer.begin() + peer_used) > max_active_cids) return {quic_identity_code::protocol};
            if (existing == peer.begin() + peer_used) {
                if (peer_used == peer.size()) return {quic_identity_code::capacity};
                existing = peer.begin() + peer_used++;
                existing->cid = cid;
                existing->sequence = frame.sequence;
            }
            existing->active = true;
            existing->token_known = true;
            std::copy(frame.reset_token.begin(), frame.reset_token.end(), existing->reset.begin());
        }
        return {};
    }
    quic_identity_result retire_peer_cids(std::uint64_t new_floor) {
        // Reserve every required reliable retirement before changing selection.
        for (const auto& c : peer)
            if (c.active && c.sequence < new_floor) {
                auto result = retain_retirement(c.sequence);
                if (!result) return result;
            }
        floor = new_floor;
        for (auto& c : peer)
            if (c.sequence < floor) c.active = false;
        auto replacement = std::find_if(peer.begin(), peer.begin() + peer_used, [](const auto& c) { return c.active; });
        if (replacement == peer.begin() + peer_used) return {quic_identity_code::protocol};
        for (std::size_t i = 0; i < paths.size(); ++i) {
            auto& p = paths[i];
            if (!p.budget || peer[p.peer_cid].active) continue;
            if (i == active) {
                p.peer_cid = replacement - peer.begin();
            } else {
                // Revalidate with an unused CID rather than reusing the active
                // CID on another local endpoint or accepting its old probe.
                if (intent == i) intent.reset();
                p = {};
            }
        }
        return {};
    }
    bool activate(time_point now) {
        if (!intent || *intent == active || pending) return false;
        auto& candidate = paths[*intent];
        if (!candidate.budget || !candidate.budget->validated() || !candidate.mtu_validated || !policy.classify(candidate.budget->path().peer.peer).accepted) return false;
        if (!recovery.activate_path()) return false;
        const auto interval = std::min(retry_interval(), std::chrono::steady_clock::duration(std::chrono::seconds(5)));
        paths[active].abandon_due = now + 3 * interval;
        active = *intent;
        candidate.abandon_due.reset();
        return true;
    }
    std::chrono::steady_clock::duration retry_interval() const { return recovery.path_probe_interval(); }
};
quic_connection_identity::quic_connection_identity(quic_cid local, quic_cid peer, std::shared_ptr<quic_amplification_budget> admitted, quic_recovery& recovery, const server::peer_policy& policy,
                                                   random_fill random, quic_storage_lease storage)
    : state_(std::make_unique<state>(local, peer, std::move(admitted), recovery, policy, std::move(random), std::move(storage))) {}
quic_connection_identity::~quic_connection_identity() {
    auto& s = *state_;
    if (s.pending) complete_control(s.pending->control.token, quic_identity_emission::unsent, {});
    if (s.routes)
        for (auto& c : s.local)
            if (c.route) s.routes->remove(*c.route);
}
quic_identity_result quic_connection_identity::attach_routes(quic_datagram_dispatch& routes, io_connection_owner::datagram_port owner, std::shared_ptr<datagram_sink> forwarder,
                                                             quic_datagram_dispatch::cid_registration initial) {
    auto& s = *state_;
    if (s.routes || !forwarder) return {quic_identity_code::protocol};
    s.routes = &routes;
    s.owner = owner;
    s.forwarder = std::move(forwarder);
    s.local[0].route = std::move(initial);
    return {};
}
void quic_connection_identity::set_peer_active_limit(std::uint64_t limit) { state_->peer_limit = std::min<std::uint64_t>(limit, max_active_cids); }
quic_identity_result quic_connection_identity::issue_cid() {
    auto& s = *state_;
    if (!s.routes || !s.owner) return {quic_identity_code::busy};
    const auto active_count = std::count_if(s.local.begin(), s.local.end(), [](const auto& c) { return c.active; });
    if (s.local_used == max_cid_history || active_count >= s.peer_limit) return {quic_identity_code::capacity};
    state::cid_record c;
    c.sequence = s.local_used;
    c.cid.size = s.local[0].cid.size;
    bool unique = false;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        if (!s.random(std::span(c.cid.bytes).first(c.cid.size)) || !s.random(c.reset)) return {quic_identity_code::random_failure};
        unique = std::none_of(s.local.begin(), s.local.begin() + s.local_used, [&](const auto& old) { return old.cid == c.cid || old.reset == c.reset; });
        if (!unique) continue;
        c.route = s.routes->register_cid(c.cid, *s.owner, s.forwarder);
        if (c.route) break;
        unique = false;
    }
    if (!unique || !c.route) return {quic_identity_code::capacity};
    auto retained = s.recovery.retain_new_connection_id({c.sequence, 0, cid_bytes(c.cid), c.reset});
    if (!retained) {
        s.routes->remove(*c.route);
        return {recovery_code(retained.code)};
    }
    c.advertisement = retained.id;
    c.active = c.token_known = true;
    s.local[s.local_used++] = std::move(c);
    return {};
}
quic_identity_result quic_connection_identity::receive(const quic_authenticated_path& packet, std::span<const quic_frame> frames, time_point now) {
    auto& s = *state_;
    if (!packet.authenticated || !packet.fresh || packet.number > k_quic_max_integer) return {quic_identity_code::dropped};
    if (s.pending || s.recovery.preparation_pending()) return {quic_identity_code::busy};
    // The caller parsed all frame shapes before passing their borrowed views.
    for (const auto& frame : frames)
        if (auto* p = std::get_if<quic_path_frame>(&frame); p && p->data.size() != 8) {
            return {quic_identity_code::protocol};
        }
    auto packet_floor = s.floor;
    for (const auto& frame : frames)
        if (const auto* c = std::get_if<quic_new_connection_id_frame>(&frame)) {
            if (c->retire_prior_to > c->sequence) return {quic_identity_code::protocol};
            packet_floor = std::max(packet_floor, c->retire_prior_to);
        }
    for (const auto& frame : frames)
        if (auto* c = std::get_if<quic_new_connection_id_frame>(&frame)) {
            auto result = s.receive_new(*c, packet_floor);
            if (!result) return result;
        }
    // Later NEW frames may supply the only CID surviving this packet's floor.
    auto retired = s.retire_peer_cids(packet_floor);
    if (!retired) return retired;
    for (const auto& frame : frames)
        if (auto* c = std::get_if<quic_retire_connection_id_frame>(&frame)) {
            if (c->sequence >= s.local_used || s.local[c->sequence].cid == packet.destination) return {quic_identity_code::protocol};
            auto& local = s.local[c->sequence];
            if (local.active) {
                if (local.route) {
                    s.routes->remove(*local.route);
                    local.route.reset();
                }
                local.active = false;
                // Retain bounded token history; inactive IDs no longer authorize resets.
                s.recovery.cancel_information(local.advertisement);
                issue_cid();
            }
        }
    auto index = s.find_path(packet.path);
    if (!index) {
        if (!s.policy.classify(packet.path.peer.peer).accepted) return {quic_identity_code::policy_denied};
        auto destination = s.select_peer(packet);
        if (!destination) return {quic_identity_code::dropped};
        for (std::size_t i = 0; i < max_paths; ++i)
            if (!s.paths[i].budget) {
                index = i;
                break;
            }
        if (!index) return {quic_identity_code::dropped};
        auto& p = s.paths[*index];
        try {
            p.budget = std::make_shared<quic_amplification_budget>(packet.path);
        } catch (const std::bad_alloc&) {
            return {quic_identity_code::no_memory};
        }
        p.incoming = packet.destination;
        p.peer_cid = *destination;
        const auto interval = std::min(s.retry_interval(), std::chrono::steady_clock::duration(std::chrono::seconds(5)));
        p.abandon_due = now + 3 * interval;
    }
    auto& path = s.paths[*index];
    if (*index != s.active && !s.policy.classify(packet.path.peer.peer).accepted) return {quic_identity_code::policy_denied};
    if (packet.received_bytes) path.budget->receive_datagram(packet.received_bytes);
    bool nonprobing = std::any_of(frames.begin(), frames.end(), [](const auto& f) { return !probing(f); });
    if (nonprobing && (!s.newest || packet.number > *s.newest)) {
        s.newest = packet.number;
        s.intent = index;
        if (*index != s.active) {
            if (!path.budget->validated()) path.challenge_due = !path.sent_challenge;
            auto& old = s.paths[s.active];
            if (!old.sent_challenge) old.challenge_due = true;
        }
    }
    for (const auto& frame : frames)
        if (const auto* control = std::get_if<quic_path_frame>(&frame)) {
            std::array<std::byte, 8> token;
            std::copy(control->data.begin(), control->data.end(), token.begin());
            if (!control->response) {
                path.response = token;
            } else {
                for (auto& sent : s.paths) {
                    // RFC 9000 8.2.3: arrival may differ; validate the challenge's sent path.
                    if (!sent.budget || sent.sent_challenge != token || !sent.sent_until || now >= *sent.sent_until) continue;
                    sent.budget->mark_address_validated();
                    sent.mtu_validated |= sent.padded_challenge;
                    sent.sent_challenge.reset();
                    sent.sent_until.reset();
                    sent.retry_due.reset();
                    sent.challenge_due = !sent.mtu_validated;
                }
            }
        }
    quic_identity_result result;
    result.activated = s.activate(now);
    result.allow_payload = *index == s.active && path.budget->validated();
    // Before handshake, only the admitted path can carry CRYPTO/ACK work.
    if (*index == s.active && s.active == 0 && !path.budget->validated()) result.allow_payload = true;
    return result;
}
std::optional<quic_identity_control> quic_connection_identity::prepare_control(time_point now, std::size_t overhead) {
    auto& s = *state_;
    if (s.pending || overhead + 9 > 1200) return {};
    expire(now);
    for (unsigned pass = 0; pass < 2; ++pass)
        for (std::size_t n = 0; n < max_paths; ++n) {
            const auto i = (s.intent.value_or(s.active) + n) % max_paths;
            auto& p = s.paths[i];
            if (!p.budget || (pass == 0 ? !p.response : !p.challenge_due)) continue;
            quic_identity_control control;
            control.path = p.budget->path();
            control.budget = p.budget;
            control.destination = s.peer[p.peer_cid].cid;
            const auto packet_overhead = overhead + control.destination.size;
            if (packet_overhead + 9 > 1200) continue;
            control.response = pass == 0;
            if (control.response) control.data = *p.response;
            else if (p.attempts >= 3 || !s.random(control.data)) continue;
            auto debit = p.budget->reserve_send(1200);
            control.wire_bytes = 1200;
            if (!debit) {
                control.wire_bytes = packet_overhead + 9;
                debit = p.budget->reserve_send(control.wire_bytes);
            }
            if (!debit) continue;
            std::array<std::byte, 1200> payload{};
            quic_send_request request;
            request.max_wire_bytes = control.wire_bytes;
            request.protection_overhead = packet_overhead;
            control.packet = s.recovery.prepare_path_packet({control.response, control.data}, payload, now, request, i == s.active);
            if (!control.packet) {
                p.budget->cancel_unsent(*debit);
                continue;
            }
            control.token = control.packet.token;
            s.pending = state::preparation{control, *debit, i, control.wire_bytes == 1200};
            return control;
        }
    return {};
}
quic_identity_result quic_connection_identity::complete_control(std::uint64_t token, quic_identity_emission outcome, time_point now, std::uint64_t generation) {
    auto& s = *state_;
    if (!s.pending || s.pending->control.token != token) return {quic_identity_code::protocol};
    auto& prepared = *s.pending;
    auto& p = s.paths[prepared.path];
    if (outcome == quic_identity_emission::unsent) {
        s.recovery.abandon_packet(token);
        p.budget->cancel_unsent(prepared.debit);
    } else {
        auto result = s.recovery.commit_sent(token, now, prepared.control.wire_bytes, true, true, generation);
        if (!result) return {recovery_code(result.code)};
        p.budget->complete_send(prepared.debit);
        if (prepared.control.response) {
            p.response.reset();
        } else {
            p.sent_challenge = prepared.control.data;
            p.padded_challenge = prepared.padded;
            const auto delay = s.retry_interval();
            const auto maximum = time_point::max().time_since_epoch().count();
            p.retry_due = p.sent_until = now.time_since_epoch().count() > maximum - delay.count() ? time_point::max() : now + delay;
            p.challenge_due = false;
            ++p.attempts;
        }
    }
    s.pending.reset();
    return {};
}
void quic_connection_identity::information_completed(const quic_information_completion& completion) {
    if (completion.kind != quic_information_kind::retire_connection_id) return;
    for (auto& r : state_->retirements)
        if (r.information == completion.id) r = {};
}
void quic_connection_identity::expire(time_point now) {
    auto& s = *state_;
    for (std::size_t i = 0; i < max_paths; ++i) {
        auto& p = s.paths[i];
        if (!p.budget || (s.pending && s.pending->path == i)) continue;
        if (i != s.active && p.abandon_due && now >= *p.abandon_due) {
            if (s.intent == i) s.intent.reset();
            p = {};
            continue;
        }
        if (p.retry_due && now >= *p.retry_due) {
            p.sent_challenge.reset();
            p.sent_until.reset();
            p.retry_due.reset();
            p.challenge_due = p.attempts < 3;
        }
    }
}
std::optional<quic_connection_identity::time_point> quic_connection_identity::deadline() const {
    std::optional<time_point> result;
    for (const auto& p : state_->paths)
        for (auto due : {p.retry_due, p.abandon_due})
            if (due && (!result || *due < *result)) result = due;
    return result;
}
const quic_path_identity& quic_connection_identity::application_path() const { return state_->paths[state_->active].budget->path(); }
std::shared_ptr<quic_amplification_budget> quic_connection_identity::application_budget() const { return state_->paths[state_->active].budget; }
quic_cid quic_connection_identity::application_cid() const { return state_->peer[state_->paths[state_->active].peer_cid].cid; }
std::size_t quic_connection_identity::active_peer_cids() const { return state_->peer_count(); }
std::size_t quic_connection_identity::path_count() const {
    return std::count_if(state_->paths.begin(), state_->paths.end(), [](const auto& p) { return !!p.budget; });
}
std::uint64_t quic_connection_identity::retirement_floor() const { return state_->floor; }
}  // namespace httpserver::detail
