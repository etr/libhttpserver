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

// TASK-101 Step 3: hierarchical resource budgets (architecture §3.4).
// Admission refuses before capacity is committed; child construction is
// bounded by the parent; and every terminal path -- scope exit, explicit
// release, move, stack unwinding through an exception, and destruction
// of the budget handle itself -- releases exactly what was charged, at
// every level of the server > listener > connection > stream chain.

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <httpserver/server/budgets.hpp>

#include "./littletest.hpp"

namespace {

namespace srv = httpserver::server;
namespace http = httpserver::http;

using outcome_code = httpserver::http::outcome_code;

// --- helpers -----------------------------------------------------------------
// LT_CHECK expands harness-local identifiers, so the checks stay in the
// test bodies and these helpers only compute the verdict.

srv::resource_budget make_root(srv::resource kind, std::size_t capacity) {
    srv::budget_limits limits;
    limits.set(kind, capacity);
    return srv::resource_budget::root(limits);
}

bool reserves(const srv::resource_budget& budget, srv::resource kind,
              std::size_t units, srv::reservation& out) {
    return budget.reserve(kind, units, out).ok();
}

bool refuses_reserve(const srv::resource_budget& budget, srv::resource kind,
                     std::size_t units, srv::reservation& out) {
    const http::outcome result = budget.reserve(kind, units, out);
    return result.code() == outcome_code::limit_exceeded
        && !result.message().empty();
}

bool derive_child(const srv::resource_budget& parent,
                  const srv::budget_limits& limits,
                  srv::resource_budget& out) {
    return parent.child(limits, out).ok();
}

bool child_refused(const srv::resource_budget& parent,
                   const srv::budget_limits& limits,
                   srv::resource_budget& out) {
    const http::outcome result = parent.child(limits, out);
    return result.code() == outcome_code::invalid_argument
        && !result.message().empty();
}

// Reserves one unit and throws; unwinding must release it.
void reserve_then_throw(const srv::resource_budget& budget,
                        srv::resource kind) {
    srv::reservation guard;
    if (!budget.reserve(kind, 1, guard).ok()) {
        throw std::runtime_error("reserve refused before the throw");
    }
    throw std::runtime_error("planned failure after admission");
}

}  // namespace

