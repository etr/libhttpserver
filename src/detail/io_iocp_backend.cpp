/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <httpserver/detail/io_iocp_backend.hpp>
#if defined(_WIN32)
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include <httpserver/detail/io_timer_sweep.hpp>

namespace httpserver {
namespace detail {
namespace {
constexpr std::uint64_t k_listener_id_base = 1ULL << 62;
constexpr ULONG_PTR k_control = 1;
io_result closed_result() { return {http::outcome_code::connection_closed}; }
bool transient_accept_error(DWORD error) {
    return error == WSAECONNRESET || error == WSAECONNABORTED
        || error == ERROR_NETNAME_DELETED || error == ERROR_CONNECTION_ABORTED;
}
}  // namespace

thread_local io_iocp_backend* io_iocp_backend::current_driver_ = nullptr;
thread_local io_iocp_backend* io_iocp_backend::current_delivery_ = nullptr;

io_iocp_backend::io_iocp_backend() {
    winsock_held_ = pollsys::ensure_winsock();
    if (winsock_held_) port_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    failed_ = port_ == nullptr;
    if (!failed_) {
        try {
            thread_ = std::thread([this] { run_loop(); });
        } catch (...) {
            ::CloseHandle(port_);
            pollsys::release_winsock();
            throw;
        }
    }
}

io_iocp_backend::~io_iocp_backend() {
    close();
    // The driver drains all packets before join. The port and Winsock reference
    // outlive every request, candidate socket, and registration.
    if (port_ != nullptr) ::CloseHandle(port_);
    if (winsock_held_) pollsys::release_winsock();
}

http::outcome io_iocp_backend::ready() const {
    std::lock_guard<std::mutex> lock(mu_);
    return closed_ || failed_ ? http::outcome(http::outcome_code::invalid_state,
        "httpserver: IOCP backend unavailable") : http::outcome::okay();
}

http::outcome io_iocp_backend::dispatch(std::span<const server::readiness_event>,
                                       std::chrono::steady_clock::time_point) {
    return http::outcome(http::outcome_code::invalid_state, "httpserver: IOCP backend is managed");
}

void io_iocp_backend::notify_locked() {
    if (port_ != nullptr && !wake_pending_) {
        wake_pending_ = true;
        if (!::PostQueuedCompletionStatus(port_, 0, k_control, nullptr)) failed_ = true;
    }
}

void io_iocp_backend::finish_locked(const std::shared_ptr<op_state>& op, io_result result, completions& done) {
    done.emplace_back(op, result);
    if (op->claim_terminal()) {
        ++deliveries_;
    } else {
        done.pop_back();
    }
    pending_.erase(op.get());
}

void io_iocp_backend::deliver(const completions& done) {
    auto* previous = std::exchange(current_delivery_, this);
    for (const auto& entry : done) entry.first->owner()->enqueue(entry.first, entry.second);
    current_delivery_ = previous;
    {
        std::lock_guard<std::mutex> lock(mu_);
        deliveries_ -= done.size();
    }
    delivery_cv_.notify_all();
}

http::outcome_code io_iocp_backend::cancel_locked(op_state& target, completions& done) {
    if (closed_) return http::outcome_code::connection_closed;
    const auto found = pending_.find(&target);
    if (found == pending_.end()) return http::outcome_code::invalid_state;
    // Keep the shared_ptr independent of the pending entry erased by finish.
    const auto state = found->second;
    finish_locked(state, {http::outcome_code::cancelled}, done);
    const auto posted = posted_.find(&target);
    if (posted != posted_.end()) {
        const auto& request = *outstanding_.at(posted->second);
        ::CancelIoEx(reinterpret_cast<HANDLE>(request.socket->socket), posted->second);
        // Including ERROR_NOT_FOUND: the packet, not CancelIoEx, retires memory.
    }
    notify_locked();
    return http::outcome_code::ok;
}

void io_iocp_backend::submit(op_state& op) {
    const auto state = op.shared_from_this();
    completions done;
    {
        std::lock_guard<std::mutex> lock(mu_);
        op.set_sequence(next_sequence_++);
        if (is_tls_control(op.kind())) {
            finish_locked(state, {http::outcome_code::not_supported}, done);
        } else if (closed_ || failed_) {
            finish_locked(state, {closed_ ? http::outcome_code::connection_closed : http::outcome_code::invalid_state}, done);
        } else if (op.kind() == io_op_kind::cancel) {
            const auto target = std::get<cancel_payload>(op.payload()).target;
            const auto result = cancel_locked(*target, done);
            finish_locked(state, {result}, done);
        } else if (op.connection() != 0 && !connections_.contains(op.connection())) {
            finish_locked(state, closed_result(), done);
        } else {
            pending_.emplace(&op, state);
        }
        notify_locked();
    }
    deliver(done);
}

http::outcome_code io_iocp_backend::request_cancel(op_state& target) {
    completions done;
    http::outcome_code result;
    {
        std::lock_guard<std::mutex> lock(mu_);
        result = cancel_locked(target, done);
    }
    deliver(done);
    return result;
}

bool io_iocp_backend::adopt_locked(std::uint64_t id, pollsys::native_socket_t socket, bool listener) {
    if (connections_.contains(id)) throw std::logic_error("httpserver: duplicate connection id");
    if (closed_ || failed_) {
        pollsys::close_socket(socket);
        return false;
    }
    if (next_token_ == std::numeric_limits<ULONG_PTR>::max() || id == std::numeric_limits<std::uint64_t>::max()) {
        pollsys::close_socket(socket);
        throw std::overflow_error("httpserver: IOCP identity exhausted");
    }
    std::shared_ptr<registration> record;
    try {
        record = std::make_shared<registration>();
    } catch (...) {
        pollsys::close_socket(socket);
        throw;
    }
    record->socket = socket;
    record->token = next_token_++;
    record->listener = listener;
    bool configured = pollsys::set_nonblocking(socket, true);
    pollsys::prepare_stream_socket(socket);
    if (listener) {
        int size = sizeof(record->protocol);
        DWORD bytes = 0;
        GUID accept_id = WSAID_ACCEPTEX;
        configured = configured
            && ::getsockopt(socket, SOL_SOCKET, SO_PROTOCOL_INFOW, reinterpret_cast<char*>(&record->protocol), &size) == 0
            && ::WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &accept_id, sizeof(accept_id),
                &record->accept, sizeof(record->accept), &bytes, nullptr, nullptr) == 0;
    }
    if (!configured || ::CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket), port_, record->token, 0) != port_) {
        failed_ = true;
        notify_locked();
        return false;
    }
    connections_.emplace(id, record);
    if (id < k_listener_id_base) next_connection_ = std::max(next_connection_, id + 1);
    return true;
}

