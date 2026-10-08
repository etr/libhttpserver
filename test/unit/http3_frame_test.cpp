/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <cstdlib>
#include <new>
#include <httpserver/detail/http3_frame.hpp>
#include "./http3_fixture.hpp"
#include "./littletest.hpp"
namespace {
bool observe_allocations = false;
std::size_t largest_allocation = 0;
}
void* operator new(std::size_t size) {
    if (observe_allocations) largest_allocation = std::max(largest_allocation, size);
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
using namespace h3test;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(http3_frame_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(http3_frame_suite)
LT_BEGIN_AUTO_TEST(http3_frame_suite, all_header_widths_and_splits)
    for (auto width : {1U, 2U, 4U, 8U}) {
        auto wire = frame(1, bytes({0, 0, 0xd1}), width);
        for (std::size_t split = 0; split <= wire.size(); ++split) {
            hd::quic_storage_pool pool(200000, 10000, budget());
            hd::http3_frame_parser p(pool.data(), pool.critical());
            std::size_t used = 0;
            for (auto part : {std::span(wire).first(split), std::span(wire).subspan(split)}) {
                while (!part.empty()) {
                    auto r = p.feed(part, used); used += r.consumed; part = part.subspan(r.consumed);
                    LT_ASSERT(!r.error);
                    if (r.progress == hd::http3_progress::header) p.accept_header();
                    else if (r.progress == hd::http3_progress::event) break;
                }
            }
            LT_CHECK_EQ(used, wire.size());
            LT_ASSERT(p.event());
            LT_CHECK_EQ(p.event()->type, 1U);
            LT_CHECK_EQ(p.event()->payload.size(), 3U);
            LT_CHECK_EQ(p.event()->payload_begin, 2U * width);
            LT_CHECK_EQ(p.feed(wire, used).consumed, 0U);
            p.release_event(); LT_CHECK(!p.finish());
        }
    }
LT_END_AUTO_TEST(all_header_widths_and_splits)
LT_BEGIN_AUTO_TEST(http3_frame_suite, data_chunks_unknown_skip_and_zero)
    hd::quic_storage_pool pool(200000, 10000, budget());
    hd::http3_limits limits; limits.data_chunk = 3;
    hd::http3_frame_parser p(pool.data(), pool.critical(), limits);
    auto wire = frame(0, bytes({1, 2, 3, 4, 5}));
    auto r = p.feed(wire, 0); LT_CHECK_EQ(r.consumed, 2U); p.accept_header();
    r = p.feed(std::span(wire).subspan(2), 2); LT_CHECK_EQ(r.consumed, 3U);
    LT_ASSERT(p.event()); LT_CHECK(!p.event()->last); p.release_event();
    r = p.feed(std::span(wire).subspan(5), 5); LT_CHECK_EQ(r.consumed, 2U);
    LT_CHECK(p.event()->last); p.release_event();
    wire = frame(33, bytes({1, 2, 3}));
    p.feed(wire, 7); p.accept_header(); r = p.feed(std::span(wire).subspan(2), 9);
    LT_CHECK_EQ(r.consumed, 3U); LT_CHECK(p.event()->payload.empty()); p.release_event();
    wire = bytes({0, 0}); p.feed(wire, 12); p.accept_header(); p.feed({}, 14);
    LT_ASSERT(p.event()); LT_CHECK(p.event()->last); p.release_event(); LT_CHECK(!p.finish());
LT_END_AUTO_TEST(data_chunks_unknown_skip_and_zero)
LT_BEGIN_AUTO_TEST(http3_frame_suite, bounds_truncation_and_sticky_error)
    hd::quic_storage_pool pool(200000, 10000, budget());
    hd::http3_limits limits; limits.headers.max_compressed_bytes = 3;
    hd::http3_frame_parser p(pool.data(), pool.critical(), limits);
    auto r = p.feed(bytes({1, 4}), 0); LT_CHECK_EQ(r.consumed, 2U);
    auto error = p.accept_header(); LT_ASSERT(error); LT_CHECK_EQ(error->wire_code, 0x107U);
    LT_CHECK_EQ(p.feed(bytes({0}), 2).consumed, 0U);
    hd::http3_frame_parser truncated(pool.data(), pool.critical());
    truncated.feed(bytes({0x40}), 0); error = truncated.finish(); LT_ASSERT(error);
    LT_CHECK_EQ(error->wire_code, 0x106U);
    hd::http3_frame_parser payload(pool.data(), pool.critical());
    payload.feed(bytes({1, 3}), 0); payload.accept_header(); payload.feed(bytes({0}), 2);
    LT_CHECK_EQ(payload.finish()->wire_code, 0x106U);
LT_END_AUTO_TEST(bounds_truncation_and_sticky_error)
LT_BEGIN_AUTO_TEST(http3_frame_suite, invalid_transport_offsets_fail_without_consumption)
    hd::quic_storage_pool pool(200000, 10000, budget());
    hd::http3_frame_parser p(pool.data(), pool.critical());
    auto r = p.feed(bytes({1}), 1); LT_ASSERT(r.error); LT_CHECK_EQ(r.consumed, 0U);
LT_END_AUTO_TEST(invalid_transport_offsets_fail_without_consumption)
LT_BEGIN_AUTO_TEST(http3_frame_suite, scalar_controls_use_fixed_scratch_and_oversize_rejects_before_growth)
    hd::quic_storage_pool pool(200000, 10000, budget());
    hd::http3_frame_parser scalar(pool.data(), pool.critical());
    auto wire = bytes({7, 1, 0}); scalar.feed(wire, 0);
    largest_allocation = 0; observe_allocations = true;
    scalar.accept_header(); scalar.feed(std::span(wire).subspan(2), 2);
    observe_allocations = false;
    LT_CHECK_EQ(largest_allocation, 0U); LT_ASSERT(scalar.event());
    LT_CHECK_EQ(scalar.event()->payload.size(), 1U);
    hd::http3_frame_parser oversized(pool.data(), pool.critical());
    wire = bytes({1}); integer(wire, hd::k_quic_max_integer); oversized.feed(wire, 0);
    largest_allocation = 0; observe_allocations = true; auto error = oversized.accept_header(); observe_allocations = false;
    LT_ASSERT(error); LT_CHECK_EQ(error->wire_code, 0x107U); LT_CHECK_EQ(largest_allocation, 0U);
    hd::http3_frame_parser unknown(pool.data(), pool.critical());
    wire[0] = std::byte{33}; unknown.feed(wire, 0);
    largest_allocation = 0; observe_allocations = true; unknown.accept_header(); observe_allocations = false;
    LT_CHECK_EQ(largest_allocation, 0U); LT_CHECK_EQ(unknown.length(), hd::k_quic_max_integer);
LT_END_AUTO_TEST(scalar_controls_use_fixed_scratch_and_oversize_rejects_before_growth)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
