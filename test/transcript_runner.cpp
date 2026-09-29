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
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// TASK-096 Cycle 5: the segmented-input transcript runner.
//
// Executes the committed *.tseq corpus over real loopback TCP
// connections against the pluggable server_fixture (v2 today, the v3
// native engine in later milestones). Engine-agnostic by construction:
// this TU knows nothing about libmicrohttpd.
//
// Per case the runner
//   1. connects to the fixture's bound port,
//   2. writes every `send` segment with one write() per segment
//      (segment boundaries exercise server buffering; output
//      determinism never depends on kernel coalescing),
//   3. reads responses delimited purely by protocol framing
//      (Content-Length / chunked / connection close; no sleeps),
//   4. optionally probes or waits for the connection outcome,
//   5. compares normalized observations against the curated
//      expectations, reporting
//      "transcript=<file> case=<name> expect-line=<n>: <diff>".
//
// Modes (parsed from argv before the littletest runner starts):
//   (default)  run every corpus file (optionally filtered by substring
//              or exact file name given as a positional argument)
//   --record   write observed normalized exchanges as .recorded
//              sidecars instead of asserting (D4 record-then-curate)
//   --list     print transcript/case names and exit
//
// Determinism rules: read deadlines only bound failure detection; no
// wall-clock value is ever a pass condition; volatile headers (Date)
// are elided and Digest nonces are masked in the corpus.

#include "parity/normalize.hpp"
#include "parity/response_frame.hpp"
#include "parity/server_fixture.hpp"
#include "parity/socket_io.hpp"
#include "parity/transcript.hpp"
#include "parity/v2_fixture.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "./integ/server_ready.hpp"
#include "./littletest.hpp"

#ifdef PARITY_TRANSCRIPT_DIR
#define PARITY_TRANSCRIPTS PARITY_TRANSCRIPT_DIR
#else
#define PARITY_TRANSCRIPTS "parity/transcripts"
#endif

