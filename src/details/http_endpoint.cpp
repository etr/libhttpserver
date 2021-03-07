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

#include "httpserver/details/http_endpoint.hpp"
#include "httpserver/http_utils.hpp"

using std::string;
using std::vector;

namespace httpserver {

namespace details {

http_endpoint::~http_endpoint() {
}

http_endpoint::http_endpoint(const string& url, bool family, bool registration, bool use_regex):
    family_url(family),
    reg_compiled(false) {
    if (use_regex && !registration) {
        throw std::invalid_argument("Cannot use regex if not during registration");
    }

    url_normalized = use_regex ? "^/" : "/";
    vector<string> parts;

#ifdef CASE_INSENSITIVE
    string_utilities::to_lower_copy(url, url_complete);
#else
    url_complete = url;
#endif

    if (url_complete[url_complete.size() - 1] == '/') {
        url_complete = url_complete.substr(0, url_complete.size() - 1);
    }

    if (url_complete[0] != '/') {
        url_complete = "/" + url_complete;
    }

    parts = httpserver::http::http_utils::tokenize_url(url);
    string buffered;
    bool first = true;

    for (unsigned int i = 0; i < parts.size(); i++) {
        if (!registration) {
            url_normalized += (first ? "" : "/") + parts[i];
            first = false;

            url_pieces.push_back(parts[i]);

            continue;
        }

        if ((parts[i] != "") && (parts[i][0] != '{')) {
            if (first) {
                url_normalized = (parts[i][0] == '^' ? "" : url_normalized) + parts[i];
                first = false;
            } else {
                url_normalized += "/" + parts[i];
            }
            url_pieces.push_back(parts[i]);

            continue;
        }

        if ((parts[i].size() < 3) || (parts[i][0] != '{') || (parts[i][parts[i].size() - 1] != '}')) {
            throw std::invalid_argument("Bad URL format");
        }

        std::string::size_type bar = parts[i].find_first_of('|');
        url_pars.push_back(parts[i].substr(1, bar != string::npos ? bar - 1 : parts[i].size() - 2));
        url_normalized += (first ? "" : "/") + (bar != string::npos ? parts[i].substr(bar + 1, parts[i].size() - bar - 2) : "([^\\/]+)");

        first = false;

        chunk_positions.push_back(i);

        url_pieces.push_back(parts[i]);
    }

    if (use_regex) {
        url_normalized += "$";
        try {
            re_url_normalized = std::regex(url_normalized, std::regex::extended | std::regex::icase | std::regex::nosubs);
        } catch (std::regex_error& e) {
            throw std::invalid_argument("Not a valid regex in URL: " + url_normalized);
        }
        reg_compiled = true;
    }
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
    COMPARATOR(url_normalized, b.url_normalized, std::toupper);
}

bool http_endpoint::match(const http_endpoint& url) const {
    if (!reg_compiled) throw std::invalid_argument("Cannot run match. Regex suppressed.");

    if (!family_url || url.url_pieces.size() < url_pieces.size()) {
        return regex_match(url.url_complete, re_url_normalized);
    }

    string nn = "/";
    bool first = true;
    for (unsigned int i = 0; i < url_pieces.size(); i++) {
        nn += (first ? "" : "/") + url.url_pieces[i];
        first = false;
    }
    return regex_match(nn, re_url_normalized);
}

}  // namespace details

}  // namespace httpserver
