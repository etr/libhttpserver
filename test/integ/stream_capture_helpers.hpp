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

// TASK-074 review: shared stdout/stderr capture helpers for integ
// tests that must assert on what the process writes to those streams
// (debug_dump_request_body_set_test.cpp and
// debug_dump_request_body_unset_test.cpp previously carried verbatim
// copies of these).
//
// Redirects FILENO to a tmp file via dup2 so the test can read back
// what the process wrote to stdout/stderr in-process. Pair every
// begin_capture with an end_capture on the same fd; end_capture
// restores the original stream, returns the captured bytes, and
// unlinks the tmp file.

#ifndef TEST_INTEG_STREAM_CAPTURE_HELPERS_HPP_
#define TEST_INTEG_STREAM_CAPTURE_HELPERS_HPP_

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace httpserver_test {

struct stream_capture {
    int saved_fd = -1;
    int captured_fd = -1;
    std::string path;
};

// Redirect @p fileno_to_capture (STDOUT_FILENO / STDERR_FILENO) into a
// fresh mkstemp file whose name embeds @p tmp_label. The stdio buffer
// of the corresponding stream is flushed first so previously buffered
// bytes do not leak into the capture window.
inline stream_capture begin_capture(int fileno_to_capture,
                                    const char* tmp_label) {
    stream_capture c;
    char tpl[256];
    std::snprintf(tpl, sizeof(tpl),
                  "/tmp/libhttpserver_%s_XXXXXX", tmp_label);
    c.captured_fd = mkstemp(tpl);
    c.path = tpl;
    c.saved_fd = dup(fileno_to_capture);
    if (fileno_to_capture == STDOUT_FILENO) std::fflush(stdout);
    else if (fileno_to_capture == STDERR_FILENO) std::fflush(stderr);
    dup2(c.captured_fd, fileno_to_capture);
    return c;
}

// Flush, restore the original stream onto @p fileno_to_restore, and
// return everything written to the captured stream since the matching
// begin_capture. The tmp file is unlinked before returning.
inline std::string end_capture(stream_capture& c, int fileno_to_restore) {
    if (fileno_to_restore == STDOUT_FILENO) std::fflush(stdout);
    else if (fileno_to_restore == STDERR_FILENO) std::fflush(stderr);
    dup2(c.saved_fd, fileno_to_restore);
    ::close(c.saved_fd);
    ::close(c.captured_fd);
    FILE* f = std::fopen(c.path.c_str(), "rb");
    std::string out;
    if (f != nullptr) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
            out.append(buf, n);
        }
        std::fclose(f);
    }
    ::unlink(c.path.c_str());
    return out;
}

}  // namespace httpserver_test

#endif  // TEST_INTEG_STREAM_CAPTURE_HELPERS_HPP_
