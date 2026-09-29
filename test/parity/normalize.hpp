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

// TASK-096: expectation-vs-observation comparison for the transcript
// runner.
//
// normalize() converts an observed_response into a normalized_exchange:
// declared-volatile headers (Date) are elided so expectations never pin
// wall-clock values. check_expectation() compares one parsed expectation
// against one normalized exchange and produces a human-readable diff for
// failure output. Header names compare case-insensitively; header values
// may carry <*> masks (matching any, possibly empty, run) so volatile
// material such as Digest nonces is normalized rather than pinned.
//
// Pure: no sockets, no libhttpserver.

#ifndef TEST_PARITY_NORMALIZE_HPP_
#define TEST_PARITY_NORMALIZE_HPP_

#include <string>
#include <vector>

#include "response_frame.hpp"
#include "transcript.hpp"

namespace parity {

struct normalized_exchange {
    int status = 0;
    std::string status_line;
    std::vector<observed_header> headers;
    std::string body;
    std::string framing;
};

// Elide volatile headers and case-fold nothing else; headers keep wire
// order. Returns the normalized view of one observed response.
normalized_exchange normalize(const observed_response& r);

// Case-insensitive header lookup on a normalized exchange (nullptr if
// absent). Names on both sides are compared case-insensitively.
const observed_header* find_header(const normalized_exchange& ex,
                                   const std::string& name);

// <*> mask match: each "<*>" in `pattern` matches any (possibly empty)
// run of characters; everything else must match literally.
bool mask_match(const std::string& pattern, const std::string& value);

struct match_result {
    bool ok = false;
    // Empty when ok; otherwise includes "expect-line=N: ..." plus a
    // short expected/actual description.
    std::string diff;
};

// Compare one expectation against one normalized exchange.
// `body_file_base` is the directory body_file names resolve against.
match_result check_expectation(const expectation& e,
                               const normalized_exchange& ex,
                               const std::string& body_file_base);

}  // namespace parity

#endif  // TEST_PARITY_NORMALIZE_HPP_
