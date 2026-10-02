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

// TASK-117 step 2: the multipart form vocabulary, the one-shot
// decode_multipart, and the multipart_read rejection vocabulary
// (PRD-V3N-REQ-021/025, DR-V3-001). The members live in the library
// (detail/forms_multipart.cpp in v3core), so this suite links
// libhttpserver.la (the basic_auth_policy convention). The suite
// pins:
//   - the limits factory (every cap at least 1) and the
//     v2-arg-budget defaults;
//   - decode_multipart's happy path: parts reach the sink in arrival
//     order with the parsed identity, repeated names included, and
//     the verdict counts the completed parts;
//   - the documented hooks: on_part_abort fires EXACTLY ONCE for the
//     part that began but did not end (mid-part malformed input,
//     mid-part over-cap) and NEVER on a clean decode or on a
//     before-any-part boundary failure;
//   - the typed verdicts: a malformed body or an unusable
//     Content-Type fails invalid_argument and leaves the caller's
//     multipart_read untouched; the caps fail limit_exceeded;
//   - multipart_read's ready-made rejection vocabulary: 413 for a
//     cap, 400 for a malformed body, an invalid status for transport
//     failures (nothing to commit), and Content-Length: 0 rejection
//     fields;
//   - a sink-returned failure is sticky and propagates as the
//     decode's terminal (with the abort).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/forms/multipart.hpp>
#include <httpserver/http/outcome.hpp>

#include "./littletest.hpp"

namespace {

namespace forms = httpserver::forms;
namespace http = httpserver::http;

// What one test sink observed.
struct sink_log {
    int begins = 0;
    int datas = 0;
    int ends = 0;
    int aborts = 0;
    std::vector<std::string> begun;     // "name|filename|type" per begin
    std::vector<std::string> data;      // one entry per data callback
    http::outcome abort_reason;
};

// A part_sink that records everything it sees.
class recording_sink final : public forms::part_sink {
 public:
    explicit recording_sink(sink_log& log) : log_(log) { }

    http::outcome on_part_begin(
        const forms::part_descriptor& part) override {
        ++log_.begins;
        log_.begun.push_back(std::string(part.name) + "|"
                             + std::string(part.filename) + "|"
                             + std::string(part.content_type));
        return http::outcome::okay();
    }

    http::outcome on_part_data(std::span<const std::byte> data) override {
        ++log_.datas;
        const char* raw = reinterpret_cast<const char*>(data.data());
        log_.data.push_back(std::string(raw, data.size()));
        return http::outcome::okay();
    }

    http::outcome on_part_end() override {
        ++log_.ends;
        return http::outcome::okay();
    }

    void on_part_abort(http::outcome reason) override {
        ++log_.aborts;
        log_.abort_reason = reason;
    }

 private:
    sink_log& log_;
};

// A part_sink whose data callback fails from its n-th call on.
class failing_sink final : public forms::part_sink {
 public:
    http::outcome on_part_begin(
        const forms::part_descriptor&) override {
        return http::outcome::okay();
    }

    http::outcome on_part_data(std::span<const std::byte>) override {
        return http::outcome(
            http::outcome_code::protocol_error, "sink is full");
    }

    http::outcome on_part_end() override {
        return http::outcome::okay();
    }

    void on_part_abort(http::outcome reason) override {
        ++aborts;
        abort_reason = reason;
    }

    int aborts = 0;
    http::outcome abort_reason;
};

std::vector<std::byte> bytes_of(const std::string& s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(std::byte(static_cast<unsigned char>(c)));
    }
    return out;
}

// The multipart_field corpus body and the canonical two-part body.
const char* k_type = "multipart/form-data; boundary=PARITY096B";

std::string field_body() {
    return "--PARITY096B\r\n"
           "Content-Disposition: form-data; name=\"note\"\r\n"
           "\r\n"
           "hello\r\n"
           "--PARITY096B--\r\n";
}

std::string two_part_body() {
    return "--PARITY096B\r\n"
           "Content-Disposition: form-data; name=\"a\"\r\n"
           "\r\n"
           "1\r\n"
           "--PARITY096B\r\n"
           "Content-Disposition: form-data; name=\"file\"; "
           "filename=\"f.bin\"\r\n"
           "Content-Type: application/octet-stream\r\n"
           "\r\n"
           "bytes\r\n"
           "--PARITY096B--\r\n";
}

