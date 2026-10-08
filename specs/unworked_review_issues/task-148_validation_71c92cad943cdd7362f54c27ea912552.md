# Unworked Review Issues

**Run:** 2026-10-07 22:47:50
**Task:** TASK-148
**Total:** 2 (0 critical, 2 major, 0 minor)

## Major

1. [ ] **code-quality-reviewer** | `scripts/check-v3-installed-consumer.py:32` | error-handling
   Startup handling is not bounded after selector readiness. readline() can wait indefinitely for a newline, and the invalid-port branch calls stderr.read() before killing a still-running child. A fake consumer printing invalid-port and sleeping 17 seconds made traffic() report failure after 17.32 seconds despite its 15-second startup deadline; an indefinitely stalled child prevents the finally cleanup from being reached. This contradicts the runner's documented bounded smoke behavior and can hang validation on a consumer regression.
   *Recommendation:* Apply an absolute startup deadline to pipe reading, bound the startup line, and terminate/reap the child before collecting error output with a timeout. Add runner tests for partial startup lines and malformed startup lines from a child that remains alive.

2. [ ] **housekeeper** | `specs/tasks/_index.md:296` | task-not-marked-complete
   The TASK-148 task card records Status: In Progress and the implementation evidence says it remains In Progress pending caller-owned validation/finalization, but the task index still labels TASK-148 Not Started.
   *Recommendation:* Synchronize the TASK-148 row in specs/tasks/_index.md to In Progress; keep the task open pending caller-owned validation/finalization.
