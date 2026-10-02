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

// TASK-117 step 3: temp_file_part_sink -- the v2 disk-upload port
// (PRD-V3N-REQ-021/025, DR-V3-001). Driven through the real
// decode_multipart driver (the sink is exercised exactly the way the
// streaming read drives it); the members live in the library
// (detail/forms_multipart_files.cpp in v3core), so this suite links
// libhttpserver.la (default LDADD). The suite pins:
//   - file part bytes land on disk byte-identical (NUL and high-bit
//     included), with the completed ledger recording name, client
//     filename, path, Content-Type, and size; field parts land in
//     take_fields();
//   - random names are unique across parts and requests;
//   - sanitized-name mode ports v2's sanitize_upload_filename: the
//     basename survives (any path prefix stripped, so traversal
//     neutralized), an empty or "."/".." basename and an embedded NUL
//     are typed rejections, and a leftover file at the destination is
//     truncated, not appended;
//   - an abort mid-part removes the partial file (and the removal is
//     idempotent at destruction);
//   - destruction removes every unclaimed completed file and consults
//     should_keep EXACTLY ONCE per completed file (null or false ->
//     removed, true -> kept, throws -> removed -- v2 parity).

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/forms/multipart.hpp>
#include <httpserver/forms/urlencoded.hpp>
#include <httpserver/http/outcome.hpp>

#include "./littletest.hpp"

namespace {

namespace forms = httpserver::forms;
namespace http = httpserver::http;

constexpr const char* k_type = "multipart/form-data; boundary=P9B";

// One scratch directory per case (mkdtemp), removed on destruction.
class scratch_dir {
 public:
    scratch_dir() {
        char tmpl[] = "/tmp/lht-multipart-XXXXXX";
        char* made = mkdtemp(tmpl);
        if (made != nullptr) path_ = made;
    }

    ~scratch_dir() {
        if (path_.empty()) return;
        for (const std::string& name : entries()) {
            std::remove((path_ + "/" + name).c_str());
        }
        std::remove(path_.c_str());
    }

    const std::string& path() const noexcept { return path_; }

    std::vector<std::string> entries() const {
        std::vector<std::string> out;
        DIR* dir = opendir(path_.c_str());
        if (dir == nullptr) return out;
        while (dirent* entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name != "." && name != "..") out.push_back(name);
        }
        closedir(dir);
        return out;
    }

    bool has(const std::string& name) const {
        struct stat st;
        const std::string full = path_ + "/" + name;
        return ::stat(full.c_str(), &st) == 0;
    }

    std::size_t size_of(const std::string& name) const {
        struct stat st;
        const std::string full = path_ + "/" + name;
        if (::stat(full.c_str(), &st) != 0) {
            return static_cast<std::size_t>(-1);
        }
        return static_cast<std::size_t>(st.st_size);
    }

 private:
    std::string path_;
};

std::string read_file(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return "<missing>";
    std::string out;
    char buf[512];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    std::fclose(f);
    return out;
}

std::string write_scratch(const std::string& path, const std::string& data) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return "cannot create " + path;
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
    return "";
}

std::vector<std::byte> bytes_of(const std::string& s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(std::byte(static_cast<unsigned char>(c)));
    }
    return out;
}

struct part_spec {
    std::string name;
    std::string filename;               // empty = field part
    const char* content_type = nullptr; // null = no part Content-Type
    std::string data;
};

// A complete multipart body over P9B: every part framed, the final
// boundary closed.
std::string body_of(const std::vector<part_spec>& parts) {
    std::string out;
    for (const part_spec& p : parts) {
        out += "--P9B\r\nContent-Disposition: form-data; name=\"" + p.name
            + "\"";
        if (!p.filename.empty()) {
            out += "; filename=\"" + p.filename + "\"";
        }
        out += "\r\n";
        if (p.content_type != nullptr) {
            out += std::string("Content-Type: ") + p.content_type + "\r\n";
        }
        out += "\r\n" + p.data + "\r\n";
    }
    out += "--P9B--\r\n";
    return out;
}

