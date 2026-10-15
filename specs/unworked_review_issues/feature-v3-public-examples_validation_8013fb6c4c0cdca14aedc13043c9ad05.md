# Unworked Review Issues

**Run:** 2026-10-15 02:02:16
**Task:** FEATURE-v3-public-examples
**Total:** 4 (0 critical, 3 major, 1 minor)

## Major

1. [ ] **architecture-alignment-checker** | `test/examples/v3_tls_rotation.cpp:130` | pattern-violation
   The newly promoted rotation example can hang forever during external-loop error cleanup. An unknown stdin command throws at require(line == "replace") before stopping is set. Stack unwinding destroys poller first, whose jthread destructor requests its stop token and joins, but the polling lambda only tests stopping and ignores that token. The same path occurs when reading replacement PEMs throws after startup. A bounded probe of the TLS-on executable with stdin 'unknown\n' exited 1 in managed mode but external mode exceeded 3 seconds and required kill. This violates the plan's deterministic-stop/error-output requirement and DR-V3-004/008's explicit external-loop ownership and shutdown split.
   *Recommendation:* Make the polling host exception-safe: have its jthread observe its stop token or add an owned cleanup guard that requests server stop and terminates/joins the poller on every exit before server destruction. Add a bounded external-mode invalid-command regression case; retain the existing numeric startup and replace/invalid/stop protocol.

2. [ ] **code-quality-reviewer** | `test/examples/v3_tls_rotation.cpp:130` | error-handling
   The promoted external-loop rotation example deadlocks on invalid stdin or another exception after poller creation. require(line == "replace") throws before stopping=true/request_stop; unwinding destroys the jthread first, but its loop only checks stopping and ignores jthread stop requests. A real TLS-on probe sending unknown exited 1 in managed mode and remained running past two seconds in external mode. This directly affects the plan's rotation input validation and deterministic error cleanup requirement, although the loop originally lived in the retained test example.
   *Recommendation:* Make external poller cleanup exception-safe, for example by honoring its stop_token and ensuring server stop/join occurs during unwinding, or by returning through a single shutdown path. Add a bounded TLS-on external-mode invalid-command test requiring a diagnostic and process termination.

3. [ ] **performance-reviewer** | `test/examples/v3_tls_rotation.cpp:117` | resource-leak
   An exception after the external poller starts cannot retire its thread. The loop tests only stopping, while stopping is set only on the normal exit path. An unknown stdin command throws at line 130; unwinding destroys std::jthread, whose implicit stop request is ignored, so it joins forever before reaching the catch block. The same issue occurs if replacement PEM input becomes unavailable. This pre-existing canonical-source behavior is directly affected by promotion into the normal runnable inventory and contradicts the plan's deterministic stop/input-error behavior. A bounded probe against the frozen TLS-on binary and supplied fixtures confirmed external 'stop' exits 0, while 'unknown-command' remains running after two seconds and requires termination.
   *Recommendation:* Make the poller observe its std::stop_token (or use an exception-safe scope guard that sets stopping before joining), while preserving request_stop/join/stop ordering. Add a bounded external-mode invalid-command or replacement-input-failure smoke proving diagnostic exit and thread retirement.

## Minor

4. [ ] **code-simplifier** | `scripts/lib/v3_example_consumers.py:16` | code-structure
   The new command wrapper accepts **kwargs but never reads or forwards them, leaving an unused part of its interface.
   *Recommendation:* Remove **kwargs from the signature; compile_probe calls this wrapper with only argv, cwd, and environment.
