/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/http/method.hpp>
#include <string>
#include <utility>

namespace httpserver::http {

auto method::extension(std::string name) -> method {
    std::string upper;
    upper.reserve(name.size());
    for (const char c : name) {
        upper.push_back(static_cast<char>(detail::ascii_upper(c)));
    }
    return method{method_id::extension, std::move(upper)};
}

std::string to_string(const method_set& set) {
    std::string out;
    for (std::size_t id = 0; id < static_cast<std::size_t>(method_id::extension); ++id) {
        if (!set.contains(method::known(static_cast<method_id>(id)))) {
            continue;
        }
        if (!out.empty()) out.append(", ");
        out.append(detail::method_tokens[id]);
    }
    return out;
}

}  // namespace httpserver::http
