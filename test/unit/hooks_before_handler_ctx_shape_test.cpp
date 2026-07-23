/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// Compile-time pin for before_handler_ctx and route_resolved_ctx.
//
// before_handler_ctx additionally carries
// the http_method + http_resource* surface the in-dispatch firing site
// exposes (the 405-alias body needs both). Pin the shape so a future
// refactor that drops or reshapes either field breaks here, where the
// contract is documented.

#include <optional>
#include <string_view>
#include <type_traits>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::before_handler_ctx;
using httpserver::http_method;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::method_set;
using httpserver::route_descriptor;
using httpserver::route_resolved_ctx;

// ---- before_handler_ctx --------------------------------------------------
static_assert(std::is_same_v<decltype(before_handler_ctx{}.request), http_request*>,
              "before_handler_ctx::request must be http_request* (mutable)");
static_assert(std::is_same_v<decltype(before_handler_ctx{}.matched),
                             std::optional<route_descriptor>>,
              "before_handler_ctx::matched must be std::optional<route_descriptor>");
static_assert(std::is_same_v<decltype(before_handler_ctx{}.method), http_method>,
              "before_handler_ctx::method must be http_method");
static_assert(std::is_same_v<decltype(before_handler_ctx{}.resource),
                             const http_resource*>,
              "before_handler_ctx::resource must be const http_resource*");

// Default values: nullptr request, no matched route, count_ method, nullptr resource.
static_assert(before_handler_ctx{}.request == nullptr,
              "before_handler_ctx default request must be nullptr");
static_assert(!before_handler_ctx{}.matched.has_value(),
              "before_handler_ctx default matched must be nullopt");
static_assert(before_handler_ctx{}.method == http_method::count_,
              "before_handler_ctx default method must be http_method::count_");
static_assert(before_handler_ctx{}.resource == nullptr,
              "before_handler_ctx default resource must be nullptr");

// ---- route_resolved_ctx --------------------------------------------------
static_assert(std::is_same_v<decltype(route_resolved_ctx{}.request),
                             const http_request*>,
              "route_resolved_ctx::request must be const http_request*");
static_assert(std::is_same_v<decltype(route_resolved_ctx{}.matched),
                             std::optional<route_descriptor>>,
              "route_resolved_ctx::matched must be std::optional<route_descriptor>");
static_assert(std::is_same_v<decltype(route_resolved_ctx{}.resource),
                             const http_resource*>,
              "route_resolved_ctx::resource must be const http_resource*");

// ---- route_descriptor ----------------------------------------------------
static_assert(std::is_same_v<decltype(route_descriptor{}.path_template),
                             std::string_view>,
              "route_descriptor::path_template must be std::string_view");
static_assert(std::is_same_v<decltype(route_descriptor{}.methods), method_set>,
              "route_descriptor::methods must be method_set");
static_assert(std::is_same_v<decltype(route_descriptor{}.is_prefix), bool>,
              "route_descriptor::is_prefix must be bool");

LT_BEGIN_SUITE(hooks_before_handler_ctx_shape_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_before_handler_ctx_shape_suite)

LT_BEGIN_AUTO_TEST(hooks_before_handler_ctx_shape_suite, aggregate_init_compiles)
    // The firing sites use aggregate-init for before_handler_ctx; pin
    // that this compiles with the current shape.
    before_handler_ctx ctx{
        /*request=*/nullptr,
        /*matched=*/std::nullopt,
        /*method=*/http_method::get,
        /*resource=*/nullptr};
    LT_CHECK_EQ(ctx.request, static_cast<http_request*>(nullptr));
    LT_CHECK(!ctx.matched.has_value());
    LT_CHECK_EQ(static_cast<int>(ctx.method), static_cast<int>(http_method::get));
    LT_CHECK_EQ(ctx.resource, static_cast<const http_resource*>(nullptr));

    route_resolved_ctx rctx{
        /*request=*/nullptr,
        /*matched=*/std::nullopt,
        /*resource=*/nullptr};
    LT_CHECK_EQ(rctx.request, static_cast<const http_request*>(nullptr));
LT_END_AUTO_TEST(aggregate_init_compiles)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
