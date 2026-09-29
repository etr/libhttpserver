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

// See transcript.hpp for the transcript grammar and its rationale.

#include "transcript.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

namespace parity {
namespace {

bool is_blank_or_comment(const std::string& line) {
    return line.empty() || line[0] == '#';
}

std::string first_token(const std::string& s) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    for (; i < s.size(); ++i) {
        if (std::isspace(static_cast<unsigned char>(s[i]))) break;
        out.push_back(s[i]);
    }
    return out;
}

std::size_t skip_ws(const std::string& s, std::size_t from) {
    while (from < s.size() && std::isspace(static_cast<unsigned char>(s[from]))) ++from;
    return from;
}

std::size_t skip_nonws(const std::string& s, std::size_t from) {
    while (from < s.size() && !std::isspace(static_cast<unsigned char>(s[from]))) ++from;
    return from;
}

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> split_tokens(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool is_hex_digit(char ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) ||
           (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return ch - 'A' + 10;
}

bool parse_int(const std::string& s, int& out) {
    if (s.empty()) return false;
    std::size_t i = 0;
    bool negative = false;
    if (s[0] == '-') { negative = true; i = 1; } else if (s[0] == '+') { i = 1; }
    if (i >= s.size()) return false;
    long long value = 0;
    for (; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        value = value * 10 + (s[i] - '0');
        if (value > 1000000000LL) return false;
    }
    out = static_cast<int>(negative ? -value : value);
    return true;
}

bool decode_hex_byte(char hi, char lo, char& out) {
    if (!is_hex_digit(hi) || !is_hex_digit(lo)) return false;
    out = static_cast<char>((hex_value(hi) << 4) | hex_value(lo));
    return true;
}

bool is_valid_hex(const std::string& s) {
    if (s.empty() || (s.size() % 2) != 0) return false;
    for (char ch : s) {
        if (!is_hex_digit(ch)) return false;
    }
    return true;
}

std::string hex_to_bytes(const std::string& s) {
    std::string out;
    out.reserve(s.size() / 2);
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) {
        char byte = 0;
        decode_hex_byte(s[i], s[i + 1], byte);
        out.push_back(byte);
    }
    return out;
}

bool valid_framing_token(const std::string& t) {
    return t == "content-length" || t == "chunked" || t == "none";
}

bool valid_connection_token(const std::string& t) {
    return t == "keep-alive" || t == "close";
}

bool valid_closer_token(const std::string& t) {
    return t == "server" || t == "client";
}

// Expectation argument parser state, shared by the per-kind handlers.
struct parser_state {
    transcript* t = nullptr;
    tcase* current_case = nullptr;
    std::string path;
    int line = 0;
};

[[noreturn]] void fail(const parser_state& st, const std::string& message) {
    throw transcript_error(st.path, st.line, message);
}

// Unescape a quoted string (leading and trailing double quote required).
std::string quoted_unescape(const std::string& arg, const parser_state& st) {
    if (arg.size() < 2 || arg.front() != '"' || arg.back() != '"') {
        fail(st, "expect body/status_line requires a double-quoted value");
    }
    return unescape(arg.substr(1, arg.size() - 2), st.path, st.line);
}

bool contains_mask(const std::string& value) {
    return value.find("<*>") != std::string::npos;
}

void expect_status(parser_state& st, const std::string& arg) {
    if (!parse_int(trim(arg), st.current_case->expects.back().number) ||
        st.current_case->expects.back().number < 100 ||
        st.current_case->expects.back().number > 599) {
        fail(st, "expect status requires an HTTP status code in [100, 599]");
    }
}

void expect_header(parser_state& st, const std::string& arg) {
    expectation& e = st.current_case->expects.back();
    if (!arg.empty() && arg[0] == '~') {
        e.kind = expect_kind::header_absent;
        e.name = trim(arg.substr(1));
        if (e.name.empty()) fail(st, "expect header ~<Name> requires a header name");
        return;
    }
    std::size_t colon = arg.find(':');
    if (colon == std::string::npos) {
        fail(st, "expect header requires '<Name>: <value>'");
    }
    e.name = trim(arg.substr(0, colon));
    e.value = trim(arg.substr(colon + 1));
    if (e.name.empty()) fail(st, "expect header requires a non-empty header name");
}

void expect_header_order(parser_state& st, const std::string& arg) {
    st.current_case->expects.back().order = split_tokens(arg);
    if (st.current_case->expects.back().order.empty()) {
        fail(st, "expect header_order requires at least one header name");
    }
}

void expect_body(parser_state& st, const std::string& arg) {
    std::string text = quoted_unescape(arg, st);
    if (contains_mask(text)) {
        fail(st, "<*> masks are only allowed in expect header values");
    }
    st.current_case->expects.back().value = std::move(text);
}

void expect_body_hex(parser_state& st, const std::string& arg) {
    if (!is_valid_hex(trim(arg))) {
        fail(st, "expect body_hex requires an even-length hex string");
    }
    st.current_case->expects.back().value = trim(arg);
}

void expect_body_len(parser_state& st, const std::string& arg) {
    int len = 0;
    if (!parse_int(trim(arg), len) || len < 0) {
        fail(st, "expect body_len requires a non-negative integer");
    }
    st.current_case->expects.back().number = len;
}

void expect_token_kind(parser_state& st, const std::string& arg,
                       bool (*valid)(const std::string&), const char* what) {
    std::string token = trim(arg);
    if (!valid(token)) fail(st, std::string("expect ") + what + " token is invalid: " + token);
    st.current_case->expects.back().value = std::move(token);
}

void expect_body_file(parser_state& st, const std::string& arg) {
    std::string name = trim(arg);
    if (name.empty()) fail(st, "expect body_file requires a file name");
    st.current_case->expects.back().name = std::move(name);
}

void parse_expect(parser_state& st, const std::string& rest) {
    if (st.current_case == nullptr) {
        fail(st, "expect directive outside a case");
    }
    std::size_t begin = skip_ws(rest, 0);
    std::size_t end = skip_nonws(rest, begin);
    std::string kind = rest.substr(begin, end - begin);
    std::string arg = rest.substr(skip_ws(rest, end));
    expectation e;
    e.kind = expect_kind::status;
    e.line = st.line;
    st.current_case->expects.push_back(std::move(e));

    if (kind == "status") { expect_status(st, arg); }
    else if (kind == "status_line") {
        st.current_case->expects.back().kind = expect_kind::status_line;
        st.current_case->expects.back().value = quoted_unescape(arg, st);
    }
    else if (kind == "header") {
        st.current_case->expects.back().kind = expect_kind::header;
        expect_header(st, arg);
    }
    else if (kind == "header_order") {
        st.current_case->expects.back().kind = expect_kind::header_order;
        expect_header_order(st, arg);
    }
    else if (kind == "body") {
        st.current_case->expects.back().kind = expect_kind::body;
        expect_body(st, arg);
    }
    else if (kind == "body_hex") {
        st.current_case->expects.back().kind = expect_kind::body_hex;
        expect_body_hex(st, arg);
    }
    else if (kind == "body_len") {
        st.current_case->expects.back().kind = expect_kind::body_len;
        expect_body_len(st, arg);
    }
    else if (kind == "body_file") {
        st.current_case->expects.back().kind = expect_kind::body_file;
        expect_body_file(st, arg);
    }
    else if (kind == "framing") {
        st.current_case->expects.back().kind = expect_kind::framing;
        expect_token_kind(st, arg, valid_framing_token, "framing");
    }
    else if (kind == "connection") {
        st.current_case->expects.back().kind = expect_kind::connection;
        expect_token_kind(st, arg, valid_connection_token, "connection");
    }
    else if (kind == "closer") {
        st.current_case->expects.back().kind = expect_kind::closer;
        expect_token_kind(st, arg, valid_closer_token, "closer");
    }
    else {
        fail(st, "unknown expect kind: " + kind);
    }
}

void parse_send(parser_state& st, const std::string& rest, bool hex) {
    if (st.current_case == nullptr) fail(st, "send directive outside a case");
    send_segment seg;
    seg.line = st.line;
    if (hex) {
        std::string hex_text = trim(rest);
        if (!is_valid_hex(hex_text)) {
            fail(st, "send_hex requires an even-length hex string");
        }
        seg.bytes = hex_to_bytes(hex_text);
    } else {
        // The directive keyword is followed by exactly one separating
        // space; the rest of the line is the segment content. Additional
        // leading spaces are content (intentional trailing spaces would
        // have been stripped earlier and must be written as \x20).
        std::string content = rest;
        if (!content.empty() && content[0] == ' ') content = content.substr(1);
        if (content.empty()) fail(st, "send requires segment content (use send_hex for raw bytes)");
        seg.bytes = unescape(content, st.path, st.line);
    }
    st.current_case->sends.push_back(std::move(seg));
}

void parse_option(parser_state& st, const std::string& rest) {
    if (st.current_case == nullptr) fail(st, "option directive outside a case");
    std::string name = first_token(rest);
    std::string arg = trim(rest.substr(skip_nonws(rest, skip_ws(rest, 0))));
    if (name == "read_timeout_ms") {
        int ms = 0;
        if (!parse_int(arg, ms) || ms <= 0) {
            fail(st, "option read_timeout_ms requires a positive integer");
        }
        st.current_case->read_timeout_ms = ms;
        return;
    }
    if (name == "transport") {
        if (arg != "raw" && arg != "curl") {
            fail(st, "option transport must be raw or curl: " + arg);
        }
        st.current_case->transport = arg;
        return;
    }
    if (name == "curl_user") {
        if (arg.empty()) fail(st, "option curl_user requires user:password");
        st.current_case->curl_user = arg;
        return;
    }
    if (name == "curl_auth") {
        if (arg != "basic" && arg != "digest") {
            fail(st, "option curl_auth must be basic or digest: " + arg);
        }
        st.current_case->curl_auth = arg;
        return;
    }
    if (name == "curl_tls") {
        if (arg != "true" && arg != "false") {
            fail(st, "option curl_tls must be true or false: " + arg);
        }
        st.current_case->curl_tls = (arg == "true");
        return;
    }
    fail(st, "unknown option: " + name);
}

void parse_profile(parser_state& st, const std::string& rest, bool inside_case) {
    if (inside_case) fail(st, "profile directive inside a case");
    if (!st.t->profile.empty()) fail(st, "duplicate profile directive");
    std::string name = trim(rest);
    if (name.empty()) fail(st, "profile requires a name");
    st.t->profile = name;
    st.t->profile_line = st.line;
}

// A `case` while another case is open implicitly closes it (`end` is
// optional before the next case or EOF).
void parse_case(parser_state& st, const std::string& rest) {
    std::string name = trim(rest);
    if (name.empty()) fail(st, "case requires a name");
    for (const tcase& existing : st.t->cases) {
        if (existing.name == name) fail(st, "duplicate case name: " + name);
    }
    st.t->cases.emplace_back();
    st.t->cases.back().name = name;
    st.t->cases.back().line = st.line;
    st.current_case = &st.t->cases.back();
}

}  // namespace

