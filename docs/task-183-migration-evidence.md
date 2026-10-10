# TASK-183 migration documentation and example evidence

Local implementation checks passed on Linux x86_64 (WSL2 kernel 6.6.87.2),
using Ubuntu Clang 18.1.3, C++20, and the genuine OpenSSL 3.5.9 provider at
`/home/etr/progs/libhttpserver/build/tooling/provider`. Its actual library directory
is `lib`, containing `libssl.a` and `libcrypto.a`. Public encrypted listeners and
public credential installation/rotation remain unavailable; no library transport
or public API implementation changed in this task.

## Reproduction and results

Run `./bootstrap`, then configure separate fresh VPATH build directories from
the task worktree. The local directories are `build/task183-off` and
`build/task183-on`. All build/test logs below are retained there and are ignored
build artifacts, not documentation sources committed to the repository.

```sh
# From build/task183-off:
PATH=/tmp/task183-tools:$PATH CXX=clang++ CXXFLAGS='-std=c++20 -O0 -g' \
  ../../configure --disable-v3-tls --enable-examples --disable-doxygen-dot
make -j4
make -C examples -j4

# From build/task183-on:
PATH=/tmp/task183-tools:$PATH CXX=clang++ CXXFLAGS='-std=c++20 -O0 -g' \
  ../../configure --enable-v3-tls --enable-examples --disable-doxygen-dot \
  V3_TLS_CFLAGS='-I/home/etr/progs/libhttpserver/build/tooling/provider/include' \
  V3_TLS_LIBS='-L/home/etr/progs/libhttpserver/build/tooling/provider/lib -lssl -lcrypto -ldl -pthread'
make -j4
make -C examples -j4
```

Doxygen was absent initially. Ubuntu Doxygen 1.9.8 and its libfmt9 runtime were
downloaded/extracted under `/tmp/task183-tools`, with a local wrapper on PATH.
Graphviz was absent; `--disable-doxygen-dot` disables optional graphs while
still generating all requested API pages and code inclusions. This is actual
Doxygen generation, not the checker's missing-tool skip.

The focused selection in both build directories was:

```sh
make -C test -j4 check \
  TESTS='v3_examples route_sync native_server task_stop_inside_handler server_options_validate server_routes_families http2_exchange http3_exchange http1_websocket_upgrade http2_websocket http3_websocket integ/v3_examples_test.py ../scripts/test-doxygen-class-fragments.py' \
  check_PROGRAMS='v3_examples route_sync native_server task_stop_inside_handler server_options_validate server_routes_families http2_exchange http3_exchange http1_websocket_upgrade http2_websocket http3_websocket'
```

TLS-on adds `tls_credentials tls_credentials_rotation v3_tls_rotation` to both
lists. Overriding `check_PROGRAMS` keeps this an explicit focused check rather
than building every unrelated test executable.

| Local check | Result and retained log |
| --- | --- |
| Baseline route/native behavior | `route_sync` and `native_server` passed; also retained in final focused selection |
| Full C++20 library/examples build, TLS-off/on | Passed; `build-final.log`, `examples-green.log` |
| Focused TLS-off Automake selection | 13/13 passed; `focused-final2.log` |
| Focused TLS-on Automake selection | 16/16 passed; `focused-final2.log` |
| Shared example handler semantics | Four tests, 60 successful checks; `test/v3_examples.log` |
| Public example loopback smoke | Sync GET, HEAD length/no-body, binary coroutine POST, exact cap/413, handler stop, HTTP/1.1 WS rejection/text/binary echo/close, external loop, protocol options; `test/integ/v3_examples_test.log` |
| Private rotation demonstration | Old generation 1 retained, active 2, invalid replacement preserves 2; `test/v3_tls_rotation.log` |
| Independent rotation TLS peers | 2 tests / 31 checks passed; established connection survives replacement; `test/tls_credentials_rotation.log` |
| `make check-local` | Passed in both builds, including header hygiene, native linkage, README/reference invariants and documentation gates; `check-local-final.log` |
| `make check-doxygen` | Zero substantive warnings in both builds; `doxygen-final2.log` |
| Changed C++ cpplint | All seven authored C++/header files passed; `/tmp/task183-cpplint.log` |
| Python syntax/filter checks | `py_compile` passed; filter's four behavioral tests passed standalone and via Automake |
| Installed examples | Established `verify-installed-examples.sh`: 41 built, 1 legacy WS feature skip, 0 failures; `installed-examples.log` in TLS-off build |
| Installed native route lifetime smoke | Direct installed-header/shared-library consumer exited successfully after one second; `installed-routes-smoke.log` |
| Distribution and whitespace | `make dist`, tarball source inventory and `git diff --check` passed |

