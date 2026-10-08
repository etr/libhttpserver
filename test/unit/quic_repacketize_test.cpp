/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include <chrono>
#include <cstdlib>
#include <new>
#include <vector>
#include <httpserver/detail/quic_recovery.hpp>
#include "../littletest.hpp"
namespace {
int allocation_countdown = -1;
}
void* operator new(std::size_t size) {
    if (allocation_countdown == 0) {
        allocation_countdown = -1;
        throw std::bad_alloc();
    }
    if (allocation_countdown > 0) --allocation_countdown;
    if (auto* pointer = std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
namespace hd = httpserver::detail;
using space = hd::quic_pn_space;
using time_point = hd::quic_recovery::time_point;
using namespace std::chrono_literals;  // NOLINT(build/namespaces)
namespace {
auto budget() { return httpserver::server::resource_budget::root({}); }
constexpr std::array data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
hd::quic_ack_frame ack(std::uint64_t pn) { return {pn, 0, 0, 0, {}, {}}; }
void ping(hd::quic_recovery& r, space s) {
    auto p = r.reserve_packet(s);
    if (!p || !r.commit_sent(p.token, {}, 100, true, true)) throw std::runtime_error("Ping refused");
}
hd::quic_frame frame(std::span<const std::byte> bytes, space s = space::application) {
    hd::quic_frame_cursor cursor;
    hd::quic_frame_context context;
    context.packet = s == space::initial ? hd::quic_packet_kind::initial : s == space::handshake ? hd::quic_packet_kind::handshake : hd::quic_packet_kind::one_rtt;
    auto f = hd::next_quic_frame(bytes, cursor, context);
    if (f.code != hd::quic_codec_code::ok) throw std::runtime_error("Invalid prepared frame");
    return f.value;
}
}  // namespace
LT_BEGIN_SUITE(repacketize_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(repacketize_suite)
LT_BEGIN_AUTO_TEST(repacketize_suite, reliable_controls_survive_unsent_and_lost_packets)
    for (bool stop : {false, true}) {
        hd::quic_recovery r({}, budget());
        auto information = stop ? r.retain_stop_sending({0, 0x10c}) : r.retain_handshake_done();
        LT_ASSERT(information);
        std::array<std::byte, 100> out{};
        auto unsent = r.prepare_packet(space::application, out, {});
        LT_ASSERT(unsent);
        LT_ASSERT(r.abandon_packet(unsent.token));
        auto original = r.prepare_packet(space::application, out, {});
        LT_ASSERT(original);
        LT_ASSERT(r.commit_sent(original.token, {}, 100, true, true));
        for (unsigned i = 0; i < 3; ++i) ping(r, space::application);
        LT_ASSERT(r.receive_ack(space::application, ack(original.packet_number + 3), {}));
        auto replacement = r.prepare_packet(space::application, out, {});
        LT_ASSERT(replacement);
        LT_CHECK(replacement.packet_number > original.packet_number);
        auto parsed = frame(std::span(out).first(replacement.bytes));
        if (stop) {
            const auto control = std::get<hd::quic_stop_sending_frame>(parsed);
            LT_CHECK(control.stream == 0 && control.error == 0x10c);
        } else {
            LT_CHECK(std::holds_alternative<hd::quic_handshake_done_frame>(parsed));
        }
        LT_ASSERT(r.commit_sent(replacement.token, {}, 100, true, true));
        LT_ASSERT(r.receive_ack(space::application, ack(replacement.packet_number), {}));
        auto completion = r.take_completion();
        LT_ASSERT(completion);
        LT_CHECK(completion->id == information.id);
        LT_CHECK(!r.take_completion());
    }
LT_END_AUTO_TEST(reliable_controls_survive_unsent_and_lost_packets)
LT_BEGIN_AUTO_TEST(repacketize_suite, loss_replacement_and_original_ack_share_delivery)
    hd::quic_recovery r({}, budget());
    auto retained = r.retain_crypto(space::initial, 0, data);
    LT_ASSERT(retained);
    std::array<std::byte, 100> out{};
    auto original = r.prepare_packet(space::initial, out, {});
    LT_ASSERT(original);
    LT_ASSERT(r.commit_sent(original.token, {}, 100, true, true));
    for (unsigned i = 0; i < 3; ++i) ping(r, space::initial);
    LT_CHECK(r.receive_ack(space::initial, ack(3), {}).lost_bytes == 100);
    auto replacement = r.prepare_packet(space::initial, out, {});
    LT_ASSERT(replacement);
    LT_CHECK(replacement.packet_number > original.packet_number);
    auto f = std::get<hd::quic_crypto_frame>(frame(std::span(out).first(replacement.bytes), space::initial));
    LT_CHECK(f.offset == 0 && std::equal(f.data.begin(), f.data.end(), data.begin(), data.end()));
    LT_ASSERT(r.receive_ack(space::initial, ack(original.packet_number), {}));
    auto completed = r.take_completion();
    LT_ASSERT(completed);
    LT_CHECK(completed->id == retained.id && completed->through_offset == data.size());
    LT_ASSERT(r.commit_sent(replacement.token, {}, 100, true, true));
    for (unsigned i = 0; i < 3; ++i) ping(r, space::initial);
    LT_ASSERT(r.receive_ack(space::initial, ack(7), {}));
    LT_CHECK(!r.take_completion());
    LT_CHECK(r.prepare_packet(space::initial, out, {}).code == hd::quic_recovery_code::no_data);
LT_END_AUTO_TEST(loss_replacement_and_original_ack_share_delivery)
LT_BEGIN_AUTO_TEST(repacketize_suite, splitting_preserves_offset_and_single_fin_completion)
    hd::quic_recovery r({}, budget());
    auto retained = r.retain_stream({1, 9, data, true, true, true});
    LT_ASSERT(retained);
    std::array<std::byte, 7> out{};
    auto first = r.prepare_packet(space::application, out, {});
    LT_ASSERT(first);
    auto head = std::get<hd::quic_stream_frame>(frame(std::span(out).first(first.bytes)));
    const auto split = head.data.size();
    LT_CHECK(split > 0 && split < data.size() && !head.fin && head.offset == 9);
    LT_ASSERT(r.commit_sent(first.token, {}, 100, true, true));
    std::array<std::byte, 100> tail_buffer{};
    auto second = r.prepare_packet(space::application, tail_buffer, {});
    LT_ASSERT(second);
    auto tail = std::get<hd::quic_stream_frame>(frame(std::span(tail_buffer).first(second.bytes)));
    LT_CHECK(tail.offset == 9 + split && tail.fin && tail.data.size() == data.size() - split);
    LT_ASSERT(r.commit_sent(second.token, {}, 100, true, true));
    LT_ASSERT(r.receive_ack(space::application, ack(second.packet_number), {}));
    LT_CHECK(r.delivered_prefix(retained.id) == 9 && !r.take_completion());
    LT_ASSERT(r.receive_ack(space::application, ack(first.packet_number), {}));
    LT_CHECK(r.delivered_prefix(retained.id) == 17);
    auto complete = r.take_completion();
    LT_ASSERT(complete);
    LT_CHECK(complete->fin && complete->stream == 1 && !r.take_completion());
LT_END_AUTO_TEST(splitting_preserves_offset_and_single_fin_completion)
LT_BEGIN_AUTO_TEST(repacketize_suite, failed_output_send_and_probe_leave_information_recoverable)
    hd::quic_recovery r({}, budget());
    LT_ASSERT(r.retain_crypto(space::initial, 0, data));
    LT_CHECK(r.prepare_packet(space::initial, {}, {}).code == hd::quic_recovery_code::no_space);
    std::array<std::byte, 100> out{};
    auto unsent = r.prepare_packet(space::initial, out, {});
    LT_ASSERT(unsent);
    LT_CHECK(r.bytes_in_flight() == 0);
    LT_ASSERT(r.abandon_packet(unsent.token));
    auto original = r.prepare_packet(space::initial, out, {});
    LT_ASSERT(original);
    LT_CHECK(original.packet_number > unsent.packet_number);
    LT_ASSERT(r.commit_sent(original.token, {}, 100, true, true));
    LT_CHECK(r.prepare_packet(space::initial, out, {}).code == hd::quic_recovery_code::no_data);
    auto probe = r.prepare_packet(space::initial, out, {}, true);
    LT_ASSERT(probe);
    LT_CHECK(std::get<hd::quic_crypto_frame>(frame(std::span(out).first(probe.bytes), space::initial)).data.size() == data.size());
    LT_ASSERT(r.commit_sent(probe.token, {}, 100, true, true));
    LT_ASSERT(r.receive_ack(space::initial, ack(probe.packet_number), {}));
    LT_CHECK(r.take_completion().has_value());
    LT_ASSERT(r.receive_ack(space::initial, ack(original.packet_number), {}));
    LT_CHECK(!r.take_completion());
LT_END_AUTO_TEST(failed_output_send_and_probe_leave_information_recoverable)
LT_BEGIN_AUTO_TEST(repacketize_suite, reset_and_empty_fin_are_value_information)
    hd::quic_recovery r({}, budget());
    auto reset = r.retain_reset({1, 42, 17});
    LT_ASSERT(reset);
    std::array<std::byte, 100> out{};
    auto p = r.prepare_packet(space::application, out, {});
    LT_ASSERT(p);
    auto f = std::get<hd::quic_reset_stream_frame>(frame(std::span(out).first(p.bytes)));
    LT_CHECK(f.error == 42 && f.final_size == 17);
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), {}));
    LT_CHECK(r.take_completion()->kind == hd::quic_information_kind::reset_stream);
    auto fin = r.retain_stream({1, 0, {}, true});
    LT_ASSERT(fin);
    p = r.prepare_packet(space::application, out, {});
    LT_ASSERT(p);
    LT_CHECK(std::get<hd::quic_stream_frame>(frame(std::span(out).first(p.bytes))).fin);
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    LT_ASSERT(r.receive_ack(space::application, ack(p.packet_number), {}));
    LT_CHECK(r.take_completion()->fin);
