/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

#include "httpserver/http_response.hpp"

#include <sys/types.h>          // ssize_t (for the deferred() producer)

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "httpserver/detail/body.hpp"   // complete type for body_->~body()
#include "httpserver/detail/http_field_validation.hpp"
#include "httpserver/feature_unavailable.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/iovec_entry.hpp"

namespace httpserver {

// http_response_factories.cpp -- static named-constructor factories and the
// shared emplace_body placement-new entry point.
//
// Carved out of src/http_response.cpp in TASK-086 to keep both translation
// units under the project per-file LOC ceiling (FILE_LOC_MAX in
// scripts/check-file-size.sh). emplace_body is moved here (not left in
// http_response.cpp) because every instantiation lives in this file's
// factories, so no separate-TU instantiation is needed. No behaviour
// change: the bodies are moved verbatim.

// -----------------------------------------------------------------------
// emplace_body — single placement-new entry point shared by all
// factories (TASK-010). Centralising the SBO-vs-heap decision here means
// the matched ::operator new(sizeof(T)) / ::operator delete pairing the
// destructor relies on (TASK-009 OQ-4) lives in exactly one place; a
// stray plain `new T(...)` in any factory would mismatch the
// destructor's ::operator delete and trip ASan immediately.
//
// Defined out-of-line in this TU because every factory in this file
// instantiates it (so no separate-TU instantiation is needed) and the
// template body needs the complete type detail::body. Per-T size+align
// guards duplicate the SBO budget asserts in detail/body.hpp so an
// over-sized future body subclass fails to compile at the factory site
// rather than silently triggering the heap fallback.
// -----------------------------------------------------------------------
template <typename T, typename... Args>
void http_response::emplace_body(body_kind k, Args&&... args) {
    static_assert(std::is_base_of_v<detail::body, T>,
                  "emplace_body: T must derive from detail::body");
    assert(body_ == nullptr &&
           "emplace_body: body slot already populated");
    if constexpr (sizeof(T) <= body_buf_size && alignof(T) <= 16) {
        // SBO inline path.
        body_ = ::new (body_storage_) T(std::forward<Args>(args)...);
        body_inline_ = true;
    } else {
        // Heap fallback. ::operator new(sizeof(T)) is paired exactly
        // with the destructor's ::operator delete(body_); a plain
        // `new T(...)` here would mismatch.
        void* mem = ::operator new(sizeof(T));
        try {
            body_ = ::new (mem) T(std::forward<Args>(args)...);
        } catch (...) {
            ::operator delete(mem);
            throw;
        }
        body_inline_ = false;
    }
    kind_ = k;
}

// -----------------------------------------------------------------------
// Static factories (TASK-010). Each factory:
//   1. constructs a default http_response (status_code_ = -1, no body),
//   2. sets the status code and any per-kind headers,
//   3. emplaces the appropriate detail::body subclass via emplace_body.
//
// The status-code defaults match v1: 200 for content-bearing bodies,
// 204 for empty(), 401 for unauthorized().
// -----------------------------------------------------------------------

http_response http_response::empty(int mhd_flags) {
    http_response r;
    r.status_code_ = http::http_utils::http_no_content;  // 204
    r.emplace_body<detail::empty_body>(body_kind::empty, mhd_flags);
    return r;
}

http_response http_response::string(std::string body,
                                    std::string content_type) {
    http_response r;
    r.status_code_ = http::http_utils::http_ok;          // 200
    r.with_header(http::http_utils::http_header_content_type,
                  std::move(content_type));
    r.emplace_body<detail::string_body>(body_kind::string,
                                        std::move(body));
    return r;
}

http_response http_response::file(std::string path) {
    http_response r;
    r.status_code_ = http::http_utils::http_ok;
    // Match v1 file_response default Content-Type. Callers can override
    // with .with_header("Content-Type", "...") in the chain.
    r.with_header(http::http_utils::http_header_content_type,
                  http::http_utils::application_octet_stream);
    r.emplace_body<detail::file_body>(body_kind::file, std::move(path));
    return r;
}

http_response http_response::iovec(std::span<const iovec_entry> entries) {
    // Deep-copy into the body's owned vector so the caller's span need
    // not outlive the response. The buffers each entry's `base` points
    // at remain BORROWED — see detail::iovec_body's lifetime contract.
    std::vector<iovec_entry> v(entries.begin(), entries.end());
    http_response r;
    r.status_code_ = http::http_utils::http_ok;
    r.emplace_body<detail::iovec_body>(body_kind::iovec, std::move(v));
    return r;
}

http_response http_response::pipe(int fd) {
    http_response r;
    r.status_code_ = http::http_utils::http_ok;
    r.emplace_body<detail::pipe_body>(body_kind::pipe, fd);
    return r;
}

http_response http_response::deferred(
        std::function<ssize_t(std::uint64_t, char*, std::size_t)> producer) {
    http_response r;
    r.status_code_ = http::http_utils::http_ok;
    r.emplace_body<detail::deferred_body>(body_kind::deferred,
                                          std::move(producer));
    return r;
}

http_response http_response::unauthorized(std::string_view scheme,
                                          std::string_view realm,
                                          std::string body) {
    // Security: reject scheme or realm values containing CR, LF, or NUL.
    // Any of these characters can be used to inject additional HTTP headers
    // into the WWW-Authenticate response header (CWE-113). This is always a
    // caller error — callers must never pass untrusted user input as scheme
    // or realm without first validating it. Throw std::invalid_argument so
    // the error is visible and cannot be silently swallowed.
    // detail::kForbiddenFieldChars is the same constant used by
    // validate_http_field in http_response.cpp — shared to avoid a
    // duplicate definition.
    if (scheme.find_first_of(detail::kForbiddenFieldChars) != std::string_view::npos) {
        throw std::invalid_argument(
            "http_response::unauthorized: scheme contains forbidden control "
            "character (CR, LF, or NUL)");
    }
    if (realm.find_first_of(detail::kForbiddenFieldChars) != std::string_view::npos) {
        throw std::invalid_argument(
            "http_response::unauthorized: realm contains forbidden control "
            "character (CR, LF, or NUL)");
    }

    // Security: escape backslash and double-quote characters inside realm
    // per RFC 7235 §2.1 quoted-string rules.  RFC 7235 allows both to be
    // escaped via the quoted-pair rule `\X`; an unescaped `"` terminates
    // the quoted-string early (CWE-116) and an unescaped `\` is
    // misinterpreted as starting an escape sequence by strict parsers.
    // Backslash must be escaped first to avoid double-escaping the
    // backslashes we insert for double-quote escaping.
    //
    // Fast path: the common case (e.g. the canonical `Basic realm="myrealm"`)
    // has no backslash or double-quote to escape, so realm can be appended
    // directly and the char-by-char copy + heap allocation below is skipped.
    http_response r;
    r.status_code_ = http::http_utils::http_unauthorized;        // 401
    // Build `<scheme> realm="<escaped_realm>"`. AC #3 requires byte-for-byte
    // `Basic realm="myrealm"` for the canonical case (which has no quotes).
    std::string challenge;
    challenge.reserve(scheme.size() + realm.size() + 10);
    challenge.append(scheme.data(), scheme.size());
    challenge.append(" realm=\"");
    if (realm.find_first_of("\\\"") == std::string_view::npos) {
        challenge.append(realm.data(), realm.size());
    } else {
        std::string escaped_realm;
        escaped_realm.reserve(realm.size());
        for (char c : realm) {
            if (c == '\\') {
                escaped_realm.push_back('\\');  // escape backslash first
            } else if (c == '"') {
                escaped_realm.push_back('\\');  // escape double-quote
            }
            escaped_realm.push_back(c);
        }
        challenge.append(escaped_realm);
    }
    challenge.push_back('"');
    r.with_header(http::http_utils::http_header_www_authenticate,
                  challenge);
    // The body slot literally holds a string_body (possibly empty), so
    // kind() reports body_kind::string. Switching to body_kind::empty
    // for the empty-body case would fork the construction path and
    // break the invariant that kind() reflects the placed-new body.
    r.emplace_body<detail::string_body>(body_kind::string,
                                        std::move(body));
    return r;
}

#ifdef HAVE_DAUTH
// TASK-062: RFC 7616 §3.3-compliant Digest challenge factory.
//
// Validates the user-supplied fields for header-injection control
// characters (CR/LF/NUL) and packs the parameters into a
// detail::digest_challenge_body. No `WWW-Authenticate` header is added
// at the response-value layer; the dispatch path (TASK-062 branch in
// materialize_and_queue_response) calls
// MHD_queue_auth_required_response3 to attach the authoritative
// challenge with nonce/opaque/algorithm/qop/charset/userhash bits.
//
// Empty opaque is preserved: the dispatch path substitutes
// webserver_impl::digest_opaque_ at queue time, so the factory remains
// side-effect-free (no webserver reference required).
http_response http_response::unauthorized(digest_challenge challenge) {
    // Same forbidden-character set as validate_http_field above.
    auto reject_ctrl_chars = [](std::string_view field,
                                std::string_view value) {
        if (value.find_first_of(detail::kForbiddenFieldChars) !=
                std::string_view::npos) {
            throw std::invalid_argument(
                std::string("http_response::unauthorized(digest_challenge): ") +
                std::string(field) +
                " contains forbidden control character (CR, LF, or NUL)");
        }
    };
    reject_ctrl_chars("realm",  challenge.realm);
    reject_ctrl_chars("opaque", challenge.opaque);
    reject_ctrl_chars("domain", challenge.domain);
    reject_ctrl_chars("body",   challenge.body);

    // qop="auth-int" is not implemented: the dispatch path
    // (map_to_mhd_digest_args_) has no MHD mapping for it and would
    // silently ignore the flag. Fail loudly rather than let a caller
    // believe integrity protection was negotiated.
    if (challenge.qop_auth_int) {
        throw std::invalid_argument(
            "http_response::unauthorized(digest_challenge): "
            "qop_auth_int (qop=\"auth-int\") is not implemented; "
            "leave it false");
    }

    detail::digest_challenge_body::params p{
        /*realm=*/        std::move(challenge.realm),
        /*opaque=*/       std::move(challenge.opaque),
        /*domain=*/       std::move(challenge.domain),
        /*body_text=*/    std::move(challenge.body),
        /*algorithm=*/    challenge.algorithm,
        /*qop_auth=*/     challenge.qop_auth,
        /*qop_auth_int=*/ challenge.qop_auth_int,
        /*signal_stale=*/ challenge.signal_stale,
        /*userhash_support=*/ challenge.userhash_support,
        /*prefer_utf8=*/  challenge.prefer_utf8,
    };

    http_response r;
    r.status_code_ = http::http_utils::http_unauthorized;        // 401
    r.emplace_body<detail::digest_challenge_body>(
        body_kind::digest_challenge, std::move(p));
    return r;
}
#else  // !HAVE_DAUTH
// PRD-FLG-REQ-002/004: the declaration in http_response_factories.hpp is
// unconditional, so a HAVE_DAUTH-off build must still define this overload
// rather than leave it as a link error. Throw feature_unavailable instead
// of constructing a response.
http_response http_response::unauthorized(digest_challenge challenge) {
    (void)challenge;
    throw feature_unavailable("digest_auth", "HAVE_DAUTH");
}
#endif  // HAVE_DAUTH

}  // namespace httpserver