namespace {

using parity::expect_kind;
using parity::expectation;
using parity::normalized_exchange;
using parity::normalize;
using parity::observed_response;
using parity::response_frame_parser;
using parity::tcp_client;
using parity::tcase;
using parity::transcript;

struct run_config {
    bool record = false;
    bool list = false;
    std::string filter;
};

run_config g_cfg;

// ------------------------------------------------------------------
// Case-shape helpers.
// ------------------------------------------------------------------

// The Nth `expect status`/`expect status_line` selects the Nth response
// (normative cursor rule); other response-scoped expects attach to the
// current response without advancing the cursor.
std::size_t count_responses(const tcase& c) {
    std::size_t n = 0;
    for (const expectation& e : c.expects) {
        if (e.kind == expect_kind::status || e.kind == expect_kind::status_line) ++n;
    }
    return n;
}

bool has_connection_expect(const tcase& c, const char* token) {
    for (const expectation& e : c.expects) {
        if (e.kind == expect_kind::connection && e.value == token) return true;
    }
    return false;
}

// Who terminates the connection at case end. Default "client": the
// harness closes after observations. "server": the harness waits for
// the server's EOF (bounded by the read deadline) before closing.
std::string closer_for(const tcase& c) {
    for (auto it = c.expects.rbegin(); it != c.expects.rend(); ++it) {
        if (it->kind == expect_kind::closer) return it->value;
    }
    return "client";
}

// Extract "METHOD /path" from the first send segment (curl transport).
bool parse_request_line(const tcase& c, std::string& method, std::string& path) {
    if (c.sends.empty()) return false;
    const std::string& first = c.sends[0].bytes;
    std::size_t sp1 = first.find(' ');
    if (sp1 == std::string::npos) return false;
    std::size_t sp2 = first.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return false;
    method = first.substr(0, sp1);
    path = first.substr(sp1 + 1, sp2 - sp1 - 1);
    return !method.empty() && !path.empty() && path[0] == '/';
}

// ------------------------------------------------------------------
// Raw-socket case execution.
// ------------------------------------------------------------------

struct raw_case_result {
    std::vector<observed_response> responses;
    // Responses observed before the connection-outcome phase (the
    // keep-alive probe response is never part of the case).
    std::size_t case_responses = 0;
    std::string connection_outcome;  // "ok" | "closed" | error text
    std::string error;
};

// Reads until `need` responses completed (or EOF flushes an
// until-close response). All framing comes from the wire.
raw_case_result run_raw_case(uint16_t port, const tcase& c) {
    raw_case_result out;
    tcp_client sock;
    if (!sock.connect(port, c.read_timeout_ms)) {
        out.error = "connect failed";
        return out;
    }
    response_frame_parser parser;
    // HEAD requests get headers-only responses (RFC 7231 §4.3.2).
    if (!c.sends.empty() && c.sends[0].bytes.compare(0, 5, "HEAD ") == 0) {
        parser.set_head_only(true);
    }
    std::size_t need = count_responses(c);
    std::string chunk;

    for (const parity::send_segment& seg : c.sends) {
        if (!sock.write_all(seg.bytes, c.read_timeout_ms)) {
            out.error = "write failed";
            return out;
        }
    }
    // A client half-close is deliberately NOT performed: the v2
    // baseline treats a client FIN after a request as connection
    // termination and never answers (MHD-artifact, recorded in the
    // parity inventory). The wire's own framing delimits responses.

    while (true) {
        if (parser.failed()) {
            out.error = "framing error: " + parser.error();
            return out;
        }
        if (out.responses.size() >= need && !parser.pending_until_close()) break;
        auto status = sock.read_some(chunk, c.read_timeout_ms);
        if (status == tcp_client::READ_DATA) {
            for (observed_response& r : parser.feed(chunk)) {
                out.responses.push_back(std::move(r));
            }
            chunk.clear();
            continue;
        }
        if (status == tcp_client::READ_EOF) {
            for (observed_response& r : parser.finish()) {
                out.responses.push_back(std::move(r));
            }
            break;
        }
        out.error = "read timeout waiting for response framing";
        return out;
    }
    if (parser.failed()) {
        out.error = "framing error: " + parser.error();
        return out;
    }
    out.case_responses = out.responses.size();

    // Connection-outcome phase.
    if (has_connection_expect(c, "keep-alive")) {
        const char* probe =
            "GET /__parity_probe HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
        if (!sock.write_all(probe, c.read_timeout_ms)) {
            out.connection_outcome = "probe write failed";
            return out;
        }
        std::string chunk;
        while (true) {
            auto status = sock.read_some(chunk, c.read_timeout_ms);
            bool got_more = false;
            for (observed_response& r : parser.feed(chunk)) {
                out.responses.push_back(std::move(r));
                got_more = true;
            }
            chunk.clear();
            if (got_more) break;
            if (status == tcp_client::READ_EOF) {
                out.connection_outcome = "server closed before probe response";
                return out;
            }
            if (status == tcp_client::READ_ERROR) {
                out.connection_outcome = "no response to keep-alive probe";
                return out;
            }
            if (parser.failed()) {
                out.connection_outcome = "framing error: " + parser.error();
                return out;
            }
        }
        out.connection_outcome = "ok";
    } else if (has_connection_expect(c, "close")) {
        std::string chunk;
        while (true) {
            auto status = sock.read_some(chunk, c.read_timeout_ms);
            for (observed_response& r : parser.feed(chunk)) {
                out.responses.push_back(std::move(r));
            }
            chunk.clear();
            if (status == tcp_client::READ_EOF) {
                out.connection_outcome = "closed";
                break;
            }
            if (status == tcp_client::READ_ERROR) {
                out.connection_outcome = "server did not close within deadline";
                return out;
            }
            if (out.responses.size() > need) {
                out.connection_outcome = "expected close, got another response";
                return out;
            }
        }
    }
    if (closer_for(c) == "server") {
        std::string chunk;
        while (true) {
            auto status = sock.read_some(chunk, c.read_timeout_ms);
            for (observed_response& r : parser.feed(chunk)) {
                out.responses.push_back(std::move(r));
            }
            chunk.clear();
            if (status == tcp_client::READ_EOF) break;
            if (status == tcp_client::READ_ERROR) {
                out.connection_outcome = "server never closed";
                return out;
            }
        }
    }
    return out;
}

// ------------------------------------------------------------------
// Expectation comparison (raw transport).
// ------------------------------------------------------------------

std::string compare_case(const tcase& c, const raw_case_result& result,
                         const std::string& body_file_base) {
    if (!result.error.empty()) {
        return "transport error: " + result.error;
    }
    std::vector<normalized_exchange> exchanges;
    for (const observed_response& r : result.responses) {
        exchanges.push_back(normalize(r));
    }
    if (exchanges.empty()) {
        return "no response observed";
    }

    for (const expectation& e : c.expects) {
        if (e.kind != expect_kind::connection) continue;
        bool want_close = e.value == "close";
        bool observed_closed = result.connection_outcome == "closed";
        if (want_close != observed_closed) {
            return "expect-line=" + std::to_string(e.line) +
                   ": expected connection " + e.value + ", observed \"" +
                   result.connection_outcome + "\"";
        }
    }

    // Response-scoped expectations with the Nth-status cursor rule.
    std::size_t response_index = 0;
    bool first_response_scoped = true;
    for (const expectation& e : c.expects) {
        bool advances = e.kind == expect_kind::status ||
                        e.kind == expect_kind::status_line;
        if (advances && !first_response_scoped) {
            ++response_index;
        }
        if (advances) first_response_scoped = false;
        if (e.kind == expect_kind::connection || e.kind == expect_kind::closer) {
            continue;
        }
        if (response_index >= exchanges.size()) {
            return "expect-line=" + std::to_string(e.line) + ": expected " +
                   std::to_string(response_index + 1) +
                   "th response but only " +
                   std::to_string(exchanges.size()) + " observed";
        }
        parity::match_result m =
            parity::check_expectation(e, exchanges[response_index], body_file_base);
        if (!m.ok) return m.diff;
    }
    return "";
}

// ------------------------------------------------------------------
// curl-mediated case execution (TLS / auth round-trips).
// ------------------------------------------------------------------

struct curl_case_result {
    long status = 0;
    std::string body;
    std::vector<parity::observed_header> headers;
    std::string error;
};

std::size_t curl_body_sink(char* ptr, std::size_t size, std::size_t nmemb,
                           std::string* s) {
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

std::size_t curl_header_sink(char* ptr, std::size_t size, std::size_t nmemb,
                             void* userdata) {
    auto* headers = static_cast<std::vector<parity::observed_header>*>(userdata);
    std::string line(ptr, size * nmemb);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }
    std::size_t colon = line.find(':');
    if (colon != std::string::npos) {
        std::string value = line.substr(colon + 1);
        std::size_t begin = value.find_first_not_of(" \t");
        if (begin == std::string::npos) value.clear();
        else value.erase(0, begin);
        headers->push_back({line.substr(0, colon), std::move(value)});
    }
    return size * nmemb;
}

curl_case_result run_curl_case(uint16_t port, const tcase& c) {
    curl_case_result out;
    std::string method, path;
    if (!parse_request_line(c, method, path)) {
        out.error = "curl transport requires a first send segment with a request line";
        return out;
    }
    const std::string url = (c.curl_tls ? "https://" : "http://") +
                            std::string("127.0.0.1:") + std::to_string(port) + path;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        out.error = "curl init failed";
        return out;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &curl_body_sink);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &curl_header_sink);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &out.headers);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                     static_cast<long>(c.read_timeout_ms));
    if (c.curl_tls) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    if (!c.curl_user.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, c.curl_user.c_str());
        std::string pass = c.curl_user;
        std::size_t colon = pass.find(':');
        pass = colon == std::string::npos ? "" : pass.substr(colon + 1);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, pass.c_str());
        long auth = (c.curl_auth == "digest") ? CURLAUTH_DIGEST : CURLAUTH_BASIC;
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, auth);
    }
    CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out.status);
    } else {
        out.error = std::string("curl error: ") + curl_easy_strerror(rc);
    }
    curl_easy_cleanup(curl);
    return out;
}

