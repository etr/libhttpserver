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

#include <httpserver.hpp>
#include <iostream>

using namespace httpserver;

class file_upload_resource : public httpserver::http_resource {
 public:
     const std::shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request& req) {
         std::string get_response = " \
         <html> \
           <body> \
             <form method=\"POST\" enctype=\"multipart/form-data\"> \
               <input type=\"file\" name=\"files\" multiple> \
               <br><br> \
               <input type=\"submit\" value=\"Upload\"> \
             </form> \
           </body> \
         </html>";
         return std::shared_ptr<httpserver::http_response>(new httpserver::string_response(get_response, 200, "text/html"));
     }

     const std::shared_ptr<httpserver::http_response> render_POST(const httpserver::http_request& req) {
        std::string post_response = " \
        <html> \
          <head> \
            <style> \
              table, th, td { \
                border: 1px solid black; \
                border-collapse: collapse; \
              } \
            </style> \
          </head> \
        <body> \
          Uploaded files: \
          <br><br> \
          <table> \
            <tr> \
              <th>Uploaded filename</th> \
              <th>File system path</th> \
              <th>File size</th> \
            </tr>";

        std::map<std::string, file_info_s> files = req.get_files();
        for (std::map<std::string, file_info_s>::iterator it = files.begin(); it != files.end(); it++) {
             post_response += "<tr><td>";
             post_response += it->first;
             post_response += "</td><td>";
             post_response += it->second.file_system_file_name;
             post_response += "</td><td>";
             post_response += std::to_string(it->second.file_size);
             post_response += "</td></tr>";
        }

        post_response += "</table><br><br><a href=\"/\">back</a></body></html>";
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
                              .post_upload_dir(std::string(argv[1]))
                              .generate_random_filename_on_upload()
                              .file_upload_target(FILE_UPLOAD_DISK_ONLY);

    file_upload_resource fur;
    ws.register_resource("/", &fur);
    ws.start(true);

    return 0;
}

