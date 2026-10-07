/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef TEST_UNIT_TLS_ACME_FIXTURE_HPP_
#define TEST_UNIT_TLS_ACME_FIXTURE_HPP_
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "./tls_credentials_fixture.hpp"
namespace acme_test {
namespace hd = httpserver::detail;
using certificate_ptr = std::unique_ptr<X509, decltype(&X509_free)>;
inline void check(bool ok) { if (!ok) throw std::runtime_error("ACME fixture invalid"); }
inline void extension(X509* cert, const char* oid, bool critical, const std::vector<unsigned char>& bytes) {
    std::unique_ptr<ASN1_OBJECT, decltype(&ASN1_OBJECT_free)> object(OBJ_txt2obj(oid, 1), ASN1_OBJECT_free);
    std::unique_ptr<ASN1_OCTET_STRING, decltype(&ASN1_OCTET_STRING_free)> value(ASN1_OCTET_STRING_new(), ASN1_OCTET_STRING_free);
    check(object && value && ASN1_OCTET_STRING_set(value.get(), bytes.data(), static_cast<int>(bytes.size())) == 1);
    std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> ext(X509_EXTENSION_create_by_OBJ(nullptr, object.get(), critical, value.get()), X509_EXTENSION_free);
    check(ext && X509_add_ext(cert, ext.get(), -1) == 1);
}
inline void san(X509* cert, const std::vector<std::string>& names, int type = GEN_DNS) {
    std::unique_ptr<GENERAL_NAMES, decltype(&GENERAL_NAMES_free)> entries(sk_GENERAL_NAME_new_null(), GENERAL_NAMES_free);
    for (const auto& name : names) {
        auto* entry = GENERAL_NAME_new();
        auto* value = ASN1_IA5STRING_new();
        check(entry && value && ASN1_STRING_set(value, name.data(), static_cast<int>(name.size())) == 1);
        GENERAL_NAME_set0_value(entry, type, value);
        check(sk_GENERAL_NAME_push(entries.get(), entry) > 0);
    }
    check(X509_add1_ext_i2d(cert, NID_subject_alt_name, entries.get(), 0, X509V3_ADD_APPEND) == 1);
}
inline int acme_index(X509* cert) {
    std::unique_ptr<ASN1_OBJECT, decltype(&ASN1_OBJECT_free)> object(OBJ_txt2obj("1.3.6.1.5.5.7.1.31", 1), ASN1_OBJECT_free);
    return X509_get_ext_by_OBJ(cert, object.get(), -1);
}
inline void erase_acme(X509* cert) {
    const int index = acme_index(cert);
    check(index >= 0);
    X509_EXTENSION_free(X509_delete_ext(cert, index));
}
inline void erase(X509* cert, int nid) {
    const int index = X509_get_ext_by_NID(cert, nid, -1);
    check(index >= 0);
    X509_EXTENSION_free(X509_delete_ext(cert, index));
}
inline hd::tls_acme_challenge challenge(const std::string& name = "a.example", std::int64_t serial = 303,
                                      const std::function<void(X509*)>& change = {}, std::byte digest = std::byte{0x42}) {
    hd::tls_acme_challenge result;
    result.host = name;
    result.private_key_pem = tls_test::pem("data/tls_credentials/a-key.pem");
    result.key_authorization_sha256.fill(digest);
    result.expires_at = std::chrono::system_clock::now() + std::chrono::minutes(10);
    std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_mem_buf(result.private_key_pem.data(), static_cast<int>(result.private_key_pem.size())), BIO_free);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(PEM_read_bio_PrivateKey(input.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    certificate_ptr cert(X509_new(), X509_free);
    check(cert && key && X509_set_version(cert.get(), 2) == 1);
    check(ASN1_INTEGER_set_int64(X509_get_serialNumber(cert.get()), serial) == 1);
    check(X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60) && X509_gmtime_adj(X509_getm_notAfter(cert.get()), 3600));
    check(X509_set_pubkey(cert.get(), key.get()) == 1);
    auto* subject = X509_get_subject_name(cert.get());
    check(X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(name.data()), static_cast<int>(name.size()), -1, 0) == 1);
    check(X509_set_issuer_name(cert.get(), subject) == 1);
    san(cert.get(), {name});
    std::vector<unsigned char> bytes(34, std::to_integer<unsigned char>(digest));
    bytes[0] = 4;
    bytes[1] = 32;
    extension(cert.get(), "1.3.6.1.5.5.7.1.31", true, bytes);
    if (change) change(cert.get());
    check(X509_sign(cert.get(), key.get(), EVP_sha256()) > 0);
    std::unique_ptr<BIO, decltype(&BIO_free)> output(BIO_new(BIO_s_mem()), BIO_free);
    check(output && PEM_write_bio_X509(output.get(), cert.get()) == 1);
    char* data = nullptr;
    const auto length = BIO_get_mem_data(output.get(), &data);
    result.certificate_pem.assign(data, static_cast<std::size_t>(length));
    return result;
}
}  // namespace acme_test
#endif  // TEST_UNIT_TLS_ACME_FIXTURE_HPP_
