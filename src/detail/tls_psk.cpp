/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <httpserver/detail/tls_psk.hpp>
#include <algorithm>
#include <utility>
#include <httpserver/detail/secure_zero.hpp>
namespace httpserver::detail {
secure_bytes::secure_bytes(std::span<const std::byte> bytes, release_observer observer, void* argument)
    : data_(std::make_unique<std::byte[]>(bytes.size())), size_(bytes.size()), observer_(observer), argument_(argument) {
    std::copy(bytes.begin(), bytes.end(), data_.get());
}
secure_bytes::~secure_bytes() { clear(); }
secure_bytes::secure_bytes(secure_bytes&& other) noexcept { *this = std::move(other); }
secure_bytes& secure_bytes::operator=(secure_bytes&& other) noexcept {
    if (this != &other) {
        clear();
        data_ = std::move(other.data_);
        size_ = std::exchange(other.size_, 0);
        observer_ = std::exchange(other.observer_, nullptr);
        argument_ = std::exchange(other.argument_, nullptr);
    }
    return *this;
}
void secure_bytes::clear() noexcept {
    secure_zero(data_.get(), size_);
    if (data_ && observer_) observer_(bytes(), argument_);
    data_.reset();
    size_ = 0;
}
}  // namespace httpserver::detail
