/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <memory>
#include <utility>
#include <vector>
#include <httpserver/detail/quic_server_admission.hpp>
#include "./quic_crypto_test_support.hpp"
static hd::quic_cid admission_cid(unsigned value) {
    hd::quic_cid result;
    result.size = 8;
    result.bytes[0] = std::byte(value & 255);
    result.bytes[1] = std::byte(value >> 8);
    return result;
}
static std::shared_ptr<hd::io_datagram> admission_initial(unsigned value = 1,
        std::span<const std::byte> token = {}, std::span<const std::byte> destination = {}) {
    auto cid = admission_cid(value), source = admission_cid(99);
    if (destination.empty()) destination = std::span(cid.bytes).first(cid.size);
    hd::quic_initial_keys keys;
    if (hd::derive_quic_initial_keys(destination, keys) != hd::quic_crypto_code::ok) throw std::runtime_error("derive fixture");
    std::vector<std::byte> payload(1200, std::byte{0}), scratch(2000);
    hd::quic_packet_write write;
    write.destination = destination;
    write.source = std::span(source.bytes).first(source.size);
    write.token = token;
    write.payload = payload;
    auto packet = std::make_shared<hd::io_datagram>();
    packet->bytes.resize(2000);
    auto protected_packet = hd::protect_quic_packet(keys.client, write, packet->bytes, scratch);
    if (protected_packet.code != hd::quic_crypto_code::ok) throw std::runtime_error("protect fixture");
    packet->bytes.resize(protected_packet.consumed);
    packet->bytes.shrink_to_fit();
    packet->peer.peer.address = *httpserver::net::parse_address("192.0.2.1");
    packet->peer.peer.port = 4000;
    packet->socket_id = 7;
    packet->local = packet->peer;
    packet->local->peer.port = 443;
    return packet;
}
static hd::quic_admission_limits direct_limits() {
    hd::quic_admission_limits limits;
    limits.require_retry = false;
    limits.max_pending = 2;
    limits.max_retained_bytes = 2600;
    return limits;
}
struct admission_sink : hd::datagram_sink {
    std::vector<std::shared_ptr<const hd::io_datagram>> packets;
    httpserver::executor* expected = nullptr;
    bool affinity = true;
    void on_datagram(std::shared_ptr<const hd::io_datagram> packet) noexcept override {
        affinity &= httpserver::current_executor() == expected;
        packets.push_back(std::move(packet));
    }
};
LT_BEGIN_SUITE(admission_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(admission_suite)
LT_BEGIN_AUTO_TEST(admission_suite, retry_roundtrip_validates_only_bound_path_and_preserves_original)
    hd::quic_server_admission admission;
    auto first = admission_initial();
    auto reply = admission.receive(first, 100);
    LT_ASSERT(reply.code == hd::quic_admission_code::reply && reply.reply);
    LT_CHECK_EQ(admission.pending_count(), std::size_t{0});
    auto retry = hd::parse_quic_envelope(reply.reply->packet()->bytes);
    LT_ASSERT(retry.code == hd::quic_codec_code::ok);
    LT_CHECK(retry.value.kind == hd::quic_packet_kind::retry);
    auto original = hd::parse_quic_envelope(first->bytes);
    LT_CHECK(std::equal(retry.value.destination.begin(), retry.value.destination.end(), original.value.source.begin(), original.value.source.end()));
    LT_CHECK(hd::verify_quic_retry_tag(original.value.destination, reply.reply->packet()->bytes) == hd::quic_crypto_code::ok);
    LT_CHECK(reply.reply->packet()->bytes.size() <= 3 * first->bytes.size());
    LT_CHECK(reply.reply->complete_send());
    LT_CHECK(!reply.reply->cancel_unsent());
    auto returned = admission_initial(1, retry.value.token, retry.value.source);
    auto accepted = admission.receive(returned, 101);
    LT_ASSERT(accepted.code == hd::quic_admission_code::pending);
    auto facts = admission.inspect(accepted.pending);
    LT_ASSERT(facts);
    LT_CHECK(facts->budget->validated());
    LT_CHECK(facts->original_destination == admission_cid(1));
    LT_CHECK_EQ(facts->budget->received(), static_cast<std::uint64_t>(returned->bytes.size()));
    auto wrong = admission_initial(1, retry.value.token, retry.value.source);
    wrong->peer.peer.port++;
    LT_CHECK(admission.receive(wrong, 101).code == hd::quic_admission_code::dropped);
    wrong = admission_initial(1, retry.value.token, std::span<const std::byte>(admission_cid(55).bytes).first(8));
    LT_CHECK(admission.receive(wrong, 101).code == hd::quic_admission_code::dropped);
    LT_CHECK(admission.receive(returned, 111).code == hd::quic_admission_code::dropped);
    auto invalid_token = std::vector<std::byte>(retry.value.token.begin(), retry.value.token.end());
    invalid_token.back() ^= std::byte{1};
    LT_CHECK(admission.receive(admission_initial(1, invalid_token, retry.value.source), 101).code == hd::quic_admission_code::dropped);
LT_END_AUTO_TEST(retry_roundtrip_validates_only_bound_path_and_preserves_original)
LT_BEGIN_AUTO_TEST(admission_suite, unknown_versions_use_only_invariants_and_never_allocate)
    hd::quic_server_admission admission;
    auto packet = admission_initial();
    packet->bytes[4] = std::byte{2};
    packet->bytes[0] = std::byte{0x80};
    auto response = admission.receive(packet, 100);
    LT_ASSERT(response.reply);
    auto vn = hd::parse_quic_envelope(response.reply->packet()->bytes);
    LT_ASSERT(vn.code == hd::quic_codec_code::ok);
    LT_CHECK(vn.value.kind == hd::quic_packet_kind::version_negotiation);
    LT_CHECK((std::vector<std::byte>(vn.value.versions.begin(), vn.value.versions.end()) == octets({0, 0, 0, 1})));
    auto input = hd::extract_quic_invariant_header(packet->bytes);
    LT_CHECK(std::equal(vn.value.destination.begin(), vn.value.destination.end(), input->source.bytes.begin(), input->source.bytes.begin() + input->source.size));
    LT_CHECK(std::equal(vn.value.source.begin(), vn.value.source.end(), input->destination.bytes.begin(), input->destination.bytes.begin() + input->destination.size));
    LT_CHECK(response.reply->cancel_unsent());
    LT_CHECK(!response.reply->complete_send());
    LT_CHECK_EQ(admission.pending_count(), std::size_t{0});
    packet->bytes[4] = std::byte{0};
    LT_CHECK(admission.receive(packet, 100).code == hd::quic_admission_code::dropped);
    packet->bytes[4] = std::byte{2};
    packet->bytes.resize(1199);
    LT_CHECK(admission.receive(packet, 100).code == hd::quic_admission_code::dropped);
LT_END_AUTO_TEST(unknown_versions_use_only_invariants_and_never_allocate)
LT_BEGIN_AUTO_TEST(admission_suite, direct_dedup_counts_datagrams_once_and_bounds_flood)
    hd::quic_server_admission admission(direct_limits());
    auto packet = admission_initial();
    auto attempt = admission.receive(packet, 100);
    LT_ASSERT(attempt.code == hd::quic_admission_code::pending);
    auto facts = admission.inspect(attempt.pending);
    LT_ASSERT(facts);
    LT_CHECK(!facts->budget->validated());
    LT_CHECK_EQ(facts->budget->received(), static_cast<std::uint64_t>(packet->bytes.size()));
    auto send = facts->budget->reserve_send(packet->bytes.size() * 3);
    LT_ASSERT(send);
    LT_CHECK(facts->budget->complete_send(*send));
    LT_CHECK(!facts->budget->reserve_send(1));
    auto duplicate = admission.receive(packet, 101);
    LT_ASSERT(duplicate.code == hd::quic_admission_code::pending);
    LT_CHECK(admission.inspect(duplicate.pending)->budget == facts->budget);
    LT_CHECK_EQ(admission.retained_bytes(), packet->bytes.size());
    LT_CHECK_EQ(facts->budget->received(), static_cast<std::uint64_t>(packet->bytes.size() * 2));
    LT_CHECK(facts->budget->reserve_send(packet->bytes.size() * 3).has_value());
    LT_CHECK(!facts->budget->reserve_send(1));
    LT_CHECK(admission.receive(admission_initial(2), 100).code == hd::quic_admission_code::pending);
    for (unsigned i = 3; i < 2003; ++i) {
        auto spoofed = admission_initial(i);
        spoofed->peer.peer.port = static_cast<std::uint16_t>(4000 + i);
        LT_CHECK(admission.receive(spoofed, 100).code == hd::quic_admission_code::capacity_exhausted);
    }
    LT_CHECK_EQ(admission.pending_count(), std::size_t{2});
    LT_CHECK(admission.retained_bytes() <= direct_limits().max_retained_bytes);
LT_END_AUTO_TEST(direct_dedup_counts_datagrams_once_and_bounds_flood)
LT_BEGIN_AUTO_TEST(admission_suite, all_truncations_authentication_failures_and_noninitials_drop)
    hd::quic_server_admission admission(direct_limits());
    auto packet = admission_initial();
    for (std::size_t size = 0; size < packet->bytes.size(); ++size) {
        auto truncated = std::make_shared<hd::io_datagram>(*packet);
        truncated->bytes.resize(size);
        LT_CHECK(admission.receive(truncated, 100).code == hd::quic_admission_code::dropped);
    }
    packet->bytes.back() ^= std::byte{1};
    LT_CHECK(admission.receive(packet, 100).code == hd::quic_admission_code::dropped);
    packet = admission_initial();
    packet->bytes[0] = std::byte{0xe0};
    LT_CHECK(admission.receive(packet, 100).code == hd::quic_admission_code::dropped);
    packet->peer.peer.address.family = httpserver::net::address_family::unspec;
    LT_CHECK(admission.receive(packet, 100).code == hd::quic_admission_code::dropped);
    packet->bytes.resize(hd::k_max_datagram_bytes + 1);
    LT_CHECK(admission.receive(packet, 100).code == hd::quic_admission_code::dropped);
    LT_CHECK_EQ(admission.pending_count(), std::size_t{0});
LT_END_AUTO_TEST(all_truncations_authentication_failures_and_noninitials_drop)
LT_BEGIN_AUTO_TEST(admission_suite, expiry_cancellation_and_stale_promotion_release_owned_capacity)
    hd::quic_server_admission admission(direct_limits());
    auto old = admission.receive(admission_initial(), 100);
    LT_ASSERT(old.code == hd::quic_admission_code::pending);
    admission.expire(110);
    LT_CHECK_EQ(admission.pending_count(), std::size_t{0});
    LT_CHECK_EQ(admission.retained_bytes(), std::size_t{0});
    auto fresh = admission.receive(admission_initial(), 110);
    LT_CHECK(!admission.cancel(old.pending));
    httpserver::manual_executor executor;
    hd::io_connection_owner owner(executor);
    auto sink = std::make_shared<admission_sink>();
    LT_CHECK(!admission.promote(old.pending, owner.datagrams(), sink, 110));
    LT_CHECK_EQ(admission.pending_count(), std::size_t{1});
    LT_CHECK(admission.cancel(fresh.pending));
    LT_CHECK(!admission.cancel(fresh.pending));
    LT_CHECK_EQ(admission.retained_bytes(), std::size_t{0});
LT_END_AUTO_TEST(expiry_cancellation_and_stale_promotion_release_owned_capacity)
LT_BEGIN_AUTO_TEST(admission_suite, duplicate_does_not_extend_original_expiry_or_hold_capacity)
    auto packet = admission_initial();
    auto limits = direct_limits();
    limits.max_pending = 1;
    limits.max_retained_bytes = packet->bytes.capacity();
    hd::quic_server_admission admission(limits);
    auto original = admission.receive(packet, 100);
    LT_ASSERT(original.code == hd::quic_admission_code::pending);
    auto facts = admission.inspect(original.pending);
    LT_ASSERT(facts);

    auto duplicate = admission.receive(admission_initial(), 109);
    LT_ASSERT(duplicate.code == hd::quic_admission_code::pending);
    auto duplicate_facts = admission.inspect(duplicate.pending);
    LT_ASSERT(duplicate_facts);
    LT_CHECK(duplicate_facts->initial == packet);
    LT_CHECK(duplicate_facts->budget == facts->budget);
    LT_CHECK_EQ(facts->budget->received(), static_cast<std::uint64_t>(packet->bytes.size() * 2));
    LT_CHECK_EQ(admission.pending_count(), std::size_t{1});
    LT_CHECK_EQ(admission.retained_bytes(), packet->bytes.capacity());
    LT_CHECK(admission.receive(admission_initial(2), 109).code == hd::quic_admission_code::capacity_exhausted);

    admission.expire(110);
    LT_CHECK_EQ(admission.pending_count(), std::size_t{0});
    LT_CHECK_EQ(admission.retained_bytes(), std::size_t{0});
    LT_CHECK(!admission.inspect(original.pending));
    LT_CHECK(!admission.inspect(duplicate.pending));
    auto replacement = admission.receive(packet, 110);
    LT_ASSERT(replacement.code == hd::quic_admission_code::pending);
    auto replacement_facts = admission.inspect(replacement.pending);
    LT_ASSERT(replacement_facts);
    LT_CHECK(replacement_facts->budget != facts->budget);
    LT_CHECK_EQ(replacement_facts->budget->received(), static_cast<std::uint64_t>(packet->bytes.size()));
    LT_CHECK(!admission.cancel(original.pending));
    LT_CHECK(!admission.cancel(duplicate.pending));
    httpserver::manual_executor executor;
    hd::io_connection_owner owner(executor);
    auto sink = std::make_shared<admission_sink>();
    LT_CHECK(!admission.promote(original.pending, owner.datagrams(), sink, 110));
    LT_CHECK(!admission.promote(duplicate.pending, owner.datagrams(), sink, 110));
    LT_CHECK_EQ(admission.pending_count(), std::size_t{1});
    LT_CHECK_EQ(admission.retained_bytes(), packet->bytes.capacity());
    LT_CHECK(admission.cancel(replacement.pending));
    LT_CHECK_EQ(admission.pending_count(), std::size_t{0});
    LT_CHECK_EQ(admission.retained_bytes(), std::size_t{0});
LT_END_AUTO_TEST(duplicate_does_not_extend_original_expiry_or_hold_capacity)
LT_BEGIN_AUTO_TEST(admission_suite, promotion_preserves_debits_and_owner_affinity_with_queue_retirement)
    hd::quic_server_admission admission(direct_limits());
    auto packet = admission_initial();
    auto attempt = admission.receive(packet, 100);
    auto facts = admission.inspect(attempt.pending);
    LT_ASSERT(facts);
    auto send = facts->budget->reserve_send(3600);
    LT_ASSERT(send);
    httpserver::manual_executor executor;
    hd::io_connection_owner owner(executor, 1, 3000);
    auto sink = std::make_shared<admission_sink>();
    sink->expected = &executor;
    auto promoted = admission.promote(attempt.pending, owner.datagrams(), sink, 101);
    LT_ASSERT(promoted);
    LT_CHECK(promoted->facts.budget == facts->budget);
    LT_CHECK_EQ(promoted->facts.budget->debited(), std::uint64_t{3600});
    LT_CHECK_EQ(admission.retained_bytes(), std::size_t{0});
    LT_CHECK(admission.receive(packet, 101).code == hd::quic_admission_code::queue_full);
    LT_CHECK_EQ(admission.pending_count(), std::size_t{0});
    executor.run_pending();
    LT_CHECK_EQ(sink->packets.size(), std::size_t{1});
    LT_CHECK(sink->packets[0] == packet && sink->affinity);
    LT_CHECK(admission.receive(packet, 101).code == hd::quic_admission_code::routed);
    LT_CHECK_EQ(facts->budget->received(), static_cast<std::uint64_t>(packet->bytes.size()));
    LT_CHECK(admission.routes().remove(promoted->registration));
    LT_CHECK(admission.receive(packet, 101).code == hd::quic_admission_code::retired);
    executor.run_pending();
    LT_CHECK_EQ(sink->packets.size(), std::size_t{1});
LT_END_AUTO_TEST(promotion_preserves_debits_and_owner_affinity_with_queue_retirement)
LT_BEGIN_AUTO_TEST(admission_suite, byte_capacity_failed_registration_and_queue_release_pending)
    auto limits = direct_limits();
    limits.max_retained_bytes = 1199;
    hd::quic_server_admission byte_limited(limits);
    LT_CHECK(byte_limited.receive(admission_initial(), 100).code == hd::quic_admission_code::capacity_exhausted);
    LT_CHECK_EQ(byte_limited.retained_bytes(), std::size_t{0});
    limits = direct_limits();
    limits.max_routes = 0;
    hd::quic_server_admission admission(limits);
    auto attempt = admission.receive(admission_initial(), 100);
    httpserver::manual_executor executor;
    hd::io_connection_owner owner(executor);
    auto sink = std::make_shared<admission_sink>();
    LT_CHECK(!admission.promote(attempt.pending, owner.datagrams(), sink, 100));
    LT_CHECK_EQ(admission.pending_count(), std::size_t{0});
    hd::quic_server_admission queue_limited(direct_limits());
    hd::io_connection_owner full(executor, 0, 0);
    attempt = queue_limited.receive(admission_initial(), 100);
    LT_CHECK(!queue_limited.promote(attempt.pending, full.datagrams(), sink, 100));
    LT_CHECK_EQ(queue_limited.pending_count(), std::size_t{0});
    LT_CHECK_EQ(queue_limited.retained_bytes(), std::size_t{0});
LT_END_AUTO_TEST(byte_capacity_failed_registration_and_queue_release_pending)
LT_BEGIN_AUTO_TEST(admission_suite, retained_allocation_capacity_is_bounded_before_publication)
    hd::quic_server_admission admission(direct_limits());
    auto packet = admission_initial();
    packet->bytes.reserve(65507);
    LT_CHECK(admission.receive(packet, 100).code == hd::quic_admission_code::capacity_exhausted);
    LT_CHECK_EQ(admission.retained_bytes(), std::size_t{0});
    LT_CHECK_EQ(admission.pending_count(), std::size_t{0});
LT_END_AUTO_TEST(retained_allocation_capacity_is_bounded_before_publication)
LT_BEGIN_AUTO_TEST(admission_suite, original_cid_can_differ_from_listener_retry_cid_length)
    hd::quic_admission_limits limits;
    hd::quic_server_admission admission(limits);
    auto original = admission_cid(1);
    original.size = 16;
    auto initial = admission_initial(1, {}, std::span(original.bytes).first(original.size));
    auto response = admission.receive(initial, 100);
    LT_ASSERT(response.reply);
    auto retry = hd::parse_quic_envelope(response.reply->packet()->bytes);
    LT_ASSERT(retry.code == hd::quic_codec_code::ok);
    LT_CHECK_EQ(retry.value.source.size(), std::size_t{8});
    auto accepted = admission.receive(admission_initial(1, retry.value.token, retry.value.source), 101);
    LT_ASSERT(accepted.code == hd::quic_admission_code::pending);
    LT_CHECK(admission.inspect(accepted.pending)->original_destination == original);
    limits.cid_length = 2;
    hd::quic_server_admission short_cid(limits);
    auto short_reply = short_cid.receive(initial, 100);
    LT_ASSERT(short_reply.reply);
    auto short_retry = hd::parse_quic_envelope(short_reply.reply->packet()->bytes);
    LT_CHECK_EQ(short_retry.value.source.size(), std::size_t{2});
    auto short_accepted = short_cid.receive(admission_initial(1, short_retry.value.token, short_retry.value.source), 101);
    LT_CHECK(short_accepted.code == hd::quic_admission_code::pending);
    hd::quic_server_admission direct(direct_limits());
    LT_CHECK(direct.receive(initial, 100).code == hd::quic_admission_code::dropped);
LT_END_AUTO_TEST(original_cid_can_differ_from_listener_retry_cid_length)
LT_BEGIN_AUTO_TEST(admission_suite, coalescing_credits_whole_datagram_once_and_paths_stay_independent)
    auto limits = direct_limits();
    limits.max_retained_bytes = 10000;
    hd::quic_server_admission admission(limits);
    auto first = admission_initial();
    auto second = admission_initial();
    first->bytes.insert(first->bytes.end(), second->bytes.begin(), second->bytes.end());
    auto accepted = admission.receive(first, 100);
    auto facts = admission.inspect(accepted.pending);
    LT_ASSERT(facts);
    LT_CHECK_EQ(facts->budget->received(), static_cast<std::uint64_t>(first->bytes.size()));
    auto full = facts->budget->reserve_send(first->bytes.size() * 3);
    LT_ASSERT(full);
    LT_CHECK(!facts->budget->reserve_send(1));
    second->peer.peer.port++;
    auto other = admission.receive(second, 100);
    auto other_facts = admission.inspect(other.pending);
    LT_ASSERT(other_facts);
    LT_CHECK(other_facts->budget != facts->budget);
    LT_CHECK_EQ(other_facts->budget->received(), static_cast<std::uint64_t>(second->bytes.size()));
    LT_CHECK(!other_facts->budget->validated());
    facts->budget->mark_address_validated();
    LT_CHECK(!other_facts->budget->validated());
    LT_CHECK(!other_facts->budget->cancel_unsent(*full));
    LT_CHECK(facts->budget->cancel_unsent(*full));
LT_END_AUTO_TEST(coalescing_credits_whole_datagram_once_and_paths_stay_independent)
LT_BEGIN_AUTO_TEST(admission_suite, stateless_retry_flood_keeps_no_pending_table)
    hd::quic_server_admission admission;
    auto initial = admission_initial();
    for (unsigned i = 0; i < 1000; ++i) {
        initial->peer.peer.port = static_cast<std::uint16_t>(4000 + i);
        auto response = admission.receive(initial, 100);
        LT_ASSERT(response.reply);
        LT_CHECK(response.reply->packet()->bytes.size() <= initial->bytes.size() * 3);
        LT_CHECK(response.reply->cancel_unsent());
    }
    LT_CHECK_EQ(admission.pending_count(), std::size_t{0});
    LT_CHECK_EQ(admission.retained_bytes(), std::size_t{0});
LT_END_AUTO_TEST(stateless_retry_flood_keeps_no_pending_table)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
