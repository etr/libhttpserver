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

// upload_pipeline behavior service (DR-014 §4.11). Logic moved verbatim out
// of detail/webserver_callbacks.cpp (handle_post_form_arg /
// setup_new_upload_file_info / manage_upload_stream / process_file_upload and
// the file branch of post_iterator, here iterate_file). The post_iterator
// static MHD trampoline stays on webserver_impl and forwards here. Rewiring:
// parent->config.* becomes config_.*.

#include "httpserver/detail/upload_pipeline.hpp"

#include <microhttpd.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <memory>
#include <string>

#include "httpserver/create_webserver.hpp"
#include "httpserver/file_info.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/detail/connection_context.hpp"

namespace httpserver {

using httpserver::http::http_utils;

namespace detail {

MHD_Result upload_pipeline::handle_post_form_arg(detail::connection_context* conn,
        const char* key, const char* data, size_t size, uint64_t off) {
    // MHD may invoke the post iterator with a null key on a continuation
    // chunk (off > 0): the field name was supplied on the first call and is
    // not repeated. With no field name there is nothing to store the value
    // under, so silently accept the chunk (MHD_YES; MHD_NO would abort the
    // request). Guarding here also stops the raw pointer from reaching
    // std::string, which throws std::logic_error on null and aborts via
    // std::terminate (the throw escapes a C callback). See issue #375.
    if (key == nullptr) {
        return MHD_YES;
    }
    // A non-zero @p off means MHD is feeding a continuation chunk of a
    // previously-started value, so append rather than replace.
    if (off > 0) {
        conn->request->grow_last_arg(key, std::string(data, size));
    } else {
        conn->request->set_arg(key, std::string(data, size));
    }
    return MHD_YES;
}

bool upload_pipeline::setup_new_upload_file_info(http::file_info& file,
        const char* filename, const char* content_type,
        const char* transfer_encoding) const {
    // First chunk for this (key, filename) pair: choose the on-disk
    // destination path (random if generate_random_filename_on_upload,
    // otherwise sanitize the client-supplied filename) and prime the
    // file_info with content_type / transfer_encoding when MHD gave them.
    if (config_.generate_random_filename_on_upload) {
        file.set_file_system_file_name(
            http_utils::generate_random_upload_filename(config_.file_upload_dir));
    } else {
        std::string safe_name = http_utils::sanitize_upload_filename(filename);
        if (safe_name.empty()) return false;
        file.set_file_system_file_name(config_.file_upload_dir + "/" + safe_name);
    }
    // Avoid appending to a leftover file from a previous request.
    unlink(file.get_file_system_file_name().c_str());
    if (content_type != nullptr) file.set_content_type(content_type);
    if (transfer_encoding != nullptr) file.set_transfer_encoding(transfer_encoding);
    return true;
}

void upload_pipeline::manage_upload_stream(detail::connection_context* conn,
        const char* filename, const char* key, http::file_info& file) {
    // If MHD switches us to a different (filename, key) pair, close the
    // previous output stream. The four-way OR covers fresh state (both
    // tracking strings empty) and either coordinate changing.
    if (conn->upload_filename.empty()
            || conn->upload_key.empty()
            || strcmp(filename, conn->upload_filename.c_str()) != 0
            || strcmp(key, conn->upload_key.c_str()) != 0) {
        if (conn->upload_ostrm != nullptr) conn->upload_ostrm->close();
    }
    // Open a stream when we don't already have one (first chunk, or
    // just-closed above).
    if (conn->upload_ostrm == nullptr || !conn->upload_ostrm->is_open()) {
        conn->upload_key = key;
        conn->upload_filename = filename;
        conn->upload_ostrm = std::make_unique<std::ofstream>();
        conn->upload_ostrm->open(file.get_file_system_file_name(),
                               std::ios::binary | std::ios::app);
    }
}

MHD_Result upload_pipeline::process_file_upload(detail::connection_context* conn,
        const char* key, const char* filename, const char* content_type,
        const char* transfer_encoding, const char* data, size_t size) const {
    http::file_info& file = conn->request->get_or_create_file_info(key, filename);
    if (file.get_file_system_file_name().empty()) {
        if (!setup_new_upload_file_info(file, filename, content_type,
                                        transfer_encoding)) {
            return MHD_NO;
        }
    }
    manage_upload_stream(conn, filename, key, file);
    if (size > 0) {
        conn->upload_ostrm->write(data, size);
        if (!conn->upload_ostrm->good()) return MHD_NO;
    }
    file.grow_file_size(size);
    return MHD_YES;
}

MHD_Result upload_pipeline::iterate_file(detail::connection_context* conn,
        const char* key, const char* filename, const char* content_type,
        const char* transfer_encoding, const char* data, size_t size) {
    try {
        if (config_.file_upload_target != FILE_UPLOAD_DISK_ONLY) {
            conn->request->set_arg_flat(key,
                std::string(conn->request->get_arg(key)) + std::string(data, size));
        }
        if (*filename != '\0'
                && config_.file_upload_target != FILE_UPLOAD_MEMORY_ONLY) {
            MHD_Result r = process_file_upload(
                conn, key, filename, content_type, transfer_encoding, data, size);
            if (r != MHD_YES) return r;
        }
        return MHD_YES;
    } catch (const http::generateFilenameException&) {
        return MHD_NO;
    }
}

}  // namespace detail
}  // namespace httpserver
