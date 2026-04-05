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

#include "httpserver/iovec_response.hpp"
#include <microhttpd.h>
#include <vector>

struct MHD_Response;

namespace httpserver {

MHD_Response* iovec_response::get_raw_response() {
    // MHD_create_response_from_iovec makes an internal copy of the iov array,
    // so the local vector is safe. The buffer data pointed to by iov_base must
    // remain valid until the response is destroyed — this is guaranteed because
    // the buffers are owned by this iovec_response object.
    std::vector<MHD_IoVec> iov(buffers.size());
    for (size_t i = 0; i < buffers.size(); ++i) {
        iov[i].iov_base = buffers[i].data();
        iov[i].iov_len = buffers[i].size();
    }
    return MHD_create_response_from_iovec(
        iov.data(),
        static_cast<unsigned int>(iov.size()),
        nullptr,
        nullptr);
}

}  // namespace httpserver
