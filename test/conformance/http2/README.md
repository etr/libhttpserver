# HTTP/2 private connection transcripts

RFC 9113 is the oracle: https://www.rfc-editor.org/rfc/rfc9113.html.
Each binary `.wire` starts at the client magic. The shared corpus loader reads
five TSV columns: name, RFC sections, feed/EOF stage, verdict, expected output
hexadecimal octets. Verdict is `scope wire-code consumed-offset output-mode`;
scope is `ok`, `stream`, or `connection`. `drain` consumes output after each
completed input event; `hold` preserves output to exercise the two-slot queue.
The initial server SETTINGS is consumed before client input. EOF is applied
when all bytes have been consumed. Every fixture runs coalesced, bytewise and
at every split. Stream errors consume the entire offending frame and allow
later framing; connection errors consume no subsequent suffix. Expected output
includes the initial SETTINGS, ACKs and at most one terminal GOAWAY. A stream
owner would use the typed stream error to send RST_STREAM.

These prove private framing and control behavior. Request dispatch, stream
state, HPACK block collection, multiplexing and flow control are later tasks.
