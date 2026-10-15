/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <string>
#include <utility>

#include <httpserver/detail/hpack_decoder.hpp>

namespace httpserver::detail {

auto hpack_decoder::acknowledge_maximum(std::uint64_t maximum) noexcept -> hpack_status {
    if (maximum > UINT32_MAX) return {hpack_state::invalid_argument};
    if (!usable_) return {hpack_state::malformed};
    maximum_ = static_cast<std::size_t>(maximum);
    if (maximum_ < table_.capacity()) required_minimum_ = std::min(required_minimum_.value_or(maximum_), maximum_);
    return {};
}

auto hpack_decoder::decode(std::span<const std::uint8_t> wire, hpack_section_limits limits) -> hpack_section_result {
    if (!usable_) return {{hpack_state::malformed}, {}};
    if (wire.size() > limits.max_compressed_bytes) return fail({hpack_state::limit_exceeded});
    try {
        return process(wire, limits);
    } catch (const std::bad_alloc&) {
        return fail({hpack_state::limit_exceeded});
    } catch (const std::length_error&) {
        return fail({hpack_state::limit_exceeded});
    }
}

auto hpack_decoder::fail(hpack_status status) noexcept -> hpack_section_result {
    usable_ = false;
    table_.clear();
    if (status.state == hpack_state::incomplete) status.state = hpack_state::malformed;
    return {status, {}};
}

auto hpack_decoder::indexed(std::span<const std::uint8_t>& wire, std::size_t& remaining, hpack_field& field) -> hpack_status {
    const auto index = hpack_decode_integer(wire, 7, {});
    if (!index.status.ok()) return index.status;
    const auto entry = table_.lookup(index.value);
    if (!entry) return {hpack_state::malformed};
    if (!hpack_admit_field(entry->name.size(), entry->value.size(), remaining)) return {hpack_state::limit_exceeded};
    field = {std::string(entry->name), std::string(entry->value), hpack_indexing::incremental};
    wire = wire.subspan(index.consumed);
    return {};
}

auto hpack_decoder::literal(std::span<const std::uint8_t>& wire, std::size_t& remaining, hpack_field& field) -> hpack_status {
    const bool incremental = (wire[0] & 0x40) != 0;
    const bool never = !incremental && (wire[0] & 0x10) != 0;
    const auto index = hpack_decode_integer(wire, incremental ? 6 : 4, {});
    if (!index.status.ok()) return index.status;
    wire = wire.subspan(index.consumed);
    const auto entry = table_.lookup(index.value);
    if (index.value != 0 && !entry) return {hpack_state::malformed};
    field.indexing = incremental ? hpack_indexing::incremental : never ? hpack_indexing::never_indexed : hpack_indexing::without_indexing;
    const auto status = read_literal(entry, wire, remaining, field);
    if (!status.ok()) return status;
    return incremental ? table_.insert_copy(field.name, field.value) : hpack_status{};
}

auto hpack_decoder::read_literal(std::optional<hpack_static_entry> entry, std::span<const std::uint8_t>& wire, std::size_t& remaining, hpack_field& field) -> hpack_status {
    if (remaining < 32) return {hpack_state::limit_exceeded};
    const auto allowance = remaining - 32;
    hpack_integer_result name;
    if (entry)
        name.value = entry->name.size();
    else
        name = hpack_inspect_string(wire, {wire.size(), allowance});
    if (!name.status.ok()) return name.status;
    if (name.value > allowance) return {hpack_state::limit_exceeded};
    const auto value_wire = wire.subspan(name.consumed);
    const auto value = hpack_inspect_string(value_wire, {value_wire.size(), allowance - static_cast<std::size_t>(name.value)});
    if (!value.status.ok()) return value.status;
    hpack_admit_field(static_cast<std::size_t>(name.value), static_cast<std::size_t>(value.value), remaining);
    field.name = entry ? std::string(entry->name) : hpack_decode_string(wire, {wire.size(), allowance}).value;
    field.value = hpack_decode_string(value_wire, {value_wire.size(), static_cast<std::size_t>(value.value)}).value;
    wire = value_wire.subspan(value.consumed);
    return {};
}

auto hpack_decoder::size_update(std::span<const std::uint8_t>& wire) noexcept -> hpack_status {
    const auto update = hpack_decode_integer(wire, 5, {});
    if (!update.status.ok()) return update.status;
    if (update.value > maximum_) return {hpack_state::malformed};
    if (required_minimum_) {
        if (update.value > *required_minimum_) return {hpack_state::malformed};
        required_minimum_.reset();
    }
    table_.set_capacity(static_cast<std::size_t>(update.value));
    wire = wire.subspan(update.consumed);
    return {};
}

auto hpack_decoder::process(std::span<const std::uint8_t> wire, hpack_section_limits limits) -> hpack_section_result {
    hpack_section_result out;
    auto remaining = limits.max_expanded_bytes;
    bool fields_started = false;
    while (!wire.empty()) {
        if ((wire[0] & 0xe0) == 0x20) {
            if (fields_started) return fail({hpack_state::malformed});
            const auto status = size_update(wire);
            if (!status.ok()) return fail(status);
            continue;
        }
        if (required_minimum_) return fail({hpack_state::malformed});
        fields_started = true;
        if (out.fields.size() == limits.max_fields) return fail({hpack_state::limit_exceeded});
        hpack_field field;
        const auto status = (wire[0] & 0x80) != 0 ? indexed(wire, remaining, field) : literal(wire, remaining, field);
        if (!status.ok()) return fail(status);
        out.fields.push_back(std::move(field));
    }
    if (required_minimum_) return fail({hpack_state::malformed});
    return out;
}

}  // namespace httpserver::detail
