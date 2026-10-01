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

// TASK-112: immutable reusable response definitions and overlays
// (PRD-V3N-REQ-026/028/029/030, DR-V3-005). A definition is an
// immutable value built once and sent any number of times; a send
// borrows it plus one per-request overlay and never mutates either.
// The suite pins, in layered steps:
//   S1 - the validating factories (wire-safe base fields, framing
//        self-consistency, auto Content-Length pin, immutability and
//        copy-sharing of the frozen state);
//   S2 - the overlay merge (ordered append after the base fields,
//        pre-commit validation) and the send path for owned bytes;
//   S3 - the reopenable file and source-factory body cursors;
//   S4 - concurrent sends of one definition staying independent.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/http/fields.hpp>
#include <httpserver/http/status.hpp>
#include <httpserver/response_definition.hpp>

#include "./littletest.hpp"

using httpserver::body_chunk;
using httpserver::body_factory;
using httpserver::body_producer;
using httpserver::response_definition;
namespace http = httpserver::http;

namespace {

std::vector<std::byte> bytes(const std::string& s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(std::byte(static_cast<unsigned char>(c)));
    }
    return out;
}

// A factory source whose producers yield `payload` once and end.
body_factory repeating_producer(const std::string& payload) {
    return [payload]() -> body_producer {
        const std::vector<std::byte> owned = bytes(payload);
        return [owned, sent = false]() mutable -> body_chunk {
            if (sent) {
                return body_chunk{http::outcome::okay(), {}, true};
            }
            sent = true;
            return body_chunk{
                http::outcome::okay(),
                std::span<const std::byte>(owned.data(), owned.size()),
                false};
        };
    };
}

http::fields base_fields() {
    http::fields f;
    f.append("Content-Type", "text/plain");
    return f;
}

}  // namespace

LT_BEGIN_SUITE(response_definition_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(response_definition_suite)

// (S1.1) owned_bytes pins Content-Length to the body size when the
// fields carry neither framing field, leaving the caller's object
// untouched (the pin lands on the frozen copy only).
LT_BEGIN_AUTO_TEST(response_definition_suite, owned_bytes_pins_length_when_unframed)
    response_definition def;
    http::fields f = base_fields();
    const http::outcome made = response_definition::owned_bytes(
        http::status::from_code(200), f, bytes("hi"), def);

    LT_CHECK(made.ok());
    LT_CHECK(def.valid());
    LT_CHECK(def.kind() == response_definition::source_kind::owned_bytes);
    LT_CHECK_EQ(def.status().code(), 200);
    LT_CHECK_EQ(def.fields().size(), static_cast<std::size_t>(2));
    LT_CHECK_EQ(def.fields().count("content-length"),
                static_cast<std::size_t>(1));
    LT_CHECK(def.fields().first("content-length").value_or("") == "2");
    LT_CHECK(def.fields().first("content-type").value_or("")
             == "text/plain");
    LT_CHECK_EQ(f.count("content-length"), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(owned_bytes_pins_length_when_unframed)

// (S1.2) an empty owned body pins "0", keeping the framing honest.
LT_BEGIN_AUTO_TEST(response_definition_suite, owned_bytes_empty_body_pins_zero)
    response_definition def;
    const http::outcome made = response_definition::owned_bytes(
        http::status::from_code(204), http::fields(), {}, def);

    LT_CHECK(made.ok());
    LT_CHECK(def.valid());
    LT_CHECK_EQ(def.fields().count("content-length"),
                static_cast<std::size_t>(1));
    LT_CHECK(def.fields().first("content-length").value_or("") == "0");
LT_END_AUTO_TEST(owned_bytes_empty_body_pins_zero)

// (S1.3) an explicit Content-Length passes through verbatim (no
// re-pinning, no second occurrence).
LT_BEGIN_AUTO_TEST(response_definition_suite, owned_bytes_explicit_length_kept)
    response_definition def;
    http::fields f;
    f.append("Content-Length", "42");
    const http::outcome made = response_definition::owned_bytes(
        http::status::from_code(200), f, bytes("hi"), def);

    LT_CHECK(made.ok());
    LT_CHECK_EQ(def.fields().count("content-length"),
                static_cast<std::size_t>(1));
    LT_CHECK(def.fields().first("content-length").value_or("") == "42");
LT_END_AUTO_TEST(owned_bytes_explicit_length_kept)

// (S1.4) a Transfer-Encoding in the base fields is respected: no
// Content-Length is pinned beside it.
LT_BEGIN_AUTO_TEST(response_definition_suite, owned_bytes_te_respected)
    response_definition def;
    http::fields f;
    f.append("Transfer-Encoding", "chunked");
    const http::outcome made = response_definition::owned_bytes(
        http::status::from_code(200), f, bytes("hi"), def);

    LT_CHECK(made.ok());
    LT_CHECK_EQ(def.fields().count("content-length"),
                static_cast<std::size_t>(0));
    LT_CHECK_EQ(def.fields().count("transfer-encoding"),
                static_cast<std::size_t>(1));
LT_END_AUTO_TEST(owned_bytes_te_respected)

// (S1.5) every factory rejects an invalid status and leaves `out`
// untouched (the pre-seeded definition survives the failed call).
LT_BEGIN_AUTO_TEST(response_definition_suite, factories_reject_invalid_status)
    const http::status bad = http::status::from_code(0);

    response_definition seeded;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), http::fields(), bytes("x"),
        seeded).ok());

    response_definition def = seeded;
    http::outcome made = response_definition::owned_bytes(
        bad, http::fields(), bytes("x"), def);
    LT_CHECK(!made.ok());
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!made.message().empty());
    LT_CHECK(def.fields().first("content-length").value_or("") == "1");

    def = seeded;
    made = response_definition::reopen_file(
        bad, http::fields(), "/tmp/somewhere", def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(def.fields().first("content-length").value_or("") == "1");

    def = seeded;
    made = response_definition::factory(
        bad, http::fields(), repeating_producer("x"), def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(def.fields().first("content-length").value_or("") == "1");
LT_END_AUTO_TEST(factories_reject_invalid_status)

// (S1.6) a base field name outside the RFC 9110 token set is rejected
// before the definition exists.
LT_BEGIN_AUTO_TEST(response_definition_suite, factories_reject_non_token_name)
    http::fields f;
    f.append("Bad Name", "v");

    response_definition seeded;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), http::fields(), bytes("x"),
        seeded).ok());

    response_definition def = seeded;
    http::outcome made = response_definition::owned_bytes(
        http::status::from_code(200), f, bytes("x"), def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!made.message().empty());

    def = seeded;
    made = response_definition::reopen_file(
        http::status::from_code(200), f, "/tmp/somewhere", def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);

    def = seeded;
    made = response_definition::factory(
        http::status::from_code(200), f, repeating_producer("x"), def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK_EQ(def.fields().count("bad name"), static_cast<std::size_t>(0));
LT_END_AUTO_TEST(factories_reject_non_token_name)

// (S1.7) control bytes in base field values are rejected; HTAB, legal
// interior OWS, passes.
LT_BEGIN_AUTO_TEST(response_definition_suite, factories_reject_ctl_values)
    for (const std::string value :
         {std::string("a\rb"), std::string("a\nb"),
          std::string("a\x01b"), std::string("a\x7f" "b")}) {
        http::fields f;
        f.append("X-Note", value);
        response_definition def;
        const http::outcome made = response_definition::owned_bytes(
            http::status::from_code(200), f, bytes("x"), def);
        LT_CHECK(made.code() == http::outcome_code::invalid_argument);
        LT_CHECK(!def.valid());
    }

    http::fields htab;
    htab.append("X-Note", "a\tb");
    response_definition ok_def;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), htab, bytes("x"), ok_def).ok());
    LT_CHECK(ok_def.fields().first("x-note").value_or("") == "a\tb");
