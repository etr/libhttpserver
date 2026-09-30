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

// TASK-104 step 5: a consumer including the v3 response writer —
// <httpserver/response_writer.hpp> and <httpserver/exchange.hpp> —
// without HTTPSERVER_COMPILATION (or any other build/TLS configuration
// macro) must compile and link cleanly. The writer area is
// header-only, so the empty LDADD of this target is itself part of the
// contract. Every public response_writer member is named through the
// exchange's start_response()/writer() surface (the shape route
// handlers receive); the test passes by virtue of compiling and
// linking.

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include <httpserver/exchange.hpp>
#include <httpserver/response_writer.hpp>

int use_v3_response_writer(httpserver::exchange& x) {
    using httpserver::body_finish;
    using httpserver::body_write;
    using httpserver::response_writer;

    // The streaming decision commits the head; the writer is reached
    // only through the exchange.
    const httpserver::http::outcome committed =
        x.start_response(httpserver::http::status::from_code(200),
                         httpserver::http::fields());
    response_writer& writer = x.writer();

    // One bounded chunk write; one outstanding write at a time.
    std::vector<std::byte> chunk(16);
    httpserver::task<body_write> write =
        writer.write(std::span<const std::byte>(chunk));

    // Finish ends the body; trailers ride the final framing.
    httpserver::http::fields trailers;
    httpserver::task<body_finish> done = writer.finish(trailers);

    // Result shapes: typed outcome plus the accepted byte count for a
    // write; the bare typed outcome for finish.
    const body_write chunk_shape{httpserver::http::outcome(), 0};
    const body_finish end_shape{httpserver::http::outcome()};
    const bool shaped = chunk_shape.accepted == 0;

    // The writer is movable (it rides the exchange's move) but never
    // copied; the moved writer's operations are still nameable.
    httpserver::exchange seeded(x.head(), nullptr);
    response_writer moved = std::move(seeded.writer());
    httpserver::task<body_write> moved_write =
        moved.write(std::span<const std::byte>(chunk));

    return (committed.ok() || !committed.ok()) && write.valid()
           && done.valid() && shaped && moved_write.valid() ? 0 : 1;
}

int main() {
    // Nothing runs: exercising a response write needs an exchange with
    // a live engine behind it, and this sentinel must never need one.
    // Compiling is the contract.
    return 0;
}
