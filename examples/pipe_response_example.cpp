/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

#include <cstring>
#include <memory>
#include <thread>

#include <httpserver.hpp>

class pipe_resource : public httpserver::http_resource {
 public:
     std::shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request&) {
         int pipefd[2];
#if defined(_WIN32) && !defined(__CYGWIN__)
         if (_pipe(pipefd, 4096, _O_BINARY) == -1) {
#else
         if (pipe(pipefd) == -1) {
#endif
             return std::make_shared<httpserver::string_response>("pipe failed", 500);
         }

         // Spawn a thread to write data into the pipe
         std::thread writer([fd = pipefd[1]]() {
             const char* messages[] = {"Hello ", "from ", "a pipe!\n"};
             for (const char* msg : messages) {
                 auto ret = write(fd, msg, strlen(msg));
                 (void)ret;
             }
             close(fd);
         });
         writer.detach();

         // Return the read end of the pipe as the response
         return std::make_shared<httpserver::pipe_response>(pipefd[0], 200, "text/plain");
     }
};  // NOLINT(readability/braces)

int main() {
    httpserver::webserver ws = httpserver::create_webserver(8080);

    pipe_resource pr;
    ws.register_resource("/stream", &pr);
    ws.start(true);

    return 0;
}
