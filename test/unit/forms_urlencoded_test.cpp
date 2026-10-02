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

// TASK-116 step 2: the urlencoded form vocabulary, the one-shot
// decode_urlencoded, and the form_read rejection vocabulary
// (PRD-V3N-REQ-021/022, DR-V3-001). The members live in the library
// (detail/forms_urlencoded.cpp in v3core), so this suite links
// libhttpserver.la (the basic_auth_policy convention). The suite pins:
//   - decode_urlencoded's within-cap v2 semantics: arrival order,
//     repeated names appended, first-value lookup, decode-only
//     translation (+ and %HH; raw bytes verbatim);
//   - the typed verdicts: a malformed escape fails invalid_argument
//     and leaves the caller's form_fields untouched; byte and field
//     caps fail limit_exceeded; exactly-at-cap succeeds;
//   - the urlencoded_limits factory (both caps must be at least 1)
//     and the v2-mirroring defaults (65536 bytes, 64 fields);
//   - form_read's ready-made rejection vocabulary: limit_exceeded
//     answers 413, a malformed body 400, transport failures carry an
//     invalid status (nothing to commit), and the rejection fields
//     frame an explicitly empty body (v2 parity).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/forms/urlencoded.hpp>
#include <httpserver/http/outcome.hpp>

#include "./littletest.hpp"

