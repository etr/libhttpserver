# Packaged release adapters

TASK-199 provides two bounded families for TASK-185 to call after updating from
`v3`: `public-capabilities` and `native-throughput-latency`. The adapters do not
import TASK-185, discover another checkout, select a fallback installation, or
replace its release inventories. TASK-185 owns its final runner wiring and full
release acceptance.

## Python interface

Import `scripts/lib/v3_release_package_adapters.py` from the extracted archive:

```python
run_public_capabilities(inputs, output, run_id)
run_native_benchmarks(inputs, output, run_id, workload)
validate_public_receipt(path, expected_identity, expected_run_id)
validate_benchmark_receipt(path, expected_identity, expected_run_id,
                           require_full_workload=False)
identity(inputs, workload=None)
```

`inputs` is a mapping or argparse namespace with `archive`, `archive_sha256`,
`source`, `build`, `prefix`, `provider_prefix`, `tls`, `compiler`, `client_python`,
`node`, and `quic_go`. Paths identify the selected archive, its unchanged
extraction, out-of-source build, staged installation and provider. `tls` is
`yes` or `no`. Every execution uses a caller-selected new 32-character lowercase
hexadecimal run ID and a previously nonexistent output directory. Existing
outputs, including failed attempts, are preserved.

The supported clients are hyper-h2 4.4.1, aioquic 1.3.0, Node 24.15.0, and the
quic-go v0.55.0 public client built from the distributed Go sources. The compiler
must support C++20. TLS-on requires the explicitly selected OpenSSL 3.5 LTS
provider. Public TLS ACME tests bind authentic TCP port 443; use the established
isolated network namespace on hosts that cannot provide that port. Altering the
port or trusting injected local-port metadata is not an alternative witness.

## CLI and independent validation

```sh
CLIENT_PYTHON scripts/run-v3-packaged-adapters.py \
  --family public-capabilities \
  --archive ARCHIVE --archive-sha256 EXPECTED_SHA256 \
  --source EXTRACTED_SOURCE --build BUILD --prefix PREFIX \
  --provider-prefix PROVIDER --tls yes \
  --compiler COMPILER --client-python CLIENT_PYTHON \
  --node NODE --quic-go QUIC_GO --run-id RUN_ID --output NEW_OUTPUT
```

Repeat with a distinct installation/build/output for TLS-off. Each public lane
contains four shared/static × managed/external cells for H1, RFC6455, TLS, H2
and H3. Encrypted protocols have typed pre-bind rejection witnesses in TLS-off.
Supported credentials-backed TLS, H2 and explicit UDP H3 configurations bind
and complete independent protocol exchanges in TLS-on; missing credentials,
invalid configuration and mixed rollback remain separate negative cases.

Select `--family native-throughput-latency` for benchmarking. Defaults remain
4096 measured requests, 256 warmup requests (protocol epochs may increase
warmup), 1024-byte payloads, four workers, H1/H2 concurrency eight, H3 concurrency
four, and a 300-second client deadline. `--requests 16 --warmup 4` is reduced
integration evidence. It cannot satisfy `--require-full-workload`.

To validate in a new process, use the same selected inputs, workload and run ID
with `--validate NEW_OUTPUT/receipt.json`. Validation rehashes the archive,
extraction, build, installation, provider, tools and Python client modules. It
reads evidence without modifying it. For full benchmark release proof add
`--require-full-workload`; TASK-185 must also apply its existing release gates.

## Receipt schema and retained artifacts

The outer JSON receipt has `schema_version=1`, `family`, `status`, `run_id`,
`identity`, `inventory`, `inventory_sha256`, `children`, `full_workload`,
`artifact_roots`, `artifact_extras`, `artifacts` and `nonlocal_checks`. Failed
attempts retain `status=failed` and `error` and cannot pass validation.

`identity` binds the archive digest and every regular distributed source member,
source/build/install paths, installed header/library/pkg-config hashes,
configuration hashes, provider files, executable hashes, pinned client versions
and module hashes, and workload. Archive paths must be safe, consistent,
unique and regular files/directories; symlink/hardlink members are rejected.
A genuine extraction needs no `.git` directory.

`children` maps the exact family members to receipt artifact records. Every
artifact record contains absolute `path`, `bytes` and `sha256`. The complete
catalog covers child receipts, command outputs, process receipts, copied
consumers/fixtures/client code, objects, depfiles, link maps, executable files
and benchmark raw samples. Compilation must use installed public headers,
C++20, and archive-distributed copied sources. Private headers, source/build
includes and `HTTPSERVER_COMPILATION` cannot supply public proof. Static link
commands/maps must name the selected installed archive; shared runtime
inspection must resolve the selected installation. Readiness alone cannot
supply response proof. Child inventories and decoded lifecycle/semantic
observations remain subject to their original validators.

TLS authentication rejection controls retain the independent provider client's
exit 2 only with a decoded `connected=false` handshake and the exact bounded
client argument shape. The explicit peer-close cancellation control retains its
intentional killed-client terminal. These controls cannot supply a successful
protocol exchange; positive clients and server cleanup must exit zero.

Keep the archive, extraction, builds, installs, provider, clients, output roots
and recorded external consumer directories available for revalidation. Catalog
pruning, deleted provenance, changed process output, altered samples/statistics,
reused run IDs and different expected package/archive identities fail. Do not
copy historical receipts into a new execution. Nonlocal BSD/macOS/Windows
checks are explicitly unexecuted locally and belong to CI and the v3 PR.

## Contract checks

```sh
python3 scripts/test-v3-packaged-adapters.py
python3 scripts/test-v3-installed-consumer.py
python3 scripts/test-v3-public-http2.py
python3 scripts/test-v3-public-http3.py
python3 scripts/test-v3-native-benchmark.py
```

These dependency-free tests use temporary real artifacts and deterministic
failure injection. Synthetic parsing fixtures are not live integration proof.
Ordinary `make check` runs contracts, never the live network matrix or full
benchmark. Fresh execution identities and outcomes are recorded separately in
TASK-199 evidence, preserving historical TASK-179 classifications.
