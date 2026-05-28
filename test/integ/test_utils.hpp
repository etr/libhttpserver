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

// test_utils.hpp — shared helpers for integration-test translation units.
//
// Single include removes the need for per-file copies of widely-used
// test idioms. Include after <memory> and before any test-specific code.

#ifndef TEST_INTEG_TEST_UTILS_HPP_
#define TEST_INTEG_TEST_UTILS_HPP_

#include <memory>

#include "./httpserver.hpp"

namespace test_utils {

// as_shared — wrap a stack-local http_resource& in a shared_ptr with a
// no-op deleter.
//
// Purpose: integration tests frequently declare an http_resource on the
// stack (for brevity and automatic cleanup) and pass it to register_path /
// register_prefix, which require a shared_ptr. The no-op deleter tells the
// shared_ptr NOT to delete the pointed-to object, because the stack frame
// owns its lifetime.
//
// Safety contract: the caller MUST ensure the resource outlives the
// webserver. In practice every test achieves this by calling ws.stop()
// (which drains all in-flight requests) before the test body returns, so
// the stack frame with the resource is still live when the webserver
// releases its last reference. Declaring the resource object BEFORE the
// webserver object also achieves the same guarantee via LIFO destruction.
//
// Use make_shared<T>() instead when the test does not specifically exercise
// the stack-allocation pattern.
inline std::shared_ptr<httpserver::http_resource>
as_shared(httpserver::http_resource& r) {
    return std::shared_ptr<httpserver::http_resource>(
        &r, [](httpserver::http_resource*){});
}

}  // namespace test_utils

// Pull as_shared into the anonymous namespace of the including TU so that
// existing call sites (which use unqualified `as_shared(...)`) continue to
// compile without modification.
namespace {
using test_utils::as_shared;
}  // namespace

#endif  // TEST_INTEG_TEST_UTILS_HPP_
