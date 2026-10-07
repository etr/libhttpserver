# Portable external-loop readiness contract

`<httpserver/server/readiness.hpp>` defines the libhttpserver v3 consumer port
for PRD-V3N-REQ-015/016 and DR-V3-004. It requires C++20 and standard-library
headers only, plus public `http/outcome.hpp`. Include it directly without
`httpserver.hpp`, generated configuration, or `HTTPSERVER_COMPILATION`.

This document is normative for implementations of the port. TASK-124 supplies
the vocabulary and abstract declaration. TASK-125 implements the concrete,
server-owned adapter, its access path, and runtime enforcement. There is no
public registration mechanism for replacing the production transport backend.
Private operation handles, connection owners, and protocol state stay private.

## Configuration and server lifetime

Select `server_options::loop() = loop_mode::external` before constructing a
`native_server`. The default is `loop_mode::managed`. External mode supports
plaintext HTTP/1; `validate()` returns `not_supported` for TLS, HTTP/2, or HTTP/3.
An invalid loop enum returns `invalid_argument`. Worker concurrency (including
workers zero for automatic selection) continues to apply. External mode starts
route workers but no internal network polling thread.

```cpp
httpserver::server::server_options options;
options.loop() = httpserver::server::loop_mode::external;
options.add_listener({"127.0.0.1", 0, false});
httpserver::server::native_server server(options);
auto* driver = server.readiness();
auto listened = server.listen();
// If listened.ok(), acquire/reconcile the initial driver->interests().
```

`native_server::readiness()` returns a borrowed, server-owned pointer, or
`nullptr` in managed mode. Its address stays stable through `stop()` and until
server destruction. Before successful `listen()` and after `stop()`, snapshots
are empty (including wake and deadline) and dispatch returns `invalid_state`.
Wake construction or nonblocking setup failure returns a typed listen failure
before any listener binds. Hard notification failures terminate pending work;
failed dispatch requires host unregistration and server shutdown.

`request_stop()` initiates cancellation and wakes the host. `begin_drain()`
removes listeners and publishes remaining connections/deadlines: continue host
dispatch while responses drain. `stop()` cancels outstanding operations and
joins workers without further host dispatch. Unregister host handles and
quiesce callbacks before server destruction; destruction cannot overlap host
calls. Snapshot copies never retain socket ownership.

The public-header-only `examples/v3_external_event_loop.cpp` uses POSIX `poll`
or Windows `WSAPoll`, retains each key/generation pair, and rebuilds its complete
registration set after dispatch. It waits indefinitely when the library has no
deadline. An optional positive seconds argument bounds the whole demonstration
invocation; it is not a periodic wake fallback.

With an examples-enabled Autotools build:

```sh
make -C _build/task-125/examples v3_external_event_loop
_build/task-125/examples/v3_external_event_loop 10
# Use the PORT printed by the example in a second terminal:
curl --fail http://127.0.0.1:PORT/hello
```

## Registration identity and ownership

`socket_key` is an opaque, equality-comparable library identity. The unsigned
64-bit `registration_generation` accompanies it in every `socket_interest` and
`readiness_event`. A host saves the **pair** with each native registration and
copies that pair into queued callbacks. Default-constructed values do not
authorize registration; only instructions published by the driver do.

A socket lifetime change changes its generation; an interest-mask change for
the same live socket need not. No previously issued pair may be reused during
one driver lifetime, including on counter exhaustion. The library must stop or
fail rather than wrap into an old pair. Dispatch ignores removed registrations
and generation mismatches. Reusing a native descriptor cannot revive an old
callback. The wake registration follows the same identity rules.

`native_handle` carries a `std::uintptr_t` value, a `native_handle_kind`
(`posix_descriptor` or `winsock_socket`), and an explicit `valid` flag. Descriptor
zero is valid. A host must preserve pointer-width socket values on Windows and
must not use a numeric sentinel to override `valid`. An implementation publishes
only valid, correctly typed handles. The carrier is for host readiness
registration only; it is neither identity nor permission to perform socket I/O.

`interest_snapshot` owns its `sockets` vector, optional `wake` registration,
and optional `next_deadline`. Each snapshot is a complete, coherent set of
instructions: omission means unregister, and changed masks mean update the
host registration. Published pairs are unique across sockets and wake.
Snapshots can be copied and retained independently, including after driver
destruction. Their native handles remain borrowed and library-owned; retaining
a snapshot does not extend any handle lifetime. Never read, write, or close a
published handle. A saved registration does not authorize use after removal or
teardown. The host must remove registrations before awaiting more callbacks
after reconciling a newer snapshot; any already queued stale callbacks retain
their old pairs for filtering by dispatch.

## Host loop, time, and dispatch

One controlling host thread serializes snapshot acquisition, reconciliation,
waiting, and dispatch for each driver. Calls for that driver must not overlap or
recurse. Concurrent or recursive dispatch is rejected with
`http::outcome_code::invalid_state` **before consuming events**. Independent
servers may dispatch concurrently, and route workers may run concurrently.
No application handler runs under an internal I/O lock. Event order does not
establish protocol order: the private exactly-once terminal claim and
connection-owner serialization remain authoritative (TASK-099).

Deadlines and the `now` argument use `std::chrono::steady_clock` in the same
clock domain. Host samples must be nondecreasing. `nullopt` means no deadline;
there is no synthetic periodic timeout. A deadline at or before `now` requires
immediate dispatch, even with an empty event span. The library publishes its
actual nearest outstanding deadline; acquiring snapshots must not reset
protocol Close or server drain anchors.

