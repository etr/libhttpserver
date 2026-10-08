/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <cstring>
#include <memory>
#include <vector>
#include <httpserver/detail/io_completion_storage.hpp>
#include <httpserver/detail/io_connection_owner.hpp>
#include "./littletest.hpp"

namespace hd = httpserver::detail;
namespace hh = httpserver::http;

LT_BEGIN_SUITE(completion_storage_suite)
    void set_up() { }
    void tear_down() { }
LT_END_SUITE(completion_storage_suite)

LT_BEGIN_AUTO_TEST(completion_storage_suite, successful_read_copies_only_transferred_bytes)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    std::byte destination[4]{};
    hd::read_operation op(owner, 1, destination);
    hd::owned_completion_storage storage(*op.state());
    std::memcpy(storage.data(), "abc", 3);
    LT_CHECK(storage.claim_read(*op.state(), 3));
    LT_CHECK_EQ(std::memcmp(destination, "abc", 3), 0);
    LT_CHECK(destination[3] == std::byte{});
    LT_CHECK(!storage.claim_read(*op.state(), 3));
LT_END_AUTO_TEST(successful_read_copies_only_transferred_bytes)

LT_BEGIN_AUTO_TEST(completion_storage_suite, cancellation_allows_borrowed_destination_destruction)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    auto destination = std::make_unique<std::byte[]>(16);
    hd::read_operation op(owner, 1, std::span<std::byte>(destination.get(), 16));
    hd::owned_completion_storage storage(*op.state());
    LT_CHECK(op.state()->claim_terminal());
    destination.reset();
    std::memcpy(storage.data(), "late", 4);
    LT_CHECK(!storage.claim_read(*op.state(), 4));
    LT_CHECK_EQ(storage.size(), std::size_t{16});
    LT_CHECK_EQ(std::memcmp(storage.data(), "late", 4), 0);
LT_END_AUTO_TEST(cancellation_allows_borrowed_destination_destruction)

LT_BEGIN_AUTO_TEST(completion_storage_suite, write_snapshot_survives_caller_destruction)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    auto source = std::make_unique<std::byte[]>(4);
    std::memcpy(source.get(), "send", 4);
    hd::write_operation op(owner, 1, std::span<const std::byte>(source.get(), 4));
    hd::owned_completion_storage storage(*op.state());
    source.reset();
    LT_CHECK(op.state()->claim_terminal());
    LT_CHECK_EQ(std::memcmp(storage.data(), "send", 4), 0);
LT_END_AUTO_TEST(write_snapshot_survives_caller_destruction)

LT_BEGIN_AUTO_TEST(completion_storage_suite, staging_bounds_native_length_and_empty_read_is_not_eof)
    httpserver::manual_executor ex;
    hd::io_connection_owner owner(ex);
    std::vector<std::byte> bytes(70000, std::byte{42});
    hd::write_operation op(owner, 1, bytes);
    hd::owned_completion_storage storage(*op.state());
    LT_CHECK_EQ(storage.size(), std::size_t{65536});
    LT_CHECK(storage.data()[storage.size() - 1] == std::byte{42});
    hd::read_operation empty(owner, 1, {});
    hd::owned_completion_storage zero(*empty.state());
    LT_CHECK_EQ(zero.size(), std::size_t{0});
    LT_CHECK(zero.claim_read(*empty.state(), 0));
LT_END_AUTO_TEST(staging_bounds_native_length_and_empty_read_is_not_eof)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
