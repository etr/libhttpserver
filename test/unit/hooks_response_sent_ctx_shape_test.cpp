/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// Compile-time pin for response_sent_ctx.
//
// The hook bus skeleton landed a placeholder shape {request, response,
// sent_at}; sent_at was replaced with the three fields callers actually
// need: integer status, byte count queued to MHD, and the steady_clock
// elapsed measured from the first invocation of answer_to_connection for
// the request.
//
// Pin the shape so a future refactor that drops or reshapes a field breaks
// here, where the data access-logging consumers ask for is documented.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::http_request;
using httpserver::http_response;
using httpserver::response_sent_ctx;

// ---- response_sent_ctx ---------------------------------------------------
static_assert(std::is_same_v<decltype(response_sent_ctx{}.request),
                             const http_request*>,
              "response_sent_ctx::request must be const http_request*");
static_assert(std::is_same_v<decltype(response_sent_ctx{}.response),
                             const http_response*>,
              "response_sent_ctx::response must be const http_response*");
static_assert(std::is_same_v<decltype(response_sent_ctx{}.status), int>,
              "response_sent_ctx::status must be int");
static_assert(std::is_same_v<decltype(response_sent_ctx{}.bytes_queued),
                             std::size_t>,
              "response_sent_ctx::bytes_queued must be std::size_t");
static_assert(std::is_same_v<decltype(response_sent_ctx{}.elapsed),
                             std::chrono::nanoseconds>,
              "response_sent_ctx::elapsed must be std::chrono::nanoseconds");

// Default values.
static_assert(response_sent_ctx{}.request == nullptr,
              "default request must be nullptr");
static_assert(response_sent_ctx{}.response == nullptr,
              "default response must be nullptr");
static_assert(response_sent_ctx{}.status == 0,
              "default status must be 0");
static_assert(response_sent_ctx{}.bytes_queued == 0,
              "default bytes_queued must be 0");
static_assert(response_sent_ctx{}.elapsed == std::chrono::nanoseconds::zero(),
              "default elapsed must be zero");

LT_BEGIN_SUITE(hooks_response_sent_ctx_shape_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_response_sent_ctx_shape_suite)

LT_BEGIN_AUTO_TEST(hooks_response_sent_ctx_shape_suite, aggregate_init_compiles)
    response_sent_ctx ctx{
        /*request=*/nullptr,
        /*response=*/nullptr,
        /*status=*/200,
        /*bytes_queued=*/std::size_t{42},
        /*elapsed=*/std::chrono::nanoseconds{1234}};
    LT_CHECK_EQ(ctx.status, 200);
    LT_CHECK_EQ(ctx.bytes_queued, static_cast<std::size_t>(42));
    LT_CHECK_EQ(ctx.elapsed.count(), static_cast<int64_t>(1234));
LT_END_AUTO_TEST(aggregate_init_compiles)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
