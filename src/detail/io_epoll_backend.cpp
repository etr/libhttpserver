/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>
#include <httpserver/detail/io_epoll_backend.hpp>
#if defined(__linux__)
#include <sys/epoll.h>
#include <algorithm>
#include <cerrno>
#include <limits>
#include <stdexcept>
#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/io_poll_backend.hpp>
#include <httpserver/detail/io_timer_sweep.hpp>

namespace httpserver {
namespace detail {
namespace {
constexpr std::uint64_t k_listener_id_base = 1ULL << 62;
thread_local io_epoll_backend* current_driver = nullptr;
struct driver_scope {
    explicit driver_scope(io_epoll_backend* driver)
        : previous(std::exchange(current_driver, driver)) { }
    ~driver_scope() { current_driver = previous; }
    io_epoll_backend* previous;
};
io_result closed_result() { return {http::outcome_code::connection_closed, 0, 0, {}}; }
void control(int epoll, int command, int fd, epoll_event* event) {
    int result;
    do {
        result = ::epoll_ctl(epoll, command, fd, event);
    } while (result < 0 && errno == EINTR);
    if (result < 0) throw std::runtime_error("httpserver: epoll_ctl failed");
}
}  // namespace

io_epoll_backend::io_epoll_backend() {
    epoll_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_ < 0 || !wake_.valid()) {
        closed_ = true;
        return;
    }
    try {
        epoll_event event{};
        event.events = EPOLLIN | EPOLLET;
        event.data.u64 = 1;
        control(epoll_, EPOLL_CTL_ADD, wake_.read_handle(), &event);
        thread_ = std::thread([this] { run_loop(); });
    } catch (...) {
        ::close(epoll_);
        epoll_ = -1;
        throw;
    }
}

io_epoll_backend::~io_epoll_backend() {
    close();
    connections_.clear();
    if (epoll_ >= 0) ::close(epoll_);
}

http::outcome io_epoll_backend::ready() const {
    std::lock_guard<std::mutex> lock(mu_);
    return closed_ || wake_failed_ ? http::outcome(http::outcome_code::invalid_state,
        "httpserver: managed epoll backend unavailable") : http::outcome::okay();
}

http::outcome io_epoll_backend::dispatch(std::span<const server::readiness_event>,
                                       std::chrono::steady_clock::time_point) {
    return http::outcome(http::outcome_code::invalid_state,
                         "httpserver: epoll backend is managed");
}

void io_epoll_backend::deliver(const completions& done) {
    for (const auto& entry : done) entry.first->owner()->enqueue(entry.first, entry.second);
}

void io_epoll_backend::finish_locked(const std::shared_ptr<op_state>& op,
                                    io_result result, completions& done) {
    // Reserve the delivery record before consuming pending ownership.
    done.emplace_back(op, result);
    if (!op->claim_terminal()) done.pop_back();
    pending_.erase(op.get());
}

void io_epoll_backend::notify_locked() {
    if (!wake_pending_) {
        wake_pending_ = true;
        if (!wake_.signal()) wake_failed_ = true;
    }
}

void io_epoll_backend::submit(op_state& op) {
    const auto state = op.shared_from_this();
    completions done;
    {
        std::lock_guard<std::mutex> lock(mu_);
        op.set_sequence(next_sequence_++);
        if (closed_) {
            finish_locked(state, closed_result(), done);
        } else if (op.kind() == io_op_kind::cancel) {
            const auto target = std::get<cancel_payload>(op.payload()).target;
            const auto it = pending_.find(target.get());
            const bool found = it != pending_.end();
            if (found) finish_locked(target, {http::outcome_code::cancelled, 0, 0, {}}, done);
            finish_locked(state, {found ? http::outcome_code::ok
                : http::outcome_code::invalid_state, 0, 0, {}}, done);
        } else if (op.connection() != 0 && !connections_.contains(op.connection())) {
            finish_locked(state, closed_result(), done);
        } else {
            pending_.emplace(&op, state);
        }
        notify_locked();
    }
    deliver(done);
}

http::outcome_code io_epoll_backend::request_cancel(op_state& target) {
    completions done;
    http::outcome_code result;
    {
        std::lock_guard<std::mutex> lock(mu_);
        result = closed_ ? http::outcome_code::connection_closed
                         : http::outcome_code::invalid_state;
        const auto it = pending_.find(&target);
        if (!closed_ && it != pending_.end()) {
            const auto state = it->second;
            finish_locked(state, {http::outcome_code::cancelled, 0, 0, {}}, done);
            result = http::outcome_code::ok;
            notify_locked();
        }
    }
    deliver(done);
    return result;
}

void io_epoll_backend::adopt_locked(std::uint64_t id, pollsys::native_socket_t socket,
                                   bool listener) {
    if (connections_.contains(id)) throw std::logic_error("httpserver: duplicate connection id");
    if (closed_) {
        pollsys::close_socket(socket);
        return;
    }
    if (next_token_ == std::numeric_limits<std::uint64_t>::max()
        || id == std::numeric_limits<std::uint64_t>::max()) {
        pollsys::close_socket(socket);
        throw std::overflow_error("httpserver: epoll identity exhausted");
    }
    std::shared_ptr<registration> record;
    try {
        record = std::make_shared<registration>(socket, next_token_++, listener);
    } catch (...) {
        pollsys::close_socket(socket);
        throw;
    }
    tokens_.emplace(record->token, id);
    try {
        connections_.emplace(id, record);
    } catch (...) {
        tokens_.erase(record->token);
        throw;
    }
    // Native listener identities must not advance the accepted-ID counter.
    if (id < k_listener_id_base) next_connection_ = std::max(next_connection_, id + 1);
}

void io_epoll_backend::adopt_connection(std::uint64_t id, pollsys::native_socket_t socket) {
    std::lock_guard<std::mutex> lock(mu_);
    adopt_locked(id, socket, false);
    notify_locked();
}

void io_epoll_backend::adopt_listener(std::uint64_t id, pollsys::native_socket_t socket) {
    std::lock_guard<std::mutex> lock(mu_);
    adopt_locked(id, socket, true);
    notify_locked();
}

pollsys::native_socket_t io_epoll_backend::native_handle(std::uint64_t id) const {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = connections_.find(id);
    return it == connections_.end() ? pollsys::k_invalid_socket : it->second->socket;
}

void io_epoll_backend::retire_locked(std::uint64_t id, completions& done) {
    std::vector<std::shared_ptr<op_state>> swept;
    for (const auto& entry : pending_) {
        if (entry.second->connection() == id) swept.push_back(entry.second);
    }
    done.reserve(done.size() + swept.size());
    const auto it = connections_.find(id);
    if (it != connections_.end()) {
        tokens_.erase(it->second->token);  // invalidate before fd can close or be reused
        // Closing the final lease removes the registration from epoll as well.
        connections_.erase(it);
    }
    for (const auto& state : swept) finish_locked(state, closed_result(), done);
}

void io_epoll_backend::release_connection(std::uint64_t id) {
    completions done;
    {
        std::lock_guard<std::mutex> lock(mu_);
        retire_locked(id, done);
        notify_locked();
    }
    deliver(done);
}

std::size_t io_epoll_backend::wake() {
    completions done;
    {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<std::shared_ptr<op_state>> wakes;
        for (const auto& entry : pending_) {
            if (entry.second->kind() == io_op_kind::wake) wakes.push_back(entry.second);
        }
        done.reserve(wakes.size());
        for (const auto& op : wakes) finish_locked(op, {}, done);
        notify_locked();
    }
    deliver(done);
    return done.size();
}

std::size_t io_epoll_backend::close() {
    if (current_driver == this) return shutdown();
    std::lock_guard<std::mutex> lock(join_mu_);
    const auto count = shutdown();
    // Quiesce all selected completion deliveries before the executor can drain.
    if (thread_.joinable()) thread_.join();
    return count;
}

std::size_t io_epoll_backend::shutdown() {
    completions done;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (closed_) return 0;
        done.reserve(pending_.size());
        while (!pending_.empty()) {
            const auto state = pending_.begin()->second;
            finish_locked(state, closed_result(), done);
        }
        closed_ = true;
        notify_locked();
    }
    deliver(done);
    return done.size();
}

