/*
 * Copyright (C) 2026 Sebastiano Merlino
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
// Independent provider tooling: never includes or links libhttpserver.
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
struct material {
    std::string identity;
    std::array<unsigned char, 32> key;
    bool early = false;
    static unsigned legacy(SSL* ssl, const char*, char* identity, unsigned cap, unsigned char* key, unsigned maximum) {
        const auto& self = *static_cast<material*>(SSL_get_app_data(ssl));
        if (self.identity.size() + 1 > cap || self.key.size() > maximum) return 0;
        std::memcpy(identity, self.identity.c_str(), self.identity.size() + 1);
        std::memcpy(key, self.key.data(), self.key.size());
        return self.key.size();
    }
    static int session(SSL* ssl, const EVP_MD*, const unsigned char** identity, std::size_t* size, SSL_SESSION** out) {
        const auto& self = *static_cast<material*>(SSL_get_app_data(ssl));
        *out = SSL_SESSION_new();
        const unsigned char id[] = {0x13, 0x01};
        const auto* cipher = SSL_CIPHER_find(ssl, id);
        if (!*out || !cipher || !SSL_SESSION_set1_master_key(*out, self.key.data(), self.key.size())
            || !SSL_SESSION_set_cipher(*out, cipher) || !SSL_SESSION_set_protocol_version(*out, TLS1_3_VERSION)
            || !SSL_SESSION_set_max_early_data(*out, self.early ? 16384 : 0)) {
            SSL_SESSION_free(*out); *out = nullptr; return 0;
        }
        *identity = reinterpret_cast<const unsigned char*>(self.identity.data()); *size = self.identity.size(); return 1;
    }
};
std::string hex(const unsigned char* value, std::size_t size) {
    const char digits[] = "0123456789abcdef";
    std::string result;
    for (std::size_t index = 0; index < size; ++index) {
        result += digits[value[index] >> 4]; result += digits[value[index] & 15];
    }
    return result;
}
struct message_state { unsigned certificates = 0; bool hold = false; };
void messages(int writing, int, int type, const void* data, std::size_t size, SSL*, void* argument) {
    if (!writing && type == SSL3_RT_HANDSHAKE && size > 0 && *static_cast<const unsigned char*>(data) == SSL3_MT_CERTIFICATE) {
        auto& state = *static_cast<message_state*>(argument);
        ++state.certificates;
        if (state.hold) {
            std::cout << "{\"certificate_selected\":true}" << std::endl;
            std::string command; std::getline(std::cin, command);
            state.hold = false;
        }
    }
}
struct socket_owner {
#ifdef _WIN32
    SOCKET fd = INVALID_SOCKET;
    ~socket_owner() { if (fd != INVALID_SOCKET) closesocket(fd); }
#else
    int fd = -1;
    ~socket_owner() { if (fd >= 0) close(fd); }
#endif
};
void fixtures(const std::string& directory) {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(EVP_RSA_gen(2048), EVP_PKEY_free);
    require(static_cast<bool>(key), "fixture key generation");
    std::unique_ptr<FILE, decltype(&fclose)> output(fopen((directory + "/acme-key.pem").c_str(), "w"), fclose);
    require(output && PEM_write_PrivateKey(output.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) == 1, "fixture key write");
    output.reset();
    const std::vector<std::string> variants{"acme", "missing-san", "multiple-san", "wrong-san", "missing-digest",
        "duplicate-san", "duplicate-digest", "noncritical", "wrong-length", "wrong-digest", "expired", "future"};
    for (const auto& variant : variants) {
        std::unique_ptr<X509, decltype(&X509_free)> cert(X509_new(), X509_free);
        require(cert && X509_set_version(cert.get(), 2) == 1 && ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 194) == 1
            && X509_set_pubkey(cert.get(), key.get()) == 1, "fixture certificate");
        X509_gmtime_adj(X509_getm_notBefore(cert.get()), variant == "future" ? 3600 : -60);
        X509_gmtime_adj(X509_getm_notAfter(cert.get()), variant == "expired" ? -1 : 172800);
        auto* name = X509_get_subject_name(cert.get());
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("a.example"), -1, -1, 0);
        X509_set_issuer_name(cert.get(), name);
        if (variant != "missing-san") {
            const std::string san = variant == "multiple-san" ? "DNS:a.example,DNS:b.example" :
                                    variant == "wrong-san" ? "DNS:b.example" : "DNS:a.example";
            std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> extension(
                X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, san.c_str()), X509_EXTENSION_free);
            require(extension && X509_add_ext(cert.get(), extension.get(), -1) == 1, "fixture SAN");
            if (variant == "duplicate-san") require(X509_add_ext(cert.get(), extension.get(), -1) == 1, "duplicate SAN");
        }
        if (variant != "missing-digest") {
            std::array<unsigned char, 34> der; der.fill(0x42); der[0] = 4; der[1] = 32;
            if (variant == "wrong-digest") der[2] = 0;
            std::unique_ptr<ASN1_OCTET_STRING, decltype(&ASN1_OCTET_STRING_free)> bytes(ASN1_OCTET_STRING_new(), ASN1_OCTET_STRING_free);
            ASN1_OCTET_STRING_set(bytes.get(), der.data(), variant == "wrong-length" ? 33 : 34);
            std::unique_ptr<ASN1_OBJECT, decltype(&ASN1_OBJECT_free)> oid(OBJ_txt2obj("1.3.6.1.5.5.7.1.31", 1), ASN1_OBJECT_free);
            std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> extension(
                X509_EXTENSION_create_by_OBJ(nullptr, oid.get(), variant == "noncritical" ? 0 : 1, bytes.get()), X509_EXTENSION_free);
            require(extension && X509_add_ext(cert.get(), extension.get(), -1) == 1, "fixture digest");
            if (variant == "duplicate-digest") require(X509_add_ext(cert.get(), extension.get(), -1) == 1, "duplicate digest");
        }
        require(X509_sign(cert.get(), key.get(), EVP_sha256()) > 0, "fixture signing");
        output.reset(fopen((directory + "/" + variant + ".pem").c_str(), "w"));
        require(output && PEM_write_X509(output.get(), cert.get()) == 1, "fixture certificate write");
        output.reset();
    }
    // A separate trust anchor signs replacement material for the original SNI.
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> root_key(EVP_RSA_gen(2048), EVP_PKEY_free);
    require(static_cast<bool>(root_key), "rotation root key");
    std::unique_ptr<X509, decltype(&X509_free)> root(X509_new(), X509_free), leaf(X509_new(), X509_free);
    require(root && leaf, "rotation certificates");
    for (auto* cert : {root.get(), leaf.get()}) {
        require(X509_set_version(cert, 2) == 1 && ASN1_INTEGER_set(X509_get_serialNumber(cert), cert == root.get() ? 1940 : 1941) == 1,
                "rotation version and serial");
        X509_gmtime_adj(X509_getm_notBefore(cert), -60);
        X509_gmtime_adj(X509_getm_notAfter(cert), 172800);
    }
    auto* root_name = X509_get_subject_name(root.get());
    X509_NAME_add_entry_by_txt(root_name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("TASK-194 rotation root"), -1, -1, 0);
    require(X509_set_issuer_name(root.get(), root_name) == 1 && X509_set_pubkey(root.get(), root_key.get()) == 1, "rotation root identity");
    std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> constraints(
        X509V3_EXT_conf_nid(nullptr, nullptr, NID_basic_constraints, "critical,CA:TRUE"), X509_EXTENSION_free);
    require(constraints && X509_add_ext(root.get(), constraints.get(), -1) == 1
        && X509_sign(root.get(), root_key.get(), EVP_sha256()) > 0, "rotation root signing");
    X509_NAME_add_entry_by_txt(X509_get_subject_name(leaf.get()), "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("a.example"), -1, -1, 0);
    require(X509_set_issuer_name(leaf.get(), root_name) == 1 && X509_set_pubkey(leaf.get(), key.get()) == 1, "rotation leaf identity");
    std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> san(
        X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, "DNS:a.example"), X509_EXTENSION_free);
    require(san && X509_add_ext(leaf.get(), san.get(), -1) == 1
        && X509_sign(leaf.get(), root_key.get(), EVP_sha256()) > 0, "rotation leaf signing");
    output.reset(fopen((directory + "/rotation-root.pem").c_str(), "w"));
    require(output && PEM_write_X509(output.get(), root.get()) == 1, "rotation root write");
    output.reset(fopen((directory + "/root.pem").c_str(), "a"));
    require(output && PEM_write_X509(output.get(), root.get()) == 1, "rotation trust bundle");
    output.reset(fopen((directory + "/a-replacement.pem").c_str(), "w"));
    require(output && PEM_write_X509(output.get(), leaf.get()) == 1, "rotation leaf write");
    output.reset(fopen((directory + "/a-replacement-key.pem").c_str(), "w"));
    require(output && PEM_write_PrivateKey(output.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) == 1, "rotation key write");
    output.reset();
    std::cout << "FIXTURES 14" << std::endl;
}
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "fixtures") {
            fixtures(argv[2]); return 0;
        }
        require(argc == 9, "arguments: port version kind host alpn identity action fixtures");
#ifdef _WIN32
        WSADATA winsock; require(WSAStartup(MAKEWORD(2, 2), &winsock) == 0, "winsock");
#endif
        int port = std::stoi(argv[1]);
        const int version = std::string(argv[2]) == "12" ? TLS1_2_VERSION : TLS1_3_VERSION;
        const std::string action = argv[7], fixtures = argv[8];
        std::string kind = argv[3], alpn = argv[5], host = argv[4];
        material key{argv[6]}; key.key.fill(0x42);
        if (key.identity == "binary") key.identity = std::string("binary\0identity", 15);
        if (key.identity == "wrong-key") {
            key.identity = "identity"; key.key.fill(0x24);
        }
        key.early = action == "early";
        std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
        require(static_cast<bool>(context), "client context");
        SSL_CTX_set_min_proto_version(context.get(), version); SSL_CTX_set_max_proto_version(context.get(), version);
        SSL_CTX_set_session_cache_mode(context.get(), SSL_SESS_CACHE_CLIENT);
        if (kind == "psk" || kind == "acme-early") {
            SSL_CTX_set_cipher_list(context.get(), "PSK-AES128-GCM-SHA256");
            SSL_CTX_set_ciphersuites(context.get(), "TLS_AES_128_GCM_SHA256");
            SSL_CTX_set_psk_client_callback(context.get(), material::legacy);
            SSL_CTX_set_psk_use_session_callback(context.get(), material::session);
        } else if (kind == "certificate") {
            require(SSL_CTX_load_verify_locations(context.get(), (fixtures + "/root.pem").c_str(), nullptr) == 1, "trusted root");
            SSL_CTX_set_verify(context.get(), SSL_VERIFY_PEER, nullptr);
        }
        std::unique_ptr<SSL_SESSION, decltype(&SSL_SESSION_free)> saved(nullptr, SSL_SESSION_free);
        for (unsigned turn = 0; turn < (action == "interactive" ? 3U : 2U); ++turn) {
            socket_owner socket;
            socket.fd = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#ifndef _WIN32
            timeval timeout{3, 0}; setsockopt(socket.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
            setsockopt(socket.fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
#endif
            require(::connect(socket.fd, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0, "client connect");
            std::unique_ptr<SSL, decltype(&SSL_free)> ssl(SSL_new(context.get()), SSL_free);
            SSL_set_fd(ssl.get(), socket.fd); SSL_set_app_data(ssl.get(), &key);
            if (host != "-") SSL_set_tlsext_host_name(ssl.get(), host.c_str());
            if (kind == "certificate") require(SSL_set1_host(ssl.get(), host.c_str()) == 1, "verified hostname");
            std::vector<unsigned char> wire;
            if (alpn != "-") {
                std::size_t start = 0;
                do {
                    const auto end = alpn.find(',', start); const auto item = alpn.substr(start, end - start);
                    wire.push_back(item.size()); wire.insert(wire.end(), item.begin(), item.end());
                    if (end == std::string::npos) break;
                    start = end + 1;
                } while (true);
                SSL_set_alpn_protos(ssl.get(), wire.data(), wire.size());
            }
            message_state messages_state{0, action == "hold-acme"};
            SSL_set_msg_callback(ssl.get(), messages); SSL_set_msg_callback_arg(ssl.get(), &messages_state);
            if (kind == "acme") SSL_set_verify(ssl.get(), SSL_VERIFY_NONE, nullptr);
            if (saved) require(SSL_set_session(ssl.get(), saved.get()) == 1, "offer saved session");
            std::size_t early = 0;
            if (action == "early" && turn == 0) {
                const std::string request = "GET /installed HTTP/1.1\r\nHost: a.example\r\nConnection: close\r\n\r\n";
                require(SSL_write_early_data(ssl.get(), request.data(), request.size(), &early) == 1 && early == request.size(), "early bytes not transmitted");
            }
            const int connected = SSL_connect(ssl.get());
            if (connected != 1) {
                std::cout << "{\"connected\":false,\"early_sent\":" << early << "}" << std::endl; return 2;
            }
            std::string digest;
            if (auto* cert = SSL_get0_peer_certificate(ssl.get())) {
                unsigned char bytes[EVP_MAX_MD_SIZE]; unsigned size = 0;
                require(X509_digest(cert, EVP_sha256(), bytes, &size) == 1, "certificate digest"); digest = hex(bytes, size);
                if (kind == "acme" || kind == "acme-early") {
                    std::unique_ptr<GENERAL_NAMES, decltype(&GENERAL_NAMES_free)> names(
                        static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr)), GENERAL_NAMES_free);
                    require(names && sk_GENERAL_NAME_num(names.get()) == 1, "ACME sole SAN");
                    const auto* name = sk_GENERAL_NAME_value(names.get(), 0);
                    require(name->type == GEN_DNS && ASN1_STRING_length(name->d.dNSName) == 9
                        && std::memcmp(ASN1_STRING_get0_data(name->d.dNSName), "a.example", 9) == 0, "ACME SAN name");
                    std::unique_ptr<ASN1_OBJECT, decltype(&ASN1_OBJECT_free)> oid(OBJ_txt2obj("1.3.6.1.5.5.7.1.31", 1), ASN1_OBJECT_free);
                    const int index = X509_get_ext_by_OBJ(cert, oid.get(), -1);
                    require(index >= 0 && X509_get_ext_by_OBJ(cert, oid.get(), index) == -1, "ACME sole digest");
                    auto* extension = X509_get_ext(cert, index); const auto* value = X509_EXTENSION_get_data(extension);
                    const auto* bytes = ASN1_STRING_get0_data(value);
                    require(X509_EXTENSION_get_critical(extension) && ASN1_STRING_length(value) == 34
                        && bytes[0] == 4 && bytes[1] == 32, "ACME critical digest DER");
                    for (int i = 2; i < 34; ++i) require(bytes[i] == 0x42, "ACME authorization digest");
                }
            }
            const unsigned char* selected = nullptr; unsigned size = 0; SSL_get0_alpn_selected(ssl.get(), &selected, &size);
            const std::string protocol = size ? std::string(reinterpret_cast<const char*>(selected), size) : std::string{};
            std::string body;
            if (action == "hold" || action == "hold-stop") {
                std::cout << "{\"held\":true}" << std::endl;
                std::string command; require(static_cast<bool>(std::getline(std::cin, command)) && command == "request", "held request command");
            }
            if (kind != "acme" && kind != "acme-early") {
                const std::string request = "GET /installed HTTP/1.1\r\nHost: a.example\r\nConnection: close\r\n\r\n";
                std::size_t written = 0; require(SSL_write_ex(ssl.get(), request.data(), request.size(), &written) == 1 && written == request.size(), "ordinary request");
                char bytes[4096]; std::size_t count = 0;
                while (SSL_read_ex(ssl.get(), bytes, sizeof bytes, &count) == 1) {
                    body.append(bytes, count);
                    const auto head = body.find("\r\n\r\n");
                    const auto length = body.find("Content-Length: ");
                    if (head != std::string::npos && length != std::string::npos &&
                        body.size() >= head + 4 + std::stoul(body.substr(length + 16))) break;
                }
                if (action == "hold-stop") {
                    std::cout << "{\"terminal\":\"stopped\",\"response_bytes\":" << body.size() << "}" << std::endl;
                    return 0;
                }
                require(body.find("HTTP/1.1 200 ") == 0 && body.find("profile-ok\n\n") != std::string::npos, "complete profile response");
            }
            if (action != "interactive" || !saved) saved.reset(SSL_get1_session(ssl.get()));
            std::cout << "{\"connected\":true,\"version\":\"" << SSL_get_version(ssl.get())
                << "\",\"cipher\":\"" << SSL_get_cipher_name(ssl.get()) << "\",\"alpn\":\"" << protocol
                << "\",\"certificate_sha256\":\"" << digest << "\",\"certificate_messages\":" << messages_state.certificates
                << ",\"reused\":" << SSL_session_reused(ssl.get()) << ",\"early_sent\":" << early
                << ",\"early_status\":" << SSL_get_early_data_status(ssl.get())
                << ",\"ticket\":" << (saved && SSL_SESSION_has_ticket(saved.get())) << ",\"body_hex\":\""
                << hex(reinterpret_cast<const unsigned char*>(body.data()), body.size()) << "\"}" << std::endl;
            SSL_set_shutdown(ssl.get(), SSL_SENT_SHUTDOWN | SSL_RECEIVED_SHUTDOWN);
            if (action != "resume" && action != "interactive") break;
            if (action == "interactive") {
                std::string command;
                if (!std::getline(std::cin, command)) break;
                if (command == "challenge") {
                    kind = "acme"; alpn = "acme-tls/1";
                } else if (command.starts_with("next")) {
                    if (command.starts_with("next ")) {
                        std::istringstream input(command.substr(5)); input >> host;
                        int requested = 0;
                        if (input >> requested) port = requested;
                    }
                } else {
                    break;
                }
            }
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
