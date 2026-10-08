### TASK-159: Implement bounded static-only QPACK codec

**Milestone:** M11 - First HTTP/3 slice
**Component:** QPACK
**Estimate:** S
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide bounded static-only QPACK codec for libhttpserver v3.0.

**Action Items:**
- [x] Implement QPACK static table, literals and bounded prefix integers.
- [x] Decode/encode without dynamic references.
- [x] Replay RFC 9204 vectors and expanded-field-limit failures.

**Dependencies:**
- Blocked by: TASK-097
- Blocks: TASK-160, TASK-167

**Acceptance Criteria:**
- RFC 9204 static/literal vectors pass with decoded-field limits and zero dynamic capacity.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-017
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Complete

**Implementation evidence (2026-10-08):**
- Added private stateless `qpack_encoder` / `qpack_decoder`, owned ordered occurrences,
  explicit never-indexed policy, and adapters for semantic `http::fields`.
- Implemented the 99 zero-based [RFC 9204 Appendix A](https://www.rfc-editor.org/rfc/rfc9204.html#appendix-A)
  entries, static indexed fields, static-name literals, literal names, and shared
  HPACK Huffman operations. Prefix integers support values through `2^62 - 1`
  with at most ten total octets. Dynamic capacity remains fixed at zero.
- Decoder admits the complete section without allocation before owning fields;
  encoder preflights count and expanded bytes before writing. Both enforce
  compressed, expanded (`name + value + 32` per occurrence), and count ceilings,
  returning empty output on failure. Complete-section truncation is malformed;
  primitive truncation remains incomplete. Clear-sign nonzero Base is accepted;
  negative Base, nonzero Required Insert Count, and all dynamic reference families
  are rejected. Allocation failures become bounded limit errors.
- TDD: the static-table, primitive, and section tests first failed compilation
  because their respective QPACK implementation headers did not exist. Receipts:
  `/private/tmp/task159-static-red.log`, `task159-primitives-red.log`, and
  `task159-section-red.log`. The existing HPACK table baseline passed (321 checks).
- Local C++20 verification passed: `./bootstrap`, configuration with
  `--disable-examples --disable-v3-tls CPPFLAGS=-I/opt/homebrew/include
  LDFLAGS=-L/opt/homebrew/lib`, and `make -j2` in `.build-task159`.
  Homebrew include/library paths resolve the installed build dependencies.
- Focused Makefile targets and all seven executables passed: `qpack_static_table`,
  `qpack_primitives`, `qpack_field_section`, `hpack_primitives`,
  `hpack_static_table`, `hpack_corpus`, and `hpack_field_section`
  (31 tests, 5,268 checks, zero failures/skips). Coverage includes independent
  Appendix B.1 bytes, all static indexes, Huffman names/values, sensitivity,
  duplicate occurrence order, atomic failures, allocation-free rejection,
  injected allocation failure, and exact size/count ceilings.
- Strict direct focused compilation passed with C++20,
  `-Wall -Wextra -Werror -pedantic`. Changed C++ files passed
  `python3 -m cpplint --extensions=cpp,hpp --headers=hpp`; `git diff --check`
  passed. Build/test/lint receipts are under `/private/tmp/task159-*`.
- Validation repair: semantic encoding now traverses owned occurrences directly
  through private `http::fields` friendship, sharing the indexed encoder with
  span/owned inputs. Public semantic behavior is unchanged. Uncached semantic
  count/expanded/prefix rejection allocates nothing; admitted allocation and
  length failures return empty `limit_exceeded` results. New regressions first
  produced three allocation-check failures and terminated at the allocation
  boundary with sentinel exit 86 (`/private/tmp/task159-fix-section-red.log`),
  then passed seven field-section tests / 354 checks. Rebuilt and reran the seven
  codec executables above plus `http_semantic_fields` (eight tests / 49 checks),
  for 39 tests / 5,317 checks overall with zero failures/skips. Full local
  `make -j2`, strict direct C++20 QPACK compilation, changed-file cpplint and
  diff check passed; repair receipts are `/private/tmp/task159-fix-*`.
- BSD, Windows, and other nonlocal platform checks are unexecuted here and belong
  to CI and the v3 PR per `AGENTS.md`. HTTP/3 framing/control streams and dynamic
  QPACK remain the responsibility of dependent tasks.
