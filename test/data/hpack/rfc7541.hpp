// Appendix data from RFC 7541, Copyright (c) 2015 IETF Trust and the
// persons identified as authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
#ifndef TEST_DATA_HPACK_RFC7541_HPP_
#define TEST_DATA_HPACK_RFC7541_HPP_
#include <array>
#include <cstddef>
#include <string_view>

// Published wire bytes from RFC 7541 Appendix C.2-C.6. These fixtures replay
// primitives at explicit offsets, not dynamic-table field sections (TASK-138).
namespace hpack_fixture {
inline constexpr std::array<std::string_view, 16> blocks = {
    "400a637573746f6d2d6b65790d637573746f6d2d686561646572",
    "040c2f73616d706c652f70617468",
    "100870617373776f726406736563726574",
    "82",
    "828684410f7777772e6578616d706c652e636f6d",
    "828684be58086e6f2d6361636865",
    "828785bf400a637573746f6d2d6b65790c637573746f6d2d76616c7565",
    "828684418cf1e3c2e5f23a6ba0ab90f4ff",
    "828684be5886a8eb10649cbf",
    "828785bf408825a849e95ba97d7f8925a849e95bb8e8b4bf",
    "4803333032580770726976617465611d4d6f6e2c203231204f637420323031332032303a31333a323120474d546e1768747470733a2f2f7777772e6578616d706c652e636f6d",
    "4803333037c1c0bf",
    "88c1611d4d6f6e2c203231204f637420323031332032303a31333a323220474d54c05a04677a69707738666f6f3d4153444a4b48514b425a584f5157"
    "454f50495541585157454f49553b206d61782d6167653d333630303b2076657273696f6e3d31",
    "488264025885aec3771a4b6196d07abe941054d444a8200595040b8166e082a62d1bff6e919d29ad171863c78f0b97c8e9ae82ae43d3",
    "4883640effc1c0bf",
    "88c16196d07abe941054d444a8200595040b8166e084a62d1bffc05a839bd9ab77ad94e7821dd7f2e6c7b335dfdfcd5b3960d5af27087f3672c1ab270fb5291f9587316065c003ed4ee5b1063d5007",
};
struct literal {
    std::size_t block;
    std::size_t offset;
    std::size_t wire_size;
    std::string_view text;
};
inline constexpr std::array literals = {
    literal{0, 1, 11, "custom-key"},
    literal{0, 12, 14, "custom-header"},
    literal{1, 1, 13, "/sample/path"},
    literal{2, 1, 9, "password"},
    literal{2, 10, 7, "secret"},
    literal{4, 4, 16, "www.example.com"},
    literal{5, 5, 9, "no-cache"},
    literal{6, 5, 11, "custom-key"},
    literal{6, 16, 13, "custom-value"},
    literal{7, 4, 13, "www.example.com"},
    literal{8, 5, 7, "no-cache"},
    literal{9, 5, 9, "custom-key"},
    literal{9, 14, 10, "custom-value"},
    literal{10, 1, 4, "302"},
    literal{10, 6, 8, "private"},
    literal{10, 15, 30, "Mon, 21 Oct 2013 20:13:21 GMT"},
    literal{10, 46, 24, "https://www.example.com"},
    literal{11, 1, 4, "307"},
    literal{12, 3, 30, "Mon, 21 Oct 2013 20:13:22 GMT"},
    literal{12, 35, 5, "gzip"},
    literal{12, 41, 57, "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"},
    literal{13, 1, 3, "302"},
    literal{13, 5, 6, "private"},
    literal{13, 12, 23, "Mon, 21 Oct 2013 20:13:21 GMT"},
    literal{13, 36, 18, "https://www.example.com"},
    literal{14, 1, 4, "307"},
    literal{15, 3, 23, "Mon, 21 Oct 2013 20:13:22 GMT"},
    literal{15, 28, 4, "gzip"},
    literal{15, 33, 46, "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"},
};
struct integer {
    std::size_t block;
    std::size_t offset;
    unsigned prefix;
    unsigned value;
};
inline constexpr std::array integers = {
    integer{0, 0, 6, 0},
    integer{1, 0, 4, 4},
    integer{2, 0, 4, 0},
    integer{3, 0, 7, 2},
    integer{4, 0, 7, 2},
    integer{4, 1, 7, 6},
    integer{4, 2, 7, 4},
    integer{4, 3, 6, 1},
    integer{5, 0, 7, 2},
    integer{5, 1, 7, 6},
    integer{5, 2, 7, 4},
    integer{5, 3, 7, 62},
    integer{7, 0, 7, 2},
    integer{7, 1, 7, 6},
    integer{7, 2, 7, 4},
    integer{7, 3, 6, 1},
    integer{8, 0, 7, 2},
    integer{8, 1, 7, 6},
    integer{8, 2, 7, 4},
    integer{8, 3, 7, 62},
    integer{6, 0, 7, 2},
    integer{6, 1, 7, 7},
    integer{6, 2, 7, 5},
    integer{6, 3, 7, 63},
    integer{6, 4, 6, 0},
    integer{9, 0, 7, 2},
    integer{9, 1, 7, 7},
    integer{9, 2, 7, 5},
    integer{9, 3, 7, 63},
    integer{9, 4, 6, 0},
    integer{5, 4, 6, 24},
    integer{8, 4, 6, 24},
    integer{10, 0, 6, 8},
    integer{10, 5, 6, 24},
    integer{10, 14, 6, 33},
    integer{10, 45, 6, 46},
    integer{11, 0, 6, 8},
    integer{11, 5, 7, 65},
    integer{11, 6, 7, 64},
    integer{11, 7, 7, 63},
    integer{12, 0, 7, 8},
    integer{12, 1, 7, 65},
    integer{12, 2, 6, 33},
    integer{12, 33, 7, 64},
    integer{12, 34, 6, 26},
    integer{12, 40, 6, 55},
    integer{13, 0, 6, 8},
    integer{13, 4, 6, 24},
    integer{13, 11, 6, 33},
    integer{13, 35, 6, 46},
    integer{14, 0, 6, 8},
    integer{14, 5, 7, 65},
    integer{14, 6, 7, 64},
    integer{14, 7, 7, 63},
    integer{15, 0, 7, 8},
    integer{15, 1, 7, 65},
    integer{15, 2, 6, 33},
    integer{15, 26, 7, 64},
    integer{15, 27, 6, 26},
    integer{15, 32, 6, 55},
};
}  // namespace hpack_fixture
#endif  // TEST_DATA_HPACK_RFC7541_HPP_
