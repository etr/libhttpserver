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
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/detail/qpack_field_section.hpp>
#include <httpserver/detail/qpack_dynamic_table.hpp>
namespace httpserver::detail {
// Input and any supplied table must stay immutable for both decode passes.
// The connection owner serializes table mutations; the decoder retains none.
class qpack_decoder {
 public:
    struct section_prefix {
        qpack_status status;
        std::uint64_t required = 0;
        std::uint64_t base = 0;
        std::size_t consumed = 0;
    };
    static section_prefix inspect_prefix(std::span<const std::uint8_t> input, const qpack_dynamic_table& table) noexcept;
    qpack_section_result decode(std::span<const std::uint8_t> input, qpack_section_limits limits) const;
    // Owners with precharged storage distinguish allocation failure from peer
    // limits. Wire validation remains the same allocation-free first pass.
    qpack_section_result decode_allocated(std::span<const std::uint8_t> input, qpack_section_limits limits) const;

    qpack_section_result decode_dynamic(std::span<const std::uint8_t> input, const qpack_dynamic_table& table,
                                        qpack_section_limits limits) const;
    qpack_section_result decode_dynamic_allocated(std::span<const std::uint8_t> input, const qpack_dynamic_table& table,
                                                 qpack_section_limits limits, section_prefix section) const;

 private:
    struct wire_field {
        const qpack_static_entry* entry = nullptr;
        const qpack_dynamic_entry* dynamic = nullptr;
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
    static qpack_integer_result prefix(std::span<const std::uint8_t> input) noexcept;
    static qpack_status read_name(std::span<const std::uint8_t> input, std::size_t remaining, std::size_t encoded_limit, wire_field& field,
                                  const qpack_dynamic_table* table, std::uint64_t base, std::uint64_t required) noexcept;
    static qpack_status read_field(std::span<const std::uint8_t> input, std::size_t& remaining, std::size_t encoded_limit, wire_field& field,
                                    const qpack_dynamic_table* table, std::uint64_t base, std::uint64_t required) noexcept;
    static qpack_field materialize(const wire_field& field);
    static qpack_status process(std::span<const std::uint8_t> input, qpack_section_limits limits,
                                std::vector<qpack_field>* output, std::size_t& count, const qpack_dynamic_table* table = nullptr, const section_prefix* frozen = nullptr);
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QPACK_DECODER_HPP_
