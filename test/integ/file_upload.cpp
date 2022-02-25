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
#include <map>
#include <sstream>
#include <string>

#include "./httpserver.hpp"
#include "httpserver/string_utilities.hpp"
#include "./littletest.hpp"

using std::string;
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

static const char* TEST_CONTENT_FILENAME = "test_content";
static const char* TEST_CONTENT_FILEPATH = "./test_content";
static const char* FILENAME_IN_GET_CONTENT = "filename=\"test_content\"";
static const char* TEST_CONTENT = "test content of file\n";
static const char* TEST_KEY = "file";
static size_t TEST_CONTENT_SIZE = 21;

static CURLcode send_file_to_webserver() {
    curl_global_init(CURL_GLOBAL_ALL);

    CURL *curl = curl_easy_init();

    curl_mime *form = curl_mime_init(curl);
    curl_mimepart *field = curl_mime_addpart(form);
    curl_mime_name(field, TEST_CONTENT);
    curl_mime_filedata(field, TEST_CONTENT_FILEPATH);

    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/upload");
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);

    res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    curl_mime_free(form);
    return res;
}

class print_file_upload_resource : public http_resource {
 public:
     explicit print_file_upload_resource(std::map <std::string, std::string, httpserver::http::arg_comparator>* args,
                                         std::map<std::string, std::map<std::string, httpserver::http::file_info>>* files,
                                         stringstream* content) {
         this->args = args;
         this->content = content;
         this->files = files;
     }

     const shared_ptr<http_response> render_POST(const http_request& req) {
         (*content) << req.get_content();
         (*args) = req.get_args();
         (*files) = req.get_files();
         shared_ptr<string_response> hresp(new string_response("OK", 201, "text/plain"));
         return hresp;
     }

 private:
     std::map <std::string, std::string, httpserver::http::arg_comparator>* args;
     std::map<std::string, std::map<std::string, httpserver::http::file_info>>* files;
     stringstream *content;
};

