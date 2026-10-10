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

// Installed-package runtime check for the multipart invocation snapshot.
#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <utility>

#include <httpserver/forms/multipart.hpp>

namespace {
namespace http = httpserver::http;
namespace forms = httpserver::forms;

class input_body final : public httpserver::detail::body_source {
 public:
    httpserver::detail::body_pull_result pull(std::span<std::byte> into) override {
        if (at_ == wire_.size()) return {httpserver::detail::body_pull::end, 0};
        const std::size_t n = std::min(into.size(), wire_.size() - at_);
        for (std::size_t i = 0; i < n; ++i) into[i] = std::byte(wire_[at_++]);
        return {httpserver::detail::body_pull::data, n};
    }
    const http::fields& trailers() const noexcept override { return trailers_; }
    const http::outcome& failure() const noexcept override { return failure_; }
    void park(httpserver::detail::body_wait&) override { }
    void unpark(httpserver::detail::body_wait&) override { }

 private:
    const std::string wire_ = "--X\r\n"
        "Content-Disposition: form-data; name=\"field\"\r\n\r\n"
        "value\r\n--X--\r\n";
    std::size_t at_ = 0;
    http::fields trailers_;
    http::outcome failure_;
};

class parts final : public forms::part_sink {
 public:
    http::outcome on_part_begin(const forms::part_descriptor& part) override {
        ++begins;
        name = part.name;
        return http::outcome::okay();
    }
    http::outcome on_part_data(std::span<const std::byte> data) override {
        for (const std::byte b : data) value += static_cast<char>(b);
        return http::outcome::okay();
    }
    http::outcome on_part_end() override {
        ++ends;
        return http::outcome::okay();
    }
    void on_part_abort(http::outcome) override { ++aborts; }
    std::string name;
    std::string value;
    int begins = 0;
    int ends = 0;
    int aborts = 0;
};

httpserver::task<forms::multipart_read> create_read(
    httpserver::exchange& x, forms::part_sink& sink) {
    return forms::read_multipart(x, forms::multipart_limits{1024, 1, 5, 128}, sink);
}

bool read_snapshot(bool temporary) {
    http::request_head head;
    head.head_fields.append("Content-Type", "multipart/form-data; boundary=X");
    input_body input;
    httpserver::exchange x(head, nullptr, 0, &input);
    parts sink;
    forms::multipart_limits limits{1024, 1, 5, 128};
    auto operation = temporary ? create_read(x, sink)
                              : forms::read_multipart(x, limits, sink);
    limits = forms::multipart_limits{1, 1, 1, 1};
    if (x.state() != httpserver::exchange_state::head) return false;
    httpserver::manual_executor executor;
    int delivered = 0;
    bool okay = false;
    httpserver::spawn(executor, std::move(operation),
        [&](httpserver::task_result<forms::multipart_read> result) {
            ++delivered;
            okay = result.has_value() && result.value().ok()
                && result.value().parts_completed == 1;
        });
    executor.run_pending();
    executor.run_pending();
    return okay && delivered == 1 && sink.begins == 1 && sink.ends == 1
        && sink.aborts == 0 && sink.name == "field" && sink.value == "value";
}
}  // namespace

int main() {
    return read_snapshot(false) && read_snapshot(true) ? 0 : 1;
}
