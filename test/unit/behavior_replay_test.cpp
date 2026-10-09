/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <parity/behavior_replay.hpp>
#include "./littletest.hpp"

namespace {
parity::tcase sample(std::string wire) {
    parity::tcase test;
    test.sends.push_back({std::move(wire), 1});
    return test;
}
}
LT_BEGIN_SUITE(behavior_replay_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(behavior_replay_suite)

LT_BEGIN_AUTO_TEST(behavior_replay_suite, pipeline_survives_arbitrary_segment_boundaries)
    parity::tcase test;
    const std::string wire = std::string("POST /upload?q=%26 HTTP/1.1\r\nHost: host.test\r\nContent-Length: 3\r\n\r\n")
        + std::string("a\0b", 3) + "GET /hello HTTP/1.1\r\nHost: host.test\r\n\r\n";
    for (const char byte : wire) test.sends.push_back({std::string(1, byte), 1});
    const auto requests = parity::behavior_requests(test);
    LT_ASSERT(requests.size() == 2);
    LT_CHECK_EQ(requests[0].body, std::string("a\0b", 3));
    LT_CHECK_EQ(requests[0].head.raw_target, std::string("/upload?q=%26"));
    LT_CHECK_EQ(requests[0].head.route_path, std::string("/upload"));
    LT_CHECK_EQ(requests[1].head.raw_target, std::string("/hello"));
    LT_CHECK(requests[1].body.empty());
LT_END_AUTO_TEST(pipeline_survives_arbitrary_segment_boundaries)

LT_BEGIN_AUTO_TEST(behavior_replay_suite, live_pipelined_corpus_contains_two_empty_bodies)
    const auto corpus = parity::parse_transcript_file(std::string(PARITY_TRANSCRIPT_DIR) + "/routing.tseq");
    for (const auto& test : corpus.cases) {
        const auto requests = parity::behavior_requests(test);
        LT_CHECK_EQ(requests.size(), test.name == "pipelined_keepalive" ? std::size_t{2} : std::size_t{1});
        for (const auto& request : requests) LT_CHECK(request.body.empty());
    }
LT_END_AUTO_TEST(live_pipelined_corpus_contains_two_empty_bodies)

LT_BEGIN_AUTO_TEST(behavior_replay_suite, forms_corpus_payload_is_one_request)
    const auto corpus = parity::parse_transcript_file(std::string(PARITY_TRANSCRIPT_DIR) + "/forms.tseq");
    for (const auto& test : corpus.cases) {
        const auto requests = parity::behavior_requests(test);
        LT_ASSERT(requests.size() == 1);
        LT_CHECK(!requests[0].body.empty());
        LT_CHECK_EQ(requests[0].body.size(), std::stoul(std::string(*requests[0].head.head_fields.first("Content-Length"))));
    }
LT_END_AUTO_TEST(forms_corpus_payload_is_one_request)

LT_BEGIN_AUTO_TEST(behavior_replay_suite, malformed_or_unbounded_sequences_are_rejected)
    for (const std::string wire : {"GET / HTTP/1.1\r\n", "POST / HTTP/1.1\r\nContent-Length: 5\r\n\r\nx",
            "POST / HTTP/1.1\r\nContent-Length: 1x\r\n\r\nx",
            "POST / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nxx",
            "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n"}) {
        bool rejected = false;
        try {
            parity::behavior_requests(sample(wire));
        } catch (const std::runtime_error&) { rejected = true; }
        LT_CHECK(rejected);
    }
    bool rejected = false;
    try {
        parity::behavior_requests(sample(std::string(1024 * 1024 + 1, 'x')));
    }
    catch (const std::runtime_error&) { rejected = true; }
    LT_CHECK(rejected);
LT_END_AUTO_TEST(malformed_or_unbounded_sequences_are_rejected)

LT_BEGIN_AUTO_TEST(behavior_replay_suite, multiplexed_translation_preserves_application_fields_and_duplicates)
    const auto requests = parity::behavior_requests(sample("POST /echo?q=%26 HTTP/1.1\r\nHost: host.test\r\n"
        "Connection: keep-alive, x-hop\r\nX-Hop: remove\r\nKeep-Alive: 10\r\n"
        "X-Item: first\r\nX-Item: second\r\nContent-Length: 3\r\n\r\nabc"));
    const auto fields = parity::multiplexed_fields(requests.front());
    const std::vector<std::pair<std::string, std::string>> expected{
        {":method", "POST"}, {":scheme", "https"}, {":path", "/echo?q=%26"}, {":authority", "host.test"},
        {"x-item", "first"}, {"x-item", "second"}, {"content-length", "3"}};
    LT_CHECK(fields == expected);
    LT_CHECK_EQ(requests.front().body, std::string("abc"));
LT_END_AUTO_TEST(multiplexed_translation_preserves_application_fields_and_duplicates)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
