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

#include "httpserver/http_response.hpp"

#include <microhttpd.h>

#include <cstddef>
#include <iostream>
#include <map>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

#include "httpserver/detail/body.hpp"   // complete type for body_->~body()
#include "httpserver/http_utils.hpp"

namespace httpserver {

// -----------------------------------------------------------------------
// Layout / trait acceptance asserts (TASK-009 AC). Duplicated in
// test/unit/http_response_sbo_test.cpp; placing them in the .cpp catches
// drift on every library build, even before tests are linked.
// -----------------------------------------------------------------------
static_assert(std::is_nothrow_move_constructible_v<http_response>,
              "TASK-009 AC: move ctor must be noexcept");
static_assert(std::is_nothrow_move_assignable_v<http_response>,
              "TASK-009 AC: move assign must be noexcept");
static_assert(!std::is_copy_constructible_v<http_response>,
              "TASK-009 AC: move-only");
static_assert(!std::is_copy_assignable_v<http_response>,
              "TASK-009 AC: move-only");
static_assert(http_response::body_buf_size == 64,
              "DR-005: SBO buffer is 64 bytes");
static_assert(alignof(http_response) >= 16,
              "alignas(16) std::byte body_storage_[64] requires class "
              "alignment >= 16");

// -----------------------------------------------------------------------
// Body lifecycle helpers.
//
// destroy_body() and adopt_body_from() factor out the SBO destruct /
// adopt logic that the destructor, move ctor, and move-assign all need.
// Keeping each branch in exactly one place makes the inline-vs-heap
// discriminator impossible to get out of sync. Both helpers are
// noexcept (DR-005): destroy_body relies on body subclass dtors being
// noexcept, adopt_body_from relies on the noexcept move_into() virtual
// (statically asserted per-subclass in detail/body.hpp).
//
// Members are private; they live as out-of-line member functions so
// they have access without an extra friend declaration.
// -----------------------------------------------------------------------
void http_response::destroy_body() noexcept {
    if (body_ == nullptr) return;
    body_->~body();
    if (!body_inline_) {
        // Heap path: ::operator delete pairs with the
        // ::operator new(sizeof(T)) the factory uses (TASK-010).
        ::operator delete(body_);
    }
    body_ = nullptr;
    body_inline_ = false;
}

void http_response::adopt_body_from(http_response& o) noexcept {
    if (o.body_ == nullptr) {
        return;  // destination's body_/body_inline_ already cleared
    }
    if (o.body_inline_) {
        // Placement-move into our buffer, then destroy the source's
        // inline body so the source's destructor is a no-op.
        o.body_->move_into(body_storage_);
        body_ = reinterpret_cast<detail::body*>(body_storage_);
        body_inline_ = true;
        o.body_->~body();
    } else {
        // Heap path: pointer transfer — no allocation, no copy.
        body_ = o.body_;
        body_inline_ = false;
    }
    o.body_ = nullptr;
    o.body_inline_ = false;
}

// -----------------------------------------------------------------------
// Destructor.
//
// Subclass-virtual destructor: required as long as the v1 subclass
// hierarchy still inherits from http_response. TASK-013 marks the class
// `final` once those subclasses are removed.
// -----------------------------------------------------------------------
http_response::~http_response() {
    destroy_body();
}

// -----------------------------------------------------------------------
// Move constructor.
//
// noexcept because every member's move is noexcept (header_map is a
// std::map, std::map move is noexcept; std::byte[64] is trivially
// movable; per-subclass body move ctors are noexcept by static_assert in
// detail/body.hpp).
// -----------------------------------------------------------------------
http_response::http_response(http_response&& o) noexcept
    : status_code_(o.status_code_),
      headers_(std::move(o.headers_)),
      footers_(std::move(o.footers_)),
      cookies_(std::move(o.cookies_)),
      kind_(o.kind_) {
    adopt_body_from(o);
}

// -----------------------------------------------------------------------
// Move-assignment.
//
// Linearises the inline×heap inline×heap "four cases" into:
//   step 1 — destroy our existing body
//   step 2 — adopt source's body
//
// Self-assignment is guarded explicitly because step 1 would otherwise
// destroy the body we are about to read from.
// -----------------------------------------------------------------------
http_response& http_response::operator=(http_response&& o) noexcept {
    if (this == &o) {
        return *this;
    }
    destroy_body();
    status_code_ = o.status_code_;
    headers_ = std::move(o.headers_);
    footers_ = std::move(o.footers_);
    cookies_ = std::move(o.cookies_);
    kind_ = o.kind_;
    adopt_body_from(o);
    return *this;
}

MHD_Response* http_response::get_raw_response() {
    return MHD_create_response_from_buffer(0, nullptr, MHD_RESPMEM_PERSISTENT);
}

void http_response::decorate_response(MHD_Response* response) {
    std::map<std::string, std::string, http::header_comparator>::iterator it;

    for (it=headers_.begin() ; it != headers_.end(); ++it) {
        MHD_add_response_header(response, (*it).first.c_str(), (*it).second.c_str());
    }

    for (it=footers_.begin() ; it != footers_.end(); ++it) {
        MHD_add_response_footer(response, (*it).first.c_str(), (*it).second.c_str());
    }

    for (it=cookies_.begin(); it != cookies_.end(); ++it) {
        MHD_add_response_header(response, "Set-Cookie", ((*it).first + "=" + (*it).second).c_str());
    }
}

int http_response::enqueue_response(MHD_Connection* connection, MHD_Response* response) {
    return MHD_queue_response(connection, status_code_, response);
}

void http_response::shoutCAST() {
    status_code_ |= http::http_utils::shoutcast_response;
}

namespace {
static inline http::header_view_map to_view_map(const http::header_map& hdr_map) {
    http::header_view_map view_map;
    for (const auto& item : hdr_map) {
        view_map[std::string_view(item.first)] = std::string_view(item.second);
    }
    return view_map;
}
}

std::ostream &operator<< (std::ostream& os, const http_response& r) {
    os << "Response [response_code:" << r.status_code_ << "]" << std::endl;

    http::dump_header_map(os, "Headers", to_view_map(r.headers_));
    http::dump_header_map(os, "Footers", to_view_map(r.footers_));
    http::dump_header_map(os, "Cookies", to_view_map(r.cookies_));

    return os;
}

}  // namespace httpserver
