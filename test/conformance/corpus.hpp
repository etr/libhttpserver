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

#ifndef TEST_CONFORMANCE_CORPUS_HPP_
#define TEST_CONFORMANCE_CORPUS_HPP_
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
namespace protocol_corpus {
struct entry {
    std::string name, rfc, stage, verdict, wire, payload;
};
inline std::vector<entry> load(const std::string& directory) {
    std::ifstream table(directory + "/cases.tsv");
    if (!table) throw std::runtime_error("missing corpus: " + directory);
    std::vector<entry> cases;
    std::string row;
    while (std::getline(table, row)) {
        if (row.empty() || row.front() == '#') continue;
        entry c;
        std::istringstream fields(row);
        if (!std::getline(fields, c.name, '\t') || !std::getline(fields, c.rfc, '\t')
                || !std::getline(fields, c.stage, '\t') || !std::getline(fields, c.verdict, '\t')
                || !std::getline(fields, c.payload)) throw std::runtime_error("bad corpus row: " + row);
        if (c.payload == "-") c.payload.clear();
        std::ifstream input(directory + "/" + c.name + ".wire", std::ios::binary);
        if (!input) throw std::runtime_error("missing case: " + c.name);
        c.wire.assign(std::istreambuf_iterator<char>(input), {});
        if (c.wire.empty()) throw std::runtime_error("empty case: " + c.name);
        cases.push_back(std::move(c));
    }
    if (cases.empty()) throw std::runtime_error("empty corpus: " + directory);
    return cases;
}
inline void require(bool condition, const entry& c, const std::string& assertion) {
    if (!condition) throw std::runtime_error(c.name + " RFC " + c.rfc + ": " + assertion);
}
}  // namespace protocol_corpus
#endif  // TEST_CONFORMANCE_CORPUS_HPP_
