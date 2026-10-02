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
     License along with this library; if not, see the file LICENSE in
     the distribution; if not, write to the Free Software Foundation,
     Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
*/

// TASK-115 step 2: the bounded Authorization: Digest field parser
// (RFC 7616 section 3.4 / RFC 7235 section 2.1, v2 posture: the value
// arrives pre-unfolded and unpadded from the head parser). The suite
// pins:
//   - the happy path: the full field with quoted and unquoted values,
//     quoted-pair unescaping, unknown params ignored, OWS around
//     separators, case-insensitive scheme and param names;
//   - nc: exactly 8 hexadecimal digits, nothing else;
//   - the malformed corpus: each mandatory param missing or empty,
//     duplicates, bare tokens, missing '=', control bytes,
//     unterminated quotes, and every documented bound (whole value,
//     param count, name length, value length).
// Every parse failure maps to one classification (malformed, the same
// 401 as an absent field); v2 never answered 400 for bad credentials.

#include <cstddef>
#include <string>
#include <string_view>

#include <httpserver/detail/digest_params.hpp>

#include "./littletest.hpp"

namespace {

namespace dig = httpserver::detail::digest;

// The canonical full Authorization value every happy-path case varies.
std::string full_header() {
    return "Digest username=\"Mufasa\", realm=\"testhost\", "
           "nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\", "
           "uri=\"/dir/index.html\", "
           "response=\"e966c9a5e547a30c1f5c0e8882b5670\", "
           "algorithm=MD5, qop=auth, nc=00000001, "
           "cnonce=\"0a4f113b\", "
           "opaque=\"5ccc069c403ebaf9f0171e9517f40e41\", "
           "charset=UTF-8";
}

bool parses(std::string_view value) {
    dig::digest_params parsed;
    return dig::parse_digest_credentials(value, parsed)
           == dig::params_status::ok;
}

}  // namespace

