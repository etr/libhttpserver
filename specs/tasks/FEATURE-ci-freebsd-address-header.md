# FEATURE-ci-freebsd-address-header: include the address-family declaration owner

Status: Complete (local implementation, validation and finalization; native CI pending).
Base: `v3` at `98dfdc4de8d6be5355c0d2f9b94518ea53a6fc8d`.

FreeBSD package run `40030060276` / job `120675819652` and archive run
`40030063567` / job `120676108128`, source
`0cbf18116f63e0762aaad19b16358e6f61dddf04`, both fail compiling
`src/net/address.cpp`: `AF_INET` and `AF_INET6` are undeclared in the two
`inet_pton` and two `inet_ntop` calls. Each compile reports four errors;
`net/address.lo` fails and outer `gmake` exits 2. These actual native compile
failures are the RED evidence preceding this change.

The [FreeBSD 14.3 socket header](https://github.com/freebsd/freebsd-src/blob/releng/14.3/sys/sys/socket.h)
defines the address-family macros (lines 216 and 246). The
[FreeBSD 14.3 inet manual](https://man.freebsd.org/cgi/man.cgi?query=inet_pton&sektion=3&manpath=FreeBSD+14.3-RELEASE)
includes `sys/socket.h` in the synopsis. Its
[arpa/inet.h](https://github.com/freebsd/freebsd-src/blob/releng/14.3/include/arpa/inet.h)
declares the conversion routines but does not include that macro owner.
The bounded header-ownership question is resolved: include `sys/socket.h`
directly in the private POSIX include arm of `src/net/address.cpp`.

Action items:

- [x] Read the accepted plan, literal journal, primary CLAUDE guidance, owning
  headers, implementation, existing address tests and v3 source ownership.
- [x] Create the Groundwork identity helper's exact isolated worktree/branch
  from the exact local v3 base; preserve unrelated bauth work.
- [x] Inspect both actual native compile failures and verify declaration
  ownership against official FreeBSD 14.3 sources/manual before editing.
- [x] Add only the direct private POSIX declaration include; preserve the
  Windows arm, installed headers, conversion calls and address semantics.
- [x] Compile the actual changed source and execute the existing IPv4/IPv6
  address unit suite locally; run changed-C++ cpplint and diff checks.
- [x] Record local checks, native evidence limits and implementation handoff
  in the existing journal.
- [x] Caller: Groundwork validation and local finalization into v3.
- [ ] CI / v3 PR: post-fix native FreeBSD source/package/archive execution.

No new source-text assertion is appropriate for this one-include repair:
the native compile failures establish RED, and the existing address suite
already exercises actual parsing, formatting, mapped-address normalization,
CIDR/wildcard matching and invalid spellings. A focused Linux baseline
compile and its 13 existing address tests passed before editing; this is
local baseline evidence, not a reproduction of the FreeBSD failure.

Post-fix verification: GNU g++ 13.3.0 compiled the actual changed
`src/net/address.cpp`, `src/detail/net_address.cpp` and existing
`test/unit/net_address_test.cpp` with `-std=c++20 -Wall -Wextra
-DHTTPSERVER_COMPILATION -Isrc -Itest`, with no compiler output. The resulting
local Linux executable passed all 13 tests and 92 checks, with no skips.
`cpplint src/net/address.cpp` and `git diff --check` pass. These bounded
checks do not constitute a whole-library or native FreeBSD package/ABI pass.

Project notes: this focused address suite can link the two real address
translation units directly, without Autotools setup or a full native build.
No source fallback constants, SDK simulation or additional test framework is
needed. The primary checkout's untracked CLAUDE.md supplies C++ conventions
even when absent from an isolated worktree.

Raw evidence and the journal remain in the primary checkout at
`.debug/ci-freebsd-address-header-or7k_xeb/`. Post-fix native FreeBSD checks
remain incomplete and CI/v3-PR owned under AGENTS. The unknown five Darwin
C++ causes and earlier closed investigations remain closed; no unrelated
runtime, fixture, SDK, compiler or workflow repair is included.

Groundwork validation `02b0951d488a1bcb1bf8efe5c421cd89` passed in one
iteration, with all nine independent profiles approved, zero fixes and zero
unworked findings. It sealed implementation HEAD `2ecf3875d72831688df7508f8a1443bc5a6cb4e5`.
Overall CI remains unsuccessful until new native checks complete.