LT_END_AUTO_TEST(reset_and_empty_fin_are_value_information)
LT_BEGIN_AUTO_TEST(repacketize_suite, bounded_admission_cancellation_and_space_discard)
    auto b = budget();
    hd::quic_recovery_config config;
    config.max_retained_bytes = 8;
    config.max_information = 1;
    {
        hd::quic_recovery r(config, b);
        auto kept = r.retain_crypto(space::initial, 0, data);
        LT_ASSERT(kept);
        auto charged = b.in_use(httpserver::server::resource::quic_reassembly_bytes);
        LT_CHECK(r.retain_crypto(space::initial, 8, data).code == hd::quic_recovery_code::capacity);
        LT_CHECK(b.in_use(httpserver::server::resource::quic_reassembly_bytes) == charged);
        LT_ASSERT(r.cancel_information(kept.id));
        LT_CHECK(!r.take_completion());
        LT_CHECK(r.retain_crypto(space::initial, hd::k_quic_max_integer, data).code == hd::quic_recovery_code::invalid);
        LT_ASSERT(r.retain_crypto(space::initial, 0, data));
        LT_ASSERT(r.discard_space(space::initial));
        LT_CHECK(b.in_use(httpserver::server::resource::quic_reassembly_bytes) == charged - 2 * data.size());
        LT_CHECK(r.retain_crypto(space::initial, 0, data).code == hd::quic_recovery_code::discarded);
    }
    LT_CHECK(b.in_use(httpserver::server::resource::quic_reassembly_bytes) == 0);
