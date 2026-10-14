### TASK-198: Provide native public throughput and latency benchmarks

**Milestone:** M14 - v3 local release readiness repairs
**Component:** Native v3 performance tooling and receipts
**Estimate:** M
**Branch policy:** Current `v3` → isolated task branch → validated merge into `v3` → cleanup.
**Goal:** Supply executable packaged native-v3 throughput/latency evidence for the required release stage, using actual public servers.

**Workflow:** Follow `../v3-release-repair-workflow.md`; helper-resolve TASK-198, require `base_branch=v3`, RED/GREEN for measurement validity/receipt failures, then Groundwork validation/finalization.

**Action Items:**
- [x] Add an opt-in `make bench`/bounded native-v3 runner using installed public APIs and independent clients for H1 plaintext/TLS, H2 and H3. Historical `bench_*.cpp`/`test/PERFORMANCE.md` numeric v1/v2 microbenchmark claims are not a native protocol baseline; do not reuse obsolete APIs or their unrelated 10×/2× thresholds.
- [x] Define and retain reproducible warmup, measured request count/duration, fixed concurrency/payload/keepalive, managed/external modes, worker configuration, client/host/compiler/provider/library identities and raw samples. Report completed/error counts, completed requests per measured second and p50/p95/p99 latency with units; verify response contents so error/empty loops cannot appear fast. Exclude warmup/setup from measurement and retain monotonic-clock evidence.
- [x] Add failing contracts for zero/missing samples, clock/duration errors, incomplete requests, invalid responses, percentiles/units, wrong package identity, timeout/nonzero clients and malformed receipts; register tool/tests for distribution. Missing clients remain failure.
- [x] Run normal-allocator benchmarks against shared/static installed v3 packages and retain machine-readable immutable receipts. Keep existing TASK-180 CPU/RSS/slow-peer thresholds and bounded-memory acceptance unchanged; sanitizer runs are correctness evidence, not comparable performance samples.
- [x] Document that v3 PRD §3.9 has no absolute throughput/latency or relative regression number. Gate this new evidence on complete successful bounded measurement and existing stated resource requirements; report observed performance honestly, without inventing an unapproved minimum or claiming a nonexistent baseline. If a repository-wide specified native threshold is discovered, cite and enforce it.

**Acceptance Criteria:**
- Actual public H1/H2/H3 benchmark workloads produce nonempty valid raw samples, positive finite throughput, ordered finite p50/p95/p99 and zero unexpected protocol/content errors, with reproducible artifact/tool/configuration provenance.
- Tampered, stale, incomplete or synthetic receipts cannot satisfy the benchmark contract; full bounded workloads and existing normal-allocator resource gates pass.
- Focused contracts, C++20 installed consumers and Groundwork validation pass; benchmark commands/docs/receipts distributed, evidence/tracking and merge/cleanup complete.

**Dependencies:**
- Blocked by: TASK-195, TASK-197
- Blocks: TASK-199, TASK-185
**Related Requirements:** PRD-V3N-REQ-004 through PRD-V3N-REQ-009, PRD-V3N-REQ-027, PRD-V3N-REQ-038; TASK-185 native-throughput-latency inventory; bounded-memory acceptance.
**Related Decisions:** DR-V3-001, DR-V3-003, DR-V3-006; architecture §5 validation.
**Status:** In Progress
