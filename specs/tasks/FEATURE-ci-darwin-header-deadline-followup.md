# FEATURE-ci-darwin-header-deadline-followup: Unresolved Darwin external deadline failure

Status: Pending — cause unknown; further diagnostic decision deferred until safe,
confirmed independent CI fixes are finished. No new investigation is authorized
by this record alone.

Predecessor: [bounded native diagnostic stage](FEATURE-ci-darwin-header-deadline.md).
Evidence source: `v3` c0 `c0cfdabc8040273d60c610269a93d1c687ed8517` and prior
f91 native archive failure. Choose the eventual implementation base when scheduled.

## Preserved defect and requirements

Installed shared TLS-off Darwin `external_http1_e2e`, case
`published_header_deadline_drives_empty_dispatch`, failed in f91 archive run
`39962644789` / job `120444991323` / artifact `12506406058`, and c0 archive run
`39969790346` / job `120469466623` / artifact `12509855171`. On c0 the case took
10,006.1 ms: original assertion 217 (`receive_close`) and 221 (`!failed`) failed;
219 (`empty_dispatches > 0`) passed. Other three cases and native HTTP1 passed.
Preserve the original 300 ms header deadline, 10 s host failure deadline and
assertions. Original failed gates remain open/incomplete; the defect is not fixed.

## Completed bounded checks and limits

The predecessor's approved controls are finished. Live and deferred trace overlays
on the exact selected archive passed the four external cases, first in runner
context, then with corrected EUID 0 and original signal defaults. Their healthy
~303 ms timer/continuation/enforcement/native-close sequences prove no cause;
instrumentation can change scheduling. No decisive forced-interleaving regression
or broken wake/lease-retirement invariant was established.

Final run `39990090304` / macOS job `120539322802` used unchanged selected c0
archive/core/test/helpers, original package_lane setup/prelude and first two
TLS-off shared suites, direct original Popen and actual EUID 0. Native HTTP1
60 tests / 708 checks and external four / 52 passed; header 303.348 ms.
Original receipt checks preceded the partial stop. This establishes intermittency,
not cause, fix, independence or whole-matrix passage. Sampling was not triggered;
actual native sampler execution remains untested. Local real-process controller
checks do not substitute for native failure evidence.

Same-c0 checkout package job `120468733495` also passed TLS-off before a separate
TLS-on CA failure. Archive versus checkout/setup was an observed contrast, not
an explanation. Root execution correction did not establish a deadline cause.
All temporary source/test/workflow diagnostics were removed in the predecessor;
its original source/test/workflow tree equals c0 exactly.

## Pending work when scheduled

- [ ] After independent confirmed Windows staging, TLS CA fixture and FreeBSD
  selected-make fixes are finished, make the next explicit diagnostic decision
  from retained evidence. No unchanged retry or additional probe is implied.
- [ ] If further investigation is authorized, define one bounded question and
  stopping condition; preserve source/runtime context and original failures.
- [ ] Establish a concrete cause and observe a meaningful failing actual-code
  regression before any narrow behavior repair; preserve budgets/assertions.
- [ ] Validate a proven repair with relevant local checks and assigned native CI;
  remove any newly authorized diagnostics before finalization.

The closed prior Linux parity/publication-wait uncertainty is not reopened.
No dependency/policy change, broad baseline, assertion relaxation, timeout
increase or unrelated repair is authorized here.

Primary evidence: `.debug/ci-darwin-native-69af4dxe/journal.md`, original c0/f91
logs/receipts, `original-prefix-summary.json`, `original-prefix-external.log`,
and retained original-prefix diagnostic/sampling/suites/commands receipts.
Final control source artifact `12508482722` (run `39969790346`, attempt 1), tar
SHA-256 `43124552f20bd00fceddc714856d605ca9caf017a849a991c34f16f437fcab83`.
The predecessor card records complete archive and evidence ZIP bindings.
