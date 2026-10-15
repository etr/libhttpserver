/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <httpserver/detail/quic_recovery_internal.hpp>

namespace httpserver::detail {

recovery_duration recovery_scale(recovery_duration value, std::uint64_t numerator, std::uint64_t denominator) {
    const auto maximum = static_cast<std::uint64_t>(recovery_duration::max().count());
    const auto count = static_cast<std::uint64_t>(value.count());
    const auto quotient = count / denominator, remainder = count % denominator;
    if (quotient > maximum / numerator) return recovery_duration::max();
    const auto extra = remainder * numerator / denominator;
    if (quotient * numerator > maximum - extra) return recovery_duration::max();
    return recovery_duration(quotient * numerator + extra);
}

recovery_duration recovery_elapsed(quic_recovery::time_point now, quic_recovery::time_point before) {
    if (now <= before) return {};
    using unsigned_rep = std::make_unsigned_t<recovery_duration::rep>;
    auto difference = static_cast<unsigned_rep>(now.time_since_epoch().count()) - static_cast<unsigned_rep>(before.time_since_epoch().count());
    return recovery_duration(std::min(difference, static_cast<unsigned_rep>(recovery_duration::max().count())));
}

quic_recovery::time_point recovery_after(quic_recovery::time_point now, recovery_duration delay) {
    const auto current = now.time_since_epoch().count();
    if (current > recovery_duration::max().count() - delay.count()) return quic_recovery::time_point::max();
    return now + delay;
}

recovery_duration recovery_sum(recovery_duration a, recovery_duration b) {
    if (a > recovery_duration::max() - b) return recovery_duration::max();
    return a + b;
}

recovery_duration recovery_average(recovery_duration old, recovery_duration sample, std::uint64_t denominator) {
    auto a = old.count(), b = sample.count();
    return recovery_duration((a / denominator) * (denominator - 1) + b / denominator + ((a % denominator) * (denominator - 1) + b % denominator) / denominator);
}

auto quic_recovery::implementation::state(quic_pn_space s) -> space_state* {
    auto index = static_cast<unsigned>(s);
    return index < spaces.size() ? &spaces[index] : nullptr;
}

auto quic_recovery::implementation::state(quic_pn_space s) const -> const space_state* {
    auto index = static_cast<unsigned>(s);
    return index < spaces.size() ? &spaces[index] : nullptr;
}

}  // namespace httpserver::detail
