/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/http1_body_source.hpp>

namespace httpserver::detail {

auto http1_body_source::feed(std::string_view wire) -> std::size_t {
    body_wait* waiter = nullptr;
    body_wake wake = body_wake::data;
    std::size_t consumed = 0;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const std::size_t staged_before = decoder_.staged_bytes();
        const http1_body_progress p = decoder_.decode(wire);
        consumed = p.consumed;
        if (waiter_ != nullptr) {
            if (p.kind == http1_body_decode::failed) {
                waiter = take_waiter_locked();
                wake = body_wake::failed;
            } else if (decoder_.staged_bytes() > staged_before) {
                waiter = take_waiter_locked();
                wake = body_wake::data;
            } else if (p.kind == http1_body_decode::complete) {
                waiter = take_waiter_locked();
                wake = body_wake::end;
            }
        }
    }
    if (waiter != nullptr) waiter->complete(wake);
    return consumed;
}

auto http1_body_source::cancel_parked() -> void {
    body_wait* waiter = nullptr;
    {
        std::lock_guard<std::mutex> lock(mu_);
        waiter = take_waiter_locked();
    }
    if (waiter != nullptr) waiter->complete(body_wake::cancelled);
}

auto http1_body_source::failed() const noexcept -> bool {
    std::lock_guard<std::mutex> lock(mu_);
    return !decoder_.failure().ok();
}

auto http1_body_source::message_complete() const noexcept -> bool {
    std::lock_guard<std::mutex> lock(mu_);
    return decoder_.message_complete();
}

auto http1_body_source::staged_bytes() const noexcept -> std::size_t {
    std::lock_guard<std::mutex> lock(mu_);
    return decoder_.staged_bytes();
}

auto http1_body_source::length_remaining() const noexcept -> std::uint64_t {
    std::lock_guard<std::mutex> lock(mu_);
    return decoder_.length_remaining();
}

auto http1_body_source::close_policy() const noexcept -> http1_close_policy {
    std::lock_guard<std::mutex> lock(mu_);
    return decoder_.close_policy();
}

auto http1_body_source::parked() const noexcept -> bool {
    std::lock_guard<std::mutex> lock(mu_);
    return waiter_ != nullptr;
}

auto http1_body_source::pull(std::span<std::byte> into) -> body_pull_result {
    body_pull_result result;
    {
        std::lock_guard<std::mutex> lock(mu_);
        result = decoder_.pull(into);
    }
    if (result.kind == body_pull::data && room_released_) room_released_();
    return result;
}

auto http1_body_source::park(body_wait& wait) -> void {
    std::lock_guard<std::mutex> lock(mu_);
    if (!decoder_.failure().ok()) {
        wait.complete(body_wake::failed);
        return;
    }
    if (decoder_.staged_bytes() > 0) {
        wait.complete(body_wake::data);
        return;
    }
    if (decoder_.message_complete()) {
        wait.complete(body_wake::end);
        return;
    }
    if (wait.cancel_token().stop_requested()) {
        wait.complete(body_wake::cancelled);
        return;
    }
    waiter_ = &wait;  // at most one waiter per source
}

auto http1_body_source::unpark(body_wait& wait) -> void {
    std::lock_guard<std::mutex> lock(mu_);
    if (waiter_ == &wait) waiter_ = nullptr;
}

auto http1_body_source::take_waiter_locked() noexcept -> body_wait* {
    body_wait* waiter = waiter_;
    waiter_ = nullptr;
    return waiter;
}

}  // namespace httpserver::detail
