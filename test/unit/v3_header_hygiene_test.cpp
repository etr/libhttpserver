/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-097 Step 5 (Check B): banned-token sentinel for the v3
// semantic headers. The headers under V3_SEMANTIC_HEADER_DIR must:
//   (1) contain no preprocessor conditional (#if/#ifdef/#ifndef/#elif)
//       that references HAVE_, COND_, GNUTLS, OPENSSL, MHD, or
//       MICROHTTPD — i.e. no build/TLS configuration gates, so the
//       headers compile identically in TLS-on and TLS-off installs;
//   (2) contain no occurrence (case-insensitive) of backend or OS
//       socket vocabulary: microhttpd, gnutls, openssl, mhd_,
//       sockaddr, socket, epoll, kqueue, wsapoll, wsae, iocp — i.e.
//       no OS socket or backend enum types leak into the public v3
//       semantic surface (PRD-V3N-REQ-037, DR-V3-001).
//
// The scan is executable (not review practice) and runs in both
// TLS-on and TLS-off configurations of the same source. Include
// guards themselves are allowed; only conditionals naming the banned
// tokens are violations. Violations are printed individually so CI
// logs show exactly which line leaked.

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "./littletest.hpp"

namespace {

constexpr const char* kHeaderDir = V3_SEMANTIC_HEADER_DIR;

const std::vector<const char*>& header_files() {
    static const std::vector<const char*> files = {
        "/http.hpp",
        "/http/fields.hpp",
        "/http/method.hpp",
        "/http/outcome.hpp",
        "/http/protocol.hpp",
        "/http/request_head.hpp",
        // TASK-098: the v3 concurrency core joins the sentinel scan with
        // the same rules (no config gates, no backend/OS-socket tokens).
        "/concurrency/concurrency.hpp",
        "/concurrency/cancellation.hpp",
        "/concurrency/executor.hpp",
        "/concurrency/resume_signal.hpp",
        "/concurrency/task.hpp",
        // TASK-101: the v3 server configuration area joins the sentinel
        // scan with the same rules (no config gates, no backend/OS
        // tokens; "listener" vocabulary only).
        "/server/budgets.hpp",
        "/server/options.hpp",
        "/server/routes.hpp",
        "/server/configuration.hpp",
        // TASK-102: the exchange area joins the sentinel scan with the
        // same rules (no config gates, no backend/OS tokens; engine and
        // exchange vocabulary only).
        "/exchange.hpp",
        "/http/status.hpp",
        // TASK-103: the body reader joins the sentinel scan with the
        // same rules (no config gates, no backend/OS tokens; body
        // delivery vocabulary only).
        "/body_reader.hpp",
        // TASK-104: the response writer joins the sentinel scan with
        // the same rules (no config gates, no backend/OS tokens;
        // engine and body delivery vocabulary only).
        "/response_writer.hpp",
        // TASK-108: the native server lifecycle joins the sentinel
        // scan with the same rules (no config gates, no backend/OS
        // tokens; the pimpl keeps the engine vocabulary in detail/).
        "/server/server.hpp",
        // TASK-111: the sync route adapter joins the sentinel scan
        // with the same rules (no config gates, no backend/OS tokens).
        "/server/route_sync.hpp",
        // TASK-112: the immutable response definitions join the
        // sentinel scan with the same rules (no config gates, no
        // backend/OS tokens; response value and send vocabulary only).
        "/response_definition.hpp",
        // TASK-113: the response body-source vocabulary (split from
        // response_definition.hpp) joins the sentinel scan with the
        // same rules (no config gates, no backend/OS-socket tokens).
        "/response_sources.hpp",
        // TASK-114: the Basic authentication policy joins the sentinel
        // scan with the same rules (no config gates, no backend/OS
        // tokens; parsing and codecs stay in the library's detail/).
        "/auth/basic_auth.hpp",
        // TASK-115: the Digest authentication policy joins the sentinel
        // scan with the same rules (no config gates, no backend/OS
        // tokens; the nonce, ledger, parser, and hashes stay in
        // detail/).
        "/auth/digest_auth.hpp",
        // TASK-116: the urlencoded form vocabulary joins the sentinel
        // scan with the same rules (no config gates, no backend/OS
        // tokens; the decoder and adapters stay in detail/).
        "/forms/urlencoded.hpp",
        // TASK-117: the multipart form vocabulary joins the sentinel
        // scan with the same rules (no config gates, no backend/OS
        // tokens; the wire decoder, the drivers, and the temp-file
        // sink stay in detail/).
        "/forms/multipart.hpp",
        // TASK-118: the lifecycle hook vocabulary joins the sentinel
        // scan with the same rules (no config gates, no backend/OS
        // tokens; the bus storage stays in detail/).
        "/server/hooks.hpp",
    };
    return files;
}

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string read_file(const std::string& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    ok = in.is_open();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

LT_BEGIN_SUITE(v3_header_hygiene_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(v3_header_hygiene_suite)

LT_BEGIN_AUTO_TEST(v3_header_hygiene_suite, no_config_conditionals)
    std::vector<std::string> violations;
    for (const char* file : header_files()) {
        const std::string path = std::string(kHeaderDir) + file;
        bool ok = false;
        const std::string content = read_file(path, ok);
        if (!ok) {
            violations.push_back("cannot read " + path);
            continue;
        }
        std::istringstream lines(content);
        std::string line;
        int line_no = 0;
        while (std::getline(lines, line)) {
            ++line_no;
            const std::string lower = lowered(line);
            const auto first = lower.find_first_not_of(" \t");
            if (first == std::string::npos) continue;
            const std::string trimmed = lower.substr(first);
            const bool conditional =
                trimmed.rfind("#if", 0) == 0
                || trimmed.rfind("#elif", 0) == 0;
            if (!conditional) continue;
            for (const std::string& token :
                 {std::string("have_"), std::string("cond_"),
                  std::string("gnutls"), std::string("openssl"),
                  std::string("mhd"), std::string("microhttpd")}) {
                if (trimmed.find(token) != std::string::npos) {
                    violations.push_back(path + ":"
                                         + std::to_string(line_no)
                                         + ": conditional references '"
                                         + token + "'");
                }
            }
        }
    }
    for (const std::string& v : violations) {
        std::cout << "[HYGIENE VIOLATION] " << v << std::endl;
    }
    LT_CHECK(violations.empty());
LT_END_AUTO_TEST(no_config_conditionals)

LT_BEGIN_AUTO_TEST(v3_header_hygiene_suite, no_backend_or_socket_tokens)
    std::vector<std::string> violations;
    for (const char* file : header_files()) {
        const std::string path = std::string(kHeaderDir) + file;
        bool ok = false;
        const std::string content = lowered(read_file(path, ok));
        if (!ok) {
            violations.push_back("cannot read " + path);
            continue;
        }
        for (const std::string& token :
             {std::string("microhttpd"), std::string("gnutls"),
              std::string("openssl"), std::string("mhd_"),
              std::string("sockaddr"), std::string("socket"),
              std::string("epoll"), std::string("kqueue"),
              std::string("wsapoll"), std::string("wsae"),
              std::string("iocp")}) {
            if (content.find(token) != std::string::npos) {
                violations.push_back(path + ": contains banned token '"
                                     + token + "'");
            }
        }
    }
    for (const std::string& v : violations) {
        std::cout << "[HYGIENE VIOLATION] " << v << std::endl;
    }
    LT_CHECK(violations.empty());
LT_END_AUTO_TEST(no_backend_or_socket_tokens)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