void io_iocp_backend::adopt_connection(std::uint64_t id, pollsys::native_socket_t socket) {
    std::lock_guard<std::mutex> lock(mu_);
    adopt_locked(id, socket, false);
    notify_locked();
}

void io_iocp_backend::adopt_listener(std::uint64_t id, pollsys::native_socket_t socket) {
    std::lock_guard<std::mutex> lock(mu_);
    adopt_locked(id, socket, true);
    notify_locked();
}

pollsys::native_socket_t io_iocp_backend::native_handle(std::uint64_t id) const {
    std::lock_guard<std::mutex> lock(mu_);
    const auto found = connections_.find(id);
    return found == connections_.end() ? pollsys::k_invalid_socket : found->second->socket;
}

void io_iocp_backend::retire_locked(std::uint64_t id, completions& done) {
    const auto found = connections_.find(id);
    if (found != connections_.end()) {
        const auto record = found->second;
        connections_.erase(found);  // invalidate before socket reuse
        ::CancelIoEx(reinterpret_cast<HANDLE>(record->socket), nullptr);
        pollsys::close_socket(record->socket);
    }
    std::vector<std::shared_ptr<op_state>> swept;
    for (const auto& entry : pending_) {
        if (entry.second->connection() == id) swept.push_back(entry.second);
    }
    for (const auto& op : swept) finish_locked(op, closed_result(), done);
}

void io_iocp_backend::release_connection(std::uint64_t id) {
    completions done;
    {
        std::lock_guard<std::mutex> lock(mu_);
        retire_locked(id, done);
        notify_locked();
    }
    deliver(done);
}

std::size_t io_iocp_backend::wake() {
    completions done;
    {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<std::shared_ptr<op_state>> wakes;
        for (const auto& entry : pending_) {
            if (entry.second->kind() == io_op_kind::wake) wakes.push_back(entry.second);
        }
        for (const auto& op : wakes) finish_locked(op, {}, done);
        notify_locked();
    }
    deliver(done);
    return done.size();
}

