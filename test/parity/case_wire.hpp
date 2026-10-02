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
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

#ifndef TEST_PARITY_CASE_WIRE_HPP_
#define TEST_PARITY_CASE_WIRE_HPP_

// TASK-117 step 5: the request-side wire rebuild shared by the form
// corpus replay suites (promoted from
// forms_urlencoded_corpus_test.cpp so the multipart replay is not a
// copy -- validation issue #7). One corpus case's send segments are
// reassembled into a parsed request head plus the raw body bytes, the
// same shape the v2 fixture served.

#include <string>
#include <vector>

#include <httpserver/http/request_head.hpp>
#include <parity/transcript.hpp>

namespace parity {

// The request side of one corpus case.
struct corpus_request {
    httpserver::http::request_head head;
    std::string body;
};

// Splits CRLF-terminated text into lines (no terminators kept).
std::vector<std::string> split_lines(const std::string& text);

// Rebuilds one case's request head and raw body from its send
// segments. Tolerant colon handling: a header value may or may not
// carry the conventional single space after the colon.
corpus_request parse_case(const tcase& c);

}  // namespace parity

#endif  // TEST_PARITY_CASE_WIRE_HPP_
