/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fuzz/quic_tls_callbacks_fuzz.hpp"
#include <algorithm>
#include <memory>
#include <optional>
#include "detail/quic_tls_callbacks.hpp"
#include <httpserver/detail/quic_connection_lifecycle.hpp>
#include <httpserver/detail/quic_recovery.hpp>
namespace {
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
using quic_fuzz::require;
using event = quic_fuzz::event;
constexpr auto resource = hs::resource::quic_reassembly_bytes;
constexpr std::array local_parameters{std::byte{15}, std::byte{0}};
constexpr std::array peer_cid{std::byte{7}};
hd::quic_tls_config callback_config() {
    hd::quic_tls_config result;
    result.input_limits = {32, 4, 128};
    result.output_capacity = 8;
    result.receive_lease_capacity = 4;
    result.maximum_peer_parameters = 16;
    result.local_parameters = local_parameters;
    result.peer_cids.initial_source = peer_cid;
    return result;
}
hd::quic_recovery_config recovery_config() {
    hd::quic_recovery_config result;
    result.max_receive_ranges = 4;
    result.max_sent_packets = 4;
    result.max_information = 4;
    result.max_retained_bytes = 64;
    return result;
}
std::uint32_t protection_level(unsigned value) {
    switch (value % 4) {
        case 0: return OSSL_RECORD_PROTECTION_LEVEL_NONE;
        case 1: return OSSL_RECORD_PROTECTION_LEVEL_HANDSHAKE;
        case 2: return OSSL_RECORD_PROTECTION_LEVEL_APPLICATION;
        default: return OSSL_RECORD_PROTECTION_LEVEL_EARLY;
    }
}
struct callback_driver {
    hs::resource_budget budget;
    hs::reservation refusal;
    hd::quic_key_state keys;
    hd::quic_recovery recovery;
    hd::quic_connection_lifecycle lifecycle{std::chrono::milliseconds(0), {}, {}};
    hd::quic_recovery::time_point now{};
    std::unique_ptr<hd::quic_tls_callbacks> callbacks;
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context{SSL_CTX_new(TLS_method()), SSL_CTX_free};
    // Declaration order ensures SSL_free runs before callback storage is destroyed.
    std::unique_ptr<SSL, decltype(&SSL_free)> ssl{nullptr, SSL_free};
    const OSSL_DISPATCH* table = hd::quic_tls_callbacks::dispatch();
    const unsigned char* lease = nullptr;
    std::array<unsigned char, 4> lease_snapshot{};
    std::size_t leased_size = 0;
    unsigned read_level = 0, write_level = 0;
    std::array<std::uint64_t, 3> read_offsets{}, output_offsets{};
    std::array<std::vector<std::byte>, 3> output;
    std::optional<hd::quic_send_plan> pending;
    hd::quic_tls_code failure = hd::quic_tls_code::ok;
    explicit callback_driver(hs::resource_budget root) : budget(root), recovery(recovery_config(), root) {
        require(context != nullptr, "TLS context");
        callbacks = std::make_unique<hd::quic_tls_callbacks>(callback_config(), budget, keys);
        ssl.reset(SSL_new(context.get()));
        require(ssl != nullptr, "valid SSL callback argument");
        SSL_set_accept_state(ssl.get());
        callbacks->install(ssl.get());
        require(keys.install_initial(hd::quic_endpoint_role::server, peer_cid) == hd::quic_crypto_code::ok, "initial keys");
    }
    void check_lease() const {
        if (leased_size) require(std::equal(lease_snapshot.begin(), lease_snapshot.begin() + leased_size, lease), "stable owned receive lease");
    }
    void retire_callbacks() {
        check_lease();
        ssl.reset();  // Provider may release a lease here; state is still alive.
        callbacks.reset();
        lease = nullptr;
        leased_size = 0;
    }
    void retire_recovery() {
        if (pending) recovery.abandon_packet(pending->token);
        pending.reset();
        for (unsigned i = 0; i < 3; ++i) recovery.discard_space(static_cast<hd::quic_pn_space>(i));
        require(!recovery.preparation_pending() && recovery.bytes_in_flight() == 0, "cancel terminal recovery");
    }
    int send(event e) {
        auto bytes = quic_fuzz::payload(e);
        const auto data = std::span(bytes).first(e.c % 17);
        std::size_t consumed = 999;
        const auto result = OSSL_FUNC_SSL_QUIC_TLS_crypto_send(table)(ssl.get(), reinterpret_cast<const unsigned char*>(data.data()), data.size(), &consumed, callbacks.get());
        require(consumed <= data.size() && consumed <= 8 - output[write_level].size(), "send capacity and consumption");
        if (failure != hd::quic_tls_code::ok) require(result == 0 && consumed == 0, "terminal send cannot restart");
        if (result) output[write_level].insert(output[write_level].end(), data.begin(), data.begin() + consumed);
        bytes.fill(std::byte{255});
        return result;
    }
    int receive_record(quic_fuzz::report& report) {
        const unsigned char* data = nullptr;
        std::size_t size = 999;
        const auto result = OSSL_FUNC_SSL_QUIC_TLS_crypto_recv_rcd(table + 1)(ssl.get(), &data, &size, callbacks.get());
        require(size <= 4, "receive lease capacity");
        if (!result) require(data == nullptr && size == 0, "failed receive clears output");
        if (result && size) {
            lease = data;
            leased_size = size;
            std::copy_n(data, size, lease_snapshot.begin());
            for (std::size_t i = 0; i < size; ++i) require(data[i] == (read_offsets[read_level] + i) % 256, "owned ordered callback CRYPTO");
            read_offsets[read_level] += size;
            report.crypto_read += size;
        }
        return result;
    }
    int release(event e) {
        const auto size = e.b == 255 ? leased_size : e.b;
        const auto result = OSSL_FUNC_SSL_QUIC_TLS_crypto_release_rcd(table + 2)(ssl.get(), size, callbacks.get());
        if (result) {
            lease = nullptr;
            leased_size = 0;
        }
        return result;
    }
    int copy(event e) {
        std::array<std::byte, 16> bytes{};
        const auto level = e.a % 3;
        const auto result = callbacks->copy_output(static_cast<hd::quic_crypto_level>(level), e.b, std::span(bytes).first(e.c % 17));
        require(result.bytes <= e.c % 17, "copy output bound");
        if (result) {
            require(e.b >= output_offsets[level], "copy retained prefix");
            const auto offset = e.b - output_offsets[level];
            require(offset + result.bytes <= output[level].size(), "copy retained suffix");
            require(std::equal(bytes.begin(), bytes.begin() + result.bytes, output[level].begin() + offset), "owned output bytes");
        }
        return result ? 1 : 0;
    }
    int retire(event e) {
        const auto level = e.a % 3;
        const auto result = callbacks->retire_output_prefix(static_cast<hd::quic_crypto_level>(level), e.b);
        if (result) {
            require(e.b >= output_offsets[level] && e.b - output_offsets[level] <= output[level].size(), "retire output prefix");
            output[level].erase(output[level].begin(), output[level].begin() + e.b - output_offsets[level]);
            output_offsets[level] = e.b;
        }
        return result ? 1 : 0;
    }
    int secret(event e, bool dispatch) {
        constexpr std::array<unsigned, 4> suites{0x1301, 0x1302, 0x1303, 0xffff};
        std::array<std::byte, 48> bytes{};
        bytes.fill(std::byte{7});
        auto size = e.c % 4 == 1 ? 48U : 32U;
        if (e.a % 4 == 0) size = 0;
        if (e.c & 128) size = 31;
        const auto level = protection_level(e.a);
        const auto direction = e.b % 3;
        const auto result = dispatch ? OSSL_FUNC_SSL_QUIC_TLS_yield_secret(table + 3)(ssl.get(), level, direction,
            reinterpret_cast<const unsigned char*>(bytes.data()), size, callbacks.get())
            : callbacks->secret(level, direction, suites[e.c % 4], std::span(bytes).first(size));
        if (result && e.a % 4 != 0) {
            require(direction < 2 && e.a % 4 < 3, "accepted secret level/direction");
            auto& current = direction ? write_level : read_level;
            current = e.a % 4;
            require(keys.keys(static_cast<hd::quic_key_level>(current), direction ? hd::quic_key_direction::write : hd::quic_key_direction::read) != nullptr, "accepted secret installs key");
        }
        bytes.fill(std::byte{255});
        return result;
    }
    int parameters(event e) {
        std::array<unsigned char, 6> bytes{15, 1, 7, 15, 1, 7};
        std::size_t size = 3;
        if (e.b % 4 == 1) size = 6;
        if (e.b % 4 == 2) size = 0;
        if (e.b % 4 == 3) bytes[2] = 8;
        const auto result = OSSL_FUNC_SSL_QUIC_TLS_got_transport_params(table + 4)(ssl.get(), bytes.data(), size, callbacks.get());
        bytes.fill(255);
        if (result) require(callbacks->peer_parameters().size() == 3 && callbacks->peer_parameters()[2] == std::byte{7}, "owned validated parameters");
        return result;
    }
    int close(event e, quic_fuzz::report& report) {
        const hd::quic_close_frame frame{false, e.b, 0, {}};
        const auto changed = e.a % 2 == 0 ? lifecycle.local_close(frame, now, recovery.current_pto()) : lifecycle.peer_close(frame, now, recovery.current_pto());
        if (changed && report.close_notifications == 0) {
            ++report.close_notifications;
            retire_callbacks();
        }
        if (!pending && lifecycle.state() != hd::quic_lifecycle_state::active) retire_recovery();
        if (lifecycle.state() != hd::quic_lifecycle_state::closing) keys.clear();
        return changed ? 1 : 0;
    }
    int apply(event e, quic_fuzz::report& report) {
        if (e.kind == 10) return close(e, report);
        if (e.kind == 11) {
            now += std::chrono::milliseconds(1 + e.b);
            const auto changed = lifecycle.expire(now, recovery.current_pto());
            if (lifecycle.state() == hd::quic_lifecycle_state::closed) {
                retire_recovery();
                keys.clear();
            }
            return changed ? 1 : 0;
        }
        if (e.kind == 17) {
            if (!pending) return 0;
            auto result = e.a & 1 ? recovery.commit_sent(pending->token, now, 100, true, true) : recovery.abandon_packet(pending->token);
            if (result) pending.reset();
            if (lifecycle.state() != hd::quic_lifecycle_state::active) retire_recovery();
            return result ? 1 : 0;
        }
        if (e.kind == 18) {
            retire_callbacks();
            retire_recovery();
            keys.clear();
            lifecycle.stateless_reset(now, recovery.current_pto());
            return 1;
        }
        if (!callbacks) {
            ++report.rejected_queue;
            return 0;
        }
        switch (e.kind) {
            case 0: {
                auto bytes = quic_fuzz::payload(e);
                const auto result = callbacks->receive(static_cast<hd::quic_crypto_level>(e.a % 4), e.b, std::span(bytes).first(e.c % 17));
                bytes.fill(std::byte{255});
                return result ? 1 : 0;
            }
            case 1: return send(e);
            case 2: return receive_record(report);
            case 3: return release(e);
            case 4: return copy(e);
            case 5: return retire(e);
            case 6: return secret(e, false);
            case 7: return parameters(e);
            case 8: return OSSL_FUNC_SSL_QUIC_TLS_alert(table + 5)(ssl.get(), e.b, callbacks.get());
            case 9: return callbacks->fail(hd::quic_tls_code::callback_error);
            case 12: {
                refusal.release();
                return budget.reserve(resource, budget.capacity(resource) - budget.in_use(resource), refusal).ok() ? 1 : 0;
            }
            case 13: refusal.release(); return 1;
            case 14: return secret(e, true);
            case 15: return callbacks->receive(hd::quic_crypto_level::initial, e.b, {} ) ? 1 : 0;
            case 16: {
                const auto plan = recovery.reserve_packet(static_cast<hd::quic_pn_space>(e.a % 3));
                if (plan) pending = plan;
                return plan ? 1 : 0;
            }
            default: return 0;
        }
    }
    void step(event e, quic_fuzz::report& report) {
        e.kind %= 19;
        ++report.attempted[e.kind];
        check_lease();
        const auto result = apply(e, report);
        if (result) ++report.accepted[e.kind];
        if (callbacks) {
            if (failure != hd::quic_tls_code::ok) require(callbacks->failure().code == failure, "sticky callback terminal error");
            failure = callbacks->failure().code;
        }
        check_lease();
        require(budget.in_use(resource) <= budget.capacity(resource) && report.close_notifications <= 1, "callback accounting/terminal bounds");
        require(recovery.preparation_pending() == pending.has_value(), "callback owner pending transaction");
        report.trace.insert(report.trace.end(), {e.kind, static_cast<unsigned>(result), static_cast<unsigned>(failure), budget.in_use(resource),
            leased_size, read_level, write_level, static_cast<unsigned>(lifecycle.state()), recovery.bytes_in_flight()});
    }
};
quic_fuzz::report replay(std::span<const std::uint8_t> input) {
    hs::budget_limits limits;
    limits.set(resource, 131072);
    auto budget = hs::resource_budget::root(limits);
    quic_fuzz::report result;
    {
        callback_driver driver(budget);
        result = quic_fuzz::drive(input, [&](auto e, auto& r) { driver.step(e, r); });
        driver.retire_callbacks();
        driver.retire_recovery();
        driver.keys.clear();
        for (unsigned i = 0; i < 3; ++i) require(!driver.keys.keys(static_cast<hd::quic_key_level>(i), hd::quic_key_direction::read)
            && !driver.keys.keys(static_cast<hd::quic_key_level>(i), hd::quic_key_direction::write), "terminal key disposal");
    }
    require(budget.in_use(resource) == 0, "all callback reservations released");
    result.released = true;
    return result;
}
}  // namespace
quic_fuzz::report quic_tls_callbacks_fuzz_input(std::span<const std::uint8_t> input) {
    auto result = replay(input);
    require(result == replay(input), "deterministic callback event replay");
    return result;
}
#ifdef QUIC_TLS_CALLBACKS_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    quic_tls_callbacks_fuzz_input({data, size});
    return 0;
}
#endif
