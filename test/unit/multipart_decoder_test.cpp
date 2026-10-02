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
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-117 step 1: the strict incremental multipart/form-data wire
// decoder (PRD-V3N-REQ-021, v2 within-cap parity per PRD-V3N-REQ-038,
// DR-V3-001). The matrix pins:
//   - the canonical shapes: one field part (the multipart_field corpus
//     body) and a two-part field+file body, decoded one-shot;
//   - EVERY split point of the canonical body -- including splits
//     inside the delimiter dashes, inside the boundary, inside the
//     final "--", inside header lines, and inside the CRLF pairs --
//     decodes identically to the one-shot form (the TASK-116
//     every-split harness);
//   - preamble junk drained (never stored, never emitted) and an
//     epilogue after the final boundary tolerated;
//   - delimiter lookalikes inside part data are DATA (a CRLF followed
//     by a boundary prefix, or the boundary plus junk, is not a
//     delimiter; the match is case-sensitive), and a bare LF inside
//     data passes through;
//   - Content-Disposition parameter shapes: quoted filename with
//     backslash escapes, name-only parts, case-insensitive parameter
//     names, per-part Content-Type capture, empty data parts;
//   - the TASK-117 strictness deltas (typed invalid_argument): a
//     missing final boundary, a delimiter truncated at EOF, a header
//     line without a colon, a bare-LF header line, a part without
//     Content-Disposition, a Content-Disposition without a name
//     parameter; a duplicate Content-Disposition keeps the FIRST;
//   - the bounded admission contract: raw-byte, part-count, per-part
//     and header-block caps, each exactly-at-cap success and +1
//     typed limit_exceeded, sticky, with the emitted prefix
//     provably stopping at the failing feed;
//   - boundary validation at driver setup: RFC 2046 bchars, length
//     1..256, and boundary extraction from a Content-Type value.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/detail/forms_multipart.hpp>
#include <httpserver/http/outcome.hpp>

#include "./littletest.hpp"

namespace {

namespace http = httpserver::http;
using httpserver::detail::extract_boundary;
using httpserver::detail::multipart_decoder;
using httpserver::detail::multipart_events;
using httpserver::detail::multipart_part_meta;
using httpserver::detail::validate_boundary;

constexpr std::uint64_t default_bytes = 65536;
constexpr std::uint64_t default_parts = 64;
constexpr std::uint64_t default_part_bytes = 65536;
constexpr std::uint64_t default_header_bytes = 8192;

// One decoded part as the recording sink saw it.
struct part_record {
    std::string name;
    std::string filename;
    std::string content_type;
    std::string transfer_encoding;
    std::string data;

    bool operator==(const part_record&) const = default;
};

std::string quote(const std::string& raw) {
    std::string out;
    for (const unsigned char c : raw) {
        if (c >= 0x20 && c < 0x7f) {
            out.push_back(static_cast<char>(c));
        } else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02x", c);
            out += buf;
        }
    }
    return out;
}

std::string render(const std::vector<part_record>& parts) {
    std::string out = "[";
    bool first = true;
    for (const part_record& p : parts) {
        if (!first) out += ", ";
        first = false;
        out += "(" + quote(p.name) + "|" + quote(p.filename) + "|"
            + quote(p.content_type) + "|" + quote(p.transfer_encoding)
            + "|" + quote(p.data) + ")";
    }
    return out + "]";
}

// The pure wire machine's output side, recorded.
class recording_events final : public multipart_events {
 public:
    std::vector<part_record> parts;
    int data_callbacks = 0;

    http::outcome on_part_begin(const multipart_part_meta& m) override {
        parts.push_back(part_record{std::string(m.name),
                                    std::string(m.filename),
                                    std::string(m.content_type),
                                    std::string(m.transfer_encoding),
                                    std::string()});
        return http::outcome::okay();
    }

