/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "http3_connection.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP3_CONNECTION_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP3_CONNECTION_HPP_
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <httpserver/detail/qpack_instructions.hpp>
#include <httpserver/detail/http3_frame.hpp>
#include <httpserver/detail/qpack_decoder.hpp>
#include <httpserver/detail/qpack_encoder.hpp>
#include <httpserver/detail/quic_stream_state.hpp>
namespace httpserver::detail {
struct http3_peer_settings {
    bool received = false;
    std::uint64_t qpack_capacity = 0, qpack_blocked = 0, max_field_section = k_quic_max_integer;
};
struct http3_encoded_section {
    qpack_status status;
    quic_storage_lease owner;
    server::reservation charge;
    std::string value;
    std::uint64_t token = 0;
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
    qpack_status queue_encoder_instruction(const qpack_encoder_instruction& instruction);
    http3_encoded_section encode_section(std::uint64_t id, std::span<const qpack_field_view> fields);
    http3_encoded_section encode_response_section(std::uint64_t id, std::span<const qpack_field_view> fields);
    bool blocked(std::uint64_t id) const;
    void publish_section(std::uint64_t token);
    void cancel_section(std::uint64_t token);
    void cancel_unpublished(std::uint64_t id);
    void shutdown();
    void begin_turn() { frames_left_ = limits_.frames_per_turn; critical_frames_left_.fill(limits_.frames_per_turn); }

 private:
    struct stream {
        http3_role role = http3_role::pending;
        std::uint64_t offset = 0;
        std::array<std::byte, 10> scratch{};
        std::size_t scratch_size = 0;
        unsigned request_stage = 0;
        bool closed = false, blocked = false;
        std::size_t retained_bytes = 0;
        qpack_decoder::section_prefix section;
        server::reservation blocked_charge, instruction_charge;
        std::vector<std::uint8_t> instruction;
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
    bool encoder_input(stream& s, std::uint64_t id);
    bool decoder_input(stream& s, std::uint64_t id);
    std::size_t instruction_capacity(const stream& s) const;
    bool unblock_sections();
    bool queue_feedback(qpack_decoder_instruction instruction);
    void release_retained(stream& s);
    bool initialize_qpack();
    bool admit_qpack_output();
    qpack_status queue_encoder_allocated(const qpack_encoder_instruction& instruction);
    bool apply_decoder_instruction(qpack_decoder_instruction instruction);
    struct outgoing_section {
        std::uint64_t stream = 0, required = 0;
        server::reservation charge;
        qpack_reference_hold references;
        std::uint64_t token = 0;
        bool published = true;
    };
    http3_encoded_section encode_section_impl(std::uint64_t id, std::span<const qpack_field_view> fields, bool provisional);
    http3_encoded_section encode_section_allocated(std::uint64_t id, std::span<const qpack_field_view> fields, bool provisional);
    bool retain_section(std::uint64_t id, qpack_dynamic_bytes_result& encoded, server::reservation charge, bool provisional, std::uint64_t& token);
    bool section_may_block(std::uint64_t id, std::uint64_t required) const;
    bool section_stream(std::uint64_t id) const;
    void insert_response_names(std::span<const qpack_field_view> fields);
    std::size_t& frame_budget(const stream& s);
    bool potentially_blocked(std::uint64_t id) const;
    std::size_t outgoing_blocked() const;
    quic_storage_lease data_, critical_;
    http3_limits limits_;
    server::reservation metadata_;
    std::map<std::uint64_t, std::unique_ptr<stream>> streams_;
    std::array<bool, 3> critical_seen_{};
    http3_peer_settings peer_;
    qpack_dynamic_table decoder_table_{qpack_table_role::decoder}, encoder_table_{qpack_table_role::encoder};
    std::list<outgoing_section> outgoing_;
    server::reservation output_charge_;
    std::array<std::vector<std::byte>, 2> qpack_output_;
    std::size_t retained_bytes_ = 0, blocked_streams_ = 0;
    std::uint64_t reported_inserts_ = 0;
    qpack_decoder decoder_;
    qpack_encoder encoder_;
    std::vector<std::byte> control_prefix_;
    std::array<std::byte, 1> encoder_prefix_{std::byte{2}}, decoder_prefix_{std::byte{3}};
    std::array<std::optional<std::uint64_t>, 3> local_ids_{};
    std::array<std::size_t, 3> prefix_positions_{};
    std::size_t frames_left_ = 0;
    // The last allowance admits new stream types independently of existing roles.
    std::array<std::size_t, 4> critical_frames_left_{};
    std::uint64_t next_section_ = 1;
    bool stopped_ = false;
    std::optional<http3_error> error_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP3_CONNECTION_HPP_