// "" iff the one-shot decode succeeded and @p check saw the expected
// event counts and verdict.
std::string decodes_to(const std::string& body, const char* type,
                       const forms::multipart_limits& limits,
                       int begins, int ends,
                       std::uint64_t parts_completed) {
    sink_log log;
    recording_sink sink(log);
    forms::multipart_read read;
    const http::outcome decoded = forms::decode_multipart(
        bytes_of(body),
        type == nullptr ? std::optional<std::string_view>()
                        : std::optional<std::string_view>(type),
        limits, sink, read);
    if (!decoded.ok()) return "decode failed: " + decoded.message();
    if (log.begins != begins || log.ends != ends) {
        return "begins " + std::to_string(log.begins) + " ends "
            + std::to_string(log.ends);
    }
    if (log.aborts != 0) return "unexpected aborts";
    if (read.parts_completed != parts_completed) {
        return "parts_completed " + std::to_string(read.parts_completed);
    }
    return "";
}

// "" iff the one-shot decode failed with @p code, the sink aborted
// exactly @p aborts times, and the caller's verdict is untouched.
std::string fails_with(const std::string& body, const char* type,
                       const forms::multipart_limits& limits,
                       http::outcome_code code, int aborts) {
    sink_log log;
    recording_sink sink(log);
    forms::multipart_read read;
    read.status = http::outcome(
        http::outcome_code::would_deadlock, "sentinel");
    read.parts_completed = 77;
    const http::outcome decoded = forms::decode_multipart(
        bytes_of(body),
        type == nullptr ? std::optional<std::string_view>()
                        : std::optional<std::string_view>(type),
        limits, sink, read);
    if (decoded.ok()) return "decode unexpectedly succeeded";
    if (decoded.code() != code) {
        return "failure code " + std::to_string(
                   static_cast<int>(decoded.code()))
            + " want " + std::to_string(static_cast<int>(code));
    }
    if (log.aborts != aborts) {
        return "aborts " + std::to_string(log.aborts) + " want "
            + std::to_string(aborts);
    }
    if (read.status.code() != http::outcome_code::would_deadlock
            || read.parts_completed != 77) {
        return "a rejected decode touched the caller's verdict";
    }
    return "";
}

// "" iff the factory verdict matches @p want_ok (and, on success, the
// values landed).
std::string create_diff(std::uint64_t bytes_cap, std::uint64_t parts_cap,
                        std::uint64_t part_bytes_cap,
                        std::uint64_t header_bytes_cap, bool want_ok) {
    forms::multipart_limits out;
    const http::outcome created = forms::multipart_limits::create(
        bytes_cap, parts_cap, part_bytes_cap, header_bytes_cap, out);
    if (created.ok() != want_ok) {
        return "create ok=" + std::string(created.ok() ? "true" : "false");
    }
    if (want_ok && (out.max_total_bytes != bytes_cap
                    || out.max_parts != parts_cap
                    || out.max_part_bytes != part_bytes_cap
                    || out.max_part_header_bytes != header_bytes_cap)) {
        return "values did not land";
    }
    return "";
}

