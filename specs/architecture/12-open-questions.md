## 12) Open questions and risks

| ID | Question / Risk | Impact | Mitigation | Owner |
|---|---|---|---|---|
| AR-001 | RHEL 9 stock GCC 11 cannot build v2.0 without `gcc-toolset-14`. Distro packagers may push back. | M | Document the toolset requirement in §8 and RELEASE_NOTES. Confirmed Red Hat-supported path. | Maintainer |
| AR-002 | Adding a body kind > 64 B in v2.x causes silent heap fallback (correct but unexpected). | L | `static_assert` guard in `details/body.hpp`; release-process checklist includes "do new body kinds fit in SBO?". | Maintainer |
| AR-003 | Routing semantics regression in the hash + radix + regex split (DR-7). | H | Run v1's full routing-test corpus against v2.0 unchanged; treat any failure as release-blocker. | Maintainer |
| AR-004 | `http_response` move-semantics (inline↔heap cross-product) is bug-prone. | M | Sanitizer-clean tests for all 4 move cases (covered in §9). | Maintainer |
| AR-005 | Per-request arena allocator plumbing leaks abstraction (request constructor needs implicit access to connection state). | L | Plumbing is internal; documented in `webserver_impl` design notes. No public API impact. | Maintainer |
| AR-006 | Handler thread-safety contract (concurrent invocation) may surprise users porting from v1 simple-thread setups. | M | Document prominently in README + RELEASE_NOTES. Dedicated example showing per-resource state with a mutex. | Documentation |
| AR-007 | `feature_unavailable` thrown from inside a handler becomes a 500 (DR-9) — users may expect 503 mapping. | L | Document the explicit behavior; users wanting 503 catch and translate. | Documentation |

---
