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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

// upload_pipeline -- behavior service (DR-014, §4.11) owning multipart /
// file-upload handling: the MHD post-iterator body (no-file form args and
// file chunks), the on-disk destination selection, and the per-(key,
// filename) output-stream lifecycle. Holds only const webserver_config&
// (file_upload_target / file_upload_dir / generate_random_filename_on_upload)
// and operates on mr->request / mr->upload_* fields.
//
// The webserver_impl::post_iterator static MHD trampoline forwards here:
// the no-file branch calls the static handle_post_form_arg (safe without an
// owning webserver -- see post_iterator_null_key_test), the file branch
// calls iterate_file on the owning webserver's upload_ instance.
//
// Internal header; only reachable when compiling libhttpserver.
#if !defined(HTTPSERVER_COMPILATION)
#error "upload_pipeline.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_UPLOAD_PIPELINE_HPP_
#define SRC_HTTPSERVER_DETAIL_UPLOAD_PIPELINE_HPP_

#include <microhttpd.h>

#include <cstddef>
#include <cstdint>

#if MHD_VERSION < 0x00097002
typedef int MHD_Result;
#endif

namespace httpserver {

struct webserver_config;
namespace http { class file_info; }

namespace detail {

struct modded_request;

class upload_pipeline {
 public:
    explicit upload_pipeline(const webserver_config& config) noexcept
        : config_(config) {}

    upload_pipeline(const upload_pipeline&) = delete;
    upload_pipeline& operator=(const upload_pipeline&) = delete;
    upload_pipeline(upload_pipeline&&) = delete;
    upload_pipeline& operator=(upload_pipeline&&) = delete;
    ~upload_pipeline() = default;

    // No-file form arg: set (or, on a continuation chunk with off>0, append
    // to) the request arg keyed by @p key. Static and config-free so the
    // post_iterator trampoline can reach it without an owning webserver
    // (MHD may hand a null key on a continuation chunk; guarded, issue #375).
    static MHD_Result handle_post_form_arg(modded_request* mr, const char* key,
                                           const char* data, size_t size,
                                           uint64_t off);

    // File-upload branch of the post iterator: mirror the value into the
    // request args (unless disk-only), then stream the chunk to disk (unless
    // memory-only), per config_.file_upload_target. Contains the
    // generateFilenameException guard.
    MHD_Result iterate_file(modded_request* mr, const char* key,
                            const char* filename, const char* content_type,
                            const char* transfer_encoding, const char* data,
                            size_t size);

 private:
    // First chunk for a (key, filename) pair: choose the on-disk destination
    // (random or sanitized client name) and prime the file_info. Returns
    // false if the sanitized name is empty.
    bool setup_new_upload_file_info(http::file_info& file, const char* filename,
                                    const char* content_type,
                                    const char* transfer_encoding) const;

    // Open/rotate the per-(filename,key) output stream on mr as MHD feeds
    // chunks.
    static void manage_upload_stream(modded_request* mr, const char* filename,
                                     const char* key, http::file_info& file);

    // Stream one upload chunk to disk, setting up the file_info + stream on
    // the first chunk.
    MHD_Result process_file_upload(modded_request* mr, const char* key,
                                   const char* filename, const char* content_type,
                                   const char* transfer_encoding,
                                   const char* data, size_t size) const;

    const webserver_config& config_;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_UPLOAD_PIPELINE_HPP_
