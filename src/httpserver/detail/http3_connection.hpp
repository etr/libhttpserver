/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "http3_connection.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP3_CONNECTION_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP3_CONNECTION_HPP_
#include <map>
#include <memory>
#include <vector>
#include <httpserver/detail/http3_frame.hpp>
#include <httpserver/detail/qpack_decoder.hpp>
#include <httpserver/detail/qpack_encoder.hpp>
#include <httpserver/detail/quic_stream_state.hpp>
namespace httpserver::detail {
struct http3_peer_settings {
    bool received = false;
    std::uint64_t qpack_capacity = 0, qpack_blocked = 0, max_field_section = k_quic_max_integer;
};
// Serialized server-side connection core. QUIC owns opening/reassembly and
// delivers ordered bytes plus terminal notifications. No packet pump here.
class http3_connection final {
 public:
    http3_connection(quic_storage_lease data, quic_storage_lease critical, http3_limits limits = {});
    std::optional<http3_error> attach_stream(std::uint64_t opened_peer_id);
    std::optional<http3_error> attach_local_stream(http3_role role, std::uint64_t opened_local_id);
    http3_feed_result feed(std::uint64_t id, std::span<const std::byte> input, std::uint64_t offset);
    const http3_event* event(std::uint64_t id) const;
    void release_event(std::uint64_t id);
    std::optional<http3_error> terminal(std::uint64_t id, quic_stream_terminal terminal);
    std::uint64_t offset(std::uint64_t id) const;
    const http3_peer_settings& peer_settings() const { return peer_; }
    std::span<const std::byte> local_prefix(http3_role role) const;
    bool advance_local_prefix(http3_role role, std::size_t count);
    void begin_turn() { frames_left_ = limits_.frames_per_turn; }

 private:
    struct stream {
        http3_role role = http3_role::pending;
        std::uint64_t offset = 0;
        std::array<std::byte, 10> scratch{};
        std::size_t scratch_size = 0;
        unsigned request_stage = 0;
        bool closed = false;
        server::reservation descriptor, stream_charge, fields_charge;
        std::optional<http3_frame_parser> parser;
        std::vector<qpack_field> fields;
        http3_event event;
        bool ready = false;
    };
    std::optional<http3_error> fail(std::uint64_t code, std::string_view diagnostic, std::uint64_t id);
    bool classify(stream& s, std::uint64_t type, std::uint64_t id);
    bool admit(stream& s, std::uint64_t id);
    bool admit_control(stream& s, std::uint64_t id);
    bool admit_request(stream& s, std::uint64_t id);
    bool process_control(const http3_event& e, std::uint64_t id);
    bool decode_fields(stream& s, std::uint64_t id);
    bool handle_frame(stream& s, const http3_feed_result& result, std::uint64_t id);
    bool process_safe(stream& s, std::uint64_t id);
    std::size_t feed_frames(stream& s, std::span<const std::byte> input, std::uint64_t id);
    std::optional<http3_error> clean_terminal(stream& s, std::uint64_t id);
    bool process(stream& s, std::uint64_t id);
    bool settings(std::span<const std::byte> payload, std::uint64_t id);
    std::size_t uni_input(stream& s, std::span<const std::byte> input, std::uint64_t id);
    bool qpack_input(stream& s, std::byte byte, std::uint64_t id);
    quic_storage_lease data_, critical_;
    http3_limits limits_;
    server::reservation metadata_;
    std::map<std::uint64_t, std::unique_ptr<stream>> streams_;
    std::array<bool, 3> critical_seen_{};
    http3_peer_settings peer_;
    qpack_decoder decoder_;
    qpack_encoder encoder_;
    std::array<std::byte, 7> control_prefix_{std::byte{0}, std::byte{4}, std::byte{4}, std::byte{1}, std::byte{0}, std::byte{7}, std::byte{0}};
    std::array<std::byte, 1> encoder_prefix_{std::byte{2}}, decoder_prefix_{std::byte{3}};
    std::array<std::optional<std::uint64_t>, 3> local_ids_{};
    std::array<std::size_t, 3> prefix_positions_{};
    std::size_t frames_left_ = 0;
    std::optional<http3_error> error_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP3_CONNECTION_HPP_
