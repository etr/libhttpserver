/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <openssl/err.h>
#include <chrono>
#include <functional>
#include <string>
#include <vector>
#include "./tls_acme_fixture.hpp"
#include "./littletest.hpp"
namespace hd = httpserver::detail;
using clock_type = std::chrono::system_clock;
LT_BEGIN_SUITE(tls_acme_credentials_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(tls_acme_credentials_suite)
LT_BEGIN_AUTO_TEST(tls_acme_credentials_suite, publication_owns_context_and_removal_is_exact_and_idempotent)
    hd::tls_credentials_registry registry;
    auto input = acme_test::challenge("A.Example.");
    LT_CHECK(registry.publish_acme(input).code() == httpserver::http::outcome_code::invalid_state);
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    auto ordinary = registry.acquire()->select_default().context;
    LT_ASSERT(registry.publish_acme(input).ok());
    auto first = registry.acquire();
    auto selected = first->select_acme("a.example", clock_type::now());
    LT_ASSERT(selected.has_value());
    LT_CHECK(selected->snapshot == first);
    LT_CHECK(first->select_default().context == ordinary);
    LT_CHECK(!first->select_acme("other.a.example", clock_type::now()));
    LT_CHECK(first->select_acme("a.example", input.expires_at - clock_type::duration(1)).has_value());
    LT_CHECK(!first->select_acme("a.example", input.expires_at));
    input.certificate_pem.clear();
    input.private_key_pem.clear();
    LT_CHECK(first->select_acme("a.example", clock_type::now())->context == selected->context);
    LT_ASSERT(registry.publish_acme(acme_test::challenge("b.example")).ok());
    LT_ASSERT(registry.publish_acme(acme_test::challenge()).ok());
    LT_CHECK(registry.acquire()->select_acme("a.example", clock_type::now())->context != selected->context);
    LT_ASSERT(registry.replace(tls_test::credentials("b")).ok());
    LT_CHECK(registry.acquire()->select_acme("a.example", clock_type::now()).has_value());
    LT_ASSERT(registry.remove_acme("A.EXAMPLE.").ok());
    auto removed = registry.acquire();
    LT_CHECK(!removed->select_acme("a.example", clock_type::now()));
    LT_CHECK(removed->select_acme("b.example", clock_type::now()).has_value());
    LT_CHECK(first->select_acme("a.example", clock_type::now()).has_value());
    LT_CHECK_EQ(removed->generation(), std::uint64_t{6});
    LT_ASSERT(registry.remove_acme("a.example").ok());
    LT_CHECK(registry.acquire() == removed);
    LT_CHECK(registry.remove_acme("*.example").code() == httpserver::http::outcome_code::invalid_argument);
    LT_CHECK(registry.acquire() == removed);
LT_END_AUTO_TEST(publication_owns_context_and_removal_is_exact_and_idempotent)
LT_BEGIN_AUTO_TEST(tls_acme_credentials_suite, invalid_candidates_preserve_snapshot_and_generation)
    hd::tls_credentials_registry registry;
    LT_ASSERT(registry.replace(tls_test::credentials()).ok());
    LT_ASSERT(registry.publish_acme(acme_test::challenge()).ok());
    const auto active = registry.acquire();
    auto reject = [&](const hd::tls_acme_challenge& input) {
        const auto result = registry.publish_acme(input);
        LT_CHECK(result.code() == httpserver::http::outcome_code::invalid_argument);
        LT_CHECK_EQ(result.message(), "TLS credentials invalid");
        LT_CHECK(registry.acquire() == active);
        LT_CHECK_EQ(ERR_peek_error(), 0UL);
    };
    std::vector<std::function<void(X509*)>> variants{
        [](X509* c) { acme_test::erase(c, NID_subject_alt_name); },
        [](X509* c) { acme_test::san(c, {"a.example"}); },
        [](X509* c) { acme_test::erase(c, NID_subject_alt_name); acme_test::san(c, {"a.example", "b.example"}); },
        [](X509* c) { acme_test::erase(c, NID_subject_alt_name); acme_test::san(c, {"b.example"}); },
        [](X509* c) { acme_test::erase(c, NID_subject_alt_name); acme_test::san(c, {std::string("a.example\0x", 11)}); },
        [](X509* c) { acme_test::erase(c, NID_subject_alt_name); acme_test::san(c, {"a.example"}, GEN_URI); },
        [](X509* c) { acme_test::erase_acme(c); },
        [](X509* c) { acme_test::extension(c, "1.3.6.1.5.5.7.1.31", true, {4, 32}); },
        [](X509* c) { X509_EXTENSION_set_critical(X509_get_ext(c, acme_test::acme_index(c)), 0); },
        [](X509* c) { X509_gmtime_adj(X509_getm_notAfter(c), -10); },
        [](X509* c) { X509_gmtime_adj(X509_getm_notBefore(c), 60); }
    };
    for (const auto& change : variants) reject(acme_test::challenge("a.example", 303, change));
    for (unsigned mode = 0; mode < 7; ++mode) {
        reject(acme_test::challenge("a.example", 303, [mode](X509* c) {
            acme_test::erase_acme(c);
            std::vector<unsigned char> value(34, 0x42);
            value[0] = 4; value[1] = 32;
            if (mode == 0) value.erase(value.begin(), value.begin() + 2);
            if (mode == 1) value[0] = 5;
            if (mode == 2) value[1] = 31;
            if (mode == 3) value.pop_back();
            if (mode == 4) value.push_back(0);
            if (mode == 5) value.back() ^= 1;
            acme_test::extension(c, mode == 6 ? "1.3.6.1.5.5.7.1.30" : "1.3.6.1.5.5.7.1.31", true, value);
        }));
    }
    auto valid = acme_test::challenge();
    for (const auto& name : {"", "*.example", "bad..example", "-bad.example", "a.example.."}) {
        auto bad = valid; bad.host = name; reject(bad);
    }
    for (const auto& text : {std::string{}, std::string("garbage"), valid.certificate_pem + "garbage", valid.certificate_pem + valid.certificate_pem}) {
        auto bad = valid; bad.certificate_pem = text; reject(bad);
    }
    for (const auto& text : {std::string{}, std::string("garbage"), valid.private_key_pem + "garbage", valid.private_key_pem + valid.private_key_pem,
                             tls_test::pem("data/tls_credentials/b-key.pem"), std::string("-----BEGIN ENCRYPTED PRIVATE KEY-----\nAA==\n-----END ENCRYPTED PRIVATE KEY-----")}) {
        auto bad = valid; bad.private_key_pem = text; reject(bad);
    }
    for (auto deadline : {clock_type::time_point{}, clock_type::now(), clock_type::now() + std::chrono::hours(2)}) {
        auto bad = valid; bad.expires_at = deadline; reject(bad);
    }
    auto bad = valid; bad.key_authorization_sha256[0] ^= std::byte{1}; reject(bad);
LT_END_AUTO_TEST(invalid_candidates_preserve_snapshot_and_generation)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