LT_END_AUTO_TEST(bounded_admission_cancellation_and_space_discard)
LT_BEGIN_AUTO_TEST(repacketize_suite, ack_publication_commits_only_on_emission_and_acked_watermark_retires_history)
    hd::quic_recovery r({}, budget());
    LT_ASSERT(r.receive_packet(space::initial, 0, true, {}));
    std::array<std::byte, 100> out{};
    auto p = r.prepare_packet(space::initial, out, {});
    LT_ASSERT(p);
    LT_CHECK(!p.ack_eliciting && r.ack_deadline(space::initial).has_value());
    LT_ASSERT(r.abandon_packet(p.token));
    LT_CHECK(r.ack_deadline(space::initial).has_value());
    p = r.prepare_packet(space::initial, out, {});
    LT_ASSERT(p);
    LT_ASSERT(r.commit_sent(p.token, {}, 100, false, false));
    LT_CHECK(!r.ack_deadline(space::initial) && r.bytes_in_flight() == 0);
    LT_ASSERT(r.receive_packet(space::initial, 1, true, {}));
    auto acknowledged = r.receive_ack(space::initial, ack(p.packet_number), {});
    LT_ASSERT(acknowledged);
    LT_CHECK(acknowledged.acknowledged_bytes == 0 && !r.rtt().sampled);
    LT_CHECK(r.inspect_received(space::initial, 0) == hd::quic_receipt::retired);
    LT_CHECK(r.inspect_received(space::initial, 1) == hd::quic_receipt::duplicate && r.ack_deadline(space::initial).has_value());
