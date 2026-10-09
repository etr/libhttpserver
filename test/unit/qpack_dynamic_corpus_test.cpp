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

#include <array>
#include <string>
#include <utility>
#include <httpserver/detail/qpack_decoder.hpp>
#include <httpserver/detail/qpack_encoder.hpp>
#include <httpserver/detail/qpack_instructions.hpp>
#include "data/qpack/rfc9204.hpp"
#include "./littletest.hpp"
using httpserver::detail::qpack_instruction_limits;
using httpserver::detail::qpack_section_limits;
using httpserver::detail::qpack_field;
using httpserver::detail::qpack_field_view;
using httpserver::detail::qpack_dynamic_table;
using httpserver::detail::qpack_table_role;
using httpserver::detail::qpack_state;
using httpserver::detail::qpack_octets;
using httpserver::detail::qpack_apply_encoder_instruction;
using httpserver::detail::qpack_decoder_instruction_kind;
using httpserver::detail::qpack_decode_decoder_instruction;
using httpserver::detail::qpack_encoder;
using httpserver::detail::qpack_decoder;
namespace {
constexpr qpack_instruction_limits instruction_limits{4096, 1024, 1024};
constexpr qpack_section_limits section_limits{4096, 8192, 100};
std::string unhex(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        out += static_cast<char>((digit(text[i]) << 4) | digit(text[i + 1]));
    }
    return out;
}
bool same(std::span<const qpack_field> owned, std::span<const qpack_field_view> views) {
    if (owned.size() != views.size()) return false;
    for (std::size_t i = 0; i < owned.size(); ++i) {
        if (owned[i].name != views[i].name || owned[i].value != views[i].value || owned[i].never_indexed != views[i].never_indexed) return false;
    }
    return true;
}
}  // namespace
LT_BEGIN_SUITE(corpus_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(corpus_suite)
LT_BEGIN_AUTO_TEST(corpus_suite, appendix_b_continuous_replay_and_delayed_delivery)
    auto budget = httpserver::server::resource_budget::root({});
    qpack_dynamic_table encoder_table(qpack_table_role::encoder), decoder_table(qpack_table_role::decoder);
    LT_CHECK(encoder_table.initialize(budget, 220).ok() && decoder_table.initialize(budget, 220).ok());
    const auto apply = [](std::string_view fixture, qpack_dynamic_table& t) {
        return qpack_apply_encoder_instruction(qpack_octets(unhex(fixture)), instruction_limits, t).status.ok();
    };
    for (auto* t : {&encoder_table, &decoder_table}) {
        LT_CHECK(apply(qpack_fixture::dynamic_capacity, *t));
        LT_CHECK(t->capacity() == 220 && t->bytes() == 0 && t->insert_count() == 0);
        LT_CHECK(apply(qpack_fixture::dynamic_authority, *t));
        LT_CHECK(t->bytes() == 57 && t->lookup(0)->value == "www.example.com");
        LT_CHECK(apply(qpack_fixture::dynamic_path, *t));
        LT_CHECK(t->bytes() == 106 && t->insert_count() == 2 && t->lookup(1)->value == "/sample/path");
    }
    qpack_encoder encoder;
    qpack_decoder decoder;
    const std::array b2 = {qpack_field_view{":authority", "www.example.com"}, qpack_field_view{":path", "/sample/path"}};
    auto section2 = encoder.encode_dynamic(b2, encoder_table, 0, section_limits);
    LT_CHECK(section2.value == unhex(qpack_fixture::dynamic_section) && section2.references.size() == 2);
    LT_CHECK(same(decoder.decode_dynamic(qpack_octets(section2.value), decoder_table, section_limits).fields, b2));
    const auto ack = qpack_decode_decoder_instruction(qpack_octets(unhex("84")));
    LT_CHECK(ack.status.ok() && ack.instruction.kind == qpack_decoder_instruction_kind::section_acknowledgment && ack.instruction.value == 4);
    LT_CHECK(encoder_table.advance_known_received(2).ok());
    section2.references.release();
    for (auto* t : {&encoder_table, &decoder_table}) {
        LT_CHECK(apply(qpack_fixture::dynamic_custom, *t));
        LT_CHECK(t->bytes() == 160 && t->lookup(2)->name == "custom-key" && t->lookup(2)->value == "custom-value");
    }
    const auto increment = qpack_decode_decoder_instruction(qpack_octets(unhex("01")));
    LT_CHECK(increment.status.ok() && encoder_table.increment_known_received(increment.instruction.value).ok());
    LT_CHECK(encoder_table.known_received_count() == 3);
    LT_CHECK(apply(qpack_fixture::dynamic_duplicate, encoder_table));
    LT_CHECK(encoder_table.bytes() == 217 && encoder_table.lookup(3)->value == "www.example.com");
    const std::array b4 = {b2[0], qpack_field_view{":path", "/"}, qpack_field_view{"custom-key", "custom-value"}};
    auto section4 = encoder.encode_dynamic(b4, encoder_table, 4, section_limits);
    LT_CHECK(section4.value == unhex(qpack_fixture::dynamic_duplicate_section) && section4.references.size() == 2);
    LT_CHECK(encoder_table.lookup(3)->references == 1 && encoder_table.lookup(2)->references == 1);
    const auto early = decoder.decode_dynamic(qpack_octets(section4.value), decoder_table, section_limits);
    LT_CHECK(early.status.state == qpack_state::inserts_unavailable && early.required_insert_count == 4 && early.fields.empty());
    LT_CHECK(apply(qpack_fixture::dynamic_duplicate, decoder_table));
    LT_CHECK(same(decoder.decode_dynamic(qpack_octets(section4.value), decoder_table, section_limits).fields, b4));
    const auto cancel = qpack_decode_decoder_instruction(qpack_octets(unhex("48")));
    LT_CHECK(cancel.status.ok() && cancel.instruction.kind == qpack_decoder_instruction_kind::stream_cancellation && cancel.instruction.value == 8);
    section4.references.release();
    LT_CHECK(encoder_table.lookup(3)->references == 0 && encoder_table.known_received_count() == 3);
    for (auto* t : {&encoder_table, &decoder_table}) {
        LT_CHECK(apply(qpack_fixture::dynamic_eviction, *t));
        LT_CHECK(!t->lookup(0) && t->bytes() == 215 && t->size() == 4 && t->insert_count() == 5);
        LT_CHECK(t->lookup(4)->name == "custom-key" && t->lookup(4)->value == "custom-value2");
        LT_CHECK(t->lookup(1)->bytes == 49 && t->lookup(2)->bytes == 54 && t->lookup(3)->bytes == 57 && t->lookup(4)->bytes == 55);
    }
    LT_CHECK(decoder.decode_dynamic(qpack_octets(unhex("060085")), decoder_table, section_limits).status.state == qpack_state::malformed);
    LT_CHECK(!qpack_apply_encoder_instruction(qpack_octets(unhex("05")), instruction_limits, decoder_table).status.ok());
    LT_CHECK(decoder_table.bytes() == 215 && decoder_table.insert_count() == 5);
LT_END_AUTO_TEST(appendix_b_continuous_replay_and_delayed_delivery)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
