# Debug-only environment variables

This document inventories environment variables that the libhttpserver
runtime reads. **None of these are meant to be set in production.**
All are silent unless explicitly enabled; the library emits a one-shot
`SECURITY WARNING` to stderr (and to the user's `log_error` callback,
when wired) the first time `webserver::start()` observes a set
variable.

---

## `LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY`

| Field | Value |
| --- | --- |
| Introduced | TASK-074 (June 2026) |
| Replaces | The prior compile-time `#ifdef DEBUG` guard in `src/detail/webserver_body_pipeline.cpp` |
| Read by | `httpserver::detail::debug_dump_request_body_opted_in()` (cached in a function-local static) |
| Independent of | Compile-time `DEBUG` / `NDEBUG`. The runtime check is the only gate. |

### Effect

When set to any non-empty, non-`"0"` value, every inbound request body
chunk is written verbatim to `stdout`, prefixed by `Writing content: `,
from the body pipeline in `src/detail/webserver_body_pipeline.cpp`.

### Risk

Raw request bodies routinely contain:
- Basic-auth credentials (when sent in the body rather than the header),
- form-posted passwords (`application/x-www-form-urlencoded` with a
  `password=` field),
- session cookies,
- CSRF tokens,
- PII (email addresses, names, IDs, addresses).

The `http_request::operator<<` redaction policy introduced in TASK-057
does **not** cover this code path -- bytes are written verbatim. Treat
any process where this variable is set as a credential-leaking
surface (CWE-532: Insertion of Sensitive Information into Log File).

### Startup signal

The library emits a single `SECURITY WARNING` line to stderr at the
first `webserver::start()` call after the variable is observed. The
exact line is:

```
[libhttpserver] SECURITY WARNING: LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY is set. Raw request bodies will be written to stdout, including credentials, session cookies, and PII. Unset the variable for production deployments. See docs/debug-env-vars.md.
```

The same line is forwarded to the owning webserver's `log_error`
callback when one is wired via `create_webserver().log_error(...)`. The
forwarding is best-effort: an exception from the callback is swallowed
so a misconfigured logger cannot abort daemon startup.

Multiple `webserver` instances constructed in the same process share a
process-wide print-once flag, so the warning is emitted exactly once
per process.

**stdout/stderr are independent streams.** The request-body dump goes to
`stdout`; the `SECURITY WARNING` above goes to `stderr`. Process
supervisors, container log aggregators, and the systemd journal may
capture these two streams separately (different files, different log
sinks, or different retention policies) rather than interleaving them
into one ordered log. If only `stdout` is retained/shipped, the leaked
credential/PII lines can end up with no accompanying warning context;
if only `stderr` is retained, the warning fires with no visible dump to
correlate it against. Operators enabling this variable must ensure both
streams are captured together (e.g. `2>&1` redirection, or a supervisor
configured to merge/correlate them) so the warning and the dump it
describes stay associated.

### How to disable

```sh
unset LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY
```

then restart the process. The value is cached in a function-local
static on first request, so a mid-process `unsetenv()` has no effect.

### Spot-check: release build with `-DDEBUG` accidentally set

The runtime check is the only gate; the env var must be explicitly set
for the dump to fire, regardless of the compile-time `DEBUG` /
`NDEBUG` posture. A release binary built with `-DDEBUG` accidentally
defined therefore still does not leak credentials/PII unless the
operator opted in.

---

## Adding new debug env vars

When introducing a new `LIBHTTPSERVER_*` debug environment variable:

1. Read it once via a function-local static (cached, race-free C++
   memory model semantics).
2. Add a one-shot stderr warning in `webserver::start()` naming the
   risk explicitly.
3. Document the variable, its risk, and its startup signal here.
4. Cross-reference the entry from `specs/architecture/10-observability.md`.
