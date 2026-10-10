# TASK-193 implementation evidence

Implemented on `task/TASK-193` in `/home/etr/progs/libhttpserver/.worktrees/TASK-193`, from `v3` at `7631185cf803efe430bb0457b48b25a2c3c1c90c`. The primary worktree remains clean. This is executor implementation evidence; caller-owned Groundwork validation, completion status, merge and cleanup remain pending.

The [machine receipt](TASK-193-implementation-receipt.json) pins program/log hashes, exact command receipts, package hashes, independent client identity and all six public matrix receipts. All public lanes share frozen source identity `4636d82aaa09450792d08b48e65efa9e47be9f3697c1d432aab2ef16d62ab2ba`. Base revision alone identifies the pre-implementation tree; the source manifest and package hashes identify the tested changes. The invocation journal is `.debug/native-public-tls-1fqxj_tr/journal.md` and invocation identity is `build/task193/run.json`.

## Result and ownership

The installed provider-neutral certificate store reuses the existing registry: synchronous candidate validation, atomic publication, shared store ownership and generation-pinned accepted sessions. Listener associations isolate stores. PEM inputs are not retained. TLS-off declarations/link symbols remain available with typed unavailability. Native certificate HTTP/1 uses the existing connection engine and serialized owner; one shared retirement lease retains connection/timer/record-buffer reservations and drain accounting through provider/raw-child retirement. Handshake work uses the existing bounded runtime, deadlines, cancellation arbitration and TLS pump.

All endpoint validation precedes binding and all binding precedes admission. Failed later binds release earlier endpoints synchronously. Actual peer and immutable TLS metadata reach exchanges/hooks. Disabled H1 versions reject before routing; H2/H3 and public mTLS/PSK/ACME remain typed unsupported. `tcp_tls` reports actual certificate HTTP/1 capability in TLS-on builds.

## Causal RED and repairs

- Unchanged installed TLS-on public application compiled, then `listen()` returned `not_supported` (5), exit 1: `/tmp/task193-initial-public-red-7gsejzxl/red.json`, with command/dependency receipts alongside it. This is behavioral RED, independent of missing declarations.
- Minimal inert API scaffold enabled the full public consumer, which failed TLS-on missing-store outcomes: `build/task193/scaffold-red-6mphnjes/public/shared-managed/managed-serve-process.json`.
- External structural validation and independent ephemeral endpoints failed before removing the old blanket refusal/duplicate-port-zero rule: `build/task193/options-red.log`, `ephemeral-red-2.log`. Native documented 24-hour timeout acceptance failed 1/63 checks before extending the private runtime ceiling: `runtime-timeout-red-2.log`.
- Capacity-one stalled peers bypassed timer/storage limits before admission leases: `build/task193/budgets-red/shared-managed/managed-timer_budget-process.json`, `storage-red-process.json`. Final cells prove immediate excess refusal, timeout and bounded verified healthy recovery after retirement.
- Explicit matrix inventory tests failed before exact scenario enforcement: `build/task193/public-inventory-red.log`. Final tooling rejects missing, duplicate and unknown cases.
- ASan exposed a new scope → cancellation closure → listener → scope ownership cycle on successful drain. TLS-on initial 26-program run passed 24 and leaked in native/external drain cases; TLS-off reproduced the same retained graph. Weak cancellation captures removed the cycle; identical required suites then passed with LSan enabled. Retiring scopes compare executor identity without dereferencing a destroyed server/pool.

Retained harness diagnostics: a discarded RAII hook handle initially suppressed hook observations; the consumer now retains handles. The first staging helper omitted top-level pkg-config metadata; installation was corrected before positive package proof. Immediate capacity recovery after wire EOF was an invalid retirement assumption under ASan: raw-child/provider retirement still owns the lease. The witness now requires verified recovery within three seconds, records attempts, and preserves immediate refusal/deadline checks. No wire/body/metadata failure is retried. An initial stale-ABI build overlap was rejected as baseline evidence. A plaintext nonce expiry assertion coincided with a wall-clock rollback; unchanged isolated and final complete reruns passed. Intermittent restricted-namespace command-overflow descendant cleanup failures are retained; final unchanged tooling contracts pass outside that namespace.

## Final local gates

Clang 18.1.3, C++20, selected native OpenSSL 3.5.9. Independent Python SSL client uses OpenSSL 3.0.13; executable/module/runtime graph and fixture certificate hashes are recorded. Named requests verify chain and hostname; unknown/absent SNI verifies chain and compares the certificate byte-for-byte with its independently hostname-verified named certificate.

| Lane | Programs | Custom-suite cases | Checks | Installed public cells |
| --- | ---: | ---: | ---: | --- |
| Normal TLS-off | 21 | 455 | 5746 | 4 × same-source unavailable outcomes |
| Normal TLS-on | 38 | 570 | 8663 | 4 × 23 named scenarios |
| ASan/UBSan TLS-off | 11 | 212 | 4306 | 4 × same-source unavailable outcomes |
| ASan/UBSan TLS-on | 26 | 267 | 6515 | 4 × 23 named scenarios |
| TSan TLS-off | 6 | 87 | 514 | 4 × same-source unavailable outcomes |
| TSan TLS-on | 11 | 120 | 1218 | 4 × 23 named scenarios |

All required suite receipts have nonzero cases/checks and zero failures/skips; standalone public link/feature/example probes are identified separately. ASan includes leak, stack-use-after-return, use-after-scope and nonrecovering UBSan diagnostics. TSan uses halt-on-error. No suppressions. Sanitizer runs execute outside ptrace. Relevant private profile/ACME suites pass as regression evidence, without substituting for installed listener proof.

