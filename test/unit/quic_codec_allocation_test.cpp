/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif
#include <httpserver/detail/quic_frame.hpp>
#include <httpserver/detail/quic_transport_parameters.hpp>
#include <httpserver/detail/quic_varint.hpp>
#include "./quic_codec_test_support.hpp"
namespace {
thread_local bool counting = false, fail_allocation = false;
thread_local std::size_t allocation_count = 0;
void observed_allocation() {
    if (!counting) return;
    ++allocation_count;
    if (fail_allocation) std::abort();
}
void free_aligned(void* value) noexcept {
#ifdef _WIN32
    _aligned_free(value);
#else
    std::free(value);
#endif
}
struct allocation_scope {
    explicit allocation_scope(bool fail) {
        allocation_count = 0;
        fail_allocation = fail;
        counting = true;
    }
    ~allocation_scope() {
        counting = false;
        fail_allocation = false;
    }
};
}  // namespace
void* operator new(std::size_t size) {
    observed_allocation();
    if (auto* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
    return ::operator new(size);
}
void operator delete(void* value) noexcept {
    std::free(value);
}
void operator delete[](void* value) noexcept {
    std::free(value);
}
void operator delete(void* value, std::size_t) noexcept {
    std::free(value);
}
void operator delete[](void* value, std::size_t) noexcept {
    std::free(value);
}
void* operator new(std::size_t size, std::align_val_t alignment) {
    observed_allocation();
    const auto a = static_cast<std::size_t>(alignment);
    if (size > static_cast<std::size_t>(-1) - a) throw std::bad_alloc();
    const auto rounded = ((size ? size : 1) + a - 1) / a * a;
#ifdef _WIN32
    if (auto* result = _aligned_malloc(rounded, a)) return result;
#else
    if (auto* result = std::aligned_alloc(a, rounded)) return result;
#endif
    throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}
void operator delete(void* value, std::align_val_t) noexcept {
    free_aligned(value);
}
void operator delete[](void* value, std::align_val_t) noexcept {
    free_aligned(value);
}
void operator delete(void* value, std::size_t, std::align_val_t) noexcept {
    free_aligned(value);
}
void operator delete[](void* value, std::size_t, std::align_val_t) noexcept {
    free_aligned(value);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
    return ::operator new(size, tag);
}
void* operator new(std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size, a);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, std::align_val_t a, const std::nothrow_t& tag) noexcept {
    return ::operator new(size, a, tag);
}
void operator delete(void* p, const std::nothrow_t&) noexcept {
    std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
    std::free(p);
}
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    free_aligned(p);
}
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    free_aligned(p);
}
LT_BEGIN_SUITE(allocation_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(allocation_suite)
LT_BEGIN_AUTO_TEST(allocation_suite, counter_observes_ordinary_and_aligned_entry_points)
    constexpr std::size_t expected = 2;
    {
        allocation_scope scope(false);
        auto* ordinary = ::operator new(1);
        auto* aligned = ::operator new(1, std::align_val_t{64});
        ::operator delete(ordinary);
        ::operator delete(aligned, std::align_val_t{64});
    }
    LT_CHECK_EQ(allocation_count, expected);
LT_END_AUTO_TEST(counter_observes_ordinary_and_aligned_entry_points)
LT_BEGIN_AUTO_TEST(allocation_suite, codecs_borrow_large_payloads_and_reject_huge_claims_without_allocating)
    auto stream = octets({14, 0, 0, 0x80, 0, 0xff, 0xdc});
    stream.resize(65507, std::byte{0xab});
    auto crypto = stream;
    crypto[0] = std::byte{6};
    crypto.erase(crypto.begin() + 1);
    auto packet = octets({0xc0, 0, 0, 0, 1, 0, 0, 0, 18, 0, 1});
    packet.resize(27);
    auto parameters = octets({15, 1, 0xaa, 3, 2, 0x44, 0xb0});
    auto huge_length = octets({6, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    auto huge_ack = octets({2, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0});
    auto huge_parameter = huge_length;
    huge_parameter[0] = std::byte{27};
    huge_parameter.erase(huge_parameter.begin() + 1);
    auto huge_packet = octets({0xc0, 0, 0, 0, 1, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    auto duplicate_parameters = octets({27, 0, 27, 0});
    std::array<std::byte, 65535> output{};
    bool okay = true;
    {
        allocation_scope scope(true);
        auto p = hd::parse_quic_envelope(packet);
        okay &= p.code == hd::quic_codec_code::ok;
        auto h = hd::decode_quic_unprotected_header(p.value, {0xc0, std::span(packet).subspan(9, 1)}, {});
        okay &= h.code == hd::quic_codec_code::ok;
        hd::quic_packet_write write;
        write.payload = std::span(packet).subspan(10, 1);
        write.tag = std::span(packet).last(16);
        okay &= hd::encode_quic_packet(write, output).code == hd::quic_codec_code::ok;
        for (const auto& input : {std::span<const std::byte>(stream), std::span<const std::byte>(crypto)}) {
            hd::quic_frame_cursor c;
            auto f = hd::next_quic_frame(input, c);
            okay &= f.code == hd::quic_codec_code::ok;
            okay &= hd::encode_quic_frame(f.value, output).code == hd::quic_codec_code::ok;
            if (auto* s = std::get_if<hd::quic_stream_frame>(&f.value)) okay &= s->data.data() == stream.data() + 7 && s->data.size() == 65500;
            if (auto* s = std::get_if<hd::quic_crypto_frame>(&f.value)) okay &= s->data.data() == crypto.data() + 6 && s->data.size() == 65500;
        }
        auto t = hd::decode_quic_transport_parameters(parameters, hd::quic_endpoint_role::client);
        okay &= t.code == hd::quic_codec_code::ok;
        okay &= hd::encode_quic_transport_parameters(t.value, hd::quic_endpoint_role::client, output).code == hd::quic_codec_code::ok;
        hd::quic_frame_cursor c;
        okay &= hd::next_quic_frame(huge_length, c).code == hd::quic_codec_code::truncated;
        okay &= hd::next_quic_frame(huge_ack, c).code == hd::quic_codec_code::limit_exceeded;
        okay &= hd::decode_quic_transport_parameters(huge_parameter, hd::quic_endpoint_role::client).code == hd::quic_codec_code::truncated;
        okay &= hd::parse_quic_envelope(huge_packet).code == hd::quic_codec_code::truncated;
        okay &= hd::decode_quic_transport_parameters(duplicate_parameters, hd::quic_endpoint_role::client).code == hd::quic_codec_code::malformed;
        okay &= hd::decode_quic_varint(huge_parameter).code == hd::quic_codec_code::ok;
        okay &= hd::reconstruct_quic_packet_number(1, 1, {}).code == hd::quic_codec_code::ok;
    }
    LT_CHECK_EQ(allocation_count, std::size_t{0});
    LT_CHECK(okay);
LT_END_AUTO_TEST(codecs_borrow_large_payloads_and_reject_huge_claims_without_allocating)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
