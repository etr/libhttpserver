# FEATURE-ci-windows-staging: keep DOS drives out of private staged paths

Status: Complete (local) — validated and finalized into v3; native Windows CI remains pending.
Base: local `v3` at `71008fd7fa0e5e2af00feb3c1e619ab7a13a4aaa`.

Windows checkout job `120468733874` (run `39969683050`) and archive job
`120469466874` (run `39969790346`) fail the standalone hygiene install.
Concatenating DESTDIR with the configured `D:/...` prefix produces
`.hygiene-stageD:/...`; MSYS passes a malformed path to native ranlib.
The configured native prefix remains valid for native consumers and pkg-config.

Action items:

- [x] Read the saved plan, literal Windows journal and both retained failed logs;
  create the exact Groundwork worktree/branch from local v3.
- [x] Observe RED through the real configured top-level hygiene/shared recipes,
  recursive Automake header/data installation and installed-header compiler checks.
  Cover custom DOS destinations as well as the default DOS prefix.
- [x] Normalize private staging destinations from `D:/path` to `/D/path`, retain
  distinct drive components, and use the same include mapping for both consumers.
- [x] Apply the mapping to all current install destinations: libraries, DLL bin,
  public headers, pkg-config, CMake modules and manuals (including custom man3dir).
  Keep configured prefix, generated metadata, normal installation and Unix paths intact.
- [x] Test real generated Makefiles, Automake/libtool library installation and
  ranlib using a tiny compiled fixture library; preprocess/compile installed
  production public headers and run the real alias hook.
- [x] Verify standalone and shared check-local staging, single-install reuse,
  hygiene stamp reuse, cleanup after success/install failure and missing-stage errors.
- [x] Preserve negative controls for backend leakage, private/detail/impl files,
  missing alias and broken installed readiness headers.
- [x] Register/distribute the focused regression; run bootstrap, configure,
  a real TLS-off library build, actual standalone install gates, Python syntax
  and diff checks. Record execution limits and failed setup evidence.
- [x] Caller: Groundwork validation and finalization into v3.
- [ ] CI / v3 PR: native Windows checkout/archive installed matrices.

The change stays in the private staging recipes in Makefile.am. One shared
directory function normalizes only a leading drive specification. Directory
overrides are passed only to the checks' recursive install commands; configured
prefix/exec_prefix and libhttpserver.pc are untouched. Existing normal installation
and src/Makefile.am hooks are unchanged. All three staging callers share the same
destination list, and both installed-header consumers use its include mapping.
No new production helper, runtime/public API change, dependency change, global
MSYS argument-conversion setting or assertion relaxation is introduced.

Local verification:

- Original RED: `test_dos_prefix_standalone_checks_use_real_installed_headers`
  failed on the actual `.hygiene-stageD:/ci/prefix/include/...` install destination.
  Shared check-local and custom `E:/custom/headers` controls failed on the same
  malformed concatenation; the Unix/custom-directory control passed.
- A bounded counterfactual disables only the new stage destination overrides
  and uses the real installer. The actual libtool archive installation then
  reaches the rejecting ranlib boundary with `.hygiene-stageD:/.../libstagefixture.a`,
  matching the retained Windows failure mechanism.
- Eight focused tests pass, including real libtool shared/static fixture installs,
  custom directories on different drives, default and Unix prefix cases, and all
  negative controls. The registered `make check-v3-stage-install` target also passes.
  These fixtures configure real Makefiles and compile a tiny library; they replace
  the unrelated protocol engine archive and emulate only the native tools' rejection
  of embedded drive specifications. They are not native Windows execution receipts.
- `./bootstrap`, `automake`, out-of-tree TLS-off configure and `make -j4 -C src`
  pass. Bootstrap retains the existing aminclude.am .PHONY / AC_OUTPUT warnings;
  configure reports missing optional doxygen/dot. The library build emits two
  signedness warnings in unchanged quic_connection_identity.cpp; no repair was made.
- Separate `make -j4 check-hygiene` and `make -j4 check-install-layout` invocations
  pass against the actual built TLS-off library and actual installed public headers.
- Python AST syntax and `git diff --check` pass. CLAUDE.md's C++ cpplint command
  does not apply to the changed Make/Python files; no Python linter is documented.

Retained setup failures and limits:

The first enhanced fixture used `INSTALL=sh wrapper`, which libtool treated as
an installer plus source operand. Making the wrapper executable corrected this
fixture setup; the actual libtool/ranlib tests then passed. This was not a product
repair or a relaxed assertion.

An extra concurrent invocation, `make -j4 check-hygiene check-install-layout`,
failed the hygiene install on `mv: cannot stat http/.deps/method.Tpo`; layout
passed. One bounded log/file read found overlapping install recipes and regenerated
src/Makefile dependency files. This is consistent with overlapping recursive
rebuilds, but exact causality and independence from this task are not established.
The separately executed required standalone gates then passed once each. The
concurrent invocation remains failed; its passage and general concurrent install
safety are not claimed. No subsystem repair, broad retry or further diagnosis ran.

Raw commands/results, original source bindings, all failed setup logs and local
evidence are retained under the primary checkout's
`.debug/ci-windows-staging-dv_00un1/implementation/`; its original journal remains
the task's diagnosis record. No full baseline/runtime suite, nonlocal platform
execution, old Darwin/Linux diagnostic, validation loop, merge or publication ran.
The native Windows gates remain incomplete and CI/v3-PR owned under AGENTS.

Groundwork validation passed at `fa1f76c7a106e5b8fde6749307ee8690fe4e09a0`
(1 iteration, 0 fixed, 0 unworked; three reviewers approved). Native Windows
receipts remain incomplete. The extra concurrent invocation remains failed and
causality uncertain; the separate passing gates do not establish independence.
