/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <cstdio>
#include <utility>

#include <httpserver/response_sources.hpp>

namespace httpserver::detail {

http::outcome parse_content_length(const http::fields& f, std::uint64_t& out) {
    out = 0;
    const std::optional<std::string_view> value = f.first("content-length");
    if (!value.has_value()) return http::outcome::okay();
    if (value->empty()) {
        return http::outcome(http::outcome_code::invalid_argument, "response fields: Content-Length is not a number");
    }
    for (const char c : *value) {
        if (c < '0' || c > '9') {
            return http::outcome(http::outcome_code::invalid_argument, "response fields: Content-Length is not a number");
        }
        if (out > (std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(c - '0')) / 10) {
            return http::outcome(http::outcome_code::invalid_argument, "response fields: Content-Length is out of range");
        }
        out = out * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return http::outcome::okay();
}

auto transferred_handle::claim() noexcept -> bool {
    bool idle = false;
    return claimed_.compare_exchange_strong(idle, true, std::memory_order_acq_rel, std::memory_order_relaxed);
}

auto transferred_handle::close() noexcept -> void {
    if (closed_.exchange(true, std::memory_order_acq_rel)) return;
    if (close_) close_(handle_);
}

void fclose_owned(std::FILE* handle) noexcept {
    if (handle != nullptr) std::fclose(handle);
}

send_cursor::~send_cursor() {
    if (owner_ && owns_handle_) owner_->handle->close();
}

auto send_cursor::prepare(std::shared_ptr<const response_body_source> source, const http::fields& framing) -> http::outcome {
    owner_ = std::move(source);
    switch (owner_->kind) {
        case response_source_kind::owned_bytes:
            bind_span(owner_->bytes);
            return http::outcome::okay();
        case response_source_kind::reopen_file:
            return prepare_file(framing);
        case response_source_kind::factory:
            return prepare_factory();
        case response_source_kind::borrowed:
            bind_span(owner_->view);
            return http::outcome::okay();
        case response_source_kind::owned_file:
            return prepare_owned_file(framing);
        case response_source_kind::owned_pipe:
            return prepare_owned_pipe(framing);
    }
    return http::outcome::okay();
}

auto send_cursor::pull() -> body_chunk {
    switch (owner_->kind) {
        case response_source_kind::owned_bytes:
        case response_source_kind::borrowed:
            return pull_span();
        case response_source_kind::reopen_file:
            return pull_file();
        case response_source_kind::factory:
            return pull_factory();
        case response_source_kind::owned_file:
            return pull_handle();
        case response_source_kind::owned_pipe:
            return pull_handle();
    }
    return body_chunk{http::outcome::okay(), {}, true};
}

auto send_cursor::bind_span(std::span<const std::byte> body) noexcept -> void {
    view_ = body;
    offset_ = 0;
    total_ = body.size();
}

auto send_cursor::prepare_file(const http::fields& framing) -> http::outcome {
    file_.open(owner_->path, std::ios::binary);
    if (!file_.is_open()) {
        return http::outcome(http::outcome_code::invalid_argument, "send_definition: cannot reopen file '" + owner_->path + "'");
    }
    file_.seekg(0, std::ios::end);
    const std::streamoff end = file_.tellg();
    file_.seekg(0, std::ios::beg);
    file_size_ = end < 0 ? 0 : static_cast<std::uint64_t>(end);
    remaining_ = file_size_;
    if (framing.count("content-length") > 0) {
        std::uint64_t declared = 0;
        const http::outcome length = parse_content_length(framing, declared);
        if (!length.ok()) return length;
        remaining_ = declared;
    }
    buffer_.assign(k_file_chunk_bytes, std::byte{0});
    return http::outcome::okay();
}

auto send_cursor::prepare_factory() -> http::outcome {
    producer_ = owner_->make();
    if (!producer_) {
        return http::outcome(http::outcome_code::invalid_argument, "send_definition: factory returned an empty producer");
    }
    return http::outcome::okay();
}

auto send_cursor::prepare_owned_file(const http::fields& framing) -> http::outcome {
    if (!owner_->handle->claim()) {
        return http::outcome(http::outcome_code::invalid_state, "send_definition: one-shot source already used");
    }
    owns_handle_ = true;
    std::FILE* const stream = owner_->handle->get();
    // std::ftell is the ISO C probe and returns long by contract —
    // the documented LONG_MAX bound of the probed size.
    const long start = std::ftell(stream);  // NOLINT(runtime/int)
    if (start < 0 || std::fseek(stream, 0, SEEK_END) != 0) {
        return unseekable();
    }
    const long end = std::ftell(stream);  // NOLINT(runtime/int)
    if (end < 0 || std::fseek(stream, start, SEEK_SET) != 0) {
        return unseekable();
    }
    file_size_ = static_cast<std::uint64_t>(end - start);
    remaining_ = file_size_;
    if (framing.count("content-length") > 0) {
        std::uint64_t declared = 0;
        const http::outcome length = parse_content_length(framing, declared);
        if (!length.ok()) return length;
        remaining_ = declared;
    }
    buffer_.assign(k_file_chunk_bytes, std::byte{0});
    return http::outcome::okay();
}

auto send_cursor::prepare_owned_pipe(const http::fields& framing) -> http::outcome {
    if (!owner_->handle->claim()) {
        return http::outcome(http::outcome_code::invalid_state, "send_definition: one-shot source already used");
    }
    owns_handle_ = true;
    remaining_ = k_read_to_eof;
    eof_ends_ = true;
    if (framing.count("content-length") > 0) {
        std::uint64_t declared = 0;
        const http::outcome length = parse_content_length(framing, declared);
        if (!length.ok()) return length;
        remaining_ = declared;
        eof_ends_ = false;
    }
    buffer_.assign(k_file_chunk_bytes, std::byte{0});
    return http::outcome::okay();
}

auto send_cursor::pull_span() -> body_chunk {
    if (offset_ >= total_) {
        return body_chunk{http::outcome::okay(), {}, true};
    }
    const std::byte* const base = view_.data() + offset_;
    const std::size_t rest = static_cast<std::size_t>(total_ - offset_);
    offset_ = total_;
    return body_chunk{http::outcome::okay(), std::span<const std::byte>(base, rest), true};
}

auto send_cursor::pull_file() -> body_chunk {
    if (remaining_ == 0) {
        return body_chunk{http::outcome::okay(), {}, true};
    }
    file_.read(reinterpret_cast<char*>(buffer_.data()), static_cast<std::streamsize>(std::min<std::uint64_t>(remaining_, buffer_.size())));
    const std::streamsize got = file_.gcount();
    // eofbit is set only when fewer characters than requested were
    // available, and the request never exceeds the bound -- so eof
    // (or a zero count) means the file ran out before the pinned
    // length: a short body, typed like the engine's.
    if (got <= 0 || file_.eof()) {
        return body_chunk{http::outcome(http::outcome_code::protocol_error,
                                        "send_definition: body shorter than the declared "
                                        "Content-Length"),
                          {},
                          false};
    }
    remaining_ -= static_cast<std::uint64_t>(got);
    return body_chunk{http::outcome::okay(), std::span<const std::byte>(buffer_.data(), static_cast<std::size_t>(got)), remaining_ == 0};
}

auto send_cursor::pull_factory() -> body_chunk {
    const body_chunk chunk = producer_();
    if (chunk.status.ok() && !chunk.end && chunk.data.empty()) {
        return body_chunk{http::outcome(http::outcome_code::invalid_argument,
                                        "send_definition: producer returned an empty "
                                        "non-end chunk"),
                          {},
                          false};
    }
    return chunk;
}

auto send_cursor::pull_handle() -> body_chunk {
    if (remaining_ == 0) {
        return body_chunk{http::outcome::okay(), {}, true};
    }
    std::FILE* const stream = owner_->handle->get();
    const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(remaining_, buffer_.size()));
    const std::size_t got = std::fread(buffer_.data(), 1, want, stream);
    if (got == want) {
        remaining_ -= got;
        return body_chunk{http::outcome::okay(), std::span<const std::byte>(buffer_.data(), got), remaining_ == 0};
    }
    if (std::ferror(stream) != 0) {
        return body_chunk{http::outcome(http::outcome_code::invalid_state, "send_definition: handle read failed"), {}, false};
    }
    if (eof_ends_ && std::feof(stream) != 0) {
        remaining_ = 0;
        return body_chunk{http::outcome::okay(), std::span<const std::byte>(buffer_.data(), got), true};
    }
    return body_chunk{http::outcome(http::outcome_code::protocol_error,
                                    "send_definition: body shorter than the declared "
                                    "Content-Length"),
                      {},
                      false};
}

auto send_cursor::unseekable() -> http::outcome { return http::outcome(http::outcome_code::invalid_argument, "send_definition: owned_file handle is not seekable"); }

}  // namespace httpserver::detail
