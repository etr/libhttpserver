// Appendix data from RFC 7541, Copyright (c) 2015 IETF Trust and the
// persons identified as authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
#if !defined(HTTPSERVER_COMPILATION)
#error "qpack_decoder.hpp is internal to libhttpserver"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QPACK_DECODER_HPP_
#define SRC_HTTPSERVER_DETAIL_QPACK_DECODER_HPP_
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/detail/qpack_field_section.hpp>
namespace httpserver::detail {
// Complete static-only sections; input must stay immutable for the call's two
// passes. No compression state is retained across independent calls.
class qpack_decoder {
 public:
    qpack_section_result decode(std::span<const std::uint8_t> input, qpack_section_limits limits) const {
        try {
            return decode_allocated(input, limits);
        } catch (const std::bad_alloc&) {
            return {{qpack_state::limit_exceeded}, {}};
        } catch (const std::length_error&) {
            return {{qpack_state::limit_exceeded}, {}};
        }
    }
    // Owners with precharged storage distinguish allocation failure from peer
    // limits. Wire validation remains the same allocation-free first pass.
    qpack_section_result decode_allocated(std::span<const std::uint8_t> input, qpack_section_limits limits) const {
        if (input.size() > limits.max_compressed_bytes) return {{qpack_state::limit_exceeded}, {}};
        std::size_t count = 0;
        auto status = process(input, limits, nullptr, count);
        if (!status.ok()) return {complete(status), {}};
        qpack_section_result result;
        result.fields.reserve(count);
        status = process(input, limits, &result.fields, count);
        if (!status.ok()) return {complete(status), {}};
        return result;
    }

 private:
    struct wire_field {
        const qpack_static_entry* entry = nullptr;
        std::span<const std::uint8_t> name_wire;
        std::span<const std::uint8_t> value_wire;
        std::size_t name_size = 0;
        std::size_t value_size = 0;
        std::size_t consumed = 0;
        bool indexed = false;
        bool never_indexed = false;
    };
    static qpack_status complete(qpack_status status) noexcept {
        return status.state == qpack_state::incomplete ? qpack_status{qpack_state::malformed} : status;
    }
    static qpack_integer_result prefix(std::span<const std::uint8_t> input) noexcept {
        const auto required = qpack_decode_integer(input, 8, {});
        if (!required.status.ok()) return required;
        if (required.value != 0) return {{qpack_state::malformed}};
        const auto base_wire = input.subspan(required.consumed);
        const auto base = qpack_decode_integer(base_wire, 7, {});
        if (!base.status.ok()) return base;
        // With Required Insert Count zero, the sign would make Base negative.
        if ((base_wire[0] & 0x80) != 0) return {{qpack_state::malformed}};
        return {{}, 0, required.consumed + base.consumed};
    }
    static qpack_status read_name(std::span<const std::uint8_t> input, std::size_t remaining, std::size_t encoded_limit, wire_field& field) noexcept {
        const auto bits = input[0];
        if ((bits & 0x80) != 0 || (bits & 0x40) != 0) {
            field.indexed = (bits & 0x80) != 0;
            const auto static_bit = field.indexed ? 0x40 : 0x10;
            if ((bits & static_bit) == 0) return {qpack_state::malformed};
            const auto index = qpack_decode_integer(input, field.indexed ? 6 : 4, {});
            if (!index.status.ok()) return index.status;
            field.entry = qpack_static_lookup(index.value);
            if (field.entry == nullptr) return {qpack_state::malformed};
            field.consumed = index.consumed;
            field.name_size = field.entry->name.size();
            field.value_size = field.indexed ? field.entry->value.size() : 0;
            field.never_indexed = !field.indexed && (bits & 0x20) != 0;
        } else if ((bits & 0x20) != 0) {
            const auto name = qpack_inspect_string(input, {encoded_limit, remaining - 32}, 3, 8);
            if (!name.status.ok()) return name.status;
            field.name_wire = input.first(name.consumed);
            field.name_size = static_cast<std::size_t>(name.value);
            field.consumed = name.consumed;
            field.never_indexed = (bits & 0x10) != 0;
        } else {
            // Both post-Base families always reference the dynamic table.
            return {qpack_state::malformed};
        }
        return {};
    }
    static qpack_status read_field(std::span<const std::uint8_t> input, std::size_t& remaining, std::size_t encoded_limit, wire_field& field) noexcept {
        if (remaining < 32) return {qpack_state::limit_exceeded};
        auto status = read_name(input, remaining, encoded_limit, field);
        if (!status.ok()) return status;
        if (field.name_size > remaining - 32) return {qpack_state::limit_exceeded};
        if (!field.indexed) {
            const auto value_wire = input.subspan(field.consumed);
            const auto value = qpack_inspect_string(value_wire, {encoded_limit, remaining - 32 - field.name_size});
            if (!value.status.ok()) return value.status;
            field.value_wire = value_wire.first(value.consumed);
            field.value_size = static_cast<std::size_t>(value.value);
            field.consumed += value.consumed;
        }
        return qpack_admit_field(field.name_size, field.value_size, remaining) ? qpack_status{} : qpack_status{qpack_state::limit_exceeded};
    }
    static qpack_field materialize(const wire_field& field) {
        qpack_field out;
        out.never_indexed = field.never_indexed;
        out.name = field.entry != nullptr ? std::string(field.entry->name) :
            qpack_decode_string(field.name_wire, {field.name_wire.size(), field.name_size}, 3, 8).value;
        out.value = field.indexed ? std::string(field.entry->value) :
            qpack_decode_string(field.value_wire, {field.value_wire.size(), field.value_size}).value;
        return out;
    }
    static qpack_status process(std::span<const std::uint8_t> input, qpack_section_limits limits,
                                std::vector<qpack_field>* output, std::size_t& count) {
        const auto section = prefix(input);
        if (!section.status.ok()) return section.status;
        input = input.subspan(section.consumed);
        auto remaining = limits.max_expanded_bytes;
        count = 0;
        while (!input.empty()) {
            if (count == limits.max_fields) return {qpack_state::limit_exceeded};
            wire_field field;
            const auto status = read_field(input, remaining, limits.max_compressed_bytes, field);
            if (!status.ok()) return status;
            if (output != nullptr) output->push_back(materialize(field));
            input = input.subspan(field.consumed);
            ++count;
        }
        return {};
    }
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QPACK_DECODER_HPP_
