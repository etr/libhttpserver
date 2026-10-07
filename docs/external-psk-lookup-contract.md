# External-PSK lookup and timeout execution contract

This is the provider-neutral design contract for TASK-135 and subsequent TLS
adapter work. TASK-134 installs no credential callback API, PSK profile, execution
lane, or timeout implementation. The current registry still accepts only
certificate and mutual-TLS profiles and rejects external PSK. Certificate/mTLS
and external PSK remain separate profiles; 0-RTT application data stays disabled.
Transport/ALPN combinations must be validated before advertising PSK support.

## Provider constraints and current seams

OpenSSL's [PSK callback documentation](https://docs.openssl.org/3.5/man3/SSL_CTX_use_psk_identity_hint/)
places TLS 1.2 lookup at ClientKeyExchange. The callback returns key length or
failure, without a documented retry result. TLS 1.3's find-session callback also
returns success/failure, optionally supplying a session, without a retry result.
These callbacks run synchronously inside the handshake call.

The [ClientHello callback](https://docs.openssl.org/3.5/man3/SSL_CTX_set_client_hello_cb/)
can return `SSL_CLIENT_HELLO_RETRY`; the handshake then reports
`SSL_ERROR_WANT_CLIENT_HELLO_CB` and can resume later. TLS 1.2's PSK identity is
not present at this stage, so ClientHello prefetch cannot solve both versions.
[OpenSSL ASYNC](https://docs.openssl.org/3.5/man3/ASYNC_start_job/)
requires cooperative suspension and resumption on the originating thread and
is unavailable on some platforms. It is not the portable execution contract.

Current library seams, inspected at TASK-134's base `d1349b59`:

- `src/httpserver/detail/tls_credentials.hpp` pins the selected context and
  immutable snapshot in `tls_credentials_selection`.
- `src/detail/tls_credentials.cpp::valid_profile()` rejects external PSK.
- `src/detail/tls_session.cpp` selects SNI in its early ClientHello callback;
  `classify()` handles WANT_READ/WANT_WRITE but has no application suspension.
- `src/detail/tls_io_backend.cpp::core` serializes TLS calls, timer and
  cancellation events on the same executor. Blocking a callback there delays
  the owner's deadline processing.
- `src/httpserver/concurrency/executor.hpp` permits inline execution. Calling
  `post()` alone does not establish execution away from the I/O owner.
- `src/httpserver/detail/worker_pool.hpp` has an unbounded queue and drains/joins
  during destruction, including inline draining. It cannot meet this contract.

## Callback shape

The following is typed pseudocode defining obligations, **not installed C++
declarations**. Names such as `owned_host_selection`, `result`, and `secure_bytes`
stand for future library-owned value types.

```cpp
enum class psk_tls_version { tls12, tls13 };

struct handshake_context {
    psk_tls_version version;
    uint64_t credential_generation;
    owned_host_selection selected_host;
    steady_clock::time_point deadline;
    stop_token cancellation;
    size_t maximum_key_bytes;
};

using psk_lookup =
    callable<result<secure_bytes>(
        span<const byte> identity,
        const handshake_context&)>;
```

The immutable selected credential generation captures callback and lookup
runtime ownership. `selected_host` owns the selected host/profile metadata;
no mutable registry reference is exposed. `deadline` is the absolute monotonic
handshake deadline. `maximum_key_bytes` is the smaller of the selected provider
capacity and the validated profile policy. Callback arguments remain valid
through that invocation; retaining identity or context requires owned copies.

## Execution and exclusive session handoff

Use a dedicated, bounded **handshake lane**, distinct from the I/O owner and
ordinary route workers. The OpenSSL handshake call and its synchronous PSK
trampoline execute on this lane. The trampoline submits application lookup to
a separately bounded **lookup lane** and waits only until completion,
cancellation, or the absolute handshake deadline. Both lanes prohibit executing
submitted work inline on the owner. Separating them allows the trampoline to
return failure when arbitrary application lookup ignores cancellation.

At most one worker owns a session/SSL at a time. Before dispatching a step, the
owner transfers owned input and output batches and exclusive session access.
While the step is outstanding, the owner cannot call SSL, feed/drain its BIOs,
or destroy the session. Worker code cannot borrow pending application I/O
buffers. The worker returns owned output batches and a step result; the owner
regains provider access only after retirement of that step. Cancellation and
logical connection close do not authorize concurrent SSL teardown.

Publish completion through the owner's serialized queue, checking connection
and attempt generation plus terminal state before applying it. Owner deadline
processing remains independently runnable during lookup. A stale completion
retires its owned session/batches without restarting I/O or accessing a retired
adapter. Exclusive ownership also applies to provider cleanup after a failed
or cancelled step.

## Admission, deadlines, and outcomes

Set finite limits for active handshake jobs, running lookups, queued lookups,
and aggregate lookup attempts per handshake. Include any queued handshake
steps in finite admission accounting. Admission never blocks the owner;
exhaustion rejects the handshake with a typed, redacted resource-limit result.
Use a fixed worker budget, never a thread per handshake. Queued work consumes
the same absolute deadline; workers check deadline/cancellation before invoking
application code. Retrying a provider step never resets the deadline.

Accept success only before the deadline and before cancellation or terminal
completion wins. The owner's terminal arbitration publishes exactly one
outcome. Even if the worker observed timely success, the owner discards it when
its deadline or terminal check loses the race. Late success cannot establish a
connection, deliver authenticated application data, or restart I/O.

Provide typed, library-owned outcomes for application rejection, provider
failure, timeout, cancellation and admission failure. Exceptions are caught at
callback boundaries and converted to redacted failure. Never log identity,
key material, exception text, or raw provider errors. Diagnostics expose outcome
categories, not application credential data.

## Cancellation, retirement, and rotation

Cancellation requests cooperative stop and wakes the trampoline's bounded wait.
It cannot forcibly interrupt arbitrary application code. Logical handshake
timeout does not prove that lookup/provider work terminated. The owner can
publish timeout promptly while retiring the outstanding handshake step later;
drain must report incomplete work if retirement misses its deadline. Do not
promise immediate reclamation or silently join an unresponsive lookup on the
owner.

A late lookup owns only copied identity/context, retained callback/runtime
ownership and its result storage. It cannot access SSL, the adapter, application
buffers, or a stack frame of the waiting trampoline. Completion storage is
owned independently of that wait. Discard and wipe a late key immediately when
lookup eventually returns. Unreturned lookups keep consuming bounded running
capacity: timeouts cannot open replacement capacity for still-running work.
Applications must honor stop/deadline and keep the runtime alive through
retirement. Runtime teardown must expose outstanding work rather than hiding
an unbounded join on the owner.

Rotation affects new acquisitions only. An attempt retains the same selected
immutable generation, callback/runtime and deadline across repeated provider
callbacks. Independent handshakes can invoke the same callback concurrently;
applications synchronize mutable state. Within an attempt, coalesce duplicate
concurrent lookup for the same identity and bound total attempts. Any retained
per-attempt key is owned secure storage wiped at retirement; do not introduce a
global plaintext-key cache.

## Identity representation and key ownership

Identity is an opaque byte span for the duration of lookup. TLS 1.3 supplies an
explicit length; copy those bytes without text normalization. TLS 1.2's provider
callback exposes a NUL-terminated string, so this profile supports textual
identities only. Embedded wire NULs cannot be detected with `strlen()`; do not
promise lossless binary identity semantics. If strict wire-NUL rejection is
required, it needs a separately verified wire-level check before lookup.

Validate finite identity/key profile bounds before lookup admission. For the
selected OpenSSL 3.5.9 provider:

| Version | Identity constraint | Key storage ceiling |
| --- | --- | --- |
| TLS 1.2 | 256 wire identity bytes; callback exposes a C string | 512-byte server callback buffer |
| TLS 1.3 | Explicit-length identity; enforce a finite profile bound | 512-byte `SSL_SESSION` PSK storage |

The TLS 1.3 ceiling comes from `TLS13_MAX_RESUMPTION_PSK_LENGTH` and
`ssl_session_st::master_key` in
[OpenSSL 3.5.9 ssl_local.h](https://github.com/openssl/openssl/blob/openssl-3.5.9/ssl/ssl_local.h),
and the array-size check in
[SSL_SESSION_set1_master_key](https://github.com/openssl/openssl/blob/openssl-3.5.9/ssl/ssl_lib.c).
The similarly named `SSL_MAX_MASTER_KEY_LENGTH` in `include/openssl/prov_ssl.h`
is 48, but **does not bound this session PSK storage**. Local characterization
accepts 512 bytes and rejects 513; this corrects the saved plan's 48-byte
assumption. Neither the public 48-byte constant nor the legacy PSK callback
buffer alone establishes TLS 1.3 capacity. Future providers must supply their
own verified bounds. A tighter application policy, such as 48 bytes, is allowed
but must be labeled separately. Reject empty/oversized keys without truncation;
recheck actual results against `maximum_key_bytes` before copying to OpenSSL.

`secure_bytes` owns move-only storage. Wipe it on rejection, cancellation,
timeout, overwrite and destruction. Avoid reallocations that leave unwiped
copies; moving storage must transfer ownership without duplicating plaintext.
The lookup-return/timeout race must wipe whichever result loses arbitration.
Moving library storage does not erase OpenSSL's own copies. The provider's
TLS 1.2 path copies the callback key and cleanses the temporary buffer in
[`tls_process_cke_psk_preamble()`](https://github.com/openssl/openssl/blob/openssl-3.5.9/ssl/statem/statem_srvr.c).
TLS 1.3 find-session invocation is synchronous in
[`extensions_srvr.c`](https://github.com/openssl/openssl/blob/openssl-3.5.9/ssl/statem/extensions_srvr.c).
Provider-owned copies retain their session lifetime and cleanup obligations;
library zeroization alone cannot prove all copies disappeared.

## Evidence boundary

`test/unit/tls_psk_contract_test.cpp` observes ClientHello retry/resumption, the
TLS 1.2 callback's calling thread, blocking interval and actual key buffer, and
the selected provider's session-key capacity using memory BIOs and synthetic
test credentials. It does not test a future libhttpserver PSK adapter, lookup
lane, or timeout implementation. TASK-135 must implement and test admission,
exclusive handoff, cancellation/deadline races, late-result wiping and retirement
against this contract. Local acceptance is recorded in
[the TASK-134 evidence report](task-134-psk-contract-evidence.md).
