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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-096: pluggable server seam for the transcript runner (D5).
//
// The runner is server-agnostic: every server construction goes through
// this interface. The v2 implementation lives in v2_fixture.hpp; M9+
// adds a v3 native-engine fixture behind the same surface so the
// committed *.tseq corpus becomes executable parity, not a one-off.

#ifndef TEST_PARITY_SERVER_FIXTURE_HPP_
#define TEST_PARITY_SERVER_FIXTURE_HPP_

#include <cstdint>
#include <string>

namespace parity {

class server_fixture {
 public:
    virtual ~server_fixture() = default;

    // Does the registry know this profile name at all?
    virtual bool has_profile(const std::string& name) const = 0;

    // Is the profile runnable on this build (feature gates: TLS, Digest
    // auth, websocket)? Unknown names report false.
    virtual bool profile_available(const std::string& name) const = 0;

    // Start the named profile bound to port 0; returns the bound port.
    // Throws std::runtime_error on unknown profiles or failed startup.
    virtual uint16_t start(const std::string& name) = 0;

    // Stop a previously started server. Idempotent.
    virtual void stop() = 0;
};

}  // namespace parity

#endif  // TEST_PARITY_SERVER_FIXTURE_HPP_
