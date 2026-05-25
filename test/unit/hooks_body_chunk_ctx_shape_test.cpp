/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
*/

// TASK-047: compile-time pin for body_chunk_ctx and request_received_ctx.
//
// TASK-045 landed both contexts; TASK-047 wires their firing sites. The
// firing sites depend on the exact field shapes (mutable http_request*,
// std::span<const std::byte> chunk, std::uint64_t offset, bool is_final
// for body_chunk; mutable http_request* + steady_clock::time_point for
// request_received). A future refactor that reshapes either struct must
// also update these static_asserts -- this gate documents the contract.

#include <chrono>
#include <cstdint>
#include <span>
#include <type_traits>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::body_chunk_ctx;
using httpserver::http_request;
using httpserver::request_received_ctx;

// ---- body_chunk_ctx ------------------------------------------------------
static_assert(std::is_same_v<decltype(body_chunk_ctx{}.request), http_request*>,
              "body_chunk_ctx::request must be http_request* (mutable)");
static_assert(std::is_same_v<decltype(body_chunk_ctx{}.chunk),
                             std::span<const std::byte>>,
              "body_chunk_ctx::chunk must be std::span<const std::byte>");
static_assert(std::is_same_v<decltype(body_chunk_ctx{}.offset), std::uint64_t>,
              "body_chunk_ctx::offset must be std::uint64_t");
static_assert(std::is_same_v<decltype(body_chunk_ctx{}.is_final), bool>,
              "body_chunk_ctx::is_final must be bool");

// Default values: nullptr request, empty chunk, zero offset, !is_final.
static_assert(body_chunk_ctx{}.request == nullptr,
              "body_chunk_ctx default request must be nullptr");
static_assert(body_chunk_ctx{}.chunk.empty(),
              "body_chunk_ctx default chunk must be empty");
static_assert(body_chunk_ctx{}.offset == 0U,
              "body_chunk_ctx default offset must be 0");
static_assert(body_chunk_ctx{}.is_final == false,
              "body_chunk_ctx default is_final must be false");

// ---- request_received_ctx ------------------------------------------------
static_assert(std::is_same_v<decltype(request_received_ctx{}.request),
                             http_request*>,
              "request_received_ctx::request must be http_request* (mutable)");
static_assert(std::is_same_v<decltype(request_received_ctx{}.received_at),
                             std::chrono::steady_clock::time_point>,
              "request_received_ctx::received_at must be steady_clock::time_point");
static_assert(request_received_ctx{}.request == nullptr,
              "request_received_ctx default request must be nullptr");

LT_BEGIN_SUITE(hooks_body_chunk_ctx_shape_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hooks_body_chunk_ctx_shape_suite)

LT_BEGIN_AUTO_TEST(hooks_body_chunk_ctx_shape_suite, aggregate_init_compiles)
    // The firing sites use aggregate-init for both contexts; pin that this
    // compiles with the current shapes.
    body_chunk_ctx ctx{
        /*request=*/nullptr,
        /*chunk=*/std::span<const std::byte>{},
        /*offset=*/static_cast<std::uint64_t>(42),
        /*is_final=*/true};
    LT_CHECK_EQ(ctx.request, static_cast<http_request*>(nullptr));
    LT_CHECK(ctx.chunk.empty());
    LT_CHECK_EQ(ctx.offset, static_cast<std::uint64_t>(42));
    LT_CHECK_EQ(ctx.is_final, true);

    request_received_ctx rctx{
        /*request=*/nullptr,
        /*received_at=*/std::chrono::steady_clock::now()};
    LT_CHECK_EQ(rctx.request, static_cast<http_request*>(nullptr));
LT_END_AUTO_TEST(aggregate_init_compiles)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
