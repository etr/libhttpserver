# Native HTTP/1 response migration (TASK-120)

The v2 public-header contracts remain the migration authority; the native
API uses library-owned status values and per-send body cursors.

A v2 `http_response::string("OK").shoutCAST()` response maps to a native
sync value (register using `native_server::route_sync`):

```cpp
server::sync_response stream_value() {
    server::sync_response response;
    response.status = http::status::from_code(200).with_shoutcast();
    response.fields.append("Content-Type", "text/plain");
    response.body = {std::byte{'O'}, std::byte{'K'}};
    return response;
}
```

The marker is copied with the status through hooks, `exchange` and
`response_definition`. It selects the fixed `ICY` token for this final
HTTP/1 response only; all framing, HEAD/no-content rules and keepalive
stay ordinary. Status numeric equality/categories ignore the marker,
and `100 Continue` stays `HTTP/1.1`. Construct an ordinary `from_code`
value for the next response. No audio metadata is added.

`response_definition::reopen_file(status, fields, path, definition)`
replaces `http_response::file(path)` and opens a fresh file per send.
The application can preserve v2's sanitized missing-file page by
checking `send_definition`'s report before committing a response:

```cpp
const auto report = co_await send_definition(exchange, definition, {});
if (!report.status.ok() && exchange.state() != exchange_state::responded) {
    // Return a sync value with status 500, Content-Type text/plain,
    // and body "Internal Server Error" through the normal value adapter.
    // Do not expose report.status.message() in the response body.
}
```

A post-commit failure remains a typed stream failure; it cannot replace
the response. The general send API does not synthesize a file-specific
500 body. The executable adapter is `test/integ/native_http1_parity_test.cpp`.

`owned_pipe` takes a transferred `FILE*` (for a descriptor, wrap using
`fdopen`) and closes it exactly once; it is one-shot. Deferred callbacks
map to `factory`: return one fresh `body_producer` per send, whose pulls
return `body_chunk{outcome, span, end}`. The spans must remain alive
until the next pull. For scatter/gather, a factory pulls the original
borrowed spans in sequence under their owner lifetime; declare the
known Content-Length. There is no direct native iovec factory. For one
contiguous borrowed buffer, use `borrowed(..., body_lease(owner), ...)`;
the lease pins the immutable storage across concurrent sends.

Request raw query bytes remain in `exchange.head().raw_target`; split
at `?` for the v2 `get_querystring` equivalent. Query decoding is an
application opt-in with explicit limits (`forms::decode_urlencoded`).
Form and path arguments have separate adapters/surfaces rather than the
v2 merged arg map. Cookie helpers remain standalone: parse the Cookie
field with `cookie::parse_cookie_header`, render each response cookie
with `cookie::to_set_cookie_header`, and append one Set-Cookie field
per rendered value. Request trailers are `exchange.body().trailers()`
once the reader reaches end-of-body.

The inventory records the existing routing/hook/IP migration exceptions.
TLS and WebSocket are deferred to their owning tasks; TASK-120 does not
claim native coverage for those protocols.

## Native HTTP/1.1 WebSocket upgrade (TASK-122)

Register a normal coroutine route. `co_await exchange.upgrade(options)`
returns an owned `websocket_upgrade_result`; on success its move-only
`session` owns the application's protocol state and
`selected_subprotocol` names the server-preferred offered token. Success
means the trusted 101 head entered bounded output. It does not promise
TCP delivery. The transport writes the entire ordered HTTP outbox before
any frame. No extension is negotiated, including permessage-deflate.
This native path does not depend on the legacy `HAVE_WEBSOCKET` flag.

```cpp
task<void> native_echo(exchange& x) {
    ws_upgrade_options options;
    options.subprotocols = {"chat"};
    options.allowed_origins = {"https://example.com"};
    options.allow_absent_origin = false;
    auto upgraded = co_await x.upgrade(std::move(options));
    if (!upgraded.status.ok()) {
        upgraded.rejection_fields.append("Content-Length", "0");
        x.respond(upgraded.rejection_status, upgraded.rejection_fields);
        co_return;
    }
    auto session = std::move(*upgraded.session);
    resume_signal closed;
    session.on_close([closed](websocket::close_info) mutable {
        closed.signal();
    });
    for (;;) {
        auto received = co_await session.receive();
        if (!received.value) break;
        for (;;) {
            auto sent = session.try_send(received.value->kind,
                                         received.value->data);
            if (sent.disposition == websocket::send_disposition::accepted)
                break;
            if (sent.disposition != websocket::send_disposition::backpressured)
                co_return;
            if (!(co_await session.writable()).ok()) co_return;
        }
    }
    co_await closed.wait_for(std::chrono::seconds(5));
}
```

Refusal is nonterminal and leaves the exchange available for an ordinary
response: malformed handshake 400; denied origin 403; otherwise-valid
unsupported version 426 with `Sec-WebSocket-Version: 13`. Invalid API
state and invalid configured limits remain typed failures. Upgrade is
legal only before body admission or any other terminal decision.
Callers must await the lazy task while the exchange is alive; options
are copied or moved into that task before its first execution.

Default origin policy permits an absent Origin and any syntactically
valid serialized origin. A nonempty allowlist matches the complete
serialized value exactly. Accepted values are `null`, or lower-case
`http://` / `https://` plus an ASCII letter/digit/hyphen/dot hostname or
bracketed IPv6 literal and an optional decimal port 1–65535. Paths,
credentials, lists and repeated Origin fields are refused. Case and
default ports are not normalized: `https://example.com:443` differs
from `https://example.com`. Origin policy is not authentication.
Subprotocols are case-sensitive tokens selected in server preference
order; no match normally omits the response field. Set
`require_subprotocol` to refuse no-match. Policies and offer lists each
have a 256-entry bound in addition to head byte/field budgets. Session
limits project down to connection budgets and may be tightened per route.

Destroying the session cancels it. An application initiating a clean
Close must register an `on_close` signal first, call `session.close()`,
and retain the session while awaiting that signal with its own deadline.
Returning immediately after `close()` can cancel unsent output. The
transport's private progress observer is independent of this callback.
Abrupt server stop safely cancels pending receive/writable work.
`begin_drain(budget, ticket)` stops admission and starts WebSocket Close
with code 1001 and reason `server drain`. Keep the session through its
`on_close` notification, including when `receive()` reports peer Close.
Drain initiation is nonblocking inside a handler; waiting there returns
`would_deadlock`. An external ticket wait reports completion after all
engine and handler units leave. A ticket may outlive its server.

The first successful application, peer or drain Close fixes an immutable
steady-clock anchor. The effective deadline is the earlier of that anchor
plus `timeouts.ws_close` and the server's absolute drain deadline. Writes,
traffic, notifications and 101 promotion do not extend it. Full prior
HTTP responses and the entire 101 precede Close output; a stalled stream
may exhaust the Close budget before those bytes flush. A dropped ticket
still leaves the engine watchdog enforcing the deadline.

A local Close timeout reports `timeout` / `WebSocket Close timeout` and
cancels only that connection. Global drain expiry records the pre-cancel
count once and reports `timeout` / `WebSocket drain deadline` to remaining
WebSocket sessions; delayed ticket waits retain `deadline_expired`.
Explicit stop reports `connection_closed` / `WebSocket server stop`.
The first terminal outcome and the best peer/local Close metadata win.
Close initiation wakes `writable()` with `connection_closed`; `receive()`
continues through the handshake and wakes with the terminal result.
Abrupt cancellation wakes both parked operations. Callbacks run outside
protocol/engine locks and are delivered once.
