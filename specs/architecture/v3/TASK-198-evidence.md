# TASK-198 implementation evidence

The opt-in runner compiles copied C++20 consumers against the selected installed
public package outside the source/build trees. Separate independent client
processes verify protocol, certificate identity, exact bytes and run/request IDs.
The contract recomputes nearest-rank p50/p95/p99 and completions per measured
second from bounded raw monotonic samples. Fresh run IDs, exclusive output paths,
server counters, command/process exits, header/linkage audits and immutable
artifact hashes bind each accepted receipt to its recorded inputs.

Caller-approved measurement adaptations preserve production policy:

- H2 defaults limit each connection to 128 stream openings/second. Its full
  workload retains 64 connections in successive epochs, each with eight warmups
  and 64 measurements: 512 actual warmups, 4096 measured requests, concurrency 8.
- H3 has sixteen live-request seats, bounded lifetime records and a fixed data
  pool. The workload uses successive retained connection epochs,
  each with four warmups/four measured streams: 4096 actual warmups, 4096
  measured requests, 1024 epochs, concurrency 4. The 393216-byte per-connection
  data pool cannot guarantee eight simultaneous semantic buffer reservations.
- Sum measured windows after each successful warmup. Exclude each epoch's setup,
  warmup and close; retain whole-process wall seconds separately. The bounded
  cell deadline is 300 seconds, including client connection cleanup.

Fresh optimized normal allocator TLS-on/off packages use Clang 18 and the
selected OpenSSL 3.5.9 provider. The same-source encrypted declarations in TLS-off
return typed not_supported with no bound port. No production source changed.
Existing compile_probe callers retain -O0 -g; benchmarks explicitly select -O2
-DNDEBUG. Missing clients, invalid responses, stale/tampered inputs, incomplete
counts, malformed JSON, clock errors and failed/absent live observations fail.

The full H2 original single-connection attempt failed during warmup at the
opening-rate allowance. It remains retained; it is not performance acceptance.
The full H3 first epoch attempt failed after 2505 measured completions with an
independent reset/termination observation. A second discriminator identifies
H3_EXCESSIVE_LOAD (StreamReset error_code=263 on the sixteenth request). The
experimental QUIC PING ACK barrier still failed after 3890 completions,
refuting ACK timing as a sufficient fix. The caller approved fixed H3
concurrency 4 and 300 seconds; the experimental barrier was removed. Default
semantic reservations and retained per-stream records explain the capacity
pressure. H1/H2 retain concurrency 8; these are observations across documented
workloads, not a comparable protocol ranking. Failed
attempts remain failed; their samples cannot satisfy the contract. Live
discriminator and complete matrix proof are retained separately.

Supporting gates against the current installed package:

- Installed public H2: all four shared/static managed/external cells, 51 families
  per cell, Python h2 4.4.1 and Node 24.15.0.
- Installed public H3: all four cells, independent aioquic 1.3.0/quic-go v0.55.0,
  including existing public resource saturation/refill witnesses.
- TASK-180 private network fixture probe: original 90-second bound, three output
  and upload saturation/reset/refill cycles, original CPU/RSS acceptance.
- Native protocol memory: twelve >=0.99-second CPU observations and four warm RSS
  plateaus, unchanged 0.2 core/1,048,576 byte thresholds.
- resource_limits: 28 cases, 855 checks, zero failures/skips.
- Public header/hygiene, fence lint, warning suppression lint and changed C++
  consumer cpplint passed. Python syntax and focused contracts passed.

Repository-wide optional lint-file-size still reports three unchanged baseline
files above 500 SLOC: io_poll_backend.cpp 509, connection_engine.cpp 549 and
http2_request_state.hpp 511. No source file was modified to change that baseline.
Nonlocal platform checks remain CI/v3 PR owned and unexecuted here. PRD §3.9
provides no absolute throughput/latency minimum or comparable regression baseline;
historical v1/v2 numeric claims are not applied. Sanitizers are correctness
proof only, never included in these normal allocator measurements.

Task/index remain In Progress for caller-owned validation/finalization. No
staging, commit, merge, worktree removal or release publication was performed.

## Observed full workloads

