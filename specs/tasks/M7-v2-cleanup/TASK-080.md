### TASK-080: Tighten threadsafety_stress latency gate back from 100× to 10×

**Milestone:** M7 - v2 Cleanup
**Component:** `test/integ/threadsafety_stress.cpp`
**Estimate:** M

**Goal:**
`test/integ/threadsafety_stress.cpp:756-768` loosened the latency gate from
10× to 100× warmup median because of CI-runner noise. The audit grades this
as a real loss of regression bite. Restore a tighter gate by attacking the
noise rather than the threshold.

**Action Items:**
- [ ] Profile what causes the CI-runner noise (cold caches, sibling-CPU scheduling, MHD socket-accept jitter). Capture median, p99, and max under the current runner.
- [ ] Stabilise the warmup: more iterations, pin to a single CPU on Linux (`taskset`), discard the slowest N% per round, use median-of-medians rather than a single median, or switch to a high-precision monotonic timer.
- [ ] With the noise floor characterized, restore the gate to 10× (or the tightest threshold that survives 99% of CI runs across the matrix).
- [ ] If a 10× gate is genuinely infeasible on shared CI runners, document the chosen floor in the test comment and in `test/PERFORMANCE.md`, with measurement data backing it.

**Dependencies:**
- Blocked by: TASK-032 (Done; original stress test)
- Blocks: None

**Acceptance Criteria:**
- The latency gate is at 10× warmup median (or the documented tightest stable floor) with measurement data.
- The test has not flaked in the last 50 CI runs across the matrix.
- A `test/PERFORMANCE.md` entry records the gate, the runner profile, and the noise floor.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §3.6 performance acceptance
**Related Decisions:** DR-008 (thread-safety contract)

**Status:** Backlog