// The same body with the final boundary withheld: the last part began
// and never ends (the abort scenario).
std::string body_truncated(const std::vector<part_spec>& parts) {
    std::string out;
    for (const part_spec& p : parts) {
        out += "--P9B\r\nContent-Disposition: form-data; name=\"" + p.name
            + "\"";
        if (!p.filename.empty()) {
            out += "; filename=\"" + p.filename + "\"";
        }
        out += "\r\n";
        if (p.content_type != nullptr) {
            out += std::string("Content-Type: ") + p.content_type + "\r\n";
        }
        out += "\r\n" + p.data + "\r\n";
    }
    return out;
}

// Decodes @p body through the real driver into @p sink; "" iff ok.
std::string drive(forms::part_sink& sink, const std::string& body) {
    forms::multipart_read read;
    const http::outcome decoded = forms::decode_multipart(
        bytes_of(body), std::optional<std::string_view>(k_type),
        forms::multipart_limits{}, sink, read);
    if (decoded.ok() != read.ok()) return "verdict disagreement";
    if (!decoded.ok()) return "decode: " + decoded.message();
    return "";
}

// "" iff the decode failed with @p code.
std::string drive_fails(forms::part_sink& sink, const std::string& body,
                        http::outcome_code code) {
    forms::multipart_read read;
    const http::outcome decoded = forms::decode_multipart(
        bytes_of(body), std::optional<std::string_view>(k_type),
        forms::multipart_limits{}, sink, read);
    if (decoded.ok()) return "decode unexpectedly succeeded";
    if (decoded.code() != code) return "wrong code";
    return "";
}

// "" iff exactly one file remains in @p dir and its bytes are @p want.
std::string one_file_diff(const scratch_dir& dir,
                          const std::string& want) {
    const std::vector<std::string> names = dir.entries();
    if (names.size() != 1) {
        return "entries " + std::to_string(names.size()) + " in " + dir.path();
    }
    const std::string got = read_file(dir.path() + "/" + names[0]);
    if (got != want) return "content mismatch";
    return "";
}

}  // namespace

