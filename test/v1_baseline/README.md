# v1 baseline measurement workflow

This directory pins the v1 (master-branch) baseline numbers consumed
by the bench TUs in `test/bench_*.cpp`. The literals live in
[`v1_constants.hpp`](v1_constants.hpp) and are sampled ONCE on
`master`; they are not computed at v2.0 build time because the v2.0
data types (`http_resource` without `method_state`, `http_request`
with a per-call cache) cannot reproduce the v1 numbers.

See [`test/PERFORMANCE.md`](../PERFORMANCE.md) for the methodology
and acceptance criteria; this README is the operational recipe for
re-measuring when the build host's stdlib / libmicrohttpd version
drift.

## When to re-measure

Re-run the measurement TUs and update `v1_constants.hpp` if any of
the following change on the build host:

- libstdc++ major version (affects `sizeof(std::map<...>)` and the
  libstdc++ `get_headers()` ns/call constant)
- libc++ major version (affects the libc++ counterparts of both)
- libmicrohttpd major version (affects `get_headers()` ns/call only)
- Compiler vendor (affects `std::map` layout via stdlib choice)

Both the sizeof constants and `V1_GET_HEADERS_NS_PER_CALL` are selected
per-stdlib, so each has a libc++ branch and a libstdc++ branch
— re-measure and update only the branch for the stdlib you are on.

A re-measurement is a one-commit change: update the relevant constants
in `v1_constants.hpp`, update the "Baseline values" table in
`PERFORMANCE.md`, and rerun `make bench` to verify the bench
assertions still pass.

## Procedure

### Step 1 — set up a master worktree

From the libhttpserver clone. `WORKDIR` is reused by Steps 2 and 3 below,
so keep it set in the same shell session:

```sh
WORKDIR=$(mktemp -d)
git worktree add "${WORKDIR}/libhttpserver-v1" master
```

### Step 2 — measure `sizeof(http_resource)` and `sizeof(std::map<std::string,bool>)`

This needs only the v1 headers (no library link). From this v2.0
worktree:

```sh
c++ -std=c++20 -O3 -DHTTPSERVER_COMPILATION \
    -I"${WORKDIR}/libhttpserver-v1/src" \
    test/v1_baseline/measure_v1_sizes.cpp \
    -o "${WORKDIR}/measure_v1_sizes"
"${WORKDIR}/measure_v1_sizes"
```

Expected output on Darwin/arm64 (Apple clang / libc++):

```
sizeof(http_resource)             = 32
sizeof(std::map<std::string,bool>) = 24
```

On Linux x86_64 (GCC / libstdc++):

```
sizeof(http_resource)             = 56
sizeof(std::map<std::string,bool>) = 48
```

Record the two numbers as `V1_HTTP_RESOURCE_SIZEOF` and
`V1_STD_MAP_STRING_BOOL_SIZEOF` in `v1_constants.hpp`.

### Step 3 — measure v1 `get_headers()` ns/call

This TU stubs `MHD_get_connection_values` itself; it does not need
the v1 library link or a running daemon. It does need
`microhttpd.h` for the type declarations.

`V1_GET_HEADERS_NS_PER_CALL` is selected **per-stdlib**, so
re-measure it on the stdlib whose constant you are updating; the two
values live behind `#if defined(__GLIBCXX__)` / `#elif
defined(_LIBCPP_VERSION)` branches in `v1_constants.hpp`.

On macOS / Apple clang / libc++ (Homebrew `microhttpd.h`):

```sh
c++ -std=c++20 -O3 \
    -I/opt/homebrew/include \
    test/v1_baseline/measure_v1_get_headers.cpp \
    -o "${WORKDIR}/measure_v1_get_headers"
"${WORKDIR}/measure_v1_get_headers"
```

On Linux / GCC / libstdc++ (Ubuntu `apt-get install libmicrohttpd-dev`;
the verify-build.yml performance lane uses `g++-14`):

```sh
g++-14 -std=c++20 -O3 -DNDEBUG \
    -I/usr/include \
    test/v1_baseline/measure_v1_get_headers.cpp \
    -o "${WORKDIR}/measure_v1_get_headers"
"${WORKDIR}/measure_v1_get_headers"
```

Sample output (Apple Silicon, Apple clang 21, libc++):

```
v1_get_headers_ns_per_call=767.665
  (min=756.367 max=783.927)
```

Sample output (g++-14, libstdc++ `__GLIBCXX__=20240908`, native
aarch64):

```
v1_get_headers_ns_per_call=667.319
```

Take the rounded **lower** end of the observed range as
`V1_GET_HEADERS_NS_PER_CALL` for that stdlib's branch (we commit a
conservative number so the ≥10× ratio assertion has comfortable margin
under host jitter; a lower v1 baseline can only make the gate stricter,
never spuriously easier).

### Step 4 — update the constants

Edit `test/v1_baseline/v1_constants.hpp` to reflect the new
measurements. Also update the corresponding cells in the "Baseline
values" table in `test/PERFORMANCE.md` and the comment block in
`v1_constants.hpp` (commit SHA, host triple, compiler, stdlib).

### Step 5 — re-run the bench

```sh
cd build
make bench
```

If both bench TUs pass, the re-measurement is complete. Commit the
constants change and the PERFORMANCE.md edits together.

## Files in this directory

| File | Purpose |
|---|---|
| `v1_constants.hpp` | The literal baseline constants consumed by the bench TUs. **Edit when re-measuring.** |
| `measure_v1_sizes.cpp` | One-off measurement TU for the two sizeof numbers. Builds against `master`. |
| `measure_v1_get_headers.cpp` | One-off measurement TU for v1's get_headers ns/call. Self-stubs MHD. |
| `README.md` | This file. |

None of these are built by `make all`, `make check`, or `make bench`
— they ship in `EXTRA_DIST` for tarball reproducibility only.
