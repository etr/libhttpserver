### TASK-065: RFC 5952 IPv6 zero-compression in `peer_address`

**Milestone:** M7 - v2 Cleanup
**Component:** `src/peer_address.cpp`
**Estimate:** S

**Goal:**
`peer_address.cpp:49-50` skips RFC 5952 zero-compression (collapsing consecutive zero groups to `::`) so the textual IPv6 form we expose is non-canonical. Spec comment notes "TASK-046 can refine when telemetry firms up" — telemetry has firmed up; finish the canonicalization now.

**Action Items:**
- [x] Implement RFC 5952 §4 canonical form: lowercase, suppress leading zeros within groups, collapse the longest run of `2+` consecutive zero groups to `::` (ties broken by first occurrence), do not collapse a single zero group.
- [x] Reuse libc's `inet_ntop` where it already produces RFC 5952 output (glibc ≥ 2.28, musl, macOS recent) and only post-process when the platform falls short; or always post-process for determinism across platforms (preferred — document choice in commit message).
- [x] Add a unit test pinning the RFC 5952 §4.2.2 examples (`2001:db8::1`, `::1`, `::`, embedded IPv4 mappings).
- [x] Remove the "TASK-046 can refine when telemetry firms up" comment.

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- `peer_address::to_string()` returns RFC 5952 canonical form for IPv6 inputs.
- IPv4 path unchanged.
- New unit test pins RFC 5952 §4.2.2 examples and the `::ffff:192.0.2.1` IPv4-mapped form.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 observability NFR
**Related Decisions:** None new (RFC 5952)

**Status:** Done