std::size_t io_iocp_backend::shutdown() {
    completions done;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (closed_) return 0;
        closed_ = true;
        while (!pending_.empty()) {
            const auto state = pending_.begin()->second;
            finish_locked(state, closed_result(), done);
        }
        while (!connections_.empty()) retire_locked(connections_.begin()->first, done);
        notify_locked();
    }
    deliver(done);
    return done.size();
}

std::size_t io_iocp_backend::close() {
    const auto count = shutdown();
    if (current_driver_ == this) return count;
    {
        std::lock_guard<std::mutex> lock(join_mu_);
        if (thread_.joinable()) thread_.join();
    }
    if (current_delivery_ != this) {
        std::unique_lock<std::mutex> lock(mu_);
        delivery_cv_.wait(lock, [this] { return deliveries_ == 0; });
    }
    return count;
}

std::size_t io_iocp_backend::pending_count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return pending_.size();
}

std::uint64_t io_iocp_backend::poll_iterations() const {
    std::lock_guard<std::mutex> lock(mu_);
    return iterations_;
}

void io_iocp_backend::post_locked(const std::shared_ptr<op_state>& op, completions& done) {
    const auto found = connections_.find(op->connection());
    if (found == connections_.end()) {
        finish_locked(op, closed_result(), done);
        return;
    }
    const auto socket = found->second;
    const bool accept = op->kind() == io_op_kind::accept;
    if (accept != socket->listener || (op->kind() == io_op_kind::write && socket->write_closed)) {
        finish_locked(op, closed_result(), done);
        return;
    }
    auto request = std::make_unique<native_request>();
    request->op = op;
    request->socket = socket;
    if (accept) {
        request->candidate = ::WSASocketW(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO,
            &socket->protocol, 0, WSA_FLAG_OVERLAPPED);
        if (request->candidate == pollsys::k_invalid_socket) {
            finish_locked(op, closed_result(), done);
            return;
        }
    } else {
        request->storage.emplace(*op);
        if (request->storage->size() == 0) {
            finish_locked(op, {}, done);
            return;
        }
        request->buffer.buf = reinterpret_cast<char*>(request->storage->data());
        request->buffer.len = static_cast<ULONG>(request->storage->size());
    }
    auto* native = request.get();
    auto* address = &native->overlapped;
    outstanding_.emplace(address, std::move(request));
    try {
        posted_.emplace(op.get(), address);
    } catch (...) {
        outstanding_.erase(address);  // not yet posted
        throw;
    }
    // All initiating calls run on the managed thread in submission order.
    // Even synchronous success emits a packet: no skip-on-success flags.
    int status;
    if (accept) {
        const DWORD region = sizeof(sockaddr_storage) + 16;
        status = socket->accept(socket->socket, native->candidate, native->addresses.data(), 0,
            region, region, &native->bytes, address) ? 0 : SOCKET_ERROR;
    } else if (op->kind() == io_op_kind::read) {
        status = ::WSARecv(socket->socket, &native->buffer, 1, &native->bytes, &native->flags, address, nullptr);
    } else {
        status = ::WSASend(socket->socket, &native->buffer, 1, &native->bytes, 0, address, nullptr);
    }
    const auto error = status == SOCKET_ERROR ? ::WSAGetLastError() : 0;
    if (status == SOCKET_ERROR && error != WSA_IO_PENDING) {
        posted_.erase(op.get());
        outstanding_.erase(address);  // synchronous failure: no packet exists
        if (accept && transient_accept_error(error)) {
            // The failed candidate is gone. Wake the next driver iteration to
            // repost this logical accept without terminating the listener.
            notify_locked();
            return;
        }
        finish_locked(op, closed_result(), done);
        if (op->kind() == io_op_kind::write) socket->write_closed = true;
        else retire_locked(op->connection(), done);
    }
}