std::size_t io_epoll_backend::pending_count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return pending_.size();
}

std::uint64_t io_epoll_backend::poll_iterations() const {
    std::lock_guard<std::mutex> lock(mu_);
    return iterations_;
}

std::optional<std::chrono::steady_clock::time_point> io_epoll_backend::scan_interest_locked(
    std::unordered_map<std::uint64_t, std::uint32_t>& masks) const {
    std::optional<std::chrono::steady_clock::time_point> deadline;
    for (const auto& entry : pending_) {
        const auto& op = *entry.second;
        switch (op.kind()) {
            case io_op_kind::read:
            case io_op_kind::accept: masks[op.connection()] |= EPOLLIN; break;
            case io_op_kind::write: masks[op.connection()] |= EPOLLOUT; break;
            case io_op_kind::timer: {
                const auto time = std::get<timer_payload>(op.payload()).deadline;
                if (!deadline || time < *deadline) deadline = time;
                break;
            }
            default: break;
        }
    }
    return deadline;
}

void io_epoll_backend::rearm_locked(registration& record, std::uint32_t mask) {
    if (mask == 0) {
        if (record.installed) control(epoll_, EPOLL_CTL_DEL, record.socket, nullptr);
        record.installed = false;
        record.armed = false;
        return;
    }
    if (record.installed && record.armed && record.mask == mask) return;
    epoll_event event{};
    event.events = mask | EPOLLET | EPOLLONESHOT;
    if (!record.listener) event.events |= EPOLLRDHUP;
    event.data.u64 = record.token;
    control(epoll_, record.installed ? EPOLL_CTL_MOD : EPOLL_CTL_ADD,
            record.socket, &event);
    record.mask = mask;
    record.installed = true;
    record.armed = true;
}