transcript_error::transcript_error(const std::string& path, int line, const std::string& message)
    : std::runtime_error(path + ":" + std::to_string(line) + ": " + message),
      line_(line) { }

std::string unescape(const std::string& escaped, const std::string& path, int line) {
    std::string out;
    out.reserve(escaped.size());
    for (std::size_t i = 0; i < escaped.size(); ++i) {
        char ch = escaped[i];
        if (ch != '\\') { out.push_back(ch); continue; }
        if (i + 1 >= escaped.size()) {
            throw transcript_error(path, line, "truncated escape at end of segment");
        }
        char next = escaped[++i];
        switch (next) {
            case 'r': out.push_back('\r'); break;
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case '\\': out.push_back('\\'); break;
            case '"': out.push_back('"'); break;
            case 'x': {
                if (i + 2 >= escaped.size() ||
                    !decode_hex_byte(escaped[i + 1], escaped[i + 2], ch)) {
                    throw transcript_error(path, line, "bad \\xHH escape");
                }
                out.push_back(ch);
                i += 2;
                break;
            }
            default:
                throw transcript_error(path, line,
                    std::string("unknown escape: \\") + next);
        }
    }
    return out;
}

std::string escape(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    static const char* hex_digits = "0123456789ABCDEF";
    for (unsigned char ch : raw) {
        switch (ch) {
            case '\r': out += "\\r"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            default:
                if (ch >= 0x20 && ch < 0x7F) {
                    out.push_back(static_cast<char>(ch));
                } else {
                    out += "\\x";
                    out.push_back(hex_digits[(ch >> 4) & 0xF]);
                    out.push_back(hex_digits[ch & 0xF]);
                }
        }
    }
    return out;
}