namespace {

namespace forms = httpserver::forms;
namespace http = httpserver::http;

using entry = std::pair<std::string, std::string>;

std::vector<std::byte> bytes_of(const std::string& s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(std::byte(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string render(const std::vector<entry>& fields) {
    std::string out = "[";
    bool first = true;
    for (const entry& e : fields) {
        if (!first) out += ", ";
        first = false;
        out += "(" + e.first + "," + e.second + ")";
    }
    return out + "]";
}

// "" iff the decoded entries equal @p expected (order and repeats
// included).
std::string entries_diff(const forms::form_fields& got,
                         const std::vector<entry>& expected) {
    const std::vector<entry> observed = got.entries();
    if (observed == expected) return "";
    return "got " + render(observed) + " want " + render(expected);
}

// "" iff the one-shot decode succeeds and the entries equal @p expected.
std::string decodes_to(const std::string& body,
                       const forms::urlencoded_limits& limits,
                       const std::vector<entry>& expected) {
    forms::form_fields out;
    const http::outcome decoded =
        forms::decode_urlencoded(bytes_of(body), limits, out);
    if (!decoded.ok()) return "decode failed: " + decoded.message();
    return entries_diff(out, expected);
}

// "" iff the one-shot decode fails with @p code.
std::string fails_with(const std::string& body,
                       const forms::urlencoded_limits& limits,
                       http::outcome_code code) {
    forms::form_fields out;
    const http::outcome decoded =
        forms::decode_urlencoded(bytes_of(body), limits, out);
    if (decoded.ok()) return "decode unexpectedly succeeded";
    if (decoded.code() != code) {
        return "failure code " + std::to_string(
                   static_cast<int>(decoded.code()))
            + " want " + std::to_string(static_cast<int>(code));
    }
    return "";
}

// "" iff the factory verdict matches @p want_ok (and, on success, the
// values landed).
std::string create_diff(std::uint64_t bytes_cap, std::uint64_t fields_cap,
                        bool want_ok) {
    forms::urlencoded_limits out;
    const http::outcome created =
        forms::urlencoded_limits::create(bytes_cap, fields_cap, out);
    if (created.ok() != want_ok) {
        return "create(" + std::to_string(bytes_cap) + ", "
            + std::to_string(fields_cap) + ") ok=" +
            (created.ok() ? "true" : "false");
    }
    if (want_ok && (out.max_total_bytes != bytes_cap
                    || out.max_fields != fields_cap)) {
        return "values did not land";
    }
    return "";
}

// "" iff form_read over an outcome with @p code answers @p want (0:
// the status is invalid, nothing to commit).
std::string reject_status_diff(http::outcome_code code,
                               std::uint16_t want) {
    forms::form_read read;
    read.status = http::outcome(code, "probe");
    const http::status answered = read.reject_status();
    if (!answered.valid() && want == 0) return "";
    if (answered.valid() && answered.code() == want) return "";
    return "reject_status over code " + std::to_string(
               static_cast<int>(code))
        + " answered " + std::to_string(answered.code());
}

// "" iff the rejection fields are exactly one Content-Length: 0.
std::string reject_fields_diff() {
    forms::form_read read;
    read.status = http::outcome(http::outcome_code::invalid_argument,
                                "probe");
    const http::fields fields = read.reject_fields();
    if (fields.size() != 1) {
        return "expected one field, got " + std::to_string(fields.size());
    }
    if (fields.first("content-length").value_or("") != "0") {
        return "Content-Length is not 0";
    }
    return "";
}

}  // namespace

LT_BEGIN_SUITE(forms_urlencoded_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(forms_urlencoded_suite)

// (1) Within-cap v2 semantics: order, repeats, first-value lookup.
LT_BEGIN_AUTO_TEST(forms_urlencoded_suite, decode_preserves_v2_semantics)
    const forms::urlencoded_limits limits;
    LT_CHECK(decodes_to("a=1&b=two&k=1&k=2", limits,
                        {entry("a", "1"), entry("b", "two"),
                         entry("k", "1"), entry("k", "2")}).empty());

    forms::form_fields out;
    LT_CHECK(forms::decode_urlencoded(
                 bytes_of("a=1&b=two&k=1&k=2"), limits, out).ok());
    LT_CHECK(out.size() == static_cast<std::size_t>(4));
    // First-occurrence lookup: v2 get_arg_flat semantics.
    LT_CHECK(out.value("k").value_or("") == "1");
    LT_CHECK(out.value("b").value_or("") == "two");
    LT_CHECK(!out.value("missing").has_value());
    // Every value in arrival order: v2 get_arg(key) semantics.
    const std::vector<std::string_view> repeated = out.all("k");
    LT_CHECK(repeated.size() == static_cast<std::size_t>(2));
    LT_CHECK(repeated.size() < 1 || repeated[0] == "1");
    LT_CHECK(repeated.size() < 2 || repeated[1] == "2");
    LT_CHECK(out.all("missing").empty());
LT_END_AUTO_TEST(decode_preserves_v2_semantics)

// (2) Translation is decode-only: '+' is 0x20, %HH is the byte, every
// other raw byte (NUL and high-bit included) is verbatim.
LT_BEGIN_AUTO_TEST(forms_urlencoded_suite, decode_translates_only_forms)
    const forms::urlencoded_limits limits;
    LT_CHECK(decodes_to("a=b+c&x=%2F", limits,
                        {entry("a", "b c"), entry("x", "/")}).empty());
    const std::string nul_body("n=%00");
    LT_CHECK(decodes_to(nul_body, limits,
                        {entry("n", std::string(1, '\0'))}).empty());
    std::string raw(1, '\xfe');
    raw += "=v";
    LT_CHECK(decodes_to(raw, limits,
                        {entry(std::string(1, '\xfe'), "v")}).empty());
LT_END_AUTO_TEST(decode_translates_only_forms)

// (3) Typed verdicts: malformed escapes fail invalid_argument and
// leave the caller's fields untouched; both caps fail limit_exceeded
// exactly past the boundary.
LT_BEGIN_AUTO_TEST(forms_urlencoded_suite, decode_verdicts_are_typed)
    const forms::urlencoded_limits limits;
    LT_CHECK(fails_with("a=%G1", limits,
                        http::outcome_code::invalid_argument).empty());
    LT_CHECK(fails_with("a=1&b=%2", limits,
                        http::outcome_code::invalid_argument).empty());

    forms::form_fields prior;
    prior = forms::form_fields({entry("prior", "kept")});
    const http::outcome failed = forms::decode_urlencoded(
        bytes_of("a=%G1"), limits, prior);
    LT_CHECK(!failed.ok());
    LT_CHECK(failed.code() == http::outcome_code::invalid_argument);
    // Rejection precedes storage: the caller's fields are untouched.
    LT_CHECK(entries_diff(prior, {entry("prior", "kept")}).empty());

    const forms::urlencoded_limits byte_capped{5, 64};
    LT_CHECK(decodes_to("a=1&b", byte_capped,
                        {entry("a", "1"), entry("b", "")}).empty());
    LT_CHECK(fails_with("a=1&b=", byte_capped,
                        http::outcome_code::limit_exceeded).empty());

    const forms::urlencoded_limits field_capped{65536, 2};
    LT_CHECK(decodes_to("a=1&b=2", field_capped,
                        {entry("a", "1"), entry("b", "2")}).empty());
    LT_CHECK(fails_with("a=1&b=2&c=3", field_capped,
                        http::outcome_code::limit_exceeded).empty());
LT_END_AUTO_TEST(decode_verdicts_are_typed)

// (4) The limits factory: both caps must be at least 1; the defaults
// mirror v2's GET-arg defaults (65536 bytes, 64 fields).
LT_BEGIN_AUTO_TEST(forms_urlencoded_suite, limits_factory_and_defaults)
    LT_CHECK(create_diff(0, 64, false).empty());
    LT_CHECK(create_diff(65536, 0, false).empty());
    LT_CHECK(create_diff(1, 1, true).empty());
    LT_CHECK(create_diff(65536, 64, true).empty());
    const forms::urlencoded_limits defaults;
    LT_CHECK(defaults.max_total_bytes
             == static_cast<std::uint64_t>(65536));
    LT_CHECK(defaults.max_fields == static_cast<std::uint64_t>(64));
LT_END_AUTO_TEST(limits_factory_and_defaults)

// (5) The form_read rejection vocabulary: 413 for a cap, 400 for a
// malformed body, an invalid status for transport failures (nothing
// to commit), and Content-Length: 0 rejection fields.
LT_BEGIN_AUTO_TEST(forms_urlencoded_suite, form_read_rejection_vocabulary)
    LT_CHECK(reject_status_diff(
                 http::outcome_code::limit_exceeded, 413).empty());
    LT_CHECK(reject_status_diff(
                 http::outcome_code::invalid_argument, 400).empty());
    LT_CHECK(reject_status_diff(http::outcome_code::ok, 0).empty());
    LT_CHECK(reject_status_diff(
                 http::outcome_code::connection_closed, 0).empty());
    LT_CHECK(reject_fields_diff().empty());

    forms::form_read read;
    read.status = http::outcome(http::outcome_code::ok, "");
    LT_CHECK(read.ok());
    read.status = http::outcome(
        http::outcome_code::invalid_argument, "malformed");
    LT_CHECK(!read.ok());
LT_END_AUTO_TEST(form_read_rejection_vocabulary)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
