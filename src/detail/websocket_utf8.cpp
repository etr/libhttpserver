/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/websocket_utf8.hpp>

namespace httpserver::detail {

auto websocket_utf8::push(unsigned byte) noexcept -> bool {
    if (remaining_) {
        if (byte < lower_ || byte > upper_) return false;
        --remaining_;
        lower_ = 0x80;
        upper_ = 0xbf;
        return true;
    }
    if (byte <= 0x7f) return true;
    return lead(byte);
}

auto websocket_utf8::valid(std::span<const std::byte> data) noexcept -> bool {
    websocket_utf8 v;
    for (auto b : data)
        if (!v.push(std::to_integer<unsigned>(b))) return false;
    return v.complete();
}

auto websocket_utf8::lead(unsigned b) noexcept -> bool {
    lower_ = 0x80;
    upper_ = 0xbf;
    if (b < 0xc2) return false;
    if (b <= 0xdf) {
        remaining_ = 1;
        return true;
    }
    if (b <= 0xef) {
        remaining_ = 2;
        if (b == 0xe0) lower_ = 0xa0;
        if (b == 0xed) upper_ = 0x9f;
        return true;
    }
    if (b <= 0xf4) {
        remaining_ = 3;
        if (b == 0xf0) lower_ = 0x90;
        if (b == 0xf4) upper_ = 0x8f;
        return true;
    }
    return false;
}

}  // namespace httpserver::detail
