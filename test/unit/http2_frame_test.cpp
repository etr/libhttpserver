/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <cstdlib>
#include <new>
#include <vector>
#include <httpserver/detail/http2_frame.hpp>
#include "./http2_fixture.hpp"
#include "./littletest.hpp"
namespace allocation_observer {
bool enabled = false;
std::size_t maximum = 0;
}
void* operator new(std::size_t n) {
    if (allocation_observer::enabled) allocation_observer::maximum = std::max(allocation_observer::maximum, n);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
using namespace h2test;  // NOLINT(build/namespaces)
LT_BEGIN_SUITE(http2_frame_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_frame_suite)
LT_BEGIN_AUTO_TEST(http2_frame_suite, preface_every_split_and_single_event_boundary)
    auto wire = preface();
    append(wire, frame(6, 0, 0, {0, 1, 2, 3, 4, 5, 6, 255}));
    for (std::size_t split = 0; split <= 33; ++split) {
        hd::http2_frame_parser parser(budget());
        auto first = parser.feed(std::span(wire).first(split));
        LT_CHECK_EQ(first.consumed, split);
        if (split < 33) LT_CHECK(first.progress == hd::http2_progress::input);
        auto second = split == 33 ? first : parser.feed(std::span(wire).subspan(split));
        LT_ASSERT(second.progress == hd::http2_progress::frame_ready);
        LT_CHECK_EQ(second.consumed, split == 33 ? 33U : 33U - split);
        LT_CHECK_EQ(parser.header().type, 4);
        LT_CHECK_EQ(parser.feed(std::span(wire).subspan(33)).consumed, 0U);
        parser.release_frame();
        auto ping = parser.feed(std::span(wire).subspan(33));
        LT_CHECK_EQ(ping.consumed, 17U);
        LT_CHECK_EQ(parser.control_payload()[7], 255);
    }
LT_END_AUTO_TEST(preface_every_split_and_single_event_boundary)
LT_BEGIN_AUTO_TEST(http2_frame_suite, bad_magic_and_truncation_are_sticky)
    auto wire = preface();
    for (std::size_t i = 0; i < 24; ++i) {
        auto bad = wire;
        bad[i] ^= 1;
        hd::http2_frame_parser parser(budget());
        auto result = parser.feed(bad);
        LT_ASSERT(result.error.has_value());
        LT_CHECK(result.error->wire_code == hd::http2_error_code::protocol_error);
        LT_CHECK_EQ(result.consumed, i + 1);
        LT_CHECK_EQ(parser.feed(wire).consumed, 0U);
    }
    append(wire, frame(6, 0, 0, {1, 2, 3, 4, 5, 6, 7, 8}));
    for (std::size_t n = 0; n < wire.size(); ++n) {
        hd::http2_frame_parser parser(budget());
        auto result = parser.feed(std::span(wire).first(n));
        if (result.progress == hd::http2_progress::frame_ready) {
            parser.release_frame();
            parser.feed(std::span(wire).subspan(result.consumed, n - result.consumed));
        }
        if (n == 33) {
            LT_CHECK(!parser.eof().error);
        } else {
            LT_CHECK(parser.eof().error.has_value());
        }
    }
LT_END_AUTO_TEST(bad_magic_and_truncation_are_sticky)
LT_BEGIN_AUTO_TEST(http2_frame_suite, caps_unknown_skip_and_reserved_bits)
    hd::http2_frame_parser parser(budget());
    parser.feed(preface());
    parser.release_frame();
    auto unknown = frame(99, 255, 0xffffffff, std::vector<std::uint8_t>(16384, 3));
    for (auto byte : unknown) LT_CHECK_EQ(parser.feed({&byte, 1}).consumed, 1U);
    LT_CHECK_EQ(parser.header().stream_id, 0x7fffffffU);
    LT_CHECK(parser.payload().empty());
    parser.release_frame();
    auto oversized = frame(0, 0, 1, std::vector<std::uint8_t>(16385));
    auto result = parser.feed(oversized);
    LT_ASSERT(result.error.has_value());
    LT_CHECK_EQ(result.consumed, 9U);
    LT_CHECK(result.error->wire_code == hd::http2_error_code::frame_size_error);
LT_END_AUTO_TEST(caps_unknown_skip_and_reserved_bits)
LT_BEGIN_AUTO_TEST(http2_frame_suite, oversized_header_and_unknown_payload_do_not_allocate_declared_bytes)
    hd::http2_frame_parser p(budget());
    p.feed(preface());
    p.release_frame();
    auto unknown = frame(200, 255, 0, std::vector<std::uint8_t>(16384));
    allocation_observer::enabled = true;
    allocation_observer::maximum = 0;
    auto result = p.feed(unknown);
    allocation_observer::enabled = false;
    LT_CHECK(!result.error);
    LT_CHECK_EQ(allocation_observer::maximum, 0U);
    p.release_frame();
    const std::array<std::uint8_t, 9> oversized{255, 255, 255, 1, 4, 0, 0, 0, 1};
    allocation_observer::enabled = true;
    allocation_observer::maximum = 0;
    result = p.feed(oversized);
    allocation_observer::enabled = false;
    LT_ASSERT(result.error.has_value());
    LT_CHECK_EQ(result.consumed, 9U);
    LT_CHECK_EQ(allocation_observer::maximum, 0U);
LT_END_AUTO_TEST(oversized_header_and_unknown_payload_do_not_allocate_declared_bytes)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
