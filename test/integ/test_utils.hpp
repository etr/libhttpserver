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

#ifndef TEST_INTEG_TEST_UTILS_HPP_
#define TEST_INTEG_TEST_UTILS_HPP_

// Shared helpers for the integration test suite.
//
// Historical note: this header previously exposed `as_shared(http_resource&)`,
// a thin wrapper that returned a std::shared_ptr<http_resource> with a no-op
// deleter pointing at a stack-allocated resource. That helper was incompatible
// with MHD's request_completed callback, which fires from a daemon worker
// thread during webserver::stop(). When the test body returned, the stack-
// allocated resource was destroyed before stop() drained the daemon, so the
// callback dereferenced freed memory through the still-live no-op-deleter
// shared_ptr the webserver was holding. A wildcard valgrind suppression had
// been masking the UAF.
//
// The fix: tests now register resources via `std::make_shared<T>(...)`. The
// resource's storage lives on the heap and stays alive until the webserver
// releases its last reference (which happens AFTER stop() has drained MHD).
// The `as_shared` helper is gone -- there is no safe shape that preserves
// stack-allocation as the storage strategy.

#endif  // TEST_INTEG_TEST_UTILS_HPP_