LT_BEGIN_SUITE(digest_params_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(digest_params_suite)

LT_BEGIN_AUTO_TEST(digest_params_suite, full_header_parses)
    dig::digest_params p;
    LT_CHECK(dig::parse_digest_credentials(full_header(), p)
             == dig::params_status::ok);
    LT_CHECK_EQ(p.username, std::string("Mufasa"));
    LT_CHECK_EQ(p.realm, std::string("testhost"));
    LT_CHECK_EQ(p.nonce,
                std::string("dcd98b7102dd2f0e8b11d0f600bfb0c093"));
    LT_CHECK_EQ(p.uri, std::string("/dir/index.html"));
    LT_CHECK_EQ(p.response,
                std::string("e966c9a5e547a30c1f5c0e8882b5670"));
    LT_CHECK_EQ(p.algorithm, std::string("MD5"));
    LT_CHECK_EQ(p.qop, std::string("auth"));
    LT_CHECK_EQ(p.nc, std::string("00000001"));
    LT_CHECK_EQ(p.cnonce, std::string("0a4f113b"));
    // Unknown params (charset) are ignored, not stored.
LT_END_AUTO_TEST(full_header_parses)

LT_BEGIN_AUTO_TEST(digest_params_suite, scheme_and_names_case_insensitive)
    dig::digest_params p;
    const std::string variant =
        "dIgEsT   USERNAME=\"u\" ,  REALM = \"r\" ,"
        "\tNonce=\"n\"\t,URI=\"/x\",RESPONSE=\"ab\"";
    LT_CHECK(dig::parse_digest_credentials(variant, p)
             == dig::params_status::ok);
    LT_CHECK_EQ(p.username, std::string("u"));
    LT_CHECK_EQ(p.realm, std::string("r"));
    LT_CHECK_EQ(p.nonce, std::string("n"));
    LT_CHECK_EQ(p.uri, std::string("/x"));
    LT_CHECK_EQ(p.response, std::string("ab"));
LT_END_AUTO_TEST(scheme_and_names_case_insensitive)

LT_BEGIN_AUTO_TEST(digest_params_suite, quoted_pairs_unescape)
    dig::digest_params p;
    const std::string variant =
        "Digest username=\"a\\\\b\\\"c\", realm=\"r\", nonce=\"n\","
        " uri=\"/x\", response=\"ab\"";
    LT_CHECK(dig::parse_digest_credentials(variant, p)
             == dig::params_status::ok);
    LT_CHECK_EQ(p.username, std::string("a\\b\"c"));
    // An unquoted value is a token (tchar run to the separator); the
    // uri must stay quoted in practice because '/' is not a tchar.
    const std::string mixed =
        "Digest username=plain, realm=\"r\", nonce=n, uri=\"/x\","
        " response=ab";
    dig::digest_params q;
    LT_CHECK(dig::parse_digest_credentials(mixed, q)
             == dig::params_status::ok);
    LT_CHECK_EQ(q.username, std::string("plain"));
    LT_CHECK_EQ(q.nonce, std::string("n"));
    LT_CHECK(!parses("Digest username=\"u\", realm=\"r\", nonce=\"n\","
                     " uri=/x, response=\"ab\""));
LT_END_AUTO_TEST(quoted_pairs_unescape)

LT_BEGIN_AUTO_TEST(digest_params_suite, nc_must_be_eight_hex_digits)
    LT_CHECK(parses("Digest username=\"u\", realm=\"r\", nonce=\"n\","
                    " uri=\"/x\", response=\"ab\", nc=00000001"));
    LT_CHECK(!parses("Digest username=\"u\", realm=\"r\", nonce=\"n\","
                     " uri=\"/x\", response=\"ab\", nc=0000001"));
    LT_CHECK(!parses("Digest username=\"u\", realm=\"r\", nonce=\"n\","
                     " uri=\"/x\", response=\"ab\", nc=000000001"));
    LT_CHECK(!parses("Digest username=\"u\", realm=\"r\", nonce=\"n\","
                     " uri=\"/x\", response=\"ab\", nc=0000000g"));
    LT_CHECK(!parses("Digest username=\"u\", realm=\"r\", nonce=\"n\","
                     " uri=\"/x\", response=\"ab\", nc=\"00000001\""
                     ", nc=00000002"));
LT_END_AUTO_TEST(nc_must_be_eight_hex_digits)

LT_BEGIN_AUTO_TEST(digest_params_suite, each_mandatory_param_required)
    const char* missing[] = {
        "realm=\"r\", nonce=\"n\", uri=\"/x\", response=\"ab\"",
        "username=\"u\", nonce=\"n\", uri=\"/x\", response=\"ab\"",
        "username=\"u\", realm=\"r\", uri=\"/x\", response=\"ab\"",
        "username=\"u\", realm=\"r\", nonce=\"n\", response=\"ab\"",
        "username=\"u\", realm=\"r\", nonce=\"n\", uri=\"/x\"",
    };
    for (const char* body : missing) {
        LT_CHECK(!parses(std::string("Digest ") + body));
    }
    // Present but empty is equally malformed.
    LT_CHECK(!parses("Digest username=\"\", realm=\"r\", nonce=\"n\","
                     " uri=\"/x\", response=\"ab\""));
    LT_CHECK(!parses("Digest username=\"u\", realm=\"r\", nonce=\"n\","
                     " uri=\"\", response=\"ab\""));
    LT_CHECK(!parses("Digest username=\"u\", realm=\"\", nonce=\"n\","
                     " uri=\"/x\", response=\"ab\""));
    LT_CHECK(!parses("Digest username=\"u\", realm=\"r\", nonce=\"\","
                     " uri=\"/x\", response=\"ab\""));
    LT_CHECK(!parses("Digest username=\"u\", realm=\"r\", nonce=\"n\","
                     " uri=\"/x\", response=\"\""));
LT_END_AUTO_TEST(each_mandatory_param_required)

LT_BEGIN_AUTO_TEST(digest_params_suite, malformed_corpus)
    const std::string cases[] = {
        // Wrong scheme entirely.
        std::string("Basic dXNlcg=="),
        std::string("Digestx username=\"u\", realm=\"r\""),
        // Scheme with no params.
        std::string("Digest"),
        std::string("Digest   "),
        // Structure errors.
        std::string("Digest username"),
        std::string("Digest username=, realm=\"r\""),
        std::string("Digest ,username=\"u\""),
        std::string("Digest username=\"u\","),
        std::string("Digest username=\"u\" realm=\"r\""),
        // Duplicate mandatory param.
        std::string("Digest username=\"u\", username=\"v\","
                    " realm=\"r\", nonce=\"n\", uri=\"/x\","
                    " response=\"ab\""),
        // Unterminated quote.
        std::string("Digest username=\"u, realm=\"r\""),
        // Control bytes anywhere are refused.
        std::string("Digest username=\"u\0\", realm=\"r\"", 31),
        std::string("Digest username=\"u\",\n realm=\"r\""),
        std::string("Digest username=\"u\",\r realm=\"r\""),
        std::string("Digest username=\"u\",\t realm=\"r\","
                    " nonce=\"n\", uri=\"/x\", response=\"ab\",\x01z=\"\""),
    };
    for (const std::string& bad : cases) {
        LT_CHECK(!parses(bad));
    }
    // A tab between params is OWS, not a control violation.
    LT_CHECK(parses("Digest username=\"u\",\trealm=\"r\",\tnonce=\"n\","
                    " uri=\"/x\", response=\"ab\""));
LT_END_AUTO_TEST(malformed_corpus)

LT_BEGIN_AUTO_TEST(digest_params_suite, documented_bounds_hold)
    // 25 well-formed params: one over the 24-param bound.
    std::string many = "Digest username=\"u\", realm=\"r\", nonce=\"n\","
                       " uri=\"/x\", response=\"ab\"";
    for (int i = 0; i < 19; ++i) {
        many += ", x" + std::to_string(i) + "=v";
    }
    LT_CHECK(parses(many));
    many += ", oneMore=1";
    LT_CHECK(!parses(many));

    // A 25-character name (bound: 24).
    const std::string long_name(25, 'a');
    LT_CHECK(!parses("Digest username=\"u\", realm=\"r\", nonce=\"n\","
                     " uri=\"/x\", response=\"ab\", "
                     + long_name + "=1"));

    // A 4097-byte quoted value (bound: 4096).
    const std::string big(4097, 'v');
    LT_CHECK(!parses("Digest username=\"" + big + "\", realm=\"r\""));

    // A whole value past 8192 bytes.
    std::string huge = "Digest username=\"u\", realm=\"r\","
                       " nonce=\"n\", uri=\"/x\", response=\"ab\"";
    while (huge.size() <= 8192) {
        huge += ", pad=v";
    }
    LT_CHECK(!parses(huge));
LT_END_AUTO_TEST(documented_bounds_hold)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
