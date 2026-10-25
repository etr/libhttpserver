/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include "test/support/quic_interop_files.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <stdexcept>
namespace hqnet {
struct files::request {
    std::string line;
    int fd = -1;
    std::uint64_t offset = 0, size = 0;
    std::array<hd::quic_information_id, 8> pending{};
    bool finished = false;
    ~request() { if (fd >= 0) ::close(fd); }
};
files::files(const std::string& root) : root_(::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) {
    const int saved_errno = root_ < 0 ? errno : 0;
    // Diagnostic branch only: preserve errno before any logging or metadata call.
    std::fprintf(stderr, "ROOT_CAPTURE root_bytes=%zu root=%.*s fd=%d errno=%d uid=%" PRIu64 " euid=%" PRIu64 "\n",
        root.size(), 4096, root.c_str(), root_, saved_errno,
        static_cast<std::uint64_t>(getuid()), static_cast<std::uint64_t>(geteuid()));
    struct stat info{};
    const int stat_rc = lstat(root.c_str(), &info);
    const int stat_errno = stat_rc < 0 ? errno : 0;
    std::fprintf(stderr, "ROOT_CAPTURE lstat_rc=%d errno=%d mode=%" PRIo64 " symlink=%d directory=%d\n",
        stat_rc, stat_errno, static_cast<std::uint64_t>(info.st_mode),
        S_ISLNK(info.st_mode), S_ISDIR(info.st_mode));
    for (const char* key : {"TMPDIR", "TMP", "TEMP", "TEMPDIR"}) {
        const char* value = std::getenv(key);
        std::fprintf(stderr, "ROOT_CAPTURE env=%s value=%.*s\n", key, 4096, value ? value : "<unset>");
    }
    errno = saved_errno;
    if (root_ < 0) throw std::runtime_error("interop root unavailable");
}
files::~files() { if (root_ >= 0) ::close(root_); }
void files::receive(hd::quic_stream_state& stream, hd::quic_flow_control& flow) {
    auto& owned = requests_[stream.id()];
    if (!owned) owned = std::make_unique<request>();
    auto& r = *owned;
    std::array<std::byte, 1024> input{};
    for (;;) {
        auto read = stream.read(input);
        if (read.code != hd::quic_stream_code::ok) throw std::runtime_error("interop read failed");
        if (!read.bytes) break;
        if (r.fd >= 0 || r.finished || r.line.size() + read.bytes > 4096) throw std::runtime_error("interop request bound");
        r.line.append(reinterpret_cast<const char*>(input.data()), read.bytes);
        if (!flow.consume_protocol(stream, stream.consumed())) throw std::runtime_error("interop consumption failed");
    }
    if (r.fd >= 0 || r.finished) return;
    auto end = r.line.find("\r\n");
    if (end == std::string::npos) {
        if (stream.final_size() && stream.consumed() == *stream.final_size()) throw std::runtime_error("incomplete interop request");
        return;
    }
    if (end + 2 != r.line.size() || !r.line.starts_with("GET /")) throw std::runtime_error("malformed interop GET");
    const auto name = r.line.substr(5, end - 5);
    if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\\0", 0, 3) != std::string::npos) throw std::runtime_error("unsafe interop path");
    const auto fd = ::openat(root_, name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    struct stat info{};
    if (fd < 0) throw std::runtime_error("interop file unavailable");
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 || info.st_size > 16 * 1024 * 1024) {
        ::close(fd);
        throw std::runtime_error("interop regular file bound");
    }
    r.fd = fd;
    r.size = info.st_size;
    r.line.clear();
}
void files::pump(hd::quic_flow_control& flow, hd::quic_recovery& recovery) {
    for (auto& [id, owned] : requests_) {
        auto& r = *owned;
        if (r.fd < 0 || r.finished) continue;
        auto slot = std::find(r.pending.begin(), r.pending.end(), 0);
        if (slot == r.pending.end()) continue;
        const auto requested = std::min<std::uint64_t>(8192, r.size - r.offset);
        auto allowance = flow.send_allowance(id, r.offset, requested);
        if (!allowance) continue;
        std::array<std::byte, 8192> bytes{};
        const auto count = allowance.bytes;
        if (count && pread(r.fd, bytes.data(), count, r.offset) != static_cast<ssize_t>(count)) throw std::runtime_error("interop file changed or read failed");
        const bool fin = r.offset + count == r.size;
        auto retained = recovery.retain_stream({id, r.offset, std::span(bytes).first(count), fin});
        if (!retained) {
            if (retained.code == hd::quic_recovery_code::capacity || retained.code == hd::quic_recovery_code::no_memory) continue;
            throw std::runtime_error("interop STREAM retention failed");
        }
        *slot = retained.id;
        r.offset += count;
        r.finished = fin;
    }
}
void files::completed(const hd::quic_information_completion& receipt) {
    const auto found = requests_.find(receipt.stream);
    if (receipt.kind != hd::quic_information_kind::stream || found == requests_.end()) return;
    auto& r = *found->second;
    auto slot = std::find(r.pending.begin(), r.pending.end(), receipt.id);
    if (!receipt.id || slot == r.pending.end()) throw std::runtime_error("interop completion identity");
    *slot = 0;
    if (receipt.fin) {
        ::close(r.fd);
        r.fd = -1;
    }
}
void files::cancel(std::uint64_t stream, hd::quic_recovery& recovery) {
    if (auto found = requests_.find(stream); found != requests_.end()) {
        for (auto id : found->second->pending) if (id) recovery.cancel_information(id);
        requests_.erase(found);
    }
}
std::size_t files::active_files() const {
    return std::count_if(requests_.begin(), requests_.end(), [](const auto& entry) { return entry.second->fd >= 0; });
}
}  // namespace hqnet
