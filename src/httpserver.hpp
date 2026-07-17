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

#ifndef SRC_HTTPSERVER_HPP_
#define SRC_HTTPSERVER_HPP_

#if __cplusplus < 202002L
#  error("libhttpserver requires C++20 or later.")
#endif

#define _HTTPSERVER_HPP_INSIDE_

#include "httpserver/body_kind.hpp"
#include "httpserver/constants.hpp"
#include "httpserver/cookie.hpp"
#include "httpserver/feature_unavailable.hpp"
#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_handle.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/http_arg_value.hpp"
#include "httpserver/http_method.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/ip_representation.hpp"
#include "httpserver/iovec_entry.hpp"
#include "httpserver/file_info.hpp"
#include "httpserver/webserver.hpp"
// Included unconditionally. websocket_handler.hpp is safe to
// include in both HAVE_WEBSOCKET-on and HAVE_WEBSOCKET-off builds; the
// member-function bodies in src/websocket_handler.cpp handle the
// disabled-build sentinel behaviour.
#include "httpserver/websocket_handler.hpp"

#undef _HTTPSERVER_HPP_INSIDE_

#endif  // SRC_HTTPSERVER_HPP_
