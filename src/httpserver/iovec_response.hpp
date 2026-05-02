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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_IOVEC_RESPONSE_HPP_
#define SRC_HTTPSERVER_IOVEC_RESPONSE_HPP_

#include <string>
#include <utility>
#include <vector>
#include "httpserver/http_utils.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/iovec_entry.hpp"

struct MHD_Response;

namespace httpserver {

class iovec_response : public http_response {
 public:
     iovec_response() = default;

     // Owning constructor: the response takes ownership of the string buffers.
     // The iovec_entry array is built eagerly at construction so get_raw_response()
     // allocates nothing on the hot dispatch path.
     explicit iovec_response(
             std::vector<std::string> owned_buffers,
             int response_code = http::http_utils::http_ok,
             const std::string& content_type = http::http_utils::text_plain);

     /**
      * Non-owning constructor: the caller supplies pre-built iovec_entry pairs.
      * This is TASK-004's genuine zero-copy path: no heap allocation or data
      * copy is performed.
      *
      * @attention The caller is responsible for keeping the pointed-to buffers
      * alive at least until MHD_destroy_response() returns for the response
      * produced by get_raw_response(). libmicrohttpd holds a reference to the
      * buffer pointers until MHD_destroy_response() is called in the dispatch
      * path (webserver.cpp). Freeing any backing buffer before that point
      * causes a use-after-free inside libmicrohttpd (CWE-416). In practice
      * this means the buffers must outlive the iovec_response object AND the
      * MHD response lifecycle, which ends at MHD_destroy_response().
      *
      * @note This API surface is transitional (see PRD-RSP-REQ-006 /
      * TASK-010); it will be removed or replaced in a future v2.0 revision.
      */
     explicit iovec_response(
             std::vector<iovec_entry> caller_entries,
             int response_code = http::http_utils::http_ok,
             const std::string& content_type = http::http_utils::text_plain);

     // Copy construction and copy assignment are deleted: the owning constructor
     // stores void* pointers (entries_) into owned_buffers_ string storage.
     // A defaulted copy would shallow-copy entries_ while deep-copying
     // owned_buffers_ to new addresses, making entries_ dangle as soon as the
     // source is destroyed (CWE-416). Deletion forces callers onto move
     // semantics, which are safe because std::vector move transfers the heap
     // block and keeps string addresses stable.
     iovec_response(const iovec_response&) = delete;
     iovec_response& operator=(const iovec_response&) = delete;

     iovec_response(iovec_response&& other) noexcept = default;
     iovec_response& operator=(iovec_response&& b) noexcept = default;

     ~iovec_response() = default;

     // Returns a new MHD_Response* or nullptr on error (e.g. buffer count
     // exceeds MHD's unsigned-int limit). The caller does not own the returned
     // pointer; MHD manages its lifetime. May return nullptr; all callers on
     // the dispatch path must check before use.
     MHD_Response* get_raw_response();

 private:
     // Owned string buffers (populated by the owning constructor).
     std::vector<std::string> owned_buffers_;

     // Flattened iovec_entry array ready for the MHD cast. For the owning
     // constructor this is populated at construction time (zero allocation on
     // dispatch). For the non-owning constructor the caller-supplied entries
     // are stored directly.
     std::vector<iovec_entry> entries_;
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_IOVEC_RESPONSE_HPP_
