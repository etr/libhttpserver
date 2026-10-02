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

// The bounded nonce/nc replay ledger (TASK-115, plan section 3; the
// v2 nonce_nc_size analogue). One map from nonce to {expiry, last_nc},
// mutex-guarded; the ONLY mutable surface is admit(), so every read
// of an existing nonce is a read-modify-write that never evicts.
//
// The nc rule (RFC 7616 section 3.4.2 made strict): a presented nc
// must be strictly greater than the last admitted one for that nonce
// -- gaps are allowed, reuse is a replay. A slot's counter advances
// at ADMISSION, before password verification: a failed guess burns
// its nc, so an eavesdropper cannot retry the same (nonce, nc) pair
// while the password check runs. qop-absent (RFC 2617 legacy)
// requests ride the same rule with an implicit nc of 1.
//
// Bound: when a NEW nonce arrives at capacity, expired slots are
// erased first, then the smallest-expiry survivor. Eviction can only
// drop replay protection for nonces the TTL has already retired (or
// the soonest-to-retire one) -- never a live slot out of turn.
//
// max_nc (0 = unbounded, the k_no_nc_limit sentinel) is the use-count
// ceiling per nonce: once last_nc has reached it, every further use
// is exhausted (the policy's stale_nonce: re-challenge with a fresh
// nonce, stale=TRUE).

#if !defined(HTTPSERVER_COMPILATION)
#error "digest_ledger.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_DIGEST_LEDGER_HPP_
#define SRC_HTTPSERVER_DETAIL_DIGEST_LEDGER_HPP_

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace httpserver {

namespace detail {

namespace digest {

enum class nc_admission : std::uint8_t {
    admitted,   // strictly-increasing nc; slot advanced
    replayed,   // nc not greater than the last admitted one
    exhausted,  // last_nc reached max_nc: nonce used up
};

class digest_ledger {
 public:
    // max_nc value meaning "no use-count ceiling".
    static constexpr std::uint32_t k_no_nc_limit = 0;

    // capacity must be at least 1 (the policy factory refuses zero;
    // the class treats a 0 capacity as untracked insertions rather
    // than undefined behavior).
    explicit digest_ledger(std::size_t capacity)
        : capacity_(capacity) {}

    digest_ledger(const digest_ledger&) = delete;
    digest_ledger& operator=(const digest_ledger&) = delete;

    // Admits one (nonce, nc) observation taken at @p now_unix. A new
    // nonce is inserted with its expiry; an existing one only has its
    // counter advanced. Thread-safe (one mutex, no callbacks held
    // under the lock).
    nc_admission admit(const std::string& nonce, std::uint64_t now_unix,
                       std::uint64_t expires_unix, std::uint32_t nc,
                       std::uint32_t max_nc) {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto found = slots_.find(nonce);
        if (found != slots_.end()) {
            if (max_nc != k_no_nc_limit
                && found->second.last_nc >= max_nc) {
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

    std::size_t size() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return slots_.size();
    }

 private:
    struct slot {
        std::uint64_t expires_unix = 0;
        std::uint32_t last_nc = 0;
    };

    // Erases room for one insertion: expired slots first, then the
    // smallest-expiry survivor. Caller holds the mutex.
    void make_room_locked(std::uint64_t now_unix) {
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
                if (it->second.expires_unix
                    < victim->second.expires_unix) {
                    victim = it;
                }
            }
            slots_.erase(victim);
        }
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::string, slot> slots_;
    std::size_t capacity_;
};

}  // namespace digest

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_DIGEST_LEDGER_HPP_