std::optional<std::chrono::steady_clock::time_point> io_epoll_backend::reconcile_locked() {
    std::unordered_map<std::uint64_t, std::uint32_t> masks;
    const auto deadline = scan_interest_locked(masks);
    for (auto& entry : connections_) rearm_locked(*entry.second, masks[entry.first]);
    return deadline;
}

bool io_epoll_backend::accept_locked(const std::shared_ptr<op_state>& op,
    const std::shared_ptr<registration>& lease, completions& done) {
    pollsys::native_socket_t socket = pollsys::k_invalid_socket;
    net::peer_address peer;
    const auto result = pollsys::accept_one(lease->socket, &socket, &peer);
    if (result.status == pollsys::sys_status::would_block) return false;
    if (result.status != pollsys::sys_status::ok) {
        retire_locked(op->connection(), done);
        return false;
    }
    if (next_connection_ >= k_listener_id_base) {
        pollsys::close_socket(socket);
        throw std::overflow_error("httpserver: accepted connection identity exhausted");
    }
    const auto id = next_connection_;
    adopt_locked(id, socket, false);
    finish_locked(op, {http::outcome_code::ok, 0, id, peer}, done);
    return true;
}

std::pair<std::size_t, bool> io_epoll_backend::transfer_stream(
    pollsys::native_socket_t socket, const op_state& op) {
    const bool read = op.kind() == io_op_kind::read;
    const auto bytes = read ? std::span<const std::byte>(std::get<read_payload>(op.payload()).buffer)
                            : std::get<write_payload>(op.payload()).bytes;
    std::size_t transferred = 0;
    while (transferred < bytes.size()) {
        const auto result = read
            ? pollsys::read_some(socket, std::get<read_payload>(op.payload()).buffer.data() + transferred,
                                 bytes.size() - transferred)
            : pollsys::write_some(socket, bytes.data() + transferred, bytes.size() - transferred);
        if (result.status == pollsys::sys_status::would_block) break;
        if (result.status != pollsys::sys_status::ok) return {transferred, true};
        transferred += result.transferred;
        if (result.transferred == 0) break;
    }
    return {transferred, false};
}

