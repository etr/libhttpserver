/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-096: transcript data model + line parser for the segmented-input
// parity harness.
//
// A transcript (*.tseq) is an engine-agnostic text file describing
//   * a named server profile (resolved by the runner's fixture registry),
//   * per-case raw request byte segments written to a real TCP socket,
//     and
//   * expected observable outputs (status line, headers, body, framing,
//     connection end) as normalized field-level assertions.
//
// Transcripts never reference the executing engine (v2/MHD or the v3
// native HTTP/1 stack): the same corpus drives both. Anything that
// cannot be pinned cross-engine is deliberately left out of transcripts
// and recorded in specs/architecture/v3/v2-parity-inventory.md instead.
//
// Directive grammar (normative):
//   profile <name>            exactly once per file, before any case
//   case <name> ... [end]     cases are terminated by `end`, the next
//                             `case`, or EOF; names unique per file
//   send <escaped-text>       one segment = one write(); rest of line
//                             after the single separating space, with
//                             escapes \r \n \t \\ \" \xHH
//   send_hex <hex>            one segment from hex bytes
//   expect <kind> <args>      kinds: status, status_line, header,
//                             header_order, body, body_hex, body_len,
//                             body_file, framing, connection, closer
//   option read_timeout_ms N  per-case failure bound, never a pass
//                             condition
//
// Trailing whitespace is stripped from every physical line; leading
// whitespace before a directive is not allowed. `#` comments and blank
// lines are ignored. Intentional trailing spaces in a segment must be
// written as \x20 (they are stripped before unescaping).
//
// The `<*>` mask (matches any content, including nothing) is allowed
// ONLY in expect header values, where volatile material (Digest nonces,
// opaque tokens) is normalized instead of pinned.
//
// POSIX-only by design for now; Windows support is a later milestone.

#ifndef TEST_PARITY_TRANSCRIPT_HPP_
#define TEST_PARITY_TRANSCRIPT_HPP_

#include <stdexcept>
#include <string>
#include <vector>

namespace parity {

struct send_segment {
    std::string bytes;
    int line = 0;
};

enum class expect_kind {
    status,
    status_line,
    header,
    header_absent,
    header_order,
    body,
    body_hex,
    body_len,
    body_file,
    framing,
    connection,
    closer,
};

struct expectation {
    expect_kind kind{};
    int line = 0;
    // Header name, or file name for body_file.
    std::string name;
    // status_line text, header value (may contain <*> masks), body text,
    // or the framing/connection/closer token.
    std::string value;
    // status code or body_len.
    int number = 0;
    // header_order names.
    std::vector<std::string> order;
};

struct tcase {
    std::string name;
    int line = 0;
    std::vector<send_segment> sends;
    std::vector<expectation> expects;
    // Failure-detection bound only: never a pass condition.
    int read_timeout_ms = 5000;
};

struct transcript {
    std::string profile;
    int profile_line = 0;
    std::string source_path;
    std::vector<tcase> cases;
};

// Parse error carrying file:line diagnostics.
class transcript_error : public std::runtime_error {
 public:
    transcript_error(const std::string& path, int line, const std::string& message);
    int line() const noexcept { return line_; }

 private:
    int line_;
};

// Parse transcript text. `source_path` is used in error messages only.
transcript parse_transcript(const std::string& text, const std::string& source_path);

// Read and parse a transcript file. Throws transcript_error (line 0) if
// the file cannot be opened or read.
transcript parse_transcript_file(const std::string& path);

// Decode \r \n \t \\ \" \xHH escapes. Throws transcript_error on an
// unknown or truncated escape.
std::string unescape(const std::string& escaped, const std::string& path, int line);

// Encode bytes using the same escape set (used by --record mode).
std::string escape(const std::string& raw);

}  // namespace parity

#endif  // TEST_PARITY_TRANSCRIPT_HPP_
