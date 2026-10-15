/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <memory>
#include <utility>
#include <httpserver/detail/quic_tls_session.hpp>
#include <httpserver/detail/diagnostics_access.hpp>
#include <httpserver/detail/quic_tls_callbacks.hpp>
namespace httpserver::detail {
struct quic_tls_session::impl {
    quic_tls_callbacks callbacks;
    server::diagnostics diagnostics;
    std::uint64_t connection_id;
    bool handshake_recorded = false;
    // Destroy the SSL session before the callback state and retained buffers.
    std::unique_ptr<tls_session> tls;
    impl(const quic_tls_config& config, server::resource_budget budget, quic_key_state& keys)
        : callbacks(config, std::move(budget), keys), diagnostics(config.diagnostics), connection_id(config.connection_id) {}
    tls_session::result record(tls_session::result result) {
        if (result.state == tls_session::progress::failed || result.state == tls_session::progress::eof) callbacks.fail(quic_tls_code::provider_error);
        if (callbacks.failure().code != quic_tls_code::ok) result = {tls_session::progress::failed};
        const bool completed = result.state == tls_session::progress::complete;
        const bool failed = result.state == tls_session::progress::failed;
        if (!handshake_recorded && (completed || failed)) {
            handshake_recorded = true;
            server::diagnostic_event event{completed ? server::diagnostic_kind::tls_completed : server::diagnostic_kind::tls_failed, server::diagnostic_reason::handshake,
                                           completed ? http::outcome_code::ok : http::outcome_code::protocol_error};
            event.protocol = http::protocol::http_3;
            event.connection_id = connection_id;
            diagnostics_access::record(diagnostics, event);
        }
        return result;
    }
};
quic_tls_session::quic_tls_session(tls_credentials_selection selection, const quic_tls_config& config,
                                 server::resource_budget budget, quic_key_state& keys)
    : impl_(std::make_unique<impl>(config, std::move(budget), keys)) {
    impl_->tls.reset(new tls_session(std::move(selection), true, {tls_transport::quic, config.listener_port}, &impl_->callbacks));
}
quic_tls_session::quic_tls_session(tls_credentials_selection selection, const quic_tls_config& config, quic_storage_lease storage, quic_key_state& keys)
    : quic_tls_session(std::move(selection), config, storage.budget, keys) { storage_owner_ = std::move(storage); }
quic_tls_session::~quic_tls_session() = default;
quic_tls_result quic_tls_session::receive(quic_crypto_level level, std::uint64_t offset, std::span<const std::byte> bytes) {
    auto result = impl_->callbacks.receive(level, offset, bytes);
    if (impl_->callbacks.failure().code != quic_tls_code::ok) impl_->record({tls_session::progress::failed});
    return result;
}
tls_session::result quic_tls_session::handshake() {
    if (failure().code != quic_tls_code::ok) return impl_->record({tls_session::progress::failed});
    return impl_->record(impl_->tls->handshake());
}
tls_session::result quic_tls_session::process_post_handshake() {
    if (failure().code != quic_tls_code::ok) return impl_->record({tls_session::progress::failed});
#if defined(LIBHTTPSERVER_QUIC_INTEROP_TEST_PROVIDER)
    // The test provider's only additional QUIC ALPN is the gated hq-interop token.
    if (negotiated_protocol() != tls_negotiated_protocol::h3 && negotiated_protocol() != tls_negotiated_protocol::other) return {tls_session::progress::failed};
#else
    if (negotiated_protocol() != tls_negotiated_protocol::h3) return {tls_session::progress::failed};
#endif
    return impl_->record(impl_->tls->process_post_handshake());
}
quic_tls_result quic_tls_session::copy_output(quic_crypto_level level, std::uint64_t offset, std::span<std::byte> destination) const {
    return impl_->callbacks.copy_output(level, offset, destination);
}
quic_tls_result quic_tls_session::retire_output_prefix(quic_crypto_level level, std::uint64_t through) {
    return impl_->callbacks.retire_output_prefix(level, through);
}
std::span<const std::byte> quic_tls_session::peer_transport_parameter_bytes() const { return impl_->callbacks.peer_parameters(); }
tls_negotiated_protocol quic_tls_session::negotiated_protocol() const {
    return failure().code == quic_tls_code::ok ? impl_->tls->negotiated_protocol() : tls_negotiated_protocol::unknown;
}
std::shared_ptr<const server::tls_peer_metadata> quic_tls_session::peer_metadata() const {
    return failure().code == quic_tls_code::ok ? impl_->tls->peer_metadata() : nullptr;
}
quic_tls_failure quic_tls_session::failure() const { return impl_->callbacks.failure(); }
}  // namespace httpserver::detail