LT_BEGIN_SUITE(server_budgets_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(server_budgets_suite)

// Root scope reports its configured capacities; an empty budget has
// none and refuses admission.
LT_BEGIN_AUTO_TEST(server_budgets_suite, root_and_empty_budget)
    const srv::resource kind = srv::resource::header_fields;
    const srv::resource_budget root = make_root(kind, 3);
    LT_CHECK(root.valid());
    LT_CHECK_EQ(root.capacity(kind), std::size_t{3});
    LT_CHECK_EQ(root.in_use(kind), std::size_t{0});

    const srv::resource_budget empty;
    LT_CHECK(!empty.valid());
    LT_CHECK_EQ(empty.capacity(kind), std::size_t{0});
    LT_CHECK_EQ(empty.in_use(kind), std::size_t{0});
    srv::reservation rejected;
    LT_CHECK(refuses_reserve(empty, kind, 1, rejected));
    LT_CHECK(!rejected.owns());
LT_END_AUTO_TEST(root_and_empty_budget)

// A child may carry capacities equal to its parent's; exceeding any
// parent capacity is refused and leaves the out parameter untouched.
LT_BEGIN_AUTO_TEST(server_budgets_suite, child_construction_bounds)
    const srv::resource kind = srv::resource::header_fields;
    const srv::resource_budget parent = make_root(kind, 4);

    srv::budget_limits equal;
    equal.set(kind, 4);
    srv::resource_budget child;
    LT_CHECK(derive_child(parent, equal, child));
    LT_CHECK(child.valid());
    LT_CHECK_EQ(child.capacity(kind), std::size_t{4});

    srv::budget_limits over;
    over.set(kind, 5);
    srv::resource_budget rejected;
    LT_CHECK(child_refused(parent, over, rejected));
    LT_CHECK(!rejected.valid());
LT_END_AUTO_TEST(child_construction_bounds)

// Reserve to exact capacity, refuse one more with accounting intact,
// and recover after release.
LT_BEGIN_AUTO_TEST(server_budgets_suite, reserve_to_capacity)
    const srv::resource kind = srv::resource::header_fields;
    const srv::resource_budget root = make_root(kind, 2);

    srv::reservation first;
    LT_CHECK(reserves(root, kind, 1, first));
    LT_CHECK(first.owns());
    LT_CHECK(first.kind() == kind);
    LT_CHECK_EQ(first.units(), std::size_t{1});
    LT_CHECK_EQ(root.in_use(kind), std::size_t{1});

    srv::reservation second;
    LT_CHECK(reserves(root, kind, 1, second));
    LT_CHECK_EQ(root.in_use(kind), std::size_t{2});

    srv::reservation refused;
    LT_CHECK(refuses_reserve(root, kind, 1, refused));
    LT_CHECK(!refused.owns());
    LT_CHECK_EQ(root.in_use(kind), std::size_t{2});

    // A failed admission leaves an out parameter holding a prior
    // reservation untouched.
    LT_CHECK(first.owns());
    LT_CHECK_EQ(first.units(), std::size_t{1});

    second.release();
    LT_CHECK_EQ(root.in_use(kind), std::size_t{1});
    srv::reservation again;
    LT_CHECK(reserves(root, kind, 1, again));
    LT_CHECK_EQ(root.in_use(kind), std::size_t{2});
LT_END_AUTO_TEST(reserve_to_capacity)

// Parent and child limits are enforced independently and in both
// directions.
LT_BEGIN_AUTO_TEST(server_budgets_suite, hierarchy_enforced_both_ways)
    const srv::resource kind = srv::resource::header_fields;
    const srv::resource_budget parent = make_root(kind, 4);
    srv::budget_limits limits;
    limits.set(kind, 2);
    srv::resource_budget child;
    LT_CHECK(derive_child(parent, limits, child));

    srv::reservation held;
    LT_CHECK(reserves(child, kind, 2, held));
    LT_CHECK_EQ(child.in_use(kind), std::size_t{2});
    LT_CHECK_EQ(parent.in_use(kind), std::size_t{2});

    // The child is full even though the parent has room.
    srv::reservation third;
    LT_CHECK(refuses_reserve(child, kind, 1, third));
    LT_CHECK_EQ(parent.in_use(kind), std::size_t{2});

    // Two child-held units make a parent-side reserve of three exceed
    // the parent capacity of four.
    srv::reservation parent_bulk;
    LT_CHECK(refuses_reserve(parent, kind, 3, parent_bulk));
    LT_CHECK_EQ(parent.in_use(kind), std::size_t{2});
    LT_CHECK_EQ(child.in_use(kind), std::size_t{2});
LT_END_AUTO_TEST(hierarchy_enforced_both_ways)

// A refusal at an ancestor rolls back the partial charge committed at
// the descendant: two sibling scopes under a parent of capacity one.
LT_BEGIN_AUTO_TEST(server_budgets_suite, refused_reserve_rolls_back)
    const srv::resource kind = srv::resource::routes;
    const srv::resource_budget parent = make_root(kind, 1);
    srv::budget_limits limits;
    limits.set(kind, 1);
    srv::resource_budget child_a;
    LT_CHECK(derive_child(parent, limits, child_a));
    srv::resource_budget child_b;
    LT_CHECK(derive_child(parent, limits, child_b));

    srv::reservation first;
    LT_CHECK(reserves(child_a, kind, 1, first));
    LT_CHECK_EQ(parent.in_use(kind), std::size_t{1});

    // child_b believes it has room and commits; the parent refuses the
    // second unit, and the rollback must restore child_b to zero.
    srv::reservation second;
    LT_CHECK(refuses_reserve(child_b, kind, 1, second));
    LT_CHECK(!second.owns());
    LT_CHECK_EQ(child_b.in_use(kind), std::size_t{0});
    LT_CHECK_EQ(child_a.in_use(kind), std::size_t{1});
    LT_CHECK_EQ(parent.in_use(kind), std::size_t{1});

    // The refused unit left the parent usable once child_a releases.
    first.release();
    LT_CHECK(reserves(child_b, kind, 1, second));
    LT_CHECK_EQ(parent.in_use(kind), std::size_t{1});
LT_END_AUTO_TEST(refused_reserve_rolls_back)

// Every level of a three-deep chain sees the charge, and one release
// restores all levels.
LT_BEGIN_AUTO_TEST(server_budgets_suite, grandchild_chain)
    const srv::resource kind = srv::resource::routes;
    const srv::resource_budget root = make_root(kind, 8);
    srv::budget_limits mid_limits;
    mid_limits.set(kind, 4);
    srv::resource_budget mid;
    LT_CHECK(derive_child(root, mid_limits, mid));
    srv::budget_limits leaf_limits;
    leaf_limits.set(kind, 2);
    srv::resource_budget leaf;
    LT_CHECK(derive_child(mid, leaf_limits, leaf));

    srv::reservation held;
    LT_CHECK(reserves(leaf, kind, 2, held));
    LT_CHECK_EQ(leaf.in_use(kind), std::size_t{2});
    LT_CHECK_EQ(mid.in_use(kind), std::size_t{2});
    LT_CHECK_EQ(root.in_use(kind), std::size_t{2});

    held.release();
    LT_CHECK_EQ(leaf.in_use(kind), std::size_t{0});
    LT_CHECK_EQ(mid.in_use(kind), std::size_t{0});
    LT_CHECK_EQ(root.in_use(kind), std::size_t{0});
LT_END_AUTO_TEST(grandchild_chain)

// Terminal path: scope exit destroys the reservation and releases the
// charge.
LT_BEGIN_AUTO_TEST(server_budgets_suite, destructor_releases)
    const srv::resource kind = srv::resource::timers;
    const srv::resource_budget root = make_root(kind, 1);
    {
        srv::reservation scoped;
        LT_CHECK(reserves(root, kind, 1, scoped));
        LT_CHECK_EQ(root.in_use(kind), std::size_t{1});
    }
    LT_CHECK_EQ(root.in_use(kind), std::size_t{0});
LT_END_AUTO_TEST(destructor_releases)

// Terminal path: explicit release followed by destruction is safe, and
// release is idempotent.
LT_BEGIN_AUTO_TEST(server_budgets_suite, explicit_then_destructor_release)
    const srv::resource kind = srv::resource::timers;
    const srv::resource_budget root = make_root(kind, 2);

    srv::reservation guard;
    LT_CHECK(reserves(root, kind, 1, guard));
    guard.release();
    guard.release();
    LT_CHECK(!guard.owns());
    LT_CHECK_EQ(root.in_use(kind), std::size_t{0});

    {
        srv::reservation already_released(std::move(guard));
        LT_CHECK(!already_released.owns());
    }
    LT_CHECK_EQ(root.in_use(kind), std::size_t{0});
LT_END_AUTO_TEST(explicit_then_destructor_release)

// Terminal path: moving transfers the charge exactly once.
LT_BEGIN_AUTO_TEST(server_budgets_suite, move_transfers_exactly_once)
    const srv::resource kind = srv::resource::timers;
    const srv::resource_budget root = make_root(kind, 3);

    srv::reservation original;
    LT_CHECK(reserves(root, kind, 1, original));
    srv::reservation moved = std::move(original);
    LT_CHECK(moved.owns());
    LT_CHECK(!original.owns());
    LT_CHECK_EQ(moved.units(), std::size_t{1});
    LT_CHECK_EQ(root.in_use(kind), std::size_t{1});
    // The moved-from handle must release nothing at destruction.
    { srv::reservation sink(std::move(original)); }
    LT_CHECK_EQ(root.in_use(kind), std::size_t{1});

    srv::reservation one;
    LT_CHECK(reserves(root, kind, 1, one));
    srv::reservation two;
    LT_CHECK(reserves(root, kind, 1, two));
    LT_CHECK_EQ(root.in_use(kind), std::size_t{3});
    // Move assignment releases the target's holding and takes over the
    // source's.
    two = std::move(one);
    LT_CHECK(two.owns());
    LT_CHECK_EQ(two.units(), std::size_t{1});
    LT_CHECK(!one.owns());
    LT_CHECK_EQ(root.in_use(kind), std::size_t{2});

    two.release();
    moved.release();
    LT_CHECK_EQ(root.in_use(kind), std::size_t{0});
LT_END_AUTO_TEST(move_transfers_exactly_once)

// Terminal path: stack unwinding through an exception releases the
// charge.
LT_BEGIN_AUTO_TEST(server_budgets_suite, exception_path_releases)
    const srv::resource kind = srv::resource::timers;
    const srv::resource_budget root = make_root(kind, 1);
    bool thrown = false;
    try {
        reserve_then_throw(root, kind);
        LT_CHECK(false);  // unreachable
    } catch (const std::runtime_error&) {
        thrown = true;
    }
    LT_CHECK(thrown);
    LT_CHECK_EQ(root.in_use(kind), std::size_t{0});
LT_END_AUTO_TEST(exception_path_releases)

// Terminal path: a reservation outlives the budget handle it was
// created from; the shared nodes keep the accounting correct.
LT_BEGIN_AUTO_TEST(server_budgets_suite, reservation_outlives_budget)
    const srv::resource kind = srv::resource::routes;
    srv::reservation held;
    srv::resource_budget child;
    {
        const srv::resource_budget root = make_root(kind, 2);
        srv::budget_limits limits;
        limits.set(kind, 2);
        LT_CHECK(derive_child(root, limits, child));
        // Charging at the child propagates to the root node of the
        // chain as well.
        LT_CHECK(reserves(child, kind, 2, held));
    }
    // root is gone; the child handle and the reservation keep the
    // chain's shared nodes alive.
    LT_CHECK(child.valid());
    LT_CHECK_EQ(child.capacity(kind), std::size_t{2});
    LT_CHECK_EQ(child.in_use(kind), std::size_t{2});
    held.release();
    LT_CHECK_EQ(child.in_use(kind), std::size_t{0});
LT_END_AUTO_TEST(reservation_outlives_budget)

// Zero units and out-of-range kinds are invalid arguments, not
// accounting events.
LT_BEGIN_AUTO_TEST(server_budgets_suite, degenerate_reserves_refused)
    const srv::resource kind = srv::resource::timers;
    const srv::resource_budget root = make_root(kind, 4);
    srv::reservation out;
    const http::outcome zero_units = root.reserve(kind, 0, out);
    LT_CHECK(zero_units.code() == outcome_code::invalid_argument);
    LT_CHECK(!out.owns());
    LT_CHECK_EQ(root.in_use(kind), std::size_t{0});
LT_END_AUTO_TEST(degenerate_reserves_refused)

// Concurrent reserve/release ends with every unit released.
LT_BEGIN_AUTO_TEST(server_budgets_suite, concurrent_hammer)
    const srv::resource kind = srv::resource::timers;
    const srv::resource_budget root = make_root(kind, 64);
    constexpr int kThreads = 4;
    constexpr int kIterations = 500;
    std::atomic<int> granted{0};
    std::atomic<int> refused{0};
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&root, &granted, &refused] {
            for (int i = 0; i < kIterations; ++i) {
                srv::reservation unit;
                if (root.reserve(srv::resource::timers, 1, unit).ok()) {
                    granted.fetch_add(1, std::memory_order_relaxed);
                    unit.release();
                } else {
                    refused.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    LT_CHECK(granted.load() > 0);
    LT_CHECK_EQ(granted.load() + refused.load(), kThreads * kIterations);
    LT_CHECK_EQ(root.in_use(kind), std::size_t{0});
LT_END_AUTO_TEST(concurrent_hammer)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
