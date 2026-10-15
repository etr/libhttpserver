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
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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
    owned_file,
    owned_pipe,
};

// The close operation riding an owned handle transfer (REQ-028): the
// library owns the handle from the factory on and releases it through
// this callable, exactly once, whatever the send's fate. Move-only
// and transferable, so a custom close can capture state (a descriptor
// pool, a logging hook). The plain-pointer owned_file/owned_pipe
// overloads default to std::fclose.
using owned_close_fn = concurrency::unique_function<void(std::FILE*)>;

namespace detail {

// The per-pull read size for file-backed sources: bounds the send's
// own memory while keeping transport-sized reads.
inline constexpr std::size_t k_file_chunk_bytes = 16u * 1024u;

// The read bound of an UNBOUNDED pipe source (no declared
// Content-Length): a count fread can never satisfy, so the read runs
// to the stream's own EOF.
inline constexpr std::uint64_t k_read_to_eof = std::numeric_limits<std::uint64_t>::max();

// Parses the field set's Content-Length into `out`; absent is ok with
// out = 0. A present but non-numeric or overflowing value fails typed
// (the framing must be a length, not a claim). Lives in this header —
// the cursor's read bound is decided from a declared Content-Length
// here, and response_definition's framing checks reuse it.
http::outcome parse_content_length(const http::fields& f,
                                          std::uint64_t& out);

// A transferred std::FILE* with its one-shot claim and its
// exactly-once close. claim() is the one-shot gate: a compare-and-
// swap that exactly one send ever wins (a second send fails
// invalid_state before writing anything). close() routes every
// release path — cursor destruction (send completed, failed, or
// cancelled) and holder destruction (never sent: the backstop) —
// through one atomic guard, so the close operation fires exactly once
// however the definition dies. A holder cannot die while a cursor
// lives: the cursor's owner block pins the holder.
class transferred_handle {
 public:
    transferred_handle(std::FILE* handle, owned_close_fn close) noexcept
        : handle_(handle),
          close_(std::move(close)) {
    }

    transferred_handle(const transferred_handle&) = delete;
    transferred_handle& operator=(const transferred_handle&) = delete;

    // The backstop closer: an unsent definition's destruction ends
    // here. Idempotent with close(), so a send that already released
    // the handle never double-closes.
    ~transferred_handle() { close(); }

    // The one-shot gate: true for exactly one caller ever.
    bool claim() noexcept;

    // Fires the close operation exactly once.
    void close() noexcept;

    std::FILE* get() const noexcept { return handle_; }

 private:
    std::atomic<bool> claimed_{false};
    std::atomic<bool> closed_{false};
    std::FILE* handle_;
    owned_close_fn close_;
};

// The default close for a plain transferred std::FILE*.
void fclose_owned(std::FILE* handle) noexcept;

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
    // owned_file/owned_pipe: the transferred handle. Deliberately a
    // pointer to a mutable transferred_handle inside an otherwise
    // const frozen block: the one-shot claim and the exactly-once
    // close are per-handle state (the only mutability inside a frozen
    // definition), and no releasable token exists to misuse.
    std::shared_ptr<transferred_handle> handle;
};

// One send's private body state, allocated in send_definition's
// coroutine frame: independence across concurrent sends of one shared
// definition is structural — nothing here is reachable from the
// definition. Declared in this public header for the same reason as
// detail::body_source in body_reader.hpp: send_definition's inline
// body drives it, and a public header may not include a private one.
class send_cursor {
 public:
    // A cursor releases ONLY the handle it claimed: a losing one-shot
    // send closes nothing (its claim never happened), so a racing
    // loser's destruction can never close the handle under the
    // winner's read. A claimed transfer's handle is released here on
    // every send fate: completed, failed, cancelled (the coroutine
    // frame's destruction runs this). close() is idempotent, so the
    // holder's backstop destructor never double-closes.
    ~send_cursor();

