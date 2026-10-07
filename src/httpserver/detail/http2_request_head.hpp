/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "http2_request_head.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP2_REQUEST_HEAD_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP2_REQUEST_HEAD_HPP_
#include <span>
#include <httpserver/detail/hpack_field_section.hpp>
#include <httpserver/http/request_head.hpp>
namespace httpserver::detail {
bool http2_convert_request(std::span<const hpack_field> fields, http::request_head& head);
bool http2_regular_field(std::string_view name, std::string_view value);
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP2_REQUEST_HEAD_HPP_
