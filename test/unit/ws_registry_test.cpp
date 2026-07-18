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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

// Unit tests for detail::ws_registry, constructed and driven directly
// (no running webserver/MHD daemon, no websocket upgrade). ws_registry
// was extracted from webserver_impl (commit b596fc8) and owns "the URL
// -> handler map + its mutex" -- previously untested at the class
// boundary; existing coverage only reached it indirectly through
// webserver::register_ws_resource / unregister_ws_resource (see
// webserver_register_ws_smartptr_test.cpp). Per the class comment,
// ws_registry never dereferences websocket_handler (only stores/erases/
// copies shared_ptrs), so it compiles and is fully testable even on
// HAVE_WEBSOCKET-off builds; these tests are unconditional.

#include <memory>
#include <string>

#include "./httpserver.hpp"
#include "./httpserver/detail/ws_registry.hpp"
#include "./littletest.hpp"

namespace ht = httpserver;
namespace htd = httpserver::detail;

namespace {

// Trivial websocket_handler subclass. on_message is the only pure
// virtual; the default hooks suffice since these tests never drive an
// actual session.
class noop_ws_handler : public ht::websocket_handler {
 public:
    void on_message(ht::websocket_session&, std::string_view) override {}
};

}  // namespace

LT_BEGIN_SUITE(ws_registry_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(ws_registry_suite)

LT_BEGIN_AUTO_TEST(ws_registry_suite, empty_registry_is_empty_and_finds_nothing)
    htd::ws_registry reg;
    LT_CHECK(reg.empty());
    LT_CHECK(reg.find("/ws") == nullptr);
LT_END_AUTO_TEST(empty_registry_is_empty_and_finds_nothing)

LT_BEGIN_AUTO_TEST(ws_registry_suite, try_register_inserts_and_find_returns_it)
    htd::ws_registry reg;
    auto handler = std::make_shared<noop_ws_handler>();

    LT_CHECK(reg.try_register("/ws", handler));

    LT_CHECK(!reg.empty());
    auto found = reg.find("/ws");
    LT_CHECK(found != nullptr);
    LT_CHECK(found == handler);
LT_END_AUTO_TEST(try_register_inserts_and_find_returns_it)

// try_register returns false (does not overwrite) when the key already
// has a handler -- the caller (webserver::register_ws_resource) surfaces
// this as a duplicate-registration throw.
LT_BEGIN_AUTO_TEST(ws_registry_suite, try_register_duplicate_key_returns_false)
    htd::ws_registry reg;
    auto first = std::make_shared<noop_ws_handler>();
    auto second = std::make_shared<noop_ws_handler>();

    LT_CHECK(reg.try_register("/dup", first));
    LT_CHECK(!reg.try_register("/dup", second));

    // The original registration must survive the rejected duplicate.
    LT_CHECK(reg.find("/dup") == first);
LT_END_AUTO_TEST(try_register_duplicate_key_returns_false)

LT_BEGIN_AUTO_TEST(ws_registry_suite, unregister_drops_handler_and_frees_slot)
    htd::ws_registry reg;
    auto handler = std::make_shared<noop_ws_handler>();
    reg.try_register("/ws", handler);

    reg.unregister("/ws");

    LT_CHECK(reg.empty());
    LT_CHECK(reg.find("/ws") == nullptr);
    // The slot is free again: re-registration must succeed.
    LT_CHECK(reg.try_register("/ws", handler));
LT_END_AUTO_TEST(unregister_drops_handler_and_frees_slot)

// unregister on an unknown key is a documented no-op (mirrors
// unregister_path / unregister_resource semantics).
LT_BEGIN_AUTO_TEST(ws_registry_suite, unregister_unknown_key_is_noop)
    htd::ws_registry reg;
    auto handler = std::make_shared<noop_ws_handler>();
    reg.try_register("/ws", handler);

    reg.unregister("/never-registered");

    LT_CHECK(!reg.empty());
    LT_CHECK(reg.find("/ws") == handler);
LT_END_AUTO_TEST(unregister_unknown_key_is_noop)

// find() returns a shared_ptr COPY -- the caller's handle keeps the
// handler alive independent of the registry slot. This is the
// documented rationale ("keeps the handler alive across an MHD upgrade
// even if a concurrent unregister races to drop the slot").
LT_BEGIN_AUTO_TEST(ws_registry_suite, find_result_survives_concurrent_unregister)
    htd::ws_registry reg;
    auto handler = std::make_shared<noop_ws_handler>();
    reg.try_register("/ws", handler);

    auto found = reg.find("/ws");
    LT_CHECK(found != nullptr);

    reg.unregister("/ws");

    // The copy returned by find() is unaffected by the registry drop.
    LT_CHECK(found != nullptr);
    LT_CHECK(reg.find("/ws") == nullptr);
LT_END_AUTO_TEST(find_result_survives_concurrent_unregister)

LT_BEGIN_AUTO_TEST(ws_registry_suite, multiple_distinct_keys_are_independent)
    htd::ws_registry reg;
    auto h1 = std::make_shared<noop_ws_handler>();
    auto h2 = std::make_shared<noop_ws_handler>();

    LT_CHECK(reg.try_register("/a", h1));
    LT_CHECK(reg.try_register("/b", h2));
    LT_CHECK(reg.find("/a") == h1);
    LT_CHECK(reg.find("/b") == h2);

    reg.unregister("/a");
    LT_CHECK(reg.find("/a") == nullptr);
    LT_CHECK(reg.find("/b") == h2);
    LT_CHECK(!reg.empty());
LT_END_AUTO_TEST(multiple_distinct_keys_are_independent)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
