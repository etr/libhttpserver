# Native v3 packaged benchmarks

`make bench` is opt-in. It compiles an optimized C++20 server outside the source
and build trees using the selected installed public headers and library. Independent
Python clients run in separate processes. TLS-on requires all sixteen cells:
shared/static × managed/external × H1 plaintext, H1 TLS, H2 TLS and H3 QUIC.
TLS-off runs four plaintext cells and checks typed `not_supported` with no bound
port for encrypted protocols. Unavailable protocols never count as measurements.

The measurement clients require Python 3 with hyper-h2 **4.4.1** and aioquic
**1.3.0**. These are test dependencies only. Missing clients fail preflight.
Select an OpenSSL 3.5 LTS provider explicitly. For example, from a fresh checkout:

```sh
bash bootstrap
python3 -m venv /tmp/native-benchmark-clients
/tmp/native-benchmark-clients/bin/pip install h2==4.4.1 aioquic==1.3.0
mkdir -p build/native-on
cd build/native-on
CXX=clang++-18 CC=clang-18 CXXFLAGS='-O2 -DNDEBUG' \
 V3_TLS_CFLAGS="-I$PROVIDER/include" \
 V3_TLS_LIBS="-L$PROVIDER/lib -lssl -lcrypto" \
 ../../configure --enable-v3-tls=yes --disable-examples --prefix="$PREFIX"
make -j4
make install
make bench BENCH_PREFIX="$PREFIX" BENCH_PROVIDER_PREFIX="$PROVIDER" \
 BENCH_TLS=yes BENCH_OUTPUT="$NEW_OUTPUT" \
 BENCH_PYTHON_COMMAND=/tmp/native-benchmark-clients/bin/python
```

Repeat with a distinct fresh build/prefix/output and `--enable-v3-tls=no`,
`BENCH_TLS=no`. Library and consumer optimization flags are recorded. The runner
rejects nonoptimized libraries, sanitizer flags/symbols and allocator preload.
Sanitizer measurements are correctness evidence and cannot enter comparisons.
The direct entry point provides `--source`, `--build`, `--prefix`,
`--provider-prefix`, `--tls`, `--compiler`, `--client-python` and `--output`.

The full workload has 4096 measured requests, 1024 response bytes and four
server workers. H1/H2 concurrency is 8; H3 concurrency is 4. H1 warmup is 256.
H1 uses eight persistent connections;
H2 uses 64 connection epochs, each retaining one authenticated connection for
eight warmup requests and 64 measured requests with eight concurrent streams:
**512 actual warmup requests** and 4096 measured requests. The public default
limits each H2 connection to 128 stream openings per second. Epochs stay below
that allowance without changing production policy or adding artificial pacing.
H3 has sixteen live-request seats and bounded lifetime stream records, so its
full workload uses
1024 connection epochs, each with four concurrent warmup requests followed by
four measured streams: **4096 actual warmup requests** and 4096 measured requests.
Every H3 epoch performs verified connection setup, completes warmup, reuses that
connection for measurement, then closes it. The normal per-connection semantic
data pool is capped at 393216 bytes; each active request reserves body and
response buffers plus metadata. Concurrency 8 can produce H3_EXCESSIVE_LOAD
refusals; concurrency 4 stays within the default admission capacity. This
benchmark changes no production resource policy. Each epoch uses a new retained QUIC connection.
The full cell deadline is 300 seconds, including setup and shutdown of client
connections. Server startup and orderly shutdown each have an additional
15-second bound. `--deadline` permits 1..600 seconds; other bounded overrides are
`--warmup`/`--requests` (1..65536), `--concurrency` (1..8), `--h3-concurrency` (1..4, capped by
`--concurrency`), `--payload` (1..16384) and `--workers` (1..32). H2/H3 epoch warmup follows concurrency, independent of the
H1 warmup option. Overrides appear in the receipt; any deviation from defaults
is labelled a reduced/custom workload rather than full acceptance evidence.

Timestamps are integer `time.monotonic_ns()` nanoseconds with clock resolution
recorded. A request starts immediately before dispatch and ends at complete
response receipt. Clients verify protocol negotiation, certificate trust and
`a.example`, status 200, exact payload and run/request identity. Setup and all
warmup samples are retained separately. Each measurement window starts after
successful warmup and ends at its last measured completion. Duration is the sum
of nonoverlapping measured windows in seconds; throughput is successful measured
completions divided by that sum, in requests/second. For H2/H3 this excludes each
epoch's handshake, warmup and close, and is **not whole-process wall throughput**.
Whole-process wall seconds are also recorded. Differing protocol concurrency
and connection reuse settings make these observations unsuitable for a direct
protocol performance ranking. Latency percentiles use every
successful measured sample without trimming: sort durations, select one-based
rank `ceil(N*p/100)`, and display p50/p95/p99 in milliseconds. Raw request IDs,
epoch membership and timestamps permit independent recomputation.

Each attempt must use a new output directory. Failures remain there. Receipts
reference artifacts by absolute path, byte count and SHA-256, including raw samples,
server observations, commands, executable, depfiles and link maps. Keep the
recorded external consumer directory and package/provider/client installations
alongside the receipt. Deleting or changing these invalidates verification. The
server independently counts successful warmup/measured response writes and errors
after orderly shutdown. Client exit zero alone is insufficient.

`validate(path, expected_identity, expected_run_id)` is available from
`scripts/run-v3-native-benchmarks.py` for release adapter integration. Offline
verification checks current source/package/build/workload identities and every
artifact, then recomputes samples/statistics and compares server counts. A caller
requiring new execution must obtain the fresh runner's run ID; revalidating an old
receipt is integrity verification, not a new benchmark. CLI `--validate RECEIPT`
accepts `--expected-run-id`. Hashes establish integrity against recorded inputs;
fresh execution binds accepted evidence to the current run. They are not
cryptographic attestation against an attacker controlling the producer and all
artifacts. Synthetic parsing fixtures cannot substitute for a live attempt.

PRD §3.9 and the repair roadmap specify no absolute native throughput/latency
minimum or comparable regression baseline. Historical `test/PERFORMANCE.md`
v1/v2 numbers and unrelated 10×/2× claims do not apply. Acceptance requires full
successful bounded measurements, exact counts, finite positive throughput,
ordered finite percentiles, zero unexpected errors, and unchanged TASK-180
resource gates: three output/upload saturation/reset/refill cycles, stalled
observations >=0.99 seconds, CPU/wall <=0.2 of one core and warm RSS growth
<=1,048,576 bytes. Run the existing normal `native_protocol_memory`,
`resource_limits`, installed public H2/H3 drivers and 90-second resource probe
separately; benchmark throughput does not replace their proof. Private resource
fixtures remain supporting evidence. Nonlocal platform checks belong to CI/v3 PR.
