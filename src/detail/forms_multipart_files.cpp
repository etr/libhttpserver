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

// TASK-117: temp_file_part_sink, the v2 disk-upload behavior as one
// part_sink (PRD-V3N-REQ-021/025, DR-V3-001; the v2 reference is
// upload_pipeline.cpp's setup_new_upload_file_info /
// manage_upload_stream and http_request's destructor cleanup). The
// first file-I/O TU of the v3 core: std::fstream plus
// <cstdio>::remove -- portable, no third-party tokens -- and the
// randomness comes from the v3 entropy seam (detail/entropy_sys.hpp),
// so the native linkage audit stays clean.
//
// Cancellation contract: on_part_abort removes the partial file, and
// the destructor removes it again idempotently (the RAII backstop v2
// had through http_request::~http_request). Completed files are
// consulted against should_keep exactly once each at destruction;
// false, a null callback, and a throwing callback all remove the file
// (v2's file_cleanup_callback rules).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/detail/entropy_sys.hpp>
#include <httpserver/forms/multipart.hpp>
#include <httpserver/forms/urlencoded.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver {

// v2's sanitize_upload_filename, verbatim in shape: reject embedded
// NULs (the concatenated path would silently truncate at the OS
// open() -- CWE-626), take the basename (any path prefix stripped, so
// traversal is neutralized), and reject an empty, ".", or ".."
// basename. Returns "" for a rejected name.
std::string sanitize_upload_filename(std::string_view filename) {
    if (filename.empty()) return "";
    if (filename.find('\0') != std::string_view::npos) return "";
    const std::size_t slash = filename.find_last_of("/\\");
    const std::string basename = slash == std::string_view::npos
        ? std::string(filename)
        : std::string(filename.substr(slash + 1));
    if (basename.empty() || basename == "." || basename == "..") {
        return "";
    }
    return basename;
}

namespace {

std::string hex_byte(unsigned char c) {
    const char digits[] = "0123456789abcdef";
    std::string out;
    out.push_back(digits[c >> 4]);
    out.push_back(digits[c & 0x0f]);
    return out;
}

}  // namespace

namespace detail {

// The sink's library-side members (the public header carries only the
// pimpl pointer, so <fstream> stays out of the installed surface).
class multipart_temp_state {
 public:
    explicit multipart_temp_state(forms::temp_file_options options)
        : directory_(std::move(options.directory)),
          random_names_(options.random_names),
          keep_(std::move(options.should_keep)) { }

    // A random destination under the directory (128 bits from the OS
    // entropy source; the collision domain makes names unique across
    // parts and requests).
    http::outcome random_path(std::string& out) const {
        std::byte raw[16];
        const http::outcome filled =
            detail::entropy::fill(std::span<std::byte>(raw, sizeof(raw)));
        if (!filled.ok()) return filled;
        std::string name = "lht-upload-";
        for (const std::byte b : raw) {
            name += hex_byte(std::to_integer<unsigned char>(b));
        }
        out = directory_ + "/" + name;
        return http::outcome::okay();
    }

    // Removes the in-flight partial (the abort path; idempotent).
    void discard_partial() {
        if (!in_file_) return;
        stream_.close();
        stream_.clear();
        std::remove(current_.file_system_file_name.c_str());
        in_file_ = false;
    }

    // The destruction-time consultation: once per completed file,
    // false/null/throwing -> removed (v2 parity).
    void settle_completed() {
        for (const forms::part_file_info& file : completed_) {
            bool keep = false;
            if (keep_) {
                try {
                    keep = keep_(file.name, file.filename, file);
                } catch (...) {
                    keep = false;
                }
            }
            if (!keep) {
                std::remove(file.file_system_file_name.c_str());
            }
        }
    }

