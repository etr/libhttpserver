### TASK-042: Write `RELEASE_NOTES.md` for v2.0

**Milestone:** M6 - Release Readiness
**Component:** Documentation
**Estimate:** M

**Goal:**
Give v1→v2.0 porters a one-stop summary of what changed, organized by where they'll feel it. Informational, not a compatibility commitment.

**Action Items:**
- [ ] Sections:
  - "What's gone" — `*_response` subclasses, raw-pointer registration, `sweet_kill`, `ban_ip`/`unban_ip`/`allow_ip`/`disallow_ip`, paired `no_*` setters, `#define` constants, `gnutls_session_t` returns, public virtuals (`get_raw_response`, etc.), `#ifdef HAVE_*` guards.
  - "What's new" — `on_*`/`route()` lambda registration, `register_path`/`register_prefix`, `http_response` factory chain, `feature_unavailable`, `features()`, `iovec_entry`, `http_method`/`method_set`.
  - "What's renamed" — `sweet_kill` → `stop_and_wait`; `ban_ip`/`disallow_ip` etc. → `block_ip`/`unblock_ip`; `_resource` setters → `_handler`; `render_GET` → `render_get`; explicit `webserver(create_webserver const&)`.
  - "What changed semantically" — handlers return `http_response` by value (was `unique_ptr`/`shared_ptr`); request getters return `const&` / `string_view` (no insert-on-miss); thread safety contract documented (was implicit); error propagation contract documented; build-flag-disabled features now report at runtime via sentinel/throw.
  - "Build prerequisites" — C++20 floor; RHEL 9 needs `gcc-toolset-14`.
  - "SOVERSION" — bumped 1→2; `libhttpserver2` parallel-installable with `libhttpserver1`; v1.x is end-of-life.
- [ ] Lead with a one-paragraph TL;DR.
- [ ] Make explicit that this document is not a compatibility commitment.

**Dependencies:**
- Blocked by: TASK-041
- Blocks: TASK-044

**Acceptance Criteria:**
- Document covers every renamed/removed/added public surface from PRD §3.1-3.7.
- A v1 user can grep the document for any v1 method name and find what replaced it.
- Typecheck passes.

**Related Requirements:** PRD §2 documentation NFR
**Related Decisions:** §13 documentation deliverable

**Status:** Not Started
