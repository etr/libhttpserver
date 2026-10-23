# FEATURE-ci-bash-empty-arrays: preserve optional compiler arguments on legacy Bash

Status: Complete — local implementation and validation complete; native confirmation remains CI/v3-PR owned.
Base: `v3` at `103b11222e547f60471f6d43daad2759b08a8922`.
Plan: `.groundwork-plans/FEATURE-ci-bash-empty-arrays-plan.md` in the primary checkout.
Journal: `.debug/ci-bash-empty-arrays-q9c5zit7/journal.md` in the primary checkout.

Native Darwin archive run `40030063567`, job `120676108141`, demonstrates
`link_flags[@]: unbound variable` in the protocol fuzz runner and the generated
FreeBSD compiler wrapper, plus `rpath[@]: unbound variable` for static wrapper
invocation. Both owners enable nounset and expand optional empty arrays.

Action items:

- [x] Read the assigned plan, literal journal, primary CLAUDE.md, applicable
  AGENTS and v3 architecture; isolate from the exact local v3 base.
- [x] Establish actual Bash 3.2-family RED with the production fuzz runner and
  generated wrapper; pin the official source and retain argument evidence.
- [x] Handle absent optional arrays portably while retaining zero optional
  arguments, quoted paths, ordering, mode selection and failure propagation.
- [x] Verify empty target flags, empty link flags and populated provider flags;
  preserve fuzz instrumentation, bounds, corpus security and metadata.
- [x] Run focused tests with legacy/current Bash and real SDK compile/link/run
  cancellation checks using the existing private libc++20 SDK.
- [x] Check shell/Python syntax and whitespace; review the complete owned diff.
- [x] Caller: perform full Groundwork validation, finalization and merge.
- [ ] Native Darwin/FreeBSD post-repair execution: CI/v3-PR owned, not local.

Other closed or queued Darwin fixture and C++ failures remain outside this
repair. No broad baseline, CI control, merge or publication belongs to this task.

Implementation and evidence (2026-10-23):

The fuzz runner guards optional `target_flags` and `link_flags` in both the
recorded compile command and actual compiler invocation. The FreeBSD wrapper
guards optional `link_flags` and `rpath`. Bash's conditional array expansion
adds zero arguments for absent flags and retains each populated element as one
argument. Nounset, all existing mode decisions, provider selection, source
selection, instrumentation, limits, corpus validation and exit handling remain
in place. No dependency version or host Bash changes are introduced.

The task-local interpreter was built from the official GNU Bash 3.2.57 source,
SHA256 `3fa9daf85ebf35068f090ce51283ddeeb3c75eb5bc70b1a4a7cb05868bfe06a4`.
The GNU detached signature verifies with the maintainer's HTTPS-served key,
fingerprint `7C0135FB088AAF6C66C650B9BB5869F064EA74AB` (no configured web-of-trust
certification). The source needed yacc to regenerate its patched parser;
Ubuntu bison was extracted under the task proof directory, without installation.
Actual runtime: GNU Bash `3.2.57(2)-release` on Linux; comparison runtime:
`5.2.21(1)-release`. Native Darwin's exact interpreter patch level was not
captured, so this establishes the real 3.2-family behavior, not native success.

Before production edits, legacy execution failed the H3/QPACK invocation at
`link_flags[@]` and the HTTP/1 corpus invocation at `target_flags[@]`. The
generated wrapper failed eight mode cases at empty `link_flags` or `rpath`,
including `--version`. The current interpreter passed before the change.
After the repair:

- Protocol fuzz runner: 13 tests pass with each interpreter, covering empty
  flags, populated provider flags with spaces, exact compile metadata, limits,
  configuration failures, restored corpora and symlink rejection.
- FreeBSD toolchain: 4 tests pass with each interpreter. Exact argument vectors
  cover final/shared links, compile/preprocess/assembly/syntax/relocatable modes,
  version and static modes, using compiler and SDK paths containing spaces.
- Real local Clang18 builds and runs the production HTTP/1 head fuzzer with
  unchanged sanitizer flags, runs/time bounds and a spaced build path under
  each interpreter. The initial sandboxed capability run failed because
  LeakSanitizer cannot run under ptrace; authorized execution outside that
  sandbox preserves the sanitizer gate and reaches the expected legacy RED,
  then GREEN. The failure receipt is retained.
- Existing private libc++20 SDK: newly generated production wrappers compile,
  link and run the production cancellation probe under each interpreter;
  real compile/preprocess/assembly/syntax/version and relocatable checks pass.
  Raw compiler arguments, dependencies and source hashes are retained. Linux's
  forwarded `-Wl,-r` required caller flags `-no-pie -nostdlib` to suppress
  executable defaults; the two initial task-probe setup failures are retained.
  This is local Linux SDK selection proof, not native FreeBSD/libcxxrt ABI proof.
- Shell syntax under both interpreters, Python compilation and
  `git diff --check` pass. CLAUDE.md's C++ cpplint command does not apply to these
  shell/Python changes; no Python/shell linter is configured there.

Durable source pin, signature, interpreter, RED/GREEN logs, raw argument
receipts, exact source snapshots and task-executor notes are retained in the
primary checkout's `.debug/ci-bash-empty-arrays-q9c5zit7/implementation/`.
The task-local build and SDK probes remain in `/tmp/ci-bash-empty-arrays-proof`.
All other failure assessments remain closed with their prior uncertainty.
Post-repair native execution belongs to CI/v3 PR; overall CI is not green.

Final local validation/finalization (2026-10-23):

Groundwork sealed exact clean implementation `118db3bf3ba3eed0dcfb2b202ba76a2323f99dbe` / tree `ea89ccad7217f2861864c91d98bb052741c39dcd`: nine fresh independent profiles approved, 1 iteration, 0 fixed, 0 unworked. Durable receipt: `.git/groundwork/validation/cdb4ee2aea69cc6a/e0be2ef7dab5001c/groundwork-validation-3cefc1a6aa3b8eb2c15f656700685e35`. Existing hash-matched interpreter, regression, real fuzz and SDK receipts were reused; no broader suite or unrelated diagnostic was repeated. The initial spec-artifact automatic approval rejection was resolved with an evidence-based approval explicitly limited to local acceptance, preserving historical failures and native limits; no approval blocker remains. Caller changes only this bookkeeping before serial merge/cleanup. Native Darwin/FreeBSD remains unexecuted after this repair and assigned to CI/v3 PR. Other established selected-tool/fixture-path setup repairs follow serially; the five C++ causes remain uncertain and closed to further probes pending a later decision after independent work. Overall CI is not green.
