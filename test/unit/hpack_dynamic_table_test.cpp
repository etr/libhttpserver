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

#include <cstddef>
#include <cstdint>
#include <string>
#include <httpserver/detail/hpack_dynamic_table.hpp>
#include "./littletest.hpp"

namespace {
namespace server = httpserver::server;
using httpserver::detail::hpack_dynamic_table;
using httpserver::detail::hpack_entry_size;
using httpserver::detail::hpack_state;
server::resource_budget budget(std::size_t cap = 4096) {
    server::budget_limits limits;
    limits.set(server::resource::hpack_table_bytes, cap);
    return server::resource_budget::root(limits);
}
}  // namespace
LT_BEGIN_SUITE(hpack_dynamic_table_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hpack_dynamic_table_suite)
LT_BEGIN_AUTO_TEST(hpack_dynamic_table_suite, indexing_accounting_and_oldest_eviction)
    auto scope = budget();
    hpack_dynamic_table table(scope, 70);
    LT_CHECK(!table.lookup(0));
    LT_CHECK(table.lookup(61)->name == "www-authenticate");
    LT_CHECK(!table.lookup(62));
    LT_CHECK(table.insert("a", "bb").ok());
    LT_CHECK(table.insert("a", "bb").ok());
    LT_CHECK(table.bytes() == 70 && table.size() == 2);
    LT_CHECK(scope.in_use(server::resource::hpack_table_bytes) == 70);
    LT_CHECK(table.insert("c", "ddd").ok());
    LT_CHECK(table.size() == 1 && table.bytes() == 36);
    LT_CHECK(table.lookup(62)->name == "c" && !table.lookup(63));
    table.set_capacity(0);
    LT_CHECK(table.size() == 0 && scope.in_use(server::resource::hpack_table_bytes) == 0);
    table.set_capacity(70);
    LT_CHECK(table.insert("oversize", std::string(64, 'x')).ok());
    LT_CHECK(table.size() == 0);
LT_END_AUTO_TEST(indexing_accounting_and_oldest_eviction)
LT_BEGIN_AUTO_TEST(hpack_dynamic_table_suite, owned_name_shrink_grow_and_hierarchical_refusal)
    auto root = budget(70);
    server::budget_limits limits;
    limits.set(server::resource::hpack_table_bytes, 70);
    server::resource_budget child;
    LT_CHECK(root.child(limits, child).ok());
    {
        hpack_dynamic_table table(child, 70);
        LT_CHECK(table.insert("a", "b").ok());
        const auto source = table.lookup(62);
        LT_CHECK(table.insert(std::string(source->name), std::string(37, 'x')).ok());
        LT_CHECK(table.bytes() == 70 && table.lookup(62)->name == "a");
        table.set_capacity(69);
        LT_CHECK(table.size() == 0);
        table.set_capacity(70);
        LT_CHECK(!table.lookup(62));
        LT_CHECK(table.insert("a", "b").ok());
        server::reservation other;
        LT_CHECK(root.reserve(server::resource::hpack_table_bytes, 36, other).ok());
        LT_CHECK(table.insert("c", "d").state == hpack_state::limit_exceeded);
        LT_CHECK(table.bytes() == 34 && child.in_use(server::resource::hpack_table_bytes) == 34);
    }
    LT_CHECK(root.in_use(server::resource::hpack_table_bytes) == 0);
    std::size_t bytes = 0;
    LT_CHECK(!hpack_entry_size(SIZE_MAX, 1, bytes));
    LT_CHECK(!hpack_entry_size(1, SIZE_MAX, bytes));
    LT_CHECK(hpack_entry_size(1, 2, bytes) && bytes == 35);
LT_END_AUTO_TEST(owned_name_shrink_grow_and_hierarchical_refusal)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
