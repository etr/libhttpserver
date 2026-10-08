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

#include <cstdint>
#include <string_view>
#include <httpserver/detail/qpack_static_table.hpp>
#include "data/qpack/rfc9204.hpp"
#include "./littletest.hpp"
using httpserver::detail::qpack_static_entries;
using httpserver::detail::qpack_static_find;
using httpserver::detail::qpack_static_find_name;
using httpserver::detail::qpack_static_lookup;
LT_BEGIN_SUITE(qpack_static_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(qpack_static_suite)
LT_BEGIN_AUTO_TEST(qpack_static_suite, appendix_a_and_zero_based_search)
    LT_CHECK(qpack_static_entries.size() == 99);
    for (std::size_t i = 0; i < qpack_fixture::entries.size(); ++i) {
        const auto* entry = qpack_static_lookup(i);
        LT_CHECK(entry != nullptr);
        LT_CHECK(entry->name == qpack_fixture::entries[i].name);
        LT_CHECK(entry->value == qpack_fixture::entries[i].value);
        LT_CHECK(qpack_static_find(entry->name, entry->value) == i);
        LT_CHECK(qpack_static_lookup(i) == entry);
    }
    LT_CHECK(qpack_static_lookup(99) == nullptr);
    LT_CHECK(qpack_static_lookup(UINT64_MAX) == nullptr);
    LT_CHECK(qpack_static_find(":authority", "") == 0);
    LT_CHECK(qpack_static_find_name(":authority") == 0);
    LT_CHECK(qpack_static_find_name(":method") == 15);
    LT_CHECK(!qpack_static_find_name("Cookie"));
    LT_CHECK(!qpack_static_find_name("unknown"));
    LT_CHECK(!qpack_static_find(":method", "get"));
LT_END_AUTO_TEST(appendix_a_and_zero_based_search)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
