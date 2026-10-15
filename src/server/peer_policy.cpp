/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/peer_policy.hpp>

namespace httpserver::server {

peer_policy::peer_policy() : impl_(detail::peer_policy_create()) {}

auto peer_policy::deny(std::string_view pattern) -> http::outcome { return detail::peer_policy_denied_add(impl_, pattern); }

auto peer_policy::allow(std::string_view pattern) -> http::outcome { return detail::peer_policy_allowed_add(impl_, pattern); }

auto peer_policy::remove_denied(std::string_view pattern) -> http::outcome { return detail::peer_policy_denied_drop(impl_, pattern); }

auto peer_policy::remove_allowed(std::string_view pattern) -> http::outcome { return detail::peer_policy_allowed_drop(impl_, pattern); }

auto peer_policy::set_mode(peer_policy_mode mode) noexcept -> void { detail::peer_policy_set_mode(impl_, mode); }

auto peer_policy::mode() const noexcept -> peer_policy_mode { return detail::peer_policy_get_mode(impl_); }

auto peer_policy::set_enabled(bool enabled) noexcept -> void { detail::peer_policy_set_enabled(impl_, enabled); }

auto peer_policy::enabled() const noexcept -> bool { return detail::peer_policy_get_enabled(impl_); }

auto peer_policy::armed() const noexcept -> bool { return detail::peer_policy_armed(impl_); }

auto peer_policy::classify(const net::peer_address& peer) const -> peer_verdict { return detail::peer_policy_classify(impl_, peer); }

}  // namespace httpserver::server
