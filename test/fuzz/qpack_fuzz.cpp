/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <cstdlib>
#include <httpserver/detail/qpack_decoder.hpp>
#include <httpserver/detail/qpack_encoder.hpp>
#include <httpserver/detail/qpack_instructions.hpp>
#include "fuzz/qpack_fuzz.hpp"
#include "unit/quic_event_replay.hpp"
namespace {
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
using quic_fuzz::require;
void accounting(const hs::resource_budget& budget, bool empty = false) {
    for (unsigned i = 0; i < hs::resource_count; ++i) {
        const auto resource = static_cast<hs::resource>(i);
        require(budget.in_use(resource) <= budget.capacity(resource), "QPACK budget capacity");
        if (empty) require(budget.in_use(resource) == 0, "QPACK teardown");
    }
}
void status(qpack_fuzz_report& report, hd::qpack_status result, unsigned operation) {
    require(static_cast<unsigned>(result.state) <= static_cast<unsigned>(hd::qpack_state::eviction_prohibited), "typed QPACK state");
    report.trace.push_back(static_cast<unsigned>(result.state));
    ++report.attempted[operation];
    if (result.ok()) ++report.accepted[operation];
}
void bytes(qpack_fuzz_report& report, const hd::qpack_bytes_result& result, std::size_t size, unsigned operation) {
    status(report, result.status, operation);
    require(result.consumed <= size && result.value.size() <= 128, "bounded QPACK bytes");
    if (!result.status.ok()) require(result.value.empty() && !result.consumed, "transactional QPACK bytes");
    report.trace.push_back(result.consumed);
    for (unsigned char value : result.value) report.trace.push_back(value);
}
qpack_fuzz_report replay(std::span<const std::uint8_t> input) {
    input = input.first(std::min(input.size(), std::size_t{512}));
    qpack_fuzz_report report;
    const unsigned profile = input.empty() ? 0 : input[0] % 8;
    const auto instruction_size = input.size() < 2 ? 0 : std::min<std::size_t>(input[1], input.size() - 2);
    auto wire = input.subspan(std::min(input.size(), std::size_t{2}));
    const auto instructions = wire.first(instruction_size);
    const auto section = wire.subspan(instruction_size);
    const hd::qpack_section_limits limits{profile == 3 ? 2U : 128U,
        profile == 1 ? 41U : profile == 6 ? 42U : 256U, profile == 2 ? 0U : 8U};
    const hd::qpack_instruction_limits instruction_limits{128, 128, 128};
    hs::budget_limits capacity; capacity.set(hs::resource::qpack_table_bytes, 256);
    const auto budget = hs::resource_budget::root(capacity);
    {
        hd::qpack_dynamic_table table(hd::qpack_table_role::decoder), outgoing(hd::qpack_table_role::encoder);
        require(table.initialize(budget, profile == 4 ? 0 : 128).ok(), "initialize QPACK decoder");
        require(outgoing.initialize(budget, 128).ok(), "initialize QPACK encoder");
        std::size_t at = 0;
        for (unsigned count = 0; at < instructions.size() && count < 32; ++count) {
            const auto decoded = hd::qpack_decode_encoder_instruction(instructions.subspan(at), instruction_limits);
            status(report, decoded.status, 0);
            require(decoded.consumed <= instructions.size() - at && decoded.consumed <= 128, "encoder instruction consumption");
            if (!decoded.status.ok()) {
                require(!decoded.consumed, "failed encoder consumption"); break;
            }
            require(decoded.consumed > 0, "encoder progress");
            auto encoded = hd::qpack_encode_encoder_instruction(decoded.instruction, instruction_limits);
            require(encoded.status.ok(), "encoder instruction roundtrip encode");
            auto roundtrip = hd::qpack_decode_encoder_instruction(hd::qpack_octets(encoded.value), instruction_limits);
            require(roundtrip.status.ok() && roundtrip.instruction.kind == decoded.instruction.kind &&
                roundtrip.instruction.index == decoded.instruction.index && roundtrip.instruction.static_name == decoded.instruction.static_name &&
                roundtrip.instruction.name == decoded.instruction.name && roundtrip.instruction.value == decoded.instruction.value,
                "encoder instruction roundtrip");
            status(report, hd::qpack_apply_encoder_instruction(decoded.instruction, instruction_limits, table), 1);
            status(report, hd::qpack_apply_encoder_instruction(decoded.instruction, instruction_limits, outgoing), 2);
            require(table.bytes() <= table.capacity() && outgoing.bytes() <= outgoing.capacity(), "table capacity");
            require(table.size() <= 4 && outgoing.size() <= 4, "bounded table entries");
            accounting(budget); at += decoded.consumed;
        }
        at = 0;
        for (unsigned count = 0; at < instructions.size() && count < 32; ++count) {
            const auto decoded = hd::qpack_decode_decoder_instruction(instructions.subspan(at));
            status(report, decoded.status, 3);
            require(decoded.consumed <= instructions.size() - at, "decoder consumption");
            if (!decoded.status.ok()) {
                require(!decoded.consumed, "failed decoder consumption"); break;
            }
            require(decoded.consumed > 0, "decoder progress");
            const auto encoded = hd::qpack_encode_decoder_instruction(decoded.instruction, 128);
            require(encoded.status.ok(), "decoder encode");
            const auto roundtrip = hd::qpack_decode_decoder_instruction(hd::qpack_octets(encoded.value));
            require(roundtrip.status.ok() && roundtrip.instruction.kind == decoded.instruction.kind &&
                roundtrip.instruction.value == decoded.instruction.value, "decoder instruction roundtrip");
            if (decoded.instruction.kind == hd::qpack_decoder_instruction_kind::insert_count_increment)
                status(report, outgoing.increment_known_received(decoded.instruction.value), 4);
            at += decoded.consumed; accounting(budget);
        }
        const auto prefix = hd::qpack_decoder::inspect_prefix(section, table);
        status(report, prefix.status, 5); report.trace.push_back(prefix.required); report.trace.push_back(prefix.base);
        const auto decoded = hd::qpack_decoder{}.decode_dynamic(section, table, limits);
        status(report, decoded.status, 6); report.section_state = decoded.status.state;
        require(decoded.status.ok() || decoded.fields.empty(), "failed section has no fields");
        report.fields = decoded.fields;
        for (const auto& field : decoded.fields) report.expanded += 32 + field.name.size() + field.value.size();
        require(report.fields.size() <= limits.max_fields && report.expanded <= limits.max_expanded_bytes, "decoded field bounds");
        if (decoded.status.ok()) {
            // Static re-encoding can be larger than an admitted dynamic input.
            const auto encoded = hd::qpack_encoder{}.encode_section(decoded.fields, {128, 256, 8}, (profile & 1) != 0);
            status(report, encoded.status, 7);
            if (encoded.status.ok()) {
                const auto again = hd::qpack_decoder{}.decode(hd::qpack_octets(encoded.value), {128, 256, 8});
                require(again.status.ok() && again.fields.size() == decoded.fields.size(), "field roundtrip size");
                for (std::size_t i = 0; i < decoded.fields.size(); ++i)
                    require(again.fields[i].name == decoded.fields[i].name && again.fields[i].value == decoded.fields[i].value &&
                        again.fields[i].never_indexed == decoded.fields[i].never_indexed, "ordered field roundtrip");
            }
        }
        report.table_bytes = table.bytes(); report.inserts = table.insert_count(); accounting(budget);
    }
    accounting(budget, true);
    const auto integer = hd::qpack_decode_integer(wire, profile + 1, {});
    status(report, integer.status, 8); report.trace.push_back(integer.value); report.trace.push_back(integer.consumed);
    require(integer.consumed <= wire.size(), "integer consumption");
    if (integer.status.ok()) {
        const auto encoded = hd::qpack_encode_integer(integer.value, profile + 1, 0, {}, 128);
        require(encoded.status.ok(), "integer encode");
        require(hd::qpack_decode_integer(hd::qpack_octets(encoded.value), profile + 1, {}).value == integer.value, "integer roundtrip");
    } else {
        require(integer.value == 0 && integer.consumed == 0, "failed integer empty");
    }
    bytes(report, hd::qpack_decode_string(wire, {128, 128}), wire.size(), 9);
    const auto huffman = hd::hpack_decode_huffman(wire, {128, 128});
    status(report, hd::qpack_codec::translate(huffman.status), 10);
    require(huffman.value.size() <= 128 && huffman.consumed <= wire.size(), "Huffman bounds");
    if (huffman.status.ok()) {
        const auto encoded = hd::hpack_encode_huffman(hd::qpack_octets(huffman.value), {128, 128}, 128);
        if (encoded.status.ok()) require(hd::hpack_decode_huffman(hd::qpack_octets(encoded.value), {128, 128}).value == huffman.value, "Huffman roundtrip");
    }
    require(report.trace.size() <= 2048, "bounded QPACK trace");
    return report;
}
}  // namespace
bool qpack_fuzz_report::operator==(const qpack_fuzz_report& other) const {
    if (section_state != other.section_state || expanded != other.expanded || table_bytes != other.table_bytes ||
        inserts != other.inserts || trace != other.trace || attempted != other.attempted || accepted != other.accepted || fields.size() != other.fields.size()) return false;
    for (std::size_t i = 0; i < fields.size(); ++i)
        if (fields[i].name != other.fields[i].name || fields[i].value != other.fields[i].value || fields[i].never_indexed != other.fields[i].never_indexed) return false;
    return true;
}
qpack_fuzz_report qpack_fuzz_replay(std::span<const std::uint8_t> input) {
    auto result = replay(input); require(result == replay(input), "deterministic QPACK replay"); return result;
}
std::size_t qpack_fuzz_replay_corpus(const char* directory) { return quic_fuzz::replay_corpus(directory, qpack_fuzz_replay); }
#ifdef QPACK_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) { qpack_fuzz_replay({data, size}); return 0; }
#endif
