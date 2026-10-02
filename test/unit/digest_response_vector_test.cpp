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

// TASK-115 step 3: the RFC 7616 response computation (HA1, HA2, the
// qop chain, and the legacy no-qop chain), algorithm-parameterized
// through one descriptor per hash -- no parallel MD5/SHA-256 blocks.
// The suite pins:
//   - RFC 7616 section 3.9.1 verbatim: the same GET with MD5 and with
//     SHA-256 (Mufasa / http-auth@example.org / "Circle of Life",
//     nonce "7ypf/xlj...", cnonce "f2/wE4q7...", nc 00000001,
//     qop auth) and their printed responses;
//   - the RFC 2617-heritage MD5 example (testrealm@host.com / "Circle
//     Of Life") with its printed HA1/HA2/response;
//   - the algorithm tokens and digest widths the policy matches
//     against client values;
//   - the legacy no-qop chain H(HA1:nonce:HA2), cross-checked against
//     the independent integ/digest_client.hpp MD5 (deliberate upward
//     include, the digest_client_self_test convention).

#include <cstddef>
#include <string>
#include <string_view>

// Deliberate upward include: the legacy cross-check needs an MD5
// implementation independent of the library's in-tree one.
#include "../integ/digest_client.hpp"
#include <httpserver/detail/digest_response.hpp>

#include "./littletest.hpp"

namespace {

namespace dig = httpserver::detail::digest;

const char* const k_rfc_nonce = "7ypf/xlj9XXwfDPEoM4URrv/xwf94BcCAzFZH4GiTo0v";
const char* const k_rfc_cnonce = "f2/wE4q74E6zIJEtWaHKaf5wv/H5QzzpXusqGemxURZJ";
const char* const k_rfc_uri = "/dir/index.html";
const char* const k_rfc_method = "GET";
const char* const k_nc = "00000001";
const char* const k_qop = "auth";

}  // namespace

LT_BEGIN_SUITE(digest_response_vector_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(digest_response_vector_suite)

// RFC 7616 section 3.9.1, algorithm=MD5: username "Mufasa", realm
// "http-auth@example.org", password "Circle of Life".
LT_BEGIN_AUTO_TEST(digest_response_vector_suite, rfc7616_md5_worked_example)
    const dig::hash_descriptor& d = dig::md5_descriptor;
    LT_CHECK_EQ(std::string(d.token), std::string("MD5"));
    LT_CHECK_EQ(d.digest_size, std::size_t{16});

    const std::string ha1 = dig::compute_ha1(
        d, "Mufasa", "http-auth@example.org", "Circle of Life");
    LT_CHECK_EQ(ha1, std::string(
        "3d78807defe7de2157e2b0b6573a855f"));
    const std::string ha2 = dig::compute_ha2(d, k_rfc_method, k_rfc_uri);
    LT_CHECK_EQ(ha2, std::string(
        "39aff3a2bab6126f332b942af96d3366"));
    const std::string response = dig::compute_response_qop(
        d, ha1, k_rfc_nonce, k_nc, k_rfc_cnonce, k_qop, ha2);
    LT_CHECK_EQ(response, std::string(
        "8ca523f5e9506fed4657c9700eebdbec"));
LT_END_AUTO_TEST(rfc7616_md5_worked_example)

// RFC 7616 section 3.9.1, algorithm=SHA-256: same request, same
// credentials, the same nonce/cnonce/nc/qop.
LT_BEGIN_AUTO_TEST(digest_response_vector_suite, rfc7616_sha256_worked_example)
    const dig::hash_descriptor& d = dig::sha256_descriptor;
    LT_CHECK_EQ(std::string(d.token), std::string("SHA-256"));
    LT_CHECK_EQ(d.digest_size, std::size_t{32});

    const std::string ha1 = dig::compute_ha1(
        d, "Mufasa", "http-auth@example.org", "Circle of Life");
    LT_CHECK_EQ(ha1, std::string(
        "7987c64c30e25f1b74be53f966b49b90f2808aa92faf9a00262392d7b4794232"));
    const std::string ha2 = dig::compute_ha2(d, k_rfc_method, k_rfc_uri);
    LT_CHECK_EQ(ha2, std::string(
        "9a3fdae9a622fe8de177c24fa9c070f2b181ec85e15dcbdc32e10c82ad450b04"));
    const std::string response = dig::compute_response_qop(
        d, ha1, k_rfc_nonce, k_nc, k_rfc_cnonce, k_qop, ha2);
    LT_CHECK_EQ(response, std::string(
        "753927fa0e85d155564e2e272a28d1802ca10daf4496794697cf8db5856cb6c1"));
LT_END_AUTO_TEST(rfc7616_sha256_worked_example)

// The RFC 2617 section 3.5 example the MD5 chain inherits (a
// different realm and the differently-cased password): HA1/HA2 are
// printed in the RFC itself.
LT_BEGIN_AUTO_TEST(digest_response_vector_suite, rfc2617_heritage_md5_example)
    const dig::hash_descriptor& d = dig::md5_descriptor;
    const std::string ha1 = dig::compute_ha1(
        d, "Mufasa", "testrealm@host.com", "Circle Of Life");
    LT_CHECK_EQ(ha1, std::string(
        "939e7578ed9e3c518a452acee763bce9"));
    const std::string ha2 = dig::compute_ha2(d, k_rfc_method, k_rfc_uri);
    LT_CHECK_EQ(ha2, std::string(
        "39aff3a2bab6126f332b942af96d3366"));
    const std::string response = dig::compute_response_qop(
        d, ha1, "dcd98b7102dd2f0e8b11d0f600bfb0c093", k_nc, "0a4f113b",
        k_qop, ha2);
    LT_CHECK_EQ(response, std::string(
        "6629fae49393a05397450978507c4ef1"));
LT_END_AUTO_TEST(rfc2617_heritage_md5_example)

// The legacy (RFC 2617 no-qop) chain response = H(HA1:nonce:HA2),
// cross-checked with the independent test-side MD5.
LT_BEGIN_AUTO_TEST(digest_response_vector_suite, legacy_no_qop_chain)
    const std::string ha1 = dig::compute_ha1(
        dig::md5_descriptor, "alice", "transcript", "wonderland");
    const std::string ha2 = dig::compute_ha2(
        dig::md5_descriptor, "GET", "/secret");
    const std::string nonce = "abc123def456abc123def456abc123de";
    const std::string legacy = dig::compute_response_legacy(
        dig::md5_descriptor, ha1, nonce, ha2);

    const std::string independent =
        httpserver_test::digest_client_internal::H_hex(
            httpserver_test::digest_hash::md5,
            ha1 + ":" + nonce + ":" + ha2);
    LT_CHECK_EQ(legacy, independent);
    // And it differs from the qop chain over the same material (the
    // chains are genuinely distinct formulas).
    const std::string qopped = dig::compute_response_qop(
        dig::md5_descriptor, ha1, nonce, k_nc, "0a4f113b", k_qop, ha2);
    LT_CHECK(legacy != qopped);
LT_END_AUTO_TEST(legacy_no_qop_chain)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