bool io_epoll_backend::step_locked(const std::shared_ptr<op_state>& op,
    const std::shared_ptr<registration>& lease, completions& done) {
    if (op->kind() == io_op_kind::accept) return accept_locked(op, lease, done);
    const bool read = op->kind() == io_op_kind::read;
    const auto [transferred, hangup] = transfer_stream(lease->socket, *op);
    if (read && transferred == 0 && !hangup
        && !std::get<read_payload>(op->payload()).buffer.empty()) return false;
    finish_locked(op, hangup && (!read || transferred == 0)
        ? closed_result() : io_result{http::outcome_code::ok, transferred, 0, {}}, done);
    if (hangup) retire_locked(op->connection(), done);
    return !hangup;
}

bool io_epoll_backend::wants_event(const op_state& op, std::uint32_t events) {
    const bool read = op.kind() == io_op_kind::read || op.kind() == io_op_kind::accept;
    const auto errors = EPOLLERR | EPOLLHUP | EPOLLRDHUP;
    if (read) return events & (EPOLLIN | errors);
    return op.kind() == io_op_kind::write && (events & (EPOLLOUT | errors));
}

std::vector<std::shared_ptr<op_state>> io_epoll_backend::collect_event_locked(
    std::uint64_t id, std::uint32_t events) const {
    std::vector<std::shared_ptr<op_state>> batch;
    for (const auto& entry : pending_) {
        const auto& op = entry.second;
        if (op->connection() == id && wants_event(*op, events)) batch.push_back(op);
    }
    std::sort(batch.begin(), batch.end(), [](const auto& a, const auto& b) {
        return a->sequence() < b->sequence();
    });
    return batch;
}

void io_epoll_backend::dispatch_event(std::uint64_t token, std::uint32_t events) {
    std::shared_ptr<registration> lease;
    std::vector<std::shared_ptr<op_state>> batch;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto found = tokens_.find(token);
        if (closed_ || found == tokens_.end()) return;
        lease = connections_.at(found->second);
        lease->armed = false;  // one-shot disables both directions
        batch = collect_event_locked(found->second, events);
    }
    dispatch_batch(token, batch);
}

void io_epoll_backend::dispatch_batch(std::uint64_t token,
    const std::vector<std::shared_ptr<op_state>>& batch) {
    std::shared_ptr<registration> lease;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto found = tokens_.find(token);
        if (closed_ || found == tokens_.end()) return;
        lease = connections_.at(found->second);
    }
    bool read_blocked = false;
    bool write_blocked = false;
    for (const auto& op : batch) {
        completions done;
        {
            std::lock_guard<std::mutex> lock(mu_);
            // Cancel/release can win between operations, including selected ones.
            if (closed_ || !tokens_.contains(token)) break;
            if (!pending_.contains(op.get())) continue;
            bool& blocked = op->kind() == io_op_kind::write ? write_blocked : read_blocked;
            if (!blocked) blocked = !step_locked(op, lease, done);
        }
        deliver(done);
    }
}

void io_epoll_backend::run_loop() {
    driver_scope scope(this);
    try {
        epoll_event events[64];
        for (;;) {
            std::optional<std::chrono::steady_clock::time_point> deadline;
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (closed_) return;
                if (wake_failed_) throw std::runtime_error("httpserver: epoll wake failed");
                deadline = reconcile_locked();
            }
            const auto timeout = poll_timeout_ms(std::chrono::steady_clock::now(), deadline);
            const int count = ::epoll_wait(epoll_, events, 64, timeout);
            if (count < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error("httpserver: epoll_wait failed");
            }
            {
                std::lock_guard<std::mutex> lock(mu_);
                ++iterations_;
                wake_.drain();
                wake_pending_ = false;
            }
            for (int i = 0; i < count; ++i) {
                if (events[i].data.u64 != 1) dispatch_event(events[i].data.u64, events[i].events);
            }
            sweep_due_timers(pending_, mu_, std::chrono::steady_clock::now());
        }
    } catch (...) {
        shutdown();  // fatal kernel/control failures must resolve pending work
    }
}

}  // namespace detail
}  // namespace httpserver
#endif  // defined(__linux__)
