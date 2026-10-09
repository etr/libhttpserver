# QPACK fuzz corpus

These authored binary `.seed` files exercise the existing private codecs and
connection-owned dynamic tables. The runner mutates writable copies only.

Inputs are capped at 512 bytes. Byte zero selects a limit profile modulo eight;
byte one gives the encoder-instruction prefix length, clamped to the available
suffix. That prefix is decoded in both instruction directions, with at most
32 instructions in each pass. The remaining suffix is a field section.
The complete suffix is also exercised as a prefixed integer, encoded string
and Huffman input. Short inputs and incomplete instructions remain valid fuzz
inputs.

Profiles 1, 2 and 3 restrict expanded fields to 41 bytes, field count to zero,
and compressed sections to two bytes. Profile 4 disables the decoder table;
profile 6 sets the exact 42-byte expanded static GET boundary. Other profiles
use compressed sections 128 bytes, expanded fields 256 bytes and eight fields.
Each dynamic table reserves at most 128 bytes under a 256-byte ancestor budget.
Instructions and decoded primitive strings are capped at 128 bytes; tables
hold at most four entries. Normalized traces are capped at 2048 entries.

Seeds include static/literal representations from the RFC 9204 fixture family,
dynamic insertion and duplication, decoder instructions, blocked Required
Insert Counts, post-base and invalid indexes, truncated/overflowing integers,
Huffman EOS/padding failures, repeated never-indexed fields, and compressed,
expanded and field-count limits.

Every input runs twice with fresh state and compares results. Successful
instructions and supported field sections round-trip with ordered field
values and never-indexed flags preserved. Typed codec states and consumed-byte
contracts are checked for failures, including `incomplete` and
`inserts_unavailable`. Table accounting is checked after instructions and must
reach zero after both tables are destroyed. The replay suite runs sorted seeds,
32 fixed-seed mutations and 32 prefixes per seed and prints attempted/accepted
operation counts.
