/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <parity/behavior_replay.hpp>
#include <parity/normalize.hpp>
#include <parity/v3_behavior_profiles.hpp>
#include "../integ/digest_client.hpp"
#include "./http2_request_fixture.hpp"
#include "./http3_request_fixture.hpp"
#include "./littletest.hpp"

namespace {
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
namespace http = httpserver::http;
using observation = parity::normalized_exchange;
void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }

class h2_replay {
 public:
    hs::resource_budget budget = h2test::budget();
    parity::behavior_profile profile{budget};
    httpserver::manual_executor executor;
    std::unique_ptr<hd::http2_request_engine> engine;
    hd::hpack_encoder encoder{budget};
    hd::hpack_decoder decoder{budget};
    std::uint32_t next_stream = 1;
    explicit h2_replay(const std::string& name) {
        profile.install(name, PARITY_DATA_ROOT);
        engine = std::make_unique<hd::http2_request_engine>(budget, profile.routes, executor,
            hd::http2_request_limits{}, profile.context(), 77,
            httpserver::net::peer_address{*httpserver::net::parse_address("127.0.0.1"), 4321});
        require(h2test::feed(*engine, h2test::preface()), "HTTP/2 preface");
        h2test::output(*engine);
    }
    observation request(const parity::corpus_request& request) {
        std::vector<hd::hpack_field> fields;
        for (const auto& [name, value] : parity::multiplexed_fields(request)) fields.push_back({name, value});
        const auto stream = next_stream;
        next_stream += 2;
        auto wire = h2test::frame(1, request.body.empty() ? 5 : 4, stream, h2test::encode(encoder, fields));
        if (!request.body.empty()) h2test::append(wire, h2test::frame(0, 1, stream,
            {request.body.begin(), request.body.end()}));
        require(h2test::feed(*engine, wire), "HTTP/2 request feed");
        observation observed;
        observed.framing = "data";
        bool ended = false;
        std::vector<std::uint8_t> block;
        for (unsigned turn = 0; turn < 1000 && !ended; ++turn) {
            executor.run_pending();
            for (const auto& frame : h2test::frames(h2test::output(*engine))) {
                if (frame.type == 3) throw std::runtime_error("HTTP/2 response reset");
                if (frame.stream != stream) continue;
                if (frame.type == 1 || frame.type == 9) {
                    block.insert(block.end(), frame.payload.begin(), frame.payload.end());
                    if (frame.flags & 4) {
                        auto decoded = decoder.decode(block, {65536, 65536, 256});
                        require(decoded.status.ok(), "HTTP/2 response decode");
                        for (const auto& field : decoded.fields) {
                            if (field.name == ":status") observed.status = std::stoi(field.value);
                            else observed.headers.push_back({field.name, field.value});
                        }
                        block.clear();
                    }
                } else if (frame.type == 0) {
                    observed.body.append(frame.payload.begin(), frame.payload.end());
                }
                if ((frame.type == 0 || frame.type == 1) && (frame.flags & 1)) ended = true;
            }
        }
        require(ended && block.empty() && observed.status, "HTTP/2 unfinished response");
        return observed;
    }
};

class h3_replay {
 public:
    h3test::request_fixture transport;
    parity::behavior_profile profile{transport.root};
    std::uint64_t next_stream = 0;
    explicit h3_replay(const std::string& name) {
        profile.install(name, PARITY_DATA_ROOT);
        transport.engine = std::make_unique<hd::http3_request_engine>(transport.pool.data(), transport.pool.critical(),
            transport.flow, transport.recovery, profile.routes, transport.executor, hd::http3_request_limits{}, 77,
            httpserver::net::peer_address{*httpserver::net::parse_address("127.0.0.1"), 4321}, profile.context());
    }
    observation request(const parity::corpus_request& request) {
        std::vector<hd::qpack_field> fields;
        for (const auto& [name, value] : parity::multiplexed_fields(request)) fields.push_back({name, value});
        const auto stream = next_stream;
        next_stream += 4;
        transport.open(stream);
        auto wire = h3test::headers(fields);
        if (!request.body.empty()) {
            auto data = h3test::data(request.body);
            wire.insert(wire.end(), data.begin(), data.end());
        }
        require(transport.feed(stream, wire, true), "HTTP/3 request feed");
        for (unsigned turn = 0; turn < 100 && !transport.fins[stream]; ++turn) {
            transport.executor.run_pending();
            transport.pump(stream);
            transport.drain();
        }
        require(transport.fins[stream] == 1, "HTTP/3 unfinished response");
        const auto response = h3test::decode_response(transport.output[stream]);
        require(response.heads.size() == 1, "HTTP/3 initial headers");
        observation observed;
        observed.framing = "data";
        observed.body = response.body;
        for (const auto& field : response.heads.front()) {
            if (field.name == ":status") observed.status = std::stoi(field.value);
            else observed.headers.push_back({field.name, field.value});
        }
        return observed;
    }
};