LT_BEGIN_SUITE(file_upload_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(file_upload_suite)

LT_BEGIN_AUTO_TEST(file_upload_suite, server_runs)

LT_END_AUTO_TEST(server_runs)


LT_BEGIN_AUTO_TEST(file_upload_suite, file_upload_memory_and_disk)
    stringstream content;
    std::map<std::string, std::string, httpserver::http::arg_comparator> args;
    std::map<std::string, std::map<std::string, httpserver::http::file_info>> files;
    string upload_directory = ".";
    webserver* ws;

    ws = new webserver(create_webserver(8080)
                       .put_processed_data_to_content()
                       .file_upload_target(httpserver::FILE_UPLOAD_MEMORY_AND_DISK)
                       .file_upload_dir(upload_directory)
                       .generate_random_filename_on_upload());
    ws->start(false);
    LT_CHECK_EQ(ws->is_running(), true);

    print_file_upload_resource resource(&args, &files, &content);
    ws->register_resource("upload", &resource);

    CURLcode res = send_file_to_webserver();
    LT_ASSERT_EQ(res, 0);

    string actual_content = content.str();
    LT_CHECK_EQ(actual_content.find(FILENAME_IN_GET_CONTENT) != string::npos, true);
    LT_CHECK_EQ(actual_content.find(TEST_CONTENT) != string::npos, true);

    LT_CHECK_EQ(args.size(), 1);
    std::map<std::string, std::string, httpserver::http::arg_comparator>::iterator arg = args.begin();
    LT_CHECK_EQ(arg->first, TEST_KEY);
    LT_CHECK_EQ(arg->second, TEST_CONTENT);

    LT_CHECK_EQ(files.size(), 1);
    std::map<std::string, std::map<std::string, httpserver::http::file_info>>::iterator file_key = files.begin();
    LT_CHECK_EQ(file_key->first, TEST_KEY);
    LT_CHECK_EQ(file_key->second.size(), 1);
    std::map<std::string, httpserver::http::file_info>::iterator file = file_key->second.begin();
    LT_CHECK_EQ(file->first, TEST_CONTENT_FILENAME);
    LT_CHECK_EQ(file->second.get_file_size(), TEST_CONTENT_SIZE);
    LT_CHECK_EQ(file->second.get_content_type(), httpserver::http::http_utils::application_octet_stream);

    string expected_filename = upload_directory + httpserver::http::http_utils::path_separator + httpserver::http::http_utils::upload_filename_template;
    LT_CHECK_EQ(file->second.get_file_system_file_name().substr(0, file->second.get_file_system_file_name().size() - 6), expected_filename.substr(0, expected_filename.size() - 6));
    unlink(file->second.get_file_system_file_name().c_str());

    ws->stop();
    delete ws;
LT_END_AUTO_TEST(file_upload_memory_and_disk)

LT_BEGIN_AUTO_TEST(file_upload_suite, file_upload_disk_only)
    stringstream content;
    std::map<std::string, std::string, httpserver::http::arg_comparator> args;
    std::map<std::string, std::map<std::string, httpserver::http::file_info>> files;
    string upload_directory = ".";
    webserver* ws;

    ws = new webserver(create_webserver(8080)
                       .no_put_processed_data_to_content()
                       .file_upload_target(httpserver::FILE_UPLOAD_DISK_ONLY)
                       .file_upload_dir(upload_directory)
                       .generate_random_filename_on_upload());
    ws->start(false);
    LT_CHECK_EQ(ws->is_running(), true);

    print_file_upload_resource resource(&args, &files, &content);
    ws->register_resource("upload", &resource);

    CURLcode res = send_file_to_webserver();
    LT_ASSERT_EQ(res, 0);

    string actual_content = content.str();
    LT_CHECK_EQ(actual_content.size(), 0);

    LT_CHECK_EQ(args.size(), 0);

    LT_CHECK_EQ(files.size(), 1);
    std::map<std::string, std::map<std::string, httpserver::http::file_info>>::iterator file_key = files.begin();
    LT_CHECK_EQ(file_key->first, TEST_KEY);
    LT_CHECK_EQ(file_key->second.size(), 1);
    std::map<std::string, httpserver::http::file_info>::iterator file = file_key->second.begin();
    LT_CHECK_EQ(file->first, TEST_CONTENT_FILENAME);
    LT_CHECK_EQ(file->second.get_file_size(), TEST_CONTENT_SIZE);
    LT_CHECK_EQ(file->second.get_content_type(), httpserver::http::http_utils::application_octet_stream);

    string expected_filename = upload_directory + httpserver::http::http_utils::path_separator + httpserver::http::http_utils::upload_filename_template;
    LT_CHECK_EQ(file->second.get_file_system_file_name().substr(0, file->second.get_file_system_file_name().size() - 6), expected_filename.substr(0, expected_filename.size() - 6));
    unlink(file->second.get_file_system_file_name().c_str());

    ws->stop();
    delete ws;
LT_END_AUTO_TEST(file_upload_disk_only)

LT_BEGIN_AUTO_TEST(file_upload_suite, file_upload_memory_only_incl_content)
    stringstream content;
    std::map<std::string, std::string, httpserver::http::arg_comparator> args;
    std::map<std::string, std::map<std::string, httpserver::http::file_info>> files;
    string upload_directory = ".";
    webserver* ws;

    ws = new webserver(create_webserver(8080)
                       .put_processed_data_to_content()
                       .file_upload_target(httpserver::FILE_UPLOAD_MEMORY_ONLY));
    ws->start(false);
    LT_CHECK_EQ(ws->is_running(), true);

    print_file_upload_resource resource(&args, &files, &content);
    ws->register_resource("upload", &resource);

    CURLcode res = send_file_to_webserver();
    LT_ASSERT_EQ(res, 0);

    string actual_content = content.str();
    LT_CHECK_EQ(actual_content.find(FILENAME_IN_GET_CONTENT) != string::npos, true);
    LT_CHECK_EQ(actual_content.find(TEST_CONTENT) != string::npos, true);

    LT_CHECK_EQ(args.size(), 1);
    std::map<std::string, std::string, httpserver::http::arg_comparator>::iterator arg = args.begin();
    LT_CHECK_EQ(arg->first, TEST_KEY);
    LT_CHECK_EQ(arg->second, TEST_CONTENT);

    LT_CHECK_EQ(files.size(), 0);

    ws->stop();
    delete ws;
LT_END_AUTO_TEST(file_upload_memory_only_incl_content)

LT_BEGIN_AUTO_TEST(file_upload_suite, file_upload_memory_only_excl_content)
    stringstream content;
    std::map<std::string, std::string, httpserver::http::arg_comparator> args;
    std::map<std::string, std::map<std::string, httpserver::http::file_info>> files;
    string upload_directory = ".";
    webserver* ws;

    ws = new webserver(create_webserver(8080)
                       .no_put_processed_data_to_content()
                       .file_upload_target(httpserver::FILE_UPLOAD_MEMORY_ONLY));
    ws->start(false);
    LT_CHECK_EQ(ws->is_running(), true);

    print_file_upload_resource resource(&args, &files, &content);
    ws->register_resource("upload", &resource);

    CURLcode res = send_file_to_webserver();
    LT_ASSERT_EQ(res, 0);

    string actual_content = content.str();
    LT_CHECK_EQ(actual_content.size(), 0);

    LT_CHECK_EQ(args.size(), 1);
    std::map<std::string, std::string, httpserver::http::arg_comparator>::iterator arg = args.begin();
    LT_CHECK_EQ(arg->first, TEST_KEY);
    LT_CHECK_EQ(arg->second, TEST_CONTENT);

    LT_CHECK_EQ(files.size(), 0);

    ws->stop();
    delete ws;
LT_END_AUTO_TEST(file_upload_memory_only_excl_content)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
