/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <vector>
#include <httpserver/detail/http3_body_stream.hpp>
namespace httpserver::detail {
bool http3_body_stream::receive(std::span<const std::byte> bytes) {
    return rings_.receive({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()}, false);
}
body_pull_result http3_body_stream::pull(std::span<std::byte> into) {
    auto result = rings_.pull(into);
    if (result.kind == body_pull::data && consumed_) consumed_(result.copied);
    return result;
}
std::vector<std::byte> http3_body_stream::send_prefix(std::size_t size) const {
    auto prefix = rings_.send_prefix(size);
    auto bytes = std::as_bytes(std::span(prefix));
    return {bytes.begin(), bytes.end()};
}
}  // namespace httpserver::detail
