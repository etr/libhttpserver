/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_UNIT_QUIC_CRYPTO_TEST_SUPPORT_HPP_
#define TEST_UNIT_QUIC_CRYPTO_TEST_SUPPORT_HPP_
#include <stdexcept>
#include <string_view>
#include <vector>
#include <httpserver/detail/quic_crypto.hpp>
#include "./quic_codec_test_support.hpp"
inline std::vector<std::byte> hex(std::string_view text) {
    std::vector<std::byte> result;
    unsigned value = 0, count = 0;
    for (char c : text) {
        if (c == ' ' || c == '\n') continue;
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else throw std::invalid_argument("invalid hex fixture");
        value = value * 16 + digit;
        if (++count == 2) {
            result.push_back(std::byte(value));
            count = value = 0;
        }
    }
    if (count) throw std::invalid_argument("odd hex fixture");
    return result;
}
inline bool equals(std::span<const std::byte> actual, std::string_view expected) {
    auto bytes = hex(expected);
    return std::equal(actual.begin(), actual.end(), bytes.begin(), bytes.end());
}
inline bool zeros(std::span<const std::byte> bytes) {
    return std::all_of(bytes.begin(), bytes.end(), [](auto b) { return b == std::byte{0}; });
}
struct cleanse_receipt {
    std::size_t releases = 0;
    bool clean = true;
    static void observe(std::span<const std::byte> bytes, void* argument) noexcept {
        auto& receipt = *static_cast<cleanse_receipt*>(argument);
        ++receipt.releases;
        receipt.clean &= zeros(bytes);
    }
};
// RFC 9001 Appendix A fixed wire fixtures.
inline constexpr std::string_view client_payload =
    "060040f1010000ed0303ebf8fa56f12939b9584a3896472ec40bb863cfd3e868"
    "04fe3a47f06a2b69484c00000413011302010000c000000010000e00000b6578616d706c652e636f6dff01000100000a00080006001d001700180010"
    "0007000504616c706e000500050100000000003300260024001d00209370b2c9caa47fbabaf4559fedba753de171fa71f50f1ce15d43e994ec74d748"
    "002b0003020304000d0010000e0403050306030203080408050806002d00020101001c00024001003900320408ffffffffffffffff05048000ffff07"
    "048000ffff0801100104800075300901100f088394c8f03e51570806048000ffff";
inline constexpr std::string_view client_packet =
    "c000000001088394c8f03e5157080000449e7b9aec34d1b1c98dd7689fb8ec11d242b123dc9bd8bab936b47d92ec356c0bab7df5976d27cd449f6330"
    "0099f3991c260ec4c60d17b31f8429157bb35a1282a643a8d2262cad67500cadb8e7378c8eb7539ec4d4905fed1bee1fc8aafba17c750e2c7ace01e6"
    "005f80fcb7df621230c83711b39343fa028cea7f7fb5ff89eac2308249a02252155e2347b63d58c5457afd84d05dfffdb20392844ae812154682e9cf"
    "012f9021a6f0be17ddd0c2084dce25ff9b06cde535d0f920a2db1bf362c23e596d11a4f5a6cf3948838a3aec4e15daf8500a6ef69ec4e3feb6b1d98e"
    "610ac8b7ec3faf6ad760b7bad1db4ba3485e8a94dc250ae3fdb41ed15fb6a8e5eba0fc3dd60bc8e30c5c4287e53805db059ae0648db2f64264ed5e39"
    "be2e20d82df566da8dd5998ccabdae053060ae6c7b4378e846d29f37ed7b4ea9ec5d82e7961b7f25a9323851f681d582363aa5f89937f5a67258bf63"
    "ad6f1a0b1d96dbd4faddfcefc5266ba6611722395c906556be52afe3f565636ad1b17d508b73d8743eeb524be22b3dcbc2c7468d54119c7468449a13"
    "d8e3b95811a198f3491de3e7fe942b330407abf82a4ed7c1b311663ac69890f4157015853d91e923037c227a33cdd5ec281ca3f79c44546b9d90ca00"
    "f064c99e3dd97911d39fe9c5d0b23a229a234cb36186c4819e8b9c5927726632291d6a418211cc2962e20fe47feb3edf330f2c603a9d48c0fcb5699d"
    "bfe5896425c5bac4aee82e57a85aaf4e2513e4f05796b07ba2ee47d80506f8d2c25e50fd14de71e6c418559302f939b0e1abd576f279c4b2e0feb85c"
    "1f28ff18f58891ffef132eef2fa09346aee33c28eb130ff28f5b766953334113211996d20011a198e3fc433f9f2541010ae17c1bf202580f6047472f"
    "b36857fe843b19f5984009ddc324044e847a4f4a0ab34f719595de37252d6235365e9b84392b061085349d73203a4a13e96f5432ec0fd4a1ee65accd"
    "d5e3904df54c1da510b0ff20dcc0c77fcb2c0e0eb605cb0504db87632cf3d8b4dae6e705769d1de354270123cb11450efc60ac47683d7b8d0f811365"
    "565fd98c4c8eb936bcab8d069fc33bd801b03adea2e1fbc5aa463d08ca19896d2bf59a071b851e6c239052172f296bfb5e72404790a2181014f3b94a"
    "4e97d117b438130368cc39dbb2d198065ae3986547926cd2162f40a29f0c3c8745c0f50fba3852e566d44575c29d39a03f0cda721984b6f440591f35"
    "5e12d439ff150aab7613499dbd49adabc8676eef023b15b65bfc5ca06948109f23f350db82123535eb8a7433bdabcb909271a6ecbcb58b936a88cd4e"
    "8f2e6ff5800175f113253d8fa9ca8885c2f552e657dc603f252e1a8e308f76f0be79e2fb8f5d5fbbe2e30ecadd220723c8c0aea8078cdfcb3868263f"
    "f8f0940054da48781893a7e49ad5aff4af300cd804a6b6279ab3ff3afb64491c85194aab760d58a606654f9f4400e8b38591356fbf6425aca26dc852"
    "44259ff2b19c41b9f96f3ca9ec1dde434da7d2d392b905ddf3d1f9af93d1af5950bd493f5aa731b4056df31bd267b6b90a079831aaf579be0a390131"
    "37aac6d404f518cfd46840647e78bfe706ca4cf5e9c5453e9f7cfd2b8b4c8d169a44e55c88d4a9a7f9474241e221af44860018ab0856972e194cd934";
inline constexpr std::string_view server_payload =
    "02000000000600405a020000560303eefce7f7b37ba1d1632e96677825ddf73988cfc79825df566dc5430b9a045a1200130100002e00330024001d00"
    "209d3c940d89690b84d08a60993c144eca684d1081287c834d5311bcf32bb9da1a002b00020304";
inline constexpr std::string_view server_packet =
    "cf000000010008f067a5502a4262b5004075c0d95a482cd0991cd25b0aac406a5816b6394100f37a1c69797554780bb38cc5a99f5ede4cf73c3ec249"
    "3a1839b3dbcba3f6ea46c5b7684df3548e7ddeb9c3bf9c73cc3f3bded74b562bfb19fb84022f8ef4cdd93795d77d06edbb7aaf2f58891850abbdca3d"
    "20398c276456cbc42158407dd074ee";
inline constexpr std::string_view retry_packet =
    "ff000000010008f067a5502a4262b5746f6b656e04a265ba2eff4d829058fb3f0f2496ba";
#endif  // TEST_UNIT_QUIC_CRYPTO_TEST_SUPPORT_HPP_
