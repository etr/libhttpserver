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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

// TASK-079: pure-CPU self-test for the test-side digest client helper
// (`test/integ/digest_client.hpp`). Pins the FIPS MD5/SHA-256 test
// vectors that prove the inline reference crypto primitives are correct,
// pins parse_www_authenticate() against the canonical RFC 7616 §3.3
// challenge shape, and pins compute_response_cleartext() against the
// RFC 7616 §3.9.1 worked example so the helper's response computation
// can be trusted before any integration test consumes it.

#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#include "../integ/digest_client.hpp"
#include "./littletest.hpp"

using httpserver_test::compute_response_cleartext;
using httpserver_test::digest_hash;
using httpserver_test::extract_digest_challenge;
using httpserver_test::parse_www_authenticate;
using httpserver_test::parsed_challenge;

LT_BEGIN_SUITE(digest_client_self_test_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(digest_client_self_test_suite)

namespace {

std::string hex_of(const unsigned char* bytes, std::size_t n) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(digits[(bytes[i] >> 4) & 0xF]);
        out.push_back(digits[bytes[i] & 0xF]);
    }
    return out;
}

}  // namespace

// FIPS 180-2 / RFC 1321 canonical MD5 vector: MD5("abc")
LT_BEGIN_AUTO_TEST(digest_client_self_test_suite, md5_abc_vector)
    unsigned char out[16];
    httpserver_test::detail::md5(
        reinterpret_cast<const unsigned char*>("abc"), 3, out);
    LT_CHECK_EQ(hex_of(out, 16),
                std::string("900150983cd24fb0d6963f7d28e17f72"));
LT_END_AUTO_TEST(md5_abc_vector)

// FIPS 180-2 canonical SHA-256 vector: SHA-256("abc")
LT_BEGIN_AUTO_TEST(digest_client_self_test_suite, sha256_abc_vector)
    unsigned char out[32];
    httpserver_test::detail::sha256(
        reinterpret_cast<const unsigned char*>("abc"), 3, out);
    LT_CHECK_EQ(hex_of(out, 32),
                std::string("ba7816bf8f01cfea414140de5dae2223"
                            "b00361a396177a9cb410ff61f20015ad"));
LT_END_AUTO_TEST(sha256_abc_vector)

// Empty-string vectors pin the padding logic.
LT_BEGIN_AUTO_TEST(digest_client_self_test_suite, md5_empty_vector)
    unsigned char out[16];
    httpserver_test::detail::md5(
        reinterpret_cast<const unsigned char*>(""), 0, out);
    LT_CHECK_EQ(hex_of(out, 16),
                std::string("d41d8cd98f00b204e9800998ecf8427e"));
LT_END_AUTO_TEST(md5_empty_vector)

LT_BEGIN_AUTO_TEST(digest_client_self_test_suite, sha256_empty_vector)
    unsigned char out[32];
    httpserver_test::detail::sha256(
        reinterpret_cast<const unsigned char*>(""), 0, out);
    LT_CHECK_EQ(hex_of(out, 32),
                std::string("e3b0c44298fc1c149afbf4c8996fb924"
                            "27ae41e4649b934ca495991b7852b855"));
LT_END_AUTO_TEST(sha256_empty_vector)

// RFC 7616 §3.3 challenge parse: realm, nonce, opaque, algorithm, qop.
LT_BEGIN_AUTO_TEST(digest_client_self_test_suite, parse_well_formed_challenge)
    std::string raw =
        R"(Digest realm="testrealm@host.com", )"
        R"(qop="auth", )"
        R"(nonce="dcd98b7102dd2f0e8b11d0f600bfb0c093", )"
        R"(opaque="5ccc069c403ebaf9f0171e9517f40e41", )"
        R"(algorithm=MD5)";
    auto parsed = parse_www_authenticate(raw);
    LT_ASSERT_EQ(parsed.has_value(), true);
    LT_CHECK_EQ(parsed->realm, std::string("testrealm@host.com"));
    LT_CHECK_EQ(parsed->nonce, std::string("dcd98b7102dd2f0e8b11d0f600bfb0c093"));
    LT_CHECK_EQ(parsed->opaque, std::string("5ccc069c403ebaf9f0171e9517f40e41"));
    LT_CHECK_EQ(parsed->algorithm, std::string("MD5"));
    LT_CHECK_EQ(parsed->qop, std::string("auth"));
LT_END_AUTO_TEST(parse_well_formed_challenge)

// Algorithm token "SHA-256" must round-trip verbatim (RFC 7616 §3.3).
LT_BEGIN_AUTO_TEST(digest_client_self_test_suite, parse_sha256_algorithm)
    std::string raw =
        R"(Digest realm="r", nonce="n", qop="auth", algorithm=SHA-256)";
    auto parsed = parse_www_authenticate(raw);
    LT_ASSERT_EQ(parsed.has_value(), true);
    LT_CHECK_EQ(parsed->algorithm, std::string("SHA-256"));
LT_END_AUTO_TEST(parse_sha256_algorithm)

// Extracting a Digest challenge from a multi-line raw header block.
LT_BEGIN_AUTO_TEST(digest_client_self_test_suite, extract_from_header_block)
    std::string headers =
        "HTTP/1.1 401 Unauthorized\r\n"
        "Content-Type: text/plain\r\n"
        "WWW-Authenticate: Digest realm=\"r\", nonce=\"n\", "
        "qop=\"auth\", algorithm=MD5, opaque=\"o\"\r\n"
        "Content-Length: 4\r\n"
        "\r\n";
    auto parsed = extract_digest_challenge(headers);
    LT_ASSERT_EQ(parsed.has_value(), true);
    LT_CHECK_EQ(parsed->realm, std::string("r"));
    LT_CHECK_EQ(parsed->nonce, std::string("n"));
    LT_CHECK_EQ(parsed->qop, std::string("auth"));
    LT_CHECK_EQ(parsed->opaque, std::string("o"));