    std::string directory_;
    bool random_names_ = true;
    forms::part_keep_callback keep_;
    std::vector<forms::part_file_info> completed_;
    std::vector<std::pair<std::string, std::string>> entries_;
    std::string field_name_;
    std::string field_value_;
    forms::part_file_info current_;
    std::ofstream stream_;
    bool in_file_ = false;
    bool in_field_ = false;
};

}  // namespace detail

namespace forms {

temp_file_part_sink::temp_file_part_sink() : state_(std::make_unique<detail::multipart_temp_state>(temp_file_options{"/tmp"})) { }

temp_file_part_sink::temp_file_part_sink(temp_file_options options) : state_(std::make_unique<detail::multipart_temp_state>(std::move(options))) { }

temp_file_part_sink::~temp_file_part_sink() {
    // The RAII backstop: a still-in-flight part (the driver normally
    // aborted it already) and every unclaimed completed file go.
    state_->discard_partial();
    state_->settle_completed();
}

http::outcome temp_file_part_sink::on_part_begin(
    const part_descriptor& part) {
    state_->discard_partial();
    if (part.filename.empty()) {
        state_->in_field_ = true;
        state_->field_name_.assign(part.name);
        state_->field_value_.clear();
        return http::outcome::okay();
    }
    std::string path;
    if (state_->random_names_) {
        const http::outcome drawn = state_->random_path(path);
        if (!drawn.ok()) return drawn;
    } else {
        const std::string safe = sanitize_upload_filename(part.filename);
        if (safe.empty()) {
            return http::outcome(
                http::outcome_code::invalid_argument,
                "temp_file_part_sink: client filename is unsafe");
        }
        path = state_->directory_ + "/" + safe;
    }
    // v2 pre-unlinks a leftover so the stream never appends to one.
    std::remove(path.c_str());
    state_->stream_.open(path,
                         std::ios::binary | std::ios::trunc);
    if (!state_->stream_.is_open()) {
        return http::outcome(
            http::outcome_code::protocol_error,
            "temp_file_part_sink: cannot create the upload file");
    }
    state_->in_file_ = true;
    state_->current_ = part_file_info{
        std::string(part.name), std::string(part.filename), path,
        std::string(part.content_type), 0};
    return http::outcome::okay();
}

http::outcome temp_file_part_sink::on_part_data(
    std::span<const std::byte> data) {
    if (state_->in_file_) {
        const char* raw = reinterpret_cast<const char*>(data.data());
        state_->stream_.write(raw, static_cast<std::streamsize>(data.size()));
        if (!state_->stream_.good()) {
            return http::outcome(
                http::outcome_code::protocol_error,
                "temp_file_part_sink: upload write failed");
        }
        state_->current_.file_size += data.size();
        return http::outcome::okay();
    }
    if (state_->in_field_) {
        const char* raw = reinterpret_cast<const char*>(data.data());
        state_->field_value_.append(raw, data.size());
    }
    return http::outcome::okay();
}

http::outcome temp_file_part_sink::on_part_end() {
    if (state_->in_file_) {
        state_->stream_.close();
        if (state_->stream_.fail()) {
            state_->discard_partial();
            return http::outcome(
                http::outcome_code::protocol_error,
                "temp_file_part_sink: upload close failed");
        }
        state_->stream_.clear();
        state_->in_file_ = false;
        state_->completed_.push_back(state_->current_);
        return http::outcome::okay();
    }
    if (state_->in_field_) {
        state_->entries_.emplace_back(std::move(state_->field_name_),
                                      std::move(state_->field_value_));
        state_->in_field_ = false;
    }
    return http::outcome::okay();
}

void temp_file_part_sink::on_part_abort(http::outcome) {
    state_->discard_partial();
    state_->in_field_ = false;
    state_->field_value_.clear();
}

const std::vector<part_file_info>& temp_file_part_sink::completed() const noexcept {
    return state_->completed_;
}

form_fields temp_file_part_sink::take_fields() {
    return form_fields(std::move(state_->entries_));
}

}  // namespace forms

}  // namespace httpserver
