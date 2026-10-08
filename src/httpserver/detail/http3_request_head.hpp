/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "http3_request_head.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP3_REQUEST_HEAD_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP3_REQUEST_HEAD_HPP_
#include <optional>
#include <httpserver/detail/qpack_field_section.hpp>
#include <httpserver/detail/http1_target.hpp>
#include <httpserver/http/request_head.hpp>
namespace httpserver::detail {
bool http3_regular_field(std::string_view name, std::string_view value);
bool http3_trailer_field(std::string_view name, std::string_view value);
bool http3_content_length(const http::fields& fields, std::optional<std::uint64_t>& length);
bool http3_convert_request(std::span<const qpack_field> fields, http::request_head& head);
bool http3_convert_trailers(std::span<const qpack_field> fields, http::fields& trailers);
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP3_REQUEST_HEAD_HPP_
