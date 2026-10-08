/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "io_completion_storage.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_IO_COMPLETION_STORAGE_HPP_
#define SRC_HTTPSERVER_DETAIL_IO_COMPLETION_STORAGE_HPP_

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>
#include <httpserver/detail/io_operation.hpp>

namespace httpserver {
namespace detail {

// A kernel request owns its bytes until its completion packet, independently
// of the operation's logical lifetime. Bounded staging permits partial transfers
// and avoids narrowing size_t to Win32's ULONG or allocating multi-GB buffers.
class owned_completion_storage final {
 public:
    explicit owned_completion_storage(const op_state& op) {
        const auto source = op.kind() == io_op_kind::read
            ? std::span<const std::byte>(std::get<read_payload>(op.payload()).buffer)
            : std::get<write_payload>(op.payload()).bytes;
        bytes_.resize(std::min(source.size(), std::size_t{65536}));
        if (op.kind() == io_op_kind::write && !bytes_.empty()) {
            std::memcpy(bytes_.data(), source.data(), bytes_.size());
        }
    }
    std::byte* data() noexcept { return bytes_.data(); }
    std::size_t size() const noexcept { return bytes_.size(); }

    // Never inspect the borrowed span after cancellation/release won. The
    // backend serializes the claim and this copy before publishing success.
    bool claim_read(op_state& op, std::size_t transferred) const {
        if (!op.claim_terminal()) return false;
        if (transferred != 0) {
            const auto destination = std::get<read_payload>(op.payload()).buffer;
            std::memcpy(destination.data(), bytes_.data(), std::min(transferred, bytes_.size()));
        }
        return true;
    }

 private:
    std::vector<std::byte> bytes_;
};

}  // namespace detail
}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DETAIL_IO_COMPLETION_STORAGE_HPP_