LT_END_AUTO_TEST(factories_reject_ctl_values)

// (S1.8) framing self-consistency of the base fields: a duplicate
// Content-Length and a Transfer-Encoding beside a Content-Length are
// both rejected (mirroring the engine's response-mode gate).
LT_BEGIN_AUTO_TEST(response_definition_suite, factories_reject_framing_conflicts)
    http::fields dup;
    dup.append("Content-Length", "1");
    dup.append("Content-Length", "2");
    response_definition def;
    http::outcome made = response_definition::owned_bytes(
        http::status::from_code(200), dup, bytes("x"), def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!def.valid());

    http::fields both;
    both.append("Transfer-Encoding", "chunked");
    both.append("Content-Length", "1");
    made = response_definition::reopen_file(
        http::status::from_code(200), both, "/tmp/somewhere", def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!def.valid());
LT_END_AUTO_TEST(factories_reject_framing_conflicts)

// (S1.9) reopen_file stores the path without opening anything: a path
// that does not resolve still constructs (each send observes the file
// system at its own prepare).
LT_BEGIN_AUTO_TEST(response_definition_suite, reopen_file_requires_path_and_defers_open)
    response_definition def;
    const http::outcome made = response_definition::reopen_file(
        http::status::from_code(200), http::fields(),
        "/definitely/not/here/task112.txt", def);
    LT_CHECK(made.ok());
    LT_CHECK(def.valid());
    LT_CHECK(def.kind() == response_definition::source_kind::reopen_file);
    LT_CHECK_EQ(def.fields().count("content-length"),
                static_cast<std::size_t>(0));

    response_definition rejected;
    const http::outcome empty_path = response_definition::reopen_file(
        http::status::from_code(200), http::fields(), "", rejected);
    LT_CHECK(empty_path.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!rejected.valid());
LT_END_AUTO_TEST(reopen_file_requires_path_and_defers_open)

// (S1.10) a factory source requires a non-empty callable.
LT_BEGIN_AUTO_TEST(response_definition_suite, factory_requires_callable)
    response_definition def;
    const http::outcome made = response_definition::factory(
        http::status::from_code(200), http::fields(), body_factory{},
        def);
    LT_CHECK(made.code() == http::outcome_code::invalid_argument);
    LT_CHECK(!def.valid());

    response_definition ok_def;
    LT_CHECK(response_definition::factory(
        http::status::from_code(200), http::fields(),
        repeating_producer("x"), ok_def).ok());
    LT_CHECK(ok_def.valid());
    LT_CHECK(ok_def.kind() == response_definition::source_kind::factory);
LT_END_AUTO_TEST(factory_requires_callable)

// (S1.11) copies share the one frozen state (the fields and status
// alias the same immutable block); a move hands the state over and
// leaves the source empty.
LT_BEGIN_AUTO_TEST(response_definition_suite, copies_share_frozen_state)
    response_definition def;
    LT_CHECK(response_definition::owned_bytes(
        http::status::from_code(200), base_fields(), bytes("abcd"),
        def).ok());

    const response_definition copy = def;
    LT_CHECK(copy.valid());
    LT_CHECK(&copy.fields() == &def.fields());
    LT_CHECK(&copy.status() == &def.status());
    LT_CHECK(copy.kind() == def.kind());

    const response_definition moved = std::move(def);
    LT_CHECK(moved.valid());
    LT_CHECK(moved.fields().first("content-length").value_or("") == "4");
    LT_CHECK(!def.valid());
LT_END_AUTO_TEST(copies_share_frozen_state)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
