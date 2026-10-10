# v3 local release repair roadmap

Source baseline: `v3` at `1f597fb6a222a45956c6de88fe56976831ad4641`. Preserved release worktree: `.worktrees/TASK-185`, branch `task/TASK-185`, HEAD `34369bae26bdda7d512c678d626d3fff3d61ca42` plus recovery changes. The roadmap records dependency planning; task status and evidence record execution results.

The user's new authorization supersedes the old TASK-185 plan's product/benchmark exclusions. Complete new dependency tasks serially on current `v3`, merging and removing each isolated task worktree before the next. Keep TASK-185 open and preserve all existing repairs, failed/historical receipts and validation ledger. Public success requires real transport and installed supported APIs; removing `not_supported` alone is insufficient.

| Order | Task | Deliverable |
|---|---|---|
| 1 | TASK-186 | Concurrency consumer lifetime and TSan allocator-build repair |
| 2 | TASK-187 | Response-source and multipart lifetime repairs |
| 3 | TASK-188 | Peer-policy mutation progress under reader pressure |
| 4 | TASK-189 | Raw/independent H1 WebSocket handshake/flood repair |
| 5 | TASK-190 | Five H2 corpus transcripts/six route-effect checks repaired |
| 6 | TASK-191 | Independent quic-go H3 semantic/drain completion |
| 7 | TASK-192 | Current-v3 cookie parity execution witnesses |
| 8 | TASK-193 | Public credentials and native TCP TLS listener |
| 9 | TASK-194 | Public SNI/mTLS/PSK/rotation/ACME support |
| 10 | TASK-195 | Public TLS ALPN H2 owner, routes/streams/RFC8441/lifecycle |
| 11 | TASK-196 | Production UDP QUIC owner and public H3 route |
| 12 | TASK-197 | Complete public H3 semantics, limits/drain/RFC9220/TLS policy |
| 13 | TASK-198 | Native public throughput/latency runner and valid measurements |
| 14 | TASK-199 | Package-callable public/benchmark adapters, negative contracts and current docs reconciliation |
| 15 | TASK-185 resumed | Bind new witnesses, one fresh complete packaged local inventory, validation and finalization |

