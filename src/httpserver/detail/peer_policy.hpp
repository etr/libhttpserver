/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "peer_policy.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_PEER_POLICY_HPP_
#define SRC_HTTPSERVER_DETAIL_PEER_POLICY_HPP_
#include <httpserver/server/peer_policy.hpp>
#include <memory>
namespace httpserver::server::detail {
// TU-defined bridges over the incomplete pimpl: the header stays
// vocabulary-only, the storage lives in the library.
std::shared_ptr<peer_policy_impl> peer_policy_create();
http::outcome peer_policy_denied_add(
    const std::shared_ptr<peer_policy_impl>& owner, std::string_view text);
http::outcome peer_policy_allowed_add(
    const std::shared_ptr<peer_policy_impl>& owner, std::string_view text);
http::outcome peer_policy_denied_drop(
    const std::shared_ptr<peer_policy_impl>& owner, std::string_view text);
http::outcome peer_policy_allowed_drop(
    const std::shared_ptr<peer_policy_impl>& owner, std::string_view text);
void peer_policy_set_mode(const std::shared_ptr<peer_policy_impl>& owner,
                          peer_policy_mode mode) noexcept;
peer_policy_mode peer_policy_get_mode(
    const std::shared_ptr<peer_policy_impl>& owner) noexcept;
void peer_policy_set_enabled(
    const std::shared_ptr<peer_policy_impl>& owner, bool enabled) noexcept;
bool peer_policy_get_enabled(
    const std::shared_ptr<peer_policy_impl>& owner) noexcept;
bool peer_policy_armed(
    const std::shared_ptr<peer_policy_impl>& owner) noexcept;
peer_verdict peer_policy_classify(
    const std::shared_ptr<peer_policy_impl>& owner,
    const net::peer_address& peer);

}  // namespace httpserver::server::detail
#endif  // SRC_HTTPSERVER_DETAIL_PEER_POLICY_HPP_
