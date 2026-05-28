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

// Negative test (Check A.2b): a consumer including modded_request.hpp directly,
// without HTTPSERVER_COMPILATION or _HTTPSERVER_HPP_INSIDE_, must hit the gate.
// Complements consumer_detail.cpp (A.2) which covers http_endpoint.hpp; both
// detail headers carry the same gate and both must be verified independently.
//
// TODO(TASK-014): when TASK-014 tightens the detail gate to HTTPSERVER_COMPILATION-
// only (by removing the transitive include from webserver.hpp), add
// '#define _HTTPSERVER_HPP_INSIDE_' here so A.2b validates that _HTTPSERVER_HPP_INSIDE_
// alone is still rejected by the stricter HTTPSERVER_COMPILATION-only gate.
#include "httpserver/detail/modded_request.hpp"
int main() { return 0; }
