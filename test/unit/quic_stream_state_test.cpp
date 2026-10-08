/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdexcept>
#include <httpserver/detail/quic_stream_state.hpp>
#include "./quic_codec_test_support.hpp"
namespace {
using code = hd::quic_stream_code;
using role = hd::quic_endpoint_role;
auto budget() { return httpserver::server::resource_budget::root({}); }
}  // namespace
LT_BEGIN_SUITE(stream_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(stream_suite)
LT_BEGIN_AUTO_TEST(stream_suite, all_id_classes_and_high_water_opening)
    for (std::uint64_t id = 0; id < 4; ++id) {
        auto identity = hd::quic_stream_id(id);
        LT_ASSERT(identity);
        LT_CHECK(identity->initiator == (id & 1 ? role::server : role::client));
        LT_CHECK(identity->unidirectional == static_cast<bool>(id & 2));
        LT_CHECK(identity->ordinal == 0);
        LT_CHECK(hd::quic_stream_id(hd::k_quic_max_integer - 3 + id)->ordinal == (hd::k_quic_max_integer >> 2));
    }
    LT_CHECK(!hd::quic_stream_id(hd::k_quic_max_integer + 1));
    for (auto local : {role::client, role::server}) {
        hd::quic_stream_ids ids(local);
        const std::uint64_t local_bit = local == role::server ? 1 : 0;
        LT_CHECK(ids.open_local(false) == local_bit);
        LT_CHECK(ids.open_local(false) == local_bit + 4);
        LT_CHECK(ids.open_local(true) == local_bit + 2);
        LT_CHECK(ids.observe_peer(local_bit + 8).code == code::stream_state_error);
        const auto peer_class = (local_bit ^ 1) + 2;
        LT_CHECK(ids.observe_peer(hd::k_quic_max_integer - 3 + peer_class));
        LT_CHECK(ids.opened(peer_class) && ids.opened(peer_class + 40));
        LT_CHECK(ids.opened_count(static_cast<std::uint8_t>(peer_class)) == (std::uint64_t{1} << 60));
        LT_CHECK(!ids.opened(local_bit + 8));
        LT_CHECK(ids.observe_peer(local_bit));
        LT_CHECK(ids.observe_peer(hd::k_quic_max_integer + 1).code == code::frame_encoding_error);
        LT_CHECK(ids.opened_count(4) == 0);
    }
LT_END_AUTO_TEST(all_id_classes_and_high_water_opening)
LT_BEGIN_AUTO_TEST(stream_suite, unavailable_directions_and_unopened_ids_reject)
    for (auto local : {role::client, role::server}) {
        hd::quic_stream_ids ids(local);
        auto allocated = ids.open_local(true);
        LT_ASSERT(allocated);
        auto own = *allocated;
        auto peer = own ^ 1;
        LT_ASSERT(ids.observe_peer(peer));
        hd::quic_stream_state sender(own, local, ids, {}, budget()), receiver(peer, local, ids, {}, budget());
        LT_CHECK(sender.receive_state() == hd::quic_receive_state::unavailable);
        LT_CHECK(receiver.send_state() == hd::quic_send_state::unavailable);
        LT_CHECK(sender.receive(hd::quic_stream_frame{own, 0, {}, true}).code == code::stream_state_error);
        LT_CHECK(sender.receive(hd::quic_reset_stream_frame{own, 0, 0}).code == code::stream_state_error);
        LT_CHECK(receiver.receive(hd::quic_stop_sending_frame{peer, 0}).code == code::stream_state_error);
        LT_CHECK(receiver.record_stream_sent(0, 0, true).code == code::stream_state_error);
        LT_CHECK(sender.read({}).code == code::stream_state_error);
        LT_CHECK(receiver.receive(hd::quic_stream_frame{peer + 4, 0, {}}).code == code::stream_state_error);
        bool rejected = false;
        try {
            hd::quic_stream_state unopened(own + 4, local, ids, {}, budget());
        }
        catch (const std::invalid_argument&) { rejected = true; }
        LT_CHECK(rejected);
    }
LT_END_AUTO_TEST(unavailable_directions_and_unopened_ids_reject)
LT_BEGIN_AUTO_TEST(stream_suite, fin_waits_for_gaps_and_consumption_with_independent_halves)
    hd::quic_stream_ids ids(role::server);
    LT_ASSERT(ids.observe_peer(0));
    hd::quic_stream_state s(0, role::server, ids, {8, 2, 10}, budget());
    auto tail = octets({3, 4}), head = octets({1, 2});
    LT_CHECK(s.receive(hd::quic_stream_frame{0, 2, tail, true}));
    LT_CHECK(s.final_size() == 4 && s.highest_received() == 4);
    LT_CHECK(s.receive_state() == hd::quic_receive_state::size_known);
    LT_CHECK(!s.take_terminal());
    std::array<std::byte, 4> out{};
    LT_CHECK(s.read(out).bytes == 0);
    LT_CHECK(s.record_stream_sent(0, 2, true));
    LT_CHECK(s.send_state() == hd::quic_send_state::fin_sent);
    LT_CHECK(s.receive(hd::quic_stream_frame{0, 0, head}));
    LT_CHECK(s.receive_state() == hd::quic_receive_state::data_received);
    LT_CHECK(s.read(std::span(out).first(1)).bytes == 1);
    LT_CHECK(!s.take_terminal());
    LT_CHECK(s.read(out).bytes == 3);
    LT_CHECK(std::equal(out.begin(), out.begin() + 3, octets({2, 3, 4}).begin()));
    auto eof = s.take_terminal();
    LT_ASSERT(eof);
    LT_CHECK(eof->kind == hd::quic_terminal_kind::eof && !s.take_terminal());
    LT_CHECK(s.receive_state() == hd::quic_receive_state::data_consumed);
    LT_CHECK(s.acknowledge_all_stream_data());
    LT_CHECK(s.acknowledge_all_stream_data());
    LT_CHECK(s.send_state() == hd::quic_send_state::data_acknowledged);
LT_END_AUTO_TEST(fin_waits_for_gaps_and_consumption_with_independent_halves)
LT_BEGIN_AUTO_TEST(stream_suite, final_size_checks_survive_reset_and_terminal_delivery)
    hd::quic_stream_ids ids(role::server);
    LT_ASSERT(ids.observe_peer(0));
    hd::quic_stream_state s(0, role::server, ids, {8, 2, 10}, budget());
    auto data = octets({3, 4});
    LT_ASSERT(s.receive(hd::quic_stream_frame{0, 2, data}));
    const auto storage = s.retained_storage();
    LT_CHECK(s.receive(hd::quic_stream_frame{0, 0, {}, true}).code == code::final_size_error);
    LT_CHECK(s.receive(hd::quic_reset_stream_frame{0, 7, 3}).code == code::final_size_error);
    LT_CHECK(s.retained_storage() == storage && !s.final_size());
    LT_ASSERT(s.receive(hd::quic_reset_stream_frame{0, 7, 4}));
    LT_CHECK(s.retained_storage() == 0 && s.buffered_bytes() == 0 && s.final_size() == 4);
    LT_CHECK(s.receive_state() == hd::quic_receive_state::reset_received);
    LT_CHECK(s.receive(hd::quic_reset_stream_frame{0, 99, 4}));
    LT_CHECK(s.receive(hd::quic_stream_frame{0, 2, data, true}));
    LT_CHECK(s.retained_storage() == 0 && s.receive_state() == hd::quic_receive_state::reset_received);
    LT_CHECK(s.read(data).bytes == 0);
    auto reset = s.take_terminal();
    LT_ASSERT(reset);
    LT_CHECK(reset->kind == hd::quic_terminal_kind::reset && reset->error == 7);
    LT_CHECK(s.receive_state() == hd::quic_receive_state::reset_consumed && !s.take_terminal());
    LT_CHECK(s.receive(hd::quic_stream_frame{0, 4, data}).code == code::final_size_error);
    LT_CHECK(s.receive(hd::quic_stream_frame{0, 0, data, true}).code == code::final_size_error);
    LT_CHECK(s.receive(hd::quic_reset_stream_frame{0, 7, 5}).code == code::final_size_error);
    LT_CHECK(s.receive(hd::quic_reset_stream_frame{0, 7, 4}));
    LT_CHECK(!s.take_terminal());
LT_END_AUTO_TEST(final_size_checks_survive_reset_and_terminal_delivery)
LT_BEGIN_AUTO_TEST(stream_suite, reset_interrupts_unread_data_but_never_repeats_application_terminal)
    hd::quic_stream_ids ids(role::server);
    LT_ASSERT(ids.observe_peer(0));
    hd::quic_stream_state s(0, role::server, ids, {}, budget());
    auto data = octets({1, 2});
    LT_ASSERT(s.receive(hd::quic_stream_frame{0, 0, data, true}));
    LT_CHECK(s.receive(hd::quic_reset_stream_frame{0, 9, 3}).code == code::final_size_error);
    LT_ASSERT(s.receive(hd::quic_reset_stream_frame{0, 9, 2}));
    LT_CHECK(s.read(data).bytes == 0);
    auto terminal = s.take_terminal();
    LT_ASSERT(terminal);
    LT_CHECK(terminal->kind == hd::quic_terminal_kind::reset);
    hd::quic_stream_state empty(0, role::server, ids, {}, budget());
    LT_ASSERT(empty.receive(hd::quic_stream_frame{0, 0, {}, true}));
    terminal = empty.take_terminal();
    LT_ASSERT(terminal);
    LT_CHECK(terminal->kind == hd::quic_terminal_kind::eof);
    LT_CHECK(empty.receive(hd::quic_reset_stream_frame{0, 8, 0}));
    LT_CHECK(empty.receive(hd::quic_stream_frame{0, 0, {}, true}));
    LT_CHECK(!empty.take_terminal());
    hd::quic_stream_state reset_first(0, role::server, ids, {}, budget());
    LT_ASSERT(reset_first.receive(hd::quic_reset_stream_frame{0, 3, hd::k_quic_max_integer}));
    LT_CHECK(reset_first.retained_storage() == 0);
    LT_CHECK(reset_first.receive(hd::quic_stream_frame{0, 0, {}, true}).code == code::final_size_error);
LT_END_AUTO_TEST(reset_interrupts_unread_data_but_never_repeats_application_terminal)
LT_BEGIN_AUTO_TEST(stream_suite, stop_sending_creates_one_reset_request_and_requires_sent_confirmation)
    hd::quic_stream_ids ids(role::client);
    LT_ASSERT(ids.open_local(false));
    hd::quic_stream_state s(0, role::client, ids, {}, budget());
    LT_CHECK(s.acknowledge_all_stream_data().code == code::stream_state_error);
    LT_CHECK(s.acknowledge_reset().code == code::stream_state_error);
    LT_ASSERT(s.record_stream_sent(0, 3, true));
    LT_CHECK(s.receive(hd::quic_stop_sending_frame{0, 9}));
    LT_CHECK(s.receive(hd::quic_stop_sending_frame{0, 99}));
    LT_CHECK(s.send_state() == hd::quic_send_state::reset_pending);
    LT_CHECK(s.record_stream_sent(0, 3, true).code == code::stream_state_error);
    LT_CHECK(s.record_reset_sent({0, 99, 3}).code == code::stream_state_error);
    auto request = s.take_reset_request();
    LT_ASSERT(request);
    LT_CHECK(request->stream == 0 && request->error == 9 && request->final_size == 3);
    LT_CHECK(!s.take_reset_request());
    LT_CHECK(s.acknowledge_reset().code == code::stream_state_error);
    LT_CHECK(s.record_reset_sent(*request));
    LT_CHECK(s.record_reset_sent(*request));
    LT_CHECK(s.send_state() == hd::quic_send_state::reset_sent);
    LT_CHECK(s.receive(hd::quic_stop_sending_frame{0, 20}));
    LT_CHECK(!s.take_reset_request());
    LT_CHECK(s.acknowledge_reset());
    LT_CHECK(s.acknowledge_reset());
    LT_CHECK(s.send_state() == hd::quic_send_state::reset_acknowledged);
    LT_CHECK(s.record_reset_sent(*request));
    LT_CHECK(s.send_state() == hd::quic_send_state::reset_acknowledged);
    LT_CHECK(s.receive(hd::quic_stream_frame{0, 0, {}, true}));
    LT_CHECK(s.take_terminal().has_value());
LT_END_AUTO_TEST(stop_sending_creates_one_reset_request_and_requires_sent_confirmation)
LT_BEGIN_AUTO_TEST(stream_suite, malformed_input_and_failed_fin_insertion_leave_state_unchanged)
    hd::quic_stream_ids ids(role::server);
    LT_ASSERT(ids.observe_peer(0));
    hd::quic_stream_state s(0, role::server, ids, {2, 1, 10}, budget());
    auto two = octets({1, 2});
    LT_ASSERT(s.receive(hd::quic_stream_frame{0, 4, two}));
    auto charged = s.retained_storage();
    LT_CHECK(s.receive(hd::quic_stream_frame{0, 0, two, true}).code == code::final_size_error);
    LT_CHECK(s.receive(hd::quic_stream_frame{0, 6, two, true}).code == code::byte_limit_exceeded);
    LT_CHECK(!s.final_size() && s.highest_received() == 6 && s.retained_storage() == charged);
    LT_CHECK(s.receive(hd::quic_stream_frame{0, hd::k_quic_max_integer, two}).code == code::frame_encoding_error);
    LT_CHECK(s.receive(hd::quic_reset_stream_frame{0, hd::k_quic_max_integer + 1, 6}).code == code::frame_encoding_error);
    LT_CHECK(s.receive(hd::quic_reset_stream_frame{0, 0, hd::k_quic_max_integer + 1}).code == code::frame_encoding_error);
    LT_CHECK(s.receive(hd::quic_stop_sending_frame{0, hd::k_quic_max_integer + 1}).code == code::frame_encoding_error);
    LT_CHECK(s.receive(hd::quic_stream_frame{hd::k_quic_max_integer + 1, 0, {}}).code == code::frame_encoding_error);
    LT_CHECK(s.record_stream_sent(hd::k_quic_max_integer, 2, false).code == code::frame_encoding_error);
    LT_CHECK(s.receive_state() == hd::quic_receive_state::receiving && s.send_state() == hd::quic_send_state::ready);
LT_END_AUTO_TEST(malformed_input_and_failed_fin_insertion_leave_state_unchanged)
LT_BEGIN_AUTO_TEST(stream_suite, reset_before_stream_still_tracks_valid_late_ends_without_buffering)
    hd::quic_stream_ids ids(role::server);
    LT_ASSERT(ids.observe_peer(0));
    hd::quic_stream_state s(0, role::server, ids, {}, budget());
    LT_ASSERT(s.receive(hd::quic_reset_stream_frame{0, 8, 6}));
    LT_CHECK(s.receive(hd::quic_stream_frame{0, 4, octets({5, 6})}));
    LT_CHECK(s.highest_received() == 6 && s.retained_storage() == 0);
    LT_CHECK(s.receive_state() == hd::quic_receive_state::reset_received);
LT_END_AUTO_TEST(reset_before_stream_still_tracks_valid_late_ends_without_buffering)
LT_BEGIN_AUTO_TEST(stream_suite, sent_fin_and_local_reset_keep_final_size_and_ack_states)
    hd::quic_stream_ids ids(role::client);
    LT_ASSERT(ids.open_local(false));
    hd::quic_stream_state s(0, role::client, ids, {}, budget());
    LT_CHECK(s.record_stream_sent(2, 2, false));
    LT_CHECK(s.record_stream_sent(0, 2, true).code == code::final_size_error);
    LT_CHECK(s.highest_sent() == 4 && s.send_state() == hd::quic_send_state::sending);
    LT_CHECK(s.record_stream_sent(4, 0, true));
    LT_CHECK(s.record_stream_sent(0, 4, true));
    LT_CHECK(s.record_stream_sent(4, 1, false).code == code::final_size_error);
    LT_CHECK(s.record_reset_sent({0, 5, 3}).code == code::final_size_error);
    LT_CHECK(s.record_reset_sent({4, 5, 4}).code == code::stream_state_error);
    LT_CHECK(s.record_reset_sent({0, hd::k_quic_max_integer + 1, 4}).code == code::frame_encoding_error);
    LT_CHECK(s.record_reset_sent({0, 5, 4}));
    LT_CHECK(!s.take_reset_request());
    LT_CHECK(s.receive(hd::quic_stop_sending_frame{0, 6}));
    LT_CHECK(!s.take_reset_request());
    LT_CHECK(s.acknowledge_all_stream_data().code == code::stream_state_error);
    LT_CHECK(s.acknowledge_reset());
    hd::quic_stream_state done(0, role::client, ids, {}, budget());
    LT_CHECK(done.record_stream_sent(0, 0, true));
    LT_CHECK(done.acknowledge_all_stream_data());
    LT_CHECK(done.receive(hd::quic_stop_sending_frame{0, 7}));
    LT_CHECK(!done.take_reset_request());
    LT_CHECK(done.record_stream_sent(0, 0, true));
    LT_CHECK(done.send_state() == hd::quic_send_state::data_acknowledged);
    LT_CHECK(done.record_reset_sent({0, 7, 0}).code == code::stream_state_error);
LT_END_AUTO_TEST(sent_fin_and_local_reset_keep_final_size_and_ack_states)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
