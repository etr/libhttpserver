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

#ifndef SRC_HTTPSERVER_CONCURRENCY_CONCURRENCY_HPP_
#define SRC_HTTPSERVER_CONCURRENCY_CONCURRENCY_HPP_

// Umbrella for the v3 concurrency core (architecture §3.1, DR-V3-003):
// executor seam, task<T> coroutine ABI, cancellation fan-out, and the
// application resume signal. All headers are self-contained and compile
// identically with every build/TLS configuration.

#include <httpserver/concurrency/cancellation.hpp>
#include <httpserver/concurrency/executor.hpp>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/concurrency/task.hpp>

#endif  // SRC_HTTPSERVER_CONCURRENCY_CONCURRENCY_HPP_
