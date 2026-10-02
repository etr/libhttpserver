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

// Opens the scratch asset for reading as a std::FILE*, ready for an
// owned transfer (closing becomes the definition's business).
inline std::FILE* open_for_read(const temp_file& asset) {
    std::FILE* f = std::fopen(asset.path().c_str(), "rb");
    if (f == nullptr) {
        throw std::runtime_error("task113: fopen failed");
    }
    return f;
}

// A close operation that counts its calls: the exactly-once close
// contract of owned transfers is pinned by watching the counter, not
// by observing descriptor state.
inline httpserver::owned_close_fn counting_close(
    std::shared_ptr<std::atomic<int>> calls) {
    return [calls](std::FILE* handle) {
        calls->fetch_add(1, std::memory_order_relaxed);
        std::fclose(handle);
    };
}

// A pre-filled pipe read end as a std::FILE* (pipe(2) + fdopen; the
// write end closes immediately so the read end sees EOF right after
// the payload). One-shot by construction: no seek position, no second
// read of the same bytes.
//
// reason: MSYS2/mingw does not expose POSIX ::pipe() — Windows pipes
// use _pipe()/CreatePipe() with different fd semantics. The
// Linux/macOS CI matrix exercises this path; the Windows gap is
// tracked in test/PORTABILITY.md.
#ifndef _WIN32
class filled_pipe {
 public:
    explicit filled_pipe(const std::string& payload) {
        if (::pipe(fds_) != 0) {
            throw std::runtime_error("task113: pipe failed");
        }
        std::size_t written = 0;
        while (written < payload.size()) {
            const ssize_t n = ::write(fds_[1], payload.data() + written,
                                      payload.size() - written);
            if (n <= 0) {
                ::close(fds_[0]);
                ::close(fds_[1]);
                throw std::runtime_error("task113: pipe write failed");
            }
            written += static_cast<std::size_t>(n);
        }
        ::close(fds_[1]);
        fds_[1] = -1;
        read_ = ::fdopen(fds_[0], "r");
        if (read_ == nullptr) {
            ::close(fds_[0]);
            throw std::runtime_error("task113: fdopen failed");
        }
        fds_[0] = -1;
    }

    ~filled_pipe() {
        if (read_ != nullptr) std::fclose(read_);
    }

    filled_pipe(const filled_pipe&) = delete;
    filled_pipe& operator=(const filled_pipe&) = delete;

    std::FILE* get() const noexcept { return read_; }

    // Hands the read end away (an owned transfer takes it).
    std::FILE* release() noexcept {
        std::FILE* out = read_;
        read_ = nullptr;
        return out;
    }

 private:
    int fds_[2] = {-1, -1};
    std::FILE* read_ = nullptr;
};
#endif  // !_WIN32

}  // namespace httpserver_test

#endif  // TEST_UNIT_RESPONSE_SOURCE_RIG_HPP_
