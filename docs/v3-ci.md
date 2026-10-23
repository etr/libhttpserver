# v3 CI and publication

Native push/PR checks target `v3` and `master`. Routing in Verify Build examines
`src/Makefile.am`: historical sources retain the historical matrix, while native
sources use `v3-native.yml`. CodeQL compiles the native core and private TLS
sources with the selected OpenSSL provider. ChangeLog checks compare the actual
PR base, passed as data through `BASE_REF`.

`v3-native.yml` runs C++20 `make check` with TLS off/on, including deterministic
H1/WebSocket, HPACK, H2, QUIC parser/state/recovery/callback, H3 and QPACK replay,
registered helper tests, public headers, examples and install hygiene. Checkout
README/release-note/Doxygen checks retain their repository/spec prerequisites.
`v3-package-validation.yml` separately runs complete installed shared/static,
managed/external matrices on Linux, macOS, MinGW64 and FreeBSD. Provider, build
and evidence directories live outside the source checkout; VM evidence is
copied back only after auditing, for upload. Native Python verifies executable
Git on Windows and FreeBSD. Selected clients are hyper-h2 4.4.1 and Node 24.15.0;
FreeBSD builds the checksummed Node source if its ports package differs. Linux
and macOS package jobs probe actual loopback TCP443 binding for ACME witnesses;
when privileges require it, they run the producer with sudo and an exact
checkout-scoped Git ownership exception. They restore artifact ownership before
uploading privileged proof, including on failure.

macOS uses the bottled Homebrew LLVM toolchain on macOS 15. A generated compiler
wrapper explicitly selects its matching libc++ library and runtime search path,
so installed-consumer audit environments can continue removing ambient compiler
flags. An actual C++20 cancellation/jthread compile-and-run probe gates the
matrix. The wrapper, compiler version and probe source are retained. Windows
inherits the host tool paths into MSYS2 for GitHub CLI and Node and uses the
MSYS2 Python virtual environment's `bin/python.exe` layout. FreeBSD uses the
Python package's bundled `ensurepip` through standard-library `venv`, without
requiring retired version-specific pip or virtualenv ports.

## Fuzz smoke and campaigns

Eleven independent target jobs prevent one failure from suppressing other
attempts. Ten targets compile TLS-off native sources; `quic_tls_callbacks`
uses genuine TLS-on configure/provider flags. Native project objects are built
with libFuzzer, ASan and UBSan instrumentation. OpenSSL and external clients
remain outside this instrumentation boundary.

Push/PR smoke requests 2,000 runs, at most 30 seconds per target, seed 128.
Scheduled/manual campaigns request a high run cap, 600 seconds per target and a
varying recorded seed. Existing 5-second input timeout, 512-MiB RSS and target
input-length bounds remain in force. `compile.sh`, `reproduce.sh`, build/run
logs, source lists, environment and crash artifacts are retained on failure.
Fuzz artifacts retain 30 days; native/package artifacts retain 14 days.

```sh
bash scripts/run-v3-protocol-fuzz.sh --compiler clang++ \
  --build-dir /tmp/fuzz-attempt --native-config-dir /tmp/configured-off \
  --targets 'http3_engine qpack' --seed 128 --runs 2000 --seconds 30 \
  --corpus-dir /tmp/writable-corpus
```

