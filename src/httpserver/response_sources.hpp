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
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// Response body sources and their ownership (architecture §3.2,
// DR-V3-005, PRD-V3N-REQ-026/028): the vocabulary a response
// definition's body is made of — pull chunks, per-send producers and
// their factories, the source-kind taxonomy, and the per-send cursor
// that streams one source. Split from response_definition.hpp
// (TASK-113) so the definition value and the body-source machinery
// can grow independently without either header passing the per-file
// size gate.
//
// The send cursor is declared in this public header for the same
// reason detail::body_source lives in body_reader.hpp: the
// response-definition header's inline send path drives it, and a
// public header may not include a private one. It is library
// plumbing, not consumer surface.

#ifndef SRC_HTTPSERVER_RESPONSE_SOURCES_HPP_
#define SRC_HTTPSERVER_RESPONSE_SOURCES_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/concurrency/executor.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver {

// One pull from a response body producer (REQ-026). Exactly one of the
// three members carries meaning:
//   - !status.ok(): the body failed; the producer is finished;
//   - data (status ok): valid until the NEXT pull on that producer;
//     MUST be non-empty while !end (an empty non-end chunk is a
//     producer contract violation; the send fails it typed);
//   - end (status ok): the body is complete once this chunk's data
//     (if any) has been consumed — a cursor may carry the final data
//     and end together in one chunk.
struct body_chunk {
    http::outcome status;
    std::span<const std::byte> data;
    bool end = false;
};

// A fresh per-send producer returned by a definition's factory. Used
// by exactly one send; not thread-safe.
using body_producer = concurrency::unique_function<body_chunk()>;

// Creates one new producer per send. Must be safe to invoke
// concurrently (one invocation per in-flight send).
using body_factory = concurrency::unique_function<body_producer()>;

// A lifetime lease for borrowed body memory (REQ-028, DR-V3-005): one
// shared keeper pinning the application's buffer alive. The
// definition stores the lease beside the borrowed span, so every send
// — and the frozen definition itself — provably outlives the memory:
// releasing the last definition releases the keeper. Application
// contract: while the definition lives, the keeper keeps
// [data, data + size) unchanged and alive (that immutability is what
// makes a borrowed body replayable, unlike a one-shot pipe).
//
// Interpretation note (§3.2 / DR-V3-005): "borrowed memory requires a
// lease spanning send completion" combined with "reject sharing a
// borrowed body without a valid lease" reads coherently only if a
// LEASED borrowed body may be shared across sends; this class encodes
// that reading. The lease is structural, not advisory — there is no
// releasable token, so the invariant cannot be violated.
class body_lease {
 public:
    body_lease() noexcept = default;

    // Pins `keeper` (typically the buffer's owner) for the
    // definition's lifetime.
    template <typename T>
    explicit body_lease(std::shared_ptr<T> keeper) noexcept
        : keeper_(std::move(keeper)) {
    }

    // True iff the lease pins something. borrowed() rejects an empty
    // lease before a definition exists.
    bool valid() const noexcept { return keeper_ != nullptr; }

 private:
    std::shared_ptr<const void> keeper_;
};

// The source-kind taxonomy of a response body (REQ-026/028): what a
// definition froze at factory time, and therefore how a send must
// treat it. response_definition re-exports this as
// response_definition::source_kind (the TASK-112 spelling).
enum class response_source_kind : std::uint8_t {
    owned_bytes,
    reopen_file,
    factory,
    borrowed,
};

namespace detail {

// The per-pull read size for file-backed sources: bounds the send's
// own memory while keeping transport-sized reads.
inline constexpr std::size_t k_file_chunk_bytes = 16u * 1024u;

// Parses the field set's Content-Length into `out`; absent is ok with
// out = 0. A present but non-numeric or overflowing value fails typed
// (the framing must be a length, not a claim). Lives in this header —
// the cursor's read bound is decided from a declared Content-Length
// here, and response_definition's framing checks reuse it.
inline http::outcome parse_content_length(const http::fields& f,
                                          std::uint64_t& out) {
    out = 0;
    const std::optional<std::string_view> value =
        f.first("content-length");
    if (!value.has_value()) return http::outcome::okay();
    if (value->empty()) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "response fields: Content-Length is not a number");
    }
    for (const char c : *value) {
        if (c < '0' || c > '9') {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "response fields: Content-Length is not a number");
        }
        if (out > (std::numeric_limits<std::uint64_t>::max()
                   - static_cast<std::uint64_t>(c - '0')) / 10) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "response fields: Content-Length is out of range");
        }
        out = out * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return http::outcome::okay();
}

// The frozen body half of one definition: the kind plus exactly the
// state that kind needs (a response_definition's impl holds its
// status, its base fields, and one shared immutable block of this
// shape). Decoupled from the definition class so the send cursor pins
// the body state directly — a send in flight keeps its source alive
// even if the definition value is destroyed — and so this header
// compiles without the definition's type.
struct response_body_source {
    response_source_kind kind = response_source_kind::owned_bytes;
    std::vector<std::byte> bytes;   // owned_bytes
    std::string path;               // reopen_file
    body_factory make;              // factory
    std::span<const std::byte> view;  // borrowed: the application body
    body_lease lease;               // borrowed: pins view alive
};

