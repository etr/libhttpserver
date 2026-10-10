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

#include "httpserver/http.hpp"
#include "httpserver/http/outcome.hpp"
#include "httpserver/http/protocol.hpp"
#include "httpserver/http/method.hpp"
#include "httpserver/http/fields.hpp"
#include "httpserver/http/request_head.hpp"
#include "httpserver/concurrency/executor.hpp"
#include "httpserver/concurrency/task.hpp"
#include "httpserver/concurrency/cancellation.hpp"
#include "httpserver/concurrency/resume_signal.hpp"
#include "httpserver/concurrency/concurrency.hpp"
#include "httpserver/server/budgets.hpp"
#include "httpserver/server/options.hpp"
#include "httpserver/server/tls_peer_metadata.hpp"
#include "httpserver/server/routes.hpp"
#include "httpserver/server/configuration.hpp"
#include "httpserver/server/server.hpp"
#include "httpserver/server/readiness.hpp"
#include "httpserver/http/status.hpp"
#include "httpserver/exchange.hpp"
#include "httpserver/body_reader.hpp"
#include "httpserver/response_writer.hpp"
#include "httpserver/response_sources.hpp"
#include "httpserver/response_definition.hpp"
#include "httpserver/server/route_sync.hpp"
#include "httpserver/auth/basic_auth.hpp"
#include "httpserver/auth/digest_auth.hpp"
#include "httpserver/forms/urlencoded.hpp"
#include "httpserver/forms/multipart.hpp"
#include "httpserver/server/hooks.hpp"
#include "httpserver/net/address.hpp"
#include "httpserver/server/peer_policy.hpp"
#include "httpserver/websocket/message.hpp"
#include "httpserver/websocket/options.hpp"
#include "httpserver/websocket/session.hpp"
#include "httpserver/features.hpp"
#include "httpserver/server/diagnostics.hpp"

#endif  // SRC_HTTPSERVER_HPP_