Seeds are copied from regression fixtures into a writable content-addressed
corpus. Restored inputs remain alongside these seeds. Corpus paths must be
outside the checkout and cannot contain symlinks. Neither fixtures nor existing
content-addressed inputs are overwritten. Immutable cache keys include corpus
format, target, platform, instrumentation, run ID and attempt; compatible keys
supply restoration. PRs restore only. Successful trusted `v3`/default-branch
push or campaign jobs may save. Deterministic replay has no cache dependency.
Corpus retention uses [GitHub’s cache policy](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching#usage-limits-and-eviction-policy):
entries expire after seven days without access, and storage pressure evicts older
entries within the repository’s configured limit. Caches are best-effort
persistence, not test proof.

The nightly schedule is intentionally dormant while this workflow exists only
on `v3` and `master` remains the default branch. It activates when the workflow
reaches the default/master branch. Scheduled checkout uses the event/default
ref, so it follows the new default source after cutover. No dispatcher or
repository setting change is introduced. Manual dispatch is defined but its
availability before default-branch adoption must be verified in the real
repository; push/PR execution does not depend on it.

## Selected-archive acceptance

`v3-release-acceptance.yml` prepares one distribution archive and source/tooling
snapshot. Each nonlocal producer checks the exact selected archive against its
checkout, runs the complete installed and native TLS-off/on suites, validates
raw results and exports a platform bundle. The Linux job then executes all 18
existing local gates against that same selected archive. Full H3 interop,
sanitizers, installed public capabilities and full benchmark workloads retain
their existing acceptance rules and exclusions.

The fixed FreeBSD/Darwin/Windows bundles preserve original receipt bytes and
logical artifact paths. A bounded per-platform catalog retains exact source,
log, suite, audit, compiler, provider and selected-client bytes. Import checks
safe extraction, namespace uniqueness, every catalog hash and referenced
package input; it reruns the installed matrix, raw-suite/count and native-result
validators. Windows path normalization applies only to logical lookup and
comparison; foreign binaries are never executed. Sealed dyld/API-set OS leaves retain the
existing auditor’s explicit whitelist; nonexistent OS files are never
synthesized. Missing/changed bytes,
missing native replays, failed suites and stale counts block readiness.

Linux evidence stays on its original filesystem. The actual `check_tag_ready`
validator checks all 18 local gates and all three platform receipts before an
authorization is written. That authorization binds the repository, workflow
source/run/attempt, selected commit/tag/archive, accepted manifest and immutable
selected/platform artifact IDs and digests. Downloads compare the actual ZIP
SHA-256 with both producer output and GitHub artifact metadata; a warning from
a download action is insufficient. Artifact creation must belong to the current
run attempt; rerun all jobs to produce fresh release proof rather than reusing
artifacts from successful jobs in an earlier attempt.

On PR runs, artifact/run API metadata identifies the PR branch head, while
`GITHUB_SHA` and the selected checkout identify the test merge commit. Downloads
bind the API head to the trusted event's PR head. Selected archive preparation
and platform proof still require the exact selected checkout commit, source
snapshot and archive bytes; these two source identities are not interchangeable.

The final publisher alone receives `contents: write`. It checks successful
mandatory dependencies, same-run artifacts, manifest and archive hashes and the
current tag target before `gh release create --verify-tag`. It runs only inline
workflow code and the GitHub CLI, with no checkout or downloaded candidate code.
Historical release sources retain their legacy distribution validation path.
The workflow starts after an existing tag; release owners must still execute
the separate pre-tag readiness command. This is publication enforcement.

PR and validation-only dispatch use the same archive, platform and full local
path, and cannot publish. For an explicit manual validation request, provide an
already resolved 40-hex `source_commit` and its `source_version`. Empty inputs
select the current workflow ref and the version from `configure.ac`. The actual
GitHub run and dispatch availability are CI/root verification responsibilities.
No tags or releases are created by local verification.

For local callers, fresh archive creation remains the default:

```sh
python3 scripts/run-v3-release-gates.py --output /tmp/release-attempt \
  --selected-archive /tmp/selected/libhttpserver-3.0.0.tar.gz \
  --selected-sha256 ACTUAL_SHA256 --provider-prefix /tmp/provider \
  --compiler clang++ --cc clang --make make
# Supply the existing required pinned clients, captures and interop arguments.
python3 scripts/run-v3-release-gates.py --check-tag-ready /tmp/release-attempt/manifest.json \
  --platform-bundle /tmp/FreeBSD.tar.gz --platform-bundle /tmp/Darwin.tar.gz \
  --platform-bundle /tmp/Windows.tar.gz
```

Each output/import destination is exclusive. A failed attempt is preserved;
use a fresh directory for another attempt. A missing required infrastructure
capability remains an unsuccessful required dependency. In particular, the
pinned upstream QUIC simulator is provisioned first. No historical clock
compatibility receipt is imported. If a fresh run demonstrates clock
incompatibility, the QUIC simulator infrastructure owner must reprovision the
existing bounded compatibility recipe/probe and produce fresh evidence before
H3 can pass.
