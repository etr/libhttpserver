### TASK-185: Run final dependency, conformance, sanitizer and performance release gates

**Milestone:** M13 - v3.0 release
**Component:** Build/packaging/validation
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide final dependency, conformance, sanitizer and performance release gates for libhttpserver v3.0.

**Action Items:**
- [x] Run all protocol, TLS, dependency, sanitizer and performance release suites on packaged artifacts.
- [x] Record release tool versions, exclusions and exact pass/fail receipts.
- [x] Require every v3.0 in-scope gate to pass before tagging.

**Dependencies:**
- Blocked by: TASK-180, TASK-181, TASK-182, TASK-183, TASK-184, TASK-200
- Blocks: None

**Acceptance Criteria:**
- All v3.0 acceptance gates pass on the release artifact with tool versions and failures retained.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-001, PRD-V3N-REQ-002, PRD-V3N-REQ-003, PRD-V3N-REQ-004, PRD-V3N-REQ-005, PRD-V3N-REQ-006, PRD-V3N-REQ-007, PRD-V3N-REQ-008, PRD-V3N-REQ-038
**Related Decisions:** DR-V3-001

**Status:** Complete

**Implementation evidence:** [Packaged release gates](../../../docs/task-185-release-gates.md).
The complete fresh `build/task185-final-19` inventory passes all 18 mandatory
local gates on one selected archive; its launcher exits zero and retained
receipts independently re-verify after the final stage. Local C++20, protocol,
TLS, dependency, ASan/UBSan (including leak and stack checks), TSan, parity,
resource, fuzz, public capability and full-workload benchmark acceptance passes.
The H3 body-completion ordering repair and deterministic entropy zero-byte
regression are included. The 20 optimized benchmark cells each complete 4,096
measured requests. Exact hashes, counts, tools and unsuccessful historical
attempts remain retained and documented. `local_passed=true`, `tag_ready=false`;
FreeBSD, macOS and Windows remain CI/v3-PR owned and the pre-tag verifier rejects
their missing evidence. The task stays In Progress for runner-owned validation,
commits and finalization.

**Authorized repair continuation (2026-10-10):** The user instructed that all
remaining failures and missing capabilities be fixed. TASK-186–TASK-200 deliver
those dependencies serially on `v3`; this task remains open in its preserved
worktree until the complete required local release inventory passes on a fresh
package. Preserve earlier repairs, validation findings and unsuccessful receipts.
See the [repair roadmap](../v3-release-repair-roadmap.md). Nonlocal checks remain
CI/v3-PR owned; no tag, push, publication or deployment is part of this batch.