| TLS | Linkage | Loop | Protocol | Concurrency | Completed | requests/s | p50 ms | p95 ms | p99 ms |
|---|---|---|---|---:|---:|---:|---:|---:|---:|
| yes | shared | managed | h1 | 8 | 4096 | 4011.099 | 1.835190 | 3.280410 | 4.284180 |
| yes | shared | managed | h1-tls | 8 | 4096 | 3152.455 | 2.278890 | 4.316400 | 5.745870 |
| yes | shared | managed | h2 | 8 | 4096 | 1006.801 | 5.779980 | 15.910380 | 52.871400 |
| yes | shared | managed | h3 | 4 | 4096 | 724.615 | 3.450870 | 6.992730 | 15.249780 |
| yes | shared | external | h1 | 8 | 4096 | 4305.819 | 1.690740 | 3.280140 | 4.391280 |
| yes | shared | external | h1-tls | 8 | 4096 | 4252.921 | 1.691910 | 3.491550 | 4.516650 |
| yes | shared | external | h2 | 8 | 4096 | 1507.455 | 5.218380 | 6.057990 | 6.893280 |
| yes | shared | external | h3 | 4 | 4096 | 797.881 | 3.091050 | 6.508260 | 9.531630 |
| yes | static | managed | h1 | 8 | 4096 | 3923.383 | 1.871100 | 3.427110 | 4.446720 |
| yes | static | managed | h1-tls | 8 | 4096 | 3501.073 | 2.145870 | 3.643740 | 4.619340 |
| yes | static | managed | h2 | 8 | 4096 | 1406.592 | 5.600790 | 6.529140 | 7.223940 |
| yes | static | managed | h3 | 4 | 4096 | 904.514 | 2.946240 | 4.462470 | 6.204420 |
| yes | static | external | h1 | 8 | 4096 | 4066.803 | 1.793520 | 3.509010 | 4.792770 |
| yes | static | external | h1-tls | 8 | 4096 | 3677.617 | 1.947150 | 4.053780 | 5.539860 |
| yes | static | external | h2 | 8 | 4096 | 1464.588 | 5.324490 | 6.426720 | 7.175070 |
| yes | static | external | h3 | 4 | 4096 | 1013.311 | 2.570130 | 4.782510 | 5.993550 |
| no | shared | managed | h1 | 8 | 4096 | 3957.170 | 1.848150 | 3.449160 | 4.454820 |
| no | shared | external | h1 | 8 | 4096 | 3971.071 | 1.795770 | 3.604590 | 4.801050 |
| no | static | managed | h1 | 8 | 4096 | 3885.601 | 1.884060 | 3.522870 | 4.591980 |
| no | static | external | h1 | 8 | 4096 | 3945.253 | 1.846980 | 3.706200 | 4.815000 |

All 20 full cells completed with zero unexpected errors and matching server
counts. Each TLS-on cell has 4096 measured samples. H3 retains 4096 warmup
samples/1024 epochs per cell; H2 retains 512 warmups/64 epochs; H1 retains 256
warmups/one window. TLS-off additionally verifies encrypted pre-bind rejection.

Full input/command/artifact hashes and supporting gate receipts are recorded in
`TASK-198-receipt.json`. Distribution and offline revalidation proof remain in
`build/task198/distribution-receipt.json` and `build/task198/offline-validation.log`.


## Validation repair evidence

The strengthened acceptance contract binds the complete required matrix to the
supplied TLS identity, compares every exported cell statistic with recomputed raw
samples, honors the last compiler optimization option, and requires executable,
source/header, link-map, command, client and fixture provenance. Installed headers
remain bound by the independently supplied package hashes. Eight requested review
IDs cover these four defects. The new regressions failed before the repair (55
subtest failures) and all 18 focused contracts now pass; the 27 existing
installed-consumer contracts also pass.

Original full-workload receipt/sample directories remain immutable. Changes to
runner preflight and validator bytes invalidate their old current-source identity;
they are now revalidated explicitly against their original recorded identity.
`build/task198/repair-verification/receipt.json` records bounded equivalence:
every other source/package/provider/build/fixture/workload/compiler/client
identity is unchanged, original/current validator and runner snapshots and diffs
are hashed, and the producer's live `run` function has an identical AST. No old
sample is represented as a fresh measurement by the repaired producer. The
repaired producer separately passed a fresh TLS-on reduced smoke with all 16
cells and 16 requests per cell (256 total). Its first preflight attempt selected
system Python without the pinned clients and failed; that attempt is retained.
The retry selected the existing pinned client environment.

The source summary records the verification receipt, repair RED/GREEN logs and
new smoke. Distribution is refreshed into a separate `-repair` archive with
extracted runner help and focused contracts; the original archive is preserved.
The existing evidence checker verifies recorded identities plus the explicit
bounded-equivalence proof, while the public validation API continues to reject
any mismatched expected identity. Resource/installed protocol results retain
their original valid receipts because measured inputs and production policy are
unchanged. Nonlocal checks remain CI/v3 PR owned.
