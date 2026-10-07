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

#include <array>
#include <cstdint>
#include <string_view>

#include <httpserver/detail/hpack_static_table.hpp>
#include <httpserver/detail/hpack_huffman_table.hpp>
#include "./littletest.hpp"

using httpserver::detail::hpack_huffman_codes;
using httpserver::detail::hpack_static_find;
using httpserver::detail::hpack_static_find_name;
using httpserver::detail::hpack_static_lookup;
LT_BEGIN_SUITE(hpack_tables_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hpack_tables_suite)
LT_BEGIN_AUTO_TEST(hpack_tables_suite, appendix_a_exact_entries)
    // Independent Appendix A fixture, deliberately separate from production data.
    constexpr std::array<std::string_view, 61> names = {
        ":authority", ":method", ":method", ":path", ":path", ":scheme", ":scheme",
        ":status", ":status", ":status", ":status", ":status", ":status", ":status",
        "accept-charset", "accept-encoding", "accept-language", "accept-ranges", "accept",
        "access-control-allow-origin", "age", "allow", "authorization", "cache-control",
        "content-disposition", "content-encoding", "content-language", "content-length",
        "content-location", "content-range", "content-type", "cookie", "date", "etag", "expect",
        "expires", "from", "host", "if-match", "if-modified-since", "if-none-match", "if-range",
        "if-unmodified-since", "last-modified", "link", "location", "max-forwards",
        "proxy-authenticate", "proxy-authorization", "range", "referer", "refresh", "retry-after",
        "server", "set-cookie", "strict-transport-security", "transfer-encoding", "user-agent",
        "vary", "via", "www-authenticate"
    };
    constexpr std::array<std::string_view, 14> values = {
        "", "GET", "POST", "/", "/index.html", "http", "https", "200", "204", "206", "304", "400", "404", "500"
    };
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto* entry = hpack_static_lookup(i + 1);
        LT_CHECK(entry != nullptr);
        LT_CHECK(entry->name == names[i]);
        const auto value = i < 14 ? values[i] : (i == 15 ? "gzip, deflate" : "");
        LT_CHECK(entry->value == value);
        LT_CHECK(hpack_static_find(entry->name, entry->value) == i + 1);
        LT_CHECK(hpack_static_lookup(i + 1) == entry);
    }
    LT_CHECK(hpack_static_lookup(0) == nullptr);
    LT_CHECK(hpack_static_lookup(62) == nullptr);
    LT_CHECK(hpack_static_lookup(UINT64_MAX) == nullptr);
    LT_CHECK(hpack_static_find_name(":method") == 2);
    LT_CHECK(hpack_static_find_name(":status") == 8);
    LT_CHECK(hpack_static_find_name("Accept") == 0);
    LT_CHECK(hpack_static_find_name("unknown") == 0);
    LT_CHECK(hpack_static_find(":method", "get") == 0);
    LT_CHECK(hpack_static_find("cookie", "abc") == 0);
LT_END_AUTO_TEST(appendix_a_exact_entries)
LT_BEGIN_AUTO_TEST(hpack_tables_suite, independent_huffman_code_fixtures)
    LT_CHECK(hpack_huffman_codes.size() == 257);
    LT_CHECK(hpack_huffman_codes[0].code == 0x1ff8 && hpack_huffman_codes[0].length == 13);
    LT_CHECK(hpack_huffman_codes[32].code == 0x14 && hpack_huffman_codes[32].length == 6);
    LT_CHECK(hpack_huffman_codes[48].code == 0 && hpack_huffman_codes[48].length == 5);
    LT_CHECK(hpack_huffman_codes[97].code == 3 && hpack_huffman_codes[97].length == 5);
    LT_CHECK(hpack_huffman_codes[255].code == 0x3ffffee && hpack_huffman_codes[255].length == 26);
    LT_CHECK(hpack_huffman_codes[256].code == 0x3fffffff && hpack_huffman_codes[256].length == 30);
LT_END_AUTO_TEST(independent_huffman_code_fixtures)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
