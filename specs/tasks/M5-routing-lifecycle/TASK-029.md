### TASK-029: Naming consistency — `stop_and_wait`, `block_ip`/`unblock_ip`

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** `webserver` public API
**Estimate:** M

**Goal:**
Collapse synonyms to a single canonical verb per concept, per PRD §3.7.

**Action Items:**
- [ ] Rename `webserver::sweet_kill` → `webserver::stop_and_wait`. Remove the old name.
- [ ] Add `webserver::block_ip(std::string_view ip)` and `webserver::unblock_ip(std::string_view ip)`.
- [ ] Remove `ban_ip`, `unban_ip`, `allow_ip`, `disallow_ip` from the public API. The internal ban list remains; it's just exposed under one name pair.
- [ ] Verify no `// NOLINT(runtime/explicit)` survives on related constructors (covered in TASK-030).
- [ ] Verify `shoutCAST` is preserved as-is (only camelCase exception, per PRD §3.7).

**Dependencies:**
- Blocked by: TASK-014
- Blocks: None

**Acceptance Criteria:**
- `grep -E '\bsweet_kill\b' src/httpserver/*.hpp src/*.cpp` returns no results.
- `grep -E '\b(ban_ip|unban_ip|allow_ip|disallow_ip)\b' src/httpserver/*.hpp` returns no results.
- `grep -E '[a-z][A-Z]' src/httpserver/*.hpp` returns only `shoutCAST` matches.
- Existing `webserver::stop()` is unchanged (a separate verb meaning "stop without waiting"); only `sweet_kill` is renamed.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-NAM-REQ-001, PRD-NAM-REQ-002, PRD-NAM-REQ-005
**Related Decisions:** §3.7, OQ-004, OQ-005

**Status:** Not Started
