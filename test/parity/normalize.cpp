/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// See normalize.hpp for the comparison contract.

#include "normalize.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

namespace parity {
namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
}

bool iequals(const std::string& a, const std::string& b) {
    return a.size() == b.size() && to_lower(a) == to_lower(b);
}

// RFC 7231 §7.1.1.2: server clock value. Not pinned by any transcript;
// elided so no expectation can depend on wall-clock timing.
bool is_volatile_header(const std::string& name) {
    return iequals(name, "Date");
}

std::string hex_decode(const std::string& hex) {
    std::string out;
    out.reserve(hex.size() / 2);
    auto nibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        return ch - 'A' + 10;
    };
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<char>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
    }
    return out;
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return !in.bad();
}

match_result failure(const expectation& e, const std::string& description) {
    match_result m;
    m.ok = false;
    std::ostringstream ss;
    ss << "expect-line=" << e.line << ": " << description;
    m.diff = ss.str();
    return m;
}

}  // namespace

normalized_exchange normalize(const observed_response& r) {
    normalized_exchange ex;
    ex.status = r.status;
    ex.status_line = r.raw_status_line;
    ex.body = r.body;
    ex.framing = r.framing;
    for (const observed_header& h : r.headers) {
        if (is_volatile_header(h.name)) continue;
        ex.headers.push_back(h);
    }
    return ex;
}

const observed_header* find_header(const normalized_exchange& ex,
                                   const std::string& name) {
    for (const observed_header& h : ex.headers) {
        if (iequals(h.name, name)) return &h;
    }
    return nullptr;
}

bool mask_match(const std::string& pattern, const std::string& value) {
    static const std::string mask = "<*>";
    std::size_t pi = 0, vi = 0;
    std::size_t star = pattern.find(mask);
    while (star != std::string::npos) {
        // Literal prefix before the mask must match exactly.
        if (pattern.compare(pi, star - pi, value, vi, star - pi) != 0) return false;
        vi += star - pi;
        pi = star + mask.size();
        if (pi == pattern.size()) return true;  // trailing mask: rest matches
        // Try every possible end position for the mask run.
        std::size_t next_star = pattern.find(mask, pi);
        std::size_t literal_len =
            (next_star == std::string::npos ? pattern.size() : next_star) - pi;
        if (next_star == std::string::npos) {
            // Final literal tail: must appear at the very end of value.
            if (literal_len > value.size() - vi) return false;
            return pattern.compare(pi, literal_len, value,
                                   value.size() - literal_len, literal_len) == 0;
        }
        std::size_t found = value.find(pattern.substr(pi, literal_len), vi);
        while (found != std::string::npos) {
            if (mask_match(pattern.substr(pi), value.substr(found))) return true;
            found = value.find(pattern.substr(pi, literal_len), found + 1);
        }
        return false;
    }
    // No mask left: exact remainder.
    if (pattern.size() - pi != value.size() - vi) return false;
    return pattern.compare(pi, std::string::npos, value, vi, std::string::npos) == 0;
}

match_result check_expectation(const expectation& e,
                               const normalized_exchange& ex,
                               const std::string& body_file_base) {
    match_result m;
    m.ok = true;
    switch (e.kind) {
        case expect_kind::status: {
            if (e.number != ex.status) {
                return failure(e, "expected status " + std::to_string(e.number) +
                                      ", got " + std::to_string(ex.status));
            }
            return m;
        }
        case expect_kind::status_line: {
            if (e.value != ex.status_line) {
                return failure(e, "expected status line \"" + e.value +
                                      "\", got \"" + ex.status_line + "\"");
            }
            return m;
        }
        case expect_kind::header: {
            const observed_header* h = find_header(ex, e.name);
            if (h == nullptr) {
                return failure(e, "expected header " + e.name + " but it is absent");
            }
            if (!mask_match(e.value, h->value)) {
                return failure(e, "expected header " + e.name + ": \"" + e.value +
                                      "\", got \"" + h->value + "\"");
            }
            return m;
        }
        case expect_kind::header_absent: {
            if (find_header(ex, e.name) != nullptr) {
                return failure(e, "expected header " + e.name + " to be absent");
            }
            return m;
        }
        case expect_kind::header_order: {
            std::size_t cursor = 0;
            for (const std::string& want : e.order) {
                bool found = false;
                for (; cursor < ex.headers.size(); ++cursor) {
                    if (iequals(ex.headers[cursor].name, want)) {
                        ++cursor;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return failure(e, "expected header order " + want +
                                          " not satisfied on the wire");
                }
            }
            return m;
        }
        case expect_kind::body: {
            if (e.value != ex.body) {
                return failure(e, "expected body \"" + e.value + "\" (len " +
                                      std::to_string(e.value.size()) +
                                      "), got (len " +
                                      std::to_string(ex.body.size()) + ")");
            }
            return m;
        }
        case expect_kind::body_hex: {
            std::string want = hex_decode(e.value);
            if (want != ex.body) {
                return failure(e, "expected body hex " + e.value + " (len " +
                                      std::to_string(want.size()) +
                                      "), got (len " +
                                      std::to_string(ex.body.size()) + ")");
            }
            return m;
        }
        case expect_kind::body_len: {
            if (static_cast<std::size_t>(e.number) != ex.body.size()) {
                return failure(e, "expected body length " +
                                      std::to_string(e.number) + ", got " +
                                      std::to_string(ex.body.size()));
            }
            return m;
        }
        case expect_kind::body_file: {
            std::string want;
            if (!read_file(body_file_base + "/" + e.name, want)) {
                return failure(e, "body_file fixture not readable: " + e.name);
            }
            if (want != ex.body) {
                return failure(e, "expected body from file " + e.name + " (len " +
                                      std::to_string(want.size()) +
                                      "), got (len " +
                                      std::to_string(ex.body.size()) + ")");
            }
            return m;
        }
        case expect_kind::framing: {
            if (e.value != ex.framing) {
                return failure(e, "expected framing " + e.value + ", got " +
                                      ex.framing);
            }
            return m;
        }
        case expect_kind::connection:
        case expect_kind::closer:
            // Handled by the runner (connection-level, not per-response).
            return m;
    }
    return m;
}

}  // namespace parity
