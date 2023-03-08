/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

#include <curl/curl.h>
#include <sys/stat.h>
#include <cassert>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>

#include "./httpserver.hpp"
#include "httpserver/string_utilities.hpp"
#include "./littletest.hpp"

using std::string;
using std::string_view;
using std::map;
using std::shared_ptr;
using std::vector;
using std::stringstream;

using httpserver::http_resource;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::string_response;
using httpserver::file_response;
using httpserver::webserver;
using httpserver::create_webserver;
using httpserver::http::arg_map;

static const char* TEST_CONTENT_FILENAME = "test_content";
static const char* TEST_CONTENT_FILEPATH = "./test_content";
static const char* FILENAME_IN_GET_CONTENT = "filename=\"test_content\"";
static const char* TEST_CONTENT = "test content of file\n";
static const char* TEST_KEY = "file";
static size_t TEST_CONTENT_SIZE = 21;

static const char* TEST_CONTENT_FILENAME_2 = "test_content_2";
static const char* TEST_CONTENT_FILEPATH_2 = "./test_content_2";
static const char* FILENAME_IN_GET_CONTENT_2 = "filename=\"test_content_2\"";
static const char* TEST_CONTENT_2 = "test content of second file\n";
static const char* TEST_KEY_2 = "file2";
static size_t TEST_CONTENT_SIZE_2 = 28;

static const char* TEST_PARAM_KEY = "param_key";
static const char* TEST_PARAM_VALUE = "Value of test param";

// The large file test_content_large is large enough to ensure
// that MHD splits the underlying request into several chunks.
static const char* LARGE_FILENAME_IN_GET_CONTENT = "filename=\"test_content_large\"";
static const char* LARGE_CONTENT_FILEPATH = "./test_content_large";
static const char* LARGE_KEY = "large_file";

static bool file_exists(const string &path) {
    struct stat sb;

    return (stat(path.c_str(), &sb) == 0);
}

static CURLcode send_file_to_webserver(bool add_second_file, bool append_parameters) {
    curl_global_init(CURL_GLOBAL_ALL);

    CURL *curl = curl_easy_init();

    curl_mime *form = curl_mime_init(curl);
    curl_mimepart *field = curl_mime_addpart(form);
    curl_mime_name(field, TEST_KEY);
    curl_mime_filedata(field, TEST_CONTENT_FILEPATH);
    if (add_second_file) {
        field = curl_mime_addpart(form);
        curl_mime_name(field, TEST_KEY_2);
        curl_mime_filedata(field, TEST_CONTENT_FILEPATH_2);
    }

    if (append_parameters) {
        field = curl_mime_addpart(form);
        curl_mime_name(field, TEST_PARAM_KEY);
        curl_mime_data(field, TEST_PARAM_VALUE, CURL_ZERO_TERMINATED);
    }

    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/upload");
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);

    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    curl_mime_free(form);
    return res;
}

static CURLcode send_large_file(string* content, std::string args = "") {
    // Generate a large (100K) file of random bytes. Upload the file with
    // a curl request, then delete the file. The default chunk size of MHD
    // appears to be around 16K, so 100K should be enough to trigger the
    // behavior. Return the content via the pointer parameter so that test
    // cases can make required checks for the content.
    curl_global_init(CURL_GLOBAL_ALL);

    CURL *curl = curl_easy_init();

    curl_mime *form = curl_mime_init(curl);
    curl_mimepart *field = curl_mime_addpart(form);

    std::ifstream infile(LARGE_CONTENT_FILEPATH);
    std::stringstream buffer;
    buffer << infile.rdbuf();
    infile.close();
    *content = buffer.str();

    curl_mime_name(field, LARGE_KEY);
    curl_mime_filedata(field, LARGE_CONTENT_FILEPATH);

    std::string url = "localhost:8080/upload";
    if (!args.empty()) {
        url.append(args);
    }
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);

    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    curl_mime_free(form);

    return res;
}

