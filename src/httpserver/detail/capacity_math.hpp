/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "capacity_math.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_CAPACITY_MATH_HPP_
#define SRC_HTTPSERVER_DETAIL_CAPACITY_MATH_HPP_
#include <cstddef>
#include <limits>
#include <stdexcept>
namespace httpserver::detail {
inline std::size_t capacity_add(std::size_t a, std::size_t b) {
    if (b > std::numeric_limits<std::size_t>::max() - a) throw std::invalid_argument("protocol capacity addition overflow");
    return a + b;
}
inline std::size_t capacity_multiply(std::size_t a, std::size_t b) {
    if (a && b > std::numeric_limits<std::size_t>::max() / a) throw std::invalid_argument("protocol capacity multiplication overflow");
    return a * b;
}
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_CAPACITY_MATH_HPP_
