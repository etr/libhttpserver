## 6) Backend integration

### 6.1 libmicrohttpd

The only backend. v2.0 does not abstract over alternative backends and explicitly rules pluggability out (PRD §3.1 out-of-scope). The `MHD_Daemon*`, `MHD_Connection*`, `MHD_Response*` types appear only in `details/` headers and `.cpp` files.

### 6.2 GnuTLS

Optional (controlled by `HAVE_GNUTLS`). When disabled at build time, the public TLS-related methods on `http_request` (cert DN, fingerprint, etc.) return empty / sentinel values, and `webserver::features().tls == false`. When enabled, the implementation calls `gnutls_*` functions directly; `gnutls_session_t` is never returned through the public API.

### 6.3 pthread

Used by libmicrohttpd's worker pool and by libhttpserver's internal start/stop synchronization (`pthread_mutex_t mutexwait` / `pthread_cond_t mutexcond`). All `pthread.h` inclusions move to `details/` and `.cpp` files. The public API exposes no pthread types.

---
