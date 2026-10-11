# TASK-196 implementation evidence

Implemented in the helper-resolved `task/TASK-196` worktree from clean v3
`e83857afe708e6c9fdb713df2868bf91f962062d`. The implementation commit is the commit
containing this document. Task status remains In Progress; caller Groundwork
validation, archive, completion bookkeeping, merge and cleanup remain pending.
[The design](TASK-196-design.md) records the actual ownership choices and limits.

Explicit provider-neutral UDP listener entries now compose production QUICv1,
TLS1.3 and basic H3 with the existing public server, credentials, routes, hooks,
policy, metadata and hierarchical budgets. TCP remains the aggregate default;
TCP and UDP identities can share a fixed port. Validation precedes all binding,
and all endpoints activate transactionally. Public certificate policy accepts H3
and filters TCP/QUIC bindings independently; external PSK remains H1-only.

Production composition contains no fixture serving/capture/manual-executor/profile
or test-support dependency. Each request runs on an independent stream affinity,
with an owned bounded command and synchronized body/output rings. Protocol wire
owners retain storage, reliable information, aliases and close keys through
actual atomic send settlement, pending completions and real three-PTO expiry.
External stop exclusively drives native completion/time progression after
application dispatch has become inactive; request_stop stays immediate.

The journal `.debug/task196/journal.md` and raw `build/task196/` evidence retain
behavioral REDs, all failed attempts and their exact replays. Machine identities,
final gate receipts and local/external artifact directories are recorded in
[TASK-196-receipt.json](TASK-196-receipt.json) and `build/task196/handoff.json`.

Final local proof comprises all nine existing full transport/capture/corpus/fuzz stages;
normal, ASan+UBSan and TSan installed H3 matrices (each four TLS-on and four
TLS-off cells); the preserved H2 matrix in all three modes; six TLS policy
matrices; and eight strict package cells per sanitizer mode. Each independent
H3 client proves seven traffic cases plus 19 configuration/lifetime predicates
in every TLS-on cell. Bounded immediate-exit H2 repetition passes 128 fresh
processes per mode, 384 total, without success sleeps. Broad native coverage
passes 81 TLS-on and 21 TLS-off suites in each mode; the final stream-tail
repairs refresh eight affected TLS-on suites and six TLS-off suites in all six lanes and deterministic
8-test/39-check normal/ASan/TSan closure. Exact hashes and reuse scope are in the receipt.

Required causal closures include explicit mapping/rollback, actual installed
public routing, independent held-running/upload siblings, copied-field lifetime,
listener/connection/stream storage, actual emission settlement, CID cycle and
expiry retention, exclusive external retirement, and exactly-once asynchronous
admission for retransmitted Initial. Both independent pinned clients authenticate
through installed public-only shared/static managed/external consumers; the
public fixture is not the private transport endpoint.

A required H2 preservation gate exposed provider native retirement before real
TLS destructor exit. C++ TLS and warmed POSIX TLS destructor REDs refute both a
logical live count and future-only readiness. An unconditional caller join is
also invalid under a held destructor and finite deadline. Root explicitly selected
one precreated provider-only lane observer. It exclusively joins at most64 provider
handles and publishes plain completion after releasing them; it captures no lane,
provider, TLS, application or callback references. Provider drain preserves the
original steady deadline, request_stop/destruction never join, and arbitrary PSK
lookups remain detached and independent. Fixed startup fault, last-owner,
held-unrelated-lookup and invalid-handle witnesses cover ownership boundaries.
Successful drain proves all provider workers' native exit; it does not claim the
observer's own short plain C++ tail has natively exited. Immediate process-exit
ASan/TSan witnesses test that remaining limitation without success sleeps or
suppression.

The final H3 TSan destruction witness also exposed an affinity closure keeping
a retired semantic stream alive after the engine state lease was released.
The destructor redundantly accessed the expired owner to complete already
completed hooks. A deterministic ASan RED reproduces this exact use-after-free.
The destructor now skips owner-dependent completion after native retirement;
private/incomplete cleanup retains its previous completion path. Normal/ASan/TSan
prove exactly one completion, safe engine-before-affinity destruction and inert
late resume, and all final installed H3 matrices pass.