LT_END_AUTO_TEST(ack_publication_commits_only_on_emission_and_acked_watermark_retires_history)
LT_BEGIN_AUTO_TEST(repacketize_suite, partial_probe_delivery_does_not_resurrect_delivered_bytes)
    hd::quic_recovery r({}, budget());
    auto id = r.retain_crypto(space::initial, 0, data);
    LT_ASSERT(id);
    std::array<std::byte, 100> out{};
    auto original = r.prepare_packet(space::initial, out, {});
    LT_ASSERT(original);
    LT_ASSERT(r.commit_sent(original.token, {}, 100, true, true));
    std::array<std::byte, 5> partial{};
    auto probe = r.prepare_packet(space::initial, partial, {}, true);
    LT_ASSERT(probe);
    const auto delivered = std::get<hd::quic_crypto_frame>(frame(std::span(partial).first(probe.bytes), space::initial)).data.size();
    LT_ASSERT(r.commit_sent(probe.token, {}, 100, true, true));
    LT_ASSERT(r.receive_ack(space::initial, ack(probe.packet_number), {}));
    LT_CHECK(r.delivered_prefix(id.id) == delivered && !r.take_completion());
    for (unsigned i = 0; i < 3; ++i) ping(r, space::initial);
    LT_ASSERT(r.receive_ack(space::initial, ack(4), {}));
    auto remainder = r.prepare_packet(space::initial, out, {});
    LT_ASSERT(remainder);
    auto f = std::get<hd::quic_crypto_frame>(frame(std::span(out).first(remainder.bytes), space::initial));
    LT_CHECK(f.offset == delivered && f.data.size() == data.size() - delivered);
    LT_ASSERT(r.abandon_packet(remainder.token));
    LT_ASSERT(r.cancel_information(id.id));
    LT_CHECK(r.prepare_packet(space::initial, out, {}).code == hd::quic_recovery_code::no_data);
LT_END_AUTO_TEST(partial_probe_delivery_does_not_resurrect_delivered_bytes)
LT_BEGIN_AUTO_TEST(repacketize_suite, payload_budget_refusal_rolls_back_and_live_aliases_are_never_evicted)
    auto b = budget();
    std::size_t metadata = 0;
    {
        hd::quic_recovery measure({}, b);
        metadata = b.in_use(httpserver::server::resource::quic_reassembly_bytes);
    }
    httpserver::server::budget_limits limits;
    limits.set(httpserver::server::resource::quic_reassembly_bytes, metadata + 2 * data.size() - 1);
    auto bounded = httpserver::server::resource_budget::root(limits);
    hd::quic_recovery denied({}, bounded);
    LT_CHECK(denied.retain_crypto(space::initial, 0, data).code == hd::quic_recovery_code::no_memory);
    LT_CHECK(bounded.in_use(httpserver::server::resource::quic_reassembly_bytes) == metadata);
    LT_ASSERT(denied.retain_reset({1, 0, 0}));
    hd::quic_recovery_config config;
    config.max_sent_packets = 4;
    hd::quic_recovery r(config, b);
    auto id = r.retain_crypto(space::initial, 0, data);
    LT_ASSERT(id);
    std::array<std::byte, 100> out{};
    auto original = r.prepare_packet(space::initial, out, {});
    LT_ASSERT(original);
    LT_ASSERT(r.commit_sent(original.token, {}, 100, true, true));
    for (unsigned n = 0; n < 3; ++n) ping(r, space::initial);
    LT_ASSERT(r.receive_ack(space::initial, ack(3), {}));
    LT_CHECK(r.prepare_packet(space::initial, out, {}).code == hd::quic_recovery_code::capacity);
    LT_ASSERT(r.receive_ack(space::initial, ack(original.packet_number), {}));
    LT_CHECK(r.take_completion()->id == id.id);
    LT_ASSERT(r.reserve_packet(space::initial));
LT_END_AUTO_TEST(payload_budget_refusal_rolls_back_and_live_aliases_are_never_evicted)
LT_BEGIN_AUTO_TEST(repacketize_suite, crypto_replacements_in_all_spaces_keep_separate_packet_identity)
    hd::quic_recovery r({}, budget());
    std::array<std::byte, 100> out{};
    for (auto s : {space::initial, space::handshake, space::application}) {
        auto retained = r.retain_crypto(s, 7, data);
        LT_ASSERT(retained);
        auto original = r.prepare_packet(s, out, {});
        LT_ASSERT(original);
        LT_CHECK(original.packet_number == 0);
        LT_ASSERT(r.commit_sent(original.token, {}, 100, true, true));
        for (unsigned n = 0; n < 3; ++n) ping(r, s);
        LT_CHECK(r.receive_ack(s, ack(3), {}).lost_bytes == 100);
        auto replacement = r.prepare_packet(s, out, {});
        LT_ASSERT(replacement);
        auto f = std::get<hd::quic_crypto_frame>(frame(std::span(out).first(replacement.bytes), s));
        LT_CHECK(f.offset == 7 && f.data.size() == data.size() && replacement.packet_number == 4);
        LT_ASSERT(r.commit_sent(replacement.token, {}, 100, true, true));
        LT_ASSERT(r.receive_ack(s, ack(replacement.packet_number), {}));
        auto complete = r.take_completion();
        LT_ASSERT(complete);
        LT_CHECK(complete->space == s && complete->through_offset == 15);
    }