static bool send_file_via_put() {
    curl_global_init(CURL_GLOBAL_ALL);

    CURL *curl;
    CURLcode res;
    struct stat file_info;
    FILE *fd;

    fd = fopen(TEST_CONTENT_FILEPATH, "rb");
    if (!fd) {
        return false;
    }

    if (fstat(fileno(fd), &file_info) != 0) {
        return false;
    }

    curl = curl_easy_init();
    if (!curl) {
        fclose(fd);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/upload");
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READDATA, fd);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t) file_info.st_size);

    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    fclose(fd);
    if (res == CURLE_OK) {
        return true;
    }
    return false;
}

class print_file_upload_resource : public http_resource {
 public:
     shared_ptr<http_response> render_POST(const http_request& req) {
         content = req.get_content();
         auto args_view = req.get_args();
         // req may go out of scope, so we need to copy the values.
         for (auto const& item : args_view) {
            for (auto const & value : item.second.get_all_values()) {
                args[string(item.first)].push_back(string(value));
            }
         }
         files = req.get_files();
         shared_ptr<string_response> hresp(new string_response("OK", 201, "text/plain"));
         return hresp;
     }

     shared_ptr<http_response> render_PUT(const http_request& req) {
         content = req.get_content();
         auto args_view = req.get_args();
         // req may go out of scope, so we need to copy the values.
         for (auto const& item : args_view) {
            for (auto const & value : item.second.get_all_values()) {
                args[string(item.first)].push_back(string(value));
            }
         }
         files = req.get_files();
         shared_ptr<string_response> hresp(new string_response("OK", 200, "text/plain"));
         return hresp;
     }

     const std::map<string, std::vector<string>, httpserver::http::arg_comparator> get_args() const {
         return args;
     }

     const map<string, map<string, httpserver::http::file_info>> get_files() const {
          return files;
     }

     const string get_content() const {
         return content;
     }

 private:
     std::map<std::string, std::vector<std::string>, httpserver::http::arg_comparator> args;
     map<string, map<string, httpserver::http::file_info>> files;
     string content;
};

