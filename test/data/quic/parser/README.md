# QUIC invariant-header seeds

Each `.seed` is one raw datagram, including an empty datagram. The target
tries absent, zero, two, twenty and invalid twenty-one byte short CID lengths.
Seeds include long/short headers, version zero, both maximum CIDs, missing
fixed bit, truncation and oversized CID lengths. Inputs are capped at 4 KiB.
Coverage is routing metadata only: no payload, frames or cryptography.
