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

// TASK-096: v2 (MHD-era) server_fixture implementation and the named
// profile registry. This is the ONLY parity-harness TU that knows the
// executing engine is v2/libmicrohttpd, and it contains zero assertions
// — profiles wire routes/hooks/auth, the runner and corpus own all
// expectations. Each profile serves a uniform GET /__smoke -> 200
// "smoke-ok" so the registry test can guard every profile against rot.

#ifndef TEST_PARITY_V2_FIXTURE_HPP_
#define TEST_PARITY_V2_FIXTURE_HPP_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "server_fixture.hpp"

namespace httpserver {
class webserver;
}

namespace parity {

class v2_fixture : public server_fixture {
 public:
    v2_fixture() = default;
    ~v2_fixture() override;
    v2_fixture(const v2_fixture&) = delete;
    v2_fixture& operator=(const v2_fixture&) = delete;

    // Registry keys, in inventory order (routing, hooks, basic auth,
    // digest auth, forms/multipart, file responses, IP controls,
    // SHOUTcast, websocket, TLS).
    static const std::vector<std::string>& profile_names();

    bool has_profile(const std::string& name) const override;
    bool profile_available(const std::string& name) const override;
    uint16_t start(const std::string& name) override;
    void stop() override;

 private:
    std::unique_ptr<httpserver::webserver> ws_;
};

}  // namespace parity

#endif  // TEST_PARITY_V2_FIXTURE_HPP_
