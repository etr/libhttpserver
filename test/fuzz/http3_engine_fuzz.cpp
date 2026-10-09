/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/detail/http3_connection.hpp>
#include "fuzz/http3_engine_fuzz.hpp"
#include "unit/quic_event_replay.hpp"
namespace {
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
using quic_fuzz::require;
constexpr std::size_t input_cap = 4096, stream_cap = 16, event_cap = 64;
// One consuming call and one scheduler yield per byte, plus a stalled call
// per explicit feed/pump visit and final drain visit. Keep a hard loop bound.
constexpr std::size_t call_cap = 2 * input_cap + event_cap + (event_cap + stream_cap) * stream_cap;
constexpr std::size_t data_capacity = 16384, critical_capacity = 65536;
std::uint64_t stream_id(unsigned slot) { return slot < 8 ? slot * 4 : (slot - 8) * 4 + 2; }
hd::http3_role role(unsigned slot) { return static_cast<hd::http3_role>(static_cast<unsigned>(hd::http3_role::control) + slot % 3); }
hd::http3_limits limits(unsigned profile) {
    hd::http3_limits l;
    l.headers = {128, 256, 8}; l.qpack_capacity = 128; l.qpack_blocked_streams = 2;
    l.qpack_retained_bytes = 256; l.qpack_instruction_bytes = 128; l.qpack_output_bytes = 128;
    l.qpack_outstanding_sections = 8; l.settings_bytes = 128; l.settings_identifiers = 8;
    l.data_chunk = 16; l.frames_per_turn = 2; l.stream_records = stream_cap;
    switch (profile) {
        case 1: l.headers.max_expanded_bytes = 41; break;
        case 2: l.headers.max_fields = 0; break;
        case 3: l.headers.max_compressed_bytes = 2; break;
        case 4: l.qpack_blocked_streams = 0; break;
        case 5: l.qpack_retained_bytes = 3; break;
        case 6: l.qpack_output_bytes = 10; break;
        case 9: l.qpack_instruction_bytes = 3; break;
        case 10: l.qpack_outstanding_sections = 0; break;
        case 11: l.qpack_capacity = 0; break;
        case 12: l.qpack_retained_bytes = 2; break;
        case 13: l.headers.max_expanded_bytes = 42; break;
        case 14: l.settings_bytes = 0; break;
        case 15: l.headers.max_fields = 1; break;
        default: break;
    }
    return l;
}
void accounting(const hs::resource_budget& budget, bool empty = false) {
    for (unsigned i = 0; i < hs::resource_count; ++i) {
        const auto resource = static_cast<hs::resource>(i);
        require(budget.in_use(resource) <= budget.capacity(resource), "HTTP/3 budget capacity");
        if (empty) require(budget.in_use(resource) == 0, "HTTP/3 teardown");
    }
}
struct pending_stream {
    bool attached = false, cancelled = false, terminal_delivered = false, was_blocked = false, observed = false;
    std::vector<std::byte> bytes;
    std::size_t position = 0;
};
class replay_state {
 public:
    replay_state(hd::http3_connection& connection, const hs::resource_budget& root,
                 const hs::resource_budget& child, hd::quic_storage_pool& pool, unsigned profile, std::size_t fragment)
        : connection_(connection), root_(root), child_(child), pool_(pool), profile_(profile), fragment_(fragment) {}
    http3_fuzz_report run(std::span<const std::uint8_t> input) {
        std::size_t at = std::min(input.size(), std::size_t{1});
        for (unsigned count = 0; at + 3 <= input.size() && count < event_cap && !stopped_; ++count) {
            const auto op = input[at++] % 13;
            const auto slot = input[at++] % stream_cap;
            const auto requested = input[at++];
            const auto size = std::min<std::size_t>(requested, input.size() - at);
            const auto payload = input.subspan(at, size); at += size;
            ++report_.attempted[op];
            operation(op, slot, payload);
            observe();
            pump();
            check();
            if (report_.error) break;
        }
        for (unsigned round = 0; round < stream_cap && !report_.error && !stopped_; ++round) {
            connection_.begin_turn(); pump();
        }
        report_.settings = connection_.peer_settings().received;
        report_.blocked_final = root_.in_use(hs::resource::blocked_headers);
        if (report_.error && !stopped_) {
            const auto sticky = connection_.feed(0, {}, connection_.offset(0));
            require(!sticky.consumed && sticky.error && sticky.error->wire_code == report_.error, "sticky HTTP/3 error");
        }
        connection_.shutdown(); connection_.shutdown();
        require(!root_.in_use(hs::resource::qpack_table_bytes) && !root_.in_use(hs::resource::blocked_headers), "shutdown QPACK release");
        check(); return report_;
    }

