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

// TASK-103 step 5: a consumer including the v3 body reader —
// <httpserver/body_reader.hpp> and <httpserver/exchange.hpp> — without
// HTTPSERVER_COMPILATION (or any other build/TLS configuration macro)
// must compile and link cleanly. The body area is header-only, so the
// empty LDADD of this target is itself part of the contract. Every
// public body_reader member is named through the exchange's body()
// accessor (the shape route handlers receive); the test passes by
// virtue of compiling and linking.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <httpserver/body_reader.hpp>
#include <httpserver/exchange.hpp>

int use_v3_body_reader(httpserver::exchange& x) {
    using httpserver::body_collect;
    using httpserver::body_read;
    using httpserver::body_reader;

    // The reader is reached only through the exchange.
    body_reader& body = x.body();

    // Bounded incremental read: at least one byte per resolution, one
    // outstanding read at a time.
    std::vector<std::byte> buffer(64);
    httpserver::task<body_read> read =
        body.read_some(std::span<std::byte>(buffer));

    // Fully buffered read with an exact over-limit outcome.
    httpserver::task<body_collect> collected = body.collect(4096);

    // Received trailers; final once a read returned end of body.
    const std::size_t trailer_count = body.trailers().size();

    // Result shapes: typed outcome, consumed destination prefix (never
    // empty on a data read), end flag; collect's whole-body vector.
    const body_read shape{httpserver::http::outcome(),
                          std::span<const std::byte>(), false};
    const body_collect whole{httpserver::http::outcome(), {}};
    const bool ended = shape.end_of_body || whole.data.empty();

    // The engine delivery seam is a defaulted construction argument.
    httpserver::http::request_head head;
    httpserver::exchange seeded(head, nullptr);
    body_reader& seeded_body = seeded.body();
    const bool usable = &seeded_body == &seeded.body();

    return (read.valid() && collected.valid() && ended
            && trailer_count == 0 && usable) ? 0 : 1;
}

int main() {
    // Nothing runs: exercising a body read needs an admitted exchange
    // with a live engine behind it, and this sentinel must never need
    // one. Compiling is the contract.
    return 0;
}
