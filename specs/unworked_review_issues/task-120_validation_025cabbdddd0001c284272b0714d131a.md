# Unworked Review Issues

**Run:** 2026-10-03 10:20:44
**Task:** TASK-120
**Total:** 3 (0 critical, 1 major, 2 minor)

## Major

1. [ ] **housekeeper** | `specs/tasks/M9-http1-parity-websocket/TASK-120.md:12` | action-item-not-marked-complete
   The action items 'Port SHOUTcast response semantics into HTTP/1 output.' and 'Inventory other documented HTTP/1 edge cases.' are implemented by the native ICY response work and the v2 parity inventory/coverage map, but both remain unchecked.
   *Recommendation:* Mark the two completed action items as done. Keep 'Replay each against the v2 observable-behavior matrix.' and the overall task In Progress while TLS and WebSocket native parity remain deferred.

## Minor

2. [ ] **architecture-alignment-checker** | `specs/architecture/v3/http1-migration.md:58` | interface-contract
   The new native migration guide and v2-parity-inventory.md:204 describe cookie::parse_cookie_header()/to_set_cookie_header() as standalone native equivalents, but they remain legacy helpers: cookie.hpp:21-23 rejects independent inclusion, and src/Makefile.am:35-37 includes cookie.cpp only in libhttpserver.la, which links libmicrohttpd at line 53. The native libhttpserver_v3core archive contains no cookie:: symbols. The cited cookie_render tests exercise that legacy path rather than establish an available native equivalent. This obscures the no-MHD consumer boundary in architecture §5 and DR-V3-001.
   *Recommendation:* Correct both migration passages to state that cookie parsing/rendering is currently application-level and the existing helper is legacy-only, with an explicit owning task for native availability; alternatively make the helpers independently includable and linkable through the native core and verify them with a native-only consumer before describing them as native equivalents.

3. [ ] **housekeeper** | `specs/architecture/v3/TASK-120-parity-evidence.md:73` | documentation-stale
   The evidence says the final git diff --check passed, but rerunning git diff --check against the frozen base and current HEAD exits 2 for a new blank line at EOF in test/parity/transcripts/file_resp.tseq.recorded:45. The frozen baseline also records this failure.
   *Recommendation:* Remove the extra blank line and record a passing check, or amend the evidence to report the remaining failure accurately.
