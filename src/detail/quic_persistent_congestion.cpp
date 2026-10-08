/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include "./quic_recovery_internal.hpp"
namespace httpserver::detail {
void quic_recovery::implementation::note_congestion_packet(const packet& p, quic_pn_space space) {
    compact_congestion_runs();
    // Forgetting an oldest unresolved prefix can only shorten a congestion run.
    // Keep a fixed bound even when control records are repeatedly collected.
    if (congestion_runs.size() == 3 * config.max_sent_packets + 1) congestion_runs.erase(congestion_runs.begin());
    congestion_runs.push_back({p.sequence, p.sequence, p.number, p.number, space, p.sent_at, p.sent_at,
                              p.ack_eliciting ? outcome::pending : outcome::neutral, p.sampled});
}
void quic_recovery::implementation::note_congestion_outcome(const packet& p, outcome status) {
    for (auto& run : congestion_runs) {
        if (p.sequence < run.first || p.sequence > run.last) continue;
        // A late ACK of a compressed lost interval invalidates the whole
        // interval conservatively; it cannot create persistent congestion.
        run.status = status;
        return;
    }
}
void quic_recovery::implementation::acknowledge_congestion_runs(quic_pn_space space, std::span<const quic_ack_range> ranges) {
    for (auto& run : congestion_runs) {
        if (run.space != space) continue;
        bool covered = std::any_of(ranges.begin(), ranges.end(), [&](auto r) {
            return r.smallest <= run.last_number && r.largest >= run.first_number;
        });
        if (covered) run.status = outcome::acknowledged;
    }
}
void quic_recovery::implementation::compact_congestion_runs() {
    std::size_t kept = 0;
    for (auto run : congestion_runs) {
        bool resolved = run.status != outcome::pending;
        if (kept && resolved && congestion_runs[kept - 1].status == run.status && congestion_runs[kept - 1].sampled == run.sampled && congestion_runs[kept - 1].space == run.space) {
            congestion_runs[kept - 1].last = run.last;
            congestion_runs[kept - 1].end = run.end;
            congestion_runs[kept - 1].last_number = run.last_number;
        } else {
            congestion_runs[kept++] = run;
        }
    }
    congestion_runs.resize(kept);
}
bool quic_recovery::implementation::persistent_congestion() const {
    auto variance = std::max(recovery_scale(rtt.variation, 4), recovery_duration(std::chrono::milliseconds(1)));
    auto threshold = recovery_scale(recovery_sum(recovery_sum(rtt.smoothed, variance), config.peer_max_ack_delay), 3);
    std::optional<time_point> first;
    for (const auto& run : congestion_runs) {
        if (run.status == outcome::neutral) continue;
        if (run.status != outcome::lost || !run.sampled) {
            first.reset();
            continue;
        }
        if (!first) first = run.begin;
        if ((!persistent_through || run.last > *persistent_through) && recovery_elapsed(run.end, *first) > threshold) return true;
    }
    return false;
}
}  // namespace httpserver::detail
