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

#include <ctype.h>
#include <algorithm>
#include <map>
#include <memory>
// Disabling lint error on regex (the only reason it errors is because the Chromium team prefers google/re2)
#include <regex>  // NOLINT [build/c++11]
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "httpserver/detail/http_endpoint.hpp"
#include "httpserver/http_utils.hpp"

using std::string;
using std::vector;

namespace httpserver {

namespace detail {

http_endpoint::~http_endpoint() {
}

void http_endpoint::normalize_url_complete() {
    if (url_complete[url_complete.size() - 1] == '/') {
        url_complete = url_complete.substr(0, url_complete.size() - 1);
    }
    if (url_complete[0] != '/') {
        url_complete = "/" + url_complete;
    }
}

void http_endpoint::append_non_registration_part(const string& part, bool& first) {
    url_normalized += (first ? "" : "/") + part;
    first = false;
    url_pieces.push_back(part);
}

void http_endpoint::append_literal_url_part(const string& part, bool& first) {
    if (first) {
        // First piece: respect a leading '^' anchor verbatim by replacing
        // (not appending to) url_normalized's seed prefix.
        url_normalized = (part[0] == '^' ? "" : url_normalized) + part;
        first = false;
    } else {
        url_normalized += "/" + part;
    }
    url_pieces.push_back(part);
}

void http_endpoint::append_parameter_url_part(const string& part,
                                              unsigned int i, bool& first) {
    if (part.size() < 3 || part[0] != '{' || part[part.size() - 1] != '}') {
        throw std::invalid_argument("Bad URL format");
    }
    // {name} or {name|regex}: split on the optional '|'.
    std::string::size_type bar = part.find_first_of('|');
    const bool has_regex = bar != string::npos;
    url_pars.push_back(part.substr(1, has_regex ? bar - 1 : part.size() - 2));
    url_normalized += (first ? "" : "/")
        + (has_regex ? part.substr(bar + 1, part.size() - bar - 2) : "([^\\/]+)");
    first = false;
    chunk_positions.push_back(i);
    url_pieces.push_back(part);
}

void http_endpoint::process_url_part(const vector<string>& parts,
                                     unsigned int i, bool& first, bool registration) {
    if (!registration) {
        append_non_registration_part(parts[i], first);
        return;
    }
    if (!parts[i].empty() && parts[i][0] != '{') {
        append_literal_url_part(parts[i], first);
        return;
    }
    append_parameter_url_part(parts[i], i, first);
}

void http_endpoint::compile_regex_url() {
    url_normalized += "$";
    try {
        re_url_normalized = std::regex(url_normalized,
            std::regex::extended | std::regex::icase | std::regex::nosubs);
    } catch (std::regex_error& e) {
        throw std::invalid_argument("Not a valid regex in URL: " + url_normalized);
    }
    reg_compiled = true;
}

http_endpoint::http_endpoint(const string& url, bool family, bool registration, bool use_regex):
    family_url(family),
    reg_compiled(false) {
    if (use_regex && !registration) {
        throw std::invalid_argument("Cannot use regex if not during registration");
    }
    url_normalized = use_regex ? "^/" : "/";

#ifdef CASE_INSENSITIVE
    string_utilities::to_lower_copy(url, url_complete);
#else
    url_complete = url;
#endif
    normalize_url_complete();

    auto parts = httpserver::http::http_utils::tokenize_url(url);
    bool first = true;
    for (unsigned int i = 0; i < parts.size(); i++) {
        process_url_part(parts, i, first, registration);
    }

    if (use_regex) compile_regex_url();
}

http_endpoint::http_endpoint(const http_endpoint& h):
    url_complete(h.url_complete),
    url_normalized(h.url_normalized),
    url_pars(h.url_pars),
    url_pieces(h.url_pieces),
    chunk_positions(h.chunk_positions),
    re_url_normalized(h.re_url_normalized),
    family_url(h.family_url),
    reg_compiled(h.reg_compiled) {
}

http_endpoint& http_endpoint::operator =(const http_endpoint& h) {
    url_complete = h.url_complete;
    url_normalized = h.url_normalized;
    family_url = h.family_url;
    reg_compiled = h.reg_compiled;
    re_url_normalized = h.re_url_normalized;
    url_pars = h.url_pars;
    url_pieces = h.url_pieces;
    chunk_positions = h.chunk_positions;
    return *this;
}

bool http_endpoint::operator <(const http_endpoint& b) const {
    if (family_url != b.family_url) return family_url;
    COMPARATOR(url_normalized, b.url_normalized, std::toupper);
}

bool http_endpoint::match(const http_endpoint& url) const {
    if (!reg_compiled) throw std::invalid_argument("Cannot run match. Regex suppressed.");

    // Family (prefix) rule: a family endpoint matches when the FIRST N
    // request segments match the registered pattern, where N is the
    // pattern's own segment count (url_pieces.size()). A request with
    // fewer segments than the pattern can never satisfy that prefix
    // rule, so it falls through to plain full-string matching below
    // (which also handles every non-family endpoint).
    if (!family_url || url.url_pieces.size() < url_pieces.size()) {
        return regex_match(url.url_complete, re_url_normalized);
    }

    // Rebuild the request path truncated to the pattern's first N
    // segments in `nn`, so the prefix can be regex-matched against
    // re_url_normalized (which matches whole strings, not prefixes).
    string nn = "/";
    bool first = true;
    for (unsigned int i = 0; i < url_pieces.size(); i++) {
        nn += (first ? "" : "/") + url.url_pieces[i];
        first = false;
    }
    return regex_match(nn, re_url_normalized);
}

}  // namespace detail

}  // namespace httpserver