void io_iocp_backend::packet_locked(OVERLAPPED* address, ULONG_PTR token, DWORD bytes, DWORD error, completions& done) {
    const auto found = outstanding_.find(address);
    // Validate raw addresses before dereference; an unrelated/duplicate packet
    // has no ownership authority. A wrong token cannot retire a live request.
    if (found == outstanding_.end() || found->second->socket->token != token) return;
    auto request = std::move(found->second);
    outstanding_.erase(found);
    const auto op = request->op;
    posted_.erase(op.get());
    const auto registered = connections_.find(op->connection());
    if (op->is_terminal() || registered == connections_.end() || registered->second != request->socket) return;
    if (error != ERROR_SUCCESS) {
        if (op->kind() == io_op_kind::accept && transient_accept_error(error)) {
            // The packet has been consumed, so request destruction can close
            // its candidate before the next iteration posts a fresh request.
            notify_locked();
            return;
        }
        finish_locked(op, closed_result(), done);
        if (op->kind() == io_op_kind::write) request->socket->write_closed = true;
        else retire_locked(op->connection(), done);
        return;
    }
    if (op->kind() == io_op_kind::accept) {
        const auto listener = request->socket->socket;
        sockaddr_storage address_storage{};
        int length = sizeof(address_storage);
        if (::setsockopt(request->candidate, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                reinterpret_cast<const char*>(&listener), sizeof(listener)) != 0
            || ::getpeername(request->candidate, reinterpret_cast<sockaddr*>(&address_storage), &length) != 0
            || next_connection_ >= k_listener_id_base) {
            finish_locked(op, closed_result(), done);
            return;
        }
        net::peer_address peer;
        pollsys::fill_peer(address_storage, peer);
        const auto id = next_connection_;
        const auto candidate = std::exchange(request->candidate, pollsys::k_invalid_socket);
        if (!adopt_locked(id, candidate, false)) {
            finish_locked(op, closed_result(), done);
            return;
        }
        finish_locked(op, {http::outcome_code::ok, 0, id, peer}, done);
    } else if (op->kind() == io_op_kind::read && bytes != 0) {
        done.emplace_back(op, io_result{http::outcome_code::ok, bytes});
        if (request->storage->claim_read(*op, bytes)) ++deliveries_;
        else done.pop_back();
        pending_.erase(op.get());
    } else {
        finish_locked(op, op->kind() == io_op_kind::read ? closed_result()
            : io_result{http::outcome_code::ok, bytes}, done);
        if (op->kind() == io_op_kind::read) retire_locked(op->connection(), done);
    }
}

int io_iocp_backend::prepare_locked(completions& done) {
    if (closed_) return outstanding_.empty() ? -1 : 1000;
    std::vector<std::shared_ptr<op_state>> operations;
    std::optional<std::chrono::steady_clock::time_point> deadline;
    for (const auto& entry : pending_) {
        const auto& op = entry.second;
        if (op->kind() == io_op_kind::timer) {
            const auto time = std::get<timer_payload>(op->payload()).deadline;
            if (!deadline || time < *deadline) deadline = time;
        } else if (op->kind() != io_op_kind::wake && !posted_.contains(op.get())) {
            operations.push_back(op);
        }
    }
    std::sort(operations.begin(), operations.end(), [](const auto& a, const auto& b) { return a->sequence() < b->sequence(); });
    for (const auto& op : operations) post_locked(op, done);
    return poll_timeout_ms(std::chrono::steady_clock::now(), deadline);
}

void io_iocp_backend::run_loop() {
    current_driver_ = this;
    for (;;) {
        completions done;
        int timeout;
        decltype(wait_) wait;
        try {
            bool failed;
            {
                std::lock_guard<std::mutex> lock(mu_);
                failed = failed_;
            }
            if (failed) shutdown();
            {
                std::lock_guard<std::mutex> lock(mu_);
                timeout = prepare_locked(done);
                wait = wait_;
            }
            deliver(done);
            done.clear();
            if (timeout < 0) break;
            DWORD bytes = 0;
            ULONG_PTR token = 0;
            OVERLAPPED* address = nullptr;
            const BOOL success = wait(port_, &bytes, &token, &address, static_cast<DWORD>(timeout));
            const DWORD error = success ? ERROR_SUCCESS : ::GetLastError();
            done.clear();
            {
                std::lock_guard<std::mutex> lock(mu_);
                ++iterations_;
                if (address != nullptr) packet_locked(address, token, bytes, error, done);
                else if (success && token == k_control) wake_pending_ = false;
                else if (!success && error != WAIT_TIMEOUT) failed_ = true;
            }
            deliver(done);
            done.clear();
            sweep_due_timers(pending_, mu_, std::chrono::steady_clock::now());
        } catch (...) {
            // Preserve any already-claimed deliveries if native setup failed.
            if (!done.empty()) deliver(done);
            shutdown();
        }
    }
    current_driver_ = nullptr;
}

}  // namespace detail
}  // namespace httpserver
#endif  // defined(_WIN32)
