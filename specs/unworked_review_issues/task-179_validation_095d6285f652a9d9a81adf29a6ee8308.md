# Unworked Review Issues

**Run:** 2026-10-09 15:29:29
**Task:** TASK-179
**Total:** 2 (0 critical, 2 major, 0 minor)

## Major

1. [ ] **code-quality-reviewer** | `scripts/run-v3-behavior-parity.py:303` | error-handling
   check_receipt accepts an inventory-declared migration as preserved when its changes list is empty. Read-only mutation of fresh build/task179-validation/receipt.json changed capability-008/h1.1-tcp from migration to preserved and removed reference/rationale; check_receipt still returned []. The capability declares the registration-order behavior migration, so the validator can certify a known v2 deviation as preserved without its migration evidence, contrary to TASK-179's classification action item.
   *Recommendation:* Resolve the declared migration as cell.get('migration', capability.get('migration')) and require migration classification plus the declared valid migration reference/rationale for those cells. Add negative receipt mutations for both capability-level migrations and cell-level overrides.

2. [ ] **test-quality-reviewer** | `test/integ/behavior_parity_runner_test.py:137` | test-coverage
   The migration-reference mutation test only mutates an outcome already classified as migration; no test declares capability/cell migration metadata and then changes its outcome to preserved. The fresh receipt mutation for capability-008/h1.1-tcp passes after migration/reference/rationale are removed, showing that this missing negative contract leaves TASK-179's deviation-classification guarantee untested.
   *Recommendation:* Add a synthetic capability-level migration with empty changes, verify its valid receipt passes, then assert preserved classification fails even with valid supporting observations. Repeat for a cell-level migration override. Cover missing and mismatched declared migration evidence while retaining passing preserved cells.