LT_BEGIN_SUITE(file_upload_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(file_upload_suite)

LT_BEGIN_AUTO_TEST(file_upload_suite, file_upload_memory_and_disk)
    string upload_directory = ".";
    webserver* ws;

    ws = new webserver(create_webserver(8080)
                       .put_processed_data_to_content()
                       .file_upload_target(httpserver::FILE_UPLOAD_MEMORY_AND_DISK)
                       .file_upload_dir(upload_directory)
                       .generate_random_filename_on_upload());
    ws->start(false);
    LT_CHECK_EQ(ws->is_running(), true);

    print_file_upload_resource resource;
    ws->register_resource("upload", &resource);

    CURLcode res = send_file_to_webserver(false, false);
    LT_ASSERT_EQ(res, 0);

    ws->stop();
    delete ws;

    string actual_content = resource.get_content();
    LT_CHECK_EQ(actual_content.find(FILENAME_IN_GET_CONTENT) != string::npos, true);
    LT_CHECK_EQ(actual_content.find(TEST_CONTENT) != string::npos, true);

    auto args = resource.get_args();
    LT_CHECK_EQ(args.size(), 1);
    auto arg = args.begin();
    LT_CHECK_EQ(arg->first, TEST_KEY);
    LT_CHECK_EQ(arg->second[0], TEST_CONTENT);

    map<string, map<string, httpserver::http::file_info>> files = resource.get_files();
    LT_CHECK_EQ(files.size(), 1);
    map<string, map<string, httpserver::http::file_info>>::iterator file_key = files.begin();
    LT_CHECK_EQ(file_key->first, TEST_KEY);
    LT_CHECK_EQ(file_key->second.size(), 1);
    map<string, httpserver::http::file_info>::iterator file = file_key->second.begin();
    LT_CHECK_EQ(file->first, TEST_CONTENT_FILENAME);
    LT_CHECK_EQ(file->second.get_file_size(), TEST_CONTENT_SIZE);
    LT_CHECK_EQ(file->second.get_content_type(), httpserver::http::http_utils::application_octet_stream);

    string expected_filename = upload_directory +
                               httpserver::http::http_utils::path_separator +
                               httpserver::http::http_utils::upload_filename_template;
    LT_CHECK_EQ(file->second.get_file_system_file_name().substr(0, file->second.get_file_system_file_name().size() - 6),
                expected_filename.substr(0, expected_filename.size() - 6));
    LT_CHECK_EQ(file_exists(file->second.get_file_system_file_name()), false);
LT_END_AUTO_TEST(file_upload_memory_and_disk)

LT_BEGIN_AUTO_TEST(file_upload_suite, file_upload_memory_and_disk_via_put)
    string upload_directory = ".";
    webserver* ws;

    ws = new webserver(create_webserver(8080)
                       .put_processed_data_to_content()
                       .file_upload_target(httpserver::FILE_UPLOAD_MEMORY_AND_DISK)
                       .file_upload_dir(upload_directory)
                       .generate_random_filename_on_upload());
    ws->start(false);
    LT_CHECK_EQ(ws->is_running(), true);

    print_file_upload_resource resource;
    ws->register_resource("upload", &resource);

    int ret = send_file_via_put();
    LT_ASSERT_EQ(ret, true);

    string actual_content = resource.get_content();
    LT_CHECK_EQ(actual_content, TEST_CONTENT);

    auto args = resource.get_args();
    LT_CHECK_EQ(args.size(), 0);

    map<string, map<string, httpserver::http::file_info>> files = resource.get_files();
    LT_CHECK_EQ(files.size(), 0);

    ws->stop();
    delete ws;
LT_END_AUTO_TEST(file_upload_memory_and_disk_via_put)

LT_BEGIN_AUTO_TEST(file_upload_suite, file_upload_memory_and_disk_additional_params)
    string upload_directory = ".";
    webserver* ws;

    ws = new webserver(create_webserver(8080)
                       .put_processed_data_to_content()
                       .file_upload_target(httpserver::FILE_UPLOAD_MEMORY_AND_DISK)
                       .file_upload_dir(upload_directory)
                       .generate_random_filename_on_upload());
    ws->start(false);
    LT_CHECK_EQ(ws->is_running(), true);

    print_file_upload_resource resource;
    ws->register_resource("upload", &resource);

    CURLcode res = send_file_to_webserver(false, true);
    LT_ASSERT_EQ(res, 0);

    ws->stop();
    delete ws;

    string actual_content = resource.get_content();
    LT_CHECK_EQ(actual_content.find(FILENAME_IN_GET_CONTENT) != string::npos, true);
    LT_CHECK_EQ(actual_content.find(TEST_CONTENT) != string::npos, true);
    LT_CHECK_EQ(actual_content.find(TEST_PARAM_KEY) != string::npos, true);
    LT_CHECK_EQ(actual_content.find(TEST_PARAM_VALUE) != string::npos, true);

    auto args = resource.get_args();
    LT_CHECK_EQ(args.size(), 2);
    auto arg = args.begin();
    LT_CHECK_EQ(arg->first, TEST_KEY);
    LT_CHECK_EQ(arg->second[0], TEST_CONTENT);
    arg++;
    LT_CHECK_EQ(arg->first, TEST_PARAM_KEY);
    LT_CHECK_EQ(arg->second[0], TEST_PARAM_VALUE);

    map<string, map<string, httpserver::http::file_info>> files = resource.get_files();
    LT_CHECK_EQ(files.size(), 1);
    map<string, map<string, httpserver::http::file_info>>::iterator file_key = files.begin();
    LT_CHECK_EQ(file_key->first, TEST_KEY);
    LT_CHECK_EQ(file_key->second.size(), 1);
    map<string, httpserver::http::file_info>::iterator file = file_key->second.begin();
    LT_CHECK_EQ(file->first, TEST_CONTENT_FILENAME);
    LT_CHECK_EQ(file->second.get_file_size(), TEST_CONTENT_SIZE);
    LT_CHECK_EQ(file->second.get_content_type(), httpserver::http::http_utils::application_octet_stream);

    string expected_filename = upload_directory +
                               httpserver::http::http_utils::path_separator +
                               httpserver::http::http_utils::upload_filename_template;
    LT_CHECK_EQ(file->second.get_file_system_file_name().substr(0, file->second.get_file_system_file_name().size() - 6),
                expected_filename.substr(0, expected_filename.size() - 6));
    LT_CHECK_EQ(file_exists(file->second.get_file_system_file_name()), false);
LT_END_AUTO_TEST(file_upload_memory_and_disk_additional_params)

LT_BEGIN_AUTO_TEST(file_upload_suite, file_upload_memory_and_disk_two_files)
    string upload_directory = ".";
    webserver* ws;

    ws = new webserver(create_webserver(8080)
                       .put_processed_data_to_content()
                       .file_upload_target(httpserver::FILE_UPLOAD_MEMORY_AND_DISK)
                       .file_upload_dir(upload_directory)
                       .generate_random_filename_on_upload());
    ws->start(false);
    LT_CHECK_EQ(ws->is_running(), true);

    print_file_upload_resource resource;
    ws->register_resource("upload", &resource);

    CURLcode res = send_file_to_webserver(true, false);
    LT_ASSERT_EQ(res, 0);

    ws->stop();
    delete ws;

    string actual_content = resource.get_content();
    LT_CHECK_EQ(actual_content.find(FILENAME_IN_GET_CONTENT) != string::npos, true);
    LT_CHECK_EQ(actual_content.find(TEST_CONTENT) != string::npos, true);
    LT_CHECK_EQ(actual_content.find(FILENAME_IN_GET_CONTENT_2) != string::npos, true);
    LT_CHECK_EQ(actual_content.find(TEST_CONTENT_2) != string::npos, true);

    auto args = resource.get_args();
    LT_CHECK_EQ(args.size(), 2);
    auto arg = args.begin();
    LT_CHECK_EQ(arg->first, TEST_KEY);
    LT_CHECK_EQ(arg->second[0], TEST_CONTENT);
    arg++;
    LT_CHECK_EQ(arg->first, TEST_KEY_2);
    LT_CHECK_EQ(arg->second[0], TEST_CONTENT_2);

    map<string, map<string, httpserver::http::file_info>> files = resource.get_files();
    LT_CHECK_EQ(files.size(), 2);
    map<string, map<string, httpserver::http::file_info>>::iterator file_key = files.begin();
    LT_CHECK_EQ(file_key->first, TEST_KEY);
    LT_CHECK_EQ(file_key->second.size(), 1);
    map<string, httpserver::http::file_info>::iterator file = file_key->second.begin();
    LT_CHECK_EQ(file->first, TEST_CONTENT_FILENAME);
    LT_CHECK_EQ(file->second.get_file_size(), TEST_CONTENT_SIZE);
    LT_CHECK_EQ(file->second.get_content_type(), httpserver::http::http_utils::application_octet_stream);

    string expected_filename = upload_directory +
                               httpserver::http::http_utils::path_separator +
                               httpserver::http::http_utils::upload_filename_template;
    LT_CHECK_EQ(file->second.get_file_system_file_name().substr(0, file->second.get_file_system_file_name().size() - 6),
                expected_filename.substr(0, expected_filename.size() - 6));
    LT_CHECK_EQ(file_exists(file->second.get_file_system_file_name()), false);

    file_key++;
    LT_CHECK_EQ(file_key->first, TEST_KEY_2);
    LT_CHECK_EQ(file_key->second.size(), 1);
    file = file_key->second.begin();
    LT_CHECK_EQ(file->first, TEST_CONTENT_FILENAME_2);
    LT_CHECK_EQ(file->second.get_file_size(), TEST_CONTENT_SIZE_2);
    LT_CHECK_EQ(file->second.get_content_type(), httpserver::http::http_utils::application_octet_stream);

    expected_filename = upload_directory +
                               httpserver::http::http_utils::path_separator +
                               httpserver::http::http_utils::upload_filename_template;
    LT_CHECK_EQ(file->second.get_file_system_file_name().substr(0, file->second.get_file_system_file_name().size() - 6),
                expected_filename.substr(0, expected_filename.size() - 6));
    LT_CHECK_EQ(file_exists(file->second.get_file_system_file_name()), false);
LT_END_AUTO_TEST(file_upload_memory_and_disk_two_files)

LT_BEGIN_AUTO_TEST(file_upload_suite, file_upload_disk_only)
    string upload_directory = ".";
    webserver* ws;

    ws = new webserver(create_webserver(8080)
                       .no_put_processed_data_to_content()
                       .file_upload_target(httpserver::FILE_UPLOAD_DISK_ONLY)
                       .file_upload_dir(upload_directory)
                       .generate_random_filename_on_upload());
    ws->start(false);
    LT_CHECK_EQ(ws->is_running(), true);

    print_file_upload_resource resource;
    ws->register_resource("upload", &resource);

    CURLcode res = send_file_to_webserver(false, false);
    LT_ASSERT_EQ(res, 0);

    ws->stop();
    delete ws;

    string actual_content = resource.get_content();
    LT_CHECK_EQ(actual_content.size(), 0);

    auto args = resource.get_args();
    LT_CHECK_EQ(args.size(), 0);

    map<string, map<string, httpserver::http::file_info>> files = resource.get_files();
    LT_CHECK_EQ(files.size(), 1);
    map<string, map<string, httpserver::http::file_info>>::iterator file_key = files.begin();
    LT_CHECK_EQ(file_key->first, TEST_KEY);
    LT_CHECK_EQ(file_key->second.size(), 1);
    map<string, httpserver::http::file_info>::iterator file = file_key->second.begin();
    LT_CHECK_EQ(file->first, TEST_CONTENT_FILENAME);
    LT_CHECK_EQ(file->second.get_file_size(), TEST_CONTENT_SIZE);
    LT_CHECK_EQ(file->second.get_content_type(), httpserver::http::http_utils::application_octet_stream);

    string expected_filename = upload_directory +
                               httpserver::http::http_utils::path_separator +
                               httpserver::http::http_utils::upload_filename_template;
    LT_CHECK_EQ(file->second.get_file_system_file_name().substr(0, file->second.get_file_system_file_name().size() - 6),
                expected_filename.substr(0, expected_filename.size() - 6));
    LT_CHECK_EQ(file_exists(file->second.get_file_system_file_name()), false);
LT_END_AUTO_TEST(file_upload_disk_only)

LT_BEGIN_AUTO_TEST(file_upload_suite, file_upload_memory_only_incl_content)
    string upload_directory = ".";
    webserver* ws;

    ws = new webserver(create_webserver(8080)
                       .put_processed_data_to_content()
                       .file_upload_target(httpserver::FILE_UPLOAD_MEMORY_ONLY));
    ws->start(false);
    LT_CHECK_EQ(ws->is_running(), true);

    print_file_upload_resource resource;
    ws->register_resource("upload", &resource);

    CURLcode res = send_file_to_webserver(false, false);
    LT_ASSERT_EQ(res, 0);

    string actual_content = resource.get_content();
    LT_CHECK_EQ(actual_content.find(FILENAME_IN_GET_CONTENT) != string::npos, true);
    LT_CHECK_EQ(actual_content.find(TEST_CONTENT) != string::npos, true);

    auto args = resource.get_args();
    LT_CHECK_EQ(args.size(), 1);
    auto arg = args.begin();
    LT_CHECK_EQ(arg->first, TEST_KEY);
    LT_CHECK_EQ(arg->second[0], TEST_CONTENT);

    map<string, map<string, httpserver::http::file_info>> files = resource.get_files();
    LT_CHECK_EQ(resource.get_files().size(), 0);

    ws->stop();
    delete ws;
LT_END_AUTO_TEST(file_upload_memory_only_incl_content)

LT_BEGIN_AUTO_TEST(file_upload_suite, file_upload_large_content)
    string upload_directory = ".";
    webserver* ws;

    ws = new webserver(create_webserver(8080)
                       .put_processed_data_to_content()
                       .file_upload_target(httpserver::FILE_UPLOAD_MEMORY_ONLY));
    ws->start(false);
    LT_CHECK_EQ(ws->is_running(), true);

    print_file_upload_resource resource;
    ws->register_resource("upload", &resource);

    // Upload a large file to trigger the chunking behavior of MHD.
    std::string file_content;
    CURLcode res = send_large_file(&file_content);
    LT_ASSERT_EQ(res, 0);

    string actual_content = resource.get_content();
    LT_CHECK_EQ(actual_content.find(LARGE_FILENAME_IN_GET_CONTENT) != string::npos, true);
    LT_CHECK_EQ(actual_content.find(file_content) != string::npos, true);

    // The chunks of the file should be concatenated into the first
    // arg value of the key.
    auto const args = resource.get_args();
    LT_CHECK_EQ(args.size(), 1);
    auto const file_arg_iter = args.find(std::string_view(LARGE_KEY));
    if (file_arg_iter == args.end()) {
        LT_FAIL("file arg not found");
    }
    LT_CHECK_EQ(file_arg_iter->second.size(), 1);
    LT_CHECK_EQ(file_arg_iter->second[0], file_content);

    map<string, map<string, httpserver::http::file_info>> files = resource.get_files();
    LT_CHECK_EQ(resource.get_files().size(), 0);

    ws->stop();
    delete ws;
LT_END_AUTO_TEST(file_upload_large_content)

LT_BEGIN_AUTO_TEST(file_upload_suite, file_upload_large_content_with_args)
    string upload_directory = ".";
    webserver* ws;

    ws = new webserver(create_webserver(8080)
                       .put_processed_data_to_content()
                       .file_upload_target(httpserver::FILE_UPLOAD_MEMORY_ONLY));
    ws->start(false);
    LT_CHECK_EQ(ws->is_running(), true);

    print_file_upload_resource resource;
    ws->register_resource("upload", &resource);

    // Upload a large file to trigger the chunking behavior of MHD.
    // Include some additional args to make sure those are processed as well.
    std::string file_content;
    CURLcode res = send_large_file(&file_content, "?arg1=hello&arg1=world");
    LT_ASSERT_EQ(res, 0);

    string actual_content = resource.get_content();
    LT_CHECK_EQ(actual_content.find(LARGE_FILENAME_IN_GET_CONTENT) != string::npos, true);
    LT_CHECK_EQ(actual_content.find(file_content) != string::npos, true);

    auto const args = resource.get_args();
    LT_CHECK_EQ(args.size(), 2);
    auto const file_arg_iter = args.find(std::string_view(LARGE_KEY));
    if (file_arg_iter == args.end()) {
        LT_FAIL("file arg not found");
    }
    LT_CHECK_EQ(file_arg_iter->second.size(), 1);
    LT_CHECK_EQ(file_arg_iter->second[0], file_content);
    auto const other_arg_iter = args.find(std::string_view("arg1"));
    if (other_arg_iter == args.end()) {
        LT_FAIL("other arg(s) not found");
    }
    LT_CHECK_EQ(other_arg_iter->second.size(), 2);
    LT_CHECK_EQ(other_arg_iter->second[0], "hello");
    LT_CHECK_EQ(other_arg_iter->second[1], "world");

    map<string, map<string, httpserver::http::file_info>> files = resource.get_files();
    LT_CHECK_EQ(resource.get_files().size(), 0);

    ws->stop();
    delete ws;
LT_END_AUTO_TEST(file_upload_large_content_with_args)

LT_BEGIN_AUTO_TEST(file_upload_suite, file_upload_memory_only_excl_content)
    string upload_directory = ".";
    webserver* ws;

    ws = new webserver(create_webserver(8080)
                       .no_put_processed_data_to_content()
                       .file_upload_target(httpserver::FILE_UPLOAD_MEMORY_ONLY));
    ws->start(false);
    LT_CHECK_EQ(ws->is_running(), true);

    print_file_upload_resource resource;
    ws->register_resource("upload", &resource);

    CURLcode res = send_file_to_webserver(false, false);
    LT_ASSERT_EQ(res, 0);

    string actual_content = resource.get_content();
    LT_CHECK_EQ(actual_content.size(), 0);

    auto args = resource.get_args();
    LT_CHECK_EQ(args.size(), 1);
    auto arg = args.begin();
    LT_CHECK_EQ(arg->first, TEST_KEY);
    LT_CHECK_EQ(arg->second[0], TEST_CONTENT);

    map<string, map<string, httpserver::http::file_info>> files = resource.get_files();
    LT_CHECK_EQ(files.size(), 0);

    ws->stop();
    delete ws;
LT_END_AUTO_TEST(file_upload_memory_only_excl_content)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