The full H2 family additionally exposed two distinct issues. An unchanged
TASK-195 TSan disconnect replay reproduces a consumer taking only a listener
snapshot between connection completion/rearm, then terminating during exception
unwind with a joinable host thread. Snapshot capture now requires a real live
connection registration; exception cleanup reveals the actual assertion while
retired-generation dispatch and zero-accounting checks remain. Separately,
matching drain PING ACK can arrive before asynchronous successful write completion
is recorded. Deterministic full/remainder offer REDs fail twice; GREEN retains one
exact active-offer ACK until full successful send, while partial and failed sends
cannot acknowledge the barrier. Both GOAWAY stages and the original deadline
remain required.

Required retained QUIC capture checks exposed a real authenticated terminal-close
bug on a newly observed candidate path: the original client-boundary close
preceded a last challenge, and the server continued emitting STREAM after that
same authenticated close. Production and the affected fixture now consume a valid
close before path activation or application frames. Protected independent-client
RED/GREEN tests retain forged-packet rejection, unchanged path/accounting,
exactly-once terminal notification and silent expiry; normal/ASan/TSan native and
real interop/capture replays cover the repair. A challenge is excluded from the
response obligation only when exact authenticated matching zero-error close
preceded its arrival at the client boundary. Both boundaries must match the close
packet; the server no-STREAM-after-close oracle remains mandatory. Missing,
unmatched, forged, undecoded or erroneous closes and unanswered live challenges
fail closed. The original failure remains rejected by the unchanged TASK191 oracle.

The pinned quic-go semantic drain client also exposed the previously documented
TASK191 unread-buffer/remote-close race after its genuine accepted-FIN ACK. Its
client-only tracer now holds submission of that already constructed covering ACK
until the application consumes its original accepted response. It changes no ACK
contents or server timing, and the original five-second context bounds the wait.
Both GOAWAY stages, late-request refusal, accepted response contents and remote
close remain required; independent captures prove actual wire ACK before server
retirement. Race unit tests and actual normal/ASan/TSan matrices pass.

The retained simulator is provisioned explicitly as a patched-clock derivative
of the original pinned image, with all scenario/entrypoint files unchanged. Host
CLOCK_REALTIME was observed stepping backwards about7.34 seconds every5.3
monotonic seconds; the exact pinned ns3.46.1 synchronizer used system_clock for
origin and elapsed pacing. Root authorized one task-local compatibility patch:
only that synchronizer's clock/period becomes steady_clock. Exact upstream
archive, source revision, original binary, patch, compiler, build, derived image
and binary hashes are retained. An unmodified-image pacing RED and a120-sample
100ms patched-image proof distinguish the clocks under actual host backsteps.
Exported symbols remain identical. No global clock spoofing, host mutation,
client-clock changes, altered scenarios, original test-limit extension or capture
predicate waiver is used. Final nine-stage evidence labels this tool provenance
and verifies exact covered inputs when reusing completed native/fuzz proofs.

Normal/ASan/UBSan/TSan libraries and installed consumers use Clang18/C++20 and
selected OpenSSL3.5.9. Sanitizers run outside ptrace with leaks, stack diagnostics,
nonrecovering UBSan and halt-on-error; no new suppressions. The existing named
request_lifecycle allocation-count TSan exclusion remains, with full normal/ASan
proof. External adapter allocation probes retain their executable-specific
shared-libsan setup. Strict installed-package audit probes use static sanitizer
runtime so no undeclared shared runtime is accepted.

Static gates compare the clean v3 baseline honestly: seven preexisting CPD groups,
three untouched size offenders, and unchanged/reduced complexity offenders; no
new or increased offending function. Overloaded complexity matching includes the
full signature. Actual retained Doxygen1.9.8 runs with Graphviz disabled because
absent and unchanged warning filters. The new API heading warning is repaired.
Pinned PMD7.24.0/JRE21 provisioning identities are retained in the machine receipt.

TASK-197 owns full H3 parity, RFC9220, rotation, staged drain and the precisely
recorded migrated-path receive-accounting observation. TASK-198/199 own benchmarks;
TASK-200 owns the separately retained timer/manual_executor condition-variable
teardown race. Nonlocal BSD/macOS/Windows checks remain unexecuted and CI/v3-PR
owned under AGENTS.md. No publication, push, tag or deployment is performed.