LT_BEGIN_SUITE(forms_multipart_files_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(forms_multipart_files_suite)

// (1) File bytes land on disk byte-identical (NUL and high-bit
// included) with the completed ledger recording the metadata; field
// parts land in take_fields().
LT_BEGIN_AUTO_TEST(forms_multipart_files_suite, file_lands_and_ledgers)
    scratch_dir dir;
    LT_CHECK(!dir.path().empty());
    std::string payload(1, '\0');
    payload += "bin\xff";
    {
        forms::temp_file_part_sink sink(forms::temp_file_options{dir.path()});
        const std::string diff = drive(sink, body_of({
            part_spec{"doc", "a.bin", "application/octet-stream", payload},
            part_spec{"note", "", nullptr, "hello"},
        }));
        if (!diff.empty()) std::cerr << "[ledger] " << diff << std::endl;
        LT_CHECK(diff.empty());
        LT_CHECK(sink.completed().size() == static_cast<std::size_t>(1));
        if (sink.completed().size() == 1) {
            const forms::part_file_info& info = sink.completed()[0];
            LT_CHECK(info.name == "doc");
            LT_CHECK(info.filename == "a.bin");
            LT_CHECK(info.file_size
                     == static_cast<std::uint64_t>(payload.size()));
            LT_CHECK(info.content_type == "application/octet-stream");
            LT_CHECK(info.file_system_file_name.size()
                     > dir.path().size() + 1);
            LT_CHECK(read_file(info.file_system_file_name) == payload);
        }
        const forms::form_fields fields = sink.take_fields();
        LT_CHECK(fields.value("note").value_or("") == "hello");
    }
    // Unclaimed at destruction: the file is gone.
    LT_CHECK(dir.entries().empty());
LT_END_AUTO_TEST(file_lands_and_ledgers)

// (2) Random names are unique across parts and requests.
LT_BEGIN_AUTO_TEST(forms_multipart_files_suite, random_names_unique)
    scratch_dir dir;
    std::string first_path;
    std::string second_path;
    {
        forms::temp_file_part_sink sink(forms::temp_file_options{dir.path()});
        LT_CHECK(drive(sink, body_of({
            part_spec{"a", "one.bin", nullptr, "1"},
            part_spec{"b", "two.bin", nullptr, "2"},
        })).empty());
        LT_CHECK(sink.completed().size() == static_cast<std::size_t>(2));
        if (sink.completed().size() == 2) {
            first_path = sink.completed()[0].file_system_file_name;
            second_path = sink.completed()[1].file_system_file_name;
            LT_CHECK(first_path != second_path);
        }
    }
    {
        forms::temp_file_part_sink sink(forms::temp_file_options{dir.path()});
        LT_CHECK(drive(sink, body_of(
            {part_spec{"a", "one.bin", nullptr, "1"}})).empty());
        LT_CHECK(sink.completed().size() == static_cast<std::size_t>(1));
        if (!first_path.empty() && sink.completed().size() == 1) {
            // Unique across requests too.
            LT_CHECK(sink.completed()[0].file_system_file_name != first_path);
            LT_CHECK(sink.completed()[0].file_system_file_name
                     != second_path);
        }
    }
LT_END_AUTO_TEST(random_names_unique)

// (3) Sanitized-name mode: the basename survives (path prefixes
// stripped -- traversal neutralized), leftovers are truncated not
// appended, and unusable basenames are typed rejections.
LT_BEGIN_AUTO_TEST(forms_multipart_files_suite, sanitized_names)
    scratch_dir dir;
    // The options carry a move-only callback slot: build fresh copies.
    const auto make_options = [&dir] {
        forms::temp_file_options options;
        options.directory = dir.path();
        options.random_names = false;
        return options;
    };
    {
        forms::temp_file_part_sink sink(make_options());
        LT_CHECK(drive(sink, body_of({
            part_spec{"a", "sub/dir/report.txt", nullptr, "fresh"},
        })).empty());
        LT_CHECK(dir.entries().size() == static_cast<std::size_t>(1));
        LT_CHECK(dir.has("report.txt"));
        LT_CHECK(read_file(dir.path() + "/report.txt") == "fresh");
    }

    // A pre-existing leftover at the destination is truncated.
    LT_CHECK(write_scratch(dir.path() + "/report.txt",
                           "leftover junk that must go").empty());
    {
        forms::temp_file_part_sink sink(make_options());
        LT_CHECK(drive(sink, body_of({
            part_spec{"a", "../report.txt", nullptr, "new"},
        })).empty());
        LT_CHECK(read_file(dir.path() + "/report.txt") == "new");
    }

    // Unusable basenames: typed invalid_argument, no file created
    // (the directory is empty by now: the previous sink's destruction
    // already claimed the completed report.txt).
    // (reason: the sanitizer rejects "." and ".." basenames and names
    // with embedded NULs, exactly like v2's sanitize_upload_filename.)
    {
        forms::temp_file_part_sink sink(make_options());
        LT_CHECK(drive_fails(sink, body_of(
                                  {part_spec{"a", "..", nullptr, "x"}}),
                             http::outcome_code::invalid_argument).empty());
        LT_CHECK(dir.entries().empty());
    }
    {
        forms::temp_file_part_sink sink(make_options());
        LT_CHECK(drive_fails(sink, body_of(
                                  {part_spec{"a", "up/..", nullptr, "x"}}),
                             http::outcome_code::invalid_argument).empty());
        LT_CHECK(dir.entries().empty());
    }
    {
        forms::temp_file_part_sink sink(make_options());
        // An embedded NUL would silently truncate at the OS open()
        // (CWE-626): rejected like v2.
        const std::string nul_name("ok\0.php", 7);
        LT_CHECK(drive_fails(sink, body_of(
                                  {part_spec{"a", nul_name, nullptr, "x"}}),
                             http::outcome_code::invalid_argument).empty());
        LT_CHECK(dir.entries().empty());
    }
LT_END_AUTO_TEST(sanitized_names)

// (4) An abort mid-part removes the partial file, idempotently at
// destruction.
LT_BEGIN_AUTO_TEST(forms_multipart_files_suite, abort_removes_partial)
    scratch_dir dir;
    {
        forms::temp_file_part_sink sink(forms::temp_file_options{dir.path()});
        const std::string truncated = body_truncated(
            {part_spec{"doc", "a.bin", "application/octet-stream",
                       "partial bytes"}});
        LT_CHECK(drive_fails(sink, truncated,
                             http::outcome_code::invalid_argument).empty());
        // The abort already removed the partial file.
        LT_CHECK(dir.entries().empty());
        LT_CHECK(sink.completed().empty());
        LT_CHECK(sink.take_fields().entries().empty());
    }
    // Destruction after the abort leaves nothing (idempotent).
    LT_CHECK(dir.entries().empty());
LT_END_AUTO_TEST(abort_removes_partial)

// (5) should_keep is consulted EXACTLY ONCE per completed file:
// null and false remove, true keeps, a throwing callback removes (the
// v2 file_cleanup_callback rules).
LT_BEGIN_AUTO_TEST(forms_multipart_files_suite, keep_callback_once)
    const std::string body = body_of(
        {part_spec{"doc", "a.bin", nullptr, "kept"}});

    // No callback: removed.
    {
        scratch_dir dir;
        forms::temp_file_part_sink sink(forms::temp_file_options{dir.path()});
        LT_CHECK(drive(sink, body).empty());
        LT_CHECK(dir.entries().size() == static_cast<std::size_t>(1));
        // Destruction consults nothing and removes.
    }

    // false: removed; true: kept; throws: removed.
    {
        scratch_dir dir;
        forms::temp_file_options options;
        options.directory = dir.path();
        int calls = 0;
        options.should_keep = [&calls](const std::string& name,
                                       const std::string& filename,
                                       const forms::part_file_info&) {
            ++calls;
            if (name != "doc" || filename != "a.bin") throw std::logic_error(
                "bad identity");
            return false;
        };
        {
            forms::temp_file_part_sink sink(std::move(options));
            LT_CHECK(drive(sink, body).empty());
            LT_CHECK_EQ(calls, 0);  // only consulted at destruction
        }
        LT_CHECK_EQ(calls, 1);
        LT_CHECK(dir.entries().empty());
    }
    {
        scratch_dir dir;
        forms::temp_file_options options;
        options.directory = dir.path();
        options.should_keep = [](const std::string&,
                                 const std::string&,
                                 const forms::part_file_info&) {
            return true;
        };
        {
            forms::temp_file_part_sink sink(std::move(options));
            LT_CHECK(drive(sink, body).empty());
        }
        LT_CHECK(dir.entries().size() == static_cast<std::size_t>(1));
        LT_CHECK(read_file(dir.path() + "/" + dir.entries()[0]) == "kept");
    }
    {
        scratch_dir dir;
        forms::temp_file_options options;
        options.directory = dir.path();
        options.should_keep = [](const std::string&,
                                 const std::string&,
                                 const forms::part_file_info&) -> bool {
            throw std::runtime_error("keeper exploded");
        };
        {
            forms::temp_file_part_sink sink(std::move(options));
            LT_CHECK(drive(sink, body).empty());
        }
        LT_CHECK(dir.entries().empty());
    }
LT_END_AUTO_TEST(keep_callback_once)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
