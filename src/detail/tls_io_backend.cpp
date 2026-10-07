/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include <httpserver/detail/tls_io_backend.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

#include <httpserver/detail/io_connection_owner.hpp>
#include <httpserver/detail/tls_session.hpp>
#include <httpserver/detail/tls_credentials.hpp>
namespace httpserver::detail {
using http::outcome_code;
struct tls_io_backend::core : std::enable_shared_from_this<core> {
    struct request {
        std::shared_ptr<op_state> op;
        std::shared_ptr<op_state> timer;
        bool complete = false;
        io_result result;
        std::chrono::steady_clock::time_point deadline;
    };
    io_backend& raw;
    executor& ex;
    const std::uint64_t connection;
    io_connection_owner child_owner;
    std::unique_ptr<tls_session> session;
    std::shared_ptr<tls_psk_runtime> handshake_runtime;
    std::stop_source handshake_stop;
    struct delivery_gate {
        std::recursive_mutex mutex;
        bool open = true;
    };
    std::shared_ptr<delivery_gate> gate = std::make_shared<delivery_gate>();
    bool handshake_outstanding = false;
    bool handshake_needs_input = false;
    std::shared_ptr<std::vector<std::byte>> handshake_input;
    std::mutex mu;
    std::deque<executor::handler> events;
    std::unordered_set<op_state*> registered;
    bool posted = false;
    bool dead = false;
    std::atomic<bool> close_requested{false};
    bool established = false;
    std::shared_ptr<const server::tls_peer_metadata> peer;
    std::atomic<std::uint64_t> sequence{1};
    std::shared_ptr<request> control, reader, writer;
    std::shared_ptr<op_state> raw_read, raw_write;
    explicit core(io_backend& transport, executor& executor, std::uint64_t id, tls_credentials_selection selection, bool server)
        : raw(transport), ex(executor), connection(id), child_owner(executor), session(std::make_unique<tls_session>(std::move(selection), server)), handshake_runtime(session->handshake_runtime()) {}
    void enqueue(executor::handler event, const std::shared_ptr<op_state>& tracked = nullptr) {
        bool schedule = false;
        {
            std::lock_guard lock(mu);
            if (tracked) {
                registered.insert(tracked.get());
            }
            events.push_back(std::move(event));
            if (!posted) {
                posted = true;
                schedule = true;
            }
        }
        if (schedule) {
            ex.post([self = shared_from_this()] { self->run(); });
        }
    }
    void run() {
        for (unsigned turn = 0; turn < 64; ++turn) {
            executor::handler event;
            {
                std::lock_guard lock(mu);
                if (events.empty()) {
                    posted = false;
                    return;
                }
                event = std::move(events.front());
                events.pop_front();
            }
            event();
            pump();
        }
        ex.post([self = shared_from_this()] { self->run(); });
    }
    void deliver(const std::shared_ptr<op_state>& op, io_result result) {
        {
            std::lock_guard lock(mu);
            registered.erase(op.get());
        }
        if (op->claim_terminal()) {
            op->owner()->enqueue(op, result);
        }
    }
    void finish(std::shared_ptr<request>& slot, io_result result) {
        auto req = std::exchange(slot, nullptr);
        if (!req) {
            return;
        }
        if (req->timer) {
            raw.request_cancel(*req->timer);
        }
        deliver(req->op, result);
    }
    void abort(outcome_code code = outcome_code::connection_closed, const std::shared_ptr<op_state>& initiator = nullptr) {
        if (dead) {
            return;
        }
        dead = true;
        handshake_stop.request_stop();
        close_requested.store(true, std::memory_order_release);
        for (auto* slot : {&control, &reader, &writer}) {
            if (*slot) {
                finish(*slot, {initiator && (*slot)->op != initiator ? outcome_code::connection_closed : code});
            }
        }
        if (raw_read) {
            raw.request_cancel(*raw_read);
        }
        if (raw_write) {
            raw.request_cancel(*raw_write);
        }
    }
    static task<void> read_child(std::shared_ptr<core> self, read_operation op, std::shared_ptr<std::vector<std::byte>> bytes) {
        const auto result = co_await op;
        self->enqueue([self, bytes, result] {
            self->raw_read.reset();
            if (self->dead) {
                return;
            }
            if (result.code != outcome_code::ok || result.transferred == 0) {
                self->abort(result.code == outcome_code::connection_closed || result.code == outcome_code::ok ? outcome_code::protocol_error : outcome_code::connection_closed);
            } else if (result.transferred > bytes->size()) {
                self->abort(outcome_code::protocol_error);
            } else if (self->handshake_runtime && !self->established) {
                bytes->resize(result.transferred);
                self->handshake_input = bytes;
                self->handshake_needs_input = false;
            } else if (!self->session->feed(std::span(*bytes).first(result.transferred))) {
                self->abort(outcome_code::protocol_error);
            }
        });
    }
    static task<void> write_child(std::shared_ptr<core> self, write_operation op, std::shared_ptr<std::vector<std::byte>> bytes, std::size_t offset) {
        const auto result = co_await op;
        self->enqueue([self, bytes, offset, result] {
            self->raw_write.reset();
            if (self->dead) {
                return;
            }
            if (result.code != outcome_code::ok || result.transferred == 0 || result.transferred > bytes->size() - offset) {
                self->abort();
                return;
            }
            const auto next = offset + result.transferred;
            if (next < bytes->size()) {
                self->send(bytes, next);
            }
        });
    }
    static task<void> timer_child(std::shared_ptr<core> self, timer_operation op, std::shared_ptr<op_state> target) {
        const auto result = co_await op;
        self->enqueue([self, target, result] {
            if (!self->dead && !target->is_terminal()) {
                if (result.code == outcome_code::ok) {
                    self->abort(outcome_code::timeout, target);
                } else if (result.code != outcome_code::cancelled) {
                    self->abort();
                }
            }
        });
    }
    void send(const std::shared_ptr<std::vector<std::byte>>& bytes, std::size_t offset = 0) {
        write_operation op(child_owner, connection, std::span<const std::byte>(*bytes).subspan(offset));
        raw_write = op.state();
        op.submit(raw);
        spawn(ex, write_child(shared_from_this(), std::move(op), bytes, offset), [](task_result<void>) {});
    }
    void receive() {
        const auto capacity = session->input_capacity();
        if (raw_read || capacity == 0 || dead) {
            return;
        }
        auto bytes = std::make_shared<std::vector<std::byte>>(std::min<std::size_t>(capacity, 16384));
        read_operation op(child_owner, connection, *bytes);
        raw_read = op.state();
        op.submit(raw);
        spawn(ex, read_child(shared_from_this(), std::move(op), bytes), [](task_result<void>) {});
    }
    bool cancel_target(const std::shared_ptr<op_state>& target) {
        for (auto* slot : {&control, &reader, &writer}) {
            if (*slot && (*slot)->op == target) {
                abort(outcome_code::cancelled, target);
                return true;
            }
        }
        return false;
    }
    std::shared_ptr<request>* slot_for(io_op_kind kind) {
        switch (kind) {
            case io_op_kind::read:
                return &reader;
            case io_op_kind::write:
                return &writer;
            case io_op_kind::tls_handshake:
            case io_op_kind::tls_shutdown:
                return &control;
            default:
                return nullptr;
        }
    }
    bool admissible(std::shared_ptr<request>* slot, io_op_kind kind) const {
        if (*slot) {
            return false;
        }
        if (slot != &control) {
            return established && !control;
        }
        if (reader || writer) {
            return false;
        }
        return kind == io_op_kind::tls_handshake ? !established : established;
    }
    static std::chrono::steady_clock::time_point deadline_for(const op_state& op) {
        switch (op.kind()) {
            case io_op_kind::read:
                return std::get<read_payload>(op.payload()).deadline;
            case io_op_kind::write:
                return std::get<write_payload>(op.payload()).deadline;
            default:
                return std::get<timer_payload>(op.payload()).deadline;
        }
    }
    void start_timer(request& req) {
        const auto deadline = req.deadline;
        if (deadline == std::chrono::steady_clock::time_point::max()) {
            return;
        }
        timer_operation timer(child_owner, connection, deadline);
        req.timer = timer.state();
        timer.submit(raw);
        spawn(ex, timer_child(shared_from_this(), std::move(timer), req.op), [](task_result<void>) {});
    }
    void admit(const std::shared_ptr<op_state>& op) {
        if (dead) {
            deliver(op, {outcome_code::connection_closed});
            return;
        }
        if (op->connection() != connection) {
            deliver(op, {outcome_code::invalid_argument});
            return;
        }
        if (op->kind() == io_op_kind::cancel) {
            const auto target = std::get<cancel_payload>(op->payload()).target;
            deliver(op, {cancel_target(target) ? outcome_code::ok : outcome_code::invalid_state});
            return;
        }
        auto* slot = slot_for(op->kind());
        if (!slot) {
            deliver(op, {outcome_code::not_supported});
            return;
        }
        if (!admissible(slot, op->kind())) {
            deliver(op, {outcome_code::invalid_state});
            return;
        }
        *slot = std::make_shared<request>();
        (*slot)->op = op;
        (*slot)->deadline = deadline_for(*op);
        if (op->kind() == io_op_kind::tls_handshake && handshake_runtime) {
            (*slot)->deadline = std::min((*slot)->deadline, std::chrono::steady_clock::now() + handshake_runtime->handshake_timeout());
            session->handshake_limits((*slot)->deadline, handshake_stop.get_token());
        }
        start_timer(**slot);
    }
    tls_session::result invoke(const op_state& op) {
        switch (op.kind()) {
            case io_op_kind::tls_handshake:
                return session->handshake();
            case io_op_kind::tls_shutdown:
                return session->shutdown();
            case io_op_kind::read:
                return session->read(std::get<read_payload>(op.payload()).buffer);
            case io_op_kind::write:
                return session->write(std::get<write_payload>(op.payload()).bytes);
            default:
                return {tls_session::progress::failed};
        }
    }
    void apply_step(std::shared_ptr<request>& req, tls_session::result result, bool& input) {
        switch (result.state) {
            case tls_session::progress::complete:
                req->complete = true;
                req->result = {outcome_code::ok, result.bytes};
                break;
            case tls_session::progress::eof:
                if (req->op->kind() == io_op_kind::read) {
                    req->complete = true;
                    req->result = {outcome_code::connection_closed};
                } else {
                    abort(outcome_code::protocol_error);
                }
                break;
            case tls_session::progress::input:
                input = true;
                break;
            case tls_session::progress::output:
                break;
            case tls_session::progress::failed:
                abort(result.failure);
                break;
        }
    }
    void step(std::shared_ptr<request>& req, bool& input) {
        if (!req || req->complete || dead) {
            return;
        }
        apply_step(req, invoke(*req->op), input);
    }
    struct handshake_step {
        std::unique_ptr<tls_session> session;
        std::shared_ptr<std::vector<std::byte>> input;
        std::shared_ptr<std::vector<std::byte>> output = std::make_shared<std::vector<std::byte>>(16384);
        tls_session::result result{tls_session::progress::failed};
    };
    void dispatch_handshake() {
        auto step = std::make_shared<handshake_step>();
        step->session = std::move(session);
        step->input = std::exchange(handshake_input, nullptr);
        const auto request = control;
        const auto stop = handshake_stop.get_token();
        const auto weak = weak_from_this();
        const auto delivery = gate;
        handshake_outstanding = true;
        const auto admitted = handshake_runtime->submit_handshake([step, request, stop] {
            if (stop.stop_requested()) {
                step->result.failure = outcome_code::cancelled;
            } else if (std::chrono::steady_clock::now() >= request->deadline) {
                step->result.failure = outcome_code::timeout;
            } else if (!step->input || step->session->feed(*step->input)) {
                try {
                    step->result = step->session->handshake();
                    step->output->resize(step->session->drain(*step->output));
                } catch (...) { step->result = {tls_session::progress::failed}; }
            }
        }, [step, request, weak, delivery] {
            std::lock_guard lock(delivery->mutex);
            if (!delivery->open) return;
            if (auto self = weak.lock()) {
                self->enqueue([self, step, request] { self->accept_handshake(step, request); });
            }
        });
        if (admitted != outcome_code::ok) {
            session = std::move(step->session);
            handshake_outstanding = false;
            abort(admitted);
        }
    }
    void accept_handshake(const std::shared_ptr<handshake_step>& step, const std::shared_ptr<request>& request) {
        handshake_outstanding = false;
        if (dead || control != request) return;
        if (close_requested.load(std::memory_order_acquire) || handshake_stop.stop_requested()) {
            abort();
            return;
        }
        if (std::chrono::steady_clock::now() >= request->deadline) {
            abort(outcome_code::timeout);
            return;
        }
        session = std::move(step->session);
        bool input = false;
        apply_step(control, step->result, input);
        if (dead) return;
        handshake_needs_input = input;
        if (!step->output->empty()) send(step->output);
    }
    void finish_control_flushed() {
        if (std::chrono::steady_clock::now() >= control->deadline || close_requested.load(std::memory_order_acquire)) {
            abort(close_requested.load(std::memory_order_acquire) ? outcome_code::connection_closed : outcome_code::timeout);
            return;
        }
        const bool shutdown = control->op->kind() == io_op_kind::tls_shutdown;
        established = !shutdown;
        if (!shutdown) std::atomic_store_explicit(&peer, session->peer_metadata(), std::memory_order_release);
        finish(control, control->result);
        if (shutdown) {
            abort();
            return;
        }
    }
    void finish_flushed() {
        if (raw_write || session->output_pending()) {
            return;
        }
        if (control && control->complete) finish_control_flushed();
        if (dead) return;
        if (writer && writer->complete) {
            finish(writer, writer->result);
        }
    }
    bool pump_handshake() {
        if (!handshake_runtime || !control || control->op->kind() != io_op_kind::tls_handshake || control->complete) return false;
        if (handshake_needs_input) {
            receive();
        } else if (!raw_write) {
            dispatch_handshake();
        }
        return true;
    }
    void pump() {
        if (dead) {
            return;
        }
        if (handshake_outstanding) return;
        if (pump_handshake()) return;
        bool input = false;
        step(control, input);
        step(writer, input);
        step(reader, input);
        if (dead) {
            return;
        }
        if (!raw_write && session->output_pending()) {
            auto bytes = std::make_shared<std::vector<std::byte>>(16384);
            bytes->resize(session->drain(*bytes));
            send(bytes);
        }
        finish_flushed();
        if (reader && reader->complete) {
            finish(reader, reader->result);
        }
        if (input) {
            receive();
        }
    }
};
tls_io_backend::tls_io_backend(io_backend& raw, executor& ex, std::uint64_t connection,
                               std::shared_ptr<const tls_context> context, bool server) {
    core_ = std::make_shared<core>(raw, ex, connection, tls_credentials_selection{nullptr, std::move(context)}, server);
}

tls_io_backend::tls_io_backend(io_backend& raw, executor& ex, std::uint64_t connection, tls_credentials_selection selection, bool server)
    : core_(std::make_shared<core>(raw, ex, connection, std::move(selection), server)) {}
tls_io_backend::~tls_io_backend() {
    close();
}
void tls_io_backend::submit(op_state& op) {
    auto self = core_;
    op.set_sequence(self->sequence.fetch_add(1));
    const auto state = op.shared_from_this();
    self->enqueue([self, state] { self->admit(state); }, state);
}
outcome_code tls_io_backend::request_cancel(op_state& op) {
    if (op.is_terminal()) {
        return outcome_code::invalid_state;
    }
    auto self = core_;
    {
        std::lock_guard lock(self->mu);
        if (!self->registered.contains(&op)) {
            return outcome_code::invalid_state;
        }
    }
    self->enqueue([self, state = op.shared_from_this()] {
        for (auto* slot : {&self->control, &self->reader, &self->writer}) {
            if (*slot && (*slot)->op == state) {
                self->abort(outcome_code::cancelled, state);
                return;
            }
        }
    });
    return outcome_code::ok;
}
std::shared_ptr<const server::tls_peer_metadata> tls_io_backend::peer_metadata() const { return std::atomic_load_explicit(&core_->peer, std::memory_order_acquire); }
void tls_io_backend::close() {
    auto self = core_;
    if (!self) return;
    {
        std::lock_guard lock(self->gate->mutex);
        self->gate->open = false;
    }
    self->handshake_stop.request_stop();
    if (!self->close_requested.exchange(true, std::memory_order_acq_rel)) {
        self->enqueue([self] { self->abort(); });
    }
}
}  // namespace httpserver::detail
