/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_SUPPORT_HTTP3_SEMANTIC_H1_HPP_
#define TEST_SUPPORT_HTTP3_SEMANTIC_H1_HPP_
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <vector>
#include "../parity/response_frame.hpp"
namespace h3semantic {
using response_field = std::pair<std::string, std::string>;
struct h1_fields {
    std::vector<response_field> headers;
    std::vector<response_field> trailers;
};
inline h1_fields observe_h1_fields(const parity::observed_response& response, std::string_view wire) {
    const auto head_end = wire.find("\r\n\r\n");
    const auto status_end = wire.find("\r\n");
    if (head_end == std::string_view::npos || status_end > head_end) throw std::runtime_error("missing H1 initial field block");
    std::size_t initial_fields = 0;
    for (auto at = status_end + 2; at < head_end; at = wire.find("\r\n", at) + 2) ++initial_fields;
    if (initial_fields > response.headers.size()) throw std::runtime_error("H1 transcript field count mismatch");

    // The existing framer validates chunks and appends their trailer fields in
    // wire order. Split that sequence at the actual initial block boundary.
    h1_fields result;
    for (std::size_t i = 0; i < response.headers.size(); ++i) {
        const auto& field = response.headers[i];
        if (i >= initial_fields)
            result.trailers.emplace_back(field.name, field.value);
        else if (field.name.starts_with("x-"))
            result.headers.emplace_back(field.name, field.value);
    }
    return result;
}
}  // namespace h3semantic
#endif  // TEST_SUPPORT_HTTP3_SEMANTIC_H1_HPP_