    // Binds to `source` (one definition's frozen body block) and takes
    // this send's snapshot of it against the merged framing
    // `framing`: the bytes or leased span aliased (the source pins
    // them alive), the file freshly opened with its read bound taken
    // from an explicit Content-Length when the merged fields carry one
    // (the observed size otherwise), one fresh producer invoked, or
    // the transferred handle claimed (one-shot) and probed.
    http::outcome prepare(std::shared_ptr<const response_body_source> source,
                          const http::fields& framing);

    // The next chunk of this send's body (prepare() must have
    // succeeded): exactly one meaning per body_chunk, spans valid
    // until the next pull on this cursor.
    body_chunk pull();

    // File-backed sources only: the size observed at this send's
    // prepare (the pinned Content-Length); 0 for every other kind.
    std::uint64_t file_size() const noexcept { return file_size_; }

 private:
    // owned_bytes and borrowed share one streaming shape: a frozen
    // span this send walks once — the source's own bytes, or the
    // leased application span.
    void bind_span(std::span<const std::byte> body) noexcept;

    // One fresh private handle per send: concurrent sends of one
    // reopen_file definition never share seek state. The size observed
    // here is this send's pinned Content-Length; an explicit
    // Content-Length in the merged framing becomes the read bound
    // instead, so a file short of its declared length fails that send
    // typed (the ownership matrix's shrink guarantee).
    http::outcome prepare_file(const http::fields& framing);

    // One fresh producer per send; an empty one is a contract
    // violation failed before the head commits.
    http::outcome prepare_factory();

    // owned_file: the one-shot claim FIRST (two racing sends' probes
    // must never interleave), then the size probe — the handle's
    // position at transfer is honored: body = position→EOF. The claim
    // is not refundable: a failed probe (an unseekable handle) leaves
    // the definition spent — a one-shot definition admits exactly one
    // send attempt past validation.
    http::outcome prepare_owned_file(const http::fields& framing);

    // owned_pipe: the one-shot claim, NO probe — a pipe endpoint has
    // no seek position, and transferring it means the bytes feed
    // exactly one send. Without a declared Content-Length the length is
    // unknown: the read runs to the stream's own EOF and nothing is
    // pinned (the framing stays the transport's). A declared length
    // becomes the read bound instead. Like owned_file, the claim is
    // not refundable.
    http::outcome prepare_owned_pipe(const http::fields& framing);

    // owned_bytes / borrowed: the whole remaining span in one chunk
    // carrying end (the writer's internal loop paces it against queue
    // capacity, so backpressure stays the writer's).
    body_chunk pull_span();

    // reopen_file: at most one buffer's worth of the pinned size. A
    // clean EOF with bytes still owed to the pinned Content-Length is
    // a short body (the file shrank between sends): typed
    // protocol_error, mirroring the engine's short-body diagnosis.
    body_chunk pull_file();

    // factory: forwards to this send's producer; an empty non-end
    // chunk is a producer contract violation failed typed (never
    // silently skipped); end and failure pass through verbatim.
    body_chunk pull_factory();

    // owned_file / owned_pipe: at most one buffer's worth of the
    // bounded read. A short read is the stream's own verdict: an I/O
    // failure is invalid_state; EOF on an unbounded pipe (no declared
    // Content-Length) ends the body there; EOF with bytes still owed
    // to the pinned or declared length is a short body
    // (protocol_error, like reopen_file).
    body_chunk pull_handle();

 private:
    // The unseekable-handle diagnosis of owned_file's probe.
    static http::outcome unseekable();

    std::shared_ptr<const response_body_source> owner_;
    bool owns_handle_ = false;     // this send claimed owner_'s handle
    std::span<const std::byte> view_;  // owned_bytes/borrowed: the span
    std::uint64_t offset_ = 0;     // owned_bytes/borrowed span cursor
    std::uint64_t total_ = 0;      // owned_bytes/borrowed span total
    std::uint64_t file_size_ = 0;  // file-backed pinned size at prepare
    std::uint64_t remaining_ = 0;  // file-backed bytes left to the bound
    bool eof_ends_ = false;        // owned_pipe: EOF terminates the body
    std::ifstream file_;           // reopen_file: this send's private handle
    std::vector<std::byte> buffer_;  // file-backed read buffer
    body_producer producer_;       // factory: this send's producer
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_RESPONSE_SOURCES_HPP_
