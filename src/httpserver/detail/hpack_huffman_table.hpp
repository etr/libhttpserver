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
#if !defined(HTTPSERVER_COMPILATION)
#error "hpack_huffman_table.hpp is internal to libhttpserver"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HPACK_HUFFMAN_TABLE_HPP_
#define SRC_HTTPSERVER_DETAIL_HPACK_HUFFMAN_TABLE_HPP_

#include <array>
#include <cstddef>
#include <cstdint>

namespace httpserver::detail {
struct hpack_huffman_code {
    std::uint32_t code;
    unsigned length;
};
inline constexpr std::array<hpack_huffman_code, 257> hpack_huffman_codes = {{
    {0x1ff8U, 13},  // 0
    {0x7fffd8U, 23},  // 1
    {0xfffffe2U, 28},  // 2
    {0xfffffe3U, 28},  // 3
    {0xfffffe4U, 28},  // 4
    {0xfffffe5U, 28},  // 5
    {0xfffffe6U, 28},  // 6
    {0xfffffe7U, 28},  // 7
    {0xfffffe8U, 28},  // 8
    {0xffffeaU, 24},  // 9
    {0x3ffffffcU, 30},  // 10
    {0xfffffe9U, 28},  // 11
    {0xfffffeaU, 28},  // 12
    {0x3ffffffdU, 30},  // 13
    {0xfffffebU, 28},  // 14
    {0xfffffecU, 28},  // 15
    {0xfffffedU, 28},  // 16
    {0xfffffeeU, 28},  // 17
    {0xfffffefU, 28},  // 18
    {0xffffff0U, 28},  // 19
    {0xffffff1U, 28},  // 20
    {0xffffff2U, 28},  // 21
    {0x3ffffffeU, 30},  // 22
    {0xffffff3U, 28},  // 23
    {0xffffff4U, 28},  // 24
    {0xffffff5U, 28},  // 25
    {0xffffff6U, 28},  // 26
    {0xffffff7U, 28},  // 27
    {0xffffff8U, 28},  // 28
    {0xffffff9U, 28},  // 29
    {0xffffffaU, 28},  // 30
    {0xffffffbU, 28},  // 31
    {0x14U, 6},  // 32
    {0x3f8U, 10},  // 33
    {0x3f9U, 10},  // 34
    {0xffaU, 12},  // 35
    {0x1ff9U, 13},  // 36
    {0x15U, 6},  // 37
    {0xf8U, 8},  // 38
    {0x7faU, 11},  // 39
    {0x3faU, 10},  // 40
    {0x3fbU, 10},  // 41
    {0xf9U, 8},  // 42
    {0x7fbU, 11},  // 43
    {0xfaU, 8},  // 44
    {0x16U, 6},  // 45
    {0x17U, 6},  // 46
    {0x18U, 6},  // 47
    {0x0U, 5},  // 48
    {0x1U, 5},  // 49
    {0x2U, 5},  // 50
    {0x19U, 6},  // 51
    {0x1aU, 6},  // 52
    {0x1bU, 6},  // 53
    {0x1cU, 6},  // 54
    {0x1dU, 6},  // 55
    {0x1eU, 6},  // 56
    {0x1fU, 6},  // 57
    {0x5cU, 7},  // 58
    {0xfbU, 8},  // 59
    {0x7ffcU, 15},  // 60
    {0x20U, 6},  // 61
    {0xffbU, 12},  // 62
    {0x3fcU, 10},  // 63
    {0x1ffaU, 13},  // 64
    {0x21U, 6},  // 65
    {0x5dU, 7},  // 66
    {0x5eU, 7},  // 67
    {0x5fU, 7},  // 68
    {0x60U, 7},  // 69
    {0x61U, 7},  // 70
    {0x62U, 7},  // 71
    {0x63U, 7},  // 72
    {0x64U, 7},  // 73
    {0x65U, 7},  // 74
    {0x66U, 7},  // 75
    {0x67U, 7},  // 76
    {0x68U, 7},  // 77
    {0x69U, 7},  // 78
    {0x6aU, 7},  // 79
    {0x6bU, 7},  // 80
    {0x6cU, 7},  // 81
    {0x6dU, 7},  // 82
    {0x6eU, 7},  // 83
    {0x6fU, 7},  // 84
    {0x70U, 7},  // 85
    {0x71U, 7},  // 86
    {0x72U, 7},  // 87
    {0xfcU, 8},  // 88
    {0x73U, 7},  // 89
    {0xfdU, 8},  // 90
    {0x1ffbU, 13},  // 91
    {0x7fff0U, 19},  // 92
    {0x1ffcU, 13},  // 93
    {0x3ffcU, 14},  // 94
    {0x22U, 6},  // 95
    {0x7ffdU, 15},  // 96
    {0x3U, 5},  // 97
    {0x23U, 6},  // 98
    {0x4U, 5},  // 99
    {0x24U, 6},  // 100
    {0x5U, 5},  // 101
    {0x25U, 6},  // 102
    {0x26U, 6},  // 103
    {0x27U, 6},  // 104
    {0x6U, 5},  // 105
    {0x74U, 7},  // 106
    {0x75U, 7},  // 107
    {0x28U, 6},  // 108
    {0x29U, 6},  // 109
    {0x2aU, 6},  // 110
    {0x7U, 5},  // 111
    {0x2bU, 6},  // 112
    {0x76U, 7},  // 113
    {0x2cU, 6},  // 114
    {0x8U, 5},  // 115
    {0x9U, 5},  // 116
    {0x2dU, 6},  // 117
    {0x77U, 7},  // 118
    {0x78U, 7},  // 119
    {0x79U, 7},  // 120
    {0x7aU, 7},  // 121
    {0x7bU, 7},  // 122
    {0x7ffeU, 15},  // 123
    {0x7fcU, 11},  // 124
    {0x3ffdU, 14},  // 125
    {0x1ffdU, 13},  // 126
    {0xffffffcU, 28},  // 127
    {0xfffe6U, 20},  // 128
    {0x3fffd2U, 22},  // 129
    {0xfffe7U, 20},  // 130
    {0xfffe8U, 20},  // 131
    {0x3fffd3U, 22},  // 132
    {0x3fffd4U, 22},  // 133
    {0x3fffd5U, 22},  // 134
    {0x7fffd9U, 23},  // 135
    {0x3fffd6U, 22},  // 136
    {0x7fffdaU, 23},  // 137
    {0x7fffdbU, 23},  // 138
    {0x7fffdcU, 23},  // 139
    {0x7fffddU, 23},  // 140
    {0x7fffdeU, 23},  // 141
    {0xffffebU, 24},  // 142
    {0x7fffdfU, 23},  // 143
    {0xffffecU, 24},  // 144
    {0xffffedU, 24},  // 145
    {0x3fffd7U, 22},  // 146
    {0x7fffe0U, 23},  // 147
    {0xffffeeU, 24},  // 148
    {0x7fffe1U, 23},  // 149
    {0x7fffe2U, 23},  // 150
    {0x7fffe3U, 23},  // 151
    {0x7fffe4U, 23},  // 152
    {0x1fffdcU, 21},  // 153
    {0x3fffd8U, 22},  // 154
    {0x7fffe5U, 23},  // 155
    {0x3fffd9U, 22},  // 156
    {0x7fffe6U, 23},  // 157
    {0x7fffe7U, 23},  // 158
    {0xffffefU, 24},  // 159
    {0x3fffdaU, 22},  // 160
    {0x1fffddU, 21},  // 161
    {0xfffe9U, 20},  // 162
    {0x3fffdbU, 22},  // 163
    {0x3fffdcU, 22},  // 164
    {0x7fffe8U, 23},  // 165
    {0x7fffe9U, 23},  // 166
    {0x1fffdeU, 21},  // 167
    {0x7fffeaU, 23},  // 168
    {0x3fffddU, 22},  // 169
    {0x3fffdeU, 22},  // 170
    {0xfffff0U, 24},  // 171
    {0x1fffdfU, 21},  // 172
    {0x3fffdfU, 22},  // 173
    {0x7fffebU, 23},  // 174
    {0x7fffecU, 23},  // 175
    {0x1fffe0U, 21},  // 176
    {0x1fffe1U, 21},  // 177
    {0x3fffe0U, 22},  // 178
    {0x1fffe2U, 21},  // 179
    {0x7fffedU, 23},  // 180
    {0x3fffe1U, 22},  // 181
    {0x7fffeeU, 23},  // 182
    {0x7fffefU, 23},  // 183
    {0xfffeaU, 20},  // 184
    {0x3fffe2U, 22},  // 185
    {0x3fffe3U, 22},  // 186
    {0x3fffe4U, 22},  // 187
    {0x7ffff0U, 23},  // 188
    {0x3fffe5U, 22},  // 189
    {0x3fffe6U, 22},  // 190
    {0x7ffff1U, 23},  // 191
    {0x3ffffe0U, 26},  // 192
    {0x3ffffe1U, 26},  // 193
    {0xfffebU, 20},  // 194
    {0x7fff1U, 19},  // 195
    {0x3fffe7U, 22},  // 196
    {0x7ffff2U, 23},  // 197
    {0x3fffe8U, 22},  // 198
    {0x1ffffecU, 25},  // 199
    {0x3ffffe2U, 26},  // 200
    {0x3ffffe3U, 26},  // 201
    {0x3ffffe4U, 26},  // 202
    {0x7ffffdeU, 27},  // 203
    {0x7ffffdfU, 27},  // 204
    {0x3ffffe5U, 26},  // 205
    {0xfffff1U, 24},  // 206
    {0x1ffffedU, 25},  // 207
    {0x7fff2U, 19},  // 208
    {0x1fffe3U, 21},  // 209
    {0x3ffffe6U, 26},  // 210
    {0x7ffffe0U, 27},  // 211
    {0x7ffffe1U, 27},  // 212
    {0x3ffffe7U, 26},  // 213
    {0x7ffffe2U, 27},  // 214
    {0xfffff2U, 24},  // 215
    {0x1fffe4U, 21},  // 216
    {0x1fffe5U, 21},  // 217
    {0x3ffffe8U, 26},  // 218
    {0x3ffffe9U, 26},  // 219
    {0xffffffdU, 28},  // 220
    {0x7ffffe3U, 27},  // 221
    {0x7ffffe4U, 27},  // 222
    {0x7ffffe5U, 27},  // 223
    {0xfffecU, 20},  // 224
    {0xfffff3U, 24},  // 225
    {0xfffedU, 20},  // 226
    {0x1fffe6U, 21},  // 227
    {0x3fffe9U, 22},  // 228
    {0x1fffe7U, 21},  // 229
    {0x1fffe8U, 21},  // 230
    {0x7ffff3U, 23},  // 231
    {0x3fffeaU, 22},  // 232
    {0x3fffebU, 22},  // 233
    {0x1ffffeeU, 25},  // 234
    {0x1ffffefU, 25},  // 235
    {0xfffff4U, 24},  // 236
    {0xfffff5U, 24},  // 237
    {0x3ffffeaU, 26},  // 238
    {0x7ffff4U, 23},  // 239
    {0x3ffffebU, 26},  // 240
    {0x7ffffe6U, 27},  // 241
    {0x3ffffecU, 26},  // 242
    {0x3ffffedU, 26},  // 243
    {0x7ffffe7U, 27},  // 244
    {0x7ffffe8U, 27},  // 245
    {0x7ffffe9U, 27},  // 246
    {0x7ffffeaU, 27},  // 247
    {0x7ffffebU, 27},  // 248
    {0xffffffeU, 28},  // 249
    {0x7ffffecU, 27},  // 250
    {0x7ffffedU, 27},  // 251
    {0x7ffffeeU, 27},  // 252
    {0x7ffffefU, 27},  // 253
    {0x7fffff0U, 27},  // 254
    {0x3ffffeeU, 26},  // 255
    {0x3fffffffU, 30},  // 256
}};

// A full binary prefix tree with 257 leaves has exactly 513 nodes. Construction
// and structural validation happen at compile time; no runtime setup or cache.
struct hpack_huffman_node {
    std::array<int, 2> child = {-1, -1};
    int symbol = -1;
};
struct hpack_huffman_tree {
    std::array<hpack_huffman_node, 513> nodes{};
    std::size_t used = 1;
    bool valid = true;
};
constexpr bool hpack_valid_huffman_code(hpack_huffman_code code) noexcept {
    return code.length >= 5 && code.length <= 30 && code.code < (1U << code.length);
}
consteval bool hpack_insert_huffman_code(hpack_huffman_tree& tree, std::size_t symbol) {
    const auto code = hpack_huffman_codes[symbol];
    if (!hpack_valid_huffman_code(code)) return false;
    int node = 0;
    for (unsigned bit = code.length; bit > 0; --bit) {
        if (tree.nodes[node].symbol != -1) return false;
        auto& next = tree.nodes[node].child[(code.code >> (bit - 1)) & 1];
        if (next == -1) {
            if (tree.used == tree.nodes.size()) return false;
            next = static_cast<int>(tree.used++);
        }
        node = next;
    }
    auto& leaf = tree.nodes[node];
    if (leaf.symbol != -1 || leaf.child[0] != -1 || leaf.child[1] != -1) return false;
    leaf.symbol = static_cast<int>(symbol);
    return true;
}
consteval hpack_huffman_tree hpack_make_huffman_tree() {
    hpack_huffman_tree tree;
    for (std::size_t symbol = 0; symbol < hpack_huffman_codes.size(); ++symbol) {
        if (!hpack_insert_huffman_code(tree, symbol)) {
            tree.valid = false;
            return tree;
        }
    }
    return tree;
}
inline constexpr auto hpack_huffman_trie = hpack_make_huffman_tree();
static_assert(hpack_huffman_trie.valid && hpack_huffman_trie.used == 513);
static_assert(hpack_huffman_codes.size() == 257 && hpack_huffman_codes[256].length == 30
              && hpack_huffman_codes[256].code == 0x3fffffffU);
static_assert(sizeof(hpack_huffman_trie) < 8192);
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HPACK_HUFFMAN_TABLE_HPP_
