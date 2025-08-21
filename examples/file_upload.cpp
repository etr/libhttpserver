/*
     This file is part of libhttpserver
     Copyright (C) 2011, 2012, 2013, 2014, 2015 Sebastiano Merlino

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

#include <iostream>
#include <httpserver.hpp>

class file_upload_resource : public httpserver::http_resource {
 public:
     std::shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request&) {
         std::string get_response = "<html>\n";
         get_response += "  <body>\n";
         get_response += "    <form method=\"POST\" enctype=\"multipart/form-data\">\n";
         get_response += "      <h1>Upload 1 (key is 'files', multiple files can be selected)</h1><br>\n";
         get_response += "      <input type=\"file\" name=\"files\" multiple>\n";
         get_response += "      <br><br>\n";
         get_response += "      <h1>Upload 2 (key is 'files2', multiple files can be selected)</h1><br>\n";
         get_response += "      <input type=\"file\" name=\"files2\" multiple><br><br>\n";
         get_response += "      <input type=\"submit\" value=\"Upload\">\n";
         get_response += "    </form>\n";
         get_response += "  </body>\n";
         get_response += "</html>\n";

         return std::shared_ptr<httpserver::http_response>(new httpserver::string_response(get_response, 200, "text/html"));
     }

     std::shared_ptr<httpserver::http_response> render_POST(const httpserver::http_request& req) {
        std::string post_response = "<html>\n";
        post_response += "<head>\n";
        post_response += "  <style>\n";
        post_response += "    table, th, td {\n";
        post_response += "      border: 1px solid black;\n";
        post_response += "      border-collapse: collapse;\n";
        post_response += "    }\n";
        post_response += "  </style>\n";
        post_response += "</head>\n";
        post_response += "<body>\n";
        post_response += "  Uploaded files:\n";
        post_response += "  <br><br>\n";
        post_response += "  <table>\n";
        post_response += "    <tr>\n";
        post_response += "      <th>Key</th>\n";
        post_response += "      <th>Uploaded filename</th>\n";
        post_response += "      <th>File system path</th>\n";
        post_response += "      <th>File size</th>\n";
        post_response += "      <th>Content type</th>\n";
        post_response += "      <th>Transfer encoding</th>\n";
        post_response += "    </tr>\n";

        for (auto &file_key : req.get_files()) {
            for (auto &files : file_key.second) {
                post_response += "    <tr><td>";
                post_response += file_key.first;
                post_response += "</td><td>";
                post_response += files.first;
                post_response += "</td><td>";
                post_response += files.second.get_file_system_file_name();
                post_response += "</td><td>";
                post_response += std::to_string(files.second.get_file_size());
                post_response += "</td><td>";
                post_response += files.second.get_content_type();
                post_response += "</td><td>";
                post_response += files.second.get_transfer_encoding();
                post_response += "</td></tr>\n";
            }
        }

        post_response += "  </table><br><br>\n";
        post_response += "  <a href=\"/\">back</a>\n";
        post_response += "</body>\n</html>";
        return std::shared_ptr<httpserver::http_response>(new httpserver::string_response(post_response, 201, "text/html"));
    }
};

int main(int argc, char** argv) {
    // this example needs a directory as parameter
    if (2 != argc) {
        std::cout << "Usage: file_upload <upload_dir>" << std::endl;
        std::cout << std::endl;
        std::cout << "         file_upload: writeable directory where uploaded files will be stored" << std::endl;
        return -1;
    }

    std::cout << "CAUTION: this example will create files in the directory " << std::string(argv[1]) << std::endl;
    std::cout << "These files won't be deleted at termination" << std::endl;
    std::cout << "Please make sure, that the given directory exists and is writeable" << std::endl;

    httpserver::webserver ws = httpserver::create_webserver(8080)
                              .no_put_processed_data_to_content()
                              .file_upload_dir(std::string(argv[1]))
                              .generate_random_filename_on_upload()
                              .file_upload_target(httpserver::FILE_UPLOAD_DISK_ONLY);

    file_upload_resource fur;
    ws.register_resource("/", &fur);
    ws.start(true);

    return 0;
}

