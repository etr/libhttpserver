## 13) Documentation deliverables (out of architecture scope, listed for traceability)

- Rewritten `README.md` (PRD §2 documentation NFR).
- Updated `examples/`: lambda-first hello world, class-based shared-state example (PRD §3.4).
- `RELEASE_NOTES.md` (informational; not a compatibility commitment).
- Doxygen / inline doc updates for every renamed and reshaped public method. A `check-doxygen.sh` CI gate (`check-local` target in `Makefile.am`) enforces zero substantive Doxygen warnings permanently.

---
