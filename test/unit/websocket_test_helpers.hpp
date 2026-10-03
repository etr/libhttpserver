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

#ifndef TEST_UNIT_WEBSOCKET_TEST_HELPERS_HPP_
#define TEST_UNIT_WEBSOCKET_TEST_HELPERS_HPP_
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
#include <memory>
#include <utility>
#include <httpserver/websocket/session.hpp>
#include <httpserver/detail/websocket_driver.hpp>
namespace ws_test {
namespace ws = httpserver::websocket;
// Test-only aggregate driver/application owner; production consumers see
// only the semantic session returned by their HTTP upgrade adapter.
class test_session : public ws::session {
 public:
    explicit test_session(ws::options options = {}, ws::session::close_callback callback = {})
        : test_session(std::make_unique<httpserver::detail::websocket_driver>(options, std::move(callback))) { }
    ws::feed_result feed(std::span<const std::byte> data) { return driver_->feed(data); }
    std::size_t copy_output(std::span<std::byte> into) { return driver_->copy_output(into); }
    httpserver::http::outcome consume_output(std::size_t n) { return driver_->consume_output(n); }
    ws::queue_usage usage() { return driver_->usage(); }
    void eof() { driver_->eof(); }
    void cancel(httpserver::http::outcome why) { driver_->cancel(std::move(why)); }
 private:
    explicit test_session(std::unique_ptr<httpserver::detail::websocket_driver> driver)
        : ws::session(driver->take_session()), driver_(std::move(driver)) { }
    std::unique_ptr<httpserver::detail::websocket_driver> driver_;
};
inline std::vector<std::byte> bytes(std::string_view s) {
    return {reinterpret_cast<const std::byte*>(s.data()),
            reinterpret_cast<const std::byte*>(s.data() + s.size())};
}
// Independent RFC 6455 client wire builder, deliberately distinct from
// the library's server encoder; fixed mask makes vectors reproducible.
inline std::vector<std::byte> frame(unsigned op, std::span<const std::byte> data,
                                     bool fin = true, bool masked = true) {
    std::vector<std::byte> out{std::byte((fin ? 128 : 0) | op)};
    unsigned flag = masked ? 128 : 0;
    if (data.size() < 126) {
        out.push_back(std::byte(flag | data.size()));
    } else if (data.size() <= 65535) {
        out.push_back(std::byte(flag | 126));
        out.push_back(std::byte(data.size() >> 8));
        out.push_back(std::byte(data.size()));
    } else {
        out.push_back(std::byte(flag | 127));
        for (int i = 7; i >= 0; --i) out.push_back(std::byte(data.size() >> (8 * i)));
    }
    const std::byte mask[] = {std::byte{17}, std::byte{34}, std::byte{51}, std::byte{68}};
    if (masked) out.insert(out.end(), mask, mask + 4);
    for (std::size_t i = 0; i < data.size(); ++i)
        out.push_back(masked ? data[i] ^ mask[i % 4] : data[i]);
    return out;
}
inline ws::options small() {
    ws::options o;
    o.max_message_bytes = 8; o.incoming_bytes = 16; o.output_bytes = 10;
    o.incoming_messages = 2; o.outgoing_messages = 2;
    return o;
}
inline ws::receive_result receive(test_session& s) {
    httpserver::manual_executor ex;
    ws::receive_result result;
    bool done = false;
    httpserver::spawn(ex, s.receive(), [&](httpserver::task_result<ws::receive_result> r) {
        if (r.has_value()) result = std::move(r).value();
        done = true;
    });
    ex.run_pending();
    if (!done) s.cancel({httpserver::http::outcome_code::cancelled, "test cleanup"});
    ex.run_pending();
    return result;
}
inline std::vector<std::byte> output(test_session& s, std::size_t chunk = 1) {
    std::vector<std::byte> out;
    std::vector<std::byte> scratch(chunk);
    while (auto n = s.copy_output(scratch)) {
        out.insert(out.end(), scratch.begin(), scratch.begin() + n);
        if (!s.consume_output(n).ok()) break;
    }
    return out;
}
}  // namespace ws_test
#endif  // TEST_UNIT_WEBSOCKET_TEST_HELPERS_HPP_
