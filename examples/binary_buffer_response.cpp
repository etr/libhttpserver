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

// This example demonstrates how to serve binary data (e.g., images) directly
// from an in-memory buffer using string_response. Despite its name,
// string_response works with arbitrary binary content because std::string can
// hold any bytes, including null characters.
//
// This is useful when you generate or receive binary data at runtime (e.g.,
// from a camera, an image library, or a database) and want to serve it over
// HTTP without writing it to disk first.
//
// To test:
//   curl -o output.png http://localhost:8080/image

#include <memory>
#include <string>
#include <utility>

#include <httpserver.hpp>

// Generate a minimal valid 1x1 red PNG image in memory.
// In a real application, this could come from a camera capture, image
// processing library, database blob, etc.
static std::string generate_png_data() {
    // Minimal 1x1 red pixel PNG (68 bytes)
    static const unsigned char png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,  // PNG signature
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,  // IHDR chunk
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,  // 1x1
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,  // 8-bit RGB
        0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41,  // IDAT chunk
        0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0x00,  // compressed data
        0x00, 0x00, 0x03, 0x00, 0x01, 0x36, 0x28, 0x19,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,  // IEND chunk
        0x44, 0xae, 0x42, 0x60, 0x82
    };

    return std::string(reinterpret_cast<const char*>(png), sizeof(png));
}

class image_resource : public httpserver::http_resource {
 public:
    std::shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request&) {
        // Build binary content as a std::string. The string can contain any
        // bytes — it is not limited to printable characters or null-terminated
        // C strings. The size is tracked internally by std::string::size().
        std::string image_data = generate_png_data();

        // Use string_response with the appropriate content type. The response
        // will send the exact bytes contained in the string.
        return std::make_shared<httpserver::http_response>(httpserver::http_response::string(std::move(image_data), "image/png"));
    }
};

int main() {
    httpserver::webserver ws = httpserver::create_webserver(8080);

    image_resource ir;
    ws.register_resource("/image", &ir);
    ws.start(true);

    return 0;
}
