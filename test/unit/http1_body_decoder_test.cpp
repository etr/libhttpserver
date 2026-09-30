/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-106: incremental HTTP/1 body decoder unit tests.
//
// Pins detail::http1_body_decoder, the framing state machine behind the
// detail::body_source seam (PRD-V3N-REQ-004/017/021):
//   - every-split matrices (single-shot, two-way at every offset,
//     three-way, bytewise) under two drain disciplines — pull only at
//     the end, and pull-to-drain after every feed — must decode a
//     body identically;
//   - the staging budget applies backpressure inside decode() and the
//     pull releases it;
//   - the wire stays owned by the caller: octets past the message
//     boundary are never consumed;
//   - typed, sticky rejections with their close policies, and the
//     pull verdict order (failed, data, end, empty).

#include <httpserver/detail/http1_body_decoder.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "./littletest.hpp"

namespace {

namespace http = httpserver::http;
namespace detail = httpserver::detail;

using httpserver::detail::http1_body_budget;
using httpserver::detail::http1_body_decode;
using httpserver::detail::http1_body_decoder;
using httpserver::detail::http1_body_kind;
using httpserver::detail::http1_body_mode;
using httpserver::detail::http1_body_progress;
using httpserver::detail::http1_close_policy;

// The production defaults.
http1_body_budget default_budget() {
    return http1_body_budget();
}

std::vector<std::byte> bytes(const std::string& s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(std::byte(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string of(std::span<const std::byte> data) {
    std::string out;
    out.reserve(data.size());
    for (std::byte b : data) {
        out.push_back(static_cast<char>(b));
    }
    return out;
}

http1_body_mode none_mode() {
    return http1_body_mode();
}

http1_body_mode length_mode(std::uint64_t n) {
    http1_body_mode m;
    m.kind = http1_body_kind::length;
    m.content_length = n;
    return m;
}

// Everything one decode run observed. The reference and every split
// discipline must produce equal values.
struct decoded {
    std::string payload;
    http::fields trailers;
    bool complete = false;
    bool failed = false;
    http::outcome failure;
    http1_close_policy close_policy = http1_close_policy::none;
};

bool same(const decoded& a, const decoded& b) {
    if (a.payload != b.payload) return false;
    if (!(a.trailers == b.trailers)) return false;
    if (a.complete != b.complete) return false;
    if (a.failed != b.failed) return false;
    if (!a.failed) return true;
    return a.failure.code() == b.failure.code()
        && a.close_policy == b.close_policy;
}

using feed_schedule = std::vector<std::string>;

// Runs one decoder through `feeds`, draining the staging queue either
// only at the end (drain_each false) or after every decode call. A
// feed whose decode cannot progress (need_more or staging-blocked)
// ends the run; a truncated wire therefore stays incomplete.
decoded run_decode(const http1_body_mode& mode, const feed_schedule& feeds,
                   const http1_body_budget& budget, bool drain_each) {
    http1_body_decoder d(mode, budget);
    decoded out;
    std::vector<std::byte> sink(64);
    const auto drain = [&]() {
        for (;;) {
            const detail::body_pull_result pulled = d.pull(sink);
            if (pulled.kind != detail::body_pull::data) break;
            out.payload += of(std::span<const std::byte>(
                sink.data(), pulled.copied));
        }
    };
    for (const std::string& feed : feeds) {
        std::string_view wire = feed;
        while (!wire.empty()) {
            const http1_body_progress p = d.decode(wire);
            wire.remove_prefix(p.consumed);
            if (p.kind == http1_body_decode::failed) {
                out.failed = true;
                out.failure = d.failure();
                out.close_policy = d.close_policy();
                return out;
            }
            if (p.kind == http1_body_decode::complete) {
                out.complete = true;
                break;
            }
            if (drain_each) drain();
            if (p.consumed == 0) break;
        }
        if (out.complete || out.failed) break;
    }
    if (!out.failed) {
        drain();
        out.complete = d.message_complete();
    }
    out.trailers = d.trailers();
    return out;
}

// True iff `wire` decodes identically single-shot, under a two-way
// split at every offset, and bytewise — each in both drain
// disciplines — always to a complete message.
bool decodes_identically(const http1_body_mode& mode,
                         std::string_view wire,
                         const http1_body_budget& budget = default_budget()) {
    const decoded reference =
        run_decode(mode, {std::string(wire)}, budget, false);
    if (!reference.complete) return false;
    for (std::size_t split = 0; split <= wire.size(); ++split) {
        const feed_schedule parts = {std::string(wire.substr(0, split)),
                                     std::string(wire.substr(split))};
        if (!same(run_decode(mode, parts, budget, false), reference)) {
            return false;
        }
        if (!same(run_decode(mode, parts, budget, true), reference)) {
            return false;
        }
    }
    feed_schedule bytewise;
    bytewise.reserve(wire.size());
    for (const char c : wire) bytewise.emplace_back(1, c);
    if (!same(run_decode(mode, bytewise, budget, false), reference)) {
        return false;
    }
    return same(run_decode(mode, bytewise, budget, true), reference);
}

// True iff `wire` decodes identically under every three-way split.
bool decodes_identically_three_way(
    const http1_body_mode& mode, std::string_view wire,
    const http1_body_budget& budget = default_budget()) {
    const decoded reference =
        run_decode(mode, {std::string(wire)}, budget, false);
    if (!reference.complete) return false;
    for (std::size_t i = 0; i <= wire.size(); ++i) {
        for (std::size_t j = i; j <= wire.size(); ++j) {
            const feed_schedule parts = {std::string(wire.substr(0, i)),
                                         std::string(wire.substr(i, j - i)),
                                         std::string(wire.substr(j))};
            if (!same(run_decode(mode, parts, budget, false), reference)) {
                return false;
            }
            if (!same(run_decode(mode, parts, budget, true), reference)) {
                return false;
            }
        }
    }
    return true;
}

// Full payload of a fresh single-shot decode.
decoded decode_all(const http1_body_mode& mode, std::string_view wire,
                   const http1_body_budget& budget = default_budget()) {
    return run_decode(mode, {std::string(wire)}, budget, false);
}

}  // namespace

LT_BEGIN_SUITE(split_suite)

    void set_up() { }
    void tear_down() { }

LT_END_SUITE(split_suite)

LT_BEGIN_AUTO_TEST(split_suite, split_matrix_length_body)
    // Length framing must be boundary-agnostic: "hello" decodes the
    // same single-shot, under every two-way split, and bytewise.
    LT_CHECK(decodes_identically(length_mode(5), "hello"));
LT_END_AUTO_TEST(split_matrix_length_body)

LT_BEGIN_AUTO_TEST(split_suite, split_matrix_three_way_length_body)
    LT_CHECK(decodes_identically_three_way(length_mode(5), "hello"));
LT_END_AUTO_TEST(split_matrix_three_way_length_body)

LT_BEGIN_AUTO_TEST(split_suite, split_matrix_zero_length_body)
    // A zero-length body completes at construction; no wire is needed.
    const decoded d = decode_all(length_mode(0), "");
    LT_CHECK(d.complete);
    LT_CHECK(d.payload.empty());
    LT_CHECK(!d.failed);
    LT_CHECK(decodes_identically(length_mode(0), ""));
LT_END_AUTO_TEST(split_matrix_zero_length_body)

LT_BEGIN_AUTO_TEST(split_suite, split_matrix_length_across_full_wire)
    // A length body with trailing octets beyond the boundary (the next
    // pipelined message's head) must still decode, in both drain
    // disciplines and under splits: the residue stays with the caller.
    LT_CHECK(decodes_identically(length_mode(3), "abcGET / HTTP/1.1\r\n\r\n"));
LT_END_AUTO_TEST(split_matrix_length_across_full_wire)

LT_BEGIN_SUITE(contract_suite)

    void set_up() { }
    void tear_down() { }

LT_END_SUITE(contract_suite)

LT_BEGIN_AUTO_TEST(contract_suite, residue_never_consumed_past_boundary)
    http1_body_decoder d(length_mode(3), default_budget());
    const http1_body_progress p = d.decode("abcXYZ");
    LT_CHECK(p.kind == http1_body_decode::complete);
    LT_CHECK_EQ(p.consumed, 3u);
    LT_CHECK(d.message_complete());
    // Octets past the CL boundary belong to the caller.
    LT_CHECK_EQ(d.staged_bytes(), 3u);
    LT_CHECK_EQ(d.length_remaining(), 0u);
LT_END_AUTO_TEST(residue_never_consumed_past_boundary)

LT_BEGIN_AUTO_TEST(contract_suite, zero_length_completes_without_wire)
    http1_body_decoder d(length_mode(0), default_budget());
    LT_CHECK(d.message_complete());
    const http1_body_progress p = d.decode("GET / HTTP/1.1\r\n\r\n");
    LT_CHECK(p.kind == http1_body_decode::complete);
    LT_CHECK_EQ(p.consumed, 0u);
LT_END_AUTO_TEST(zero_length_completes_without_wire)

LT_BEGIN_AUTO_TEST(contract_suite, none_mode_completes_without_wire)
    http1_body_decoder d(none_mode(), default_budget());
    LT_CHECK(d.message_complete());
    const http1_body_progress p = d.decode("GET / HTTP/1.1\r\n\r\n");
    LT_CHECK(p.kind == http1_body_decode::complete);
    LT_CHECK_EQ(p.consumed, 0u);
    LT_CHECK(d.failure().ok());
LT_END_AUTO_TEST(none_mode_completes_without_wire)

LT_BEGIN_AUTO_TEST(contract_suite, empty_wire_needs_more)
    http1_body_decoder d(length_mode(5), default_budget());
    const http1_body_progress p = d.decode("");
    LT_CHECK(p.kind == http1_body_decode::need_more);
    LT_CHECK_EQ(p.consumed, 0u);
    LT_CHECK(!d.message_complete());
LT_END_AUTO_TEST(empty_wire_needs_more)

LT_BEGIN_AUTO_TEST(contract_suite, pull_empty_span_yields_empty)
    http1_body_decoder d(length_mode(3), default_budget());
    LT_CHECK(d.decode("abc").kind == http1_body_decode::complete);
    // An empty destination must never surface a zero-byte data pull.
    const detail::body_pull_result e = d.pull(std::span<std::byte>());
    LT_CHECK(e.kind == detail::body_pull::empty);
    LT_CHECK_EQ(e.copied, 0u);
    // The staged bytes are still there for a real pull.
    std::vector<std::byte> sink(8);
    const detail::body_pull_result r = d.pull(sink);
    LT_CHECK(r.kind == detail::body_pull::data);
    LT_CHECK_EQ(r.copied, 3u);
LT_END_AUTO_TEST(pull_empty_span_yields_empty)

LT_BEGIN_AUTO_TEST(contract_suite, data_then_end_pull_ordering)
    http1_body_decoder d(length_mode(3), default_budget());
    LT_CHECK(d.decode("abc").kind == http1_body_decode::complete);
    std::vector<std::byte> sink(8);
    const detail::body_pull_result data = d.pull(sink);
    LT_CHECK(data.kind == detail::body_pull::data);
    LT_CHECK(of(std::span(sink.data(), data.copied)) == "abc");
    // Queue drained + message_end: the authoritative end, repeatable.
    const detail::body_pull_result end = d.pull(sink);
    LT_CHECK(end.kind == detail::body_pull::end);
    const detail::body_pull_result end_again = d.pull(sink);
    LT_CHECK(end_again.kind == detail::body_pull::end);
LT_END_AUTO_TEST(data_then_end_pull_ordering)

LT_BEGIN_AUTO_TEST(contract_suite, end_without_data_for_zero_body)
    http1_body_decoder d(length_mode(0), default_budget());
    std::vector<std::byte> sink(8);
    const detail::body_pull_result end = d.pull(sink);
    LT_CHECK(end.kind == detail::body_pull::end);
    LT_CHECK_EQ(d.staged_bytes(), 0u);
LT_END_AUTO_TEST(end_without_data_for_zero_body)

LT_BEGIN_AUTO_TEST(contract_suite, trailers_empty_without_trailers)
    http1_body_decoder d(length_mode(2), default_budget());
    LT_CHECK(d.decode("hi").kind == http1_body_decode::complete);
    LT_CHECK(d.trailers().empty());
LT_END_AUTO_TEST(trailers_empty_without_trailers)

LT_BEGIN_AUTO_TEST(contract_suite, truncation_is_not_an_error)
    // A truncated body never completes and never fails: mapping the
    // premature EOF to a typed failure is the engine's job.
    http1_body_decoder d(length_mode(5), default_budget());
    const http1_body_progress p = d.decode("hel");
    LT_CHECK(p.kind == http1_body_decode::progressed);
    LT_CHECK_EQ(p.consumed, 3u);
    LT_CHECK(!d.message_complete());
    LT_CHECK(d.failure().ok());
    std::vector<std::byte> sink(8);
    LT_CHECK(d.pull(sink).kind == detail::body_pull::data);
    LT_CHECK(d.pull(sink).kind == detail::body_pull::empty);
LT_END_AUTO_TEST(truncation_is_not_an_error)

LT_BEGIN_AUTO_TEST(contract_suite, rejected_mode_is_sticky)
    http1_body_mode m;
    m.kind = http1_body_kind::rejected;
    m.failure = http::outcome(http::outcome_code::protocol_error,
                              "http1_body_mode: ambiguous framing");
    m.close_policy = http1_close_policy::close_now;
    http1_body_decoder d(m, default_budget());
    LT_CHECK(!d.message_complete());
    LT_CHECK(d.kind() == http1_body_kind::rejected);

    const http1_body_progress first = d.decode("anything at all");
    LT_CHECK(first.kind == http1_body_decode::failed);
    LT_CHECK_EQ(first.consumed, 0u);
    LT_CHECK(d.failure().code() == http::outcome_code::protocol_error);
    LT_CHECK(d.failure().message() == m.failure.message());
    LT_CHECK(d.close_policy() == http1_close_policy::close_now);

    std::vector<std::byte> sink(8);
    LT_CHECK(d.pull(sink).kind == detail::body_pull::failed);
    // Sticky: a later decode is a no-op with the same verdict.
    const http1_body_progress again = d.decode("x");
    LT_CHECK(again.kind == http1_body_decode::failed);
    LT_CHECK_EQ(again.consumed, 0u);
LT_END_AUTO_TEST(rejected_mode_is_sticky)

LT_BEGIN_SUITE(limits_suite)

    void set_up() { }
    void tear_down() { }

LT_END_SUITE(limits_suite)

LT_BEGIN_AUTO_TEST(limits_suite, staging_budget_applies_backpressure)
    // max_staged_bytes=4: the first decode stages only the four bytes
    // that fit and leaves the rest of the wire with the caller; a pull
    // releases the room and the remainder completes the message.
    http1_body_budget tight = default_budget();
    tight.max_staged_bytes = 4;
    http1_body_decoder d(length_mode(5), tight);

    const http1_body_progress first = d.decode("hello");
    LT_CHECK(first.kind == http1_body_decode::progressed);
    LT_CHECK_EQ(first.consumed, 4u);
    LT_CHECK_EQ(d.staged_bytes(), 4u);
    LT_CHECK(!d.message_complete());

    std::vector<std::byte> sink(8);
    const detail::body_pull_result pulled = d.pull(sink);
    LT_CHECK(pulled.kind == detail::body_pull::data);
    LT_CHECK(of(std::span(sink.data(), pulled.copied)) == "hell");
    LT_CHECK_EQ(d.staged_bytes(), 0u);

    const http1_body_progress second = d.decode("o");
    LT_CHECK(second.kind == http1_body_decode::complete);
    LT_CHECK_EQ(second.consumed, 1u);
    LT_CHECK(d.message_complete());
    const detail::body_pull_result tail = d.pull(sink);
    LT_CHECK(tail.kind == detail::body_pull::data);
    LT_CHECK(of(std::span(sink.data(), tail.copied)) == "o");
LT_END_AUTO_TEST(staging_budget_applies_backpressure)

LT_BEGIN_AUTO_TEST(limits_suite, decode_with_full_queue_consumes_nothing)
    http1_body_budget tight = default_budget();
    tight.max_staged_bytes = 2;
    http1_body_decoder d(length_mode(9), tight);
    LT_CHECK_EQ(d.decode("ab").consumed, 2u);
    // The queue is full: any further wire cannot be staged and stays
    // with the caller (progressed, zero consumed — pull first).
    const http1_body_progress blocked = d.decode("cdefg");
    LT_CHECK(blocked.kind == http1_body_decode::progressed);
    LT_CHECK_EQ(blocked.consumed, 0u);
    LT_CHECK_EQ(d.staged_bytes(), 2u);
LT_END_AUTO_TEST(decode_with_full_queue_consumes_nothing)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