 private:
    void record(std::optional<hd::http3_error> error) {
        if (!error) return;
        require(error->scope == hd::http3_error_scope::connection, "typed HTTP/3 scope");
        const auto code = error->wire_code;
        require(code == 0x102 || code == 0x103 || code == 0x104 || code == 0x105 || code == 0x106 ||
            code == 0x107 || code == 0x109 || code == 0x10a || code == 0x200 || code == 0x201 || code == 0x202,
            "defined HTTP/3 wire error");
        require(error->outcome == (code == 0x107 ? httpserver::http::outcome_code::limit_exceeded :
            httpserver::http::outcome_code::protocol_error), "HTTP/3 error outcome mapping");
        if (!report_.error) {
            report_.error = code;
            if (code == 0x107) {
                ++report_.attempted[20]; ++report_.accepted[20];
            }
        }
        require(report_.error == code, "stable HTTP/3 error");
    }
    void check() {
        accounting(root_); accounting(child_); accounting(pool_.data().budget); accounting(pool_.critical().budget);
        require(root_.in_use(hs::resource::blocked_headers) <= 2 && root_.in_use(hs::resource::qpack_table_bytes) <= 256,
            "HTTP/3 QPACK ancestor ceilings");
        require(report_.observations.size() <= 2048 && copied_ <= 16384 && calls_ <= call_cap, "HTTP/3 replay work ceiling");
    }
    bool attach(unsigned slot) {
        if (streams_[slot].attached) return true;
        record(connection_.attach_stream(stream_id(slot)));
        if (report_.error) return false;
        streams_[slot].attached = true; return true;
    }
    void observe() {
        for (unsigned slot = 0; slot < stream_cap; ++slot) {
            auto& s = streams_[slot];
            if (!s.attached) continue;
            const auto id = stream_id(slot);
            const bool blocked = connection_.blocked(id);
            if (blocked && !s.was_blocked) {
                ++report_.attempted[17]; ++report_.accepted[17];
            }
            if (blocked) s.was_blocked = true;
            const auto* event = connection_.event(id);
            require(!s.cancelled || !event, "cancelled section cannot reappear");
            if (!event || s.observed) continue;
            if (s.was_blocked && !blocked) {
                ++report_.attempted[19]; ++report_.accepted[19]; s.was_blocked = false;
            }
            http3_fuzz_observation seen{id, event->type, event->frame_begin, {}};
            if (event->type == 1) {
                ++report_.attempted[16]; ++report_.accepted[16]; std::size_t expanded = 0;
                require(event->fields.size() <= limits(profile_).headers.max_fields, "HTTP/3 field count");
                for (const auto& field : event->fields) {
                    expanded += 32 + field.name.size() + field.value.size();
                    seen.value += std::to_string(field.name.size()) + ":" + field.name;
                    seen.value += std::to_string(field.value.size()) + ":" + field.value + (field.never_indexed ? "!" : ".");
                }
                require(expanded <= limits(profile_).headers.max_expanded_bytes, "HTTP/3 expanded bytes");
            } else {
                require(event->payload.size() <= (event->type == 0 ? 16U : 128U), "HTTP/3 payload bound");
                seen.value.assign(reinterpret_cast<const char*>(event->payload.data()), event->payload.size());
            }
            copied_ += seen.value.size();
            // DATA chunks are transport fragments of the same frame.
            if (event->type == 0 && !report_.observations.empty() && report_.observations.back().stream == id &&
                report_.observations.back().type == 0 && report_.observations.back().begin == seen.begin)
                report_.observations.back().value += seen.value;
            else
                report_.observations.push_back(std::move(seen));
            if (profile_ == 7) s.observed = true;
            else connection_.release_event(id);
        }
    }
    void feed(unsigned slot, bool bad_offset = false) {
        auto& s = streams_[slot];
        while (s.position < s.bytes.size() && !report_.error && !stopped_) {
            require(++calls_ <= call_cap, "HTTP/3 feed call cap");
            const auto size = std::min(fragment_, s.bytes.size() - s.position);
            const auto offset = connection_.offset(stream_id(slot));
            const auto result = connection_.feed(stream_id(slot), std::span(s.bytes).subspan(s.position, size), offset + (bad_offset ? 1 : 0));
            require(result.consumed <= size, "HTTP/3 consumed input");
            require(connection_.offset(stream_id(slot)) == offset + result.consumed, "HTTP/3 exact offset advancement");
            s.position += result.consumed; record(result.error); observe(); check();
            if (result.progress == hd::http3_progress::yield) {
                // A defined scheduler turn services a production yield.
                connection_.begin_turn(); continue;
            }
            if (!result.consumed || connection_.blocked(stream_id(slot)) || s.observed) break;
        }
    }
    void pump() {
        if (report_.error || stopped_) return;
        for (unsigned slot = 0; slot < stream_cap; ++slot)
            if (streams_[slot].attached && !streams_[slot].cancelled) feed(slot);
    }
    void advance(unsigned slot, std::span<const std::uint8_t> payload) {
        auto prefix = connection_.local_prefix(role(slot));
        const auto n = payload.empty() ? 0 : payload[0];
        require(prefix.size() <= 138, "bounded critical output");
        const auto copied = std::min<std::size_t>(n, prefix.size());
        const std::string saved(reinterpret_cast<const char*>(prefix.data()), copied);
        const bool okay = connection_.advance_local_prefix(role(slot), n);
        if (okay) {
            report_.output[slot % 3] += saved; copied_ += copied; ++report_.accepted[4];
        } else {
            require(std::ranges::equal(prefix, connection_.local_prefix(role(slot))), "output refusal preserves prefix");
        }
    }
    void operation(unsigned op, unsigned slot, std::span<const std::uint8_t> payload) {
        if (op == 3) {
            connection_.begin_turn(); ++report_.accepted[op]; return;
        }
        if (op == 4) {
            advance(slot, payload); return;
        }
        if (op == 10) {
            connection_.shutdown(); stopped_ = true; ++report_.accepted[op]; return;
        }
        if (!attach(slot)) return;
        auto& s = streams_[slot]; const auto id = stream_id(slot);
        switch (op) {
            case 0: case 11: {
                // Retain borrowed DATA until observation/release before growing input.
                if (s.observed) break;
                require(s.bytes.size() + payload.size() <= input_cap, "bounded retained harness input");
                for (auto byte : payload) s.bytes.push_back(static_cast<std::byte>(byte));
                feed(slot, op == 11);
                if (!report_.error) ++report_.accepted[op];
                break;
            }
            case 1: case 2: {
                const bool was_blocked = connection_.blocked(id);
                const bool first_terminal = !s.terminal_delivered;
                record(connection_.terminal(id, {op == 1 ? hd::quic_terminal_kind::reset : hd::quic_terminal_kind::eof}));
                if (!report_.error) {
                    ++report_.accepted[op];
                    s.terminal_delivered = true;
                    if (op == 1 && first_terminal) {
                        if (was_blocked) {
                            ++report_.attempted[18]; ++report_.accepted[18];
                        }
                        s.cancelled = true; s.was_blocked = false; s.bytes.clear(); s.position = 0;
                        require(!connection_.blocked(id), "reset releases blocked reservation");
                    }
                }
                break;
            }
            case 5: {
                const std::array<hd::qpack_field_view, 2> fields{{{"x", "a", true}, {"x", "b", true}}};
                auto encoded = connection_.encode_response_section(id, fields);
                require(encoded.value.size() <= 128, "outgoing section bound");
                if (encoded.status.ok()) {
                    ++report_.accepted[op]; tokens_[slot] = encoded.token;
                    if (!payload.empty() && (payload[0] & 1)) connection_.publish_section(encoded.token);
                }
                break;
            }
            case 6: connection_.cancel_section(tokens_[slot]); tokens_[slot] = 0; ++report_.accepted[op]; break;
            case 7: {
                const auto decoded = hd::qpack_decode_encoder_instruction(payload, {128, 128, 128});
                if (decoded.status.ok() && connection_.queue_encoder_instruction(decoded.instruction).ok()) ++report_.accepted[op];
                break;
            }
            case 8: connection_.publish_section(tokens_[slot]); ++report_.accepted[op]; break;
            case 9: connection_.cancel_unpublished(id); ++report_.accepted[op]; break;
            case 12: connection_.release_event(id); s.observed = false; ++report_.accepted[op]; break;
            default: break;
        }
    }
    hd::http3_connection& connection_;
    const hs::resource_budget& root_;
    const hs::resource_budget& child_;
    hd::quic_storage_pool& pool_;
    unsigned profile_;
    std::size_t fragment_, calls_ = 0, copied_ = 0;
    bool stopped_ = false;
    std::array<pending_stream, stream_cap> streams_{};
    std::array<std::uint64_t, stream_cap> tokens_{};
    http3_fuzz_report report_;
};
http3_fuzz_report replay(std::span<const std::uint8_t> input, std::size_t fragment) {
    input = input.first(std::min(input.size(), input_cap));
    const unsigned profile = input.empty() ? 0 : input[0] % 16;
    hs::budget_limits capacity;
    capacity.set(hs::resource::quic_reassembly_bytes, data_capacity + critical_capacity);
    capacity.set(hs::resource::qpack_table_bytes, 256); capacity.set(hs::resource::blocked_headers, 2);
    auto root = hs::resource_budget::root(capacity);
    http3_fuzz_report report;
    {
        auto child_limits = capacity;
        if (profile == 8) child_limits.set(hs::resource::qpack_table_bytes, 127);
        hs::resource_budget child; require(root.child(child_limits, child).ok(), "HTTP/3 child budget");
        hd::quic_storage_pool pool(data_capacity, critical_capacity, child);
        {
            hd::http3_connection connection(pool.data(), pool.critical(), limits(profile));
            report = replay_state(connection, root, child, pool, profile, fragment).run(input);
        }
        accounting(child); require(!root.in_use(hs::resource::qpack_table_bytes) && !root.in_use(hs::resource::blocked_headers), "connection QPACK release");
    }
    accounting(root, true); return report;
}
}  // namespace
http3_fuzz_report http3_fuzz_replay(std::span<const std::uint8_t> input) {
    auto result = replay(input, input_cap);
    require(result == replay(input, input_cap), "deterministic HTTP/3 replay");
    const auto fragmented = replay(input, 1);
    if (!input.empty() && input[0] % 16 == 7) {
        // A finite number of releases advances different byte extents when
        // DATA events are held. Even EOF can then see a different frame extent.
        // Check each delivery schedule independently, including its errors.
        require(fragmented == replay(input, 1), "deterministic held HTTP/3 fragments");
        return result;
    }
    // Invalid transcripts can fail after consuming different wire extents;
    // compare the defined failure. Valid schedules normalize DATA fragments.
    require(result.error == fragmented.error, "fragmented HTTP/3 error");
    if (!result.error) require(result == fragmented, "fragmented HTTP/3 observations");
    return result;
}
std::size_t http3_fuzz_replay_corpus(const char* directory) { return quic_fuzz::replay_corpus(directory, http3_fuzz_replay); }
#ifdef HTTP3_ENGINE_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) { http3_fuzz_replay({data, size}); return 0; }
#endif
