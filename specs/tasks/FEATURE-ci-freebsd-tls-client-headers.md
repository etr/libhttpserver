# FEATURE-ci-freebsd-tls-client-headers

Status: Implemented — local checks complete; caller validation/finalization pending.
Base: local v3 `f4983add9dea7db5444a0d3c9e7a1e95f713e601`.

Include `<netinet/in.h>` directly in the independent TLS client's existing
POSIX include branch so FreeBSD receives the owning declarations for
`sockaddr_in` and `INADDR_LOOPBACK`.

- [x] Verify retained native FreeBSD RED and original source SHA256.
- [x] Add only the owning IPv4 header under the non-Windows branch.
- [x] Preserve the client body, Windows headers, provider/runtime policy,
  producer script, and committed TLS credential fixtures.
- [x] Compile/link the actual provider-only client with GNU C++20 and the
  existing private OpenSSL 3.5.9 headers/static libraries; never link
  libhttpserver.
- [x] Check the actual owning TU with the existing Linux LLVM 19.1.1 compiler
  and libc++ 20.1.8 headers in C++20 syntax-only mode.
- [x] Pass changed-file cpplint and `git diff --check`.
- [ ] Caller Groundwork validation and local finalization/merge into v3.
- [ ] Native FreeBSD post-fix compile: CI/v3 PR owned, incomplete locally;
  not a local completion blocker under project policy.

Evidence: `.debug/ci-d70-freebsd-tls-client-headers/journal.md` and its
`implementation/` directory in the primary checkout. Actual FreeBSD job
`121129726385` from archive run `40180213278` rejects the generated client at
lines 240–241 with five errors for missing IPv4 declarations. The producer
copies the tracked client byte-for-byte; the original source SHA256 is
`dd0b08da7e38fe2aebdfe1be05a7d9a357eefb09880da2dbd4cc614eeeef6a74`.
That retained native compile failure supplies RED; no mirrored include test
was added for this one-line header repair.

Local checks passed with the existing private provider at
`build/tooling/provider` (OpenSSL 3.5.9), rather than the separately installed
system OpenSSL 3.0.13. GNU 13.3 compiled/linked the complete client using only
the provider libraries and standard runtime dependencies. One GCC warning
about the unchanged `fclose` deleter template argument is retained in the
compile log. The libc++ check is local Linux syntax evidence, not native
FreeBSD ABI or runtime proof.

No client execution, fixture generation, handshake, full suite, SDK/provider
build, foreign probe, or network execution was performed. Earlier Darwin
header, Windows WSAPoll, and post-summary unknowns remain closed; kqueue
CLOSED25 and epoll CLOSED64 diagnostic budgets are unchanged. Caller owns
validation, merge, cleanup, and publication.
