/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-050: compile-time pin for request_completed_ctx.
//
// TASK-045 landed a placeholder shape {request, duration}; TASK-050 adds
// `resp` (nullable -- on early failure paths there may be no response object)
// and `succeeded` (mapped from MHD_RequestTerminationCode by the fire site).
// `duration` is kept for back-compat with the TASK-045 shape.
//
// Pin the shape so a future refactor that drops or reshapes a field breaks
// here.

#include <chrono>
#include <type_traits>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::http_request;
using httpserver::http_response;
using httpserver::request_completed_ctx;

// ---- request_completed_ctx ----------------------------------------------
static_assert(std::is_same_v<decltype(request_completed_ctx{}.request),
                             const http_request*>,
              "request_completed_ctx::request must be const http_request*");
static_assert(std::is_same_v<decltype(request_completed_ctx{}.resp),
                             const http_response*>,
              "request_completed_ctx::resp must be const http_response*");
static_assert(std::is_same_v<decltype(request_completed_ctx{}.succeeded), bool>,
              "request_completed_ctx::succeeded must be bool");
static_assert(std::is_same_v<decltype(request_completed_ctx{}.duration),
                             std::chrono::steady_clock::duration>,
              "request_completed_ctx::duration must be steady_clock::duration");

// Default values.
static_assert(request_completed_ctx{}.request == nullptr,
              "default request must be nullptr");
static_assert(request_completed_ctx{}.resp == nullptr,
              "default resp must be nullptr");
static_assert(request_completed_ctx{}.succeeded == false,
              "default succeeded must be false");

LT_BEGIN_SUITE(hooks_request_completed_ctx_shape_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_request_completed_ctx_shape_suite)

LT_BEGIN_AUTO_TEST(hooks_request_completed_ctx_shape_suite, aggregate_init_compiles)
    request_completed_ctx ctx{
        /*request=*/nullptr,
        /*resp=*/nullptr,
        /*succeeded=*/true,
        /*duration=*/std::chrono::steady_clock::duration{0}};
    LT_CHECK(ctx.succeeded);
    LT_CHECK_EQ(ctx.resp, static_cast<const http_response*>(nullptr));
LT_END_AUTO_TEST(aggregate_init_compiles)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
