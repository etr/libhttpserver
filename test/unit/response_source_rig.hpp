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

#ifndef TEST_UNIT_RESPONSE_SOURCE_RIG_HPP_
#define TEST_UNIT_RESPONSE_SOURCE_RIG_HPP_

// TASK-113 test rig for file, pipe and borrowed-buffer response
// sources: the temp_file scratch asset (extracted from
// response_definition_test.cpp when the file-backed suites grew), a
// destruction-counting lease keeper, a counting close operation for
// transferred std::FILE* handles, and a pre-filled pipe fixture
// (pipe(2) + fdopen, write end closed so the read end sees EOF right
// after the payload).

#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <httpserver/response_sources.hpp>

namespace httpserver_test {

// RAII scratch file: created from `content`, rewritable, removed on
// destruction.
class temp_file {
 public:
    explicit temp_file(const std::string& content) {
        char pattern[] = "/tmp/libhttpserver_task113_XXXXXX";
        const int fd = ::mkstemp(pattern);
        if (fd < 0) throw std::runtime_error("task113: mkstemp failed");
        ::close(fd);
        path_ = pattern;
        rewrite(content);
    }

    ~temp_file() { ::unlink(path_.c_str()); }

    temp_file(const temp_file&) = delete;
    temp_file& operator=(const temp_file&) = delete;

    const std::string& path() const noexcept { return path_; }

    void rewrite(const std::string& content) {
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out.write(content.data(),
                  static_cast<std::streamsize>(content.size()));
    }

 private:
    std::string path_;
};

// A lease keeper whose destructor bumps a counter held by the test:
// the borrowed-body suites pin that the frozen body block releases
// its keeper exactly once, however many definitions share it.
class tracked_keeper {
 public:
    explicit tracked_keeper(std::shared_ptr<std::atomic<int>> count)
        : count_(std::move(count)) {
    }

    ~tracked_keeper() {
        if (count_) count_->fetch_add(1, std::memory_order_relaxed);
    }

    tracked_keeper(const tracked_keeper&) = delete;
    tracked_keeper& operator=(const tracked_keeper&) = delete;

 private:
    std::shared_ptr<std::atomic<int>> count_;
};

// A body span over plain string memory (the suites build payloads as
// text).
inline std::span<const std::byte> byte_span(const std::string& text) {
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(text.data()), text.size());
}

}  // namespace httpserver_test

#endif  // TEST_UNIT_RESPONSE_SOURCE_RIG_HPP_