// curl transport: single response; expectations checked against it.
std::string compare_curl_case(const tcase& c, const curl_case_result& result,
                              const std::string& body_file_base) {
    if (!result.error.empty()) return "transport error: " + result.error;
    if (count_responses(c) > 1) {
        return "curl transport supports single-response cases only";
    }
    observed_response r;
    r.raw_status_line = "HTTP/1.1 " + std::to_string(result.status);
    r.status = static_cast<int>(result.status);
    r.headers = result.headers;
    r.body = result.body;
    r.framing = "curl";
    normalized_exchange ex = normalize(r);
    for (const expectation& e : c.expects) {
        if (e.kind == expect_kind::connection || e.kind == expect_kind::closer ||
            e.kind == expect_kind::framing) {
            continue;
        }
        if (e.kind == expect_kind::status_line) {
            // curl does not expose the raw reason phrase; compare code.
            expectation relaxed = e;
            relaxed.kind = expect_kind::status;
            relaxed.number = r.status;
            parity::match_result m = parity::check_expectation(relaxed, ex, body_file_base);
            if (!m.ok) return m.diff;
            continue;
        }
        parity::match_result m = parity::check_expectation(e, ex, body_file_base);
        if (!m.ok) return m.diff;
    }
    return "";
}

// ------------------------------------------------------------------
// Record mode (D4: record-then-curate).
// ------------------------------------------------------------------

