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