transcript parse_transcript(const std::string& text, const std::string& source_path) {
    transcript t;
    t.source_path = source_path;
    parser_state st;
    st.t = &t;
    st.path = source_path;

    std::istringstream in(text);
    std::string raw;
    int line_no = 0;
    bool inside_case = false;
    while (std::getline(in, raw)) {
        ++line_no;
        // Strip \r and trailing whitespace before any directive checks.
        std::string line = trim(raw);
        if (is_blank_or_comment(line)) continue;
        st.line = line_no;

        std::string directive = first_token(line);
        std::string rest = line.substr(skip_nonws(line, 0));
        if (directive == "profile") { parse_profile(st, rest, inside_case); }
        else if (directive == "case") { parse_case(st, rest); inside_case = true; }
        else if (directive == "end") {
            if (!inside_case) fail(st, "end directive outside a case");
            inside_case = false;
            st.current_case = nullptr;
        }
        else if (directive == "send") { parse_send(st, rest, false); }
        else if (directive == "send_hex") { parse_send(st, rest, true); }
        else if (directive == "expect") { parse_expect(st, rest); }
        else if (directive == "option") { parse_option(st, rest); }
        else { fail(st, "unknown directive: " + directive); }
    }

    if (inside_case) {
        // A case left open at EOF is fine per the grammar; current_case
        // stays valid because t outlives this loop.
        inside_case = false;
    }
    if (t.profile.empty()) {
        throw transcript_error(source_path, line_no,
            "profile directive required before any case");
    }
    if (st.current_case != nullptr) st.current_case = nullptr;
    return t;
}

transcript parse_transcript_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw transcript_error(path, 0, "cannot open transcript file");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) throw transcript_error(path, 0, "error reading transcript file");
    return parse_transcript(buffer.str(), path);
}

}  // namespace parity
