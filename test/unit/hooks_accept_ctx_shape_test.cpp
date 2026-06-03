/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-046: compile-time pin for accept_ctx's extended shape.
//
// TASK-045 landed accept_ctx with only a `peer_address peer` member.
// TASK-046 extends it to carry the {accepted, reason} decision so the
// observation-only hook can render banned-IP log entries (issue #332).
//
// The fields are pinned by static_asserts so a future refactor that
// reshapes accept_ctx must update this gate explicitly.

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::accept_ctx;
using httpserver::peer_address;

static_assert(std::is_same_v<decltype(accept_ctx{}.peer), peer_address>,
              "accept_ctx::peer must remain peer_address");
static_assert(std::is_same_v<decltype(accept_ctx{}.accepted), bool>,
              "accept_ctx::accepted must be bool");
static_assert(std::is_same_v<decltype(accept_ctx{}.reason),
                             std::optional<std::string_view>>,
              "accept_ctx::reason must be std::optional<std::string_view>");

// Default-construction: accepted defaults to true and reason is empty.
static_assert(accept_ctx{}.accepted == true,
              "accept_ctx default `accepted` must be true");
static_assert(!accept_ctx{}.reason.has_value(),
              "accept_ctx default `reason` must be nullopt");

LT_BEGIN_SUITE(hooks_accept_ctx_shape_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_accept_ctx_shape_suite)

LT_BEGIN_AUTO_TEST(hooks_accept_ctx_shape_suite,
                   default_accepted_true_reason_nullopt_and_aggregate_init_with_banned)
    accept_ctx ctx{};
    LT_CHECK_EQ(ctx.accepted, true);
    LT_CHECK(!ctx.reason.has_value());
    // Aggregate-initialize with reason set, mirroring the firing-site code.
    accept_ctx ctx2{peer_address{}, false, std::string_view{"banned"}};
    LT_CHECK_EQ(ctx2.accepted, false);
    LT_CHECK(ctx2.reason.has_value());
    LT_CHECK_EQ(std::string(*ctx2.reason), std::string("banned"));
LT_END_AUTO_TEST(default_accepted_true_reason_nullopt_and_aggregate_init_with_banned)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
