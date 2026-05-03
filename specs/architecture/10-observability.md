## 10) Observability

The library is a passive provider; callers wire their own logging:
- `log_access` callback (already in `create_webserver`): invoked per request with the URI.
- `log_error` callback: invoked on internal errors and uncaught handler exceptions.
- No metrics or tracing surface added in v2.0.

---