LT_END_AUTO_TEST(crypto_replacements_in_all_spaces_keep_separate_packet_identity)
LT_BEGIN_AUTO_TEST(repacketize_suite, allocation_refusals_release_metadata_and_payload_charges)
    auto b = budget();
    allocation_countdown = 2;
    hd::quic_recovery tables({}, b);
    LT_CHECK(tables.reserve_packet(space::initial).code == hd::quic_recovery_code::no_memory);
    LT_CHECK(b.in_use(httpserver::server::resource::quic_reassembly_bytes) == 0);
    hd::quic_recovery r({}, b);
    const auto metadata = b.in_use(httpserver::server::resource::quic_reassembly_bytes);
    allocation_countdown = 0;
    LT_CHECK(r.retain_crypto(space::initial, 0, data).code == hd::quic_recovery_code::no_memory);
    LT_CHECK(b.in_use(httpserver::server::resource::quic_reassembly_bytes) == metadata);
    LT_ASSERT(r.retain_crypto(space::initial, 0, data));
    httpserver::server::budget_limits limits;
    limits.set(httpserver::server::resource::quic_reassembly_bytes, 1);
    auto refused = httpserver::server::resource_budget::root(limits);
    allocation_countdown = 1;
    hd::quic_recovery error_message({}, refused);
    LT_CHECK(error_message.reserve_packet(space::initial).code == hd::quic_recovery_code::no_memory);
    LT_CHECK(refused.in_use(httpserver::server::resource::quic_reassembly_bytes) == 0);
LT_END_AUTO_TEST(allocation_refusals_release_metadata_and_payload_charges)
LT_BEGIN_AUTO_TEST(repacketize_suite, ack_of_ack_preserves_new_reordered_receipts_not_in_its_ranges)
    hd::quic_recovery r({}, budget());
    LT_ASSERT(r.receive_packet(space::initial, 0, true, {}));
    LT_ASSERT(r.receive_packet(space::initial, 3, true, {}));
    std::array<std::byte, 100> out{};
    auto stale = r.prepare_packet(space::initial, out, {});
    LT_ASSERT(stale);
    LT_ASSERT(r.receive_packet(space::initial, 2, true, {}));
    LT_ASSERT(r.commit_sent(stale.token, {}, 100, false, false));
    LT_ASSERT(r.receive_ack(space::initial, ack(stale.packet_number), {}));
    LT_CHECK(r.inspect_received(space::initial, 2) == hd::quic_receipt::duplicate);
    LT_CHECK(r.ack_deadline(space::initial).has_value());
    auto current = r.prepare_packet(space::initial, out, {});
    LT_ASSERT(current);
    LT_ASSERT(r.commit_sent(current.token, {}, 100, false, false));
    LT_ASSERT(r.receive_ack(space::initial, ack(current.packet_number), {}));
    LT_CHECK(r.inspect_received(space::initial, 2) == hd::quic_receipt::retired);
LT_END_AUTO_TEST(ack_of_ack_preserves_new_reordered_receipts_not_in_its_ranges)
LT_BEGIN_AUTO_TEST(repacketize_suite, retained_segment_larger_than_codec_packet_limit_still_splits)
    hd::quic_recovery r({}, budget());
    std::vector<std::byte> large(65536, std::byte{42});
    auto retained = r.retain_crypto(space::initial, 100, large);
    LT_ASSERT(retained);
    large.assign(large.size(), std::byte{99});
    std::array<std::byte, 16> out{};
    auto p = r.prepare_packet(space::initial, out, {});
    LT_ASSERT(p);
    auto f = std::get<hd::quic_crypto_frame>(frame(std::span(out).first(p.bytes), space::initial));
    LT_CHECK(f.offset == 100 && !f.data.empty() && f.data.size() < 16);
    LT_CHECK(std::all_of(f.data.begin(), f.data.end(), [](auto b) { return b == std::byte{42}; }));
    LT_CHECK(r.delivered_prefix(retained.id) == 100);
    LT_ASSERT(r.commit_sent(p.token, {}, 100, true, true));
    LT_ASSERT(r.receive_ack(space::initial, ack(p.packet_number), {}));
    LT_CHECK(r.delivered_prefix(retained.id) == 100 + f.data.size());
LT_END_AUTO_TEST(retained_segment_larger_than_codec_packet_limit_still_splits)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
