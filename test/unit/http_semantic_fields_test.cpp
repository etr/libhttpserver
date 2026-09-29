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

// TASK-097 Step 3: insertion-ordered multivalue fields
// (httpserver::http::fields) — the REQ-017/018 acceptance matrix.
//
// Pins: append preserves prior values and received order; replace
// erases every entry for the name (any case) and appends one at the
// end of the sequence; first returns the earliest occurrence; all
// returns values in received order; lookup is case-insensitive while
// entries() exposes the first-seen spelling; remove reports whether
// anything was removed; copies are deep; entry-sequence equality.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <httpserver/http/fields.hpp>

#include "./littletest.hpp"

namespace {

// Builds the "name: value" projection of entries() for compact checks.
std::vector<std::string> entry_pairs(const httpserver::http::fields& f) {
    std::vector<std::string> out;
    for (const auto e : f.entries()) {
        out.push_back(std::string(e.name) + ": " + std::string(e.value));
    }
    return out;
}

bool same_strings(const std::span<const std::string> got,
                  const std::vector<std::string>& want) {
    if (got.size() != want.size()) return false;
    for (std::size_t i = 0; i < want.size(); ++i) {
        if (got[i] != want[i]) return false;
    }
    return true;
}

}  // namespace

LT_BEGIN_SUITE(http_semantic_fields_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(http_semantic_fields_suite)

LT_BEGIN_AUTO_TEST(http_semantic_fields_suite, append_preserves_wire_order)
    httpserver::http::fields f;
    f.append("set-cookie", "a=1");
    f.append("content-type", "text/plain");
    f.append("set-cookie", "b=2");
    f.append("x-trace", "t");
    f.append("set-cookie", "c=3");

    LT_CHECK_EQ(f.size(), std::size_t{5});
    LT_CHECK(!f.empty());

    const std::vector<std::string> want = {
        "set-cookie: a=1",
        "content-type: text/plain",
        "set-cookie: b=2",
        "x-trace: t",
        "set-cookie: c=3",
    };
    LT_CHECK(entry_pairs(f) == want);

    // all(): received order, ascending entries.
    LT_CHECK(same_strings(f.all("set-cookie"), {"a=1", "b=2", "c=3"}));
    // first(): earliest received occurrence.
    LT_CHECK(f.first("set-cookie") == std::optional(std::string_view{"a=1"}));
    // count().
    LT_CHECK_EQ(f.count("set-cookie"), std::size_t{3});
    LT_CHECK_EQ(f.count("content-type"), std::size_t{1});
LT_END_AUTO_TEST(append_preserves_wire_order)

LT_BEGIN_AUTO_TEST(http_semantic_fields_suite, replace_collapses_and_appends_at_end)
    httpserver::http::fields f;
    f.append("host", "example.com");
    f.append("connection", "keep-alive");
    f.append("accept", "text/html");
    f.append("connection", "upgrade");

    f.replace("connection", "close");

    const std::vector<std::string> want = {
        "host: example.com",
        "accept: text/html",
        "connection: close",
    };
    LT_CHECK(entry_pairs(f) == want);
    LT_CHECK(same_strings(f.all("connection"), {"close"}));
    LT_CHECK_EQ(f.count("connection"), std::size_t{1});
    LT_CHECK(f.first("connection") == std::optional(std::string_view{"close"}));
    // Other names' entries are preserved untouched.
    LT_CHECK_EQ(f.count("host"), std::size_t{1});
    LT_CHECK_EQ(f.count("accept"), std::size_t{1});
LT_END_AUTO_TEST(replace_collapses_and_appends_at_end)

LT_BEGIN_AUTO_TEST(http_semantic_fields_suite, replace_absent_name_appends)
    httpserver::http::fields f;
    f.append("host", "example.com");
    f.replace("x-new", "v");

    const std::vector<std::string> want = {
        "host: example.com",
        "x-new: v",
    };
    LT_CHECK(entry_pairs(f) == want);
LT_END_AUTO_TEST(replace_absent_name_appends)

LT_BEGIN_AUTO_TEST(http_semantic_fields_suite, lookup_is_case_insensitive_spelling_preserved)
    httpserver::http::fields f;
    f.append("Set-Cookie", "a=1");
    f.append("X-Trace", "t1");
    f.append("SET-COOKIE", "b=2");

    // First-seen spelling survives in entries().
    const std::vector<std::string> want = {
        "Set-Cookie: a=1",
        "X-Trace: t1",
        "Set-Cookie: b=2",
    };
    LT_CHECK(entry_pairs(f) == want);

    // Lookup in any case finds the same values.
    LT_CHECK(same_strings(f.all("set-cookie"), {"a=1", "b=2"}));
    LT_CHECK(same_strings(f.all("SET-COOKIE"), {"a=1", "b=2"}));
    LT_CHECK(same_strings(f.all("SeT-CoOkIe"), {"a=1", "b=2"}));
    LT_CHECK(f.first("set-cookie") == std::optional(std::string_view{"a=1"}));
    LT_CHECK_EQ(f.count("SET-cookie"), std::size_t{2});

    // replace through a different spelling still targets the same name.
    f.replace("set-COOKIE", "z=9");
    LT_CHECK(same_strings(f.all("SET-COOKIE"), {"z=9"}));
    // ... and the first-seen spelling is retained for the new entry.
    LT_CHECK(entry_pairs(f).back() == "Set-Cookie: z=9");
LT_END_AUTO_TEST(lookup_is_case_insensitive_spelling_preserved)

LT_BEGIN_AUTO_TEST(http_semantic_fields_suite, remove_reports_and_erases)
    httpserver::http::fields f;
    f.append("a", "1");
    f.append("b", "2");
    f.append("A", "3");

    LT_CHECK(f.remove("a"));
    LT_CHECK_EQ(f.count("a"), std::size_t{0});
    LT_CHECK(f.all("a").empty());
    LT_CHECK(!f.first("a").has_value());

    const std::vector<std::string> want = {"b: 2"};
    LT_CHECK(entry_pairs(f) == want);

    // Removing an absent name reports false.
    LT_CHECK(!f.remove("a"));
    LT_CHECK(!f.remove("never-there"));
LT_END_AUTO_TEST(remove_reports_and_erases)

LT_BEGIN_AUTO_TEST(http_semantic_fields_suite, absent_and_empty_lookups)
    const httpserver::http::fields f;
    LT_CHECK(f.empty());
    LT_CHECK_EQ(f.size(), std::size_t{0});
    LT_CHECK(f.entries().empty());
    LT_CHECK(f.all("x").empty());
    LT_CHECK(!f.first("x").has_value());
    LT_CHECK_EQ(f.count("x"), std::size_t{0});

    httpserver::http::fields g;
    g.append("h", "");
    LT_CHECK(g.first("h") == std::optional(std::string_view{""}));
    LT_CHECK_EQ(g.size(), std::size_t{1});
LT_END_AUTO_TEST(absent_and_empty_lookups)

LT_BEGIN_AUTO_TEST(http_semantic_fields_suite, copy_is_deep)
    httpserver::http::fields f;
    f.append("a", "1");
    f.append("b", "2");

    httpserver::http::fields copy = f;
    copy.append("a", "1b");
    copy.replace("b", "changed");
    copy.remove("a");

    // Source is untouched by mutations of the copy.
    const std::vector<std::string> want = {"a: 1", "b: 2"};
    LT_CHECK(entry_pairs(f) == want);
    LT_CHECK(same_strings(f.all("a"), {"1"}));
    LT_CHECK_EQ(f.count("a"), std::size_t{1});
    LT_CHECK_EQ(f.count("b"), std::size_t{1});

    // And the copy observes its own state.
    LT_CHECK_EQ(copy.count("a"), std::size_t{0});
    LT_CHECK(same_strings(copy.all("b"), {"changed"}));

    // Copy assignment too.
    httpserver::http::fields assigned;
    assigned = f;
    LT_CHECK(assigned == f);
    assigned.append("a", "2");
    LT_CHECK(assigned != f);
LT_END_AUTO_TEST(copy_is_deep)

LT_BEGIN_AUTO_TEST(http_semantic_fields_suite, entry_sequence_equality)
    httpserver::http::fields a;
    a.append("x", "1");
    a.append("y", "2");

    httpserver::http::fields b;
    b.append("x", "1");
    b.append("y", "2");
    LT_CHECK(a == b);

    b.append("y", "3");
    LT_CHECK(a != b);

    httpserver::http::fields c;
    c.append("y", "2");
    c.append("x", "1");
    LT_CHECK(a != c);  // order matters

    httpserver::http::fields d;
    d.append("x", "1");
    d.append("y", "2");
    d.append("z", "3");
    d.remove("z");
    LT_CHECK(a == d);  // remove then re-add yields the same sequence
LT_END_AUTO_TEST(entry_sequence_equality)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
