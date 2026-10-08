# Unworked Review Issues

**Run:** 2026-10-07 21:17:20
**Task:** TASK-146
**Total:** 1 (0 critical, 1 major, 0 minor)

## Major

1. [ ] **performance-reviewer** | `src/detail/io_iocp_backend.cpp:397` | algorithmic-complexity
   Every single native completion triggers prepare_locked(), which walks every pending operation, including already-posted reads, followed by another complete registry walk in sweep_due_timers() at line 446. With N pending operations and N queued completions, draining a burst performs O(N^2) registry visits; one busy connection also pays O(N) for every transfer while unrelated connections remain idle. Both scans take the same mutex used by submit/cancel, and the IOCP has one driver. This is material at the default 1024-connection capacity and higher configured capacities, even though idle-loop iteration counts remain bounded.
   *Recommendation:* Track unposted submissions separately in submission order and maintain timer-only deadline state, so handling a native packet does not revisit every posted operation. Alternatively measure native Windows throughput and submit/cancel latency at 1, 1024, and larger supported pending counts before retaining the scans. A completion-burst or busy-plus-idle scaling probe should verify the registry work as well as idle iteration counts.
