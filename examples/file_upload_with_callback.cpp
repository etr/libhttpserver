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

#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#include <httpserver.hpp>

class file_upload_resource : public httpserver::http_resource {
 public:
     httpserver::http_response render_get(const httpserver::http_request&) override {
         return httpserver::http_response::string(R"html(<html>
  <body>
    <h1>File Upload with Cleanup Callback Demo</h1>
    <p>Uploaded files will be moved to the permanent directory.</p>
    <form method="POST" enctype="multipart/form-data">
      <input type="file" name="file" multiple>
      <br><br>
      <input type="submit" value="Upload">
    </form>
  </body>
</html>
)html", "text/html");
     }

     httpserver::http_response render_post(const httpserver::http_request& req) override {
        std::string post_response = R"html(<html>
<body>
  <h1>Upload Complete</h1>
  <p>Files have been moved to permanent storage:</p>
  <ul>
)html";
        post_response.reserve(post_response.size() + 256);

        for (auto &file_key : req.get_files()) {
            for (auto &files : file_key.second) {
                post_response += "    <li>";
                post_response += files.first;
                post_response += " (";
                post_response += std::to_string(files.second.get_file_size());
                post_response += " bytes)</li>\n";
            }
        }

        post_response += "  </ul>\n";
        post_response += "  <a href=\"/\">Upload more</a>\n";
        post_response += "</body>\n</html>";
        return httpserver::http_response::string(post_response, "text/html").with_status(201);
    }
};

int main(int argc, char** argv) {
    if (3 != argc) {
        std::cout << "Usage: file_upload_with_callback <temp_dir> <permanent_dir>" << std::endl;
        std::cout << std::endl;
        std::cout << "  temp_dir:      directory for temporary upload storage" << std::endl;
        std::cout << "  permanent_dir: directory where files will be moved after upload" << std::endl;
        return -1;
    }

    std::string temp_dir = argv[1];
    std::string permanent_dir = argv[2];

    std::cout << "Starting file upload server on port 8080..." << std::endl;
    std::cout << "  Temporary directory: " << temp_dir << std::endl;
    std::cout << "  Permanent directory: " << permanent_dir << std::endl;
    std::cout << std::endl;
    std::cout << "Open http://localhost:8080 in your browser to upload files." << std::endl;

    httpserver::webserver ws{httpserver::create_webserver(8080)
        .file_upload_target(httpserver::FILE_UPLOAD_DISK_ONLY)
        .file_upload_dir(temp_dir)
        .generate_random_filename_on_upload()
        .file_cleanup_callback([&permanent_dir](const std::string& key,
                                                 const std::string& filename,
                                                 const httpserver::http::file_info& info) {
            (void)key;  // Unused in this example
            // Move the uploaded file to permanent storage
            std::string dest = permanent_dir + "/" + filename;
            int result = std::rename(info.get_file_system_file_name().c_str(), dest.c_str());

            if (result == 0) {
                std::cout << "Moved: " << filename << " -> " << dest << std::endl;
                return false;  // Don't delete - we moved it
            } else {
                std::cerr << "Failed to move " << filename << ", will be deleted" << std::endl;
                return true;  // Delete the temp file on failure
            }
        })};

    auto fur = std::make_shared<file_upload_resource>();
    ws.register_path("/", fur);
    ws.start(true);

    return 0;
}