std::string record_case(const tcase& c, const raw_case_result& result) {
    std::ostringstream ss;
    for (std::size_t i = 0; i < result.case_responses; ++i) {
        const observed_response& r = result.responses[i];
        normalized_exchange ex = normalize(r);
        ss << "expect status " << ex.status << "\n";
        if (!ex.status_line.empty() &&
            ex.status_line.compare(0, 5, "HTTP/") != 0) {
            ss << "expect status_line \"" << parity::escape(ex.status_line)
               << "\"\n";
        }
        for (const auto& h : ex.headers) {
            ss << "expect header " << h.name << ": " << parity::escape(h.value)
               << "\n";
        }
        ss << "expect body \"" << parity::escape(ex.body) << "\"\n";
        ss << "expect framing " << ex.framing << "\n";
    }
    ss << "# connection_outcome: " << result.connection_outcome << "\n";
    return ss.str();
}

std::string record_curl_case(const curl_case_result& result) {
    std::ostringstream ss;
    ss << "expect status " << result.status << "\n";
    for (const auto& h : result.headers) {
        ss << "expect header " << h.name << ": " << parity::escape(h.value) << "\n";
    }
    ss << "expect body \"" << parity::escape(result.body) << "\"\n";
    return ss.str();
}

// ------------------------------------------------------------------
// Per-transcript driver (one littletest auto-test per corpus file).
// ------------------------------------------------------------------

struct transcript_outcome {
    bool skipped = false;
    std::string skip_reason;
    // "transcript=<file> case=<name> expect-line=<n>: <diff>" on failure.
    std::string failure;
    // Cases actually executed (list/record/compare modes).
    std::size_t cases_run = 0;
};

