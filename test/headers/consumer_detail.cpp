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

// Negative test (Check A.2): a consumer including a detail header directly,
// even when _HTTPSERVER_HPP_INSIDE_ is defined (simulating the umbrella state),
// must hit the gate when HTTPSERVER_COMPILATION is not defined.
//
// NOTE: pre-Phase-3 the detail gate is dual-mode (accepts either macro), so
// this TU defines _HTTPSERVER_HPP_INSIDE_ to exercise the strictest
// post-cleanup behavior. After TASK-014 lands the PIMPL split, the gate may
// drop the _HTTPSERVER_HPP_INSIDE_ acceptor altogether; this test should keep
// passing because the consumer-style invocation also lacks HTTPSERVER_COMPILATION.
//
// For TASK-002 we keep the dual-mode gate (per the plan's Phase 3a-i), so this
// TU is built WITHOUT defining _HTTPSERVER_HPP_INSIDE_ — the detail gate then
// fires for the same reason as A.1.
#include "httpserver/details/http_endpoint.hpp"
int main() { return 0; }
