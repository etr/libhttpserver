# Unworked Review Issues

**Run:** 2026-10-08 14:58:45
**Task:** TASK-163
**Total:** 1 (0 critical, 1 major, 0 minor)

## Major

1. [ ] **security-reviewer** | `test/integ/quic_interop_endpoint.cpp:63` | resource-exhaustion
   The declared 64 MiB capture limit counts only UDP payload bytes, while each packet writes another 28-byte IPv4/UDP envelope and 16-byte pcap record header. Receive captures occur before admission at line 218, so unauthenticated empty UDP datagrams write 44 bytes each without consuming any budget (CWE-400). A task-local probe using the unchanged capture class wrote 70,400,024 bytes for 1,600,000 empty packets without a capture-bound exception, exceeding 67,108,864 bytes. This permits network input to grow the mounted capture beyond its advertised resource limit during the 120-second test lifetime.
   *Recommendation:* Initialize the capture budget with the 24-byte global header and charge the complete encoded packet plus its 16-byte record header before each write, using an overflow-safe remaining-capacity comparison. Add coverage for empty and small datagrams that verifies actual file size never exceeds 64 MiB.
