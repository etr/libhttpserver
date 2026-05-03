### 4.1 `webserver`

**Responsibility:** Library entry point. Owns the libmicrohttpd daemon, the route table, the IP block list, the connection arena pool. Provides start/stop, route registration (lambda + class forms), `block_ip`/`unblock_ip`, `features()`.

**Implementation:** PIMPL via `std::unique_ptr<webserver_impl>`. Public header `<httpserver/webserver.hpp>` includes only `<httpserver/create_webserver.hpp>` and standard library, never `<microhttpd.h>` or `<pthread.h>`. `webserver_impl` (in `src/httpserver/details/webserver_impl.hpp`) holds the `MHD_Daemon*`, the route-table data structures, per-connection arena state, and synchronization primitives.

**Interfaces:**
- Exposes (from PRD §3.4 and §3.7):
  - `start(bool blocking = false)`, `stop()`, `stop_and_wait()` (replaces `sweet_kill`), `is_running()`
  - `register_resource(path, unique_ptr<http_resource>)` and `(path, shared_ptr<http_resource>)`; `register_path` and `register_prefix` variants
  - `register_ws_resource(path, unique_ptr<websocket_handler>)` and `(path, shared_ptr<websocket_handler>)`
  - `on_get / on_post / on_put / on_delete / on_patch / on_options / on_head` (lambda form)
  - `route(http_method, path, handler)` — generic, table-driven
  - `block_ip(ip)`, `unblock_ip(ip)`
  - `features()` returning a `struct features { bool basic_auth, digest_auth, tls, websocket; }`
  - Operational: `run`, `run_wait`, `get_fdset`, `get_timeout`, `add_connection`, `quiesce`, `get_listen_fd`, `get_active_connections`, `get_bound_port`
- Consumes: `create_webserver` (builder); user-provided `log_access` / `log_error` / `validator` / `unescaper` / `auth_handler`.

**Key design notes:**
- Public methods are thread-safe and re-entrant from handlers, with two documented exceptions (`stop()` and `~webserver()` deadlock from inside a handler — they wait for the calling thread to drain).
- Route registration grabs a writer lock; route lookup grabs a reader lock. The LRU cache (256 entries) is checked before the locks on the lookup path.
- `~webserver()` joins MHD's internal threads before returning. Users who call `stop()` themselves still receive the same join behavior on destruction.
- The constructor `webserver(const create_webserver&)` is `explicit` (PRD-NAM-REQ-004).

**Related requirements:** PRD-HDR-REQ-001..004, PRD-FLG-REQ-001..005, PRD-CFG-REQ-001..004, PRD-HDL-REQ-001..006, PRD-NAM-REQ-001..005.

---