transcript_outcome run_transcript(const std::string& file_name) {
    transcript_outcome out;
    if (!g_cfg.filter.empty() &&
        file_name.find(g_cfg.filter) == std::string::npos) {
        out.skipped = true;
        out.skip_reason = "excluded by filter";
        return out;
    }
    const std::string path = std::string(PARITY_TRANSCRIPTS) + "/" + file_name;
    transcript t = parity::parse_transcript_file(path);
    parity::v2_fixture fixture;

    if (!fixture.profile_available(t.profile)) {
        out.skipped = true;
        out.skip_reason = "profile " + t.profile + " unavailable in this build";
        return out;
    }
    uint16_t port = fixture.start(t.profile);
    httpserver_test::wait_for_server_ready(static_cast<int>(port));

    const std::string body_file_base =
        std::string(PARITY_TRANSCRIPTS) + "/..";

    std::string record_path = path + ".recorded";
    std::ofstream record_out;
    if (g_cfg.record) record_out.open(record_path, std::ios::binary);

    for (const tcase& c : t.cases) {
        if (g_cfg.list) {
            std::cout << file_name << ":" << c.name << std::endl;
            ++out.cases_run;
            continue;
        }
        std::string failure;
        if (c.transport == "curl") {
            curl_case_result r = run_curl_case(port, c);
            if (g_cfg.record) {
                record_out << "case " << c.name << "\n"
                           << record_curl_case(r) << "\n";
                continue;
            }
            failure = compare_curl_case(c, r, body_file_base);
            ++out.cases_run;
        } else {
            raw_case_result r = run_raw_case(port, c);
            if (g_cfg.record) {
                record_out << "case " << c.name << "\n"
                           << record_case(c, r) << "\n";
                continue;
            }
            failure = compare_case(c, r, body_file_base);
            ++out.cases_run;
        }
        if (!failure.empty()) {
            fixture.stop();
            out.failure = "transcript=" + file_name + " case=" + c.name +
                          " " + failure;
            return out;
        }
    }
    fixture.stop();
    return out;
}

}  // namespace

LT_BEGIN_SUITE(transcript_runner_suite)

    void set_up() { }
    void tear_down() { }

LT_END_SUITE(transcript_runner_suite)

// One auto-test per corpus file keeps failure attribution per
// transcript area; LT_SKIP / LT_FAIL must expand inside a test body,
// so the driver returns an outcome that each test maps to its outcome.
#define PARITY_CORPUS_TEST(test_name, file)                                  \
    LT_BEGIN_AUTO_TEST(transcript_runner_suite, test_name)                   \
        transcript_outcome outcome = run_transcript(file);                   \
        if (outcome.skipped) { LT_SKIP(outcome.skip_reason); }               \
        /* braces required: LT_FAIL expands to several statements */         \
        if (!outcome.failure.empty()) { LT_FAIL(outcome.failure); }          \
        LT_CHECK(outcome.cases_run > 0); /* a corpus file runs its cases */  \
    LT_END_AUTO_TEST(test_name)

PARITY_CORPUS_TEST(run_corpus_routing, "routing.tseq")
PARITY_CORPUS_TEST(run_corpus_hooks, "hooks.tseq")
PARITY_CORPUS_TEST(run_corpus_auth_basic, "auth_basic.tseq")
PARITY_CORPUS_TEST(run_corpus_auth_digest, "auth_digest.tseq")
PARITY_CORPUS_TEST(run_corpus_forms, "forms.tseq")
PARITY_CORPUS_TEST(run_corpus_file_resp, "file_resp.tseq")
PARITY_CORPUS_TEST(run_corpus_ip_controls, "ip_controls.tseq")
PARITY_CORPUS_TEST(run_corpus_shoutcast, "shoutcast.tseq")
PARITY_CORPUS_TEST(run_corpus_websocket, "websocket.tseq")
PARITY_CORPUS_TEST(run_corpus_tls, "tls.tseq")

// argv is parsed before the auto-runner so --record/--list/--filter and
// a positional file filter apply to every corpus test.
int main(int argc, char** argv) {
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--record") {
            g_cfg.record = true;
        } else if (arg == "--list") {
            g_cfg.list = true;
        } else if (arg == "--filter" && i + 1 < argc) {
            g_cfg.filter = argv[++i];
        } else {
            positional.push_back(arg);
        }
    }
    if (g_cfg.filter.empty() && !positional.empty()) {
        g_cfg.filter = positional.front();
    }
    for (littletest::test_base* t : littletest::auto_test_vector) {
        littletest::auto_test_runner(t);
    }
    return littletest::auto_test_runner();
}
