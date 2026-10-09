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
#include <limits>
#include <string>
#include <utility>
#include <httpserver/detail/qpack_dynamic_table.hpp>
#include "./littletest.hpp"
using httpserver::detail::qpack_dynamic_table;
using httpserver::detail::qpack_reference_hold;
using httpserver::detail::qpack_table_role;
using httpserver::detail::qpack_state;
using httpserver::detail::qpack_integer_maximum;
using httpserver::server::budget_limits;
using httpserver::server::resource_budget;
using httpserver::server::resource;
LT_BEGIN_SUITE(table_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(table_suite)
LT_BEGIN_AUTO_TEST(table_suite, admission_capacity_history_and_fifo)
    budget_limits limits;
    limits.set(resource::qpack_table_bytes, 100);
    auto root = resource_budget::root(limits);
    resource_budget child;
    LT_CHECK(root.child(limits, child).ok());
    {
        qpack_dynamic_table t(qpack_table_role::decoder);
        LT_CHECK(t.initialize(child, 100).ok());
        LT_CHECK(t.capacity() == 0 && root.in_use(resource::qpack_table_bytes) == 100);
        qpack_dynamic_table refused(qpack_table_role::decoder);
        LT_CHECK(refused.initialize(child, 1).state == qpack_state::limit_exceeded);
        LT_CHECK(child.in_use(resource::qpack_table_bytes) == 100);
        LT_CHECK(t.set_capacity(100).ok());
        LT_CHECK(t.insert("a", "b").ok() && t.insert("a", "b").ok());
        LT_CHECK(t.size() == 2 && t.bytes() == 68 && t.insert_count() == 2);
        LT_CHECK(t.find("a", "b") == 1 && t.encoder_relative(0)->absolute == 1);
        LT_CHECK(t.section_relative(1, 0)->absolute == 0 && t.post_base(0, 1)->absolute == 1);
        LT_CHECK(!t.section_relative(0, 0) && !t.post_base(qpack_integer_maximum, 1));
        LT_CHECK(!t.encoder_relative(2) && !t.lookup(2));
        LT_CHECK(t.duplicate(1).ok());
        LT_CHECK(!t.lookup(0) && t.lookup(2)->name == "a" && t.bytes() == 68);
        LT_CHECK(t.insert(std::string(100, 'x'), "").state == qpack_state::malformed);
        LT_CHECK(t.bytes() == 68 && t.insert_count() == 3);
        LT_CHECK(t.set_capacity(101).state == qpack_state::malformed);
        LT_CHECK(t.set_capacity(0).ok() && t.insert_count() == 3 && t.size() == 0);
        LT_CHECK(t.set_capacity(32).ok() && t.insert("", "").ok() && t.lookup(3));
        LT_CHECK(t.bytes() == 32);
    }
    LT_CHECK(root.in_use(resource::qpack_table_bytes) == 0 && child.in_use(resource::qpack_table_bytes) == 0);
    qpack_dynamic_table zero(qpack_table_role::decoder);
    LT_CHECK(zero.initialize({}, 0).ok() && zero.capacity() == 0);
    LT_CHECK(zero.initialize({}, 0).state == qpack_state::invalid_argument);
LT_END_AUTO_TEST(admission_capacity_history_and_fifo)
LT_BEGIN_AUTO_TEST(table_suite, borrowed_duplicate_survives_source_eviction)
    auto root = resource_budget::root({});
    qpack_dynamic_table t(qpack_table_role::decoder);
    LT_CHECK(t.initialize(root, 64).ok() && t.set_capacity(64).ok());
    const std::string octets("\0\xff", 2);
    LT_CHECK(t.insert(octets, "").ok());
    LT_CHECK(t.duplicate(0).ok());
    LT_CHECK(!t.lookup(0) && t.lookup(1)->name == octets);
    LT_CHECK(t.insert(t.lookup(1)->name, "v").ok());
    LT_CHECK(!t.lookup(1) && t.lookup(2)->name == octets);
LT_END_AUTO_TEST(borrowed_duplicate_survives_source_eviction)
LT_BEGIN_AUTO_TEST(table_suite, encoder_eviction_and_section_hold_lifetime)
    auto root = resource_budget::root({});
    qpack_dynamic_table t(qpack_table_role::encoder);
    LT_CHECK(t.initialize(root, 100).ok() && t.set_capacity(100).ok());
    LT_CHECK(t.insert("a", "b").ok() && t.insert("c", "d").ok());
    LT_CHECK(t.set_capacity(0).state == qpack_state::eviction_prohibited);
    LT_CHECK(t.advance_known_received(2).ok());
    LT_CHECK(t.advance_known_received(1).state == qpack_state::malformed);
    LT_CHECK(t.advance_known_received(3).state == qpack_state::malformed);
    LT_CHECK(t.increment_known_received(0).state == qpack_state::malformed);
    LT_CHECK(t.increment_known_received(1).state == qpack_state::malformed && t.known_received_count() == 2);
    qpack_reference_hold first, second;
    const std::array<std::uint64_t, 3> repeated{0, 0, 1};
    LT_CHECK(t.acquire(repeated, 3, first).ok());
    LT_CHECK(t.lookup(0)->references == 1 && t.lookup(1)->references == 1);
    const std::array<std::uint64_t, 1> one{0};
    LT_CHECK(t.acquire(one, 1, second).ok());
    LT_CHECK(t.insert("e", "f").state == qpack_state::eviction_prohibited);
    LT_CHECK(t.bytes() == 68 && t.insert_count() == 2 && t.capacity() == 100);
    LT_CHECK(t.set_capacity(34).state == qpack_state::eviction_prohibited);
    qpack_reference_hold moved(std::move(first));
    first.release(); second.release(); second.release();
    LT_CHECK(t.lookup(0)->references == 1);
    const std::array<std::uint64_t, 1> other{1};
    LT_CHECK(t.acquire(other, 1, second).ok());
    second = std::move(moved);
    LT_CHECK(t.lookup(1)->references == 1 && moved.size() == 0);
    second.release();
    moved.release();
    LT_CHECK(t.lookup(0)->references == 0 && t.known_received_count() == 2);
    const std::array<std::uint64_t, 2> invalid{0, 3};
    LT_CHECK(t.acquire(invalid, 2, first).state == qpack_state::malformed);
    LT_CHECK(t.lookup(0)->references == 0);
    LT_CHECK(t.acquire(one, 0, first).state == qpack_state::limit_exceeded);
    LT_CHECK(t.set_capacity(0).ok() && t.insert_count() == 2);
LT_END_AUTO_TEST(encoder_eviction_and_section_hold_lifetime)
LT_BEGIN_AUTO_TEST(table_suite, all_fifo_candidates_and_reference_overflow_are_atomic)
    auto budget = resource_budget::root({});
    qpack_dynamic_table t(qpack_table_role::encoder);
    LT_CHECK(t.initialize(budget, 100).ok() && t.set_capacity(100).ok());
    LT_CHECK(t.insert("a", "b").ok() && t.insert("c", "d").ok());
    LT_CHECK(t.advance_known_received(1).ok());
    LT_CHECK(t.set_capacity(0).state == qpack_state::eviction_prohibited);
    LT_CHECK(t.size() == 2 && t.bytes() == 68 && t.capacity() == 100);
    LT_CHECK(t.insert(std::string(40, 'x'), "").state == qpack_state::eviction_prohibited);
    LT_CHECK(t.lookup(0) && t.lookup(1) && t.insert_count() == 2);
    auto* entry = const_cast<httpserver::detail::qpack_dynamic_entry*>(t.lookup(1));
    entry->references = SIZE_MAX;
    qpack_reference_hold hold;
    const std::array<std::uint64_t, 2> indexes{0, 1};
    LT_CHECK(t.acquire(indexes, 2, hold).state == qpack_state::limit_exceeded);
    LT_CHECK(t.lookup(0)->references == 0 && t.lookup(1)->references == SIZE_MAX && hold.size() == 0);
    entry->references = 0;
    LT_CHECK(t.advance_known_received(2).ok());
    {
        qpack_reference_hold scope;
        LT_CHECK(t.acquire(indexes, 2, scope).ok());
    }
    LT_CHECK(t.lookup(0)->references == 0 && t.lookup(1)->references == 0 && t.set_capacity(0).ok());
LT_END_AUTO_TEST(all_fifo_candidates_and_reference_overflow_are_atomic)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
