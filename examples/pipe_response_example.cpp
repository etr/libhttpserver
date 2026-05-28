/*
     This file is part of libhttpserver
     Copyright (C) 2011-2025 Sebastiano Merlino

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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

// pipe_response_example.cpp - stream a response from the read end of a
// pipe that a background thread writes into. The lambda owns the
// short-lived pipe + writer; ownership of the read-end fd transfers
// into the response.

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

#include <cstring>
#include <thread>

#include <httpserver.hpp>

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)};

    ws.on_get("/stream", [](const httpserver::http_request&) {
        int pipefd[2];
#if defined(_WIN32) && !defined(__CYGWIN__)
        if (_pipe(pipefd, 4096, _O_BINARY) == -1) {
#else
        if (pipe(pipefd) == -1) {
#endif
            return httpserver::http_response::string("pipe failed").with_status(500);
        }

        std::thread writer([fd = pipefd[1]]() {
            const char* messages[] = {"Hello ", "from ", "a pipe!\n"};
            for (const char* msg : messages) {
                // return value ignored in this demo; production code must
                // check it in a loop to handle partial writes and EINTR.
                (void)write(fd, msg, strlen(msg));
            }
            close(fd);
        });
        writer.detach();

        return httpserver::http_response::pipe(pipefd[0])
                   .with_header("Content-Type", "text/plain");
    });

    ws.start(true);
    return 0;
}