    http::outcome on_part_data(std::span<const std::byte> data) override {
        ++data_callbacks;
        const char* raw = reinterpret_cast<const char*>(data.data());
        if (!parts.empty()) parts.back().data.append(raw, data.size());
        return http::outcome::okay();
    }

    http::outcome on_part_end() override {
        return http::outcome::okay();
    }
};

struct caps {
    std::uint64_t bytes = default_bytes;
    std::uint64_t parts = default_parts;
    std::uint64_t part_bytes = default_part_bytes;
    std::uint64_t header_bytes = default_header_bytes;
};

std::vector<std::byte> bytes_of(const std::string& s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(std::byte(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string parts_diff(const std::vector<part_record>& got,
                       const std::vector<part_record>& expected) {
    if (got == expected) return "";
    return "got " + render(got) + " want " + render(expected);
}

// "" iff @p got is a prefix-wise match of @p expected: every begun
// part exists with equal metadata, and its data is a prefix of the
// expected data (emission stops where the budget tripped).
std::string prefix_diff(const std::vector<part_record>& got,
                        const std::vector<part_record>& expected) {
    if (got.size() > expected.size()) return "more parts than expected";
    for (std::size_t i = 0; i < got.size(); ++i) {
        const part_record& g = got[i];
        const part_record& w = expected[i];
        const bool meta_eq = g.name == w.name && g.filename == w.filename
            && g.content_type == w.content_type
            && g.transfer_encoding == w.transfer_encoding;
        if (!meta_eq) return "part " + std::to_string(i) + " metadata";
        if (w.data.compare(0, g.data.size(), g.data) != 0) {
            return "part " + std::to_string(i) + " data is not a prefix";
        }
    }
    return "";
}

std::string completed_diff(std::uint64_t got, std::uint64_t want) {
    if (got == want) return "";
    return "parts_completed " + std::to_string(got) + " want "
        + std::to_string(want);
}

// One-shot decode through a fresh decoder; "" iff feed+finish succeed,
// the parts equal @p expected, and parts_completed equals the count.
std::string decodes_to(const std::string& body, const std::string& boundary,
                       const caps& limits,
                       const std::vector<part_record>& expected) {
    recording_events events;
    multipart_decoder d(boundary, limits.bytes, limits.parts,
                        limits.part_bytes, limits.header_bytes, events);
    const http::outcome fed = d.feed(bytes_of(body));
    if (!fed.ok()) return "feed failed: " + fed.message();
    const http::outcome done = d.finish();
    if (!done.ok()) return "finish failed: " + done.message();
    const std::string diff = parts_diff(events.parts, expected);
    if (!diff.empty()) return diff;
    return completed_diff(d.parts_completed(),
                          static_cast<std::uint64_t>(expected.size()));
}

// The same body fed in chunks of @p chunk bytes must decode exactly
// like the one-shot form (the state machine survives every boundary).
std::string segmented_decodes_to(const std::string& body,
                                 const std::string& boundary,
                                 std::size_t chunk, const caps& limits,
                                 const std::vector<part_record>& expected) {
    recording_events events;
    multipart_decoder d(boundary, limits.bytes, limits.parts,
                        limits.part_bytes, limits.header_bytes, events);
    for (std::size_t at = 0; at < body.size(); at += chunk) {
        std::size_t n = chunk;
        if (at + n > body.size()) n = body.size() - at;
        const http::outcome fed =
            d.feed(bytes_of(body.substr(at, n)));
        if (!fed.ok()) {
            return "chunked feed at " + std::to_string(at) + " failed: "
                + fed.message();
        }
    }
    const http::outcome done = d.finish();
    if (!done.ok()) return "chunked finish failed: " + done.message();
    const std::string diff = parts_diff(events.parts, expected);
    if (!diff.empty()) return "chunked: " + diff;
    return completed_diff(d.parts_completed(),
                          static_cast<std::uint64_t>(expected.size()));
}

// Every split point of @p body (feed the first k bytes, then the
// remainder) must decode exactly like the one-shot form.
std::string every_split_decodes_to(const std::string& body,
                                   const std::string& boundary,
                                   const std::vector<part_record>& expected) {
    for (std::size_t k = 0; k <= body.size(); ++k) {
        recording_events events;
        multipart_decoder d(boundary, default_bytes, default_parts,
                            default_part_bytes, default_header_bytes,
                            events);
        const http::outcome first =
            d.feed(bytes_of(body.substr(0, k)));
        if (!first.ok()) {
            return "first feed (k=" + std::to_string(k)
                + ") failed: " + first.message();
        }
        const http::outcome rest = d.feed(bytes_of(body.substr(k)));
        if (!rest.ok()) {
            return "second feed (k=" + std::to_string(k)
                + ") failed: " + rest.message();
        }
        const http::outcome done = d.finish();
        if (!done.ok()) {
            return "finish (k=" + std::to_string(k)
                + ") failed: " + done.message();
        }
        const std::string diff = parts_diff(events.parts, expected);
        if (!diff.empty()) {
            return "split k=" + std::to_string(k) + ": " + diff;
        }
    }
    return "";
}

// The decode must fail with @p code, having emitted exactly @p stored
// (the prefix stops at the failure).
std::string fails_with(const std::string& body, const std::string& boundary,
                       http::outcome_code code, const caps& limits,
                       const std::vector<part_record>& stored) {
    recording_events events;
    multipart_decoder d(boundary, limits.bytes, limits.parts,
                        limits.part_bytes, limits.header_bytes, events);
    const http::outcome fed = d.feed(bytes_of(body));
    const http::outcome observed = fed.ok() ? d.finish() : fed;
    if (observed.ok()) return "decode unexpectedly succeeded";
    if (observed.code() != code) {
        return "failure code " + std::to_string(
                   static_cast<int>(observed.code()))
            + " want " + std::to_string(static_cast<int>(code));
    }
    return parts_diff(events.parts, stored);
}

// "" iff the decode fails typed @p code and every emitted part is a
// prefix of @p expected (byte-cap trips land mid-emission; what was
// emitted is a prefix, never more).
std::string fails_with_prefix(const std::string& body,
                              const std::string& boundary,
                              http::outcome_code code, const caps& limits,
                              const std::vector<part_record>& expected) {
    recording_events events;
    multipart_decoder d(boundary, limits.bytes, limits.parts,
                        limits.part_bytes, limits.header_bytes, events);
    const http::outcome fed = d.feed(bytes_of(body));
    const http::outcome observed = fed.ok() ? d.finish() : fed;
    if (observed.ok()) return "decode unexpectedly succeeded";
    if (observed.code() != code) {
        return "failure code " + std::to_string(
                   static_cast<int>(observed.code()));
    }
    return prefix_diff(events.parts, expected);
}

// "" iff the failure is sticky: later feeds and finish return the same
// typed failure and emit nothing more.
std::string failure_is_sticky(const std::string& body,
                              const std::string& boundary,
                              http::outcome_code code, const caps& limits,
                              const std::vector<part_record>& stored) {
    recording_events events;
    multipart_decoder d(boundary, limits.bytes, limits.parts,
                        limits.part_bytes, limits.header_bytes, events);
    const http::outcome fed = d.feed(bytes_of(body));
    if (fed.ok()) return "feed unexpectedly succeeded";
    if (fed.code() != code) return "first failure code mismatch";
    const std::size_t parts_at_failure = events.parts.size();
    const http::outcome again = d.feed(bytes_of("--x\r\n"));
    if (again.code() != code) return "second feed not sticky";
    const http::outcome done = d.finish();
    if (done.code() != code) return "finish not sticky";
    if (events.parts.size() != parts_at_failure) {
        return "a sticky failure emitted more parts";
    }
    return parts_diff(events.parts, stored);
}

// The multipart_field corpus body (forms.tseq), one field part.
const std::string k_field_body =
    "--PARITY096B\r\n"
    "Content-Disposition: form-data; name=\"note\"\r\n"
    "\r\n"
    "hello\r\n"
    "--PARITY096B--\r\n";

// A field part and a file part, the canonical two-part shape.
const std::string k_two_part_body =
    "--B\r\n"
    "Content-Disposition: form-data; name=\"field1\"\r\n"
    "\r\n"
    "value1\r\n"
    "--B\r\n"
    "Content-Disposition: form-data; name=\"file1\"; filename=\"a.txt\"\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n"
    "file bytes\r\n"
    "--B--\r\n";

std::vector<part_record> field_part() {
    return {part_record{"note", "", "", "", "hello"}};
}

std::vector<part_record> two_parts() {
    return {part_record{"field1", "", "", "", "value1"},
            part_record{"file1", "a.txt", "text/plain", "",
                        "file bytes"}};
}

}  // namespace

LT_BEGIN_SUITE(multipart_decoder_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(multipart_decoder_suite)

// (1) The canonical shapes decode one-shot: the multipart_field corpus
// body and the two-part field+file body.
LT_BEGIN_AUTO_TEST(multipart_decoder_suite, canonical_shapes_decode)
    LT_CHECK(decodes_to(k_field_body, "PARITY096B", caps{},
                        field_part()).empty());
    LT_CHECK(decodes_to(k_two_part_body, "B", caps{}, two_parts()).empty());
    // A leading CRLF before the first delimiter is a legal preamble.
    LT_CHECK(decodes_to("\r\n" + k_two_part_body, "B", caps{},
                        two_parts()).empty());
LT_END_AUTO_TEST(canonical_shapes_decode)

// (2) Every split point of the canonical body decodes identically --
// including splits inside the dashes, the boundary, the final "--",
// the header lines, and the CRLF pairs -- and fixed-size chunking
// (bytes at a time) does too.
LT_BEGIN_AUTO_TEST(multipart_decoder_suite, every_split_decodes)
    LT_CHECK(every_split_decodes_to(k_two_part_body, "B",
                                    two_parts()).empty());
    for (std::size_t chunk = 1; chunk <= 9; ++chunk) {
        const std::string diff =
            segmented_decodes_to(k_two_part_body, "B", chunk, caps{},
                                 two_parts());
        if (!diff.empty()) {
            std::cerr << "[chunk " << chunk << "] " << diff << std::endl;
        }
        LT_CHECK(diff.empty());
    }
LT_END_AUTO_TEST(every_split_decodes)

// (3) Preamble junk is drained (never stored, never emitted) and an
// epilogue after the final boundary is tolerated; transport padding
// before a part-delimiter CRLF and after the final "--" is skipped.
LT_BEGIN_AUTO_TEST(multipart_decoder_suite, preamble_and_epilogue)
    const std::string padded =
        "preamble junk with --B lookalikes\r\nmore\r\n"
        + k_two_part_body + "epilogue bytes anything at all";
    // The preamble's "--B lookalikes" line is NOT a delimiter (the
    // boundary is followed by junk), so decoding starts at the real
    // first delimiter.
    LT_CHECK(decodes_to(padded, "B", caps{}, two_parts()).empty());

    const std::vector<part_record> padded_parts{
        part_record{"n", "", "", "", "v"},
        part_record{"m", "", "", "", "w"}};
    const std::string transport_pad =
        "--B\r\n"
        "Content-Disposition: form-data; name=\"n\"\r\n"
        "\r\n"
        "v\r\n--B \t\r\n"
        "Content-Disposition: form-data; name=\"m\"\r\n"
        "\r\n"
        "w\r\n--B--  \r\nepilogue";
    LT_CHECK(decodes_to(transport_pad, "B", caps{}, padded_parts).empty());
    LT_CHECK(every_split_decodes_to(transport_pad, "B",
                                    padded_parts).empty());
LT_END_AUTO_TEST(preamble_and_epilogue)

// (4) Delimiter lookalikes inside part data are DATA: a CRLF plus a
// boundary prefix, a CRLF plus the boundary plus junk, and a bare LF
// all pass through; a case-mangled boundary is never a delimiter (the
// match is case-sensitive, RFC 2046).
LT_BEGIN_AUTO_TEST(multipart_decoder_suite, lookalikes_are_data)
    const std::string body =
        "--B\r\n"
        "Content-Disposition: form-data; name=\"d\"\r\n"
        "\r\n"
        "a\r\n-b\r\n"
        "b\r\n--Bx\r\n"
        "c\r\n--b\r\n"
        "d\nbare lf\r\n"
        "--B--\r\n";
    // The trailing CRLF before the real delimiter belongs to the
    // delimiter (RFC 2046: delimiter := CRLF "--" boundary).
    const std::string expected_data =
        "a\r\n-b\r\n"
        "b\r\n--Bx\r\n"
        "c\r\n--b\r\n"
        "d\nbare lf";
    const std::vector<part_record> expected{
        part_record{"d", "", "", "", expected_data}};
    LT_CHECK(decodes_to(body, "B", caps{}, expected).empty());
    LT_CHECK(every_split_decodes_to(body, "B", expected).empty());
LT_END_AUTO_TEST(lookalikes_are_data)

// (5) Content-Disposition shapes: a quoted filename with backslash
// escapes, name-only parts, case-insensitive parameter names, per-part
// Content-Type capture, and empty data parts.
LT_BEGIN_AUTO_TEST(multipart_decoder_suite, disposition_shapes)
    const std::vector<part_record> expected{
        part_record{"up", "a\"b\\c.txt", "", "", "payload"},
        part_record{"token", "", "application/json",
                    "quoted-printable", ""}};
    const std::string body =
        "--B\r\n"
        "Content-Disposition: form-data; NAME=\"up\"; FILENAME=\"a\\\"b\\\\c.txt\"\r\n"
        "\r\n"
        "payload\r\n"
        "--B\r\n"
        "Content-Disposition: form-data; name=token\r\n"
        "Content-Type: application/json\r\n"
        "Transfer-Encoding: quoted-printable\r\n"
        "\r\n"
        "\r\n"
        "--B--\r\n";
    LT_CHECK(decodes_to(body, "B", caps{}, expected).empty());
    LT_CHECK(every_split_decodes_to(body, "B", expected).empty());
LT_END_AUTO_TEST(disposition_shapes)

// (6) The strictness deltas: a missing final boundary, a delimiter
// truncated at EOF, a header line without a colon, a bare-LF header
// line, a part without Content-Disposition, and a Content-Disposition
// without a name parameter all fail typed invalid_argument; a
// duplicate Content-Disposition keeps the FIRST.
LT_BEGIN_AUTO_TEST(multipart_decoder_suite, strict_rejections)
    // Missing final boundary: the body ends in part_body with nothing
    // decidable left (the trailing bytes sit inside the holdback
    // window, so they were never emitted).
    LT_CHECK(fails_with(
        "--B\r\nContent-Disposition: form-data; name=\"n\"\r\n\r\nv",
        "B", http::outcome_code::invalid_argument, caps{},
        {part_record{"n", "", "", "", ""}}).empty());
    // Delimiter truncated at EOF: the holdback released the data before
    // the undecided candidate, so "v" was emitted.
    LT_CHECK(fails_with(
        "--B\r\nContent-Disposition: form-data; name=\"n\"\r\n\r\nv\r\n--B",
        "B", http::outcome_code::invalid_argument, caps{},
        {part_record{"n", "", "", "", "v"}}).empty());
    // Header line without a colon.
    LT_CHECK(fails_with(
        "--B\r\nNoColonHere\r\n\r\nv\r\n--B--\r\n", "B",
        http::outcome_code::invalid_argument, caps{}, {}).empty());
    // A bare-LF header line terminator inside the part's header block.
    LT_CHECK(fails_with(
        "--B\r\nContent-Disposition: form-data; name=\"n\"\n\r\nv\r\n--B--\r\n",
        "B", http::outcome_code::invalid_argument, caps{}, {}).empty());
    // Part without Content-Disposition.
    LT_CHECK(fails_with(
        "--B\r\nContent-Type: text/plain\r\n\r\nv\r\n--B--\r\n", "B",
        http::outcome_code::invalid_argument, caps{}, {}).empty());
    // Content-Disposition without a name parameter.
    LT_CHECK(fails_with(
        "--B\r\nContent-Disposition: form-data; filename=\"a\"\r\n\r\nv\r\n--B--\r\n",
        "B", http::outcome_code::invalid_argument, caps{}, {}).empty());
    // A non-empty preamble-only body never reaches a first delimiter.
    LT_CHECK(fails_with("junk only, no delimiter", "B",
                        http::outcome_code::invalid_argument, caps{},
                        {}).empty());
    // A duplicate Content-Disposition: the first one wins.
    LT_CHECK(decodes_to(
        "--B\r\n"
        "Content-Disposition: form-data; name=\"first\"\r\n"
        "Content-Disposition: form-data; name=\"second\"\r\n"
        "\r\nv\r\n--B--\r\n",
        "B", caps{}, {part_record{"first", "", "", "", "v"}}).empty());
LT_END_AUTO_TEST(strict_rejections)

// (7) The caps: raw body bytes, part count, per-part bytes, and
// header-block bytes. Exactly-at-cap succeeds; one past fails typed
// limit_exceeded, sticky, with the emitted prefix stopping at the
// failing feed.
LT_BEGIN_AUTO_TEST(multipart_decoder_suite, caps_boundary)
    // Raw body cap: exactly-at-cap decodes; one byte less (the final
    // CRLF of the closing delimiter lives in the epilogue drain) fails
    // typed with both complete parts emitted.
    const std::uint64_t whole =
        static_cast<std::uint64_t>(k_two_part_body.size());
    LT_CHECK(decodes_to(k_two_part_body, "B", caps{whole},
                        two_parts()).empty());
    LT_CHECK(fails_with(k_two_part_body, "B",
                        http::outcome_code::limit_exceeded,
                        caps{whole - 1}, two_parts()).empty());
    // A cap in the middle of part data: the failure is typed and the
    // emission provably stops (prefix, never more).
    LT_CHECK(fails_with_prefix(
        k_two_part_body, "B", http::outcome_code::limit_exceeded,
        caps{55}, two_parts()).empty());

    // Part-count cap: two parts at cap 3 succeed; the third part's
    // begin fails, leaving the first two complete.
    const std::string three_parts =
        "--B\r\nContent-Disposition: form-data; name=\"a\"\r\n\r\n1\r\n"
        "--B\r\nContent-Disposition: form-data; name=\"b\"\r\n\r\n2\r\n"
        "--B\r\nContent-Disposition: form-data; name=\"c\"\r\n\r\n3\r\n"
        "--B--\r\n";
    const std::vector<part_record> first_two{
        part_record{"a", "", "", "", "1"},
        part_record{"b", "", "", "", "2"}};
    const std::vector<part_record> all_three{
        part_record{"a", "", "", "", "1"},
        part_record{"b", "", "", "", "2"},
        part_record{"c", "", "", "", "3"}};
    LT_CHECK(decodes_to(three_parts, "B",
                        caps{default_bytes, 3}, all_three).empty());
    LT_CHECK(fails_with(three_parts, "B",
                        http::outcome_code::limit_exceeded,
                        caps{default_bytes, 2}, first_two).empty());

    // Per-part byte cap: exactly-at-cap succeeds; a tighter one fails
    // typed with the emission a strict prefix.
    LT_CHECK(decodes_to(
        "--B\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\n123456\r\n--B--\r\n",
        "B", caps{default_bytes, default_parts, 6},
        {part_record{"f", "", "", "", "123456"}}).empty());
    LT_CHECK(fails_with_prefix(
        k_two_part_body, "B", http::outcome_code::limit_exceeded,
        caps{default_bytes, default_parts, 7}, two_parts()).empty());

    // Header-block cap: exactly-at-cap succeeds; one less fails (the
    // blank line trips it, so the part never begins).
    const std::string one_field =
        "--B\r\nContent-Disposition: form-data; name=\"f\"\r\n\r\nv\r\n--B--\r\n";
    // "Content-Disposition: form-data; name=\"f\"" is 40 bytes; with
    // its CRLF (42) plus the blank line's CRLF (2) the block is 44.
    LT_CHECK(decodes_to(one_field, "B",
                        caps{default_bytes, default_parts,
                             default_part_bytes, 44},
                        {part_record{"f", "", "", "", "v"}}).empty());
    LT_CHECK(fails_with(one_field, "B",
                        http::outcome_code::limit_exceeded,
                        caps{default_bytes, default_parts,
                             default_part_bytes, 43},
                        {}).empty());
    // A header line that can never terminate within the block cap
    // fails even before the line's CRLF arrives.
    LT_CHECK(fails_with(
        "--B\r\nContent-Disposition: form-data; name=\"f\"\r\nX-Pad: "
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "B", http::outcome_code::limit_exceeded,
        caps{default_bytes, default_parts, default_part_bytes, 64},
        {}).empty());

    // Stickiness: the over-cap failure repeats across later feeds and
    // finish, emitting nothing more.
    LT_CHECK(failure_is_sticky(
        three_parts, "B", http::outcome_code::limit_exceeded,
        caps{default_bytes, 2}, first_two).empty());
LT_END_AUTO_TEST(caps_boundary)

// (8) Boundary validation and extraction at driver setup: RFC 2046
// bchars, length 1..256, boundary= in token and quoted forms.
LT_BEGIN_AUTO_TEST(multipart_decoder_suite, boundary_validation)
    LT_CHECK(validate_boundary("B").ok());
    LT_CHECK(validate_boundary("PARITY096B-_'.()+,:=?").ok());
    LT_CHECK(validate_boundary("has space inside").ok());
    LT_CHECK(!validate_boundary("").ok());
    LT_CHECK(!validate_boundary(std::string(257, 'a')).ok());
    LT_CHECK(!validate_boundary("bad boundary!").ok());
    LT_CHECK(!validate_boundary("ends with space ").ok());

    std::string boundary;
    LT_CHECK(extract_boundary(
        std::optional<std::string_view>(
            "multipart/form-data; boundary=PARITY096B"),
        boundary).ok());
    LT_CHECK(boundary == "PARITY096B");
    LT_CHECK(extract_boundary(
        std::optional<std::string_view>(
            "MULTIPART/FORM-DATA; BOUNDARY=\"quo ted\\-x\""),
        boundary).ok());
    LT_CHECK(boundary == "quo ted-x");
    LT_CHECK(extract_boundary(std::optional<std::string_view>(
                                  "multipart/form-data"),
                              boundary).code()
             == http::outcome_code::invalid_argument);
    LT_CHECK(extract_boundary(
        std::optional<std::string_view>(
            "multipart/form-data; boundary=bad!charset"),
        boundary).code()
             == http::outcome_code::invalid_argument);
    LT_CHECK(extract_boundary(
        std::optional<std::string_view>("text/plain"), boundary).code()
             == http::outcome_code::invalid_argument);
    LT_CHECK(extract_boundary(std::optional<std::string_view>(), boundary)
                 .code()
             == http::outcome_code::invalid_argument);
LT_END_AUTO_TEST(boundary_validation)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
