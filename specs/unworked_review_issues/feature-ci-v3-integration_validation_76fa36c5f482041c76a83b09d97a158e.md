# Unworked Review Issues

**Run:** 2026-10-23 03:13:10
**Task:** FEATURE-ci-v3-integration
**Total:** 2 (0 critical, 1 major, 1 minor)

## Major

1. [ ] **cloud-infrastructure-reviewer** | `.github/workflows/v3-native.yml:77` | iac-anti-pattern
   [Reliability — Failure propagation] New tee pipelines at lines 77 and 86 mask bootstrap/Configure failures because the step has neither shell: bash nor set -o pipefail. GitHub implicit Linux shell is bash -e. A bounded execution of this exact script with bootstrap exit17 and Configure exit23 returned 0 and continued after bootstrap failed. New tests at scripts/test-v3-ci-workflows.py:92 and genuine evidence manually supply pipefail, masking the mismatch.
   *Recommendation:* Declare shell: bash on the step or enable pipefail in its script. Derive behavioral test shell flags from the workflow; verify bootstrap failure stops before Configure and Configure retains its nonzero status with logs uploaded. See https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#jobsjob_idstepsshell

## Minor

2. [ ] **code-simplifier** | `.github/workflows/v3-release-acceptance.yml:486` | code-structure
   The publisher constructs its complete expected authorization binding in four densely packed lines, including an embedded producer-artifact comprehension. This makes individual environment-to-binding assignments harder to audit alongside the subsequent identity checks.
   *Recommendation:* Optionally format this dictionary with one binding field per line and construct the producer_artifacts mapping in a clearly named local variable before assembling expected. Preserve every field, comparison and environment lookup, and keep the publisher code inline so its no-downloaded-code boundary remains unchanged.
