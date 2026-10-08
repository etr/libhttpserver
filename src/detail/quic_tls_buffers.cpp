/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include "detail/quic_tls_callbacks.hpp"
namespace httpserver::detail {
namespace {
bool valid_level(quic_crypto_level level) { return static_cast<std::size_t>(level) < 3; }
std::span<const std::byte> own_cid(std::span<const std::byte> source, std::array<std::byte, 20>& target) {
    if (source.size() > target.size()) throw std::invalid_argument("QUIC TLS CID too long");
    std::copy(source.begin(), source.end(), target.begin());
    return std::span(target).first(source.size());
}
}  // namespace
quic_tls_callbacks::quic_tls_callbacks(const quic_tls_config& config, server::resource_budget budget, quic_key_state& keys)
    : output_capacity_(config.output_capacity), lease_capacity_(config.receive_lease_capacity),
      peer_capacity_(config.maximum_peer_parameters), local_size_(config.local_parameters.size()), keys_(keys),
      input_{quic_reassembly(config.input_limits, budget), quic_reassembly(config.input_limits, budget), quic_reassembly(config.input_limits, budget)} {
    const auto ceiling = server::max_capacity(server::resource::quic_reassembly_bytes);
    for (auto size : {output_capacity_, lease_capacity_, peer_capacity_, local_size_}) {
        if (size > ceiling) throw std::invalid_argument("QUIC TLS storage too large");
    }
    if (!lease_capacity_) throw std::invalid_argument("QUIC TLS receive lease empty");
    const auto capacity = 3 * output_capacity_ + lease_capacity_ + peer_capacity_ + local_size_;
    if (!budget.reserve(server::resource::quic_reassembly_bytes, capacity, storage_).ok()) throw std::bad_alloc();
    for (auto& stream : output_) stream.bytes = std::make_unique<std::byte[]>(output_capacity_);
    lease_ = std::make_unique<std::byte[]>(lease_capacity_);
    local_ = std::make_unique<std::byte[]>(local_size_);
    peer_ = std::make_unique<std::byte[]>(peer_capacity_);
    std::copy(config.local_parameters.begin(), config.local_parameters.end(), local_.get());
    peer_cids_.initial_source = own_cid(config.peer_cids.initial_source, initial_cid_);
    peer_cids_.original_destination = own_cid(config.peer_cids.original_destination, original_cid_);
    if (config.peer_cids.retry_source) peer_cids_.retry_source = own_cid(*config.peer_cids.retry_source, retry_cid_);
}
quic_tls_result quic_tls_callbacks::receive(quic_crypto_level level, std::uint64_t offset, std::span<const std::byte> bytes) {
    if (failure_.code != quic_tls_code::ok) return {failure_.code};
    if (!valid_level(level)) return {quic_tls_code::invalid_level};
    const auto result = input_[static_cast<std::size_t>(level)].insert(offset, bytes);
    if (!result) return {result.code == quic_stream_code::no_memory ? quic_tls_code::no_memory : quic_tls_code::input_limit, 0, result.code};
    return {};
}
quic_tls_result quic_tls_callbacks::copy_output(quic_crypto_level level, std::uint64_t offset, std::span<std::byte> destination) const {
    if (!valid_level(level)) return {quic_tls_code::invalid_level};
    const auto& stream = output_[static_cast<std::size_t>(level)];
    if (offset < stream.begin || offset - stream.begin > stream.size) return {quic_tls_code::invalid_offset};
    auto start = static_cast<std::size_t>(offset - stream.begin);
    auto count = std::min(destination.size(), stream.size - start);
    std::copy_n(stream.bytes.get() + start, count, destination.begin());
    return {quic_tls_code::ok, count};
}
quic_tls_result quic_tls_callbacks::retire_output_prefix(quic_crypto_level level, std::uint64_t through) {
    if (!valid_level(level)) return {quic_tls_code::invalid_level};
    auto& stream = output_[static_cast<std::size_t>(level)];
    if (through < stream.begin || through - stream.begin > stream.size) return {quic_tls_code::invalid_offset};
    const auto retired = static_cast<std::size_t>(through - stream.begin);
    stream.size -= retired;
    if (stream.size) std::memmove(stream.bytes.get(), stream.bytes.get() + retired, stream.size);
    stream.begin = through;
    return {};
}
int quic_tls_callbacks::send(std::span<const std::byte> bytes, std::size_t* consumed) noexcept {
    *consumed = 0;
    if (failure_.code != quic_tls_code::ok) return 0;
    auto& stream = output_[static_cast<std::size_t>(write_level_)];
    const auto count = std::min(bytes.size(), output_capacity_ - stream.size);
    if (count > k_quic_max_integer - stream.begin - stream.size) return fail(quic_tls_code::invalid_offset);
    std::copy_n(bytes.begin(), count, stream.bytes.get() + stream.size);
    stream.size += count;
    *consumed = count;
    return 1;
}
int quic_tls_callbacks::recv(const unsigned char** buffer, std::size_t* size) noexcept {
    *buffer = nullptr;
    *size = 0;
    if (failure_.code != quic_tls_code::ok) return 0;
    if (leased_size_) return fail(quic_tls_code::callback_error);
    leased_level_ = read_level_;
    leased_size_ = input_[static_cast<std::size_t>(leased_level_)].read({lease_.get(), lease_capacity_});
    *buffer = reinterpret_cast<const unsigned char*>(lease_.get());
    *size = leased_size_;
    return 1;
}
int quic_tls_callbacks::release(std::size_t size) noexcept {
    if (size != leased_size_) return fail(quic_tls_code::callback_error);
    // read() consumed the saved level before leasing; never touch the new level.
    leased_size_ = 0;
    return 1;
}
int quic_tls_callbacks::fail(quic_tls_code code) noexcept {
    if (failure_.code == quic_tls_code::ok) failure_.code = code;
    return 0;
}
int quic_tls_callbacks::alert(unsigned char code) noexcept {
    failure_.alert = code;
    return fail(quic_tls_code::alert);
}
}  // namespace httpserver::detail