LT_END_AUTO_TEST(extract_from_header_block)

// RFC 7616 §3.9.1 worked-example: MD5, qop=auth.
//   username = "Mufasa"
//   password = "Circle of Life"
//   realm    = "http-auth@example.org"
//   nonce    = "7ypf/xlj9XXwfDPEoM4URrv/xwf94BcCAzFZH4GiTo0v"
//   nc       = "00000001"
//   cnonce   = "f2/wE4q74E6zIJEtWaHKaf5wv/H5QzzpXusqGemxURZJ"
//   uri      = "/dir/index.html"
//   method   = "GET"
//   qop      = "auth"
//   algorithm= MD5
//   expected response = "8ca523f5e9506fed4657c9700eebdbec"
LT_BEGIN_AUTO_TEST(digest_client_self_test_suite, rfc7616_md5_worked_example)
    parsed_challenge ch{};
    ch.realm     = "http-auth@example.org";
    ch.nonce     = "7ypf/xlj9XXwfDPEoM4URrv/xwf94BcCAzFZH4GiTo0v";
    ch.opaque    = "FQhe/qaU925kfnzjCev0ciny7QMkPqMAFRtzCUYo5tdS";
    ch.algorithm = "MD5";
    ch.qop       = "auth";
    std::string response = compute_response_cleartext(
        ch, digest_hash::md5,
        "GET", "/dir/index.html",
        "Mufasa", "Circle of Life",
        "f2/wE4q74E6zIJEtWaHKaf5wv/H5QzzpXusqGemxURZJ",
        "00000001");
    LT_CHECK_EQ(response,
                std::string("8ca523f5e9506fed4657c9700eebdbec"));
LT_END_AUTO_TEST(rfc7616_md5_worked_example)

// RFC 7616 §3.9.1 worked-example: SHA-256, same fixture, expected response:
//   "753927fa0e85d155564e2e272a28d1802ca10daf4496794697cf8db5856cb6c1"
LT_BEGIN_AUTO_TEST(digest_client_self_test_suite, rfc7616_sha256_worked_example)
    parsed_challenge ch{};
    ch.realm     = "http-auth@example.org";
    ch.nonce     = "7ypf/xlj9XXwfDPEoM4URrv/xwf94BcCAzFZH4GiTo0v";
    ch.opaque    = "FQhe/qaU925kfnzjCev0ciny7QMkPqMAFRtzCUYo5tdS";
    ch.algorithm = "SHA-256";
    ch.qop       = "auth";
    std::string response = compute_response_cleartext(
        ch, digest_hash::sha256,
        "GET", "/dir/index.html",
        "Mufasa", "Circle of Life",
        "f2/wE4q74E6zIJEtWaHKaf5wv/H5QzzpXusqGemxURZJ",
        "00000001");
    LT_CHECK_EQ(response,
                std::string("753927fa0e85d155564e2e272a28d18"
                            "02ca10daf4496794697cf8db5856cb6c1"));
LT_END_AUTO_TEST(rfc7616_sha256_worked_example)

// compute_response_ha1 must match compute_response_cleartext when fed
// the cleartext-derived HA1: this is the invariant that makes the
// HA1-precomputed integ-test pattern legitimate.
LT_BEGIN_AUTO_TEST(digest_client_self_test_suite, ha1_path_matches_cleartext_path)
    parsed_challenge ch{};
    ch.realm     = "examplerealm";
    ch.nonce     = "abcdef0123456789";
    ch.opaque    = "deadbeef";
    ch.algorithm = "MD5";
    ch.qop       = "auth";
    std::string cleartext_response = compute_response_cleartext(
        ch, digest_hash::md5,
        "GET", "/base",
        "myuser", "mypass",
        "01234567abcdef00", "00000001");
    // Pre-derived MD5("myuser:examplerealm:mypass") from the same constants
    // used by the integration test in authentication.cpp.
    unsigned char ha1[16] = {
        0x6c, 0xee, 0xf7, 0x50, 0xe0, 0x13, 0x0d, 0x65,
        0x28, 0xb9, 0x38, 0xc3, 0xab, 0xd9, 0x41, 0x10
    };
    std::string ha1_response = httpserver_test::compute_response_ha1(
        ch, digest_hash::md5,
        "GET", "/base",
        ha1, 16,
        "01234567abcdef00", "00000001");
    LT_CHECK_EQ(cleartext_response, ha1_response);
LT_END_AUTO_TEST(ha1_path_matches_cleartext_path)

// make_cnonce must produce a 32-hex-char string (16 bytes of random data
// encoded as full hex pairs, not nibble-only encoding) so that every bit of
// each source byte contributes to the output. The output must consist solely
// of lowercase hex characters [0-9a-f].
LT_BEGIN_AUTO_TEST(digest_client_self_test_suite, make_cnonce_produces_valid_hex)
    std::string cnonce = httpserver_test::make_cnonce();
    // A properly-encoded 16-byte random token is exactly 32 hex characters.
    LT_CHECK_EQ(cnonce.size(), std::size_t(32));
    // Every character must be a lowercase hex digit.
    bool all_hex = true;
    for (char c : cnonce) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            all_hex = false;
            break;
        }
    }
    LT_CHECK_EQ(all_hex, true);
LT_END_AUTO_TEST(make_cnonce_produces_valid_hex)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
