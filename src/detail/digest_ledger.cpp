/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/digest_ledger.hpp>
#include <string>

namespace httpserver::detail::digest {

auto digest_ledger::admit(const std::string& nonce, std::uint64_t now_unix, std::uint64_t expires_unix, std::uint32_t nc, std::uint32_t max_nc) -> nc_admission {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = slots_.find(nonce);
    if (found != slots_.end()) {
        if (max_nc != k_no_nc_limit && found->second.last_nc >= max_nc) {
            return nc_admission::exhausted;
        }
        if (nc <= found->second.last_nc) {
            return nc_admission::replayed;
        }
        found->second.last_nc = nc;
        return nc_admission::admitted;
    }
    make_room_locked(now_unix);
    slots_.emplace(nonce, slot{expires_unix, nc});
    return nc_admission::admitted;
}

auto digest_ledger::size() const -> std::size_t {
    const std::lock_guard<std::mutex> lock(mutex_);
    return slots_.size();
}

auto digest_ledger::make_room_locked(std::uint64_t now_unix) -> void {
    if (capacity_ == 0 || slots_.size() < capacity_) return;
    for (auto it = slots_.begin(); it != slots_.end();) {
        if (it->second.expires_unix < now_unix) {
            it = slots_.erase(it);
        } else {
            ++it;
        }
    }
    while (slots_.size() >= capacity_) {
        auto victim = slots_.begin();
        for (auto it = slots_.begin(); it != slots_.end(); ++it) {
            if (it->second.expires_unix < victim->second.expires_unix) {
                victim = it;
            }
        }
        slots_.erase(victim);
    }
}

}  // namespace httpserver::detail::digest
