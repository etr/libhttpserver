/*
 * Copyright (C) 2026 Sebastiano Merlino
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <barrier>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <atomic>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <httpserver.hpp>
#include "./littletest.hpp"
namespace srv = httpserver::server;
namespace http = httpserver::http;
namespace {
std::string pem(const std::string& name) {
    std::ifstream in(std::string(TLS_TEST_DIR) + "/data/tls_credentials/" + name);
    return {std::istreambuf_iterator<char>(in), {}};
}
srv::tls_credentials_config credentials(srv::tls_client_certificate_mode mode = srv::tls_client_certificate_mode::require) {
    srv::tls_host_credentials host{"a.example", pem("a.pem") + pem("intermediate.pem"), pem("a-key.pem"), {"http/1.1"}};
    host.profile = srv::tls_profile::mutual_tls;
    host.client_auth.mode = mode;
    host.trust_roots_pem = pem("client-root.pem");
    return {{host}, 0};
}
srv::server_options options(std::shared_ptr<srv::tls_credentials> store) {
    srv::server_options result;
    result.concurrency().workers = 1;
    result.tls().provider = srv::tls_provider::system_default;
    result.tls().profile = srv::tls_profile::mutual_tls;
    result.add_listener({"127.0.0.1", 0, true, std::move(store)});
    return result;
}
struct reentrant_lookup {
    std::weak_ptr<srv::tls_credentials> store;
    std::shared_ptr<std::atomic<bool>> armed;
    std::shared_ptr<std::atomic<unsigned>> retired;
    bool copied = false;
    reentrant_lookup(std::weak_ptr<srv::tls_credentials> owner,
        std::shared_ptr<std::atomic<bool>> enabled, std::shared_ptr<std::atomic<unsigned>> completed)
        : store(std::move(owner)), armed(std::move(enabled)), retired(std::move(completed)) {}
    reentrant_lookup(const reentrant_lookup& other)
        : store(other.store), armed(other.armed), retired(other.retired), copied(true) {}
    ~reentrant_lookup() {
        if (copied && armed->load()) {
            if (auto owner = store.lock(); owner && owner->remove_acme("a.example").ok()) {
                ++*retired;
            }
        }
    }
    srv::tls_psk_result operator()(std::span<const std::byte>, const srv::tls_psk_context&) const { return {}; }
};
}  // namespace
LT_BEGIN_SUITE(public_profiles)
void set_up() {}
void tear_down() {}
LT_END_SUITE(public_profiles)
LT_BEGIN_AUTO_TEST(public_profiles, public_mutual_tls_listens_and_retains_required_policy)
    auto store = std::make_shared<srv::tls_credentials>();
    LT_ASSERT(store->replace(credentials()).ok());
    srv::native_server server(options(store));
    LT_ASSERT(server.listen().ok());
    LT_CHECK(server.get_bound_port(0) != 0);
    LT_CHECK(store->replace(credentials(srv::tls_client_certificate_mode::request)).code() == http::outcome_code::invalid_argument);
    srv::native_server second(options(store));
    LT_ASSERT(second.listen().ok());
    server.request_stop();
    LT_CHECK(store->replace(credentials(srv::tls_client_certificate_mode::request)).code() == http::outcome_code::invalid_argument);
    second.request_stop();
    LT_CHECK(store->replace(credentials(srv::tls_client_certificate_mode::request)).ok());
LT_END_AUTO_TEST(public_mutual_tls_listens_and_retains_required_policy)
LT_BEGIN_AUTO_TEST(public_profiles, public_authentication_validation_preserves_existing_generation)
    srv::tls_credentials store;
    LT_ASSERT(store.replace(credentials()).ok());
    auto invalid = credentials();
    invalid.hosts[0].trust_roots_pem.clear();
    LT_CHECK(store.replace(invalid).code() == http::outcome_code::invalid_argument);
    invalid = credentials();
    invalid.hosts[0].client_auth.timing = srv::tls_client_auth_timing::post_handshake;
    LT_CHECK(store.replace(invalid).code() == http::outcome_code::not_supported);
    invalid = credentials();
    invalid.hosts[0].profile = srv::tls_profile::external_psk;
    LT_CHECK(store.replace(invalid).code() == http::outcome_code::invalid_argument);
LT_END_AUTO_TEST(public_authentication_validation_preserves_existing_generation)
LT_BEGIN_AUTO_TEST(public_profiles, concurrent_bind_and_downgrade_are_one_policy_transaction)
    for (unsigned index = 0; index < 16; ++index) {
        auto store = std::make_shared<srv::tls_credentials>();
        LT_ASSERT(store->replace(credentials()).ok());
        srv::native_server server(options(store));
        std::barrier start(2);
        http::outcome bound, replaced;
        std::jthread writer([&] { start.arrive_and_wait(); replaced = store->replace(credentials(srv::tls_client_certificate_mode::request)); });
        start.arrive_and_wait(); bound = server.listen(); writer.join();
        LT_CHECK(bound.ok() != replaced.ok());
        LT_CHECK((bound.ok() ? replaced : bound).code() == http::outcome_code::invalid_argument);
    }
LT_END_AUTO_TEST(concurrent_bind_and_downgrade_are_one_policy_transaction)
LT_BEGIN_AUTO_TEST(public_profiles, acme_public_validation_and_idempotent_removal)
    srv::tls_credentials store;
    LT_ASSERT(store.replace(credentials()).ok());
    LT_CHECK(store.publish_acme({}).code() == http::outcome_code::invalid_argument);
    LT_CHECK(store.remove_acme("A.EXAMPLE.").ok());
    LT_CHECK(store.remove_acme("bad host").code() == http::outcome_code::invalid_argument);
LT_END_AUTO_TEST(acme_public_validation_and_idempotent_removal)
LT_BEGIN_AUTO_TEST(public_profiles, retired_callback_state_destroys_outside_publication_lock)
    auto store = std::make_shared<srv::tls_credentials>();
    std::shared_ptr<srv::tls_psk_runtime> runtime;
    LT_ASSERT(srv::tls_psk_runtime::create({1, 0}, runtime).ok());
    std::atomic<bool> retired{false};
    auto state = std::shared_ptr<int>(new int(1), [weak = std::weak_ptr(store), &retired](int* value) {
        delete value;
        if (auto owner = weak.lock()) retired = owner->remove_acme("a.example").ok();
    });
    srv::tls_host_credentials host;
    host.host = "a.example";
    host.profile = srv::tls_profile::external_psk;
    host.psk = srv::tls_psk_config{[state](auto, const auto&) { return srv::tls_psk_result{}; }, runtime};
    LT_ASSERT(store->replace({{host}, 0}).ok());
    state.reset();
    host.psk->lookup = [](auto, const auto&) { return srv::tls_psk_result{}; };
    LT_ASSERT(store->replace({{host}, 0}).ok());
    LT_CHECK(retired.load());
    runtime->request_stop();
    LT_CHECK(runtime->wait_retired(std::chrono::steady_clock::now() + std::chrono::seconds(1)).ok());
LT_END_AUTO_TEST(retired_callback_state_destroys_outside_publication_lock)
LT_BEGIN_AUTO_TEST(public_profiles, rejected_callback_state_destroys_outside_publication_lock)
    auto store = std::make_shared<srv::tls_credentials>();
    LT_ASSERT(store->replace(credentials()).ok());
    srv::native_server server(options(store));
    LT_ASSERT(server.listen().ok());
    const auto original_port = server.get_bound_port(0);
    std::shared_ptr<srv::tls_psk_runtime> runtime;
    LT_ASSERT(srv::tls_psk_runtime::create({1, 0}, runtime).ok());
    auto armed = std::make_shared<std::atomic<bool>>(false);
    auto retired = std::make_shared<std::atomic<unsigned>>(0);
    srv::tls_host_credentials host;
    host.host = "a.example";
    host.profile = srv::tls_profile::external_psk;
    host.psk = srv::tls_psk_config{reentrant_lookup{store, armed, retired}, runtime};
    const srv::tls_credentials_config incompatible{{host}, 0};
    std::mutex mutex;
    std::condition_variable completed;
    bool finished = false;
    std::jthread watchdog([&] {
        std::unique_lock lock(mutex);
        if (!completed.wait_for(lock, std::chrono::seconds(5), [&] { return finished; })) {
            std::fputs("rejected candidate callable destruction blocked publication\n", stderr);
            std::abort();
        }
    });
    *armed = true;
    const auto rejected = store->replace(incompatible);
    *armed = false;
    {
        std::lock_guard lock(mutex);
        finished = true;
    }
    completed.notify_one();
    watchdog.join();
    LT_CHECK(rejected.code() == http::outcome_code::invalid_argument);
    LT_CHECK(retired->load() > 0);
    LT_CHECK(server.get_bound_port(0) == original_port);
    srv::native_server preserved(options(store));
    LT_CHECK(preserved.listen().ok());
    LT_CHECK(store->replace(credentials(srv::tls_client_certificate_mode::request)).code() == http::outcome_code::invalid_argument);
    LT_CHECK(store->replace(credentials()).ok());
    runtime->request_stop();
    LT_CHECK(runtime->wait_retired(std::chrono::steady_clock::now() + std::chrono::seconds(1)).ok());
LT_END_AUTO_TEST(rejected_callback_state_destroys_outside_publication_lock)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