Public receipts under `build/task193/`:

| Lane | Receipt |
| --- | --- |
| Normal off | `validation-repair-iter1/normal-off/receipt.json` |
| Normal on | `validation-repair-iter1/normal-on/receipt.json` |
| ASan off | `validation-repair-iter1/asan-off/receipt.json` |
| ASan on | `validation-repair-iter1/asan-on/receipt.json` |
| TSan off | `validation-repair-iter1/tsan-off/receipt.json` |
| TSan on | `validation-repair-iter1/tsan-on/receipt.json` |

Each cell checks staged public-only compilation outside source/build trees, exact pkg-config identity, shared/static linkage and dependency graphs. TLS-on scenarios cover H1.0/H1.1, named/default/absent/unknown SNI, listener isolation, atomic invalid/valid replacement, held handshake and established session, concurrent publication, caller destruction, policy-before-TLS, failed/stalled handshakes, capacity/timeout/recovery, pending stop/drain, reentrant hook/handler stop/drain, mixed plaintext/TLS, disabled protocols and partial bind rollback.

`build/task193/package-final-mfk2ngt1/summary.json` and both strict audit receipts pass. TLS-off/on install exactly 41 headers with identical effective declaration hashes. Public plaintext package traffic/configuration checks pass shared/static × managed/external in both modes. `check-local` (including headers/hygiene/install layout/native linkage/documentation), installed policy/runner fixtures, distribution generation and explicit new-file distribution assertions pass. The distribution SHA-256 is `5e134c632b6ccc57fabf75216302a81053f9ae8f7b3a5d431b98d9774afff16c`. Final changed-file cpplint, whitespace checks, 26 runner contracts and 35 policy fixtures pass; receipts are `build/task193/checks-final-k1pya_2z/commands.json`.

## Doubt disposition and limits

All eight [design doubts](TASK-193-design.md) are cleared locally: public declaration/dependency audits establish the API boundary; independent installed traffic establishes capability/SNI/policy/metadata; held/concurrent publication plus ASan/TSan establishes lifetime; stalled peers and retirement recovery establish bounded admission; stop/drain and reentrant callbacks establish settlement; occupied-port rollback establishes transactional binding. Sanitizers found and verified the ownership-cycle repair instead of weakening lifetime proof.

No unresolved local required blocker remains. BSD/macOS/Windows execution and nonlocal backends are CI/v3-PR owned under AGENTS.md. TASK-194 owns expanded public profiles/ACME/rotation interoperability; TASK-195/196 own H2/H3. TASK-200's separately owned manual-executor timer race was neither encountered nor edited. No merge, cleanup, push, publication or deployment was performed. Caller should archive the journal, `build/task193` receipts and initial `/tmp` RED evidence before task worktree cleanup.

## Reuse notes

No configured agent memory store exists. Autotools VPATH setup: `./bootstrap`, configure with Clang 18/C++20 and `--disable-examples`; place selected provider `-L`/rpath in `V3_TLS_LIBS`. Build `src` before scoped `test` targets, setting `check_PROGRAMS`, `TESTS` and `noinst_PROGRAMS` together. Install both `src` and root `install-pkgconfigDATA`. Littletest cases live under `test/unit`/`test/integ`, use `LT_CHECK` and expose counts/no-skip receipts. Macro-free public consumers live under `test/headers`/`test/installed`. ASan requires execution outside ptrace with LSan/stack diagnostics. Git worktree/ref mutations require sandbox escalation. Keep source mutation serialized with library/test builds to avoid stale ABI. Preserve registration identity and other task worktrees.

## Validation repair iteration 1

Two public witness gaps were repaired without changing production code or installed libraries. The connection-budget excess peer must now close within 200 ms, separated from the configured 500 ms stalled-handshake timeout. Its refusal elapsed time, both deadlines and later healthy recovery attempts are retained independently in each public receipt.

Handler stop/drain now has an atomic completion acknowledgement published only after the handler's own shutdown call returns. The runner polls it while its peer is still open and before coordinator stop. The fixture checks `is_running() == false` for stop and a successful typed ticket wait with `drain_status::completed` and zero remaining work for drain. The ticket publication uses release/acquire synchronization. Handler entry counts alone cannot pass either witness.

`build/task193/validation-repair-iter1/red.json` records initial RED: capacity-five fixtures timed out at the new refusal deadline in managed/external modes, and the original consumer could not acknowledge handler shutdown. `mutations.json` in that directory records six decisive rejected cells: capacity five, omitted handler `request_stop`, and omitted handler `begin_drain`, each managed/external. The latter mutations preserve the new acknowledgement stores; the stopped-admission/typed-ticket checks still reject them. Mutation source, executable and library hashes, exact compilation commands and process error receipts are retained.

All six refreshed public lanes in the table above pass shared/static × managed/external: 12 TLS-on cells × 23 scenarios and 12 TLS-off same-source cells. ASan/UBSan retains leak, stack-use-after-return and use-after-scope diagnostics; TSan retains halt-on-error. No suppressions were added. The complete affected tooling suites pass (26 runner contracts and 35 package-policy fixtures), as do changed-file cpplint and whitespace checks. Exact commands, sanitizer environment, source/consumer/library identities and reuse checks are in `build/task193/validation-repair-iter1/summary.json` and its referenced receipts.

Production/lifecycle suites and packaging declarations are unchanged. Their original suite logs were verified against recorded hashes and all installed library hashes match the original six lane receipts. Original package/layout/distribution evidence is retained for those unchanged invariants; the refreshed public receipts supersede the earlier public witnesses. Nonlocal checks remain CI/v3-PR owned.
