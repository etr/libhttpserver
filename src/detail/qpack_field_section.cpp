/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/qpack_field_section.hpp>

namespace httpserver::detail {

auto qpack_section_result::materialize() const -> http::fields {
    http::fields out;
    for (const auto& field : fields) out.append(field.name, field.value);
    return out;
}

bool qpack_admit_field(std::size_t name, std::size_t value, std::size_t& remaining) noexcept {
    // RFC 9114 field-section size includes 32 octets per occurrence.
    if (remaining < 32 || name > remaining - 32 || value > remaining - 32 - name) return false;
    remaining -= 32 + name + value;
    return true;
}

}  // namespace httpserver::detail
