# FEATURE-ci-tls-ca-fixture: make independent TLS fixture chains strictly valid

Status: In Progress — implementation complete; caller validation/finalization pending.
Base: local `v3` at `f3e01f9dddc07c5aa94051b89220095d05c47f53`.
Evidence: `.debug/ci-tls-ca-fixture-a1jpmct9/journal.md` and its `implementation/` logs.

The macOS checkout package job `120468733495` (run `39969683050`) fails
`check-v3-public-tls.py` at its initial SNI loop, line 117, with certificate
verification error 92: CA certificate lacks key usage. Those initial connections
serve the copied static a/b certificates, through intermediate.pem and root.pem.
Real strict verification reproduces missing key usage on both static CAs.
The generated rotation chain has a separate instance of incomplete extensions:
strict verification reports missing leaf authority key identifier (85), root
key usage (92), and root subject key identifier (86). A rotation-only repair
would leave the observed CI failure intact.

Action items:

- [x] Read the plan correction, literal journal and actual macOS traceback; isolate
  this worktree from the exact local v3 base and preserve other work.
- [x] Observe RED with the real independent C++ fixture generator, copied static
  credentials, and real strict TLS handshakes, including error 92 on a/b and
  error 85 on rotation. Repeat the causal original/repaired comparison with
  the existing supported OpenSSL 3.5.9 verifier.
- [x] Add critical certificate-signing/CRL-signing key usage to both static CAs;
  reissue their chain while preserving existing a/b leaf private/public keys,
  all subjects/issuer names, exact validity dates and serial numbers.
- [x] Add rotation root key usage/SKI and leaf AKI before signing, without changing
  the intentionally malformed ACME generator variants or client verification policy.
- [x] Retain reproducible server-fixture regeneration in the existing independent
  tool; preserve all mTLS credentials and private-key files byte-for-byte.
- [x] Verify actual strict handshakes, hostname/untrusted-chain rejection, valid
  mTLS acceptance and rejection for expired, untrusted and wrong-purpose clients.
- [x] Register/distribute the focused test in TLS-on check-local; run the configured
  standalone Make target, bootstrap, changed-file cpplint, syntax and diff checks.
- [ ] Caller: Groundwork validation and finalization into v3.
- [ ] CI / v3 PR: native macOS/Python 3.14 checkout/archive public TLS matrices.

Implementation and reproduction:

`test/installed/tls_client.cpp` already generates the installed-consumer rotation
and ACME fixtures independently of libhttpserver. Its `regenerate-server DIR`
mode now regenerates only root.pem, intermediate.pem, a.pem and b.pem using the
existing certificates as templates. For example, after compiling that tool
against the selected provider, run `independent-client regenerate-server
COPIED_FIXTURE_DIRECTORY`. Its existing `fixtures DIR` mode remains separate.
Original CA private keys were never tracked; changing signed CA extensions
requires replacement test CA keypairs and re-signing the server chain. New
RSA-2048 CA keys are ephemeral and discarded. Regeneration reproduces the
chain/metadata invariant with fresh random CA keys, rather than byte-identical
certificates. All leaf keys, exact dates/serials and negative client credentials
remain unchanged. Only CA public keys, related key identifiers and signatures
change in the four static certificates, in addition to the new CA key usages.

OpenSSL's [strict verification documentation](https://docs.openssl.org/3.5/man1/openssl-verification-options/)
requires key usage for CAs, SKI for version 3 CAs, and AKI for non-self-signed
version 3 certificates. The repair satisfies those chain checks without changing
runtime code, security policy, provider selection, dependencies or CI pins.

Local verification and limits:

- Seven focused tests pass through `make check-v3-tls-fixtures` in a real
  TLS-on configured build using existing OpenSSL 3.5.9 headers/static libraries.
  The test compiles/runs the actual independent C++ fixture tool, exchanges real
  TLS records through Python SSL MemoryBIOs, and sends application data after
  successful handshakes. Python 3.12.3 uses system OpenSSL 3.0.13 with
  `VERIFY_X509_STRICT` enabled explicitly to match the newer Python client policy.
- The supported OpenSSL 3.5.9 executable separately verifies original a/b chains
  fail with error 92 and original rotation fails with 85/92/86. Repaired a/b and
  rotation pass `verify -x509_strict -purpose sslserver -verify_hostname ...`.
  This executable links the existing provider statically; changing LD_LIBRARY_PATH
  does not replace Python's separate system OpenSSL engine.
- Wrong host fails specifically with verification code 62; untrusted server
  chains fail with 20/21. mTLS negatives fail specifically with 20 (untrusted),
  10 (expired), and 26 (wrong purpose); the valid client completes a handshake.
- Original/repaired actual generator outputs retain identical extension content
  for all twelve ACME variants. The ACME generator loop is unchanged, including
  expired/future date logic. No malformed-certificate acceptance is claimed.
- A regeneration test checks certificate dates/serials/subjects/issuer names,
  unchanged leaf public keys, every untouched file byte-for-byte, and strict a/b
  handshakes from a fresh regeneration. A retained pre-change hash/metadata
  manifest also confirms original leaf keys and mTLS fixture bytes were preserved.
- `./bootstrap`, real TLS-on configure, configured focused Make target,
  `cpplint test/installed/tls_client.cpp`, Python AST syntax and `git diff --check`
  pass. Bootstrap retains existing aminclude.am .PHONY / AC_OUTPUT warnings;
  the C++ compiler retains the existing fclose-deleter attribute warning in the
  pre-existing fixture function. No new build warning was introduced.
- No broad baseline, full public TLS matrix or nonlocal platform run was started.
  Native macOS/Python 3.14 execution remains CI / v3-PR owned, not passed locally.
  Prior Darwin deadline, old Linux parity and concurrent Windows install
  uncertainties remain closed to further diagnosis under this task.
