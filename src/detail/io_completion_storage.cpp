/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <httpserver/detail/io_completion_storage.hpp>

namespace httpserver::detail {

owned_completion_storage::owned_completion_storage(const op_state& op) {
    const auto source = op.kind() == io_op_kind::read ? std::span<const std::byte>(std::get<read_payload>(op.payload()).buffer) : std::get<write_payload>(op.payload()).bytes;
    bytes_.resize(std::min(source.size(), std::size_t{65536}));
    if (op.kind() == io_op_kind::write && !bytes_.empty()) {
        std::memcpy(bytes_.data(), source.data(), bytes_.size());
    }
}

auto owned_completion_storage::claim_read(op_state& op, std::size_t transferred) const -> bool {
    if (!op.claim_terminal()) return false;
    if (transferred != 0) {
        const auto destination = std::get<read_payload>(op.payload()).buffer;
        std::memcpy(destination.data(), bytes_.data(), std::min(transferred, bytes_.size()));
    }
    return true;
}

}  // namespace httpserver::detail