Dispatch never waits for network readiness or application completion. Translate
native readable/writable notifications into the corresponding Boolean flags;
translate hangup/peer closure into `closed` and readiness errors into `error`.
These indications may coexist with readable/writable flags. They convey no
native error numbers or platform-specific flag values. The engine determines
the resulting protocol/transport outcome. Duplicate or spurious notifications,
including events with all flags false, are permitted and do not imply additional
completions. Empty batches are permitted for timers and pending library work.

Obtain an initial snapshot before waiting. After **every successful dispatch**,
obtain and reconcile a fresh snapshot before waiting again. A failed dispatch
may have consumed part of a batch unless the documented rejection rule says
otherwise. Stop driving on a failed outcome: do not replay the batch or reconcile
fresh interests as if dispatch succeeded. Recovery requires an explicitly
documented adapter recovery API; absent one, unregister and shut down through
the owning server. No such recovery API is provided by this header.

Snapshot acquisition can allocate and throw `std::bad_alloc`; it is not
allocation-free and has no outcome return. Treat that exception as a stopped
host loop, unregister, and arrange owner shutdown. Operational dispatch failures
use `http::outcome`; any allocation exception escaping dispatch has the same
stop-and-shutdown posture. The interface is not `noexcept`.

The driver is owned by its server; `native_server::readiness()` defines the
borrowed-pointer lifetime above. No host call may overlap driver destruction.
Unregister host interests and coordinate callback quiescence before teardown;
never dispatch queued callbacks to a destroyed driver.

## Wake publication and registration race

An active external driver that can receive asynchronous library work must
publish a usable, readable wake registration (`readable=true`, `writable=false`)
or return a documented configuration failure before listening. Absence is
permitted only in a documented inactive state in which worker submissions
cannot arrive. Wake signals pending library work and changes to interests or
deadlines, including worker-thread submissions. Hosts must not need periodic
polling to make that work progress.

Pending work remains authoritative across the **publish/register/wait race**:
work arriving after a snapshot but before host registration must leave a
notification observable when the host registers and waits. Implementations
must preserve this condition through wake draining/rearming and registration
changes, including work arriving during dispatch. Wakes may coalesce or be
spurious. On wake notification the host calls dispatch, passing the wake pair
as a readable event; the library drains and rearms its own source. The host
must not read or close the wake handle. The managed poll backend's idle
fallback is not part of this contract.

Existing `server_options::validate` remains the pre-listen configuration
boundary for PRD-V3N-REQ-016. The concrete adapter must reject incompatible
loop/configuration combinations there; the `loop()` option selects external mode and rejects combinations whose
engines do not support host readiness dispatch.

## Host-loop sketch

The host-specific functions below reconcile registrations and translate OS
notifications using the saved pairs. They are pseudocode, not library APIs.
`host_wait` accepts an optional absolute steady-clock deadline, returns an owned
event batch, and returns immediately for an already due deadline.

```cpp
// driver is a borrowed server-owned readiness_driver reference.
try {
    auto snapshot = driver.interests();
    host_reconcile(snapshot.sockets, snapshot.wake);
    while (host_running()) {
        std::vector<httpserver::server::readiness_event> events;
        const auto before_wait = std::chrono::steady_clock::now();
        if (!snapshot.next_deadline || *snapshot.next_deadline > before_wait) {
            events = host_wait(snapshot.next_deadline);
        }
        auto result = driver.dispatch(events, std::chrono::steady_clock::now());
        if (!result.ok()) {
            host_record_failure(result);
            break;
        }
        snapshot = driver.interests();
        host_reconcile(snapshot.sockets, snapshot.wake);
    }
} catch (const std::bad_alloc&) {
    host_record_allocation_failure();
}
host_unregister_all();
owner_shutdown();
```

## Portable consumer compilation

Compile the same `test/headers/consumer_v3_readiness.cpp` bytes and public
headers on each native platform. The fixture's driver is test-only; its calls
demonstrate consumer vocabulary, not stale-event, wake-race, timer, or
reentrancy runtime enforcement. TASK-125 owns those runtime proofs.

From the source root, with a task-local output directory that already exists:

```sh
# Linux, macOS, or BSD with the native C++20 GCC/Clang compiler:
c++ -std=c++20 -I src -c test/headers/consumer_v3_readiness.cpp -o receipts/consumer_v3_readiness.o
# Native Windows MinGW64 (not the MSYS POSIX target):
g++ -std=c++20 -I src -c test/headers/consumer_v3_readiness.cpp -o receipts/consumer_v3_readiness.o
```

Native Windows MSVC, from a developer command prompt:

```bat
cl /std:c++20 /EHsc /I src /c test/headers/consumer_v3_readiness.cpp /Foreceipts/consumer_v3_readiness.obj
```

The normal test suite builds/runs the consumer and public value tests without
backend libraries. `check-headers` also directly compiles the source consumer;
`check-install-layout` compiles it using only the staged public include tree.
Installed-header compilation and the native dependency audit are packaging
proof, distinct from the four-family native compile ledger and runtime proof.
Retain source/header hashes, real platform/toolchain identity, exact command,
exit status, and compiler output per family. Cross-target builds, platform
macro changes, and CI definitions do not constitute native family receipts.
