/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <filesystem>  // NOLINT(build/c++17): C++20 tests.
#include <fstream>
#include <memory>
#include <vector>
#include <string>
#include "test/support/quic_interop_files.hpp"
#include "./littletest.hpp"
namespace {
namespace hd = httpserver::detail;
struct fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() / "task163-files";
    httpserver::server::resource_budget budget = httpserver::server::resource_budget::root({});
    hd::quic_transport_parameters local, peer;
    std::unique_ptr<hd::quic_flow_control> flow;
    std::unique_ptr<hqnet::files> files;
    fixture() {
        std::filesystem::remove_all(root);
        std::filesystem::create_directory(root);
        std::ofstream(root / "file", std::ios::binary).write("binary\0\xff", 8);
        local.initial_max_streams_bidi = 16;
        local.initial_max_data = 65536;
        local.initial_max_stream_data_bidi_remote = 8192;
        peer.initial_max_data = 65536;
        peer.initial_max_stream_data_bidi_local = 65536;
        flow = std::make_unique<hd::quic_flow_control>(hd::quic_endpoint_role::server, local, peer, 16, budget);
        files = std::make_unique<hqnet::files>(root.string());
    }
    ~fixture() { files.reset(); std::filesystem::remove_all(root); }
    std::unique_ptr<hd::quic_stream_state> stream(std::uint64_t id) {
        if (!flow->observe_peer(id)) throw std::runtime_error("admit stream");
        return std::make_unique<hd::quic_stream_state>(id, hd::quic_endpoint_role::server, flow->ids(), hd::quic_stream_limits{}, budget);
    }
    void receive(hd::quic_stream_state& stream, std::uint64_t offset, const std::string& input, bool fin = false) {
        if (!flow->receive(stream, {stream.id(), offset, std::as_bytes(std::span(input)), fin})) throw std::runtime_error("receive stream");
        files->receive(stream, *flow);
    }
};
}  // namespace
LT_BEGIN_SUITE(interop_files_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(interop_files_suite)
LT_BEGIN_AUTO_TEST(interop_files_suite, fragmented_request_opens_a_bounded_file)
    fixture f;
    auto s = f.stream(0);
    f.receive(*s, 0, "GET /fi");
    LT_CHECK(f.files->active_files() == 0);
    f.receive(*s, 7, "le\r\n", true);
    LT_CHECK(f.files->active_files() == 1);
    LT_CHECK(s->buffered_bytes() == 0);
    LT_CHECK(s->consumed() == 11);

LT_END_AUTO_TEST(fragmented_request_opens_a_bounded_file)
LT_BEGIN_AUTO_TEST(interop_files_suite, rejects_traversal_symlink_malformed_and_long_requests)
    for (const auto& input : std::vector<std::string>{"GET /../file\r\n", "GET /escape\r\n", "POST /file\r\n", "GET /file\r\nextra", std::string(4097, 'a')}) {
        fixture f;
        std::filesystem::create_symlink("/etc/passwd", f.root / "escape");
        auto s = f.stream(0);
        bool rejected = false;
        try {
            f.receive(*s, 0, input, true);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        LT_CHECK(rejected);
        LT_CHECK(f.files->active_files() == 0);
    }
LT_END_AUTO_TEST(rejects_traversal_symlink_malformed_and_long_requests)
LT_BEGIN_AUTO_TEST(interop_files_suite, exact_binary_fin_ack_retirement_and_small_credit_progress)
    fixture f;
    f.peer.initial_max_data = 4;
    f.peer.initial_max_stream_data_bidi_local = 4;
    f.flow = std::make_unique<hd::quic_flow_control>(hd::quic_endpoint_role::server, f.local, f.peer, 16, f.budget);
    auto s = f.stream(0);
    f.receive(*s, 0, "GET /file\r\n", true);
    hd::quic_recovery recovery({}, f.budget);
    std::array<std::byte, 1200> bytes{};
    const auto now = hd::quic_recovery::time_point{};
    f.files->pump(*f.flow, recovery);
    auto plan = recovery.prepare_packet(hd::quic_pn_space::application, bytes, now, *f.flow);
    LT_ASSERT(plan && plan.stream);
    LT_CHECK(plan.stream->data.size() == 4 && !plan.stream->fin);
    LT_ASSERT(recovery.commit_sent(plan.token, now, 100, true, true));
    LT_ASSERT(recovery.receive_ack(hd::quic_pn_space::application, {plan.packet_number, 0, 0, 0, {}, {}}, now));
    while (auto receipt = recovery.take_completion()) f.files->completed(*receipt);
    f.files->pump(*f.flow, recovery);
    LT_CHECK(!recovery.prepare_packet(hd::quic_pn_space::application, bytes, now, *f.flow));
    LT_ASSERT(f.flow->apply({hd::quic_flow_kind::max_data, 8}));
    LT_ASSERT(f.flow->apply({hd::quic_flow_kind::max_stream_data, 8, 0}));
    f.files->pump(*f.flow, recovery);
    plan = recovery.prepare_packet(hd::quic_pn_space::application, bytes, now, *f.flow);
    LT_ASSERT(plan && plan.stream);
    LT_CHECK(plan.stream->offset == 4 && plan.stream->fin && plan.stream->data.size() == 4);
    const std::string expected("ry\0\xff", 4);
    LT_CHECK(std::equal(plan.stream->data.begin(), plan.stream->data.end(), std::as_bytes(std::span(expected)).begin()));
    LT_ASSERT(recovery.commit_sent(plan.token, now, 100, true, true));
    LT_ASSERT(recovery.receive_ack(hd::quic_pn_space::application, {plan.packet_number, 0, 0, 0, {}, {}}, now));
    while (auto receipt = recovery.take_completion()) f.files->completed(*receipt);
    LT_CHECK(f.files->active_files() == 0);
LT_END_AUTO_TEST(exact_binary_fin_ack_retirement_and_small_credit_progress)
LT_BEGIN_AUTO_TEST(interop_files_suite, blocked_stream_does_not_prevent_sibling_file_and_cancel_releases_ownership)
    fixture f;
    f.peer.initial_max_data = 12;
    f.peer.initial_max_stream_data_bidi_local = 4;
    f.flow = std::make_unique<hd::quic_flow_control>(hd::quic_endpoint_role::server, f.local, f.peer, 16, f.budget);
    auto a = f.stream(0), b = f.stream(4);
    f.receive(*a, 0, "GET /file\r\n", true);
    f.receive(*b, 0, "GET /file\r\n", true);
    hd::quic_recovery recovery({}, f.budget);
    std::array<std::byte, 1200> bytes{};
    const auto now = hd::quic_recovery::time_point{};
    for (unsigned turn = 0; turn < 2; ++turn) {
        f.files->pump(*f.flow, recovery);
        auto plan = recovery.prepare_packet(hd::quic_pn_space::application, bytes, now, *f.flow);
        LT_ASSERT(plan && plan.stream);
        LT_CHECK(plan.stream->data.size() == 4);
        LT_ASSERT(recovery.commit_sent(plan.token, now, 100, true, true));
        LT_ASSERT(recovery.receive_ack(hd::quic_pn_space::application, {plan.packet_number, 0, 0, 0, {}, {}}, now));
        while (auto receipt = recovery.take_completion()) f.files->completed(*receipt);
    }
    LT_ASSERT(f.flow->apply({hd::quic_flow_kind::max_stream_data, 8, 4}));
    f.files->pump(*f.flow, recovery);
    auto plan = recovery.prepare_packet(hd::quic_pn_space::application, bytes, now, *f.flow);
    LT_ASSERT(plan && plan.stream);
    LT_CHECK(plan.stream->stream == 4 && plan.stream->fin);
    LT_ASSERT(recovery.abandon_packet(plan.token));
    f.files->cancel(0, recovery);
    f.files->cancel(4, recovery);
    LT_CHECK(f.files->active_files() == 0);
LT_END_AUTO_TEST(blocked_stream_does_not_prevent_sibling_file_and_cancel_releases_ownership)
LT_BEGIN_AUTO_TEST(interop_files_suite, unknown_completion_cannot_release_a_file_pipeline_slot)
    fixture f;
    auto stream = f.stream(0);
    f.receive(*stream, 0, "GET /file\r\n", true);
    hd::quic_recovery recovery({}, f.budget);
    f.files->pump(*f.flow, recovery);
    for (auto id : {0u, 999u}) {
        hd::quic_information_completion receipt{};
        receipt.id = id;
        receipt.kind = hd::quic_information_kind::stream;
        receipt.stream = 0;
        bool rejected = false;
        try {
            f.files->completed(receipt);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        LT_CHECK(rejected);
    }
    f.files->cancel(0, recovery);
    LT_CHECK(f.files->active_files() == 0);
LT_END_AUTO_TEST(unknown_completion_cannot_release_a_file_pipeline_slot)
LT_BEGIN_AUTO_TEST(interop_files_suite, large_file_pipeline_stays_eight_chunks_until_ack)
    fixture f;
    std::ofstream large(f.root / "file", std::ios::binary | std::ios::trunc);
    const std::string block(8192, 'x');
    for (unsigned i = 0; i < 512; ++i) large.write(block.data(), block.size());
    large.close();
    auto s = f.stream(0);
    f.receive(*s, 0, "GET /file\r\n", true);
    LT_ASSERT(f.flow->apply({hd::quic_flow_kind::max_data, 131072}));
    LT_ASSERT(f.flow->apply({hd::quic_flow_kind::max_stream_data, 131072, 0}));
    hd::quic_recovery_config config;
    config.max_retained_bytes = 262144;
    config.critical_retained_bytes = 65536;
    hd::quic_recovery recovery(config, f.budget);
    for (unsigned i = 0; i < 100; ++i) f.files->pump(*f.flow, recovery);
    std::array<std::byte, 1200> bytes{};
    auto now = hd::quic_recovery::time_point{};
    std::size_t sent = 0;
    std::uint64_t last_packet = 0;
    for (unsigned packet = 0; packet < 128; ++packet) {
        auto plan = recovery.prepare_packet(hd::quic_pn_space::application, bytes, now, *f.flow);
        if (!plan) break;
        LT_ASSERT(plan.stream);
        LT_CHECK(plan.stream->offset == sent && !plan.stream->fin);
        sent += plan.stream->data.size();
        last_packet = plan.packet_number;
        LT_ASSERT(recovery.commit_sent(plan.token, now, plan.bytes + 40, true, true));
        now += std::chrono::seconds(1);
        f.files->pump(*f.flow, recovery);
    }
    // Eight separately owned 8 KiB chunks may be pipelined, but no ninth may
    // become available before an information receipt frees a bounded slot.
    LT_CHECK(sent == 65536);
    LT_CHECK(!recovery.prepare_packet(hd::quic_pn_space::application, bytes, now, *f.flow));
    LT_ASSERT(recovery.receive_ack(hd::quic_pn_space::application, {last_packet, 0, last_packet, 0, {}, {}}, now));
    std::vector<hd::quic_information_completion> receipts;
    while (auto receipt = recovery.take_completion()) receipts.push_back(*receipt);
    LT_CHECK(receipts.size() == 8);
    for (auto receipt = receipts.rbegin(); receipt != receipts.rend(); ++receipt) f.files->completed(*receipt);
    f.files->pump(*f.flow, recovery);
    auto next = recovery.prepare_packet(hd::quic_pn_space::application, bytes, now, *f.flow);
    LT_ASSERT(next && next.stream);
    LT_CHECK(next.stream->offset == 65536);
    LT_ASSERT(recovery.abandon_packet(next.token));
    f.files->cancel(0, recovery);
    LT_CHECK(f.files->active_files() == 0);
LT_END_AUTO_TEST(large_file_pipeline_stays_eight_chunks_until_ack)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