std::string compare(const parity::tcase& test, const std::vector<observation>& responses) {
    std::size_t index = 0;
    bool first = true;
    for (auto expected : test.expects) {
        if (expected.kind == parity::expect_kind::status || expected.kind == parity::expect_kind::status_line) {
            if (!first) ++index;
            first = false;
        }
        if (expected.kind == parity::expect_kind::connection || expected.kind == parity::expect_kind::closer) continue;
        if (index >= responses.size()) return "missing response";
        if (expected.kind == parity::expect_kind::framing) {
            if (responses[index].framing != "data") return "missing DATA framing";
            continue;
        }
        if (expected.kind == parity::expect_kind::header && parity::field_lower(expected.name) == "transfer-encoding") {
            if (parity::find_header(responses[index], "transfer-encoding")) return "prohibited transfer-encoding";
            continue;
        }
        if (expected.kind == parity::expect_kind::status_line && expected.value == "ICY 200 OK") {
            expected.kind = parity::expect_kind::status;
            expected.number = 200;
        }
        const auto match = parity::check_expectation(expected, responses[index], PARITY_TRANSCRIPT_DIR);
        if (!match.ok) return match.diff;
    }
    return index + 1 == responses.size() ? "" : "extra responses";
}

template <typename Transport>
void replay_corpus(const char* protocol) {
    for (const char* name : {"routing", "hooks", "auth_basic", "auth_digest", "forms", "file_resp", "ip_controls", "tls", "shoutcast"}) {
        const auto transcript = parity::parse_transcript_file(std::string(PARITY_TRANSCRIPT_DIR) + "/" + name + ".tseq");
        for (const auto& test : transcript.cases) {
            Transport transport(transcript.profile);
            auto requests = parity::behavior_requests(test);
            if (test.curl_auth == "digest") {
                auto challenge_response = transport.request(requests.front());
                const auto* offered = parity::find_header(challenge_response, "WWW-Authenticate");
                require(offered && challenge_response.status == 401, "live Digest challenge");
                auto challenge = httpserver_test::parse_www_authenticate(offered->value);
                require(challenge.has_value(), "Digest challenge parse");
                const std::string cnonce = httpserver_test::make_cnonce();
                const auto response = httpserver_test::compute_response_cleartext(*challenge, httpserver_test::digest_hash::md5,
                    "GET", "/digest", "bob", "builder", cnonce, "00000001");
                requests.front().head.head_fields.append("Authorization", httpserver_test::build_authorization_header(
                    *challenge, "bob", "/digest", cnonce, "00000001", response));
            }
            std::vector<observation> observed;
            for (const auto& request : requests) observed.push_back(transport.request(request));
            const auto diff = compare(test, observed);
            if (!diff.empty()) throw std::runtime_error(std::string(protocol) + " " + name + ":" + test.name + ": " + diff);
            const auto smoke = parity::behavior_requests([] {
                parity::tcase smoke;
                smoke.sends.push_back({"GET /__smoke HTTP/1.1\r\nHost: localhost\r\n\r\n", 1});
                return smoke;
            }());
            auto followup = transport.request(smoke.front());
            require(followup.status == 200 && followup.body == "smoke-ok", "same engine persistence");
            std::cout << "observed engine-frame " << protocol << " " << name << ":" << test.name << "\n";
        }
    }
}
}  // namespace

LT_BEGIN_SUITE(v3_behavior_parity_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(v3_behavior_parity_suite)
LT_BEGIN_AUTO_TEST(v3_behavior_parity_suite, http2_corpus_uses_real_profile_handlers)
    replay_corpus<h2_replay>("HTTP/2");
    LT_CHECK(true);
LT_END_AUTO_TEST(http2_corpus_uses_real_profile_handlers)
LT_BEGIN_AUTO_TEST(v3_behavior_parity_suite, http3_corpus_uses_real_profile_handlers)
    replay_corpus<h3_replay>("HTTP/3");
    LT_CHECK(true);
LT_END_AUTO_TEST(http3_corpus_uses_real_profile_handlers)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
