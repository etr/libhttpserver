## 10) Observability

The library is a passive provider; callers wire their own logging:
- `log_access` callback (already in `create_webserver`): invoked per request with the URI.
- `log_error` callback: invoked on internal errors and uncaught handler exceptions.
- No metrics or tracing surface added in v2.0.
- Debug-only environment variables (e.g. `LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY`): see [`docs/debug-env-vars.md`](../../docs/debug-env-vars.md). None are read on the production code path; all are silent unless explicitly set, and the library emits a one-shot `SECURITY WARNING` to stderr (and to `log_error` when wired) when any of them is detected at `webserver::start()`.

---