The TLS-off Python example smoke deliberately skips its private rotation case;
the other five cases pass. TLS-on executes all six cases. Automake reports no
skipped selected executables in either mode. The legacy WebSocket example is
skipped in installed verification because MHD WS is disabled; the native WS
example compiles and runs independently.

Installed verification used:

```sh
BUILD_DIR=/home/etr/progs/libhttpserver/.worktrees/TASK-183/build/task183-off \
  INSTALL_PREFIX=/tmp/task183-installed-off CXX=clang++ CXXFLAGS='-std=c++20' \
  scripts/verify-installed-examples.sh
clang++ -std=c++20 -I/tmp/task183-installed-off/include examples/v3_routes.cpp \
  -L/tmp/task183-installed-off/lib -Wl,-rpath,/tmp/task183-installed-off/lib \
  -lhttpserver -o /tmp/task183-consumer-routes
/tmp/task183-consumer-routes 1
```

The host has MHD headers/libraries but lacks `libmicrohttpd.pc`; pkg-config
consumer recipes require complete prerequisite metadata. Installed compilation
was therefore verified through the existing direct-link script and explicit
command above, rather than fabricating missing metadata.

## Documentation and proof boundaries

Generated HTML contains `native_v3_api`, native_server, exchange,
route_registry and WebSocket session pages, and includes the actual protocol,
route/handler, WebSocket and external-loop example sources. A bounded local
HTML probe retained page paths in each build's `doc-proof.json`; it confirmed
that no credential-registry page or symbol appears. Generated output remains
under each build's `doxygen-doc/` directory.

Recursive discovery exposed an existing documentation problem: five legacy
class-body include fragments were parsed as global functions. The new narrow
Doxygen filter expands only those five public fragments into their owning
classes, and excludes their standalone duplicate inputs. Four filter tests pin
in-place expansion, untouched unrelated/private includes, and failure when a
required fragment is missing. No private include is expanded by this filter.

The shared handler header is named `v3_route_handlers.hpp` so that standalone
example sources compile with their local sibling headers in the established
installed-example verifier. It contains the actual functions used by the route
example and semantic tests. Those HTTP/1.1/H2/H3 tests are semantic evidence;
public protocol options demonstrate typed rejection, not H2/H3 interoperability.
The separately labeled private rotation program is not a public consumer;
independent TLS handshake evidence stays in the existing rotation suite.

RED evidence was recorded before adding runnable examples, shared handlers,
private rotation, and the Doxygen filter:
`/tmp/task183-examples-red.log`, `/tmp/task183-semantic-red.log`,
`/tmp/task183-rotation-red.log`, `/tmp/task183-doxygen-filter-red.log`.
The example smoke failed for missing example binaries, the semantic check for
its missing shared handler header, and the filter tests for the missing filter.

The migration guide records intentional route/hook/ownership/peer changes and
protocol applicability exclusions, linked to TASK-179 and the parity inventory.
It does not claim the private transport audit enables public TLS/H2/H3 listeners.
TASK-184 retains aggregate dependency removal and ABI cutover. BSD, Windows,
macOS and other nonlocal runs were not executed locally; CI and the v3 PR own
those checks under the repository's task policy.
