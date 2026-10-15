/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/forms_verdict.hpp>

namespace httpserver::detail::forms_verdict {

http::status reject_status(const http::outcome& status) noexcept {
    if (status.code() == http::outcome_code::limit_exceeded) {
        return http::status::from_code(413);
    }
    if (status.code() == http::outcome_code::invalid_argument) {
        return http::status::from_code(400);
    }
    return http::status::from_code(0);
}

http::fields reject_fields() {
    http::fields fields;
    fields.append("Content-Length", "0");
    return fields;
}

}  // namespace httpserver::detail::forms_verdict