// One send's private body state, allocated in send_definition's
// coroutine frame: independence across concurrent sends of one shared
// definition is structural — nothing here is reachable from the
// definition. Declared in this public header for the same reason as
// detail::body_source in body_reader.hpp: send_definition's inline
// body drives it, and a public header may not include a private one.
class send_cursor {
 public:
    // Binds to `source` (one definition's frozen body block) and takes
    // this send's snapshot of it against the merged framing
    // `framing`: the bytes or leased span aliased (the source pins
    // them alive), the file freshly opened with its read bound taken
    // from an explicit Content-Length when the merged fields carry one
    // (the observed size otherwise), or one fresh producer invoked.
    http::outcome prepare(std::shared_ptr<const response_body_source> source,
                          const http::fields& framing) {
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
        }
        return http::outcome::okay();
    }

    // The next chunk of this send's body (prepare() must have
    // succeeded): exactly one meaning per body_chunk, spans valid
    // until the next pull on this cursor.
    body_chunk pull() {
        switch (owner_->kind) {
            case response_source_kind::owned_bytes:
            case response_source_kind::borrowed:
                return pull_span();
            case response_source_kind::reopen_file:
                return pull_file();
            case response_source_kind::factory:
                return pull_factory();
        }
        return body_chunk{http::outcome::okay(), {}, true};
    }

    // reopen_file only: the size observed at this send's prepare (the
    // pinned Content-Length); 0 for every other kind.
    std::uint64_t file_size() const noexcept { return file_size_; }

 private:
    // owned_bytes and borrowed share one streaming shape: a frozen
    // span this send walks once — the source's own bytes, or the
    // leased application span.
    void bind_span(std::span<const std::byte> body) noexcept {
        view_ = body;
        offset_ = 0;
        total_ = body.size();
    }

    // One fresh private handle per send: concurrent sends of one
    // reopen_file definition never share seek state. The size observed
    // here is this send's pinned Content-Length; an explicit
    // Content-Length in the merged framing becomes the read bound
    // instead, so a file short of its declared length fails that send
    // typed (the ownership matrix's shrink guarantee).
    http::outcome prepare_file(const http::fields& framing) {
        file_.open(owner_->path, std::ios::binary);
        if (!file_.is_open()) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "send_definition: cannot reopen file '" + owner_->path
                    + "'");
        }
        file_.seekg(0, std::ios::end);
        const std::streamoff end = file_.tellg();
        file_.seekg(0, std::ios::beg);
        file_size_ = end < 0 ? 0 : static_cast<std::uint64_t>(end);
        remaining_ = file_size_;
        if (framing.count("content-length") > 0) {
            std::uint64_t declared = 0;
            const http::outcome length =
                parse_content_length(framing, declared);
            if (!length.ok()) return length;
            remaining_ = declared;
        }
        buffer_.assign(k_file_chunk_bytes, std::byte{0});
        return http::outcome::okay();
    }

    // One fresh producer per send; an empty one is a contract
    // violation failed before the head commits.
    http::outcome prepare_factory() {
        producer_ = owner_->make();
        if (!producer_) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "send_definition: factory returned an empty producer");
        }
        return http::outcome::okay();
    }

    // owned_bytes / borrowed: the whole remaining span in one chunk
    // carrying end (the writer's internal loop paces it against queue
    // capacity, so backpressure stays the writer's).
    body_chunk pull_span() {
        if (offset_ >= total_) {
            return body_chunk{http::outcome::okay(), {}, true};
        }
        const std::byte* const base = view_.data() + offset_;
        const std::size_t rest =
            static_cast<std::size_t>(total_ - offset_);
        offset_ = total_;
        return body_chunk{http::outcome::okay(),
                          std::span<const std::byte>(base, rest), true};
    }

    // reopen_file: at most one buffer's worth of the pinned size. A
    // clean EOF with bytes still owed to the pinned Content-Length is
    // a short body (the file shrank between sends): typed
    // protocol_error, mirroring the engine's short-body diagnosis.
    body_chunk pull_file() {
        if (remaining_ == 0) {
            return body_chunk{http::outcome::okay(), {}, true};
        }
        file_.read(reinterpret_cast<char*>(buffer_.data()),
                   static_cast<std::streamsize>(std::min<std::uint64_t>(
                       remaining_, buffer_.size())));
        const std::streamsize got = file_.gcount();
        // eofbit is set only when fewer characters than requested were
        // available, and the request never exceeds the bound -- so eof
        // (or a zero count) means the file ran out before the pinned
        // length: a short body, typed like the engine's.
        if (got <= 0 || file_.eof()) {
            return body_chunk{
                http::outcome(
                    http::outcome_code::protocol_error,
                    "send_definition: body shorter than the declared "
                    "Content-Length"),
                {}, false};
        }
        remaining_ -= static_cast<std::uint64_t>(got);
        return body_chunk{
            http::outcome::okay(),
            std::span<const std::byte>(buffer_.data(),
                                       static_cast<std::size_t>(got)),
            remaining_ == 0};
    }

    // factory: forwards to this send's producer; an empty non-end
    // chunk is a producer contract violation failed typed (never
    // silently skipped); end and failure pass through verbatim.
    body_chunk pull_factory() {
        const body_chunk chunk = producer_();
        if (chunk.status.ok() && !chunk.end && chunk.data.empty()) {
            return body_chunk{
                http::outcome(
                    http::outcome_code::invalid_argument,
                    "send_definition: producer returned an empty "
                    "non-end chunk"),
                {}, false};
        }
        return chunk;
    }

    std::shared_ptr<const response_body_source> owner_;
    std::span<const std::byte> view_;  // owned_bytes/borrowed: the span
    std::uint64_t offset_ = 0;     // owned_bytes/borrowed span cursor
    std::uint64_t total_ = 0;      // owned_bytes/borrowed span total
    std::uint64_t file_size_ = 0;  // reopen_file pinned size
    std::uint64_t remaining_ = 0;  // reopen_file bytes left to pinned size
    std::ifstream file_;           // reopen_file: this send's private handle
    std::vector<std::byte> buffer_;  // reopen_file read buffer
    body_producer producer_;       // factory: this send's producer
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_RESPONSE_SOURCES_HPP_
