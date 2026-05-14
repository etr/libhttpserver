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

// TASK-034 cycle D: on a HAVE_WEBSOCKET-off build,
// webserver::register_ws_resource(...) must throw feature_unavailable
// whose what() names both "websocket" and "HAVE_WEBSOCKET" (PRD-FLG-REQ-002
// and the TASK-003 invariant).
//
// On a HAVE_WEBSOCKET-on build there is no contract to pin from this TU
// (the integ-level ws_start_stop test already exercises the success path),
// so the suite is left empty there: the binary still exists, runs, exits
// 0, and contributes to the make-check totals in both configurations.

#include <string>

#include "./httpserver.hpp"
#include "./littletest.hpp"

LT_BEGIN_SUITE(webserver_ws_unavailable_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(webserver_ws_unavailable_suite)

#ifndef HAVE_WEBSOCKET
LT_BEGIN_AUTO_TEST(webserver_ws_unavailable_suite,
                   register_ws_resource_throws_feature_unavailable)
    httpserver::webserver ws{httpserver::create_webserver(8090)};
    bool caught = false;
    std::string msg;
    try {
        ws.register_ws_resource("/ws", nullptr);
    } catch (const httpserver::feature_unavailable& e) {
        caught = true;
        msg = e.what();
    }
    LT_CHECK(caught);
    LT_CHECK(msg.find("websocket") != std::string::npos);
    LT_CHECK(msg.find("HAVE_WEBSOCKET") != std::string::npos);
LT_END_AUTO_TEST(register_ws_resource_throws_feature_unavailable)
#endif  // !HAVE_WEBSOCKET

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
