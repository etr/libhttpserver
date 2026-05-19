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
// TASK-039 -- one-off measurement TU.
//
// Compile this against the `master` (v1.x) source tree to capture
// the two literal numbers consumed by
// test/bench_sizeof_http_resource.cpp via
// test/v1_baseline/v1_constants.hpp:
//
//   sizeof(httpserver::http_resource)            <- V1_HTTP_RESOURCE_SIZEOF
//   sizeof(std::map<std::string, bool>)          <- V1_STD_MAP_STRING_BOOL_SIZEOF
//
// This TU is NOT built by `make bench` or `make check`. It ships
// only in EXTRA_DIST as documentation. See test/v1_baseline/README.md
// for the full re-measurement workflow.
//
// One-liner (Apple clang / libc++):
//   git worktree add ../libhttpserver-v1 master
//   c++ -std=c++20 -O3 -DHTTPSERVER_COMPILATION \
//       -I../libhttpserver-v1/src \
//       test/v1_baseline/measure_v1_sizes.cpp \
//       -o /tmp/measure_v1_sizes
//   /tmp/measure_v1_sizes
//
// Expected output (libc++ on Darwin/arm64, master @ d8b055e):
//   sizeof(http_resource)             = 32
//   sizeof(std::map<std::string,bool>) = 24
//
// On libstdc++ (GCC on Linux) the numbers will be 56 and 48 respectively.

#define HTTPSERVER_COMPILATION 1
#include <cstdio>
#include <cstddef>
#include <map>
#include <string>

#include "httpserver/http_resource.hpp"

int main() {
    std::printf("sizeof(http_resource)             = %zu\n",
                sizeof(httpserver::http_resource));
    std::printf("sizeof(std::map<std::string,bool>) = %zu\n",
                sizeof(std::map<std::string, bool>));
    return 0;
}
