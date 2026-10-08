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
#error "qpack_field_section.hpp is internal to libhttpserver"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QPACK_FIELD_SECTION_HPP_
#define SRC_HTTPSERVER_DETAIL_QPACK_FIELD_SECTION_HPP_
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <httpserver/detail/qpack_primitives.hpp>
#include <httpserver/detail/qpack_static_table.hpp>
#include <httpserver/http/fields.hpp>
namespace httpserver::detail {
struct qpack_field_view {
    std::string_view name;
    std::string_view value;
    bool never_indexed = false;
};
struct qpack_field {
    std::string name;
    std::string value;
    bool never_indexed = false;
    qpack_field_view view() const noexcept { return {name, value, never_indexed}; }
};
struct qpack_section_limits {
    std::size_t max_compressed_bytes;
    std::size_t max_expanded_bytes;
    std::size_t max_fields;
};
struct qpack_section_result {
    qpack_status status;
    std::vector<qpack_field> fields;
    http::fields materialize() const {
        http::fields out;
        for (const auto& field : fields) out.append(field.name, field.value);
        return out;
    }
};
inline std::span<const std::uint8_t> qpack_octets(std::string_view value) noexcept {
    return {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()};
}
inline bool qpack_admit_field(std::size_t name, std::size_t value, std::size_t& remaining) noexcept {
    // RFC 9114 field-section size includes 32 octets per occurrence.
    if (remaining < 32 || name > remaining - 32 || value > remaining - 32 - name) return false;
    remaining -= 32 + name + value;
    return true;
}
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QPACK_FIELD_SECTION_HPP_