Task cards are in `M14-v3-release-repairs/TASK-186.md` through `M14-v3-release-repairs/TASK-199.md`; shared Groundwork/TDD/validation/merge contract is `v3-release-repair-workflow.md`; bounded integration design/doubts are `v3-release-integration-design.md`. Copy these into the chosen repository task/spec locations with references adjusted before runner activation. Add a M14 index, update the global task index, and make primary TASK-185 depend on TASK-186..199 (or TASK-199 plus the cards' complete transitive chain). Use standard standalone `- Blocked by:` and `- Blocks:` lines so runner catalog parsing resolves the order. Root owns final TASK-185 plan/addendum and runner recovery changes.

## Retained failures and closure mapping

| Failed inventory / retained defect | Closure |
|---|---|
| Broad normal TLS-off/on | 188 peer-policy, 189 WS, 190 H2; packaged normalizer fixture already repaired in185 and must be retained |
| ASan TLS-off/on | 186/187 lifetime cases plus normal defects; final full derivatives on one new source |
| TSan TLS-off/on | 186 allocator conflict and real owner-suite execution; new integration owner coverage required |
| H1/WS independent lane | 189 |
| H2 native corpus | 190; retain independent h2spec exclusions/predicates |
| H3 full semantic lane | 191, then public owner196/197; retain required QUIC/RFC9220/capture/fuzz predicates |
| Parity | 192 cookie mapping; public cross-version witnesses195/197 |
| TLS/resource adapter failures | Preserve185's tested adapter repairs and execute new artifact; 193/194 public TLS, 195/197 integrated budgets/slow peers |
| Public capabilities/credential installation | 193..197 installed independent witnesses and185 executable-stage wiring |
| Native throughput/latency | 198 runner/measurements and185 executable-stage wiring |
| Package envelope mutability / ineffective pending-platform test | Already repaired in185 recovery; preserve and revalidate impacted contracts after integration |

The unchanged 509-SLOC poll-backend baseline finding is not independently expanded into a new cleanup task; apply normal baseline ownership rules. LeakSanitizer is recorded unexecuted due local ptrace restrictions in185; do not fabricate a pass or silently remove required checks. The final runner must retain exact instrumentation boundaries and report any required local sanitizer execution that is still unavailable as unsuccessful; if that remains a local completion blocker, solve it with the applicable execution owner rather than waive it.

## TASK-185 resume addendum and safe base integration

1. Before activation, snapshot/hash the existing185 dirty diff, untracked source/evidence inventory and validation coordinator ledger using the runner's supported recovery mechanism. Preserve the old plan and append/replace it with a dated authorized addendum. Do not delete receipts, rewrite historic statuses/hashes, mark acceptance complete or reset the branch.
2. Update dependencies/task plan in the selected catalog and inspect runner dry-run for TASK-185..TASK-199: all new tasks must run before185. Its parser reads selected worktree cards; ensure copied cards/index and selected root are consistent. No runner may concurrently edit primary docs while the root is changing the catalog.
3. After199 merges/cleans, resolve185 via worktree helper and inspect identity/status/runner phase. Old phase/checkpoint evidence cannot imply completion against changed scope/source. Invalidate only stale implementation/validation checkpoints via supported recovery, preserving historical findings and causal evidence. Treat changed source/archive inputs as requiring new affected reviews and final checks.
4. Integrate updated `v3` into the preserved185 task branch using an explicit reviewed merge after protecting dirty repairs (commit/checkpoint task-owned repair source, or supported reversible save/apply); do not force reset/recreate/drop the worktree. Resolve conflicts preserving source/data/runner repairs and new dependencies. Verify185 still has `base_branch=v3` and the final candidate includes every dependency commit. Old artifact identities remain historical, never relabeled as new source.
5. Expand185 implementation plan to replace `v3_release_execution.py`'s hardcoded `blocked()` lambdas for `public-capabilities` and `native-throughput-latency` with the real installed public and benchmark adapters delivered by199 from193..198. Bind all executable/client/library/header/configuration/receipt provenance. Finish any installed matrix/consumer/TLS example expectation changes now necessary. Negative contracts must reject private-fixture success, wrong installed library, missing scenarios, fake benchmark samples and stale archive identities; add RED/GREEN tests before adapter changes.
6. Reuse immutable stage evidence retention, pending-platform positive control and corrected native output adapters. Run focused changed contracts and package registration first; preserve all 18 mandatory stages and existing scenario requirements, fuzz bounds, slow-peer thresholds and allowed named exclusions. No missing capability is waived or accepted by exit-zero alone.
7. Create one fresh distribution/extracted-source identity after integration and run the entire packaged TLS-off/on shared/static managed/external inventory, normal/ASan/UBSan/TSan, independent H1/H2/H3/WS, TLS, parity, resource, native benchmarks and fuzz with pinned tools/provider. All required local gates must pass on that one artifact; supplements from different archives cannot combine into completion. Record failed attempts intact and repair exact causes with TDD if fresh failures remain.
8. Reconcile current docs/card/index, close only evidence-backed validation findings, run applicable Groundwork validation and finalize185 into `v3` only after complete local acceptance. Nonlocal BSD/macOS/Windows/backend receipts remain CI/v3-PR work, and full tag readiness remains false until they arrive. No tag/push/publication/deploy is performed.

Native benchmark policy: the v3 PRD contains no absolute throughput or percentile target. TASK198 therefore requires reproducible successful measurements and existing resource thresholds, with no invented speed bar or revival of historical v2 numeric microbench claims. Any future performance regression policy needs an explicitly documented comparable baseline/threshold.