// "" iff multipart_read over an outcome with @p code answers @p want
// (0: the status is invalid, nothing to commit).
std::string reject_status_diff(http::outcome_code code,
                               std::uint16_t want) {
    forms::multipart_read read;
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
    forms::multipart_read read;
    read.status = http::outcome(http::outcome_code::limit_exceeded,
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

LT_BEGIN_SUITE(forms_multipart_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(forms_multipart_suite)

// (1) Happy path: parts reach the sink in arrival order with the
// parsed identity, repeated names included, and the verdict counts
// the completed parts.
LT_BEGIN_AUTO_TEST(forms_multipart_suite, decode_drives_the_sink)
    sink_log log;
    recording_sink sink(log);
    forms::multipart_read read;
    const http::outcome decoded = forms::decode_multipart(
        bytes_of(two_part_body()), k_type, forms::multipart_limits{},
        sink, read);
    LT_CHECK(decoded.ok());
    LT_CHECK_EQ(log.begins, 2);
    LT_CHECK_EQ(log.ends, 2);
    LT_CHECK_EQ(log.aborts, 0);
    LT_CHECK_EQ(log.begun.size(), static_cast<std::size_t>(2));
    if (log.begun.size() == 2) {
        LT_CHECK(log.begun[0] == "a||");
        LT_CHECK(log.begun[1]
                 == "file|f.bin|application/octet-stream");
    }
    // The two parts' data, each exactly what the framer emitted.
    LT_CHECK_EQ(log.data.size(), static_cast<std::size_t>(2));
    if (log.data.size() == 2) {
        LT_CHECK(log.data[0] == "1");
        LT_CHECK(log.data[1] == "bytes");
    }
    LT_CHECK(read.ok());
    LT_CHECK_EQ(read.parts_completed, static_cast<std::uint64_t>(2));

    LT_CHECK(decodes_to(field_body(), k_type,
                        forms::multipart_limits{}, 1, 1, 1).empty());
    // An empty body under a multipart type: zero parts, clean verdict.
    LT_CHECK(decodes_to("", k_type, forms::multipart_limits{}, 0, 0,
                        0).empty());
LT_END_AUTO_TEST(decode_drives_the_sink)

// (2) The documented hooks: on_part_abort fires EXACTLY ONCE for the
// part that began but did not end -- a malformed body and an over-cap
// body both trip mid-part -- and NEVER on a clean decode or on a
// before-any-part boundary failure.
LT_BEGIN_AUTO_TEST(forms_multipart_suite, abort_fires_exactly_once)
    // Mid-part malformed (missing final boundary): one abort carrying
    // the typed rejection.
    {
        sink_log log;
        recording_sink sink(log);
        forms::multipart_read read;
        const http::outcome decoded = forms::decode_multipart(
            bytes_of("--PARITY096B\r\n"
                     "Content-Disposition: form-data; name=\"n\"\r\n"
                     "\r\npartial data"),
            k_type, forms::multipart_limits{}, sink, read);
        LT_CHECK(!decoded.ok());
        LT_CHECK_EQ(log.aborts, 1);
        LT_CHECK(log.abort_reason.code()
                                 == http::outcome_code::invalid_argument);
        LT_CHECK_EQ(log.begins, 1);
        LT_CHECK_EQ(log.ends, 0);
    }
    // Mid-part over-cap: one abort carrying the typed limit.
    {
        sink_log log;
        recording_sink sink(log);
        forms::multipart_read read;
        const http::outcome decoded = forms::decode_multipart(
            bytes_of(field_body()), k_type,
            forms::multipart_limits{65536, 64, 3, 8192}, sink, read);
        LT_CHECK(!decoded.ok());
        LT_CHECK_EQ(log.aborts, 1);
        LT_CHECK(log.abort_reason.code()
                                 == http::outcome_code::limit_exceeded);
    }
    // A boundary problem before any part began: nothing to abort.
    LT_CHECK(fails_with(field_body(), "multipart/form-data",
                        forms::multipart_limits{},
                        http::outcome_code::invalid_argument, 0).empty());
    LT_CHECK(fails_with(field_body(), nullptr,
                        forms::multipart_limits{},
                        http::outcome_code::invalid_argument, 0).empty());
    LT_CHECK(fails_with(
        field_body(), "multipart/form-data; boundary=bad!charset",
        forms::multipart_limits{},
        http::outcome_code::invalid_argument, 0).empty());
LT_END_AUTO_TEST(abort_fires_exactly_once)

// (3) Typed verdicts: malformed bodies and unusable Content-Types
// fail invalid_argument; the caps fail limit_exceeded; a rejection
// never touches the caller's verdict.
LT_BEGIN_AUTO_TEST(forms_multipart_suite, decode_verdicts_are_typed)
    const forms::multipart_limits limits;
    LT_CHECK(fails_with("garbage, never a delimiter", k_type, limits,
                        http::outcome_code::invalid_argument, 0)
                 .empty());
    LT_CHECK(fails_with(
        "--PARITY096B\r\nBadHeader\r\n\r\nx\r\n--PARITY096B--\r\n",
        k_type, limits, http::outcome_code::invalid_argument, 0)
                 .empty());
    LT_CHECK(fails_with(
        "--PARITY096B\r\nContent-Disposition: form-data; name=\"n\"\r\n"
        "\r\n0123456789A\r\n--PARITY096B--\r\n",
        k_type, forms::multipart_limits{65536, 64, 10, 8192},
        http::outcome_code::limit_exceeded, 1).empty());
    LT_CHECK(fails_with(field_body(), k_type,
                        forms::multipart_limits{83, 64, 65536, 8192},
                        http::outcome_code::limit_exceeded, 0)
                 .empty());
    // Exactly-at-cap succeeds: the field body is 84 bytes.
    LT_CHECK(decodes_to(field_body(), k_type,
                        forms::multipart_limits{84, 64, 65536, 8192},
                        1, 1, 1).empty());
LT_END_AUTO_TEST(decode_verdicts_are_typed)

// (4) The limits factory: every cap must be at least 1; the defaults
// mirror v2's argument budgets (65536 bytes, 64 parts).
LT_BEGIN_AUTO_TEST(forms_multipart_suite, limits_factory_and_defaults)
    LT_CHECK(create_diff(0, 64, 65536, 8192, false).empty());
    LT_CHECK(create_diff(65536, 0, 65536, 8192, false).empty());
    LT_CHECK(create_diff(65536, 64, 0, 8192, false).empty());
    LT_CHECK(create_diff(65536, 64, 65536, 0, false).empty());
    LT_CHECK(create_diff(1, 1, 1, 1, true).empty());
    LT_CHECK(create_diff(65536, 64, 65536, 8192, true).empty());
    const forms::multipart_limits defaults;
    LT_CHECK(defaults.max_total_bytes
             == static_cast<std::uint64_t>(65536));
    LT_CHECK(defaults.max_parts == static_cast<std::uint64_t>(64));
    LT_CHECK(defaults.max_part_bytes
             == static_cast<std::uint64_t>(65536));
    LT_CHECK(defaults.max_part_header_bytes
             == static_cast<std::uint64_t>(8192));
LT_END_AUTO_TEST(limits_factory_and_defaults)

// (5) The multipart_read rejection vocabulary: 413 for a cap, 400 for
// a malformed body, an invalid status for transport/sink failures
// (nothing to commit), and Content-Length: 0 rejection fields.
LT_BEGIN_AUTO_TEST(forms_multipart_suite, read_rejection_vocabulary)
    LT_CHECK(reject_status_diff(
                 http::outcome_code::limit_exceeded, 413).empty());
    LT_CHECK(reject_status_diff(
                 http::outcome_code::invalid_argument, 400).empty());
    LT_CHECK(reject_status_diff(http::outcome_code::ok, 0).empty());
    LT_CHECK(reject_status_diff(
                 http::outcome_code::connection_closed, 0).empty());
    LT_CHECK(reject_status_diff(
                 http::outcome_code::protocol_error, 0).empty());
    LT_CHECK(reject_fields_diff().empty());

    forms::multipart_read read;
    read.status = http::outcome::okay();
    LT_CHECK(read.ok());
    read.status = http::outcome(
        http::outcome_code::limit_exceeded, "over");
    LT_CHECK(!read.ok());
LT_END_AUTO_TEST(read_rejection_vocabulary)

// (6) A sink-returned failure is sticky and propagates as the
// decode's terminal, with the abort carrying the sink's outcome.
LT_BEGIN_AUTO_TEST(forms_multipart_suite, sink_failure_propagates)
    failing_sink sink;
    forms::multipart_read read;
    const http::outcome decoded = forms::decode_multipart(
        bytes_of(two_part_body()), k_type, forms::multipart_limits{},
        sink, read);
    LT_CHECK(!decoded.ok());
    LT_CHECK(decoded.code()
             == http::outcome_code::protocol_error);
    LT_CHECK_EQ(sink.aborts, 1);
    LT_CHECK(sink.abort_reason.code()
                                == http::outcome_code::protocol_error);
LT_END_AUTO_TEST(sink_failure_propagates)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
