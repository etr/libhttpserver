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

#include "httpserver/file_response.hpp"
#include <fcntl.h>
#include <microhttpd.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iosfwd>

struct MHD_Response;

namespace httpserver {

MHD_Response* file_response::get_raw_response() {
#ifndef _WIN32
    int fd = open(filename.c_str(), O_RDONLY | O_NOFOLLOW);
#else
    int fd = open(filename.c_str(), O_RDONLY);
#endif
    if (fd == -1) return nullptr;

    struct stat sb;
    if (fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode)) {
        close(fd);
        return nullptr;
    }

    off_t size = lseek(fd, 0, SEEK_END);
    if (size == (off_t) -1) {
        close(fd);
        return nullptr;
    }

    if (size) {
        return MHD_create_response_from_fd(size, fd);
    } else {
        close(fd);
        return MHD_create_response_from_buffer(0, nullptr, MHD_RESPMEM_PERSISTENT);
    }
}

}  // namespace httpserver
